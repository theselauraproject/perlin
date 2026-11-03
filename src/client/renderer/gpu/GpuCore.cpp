#include "client/renderer/gpu/GpuCore.hpp"

#include <algorithm>
#include <unordered_set>
#include <cstdio>

namespace perlin::gpu {
    using namespace rhi;

    namespace {
        constexpr uint64_t kRingChunk = 4ull << 20;

        uint64_t AlignUp(uint64_t v, uint64_t a) {
            return a ? (v + a - 1) / a * a : v;
        }

        void Mix(size_t& h, uint64_t v) {
            h ^= static_cast<size_t>(v) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        }

        const VertexLayoutInfo kLayouts[static_cast<size_t>(VertexLayout::Count)] = {
            // terrain: position, packed colour, uv, packed light, packed normal, entity, mid uv, tangent
            {48,
             {{{0, 0, Format::RGB32Float, 0},
               {2, 0, Format::RGBA8Unorm, 12},
               {4, 0, Format::RG32Float, 16},
               {5, 0, Format::RGBA8Unorm, 24},
               {1, 0, Format::RGBA8Unorm, 28},
               {8, 0, Format::R32Float, 32},
               {9, 0, Format::RG32Float, 36},
               {10, 0, Format::R32Float, 44}}},
             8},
            {12, {{{0, 0, Format::RGB32Float, 0}}}, 1},
            {16, {{{0, 0, Format::RGB32Float, 0}, {2, 0, Format::RGBA8Unorm, 12}}}, 2},
            {20, {{{0, 0, Format::RGB32Float, 0}, {4, 0, Format::RG32Float, 12}}}, 2},
            {24,
             {{{0, 0, Format::RGB32Float, 0}, {2, 0, Format::RGBA8Unorm, 12}, {4, 0, Format::RG32Float, 16}}},
             3},
        };
    }

    const VertexLayoutInfo& LayoutInfo(VertexLayout layout) {
        return kLayouts[static_cast<size_t>(layout)];
    }

    uint32_t FormatBytes(Format format) {
        return GetFormatInfo(format).bytesPerBlock;
    }

    std::array<uint64_t, 4> PipelineKey::Pack() const {
        const auto u = [](auto v) { return static_cast<uint64_t>(static_cast<uint32_t>(v)); };
        std::array<uint64_t, 4> out{};
        out[0] = u(program) | (u(layout) << 16) | (u(cull) << 24) | (u(front) << 28) | (u(depthTest) << 32) |
                 (u(depthWrite) << 33) | (u(blend) << 34) | (u(alphaToCoverage) << 35) | (u(depthOp) << 36) |
                 (u(mask) << 40) | (u(color) << 48);
        out[1] = u(srcColor) | (u(dstColor) << 8) | (u(srcAlpha) << 16) | (u(dstAlpha) << 24) |
                 (u(depth) << 32) | (static_cast<uint64_t>(static_cast<uint16_t>(biasConstant)) << 48);
        out[2] = static_cast<uint64_t>(static_cast<uint16_t>(biasSlope)) | (u(alphaOp) << 16) |
                 (u(colorCount) << 24) | (u(color1) << 32);
        out[3] = u(color2);
        return out;
    }

    size_t PipelineKeyHash::operator()(const PipelineKey& k) const {
        size_t h = 0;
        for (uint64_t v : k.Pack())
            Mix(h, v);
        return h;
    }

    size_t TextureSetHash::operator()(const TextureSet& s) const {
        size_t h = 0;
        for (size_t i = 0; i < kTextureSlots; i++) {
            Mix(h, s.texture[i].value);
            Mix(h, static_cast<uint64_t>(s.sampler[i]));
        }
        return h;
    }

    bool Arena::Create(Device& device, uint32_t elements, uint32_t bytes, BufferUsage usage,
                       const char* name) {
        BufferDesc bd;
        bd.size = static_cast<uint64_t>(elements) * bytes;
        bd.usage = usage | BufferUsage::CopyDst | BufferUsage::CopySrc;
        bd.memory = MemoryClass::GpuOnly;
        bd.name = name;
        buffer = device.CreateBuffer(bd);
        if (!buffer)
            return false;
        this->usage = usage;
        this->name = name;
        elementBytes = bytes;
        state = Has(usage, BufferUsage::Index) ? State::IndexBuffer : State::VertexBuffer;
        ranges.Reset(elements);
        return true;
    }

    bool Arena::Rebuild(Device& device) {
        BufferDesc bd;
        bd.size = Bytes();
        bd.usage = usage | BufferUsage::CopyDst | BufferUsage::CopySrc;
        bd.memory = MemoryClass::GpuOnly;
        bd.name = name;
        buffer = device.CreateBuffer(bd);
        return static_cast<bool>(buffer);
    }

    void Arena::Destroy(Device& device) {
        if (buffer)
            device.Destroy(buffer);
        buffer = {};
        ranges.Reset(0);
    }

    Gpu::~Gpu() {
        Shutdown();
    }

    bool Gpu::Init(const GpuSettings& s, std::span<const uint8_t> shaderPack, std::string* error) {
        const auto fail = [&](const std::string& why) {
            if (error)
                *error = why;
            return false;
        };
        settings = s;
        InstanceOptions io;
        io.validation = s.validation;
        io.hooks = s.hooks;
        instance = CreateInstance(s.api, io);
        if (!instance || instance->Adapters().empty())
            return fail(std::string(ApiName(s.api)) + " is not available on this machine");

        DeviceOptions dop;
        dop.adapterIndex = s.adapterIndex;
        dop.validation = s.validation;
        dop.framesInFlight = std::max(1u, s.framesInFlight);
        dop.hooks = s.hooks;
        dop.pipelineCacheData = s.pipelineCache;
        dop.asyncPipelines = !s.offscreen;
        device = instance->CreateDevice(dop);
        if (!device)
            return fail("could not create a " + std::string(ApiName(s.api)) + " device");
        settings.framesInFlight = dop.framesInFlight;

        packBytes.assign(shaderPack.begin(), shaderPack.end());
        std::string packError;
        if (!library.Load(packBytes, &packError))
            return fail("shader pack: " + packError);
        target = ShaderLibrary::TargetFor(s.api);

        const auto& programs = library.Programs();
        vertexShaders.assign(programs.size(), {});
        fragmentShaders.assign(programs.size(), {});
        for (size_t i = 0; i < programs.size(); i++) {
            uniformRangeBytes =
                std::max(uniformRangeBytes, static_cast<uint32_t>(AlignUp(programs[i].uniformBytes, 256)));
            for (ShaderStage stage : {ShaderStage::Vertex, ShaderStage::Fragment}) {
                const auto code = library.Code(programs[i], stage, target);
                if (code.empty())
                    continue;
                ShaderDesc sd;
                sd.stage = stage;
                sd.format = ShaderLibrary::FormatFor(target);
                sd.code = code;
                sd.entry = ShaderLibrary::EntryFor(stage, target);
                sd.name = programs[i].name.c_str();
                (stage == ShaderStage::Vertex ? vertexShaders : fragmentShaders)[i] =
                    device->CreateShader(sd);
            }
        }
        if (library.TextureSlots().size() > kTextureSlots)
            return fail("shader pack has more texture slots than the renderer binds");
        if (!BuildLayouts())
            return fail("could not create the bind layouts");
        BuildSamplers();

        // defaults bound wherever a draw has no texture of its own
        {
            const uint32_t pixel[2] = {0xFFFFFFFFu, 0xFFFFFFFFu};
            TextureDesc td;
            td.format = Format::RGBA8Unorm;
            td.width = td.height = 1;
            td.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
            td.name = "white";
            white2d = CreateSampled(td);
            WriteTexture(white2d, 0, 0, 0, 1, 1, pixel);
            td.dim = TextureDim::Tex2DArray;
            td.depthOrLayers = 2;
            td.name = "whiteArray";
            whiteArray = CreateSampled(td);
            WriteTexture(whiteArray, 0, 0, 0, 1, 1, pixel);
            WriteTexture(whiteArray, 1, 0, 0, 1, 1, pixel);
        }

        rings.assign(settings.framesInFlight, {});
        ringGroups.assign(settings.framesInFlight, {});
        ringCursor.assign(settings.framesInFlight, 0);

        targetWidth = std::max(1u, s.width);
        targetHeight = std::max(1u, s.height);
        if (s.offscreen) {
            targetFormat = Format::RGBA8Unorm;
            TextureDesc td;
            td.format = targetFormat;
            td.width = targetWidth;
            td.height = targetHeight;
            td.usage = TextureUsage::ColorTarget | TextureUsage::CopySrc | TextureUsage::Sampled;
            td.name = "offscreen target";
            offscreen = device->CreateTexture(td);
        } else {
            SwapchainDesc sc;
            sc.window = s.window;
            sc.width = targetWidth;
            sc.height = targetHeight;
            sc.imageCount = std::max(2u, settings.framesInFlight + 1);
            sc.present = s.present;
            sc.hdr = s.hdr;
            sc.allowSampling = true;
            swapchain = device->CreateSwapchain(sc);
            if (!swapchain)
                return fail("could not create the swapchain");
            targetFormat = swapchain->GetFormat();
        }
        return true;
    }

    void Gpu::Shutdown() {
        if (!device)
            return;
        device->WaitIdle();
        ClearPipelines();
        for (auto& [set, group] : textureGroups)
            device->Destroy(group);
        textureGroups.clear();
        ReleaseRings();
        for (SamplerHandle& s : samplers)
            if (s)
                device->Destroy(s);
        samplers = {};
        for (auto* list : {&vertexShaders, &fragmentShaders}) {
            for (ShaderHandle h : *list)
                if (h)
                    device->Destroy(h);
            list->clear();
        }
        for (TextureHandle* t : {&white2d, &whiteArray, &offscreen})
            if (*t) {
                device->Destroy(*t);
                *t = {};
            }
        if (pipelineLayout)
            device->Destroy(pipelineLayout);
        if (uniformLayout)
            device->Destroy(uniformLayout);
        if (textureLayout)
            device->Destroy(textureLayout);
        pipelineLayout = {};
        uniformLayout = textureLayout = {};
        swapchain.reset();
        device->WaitIdle();
        device.reset();
        instance.reset();
        textureStates.clear();
        bufferStates.clear();
        pendingBytes.clear();
        bufferWrites.clear();
        imageWrites.clear();
    }

    bool Gpu::BuildLayouts() {
        std::vector<BindLayoutEntry> uniformEntries{
            {0, BindingType::UniformBufferDynamic, ShaderStage::All, 1, TextureDim::Tex2D}};
        std::vector<BindLayoutEntry> textureEntries;
        for (const TextureSlot& s : library.TextureSlots()) {
            textureEntries.push_back({s.slot * 2, BindingType::SampledTexture,
                                      ShaderStage::Fragment | ShaderStage::Vertex, 1,
                                      s.array ? TextureDim::Tex2DArray : TextureDim::Tex2D, !s.unfilterable});
            textureEntries.push_back({s.slot * 2 + 1, BindingType::Sampler,
                                      ShaderStage::Fragment | ShaderStage::Vertex, 1, TextureDim::Tex2D,
                                      !s.unfilterable});
        }
        BindLayoutDesc uld;
        uld.entries = uniformEntries;
        uld.name = "uniforms";
        BindLayoutDesc tld;
        tld.entries = textureEntries;
        tld.name = "textures";
        uniformLayout = device->CreateBindLayout(uld);
        textureLayout = device->CreateBindLayout(tld);
        PipelineLayoutDesc pld;
        pld.sets[0] = uniformLayout;
        pld.sets[1] = textureLayout;
        pld.name = "standard";
        pipelineLayout = device->CreatePipelineLayout(pld);
        return uniformLayout && textureLayout && pipelineLayout;
    }

    void Gpu::BuildSamplers() {
        const auto make = [&](Filter f, AddressMode a, bool mips) {
            SamplerDesc sd;
            sd.min = sd.mag = f;
            sd.mip = mips ? Filter::Linear : Filter::Nearest;
            sd.u = sd.v = sd.w = a;
            sd.maxLod = mips ? 1000.0f : 0.0f;
            return device->CreateSampler(sd);
        };
        samplers[static_cast<size_t>(SamplerKind::PointClamp)] =
            make(Filter::Nearest, AddressMode::Clamp, false);
        samplers[static_cast<size_t>(SamplerKind::PointRepeat)] =
            make(Filter::Nearest, AddressMode::Repeat, false);
        samplers[static_cast<size_t>(SamplerKind::LinearClamp)] =
            make(Filter::Linear, AddressMode::Clamp, false);
        samplers[static_cast<size_t>(SamplerKind::LinearRepeat)] =
            make(Filter::Linear, AddressMode::Repeat, false);
        samplers[static_cast<size_t>(SamplerKind::LinearMipRepeat)] =
            make(Filter::Linear, AddressMode::Repeat, true);
        {
            SamplerDesc sd;
            sd.min = sd.mag = Filter::Nearest;
            sd.mip = Filter::Linear;
            sd.u = sd.v = sd.w = AddressMode::Clamp;
            samplers[static_cast<size_t>(SamplerKind::PointMipClamp)] = device->CreateSampler(sd);
        }
    }

    int Gpu::Program(std::string_view name) const {
        const auto& programs = library.Programs();
        for (size_t i = 0; i < programs.size(); i++)
            if (programs[i].name == name)
                return static_cast<int>(i);
        return -1;
    }

    std::vector<PipelineKey> Gpu::PipelineKeys() const {
        std::vector<PipelineKey> keys;
        keys.reserve(pipelines.size());
        for (const auto& [key, handle] : pipelines)
            if (handle.value != 0)
                keys.push_back(key);
        return keys;
    }

    bool Gpu::PipelineReady(const PipelineKey& key) {
        return Pipeline(key).value != 0;
    }

    PipelineHandle Gpu::Pipeline(const PipelineKey& key) {
        if (const auto it = pipelines.find(key); it != pipelines.end())
            return it->second;
        PipelineHandle handle;
        const size_t program = key.program;
        if (program < vertexShaders.size() && vertexShaders[program] && fragmentShaders[program]) {
            GraphicsPipelineDesc gd;
            gd.vertex = vertexShaders[program];
            gd.fragment = fragmentShaders[program];
            gd.layout = pipelineLayout;
            const VertexLayoutInfo& layout = LayoutInfo(key.layout);
            gd.streams[0] = {layout.stride, VertexStep::Vertex};
            gd.streamCount = 1;
            for (uint32_t i = 0; i < layout.attributeCount; i++)
                gd.attributes[i] = layout.attributes[i];
            gd.attributeCount = layout.attributeCount;
            gd.raster.cull = key.cull;
            gd.raster.frontFace = key.front;
            gd.raster.depthBiasConstant = key.biasConstant / 100.0f;
            gd.raster.depthBiasSlope = key.biasSlope / 100.0f;
            gd.depthStencil.depthTest = key.depthTest;
            gd.depthStencil.depthWrite = key.depthWrite;
            gd.depthStencil.depthCompare = key.depthOp;
            gd.blend.alphaToCoverage = key.alphaToCoverage;
            BlendTarget& bt = gd.blend.targets[0];
            bt.enable = key.blend;
            bt.srcColor = key.srcColor;
            bt.dstColor = key.dstColor;
            bt.srcAlpha = key.srcAlpha;
            bt.dstAlpha = key.dstAlpha;
            bt.alphaOp = key.alphaOp;
            bt.writeMask = key.mask;
            if (key.color != Format::Unknown) {
                gd.colorFormats[0] = key.color;
                gd.colorCount = 1;
            }
            // extra MRT attachments take no blending: they carry data, not color
            if (key.colorCount > 1 && key.color1 != Format::Unknown) {
                gd.colorFormats[1] = key.color1;
                gd.colorCount = 2;
            }
            if (key.colorCount > 2 && key.color2 != Format::Unknown) {
                gd.colorFormats[2] = key.color2;
                gd.colorCount = 3;
            }
            if (library.Programs()[program].name == "java_gbuffers_water")
                gd.blend.targets[1] = bt;
            gd.depthFormat = key.depth;
            gd.name = library.Programs()[program].name.c_str();
            // driver-side compiler crashes give no callback: name the program first
            if (std::getenv("PERLIN_JAVA_DBG") && !std::strncmp(gd.name, "java_", 5))
                std::fprintf(stderr, "[gpu] pipeline %s colors=%u\n", gd.name, gd.colorCount);
            handle = device->CreateGraphicsPipeline(gd);
        }
        pipelines.emplace(key, handle);
        return handle;
    }

    void Gpu::ClearPipelines() {
        if (!device)
            return;
        for (auto& [key, handle] : pipelines)
            if (handle)
                device->Destroy(handle);
        pipelines.clear();
    }

    bool Gpu::CreateRing(uint32_t slot, uint64_t minimum) {
        BufferDesc bd;
        bd.size = std::max(kRingChunk, AlignUp(minimum, 65536)) + uniformRangeBytes;
        bd.usage = BufferUsage::Vertex | BufferUsage::Index | BufferUsage::Uniform | BufferUsage::CopySrc;
        if (Caps().features.multiDrawIndirect)
            bd.usage = bd.usage | BufferUsage::Indirect;
        bd.memory = MemoryClass::Upload;
        bd.name = "frame ring";
        RingChunk ring;
        ring.buffer = device->CreateBuffer(bd);
        if (!ring.buffer)
            return false;
        ring.data = device->Map(ring.buffer);
        ring.size = bd.size - uniformRangeBytes;
        BindGroupEntry entry;
        entry.binding = 0;
        entry.buffer = {ring.buffer, 0, uniformRangeBytes};
        BindGroupDesc gd;
        gd.layout = uniformLayout;
        gd.entries = std::span<const BindGroupEntry>(&entry, 1);
        gd.name = "uniform ring";
        rings[slot].push_back(ring);
        ringGroups[slot].push_back(device->CreateBindGroup(gd));
        return ring.data != nullptr;
    }

    void Gpu::ReleaseRings() {
        for (size_t s = 0; s < rings.size(); s++) {
            for (RingChunk& r : rings[s])
                device->Destroy(r.buffer);
            for (BindGroupHandle g : ringGroups[s])
                device->Destroy(g);
        }
        rings.clear();
        ringGroups.clear();
    }

    RingSlice Gpu::Ring(uint64_t bytes, uint64_t align) {
        const uint32_t slot = FrameSlot();
        auto& chunks = rings[slot];
        for (uint32_t i = ringCursor[slot]; i < chunks.size(); i++) {
            RingChunk& r = chunks[i];
            const uint64_t at = AlignUp(r.used, align);
            if (at + bytes <= r.size) {
                r.used = at + bytes;
                ringCursor[slot] = i;
                return {r.buffer, i, at, r.data + at};
            }
        }
        if (!CreateRing(slot, bytes + align))
            return {};
        RingChunk& r = chunks.back();
        r.used = bytes;
        ringCursor[slot] = static_cast<uint32_t>(chunks.size() - 1);
        return {r.buffer, ringCursor[slot], 0, r.data};
    }

    void Gpu::FlushRings() {
        if (rings.empty())
            return;
        for (RingChunk& r : rings[FrameSlot()])
            if (r.used)
                device->Flush(r.buffer, 0, r.used);
    }

    void Gpu::BeginFrame() {
        device->BeginFrame();
        const uint32_t slot = FrameSlot();
        for (RingChunk& r : rings[slot])
            r.used = 0;
        ringCursor[slot] = 0;
    }

    bool Gpu::AcquireImage(Swapchain::Image& out) {
        if (offscreen) {
            out.texture = offscreen;
            out.index = 0;
            return true;
        }
        if (!swapchain) {
            acquireFailures++;
            return false;
        }
        if (!swapchain->Acquire(out)) {
            acquireFailures++;
            return false;
        }
        acquireFailures = 0;
        // the chain may have settled on another size than asked for (clamping, a window that moved on): draw to what it really is
        if (swapchain->Width() && swapchain->Height()) {
            targetWidth = swapchain->Width();
            targetHeight = swapchain->Height();
        }
        return true;
    }

    void Gpu::PresentImage() {
        if (swapchain)
            swapchain->Present();
    }

    bool Gpu::Resize(uint32_t w, uint32_t h) {
        if (w == 0 || h == 0)
            return false;
        if (w == targetWidth && h == targetHeight)
            return false;
        targetWidth = w;
        targetHeight = h;
        if (offscreen) {
            device->WaitIdle();
            device->Destroy(offscreen);
            TextureDesc td;
            td.format = targetFormat;
            td.width = w;
            td.height = h;
            td.usage = TextureUsage::ColorTarget | TextureUsage::CopySrc | TextureUsage::Sampled;
            td.name = "offscreen target";
            offscreen = device->CreateTexture(td);
            textureStates.erase(offscreen.value);
            return true;
        }
        if (!swapchain)
            return false;
        const bool ok = swapchain->Resize(w, h);
        if (swapchain->Width() && swapchain->Height()) {
            targetWidth = swapchain->Width();
            targetHeight = swapchain->Height();
        }
        return ok;
    }

    bool Gpu::RecreateSurface(uint32_t w, uint32_t h) {
        if (offscreen || w == 0 || h == 0)
            return false;
        device->WaitIdle();
        swapchain.reset();
        targetWidth = w;
        targetHeight = h;
        SwapchainDesc sc;
        sc.window = settings.window;
        sc.width = w;
        sc.height = h;
        sc.imageCount = std::max(2u, settings.framesInFlight + 1);
        sc.present = settings.present;
        sc.hdr = settings.hdr;
        sc.allowSampling = true;
        swapchain = device->CreateSwapchain(sc);
        if (swapchain)
            targetFormat = swapchain->GetFormat();
        return static_cast<bool>(swapchain);
    }

    void Gpu::SetPresentMode(PresentMode mode) {
        settings.present = mode;
        if (swapchain)
            swapchain->SetPresentMode(mode);
    }

    BindGroupHandle Gpu::TextureGroup(const TextureSet& set) {
        if (const auto it = textureGroups.find(set); it != textureGroups.end())
            return it->second;
        std::vector<BindGroupEntry> entries;
        for (const TextureSlot& slot : library.TextureSlots()) {
            TextureHandle tex = set.texture[slot.slot];
            if (!tex)
                tex = slot.array ? whiteArray : white2d;
            BindGroupEntry t;
            t.binding = slot.slot * 2;
            t.texture.texture = tex;
            t.texture.viewDim = slot.array ? TextureDim::Tex2DArray : TextureDim::Tex2D;
            entries.push_back(t);
            BindGroupEntry s;
            s.binding = slot.slot * 2 + 1;
            s.sampler = samplers[static_cast<size_t>(set.sampler[slot.slot])];
            entries.push_back(s);
        }
        BindGroupDesc gd;
        gd.layout = textureLayout;
        gd.entries = entries;
        gd.name = "textures";
        const BindGroupHandle group = device->CreateBindGroup(gd);
        textureGroups.emplace(set, group);
        return group;
    }

    void Gpu::ForgetTexture(TextureHandle texture) {
        for (auto it = textureGroups.begin(); it != textureGroups.end();) {
            bool uses = false;
            for (const TextureHandle& t : it->first.texture)
                uses |= t == texture;
            if (uses) {
                device->Destroy(it->second);
                it = textureGroups.erase(it);
            } else {
                ++it;
            }
        }
        textureStates.erase(texture.value);
    }

    TextureHandle Gpu::CreateSampled(TextureDesc desc) {
        desc.usage = desc.usage | TextureUsage::Sampled | TextureUsage::CopyDst;
        const TextureHandle h = device->CreateTexture(desc);
        if (h)
            textureStates[h.value] = State::Undefined;
        return h;
    }

    void Gpu::DestroyTexture(TextureHandle texture) {
        if (!texture)
            return;
        // pending writes to a dying texture are dropped
        imageWrites.erase(std::remove_if(imageWrites.begin(), imageWrites.end(),
                                         [&](const ImageWrite& w) { return w.texture == texture; }),
                          imageWrites.end());
        ForgetTexture(texture);
        device->Destroy(texture);
    }

    void Gpu::WriteBuffer(BufferHandle buffer, State resting, uint64_t byteOffset, const void* data,
                          uint64_t bytes) {
        if (!bytes || !buffer)
            return;
        const uint64_t at = AlignUp(pendingBytes.size(), 16);
        pendingBytes.resize(at + bytes);
        std::memcpy(pendingBytes.data() + at, data, bytes);
        bufferWrites.push_back({buffer, resting, byteOffset, bytes, at});
    }

    BufferHandle Gpu::CreateStatic(const void* data, uint64_t bytes, BufferUsage usage, const char* name) {
        BufferDesc bd;
        bd.size = AlignUp(bytes, 4);
        bd.usage = usage | BufferUsage::CopyDst;
        bd.memory = MemoryClass::GpuOnly;
        bd.name = name;
        const BufferHandle h = device->CreateBuffer(bd);
        WriteBuffer(h, Has(usage, BufferUsage::Index) ? State::IndexBuffer : State::VertexBuffer, 0, data,
                    bytes);
        return h;
    }

    void Gpu::WriteTexture(TextureHandle texture, uint32_t layer, uint32_t x, uint32_t y, uint32_t width,
                           uint32_t height, const void* data, uint32_t rowBytes, uint32_t mip) {
        const TextureDesc* td = device->GetDesc(texture);
        if (!td || !width || !height)
            return;
        const uint32_t packed = width * FormatBytes(td->format);
        const uint32_t srcPitch = rowBytes ? rowBytes : packed;
        const uint32_t pitch = static_cast<uint32_t>(AlignUp(packed, Caps().limits.copyRowPitchAlignment));
        const uint64_t at =
            AlignUp(pendingBytes.size(), std::max<uint64_t>(16, Caps().limits.copyOffsetAlignment));
        pendingBytes.resize(at + static_cast<uint64_t>(pitch) * height);
        for (uint32_t row = 0; row < height; row++)
            std::memcpy(pendingBytes.data() + at + static_cast<uint64_t>(row) * pitch,
                        static_cast<const uint8_t*>(data) + static_cast<uint64_t>(row) * srcPitch, packed);
        imageWrites.push_back({texture, mip, layer, x, y, width, height, at, pitch});
    }

    State Gpu::StateOf(TextureHandle texture) const {
        const auto it = textureStates.find(texture.value);
        return it == textureStates.end() ? State::Undefined : it->second;
    }

    void Gpu::SetState(TextureHandle texture, State state) {
        textureStates[texture.value] = state;
    }

    void Gpu::Transition(CommandList& list, TextureHandle texture, State to) {
        const State from = StateOf(texture);
        if (from == to)
            return;
        TextureBarrier b;
        b.texture = texture;
        b.before = from;
        b.after = to;
        list.Barrier(b);
        textureStates[texture.value] = to;
    }

    void Gpu::Flush(CommandList& list) {
        if (bufferWrites.empty() && imageWrites.empty()) {
            pendingBytes.clear();
            return;
        }
        const RingSlice staging =
            Ring(pendingBytes.size(), std::max<uint64_t>(256, Caps().limits.copyOffsetAlignment));
        if (!staging.data) {
            bufferWrites.clear();
            imageWrites.clear();
            pendingBytes.clear();
            return;
        }
        std::memcpy(staging.data, pendingBytes.data(), pendingBytes.size());

        std::vector<BufferBarrier> toCopy, toUse;
        std::vector<TextureBarrier> texToCopy, texToUse;
        bufferWrites.erase(
            std::remove_if(bufferWrites.begin(), bufferWrites.end(),
                           [&](const BufferWrite& w) { return device->GetDesc(w.buffer) == nullptr; }),
            bufferWrites.end());
        imageWrites.erase(
            std::remove_if(imageWrites.begin(), imageWrites.end(),
                           [&](const ImageWrite& w) { return device->GetDesc(w.texture) == nullptr; }),
            imageWrites.end());
        std::unordered_set<uint32_t> seenBuffers;
        seenBuffers.reserve(16);
        for (const BufferWrite& w : bufferWrites) {
            // one barrier pair per buffer however many ranges it has (a world load writes thousands of ranges)
            if (!seenBuffers.insert(w.buffer.value).second)
                continue;
            const auto known = bufferStates.find(w.buffer.value);
            toCopy.push_back(
                {w.buffer, known == bufferStates.end() ? State::Undefined : known->second, State::CopyDst});
            toUse.push_back({w.buffer, State::CopyDst, w.state});
            bufferStates[w.buffer.value] = w.state;
        }
        std::vector<TextureHandle> touched;
        std::unordered_set<uint32_t> seenTextures;
        for (const ImageWrite& w : imageWrites) {
            if (!seenTextures.insert(w.texture.value).second)
                continue;
            touched.push_back(w.texture);
            const State from = StateOf(w.texture);
            TextureBarrier b;
            b.texture = w.texture;
            b.before = from;
            b.after = State::CopyDst;
            texToCopy.push_back(b);
            b.before = State::CopyDst;
            b.after = State::ShaderRead;
            texToUse.push_back(b);
        }
        list.Barrier(texToCopy, toCopy);
        for (const BufferWrite& w : bufferWrites)
            list.CopyBuffer(w.buffer, w.offset, staging.buffer, staging.offset + w.source, w.bytes);
        for (const ImageWrite& w : imageWrites) {
            BufferTextureCopy c;
            c.bufferOffset = staging.offset + w.source;
            c.bufferRowPitch = w.rowBytes;
            c.mip = w.mip;
            c.layer = w.layer;
            c.x = w.x;
            c.y = w.y;
            c.width = w.width;
            c.height = w.height;
            list.CopyBufferToTexture(w.texture, staging.buffer, std::span<const BufferTextureCopy>(&c, 1));
        }
        list.Barrier(texToUse, toUse);
        for (const TextureHandle& t : touched)
            textureStates[t.value] = State::ShaderRead;
        bufferWrites.clear();
        imageWrites.clear();
        pendingBytes.clear();
        // a world load or atlas upload can leave this holding hundreds of megabytes it will not need again
        if (pendingBytes.capacity() > (48ull << 20))
            std::vector<uint8_t>().swap(pendingBytes);
    }
}

namespace perlin::gpu {
    std::vector<std::vector<uint8_t>> Gpu::ReadBuffers(std::span<const BufferSource> sources) {
        std::vector<std::vector<uint8_t>> out(sources.size());
        uint64_t total = 0;
        std::vector<uint64_t> at(sources.size());
        for (size_t i = 0; i < sources.size(); i++) {
            at[i] = total;
            if (sources[i].buffer && device->GetDesc(sources[i].buffer))
                total += (sources[i].bytes + 255) & ~255ull;
        }
        if (!total)
            return out;
        BufferDesc bd;
        bd.size = total;
        bd.usage = BufferUsage::CopyDst;
        bd.memory = MemoryClass::Readback;
        bd.name = "snapshot";
        const BufferHandle readback = device->CreateBuffer(bd);
        if (!readback)
            return out;
        CommandList* list = device->AcquireCommandList(QueueType::Graphics);
        Flush(*list);
        std::vector<BufferBarrier> toRead, back;
        for (const BufferSource& s : sources) {
            if (!s.buffer || !device->GetDesc(s.buffer))
                continue;
            const auto known = bufferStates.find(s.buffer.value);
            const State from = known == bufferStates.end() ? s.resting : known->second;
            toRead.push_back({s.buffer, from, State::CopySrc});
            back.push_back({s.buffer, State::CopySrc, from});
        }
        list->Barrier({}, toRead);
        for (size_t i = 0; i < sources.size(); i++)
            if (sources[i].buffer && device->GetDesc(sources[i].buffer))
                list->CopyBuffer(readback, at[i], sources[i].buffer, 0, sources[i].bytes);
        list->Barrier({}, back);
        FlushRings();
        device->Submit(QueueType::Graphics, list);
        device->WaitIdle();
        device->Invalidate(readback, 0, total);
        if (const uint8_t* mapped = device->Map(readback))
            for (size_t i = 0; i < sources.size(); i++)
                if (sources[i].buffer && sources[i].bytes && at[i] + sources[i].bytes <= total)
                    out[i].assign(mapped + at[i], mapped + at[i] + sources[i].bytes);
        device->Destroy(readback);
        return out;
    }
}
