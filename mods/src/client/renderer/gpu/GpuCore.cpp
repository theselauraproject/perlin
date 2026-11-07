#include "client/renderer/gpu/GpuCore.hpp"
#include "util/GraphicsDebugger.hpp"

#include <algorithm>
#include <unordered_set>
#include <cstdio>

namespace perlin::gpu {
    using namespace conduit;

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
             {{{0, 0, format::rgb32_float, 0},
               {2, 0, format::rgba8_unorm, 12},
               {4, 0, format::rg32_float, 16},
               {5, 0, format::rgba8_unorm, 24},
               {1, 0, format::rgba8_unorm, 28},
               {8, 0, format::r32_float, 32},
               {9, 0, format::rg32_float, 36},
               {10, 0, format::r32_float, 44}}},
             8},
            {12, {{{0, 0, format::rgb32_float, 0}}}, 1},
            {16, {{{0, 0, format::rgb32_float, 0}, {2, 0, format::rgba8_unorm, 12}}}, 2},
            {20, {{{0, 0, format::rgb32_float, 0}, {4, 0, format::rg32_float, 12}}}, 2},
            {24,
             {{{0, 0, format::rgb32_float, 0}, {2, 0, format::rgba8_unorm, 12}, {4, 0, format::rg32_float, 16}}},
             3},
        };
    }

    const VertexLayoutInfo& LayoutInfo(VertexLayout layout) {
        return kLayouts[static_cast<size_t>(layout)];
    }

    uint32_t FormatBytes(conduit::format format) {
        return get_format_info(format).bytes_per_block;
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

    bool Arena::Create(conduit::device& device, uint32_t elements, uint32_t bytes, buffer_usage usage,
                       const char* name) {
        buffer_desc bd;
        bd.size = static_cast<uint64_t>(elements) * bytes;
        bd.usage = usage | buffer_usage::copy_dst | buffer_usage::copy_src;
        bd.memory = memory_class::gpu_only;
        bd.name = name;
        buffer = device.create_buffer(bd);
        if (!buffer)
            return false;
        this->usage = usage;
        this->name = name;
        elementBytes = bytes;
        state = has(usage, buffer_usage::index) ? state::index_buffer : state::vertex_buffer;
        ranges.Reset(elements);
        return true;
    }

    bool Arena::Rebuild(conduit::device& device) {
        buffer_desc bd;
        bd.size = Bytes();
        bd.usage = usage | buffer_usage::copy_dst | buffer_usage::copy_src;
        bd.memory = memory_class::gpu_only;
        bd.name = name;
        buffer = device.create_buffer(bd);
        return static_cast<bool>(buffer);
    }

    void Arena::Destroy(conduit::device& device) {
        if (buffer)
            device.destroy(buffer);
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
        instance_options io;
        io.app_name = "Perlin";
        io.validation = s.validation;
        io.hooks = s.hooks;
        instance = create_instance(s.api, io);
        if (!instance || instance->adapters().empty())
            return fail(std::string(api_name(s.api)) + " is not available on this machine");

        device_options dop;
        dop.adapter_index = s.adapterIndex;
        dop.validation = s.validation;
        dop.frames_in_flight = std::max(1u, s.framesInFlight);
        dop.hooks = s.hooks;
        dop.pipeline_cache_data = s.pipelineCache;
        dop.async_pipelines = !s.offscreen && !gfxdebug::Attached();
        device = instance->create_device(dop);
        if (!device)
            return fail("could not create a " + std::string(api_name(s.api)) + " device");
        settings.framesInFlight = dop.frames_in_flight;

        packBytes.assign(shaderPack.begin(), shaderPack.end());
        std::string packError;
        if (!library.load(packBytes, &packError))
            return fail("shader pack: " + packError);
        target = shader_library::target_for(s.api);

        const auto& programs = library.programs();
        vertexShaders.assign(programs.size(), {});
        fragmentShaders.assign(programs.size(), {});
        for (size_t i = 0; i < programs.size(); i++) {
            uniformRangeBytes =
                std::max(uniformRangeBytes, static_cast<uint32_t>(AlignUp(programs[i].uniform_bytes, 256)));
            for (shader_stage stage : {shader_stage::vertex, shader_stage::fragment}) {
                const auto code = library.code(programs[i], stage, target);
                if (code.empty())
                    continue;
                shader_desc sd;
                sd.stage = stage;
                sd.format = shader_library::format_for(target);
                sd.code = code;
                sd.entry = shader_library::entry_for(stage, target);
                sd.name = programs[i].name.c_str();
                (stage == shader_stage::vertex ? vertexShaders : fragmentShaders)[i] =
                    device->create_shader(sd);
            }
        }
        if (library.texture_slots().size() > kTextureSlots)
            return fail("shader pack has more texture slots than the renderer binds");
        if (!BuildLayouts())
            return fail("could not create the bind layouts");
        BuildSamplers();

        // defaults bound wherever a draw has no texture of its own
        {
            const uint32_t pixel[2] = {0xFFFFFFFFu, 0xFFFFFFFFu};
            texture_desc td;
            td.format = format::rgba8_unorm;
            td.width = td.height = 1;
            td.usage = texture_usage::sampled | texture_usage::copy_dst;
            td.name = "white";
            white2d = CreateSampled(td);
            WriteTexture(white2d, 0, 0, 0, 1, 1, pixel);
            td.dim = texture_dim::tex2d_array;
            td.depth_or_layers = 2;
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
            targetFormat = format::rgba8_unorm;
            texture_desc td;
            td.format = targetFormat;
            td.width = targetWidth;
            td.height = targetHeight;
            td.usage = texture_usage::color_target | texture_usage::copy_src | texture_usage::sampled;
            td.name = "offscreen target";
            offscreen = device->create_texture(td);
        } else {
            swapchain_desc sc;
            sc.window = s.window;
            sc.width = targetWidth;
            sc.height = targetHeight;
            sc.image_count = std::max(2u, settings.framesInFlight + 1);
            sc.present = s.present;
            sc.hdr = s.hdr;
            sc.allow_sampling = true;
            swapchain = device->create_swapchain(sc);
            if (!swapchain)
                return fail("could not create the swapchain");
            targetFormat = swapchain->get_format();
        }
        return true;
    }

    void Gpu::Shutdown() {
        if (!device)
            return;
        device->wait_idle();
        ClearPipelines();
        for (auto& [set, group] : textureGroups)
            device->destroy(group);
        textureGroups.clear();
        ReleaseRings();
        for (sampler_handle& s : samplers)
            if (s)
                device->destroy(s);
        samplers = {};
        for (auto* list : {&vertexShaders, &fragmentShaders}) {
            for (shader_handle h : *list)
                if (h)
                    device->destroy(h);
            list->clear();
        }
        for (texture_handle* t : {&white2d, &whiteArray, &offscreen})
            if (*t) {
                device->destroy(*t);
                *t = {};
            }
        if (pipelineLayout)
            device->destroy(pipelineLayout);
        if (uniformLayout)
            device->destroy(uniformLayout);
        if (textureLayout)
            device->destroy(textureLayout);
        pipelineLayout = {};
        uniformLayout = textureLayout = {};
        swapchain.reset();
        device->wait_idle();
        device.reset();
        instance.reset();
        textureStates.clear();
        bufferStates.clear();
        pendingBytes.clear();
        bufferWrites.clear();
        imageWrites.clear();
    }

    bool Gpu::BuildLayouts() {
        std::vector<bind_layout_entry> uniformEntries{
            {0, binding_type::uniform_buffer_dynamic, shader_stage::all, 1, texture_dim::tex2d}};
        std::vector<bind_layout_entry> textureEntries;
        for (const texture_slot& s : library.texture_slots()) {
            textureEntries.push_back({s.slot * 2, binding_type::sampled_texture,
                                      shader_stage::fragment | shader_stage::vertex, 1,
                                      s.array ? texture_dim::tex2d_array : texture_dim::tex2d, !s.unfilterable});
            textureEntries.push_back({s.slot * 2 + 1, binding_type::sampler,
                                      shader_stage::fragment | shader_stage::vertex, 1, texture_dim::tex2d,
                                      !s.unfilterable});
        }
        bind_layout_desc uld;
        uld.entries = uniformEntries;
        uld.name = "uniforms";
        bind_layout_desc tld;
        tld.entries = textureEntries;
        tld.name = "textures";
        uniformLayout = device->create_bind_layout(uld);
        textureLayout = device->create_bind_layout(tld);
        pipeline_layout_desc pld;
        pld.sets[0] = uniformLayout;
        pld.sets[1] = textureLayout;
        pld.name = "standard";
        pipelineLayout = device->create_pipeline_layout(pld);
        return uniformLayout && textureLayout && pipelineLayout;
    }

    void Gpu::BuildSamplers() {
        const auto make = [&](filter f, address_mode a, bool mips) {
            sampler_desc sd;
            sd.min = sd.mag = f;
            sd.mip = mips ? filter::linear : filter::nearest;
            sd.u = sd.v = sd.w = a;
            sd.max_lod = mips ? 1000.0f : 0.0f;
            return device->create_sampler(sd);
        };
        samplers[static_cast<size_t>(SamplerKind::PointClamp)] =
            make(filter::nearest, address_mode::clamp, false);
        samplers[static_cast<size_t>(SamplerKind::PointRepeat)] =
            make(filter::nearest, address_mode::repeat, false);
        samplers[static_cast<size_t>(SamplerKind::LinearClamp)] =
            make(filter::linear, address_mode::clamp, false);
        samplers[static_cast<size_t>(SamplerKind::LinearRepeat)] =
            make(filter::linear, address_mode::repeat, false);
        samplers[static_cast<size_t>(SamplerKind::LinearMipRepeat)] =
            make(filter::linear, address_mode::repeat, true);
        {
            sampler_desc sd;
            sd.min = sd.mag = filter::nearest;
            sd.mip = filter::linear;
            sd.u = sd.v = sd.w = address_mode::clamp;
            samplers[static_cast<size_t>(SamplerKind::PointMipClamp)] = device->create_sampler(sd);
        }
    }

    int Gpu::Program(std::string_view name) const {
        const auto& programs = library.programs();
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

    pipeline_handle Gpu::Pipeline(const PipelineKey& key) {
        if (const auto it = pipelines.find(key); it != pipelines.end())
            return it->second;
        pipeline_handle handle;
        const size_t program = key.program;
        if (program < vertexShaders.size() && vertexShaders[program] && fragmentShaders[program]) {
            graphics_pipeline_desc gd;
            gd.vertex = vertexShaders[program];
            gd.fragment = fragmentShaders[program];
            gd.layout = pipelineLayout;
            const VertexLayoutInfo& layout = LayoutInfo(key.layout);
            gd.streams[0] = {layout.stride, vertex_step::vertex};
            gd.stream_count = 1;
            for (uint32_t i = 0; i < layout.attributeCount; i++)
                gd.attributes[i] = layout.attributes[i];
            gd.attribute_count = layout.attributeCount;
            gd.raster.cull = key.cull;
            gd.raster.front_face = key.front;
            gd.raster.depth_bias_constant = key.biasConstant / 100.0f;
            gd.raster.depth_bias_slope = key.biasSlope / 100.0f;
            gd.depth_stencil.depth_test = key.depthTest;
            gd.depth_stencil.depth_write = key.depthWrite;
            gd.depth_stencil.depth_compare = key.depthOp;
            gd.blend.alpha_to_coverage = key.alphaToCoverage;
            blend_target& bt = gd.blend.targets[0];
            bt.enable = key.blend;
            bt.src_color = key.srcColor;
            bt.dst_color = key.dstColor;
            bt.src_alpha = key.srcAlpha;
            bt.dst_alpha = key.dstAlpha;
            bt.alpha_op = key.alphaOp;
            bt.write_mask = key.mask;
            if (key.color != format::unknown) {
                gd.color_formats[0] = key.color;
                gd.color_count = 1;
            }
            // extra MRT attachments take no blending: they carry data, not color
            if (key.colorCount > 1 && key.color1 != format::unknown) {
                gd.color_formats[1] = key.color1;
                gd.color_count = 2;
            }
            if (key.colorCount > 2 && key.color2 != format::unknown) {
                gd.color_formats[2] = key.color2;
                gd.color_count = 3;
            }
            if (library.programs()[program].name == "java_gbuffers_water")
                gd.blend.targets[1] = bt;
            gd.depth_format = key.depth;
            gd.name = library.programs()[program].name.c_str();
            // driver-side compiler crashes give no callback: name the program first
            if (std::getenv("PERLIN_JAVA_DBG") && !std::strncmp(gd.name, "java_", 5))
                std::fprintf(stderr, "[gpu] pipeline %s colors=%u\n", gd.name, gd.color_count);
            handle = device->create_graphics_pipeline(gd);
        }
        pipelines.emplace(key, handle);
        return handle;
    }

    void Gpu::ClearPipelines() {
        if (!device)
            return;
        for (auto& [key, handle] : pipelines)
            if (handle)
                device->destroy(handle);
        pipelines.clear();
    }

    bool Gpu::CreateRing(uint32_t slot, uint64_t minimum) {
        buffer_desc bd;
        bd.size = std::max(kRingChunk, AlignUp(minimum, 65536)) + uniformRangeBytes;
        bd.usage = buffer_usage::vertex | buffer_usage::index | buffer_usage::uniform | buffer_usage::copy_src;
        if (Caps().features.multi_draw_indirect)
            bd.usage = bd.usage | buffer_usage::indirect;
        bd.memory = memory_class::upload;
        bd.name = "frame ring";
        RingChunk ring;
        ring.buffer = device->create_buffer(bd);
        if (!ring.buffer)
            return false;
        ring.data = device->map(ring.buffer);
        ring.size = bd.size - uniformRangeBytes;
        bind_group_entry entry;
        entry.binding = 0;
        entry.buffer = {ring.buffer, 0, uniformRangeBytes};
        bind_group_desc gd;
        gd.layout = uniformLayout;
        gd.entries = std::span<const bind_group_entry>(&entry, 1);
        gd.name = "uniform ring";
        rings[slot].push_back(ring);
        ringGroups[slot].push_back(device->create_bind_group(gd));
        return ring.data != nullptr;
    }

    void Gpu::ReleaseRings() {
        for (size_t s = 0; s < rings.size(); s++) {
            for (RingChunk& r : rings[s])
                device->destroy(r.buffer);
            for (bind_group_handle g : ringGroups[s])
                device->destroy(g);
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
                device->flush(r.buffer, 0, r.used);
    }

    void Gpu::BeginFrame() {
        device->begin_frame();
        const uint32_t slot = FrameSlot();
        for (RingChunk& r : rings[slot])
            r.used = 0;
        ringCursor[slot] = 0;
    }

    bool Gpu::AcquireImage(swapchain::image& out) {
        if (offscreen) {
            out.texture = offscreen;
            out.index = 0;
            return true;
        }
        if (!swapchain) {
            acquireFailures++;
            return false;
        }
        if (!swapchain->acquire(out)) {
            acquireFailures++;
            return false;
        }
        acquireFailures = 0;
        // the chain may have settled on another size than asked for (clamping, a window that moved on): draw to what it really is
        if (swapchain->width() && swapchain->height()) {
            targetWidth = swapchain->width();
            targetHeight = swapchain->height();
        }
        return true;
    }

    void Gpu::PresentImage() {
        if (swapchain)
            swapchain->present();
    }

    bool Gpu::Resize(uint32_t w, uint32_t h) {
        if (w == 0 || h == 0)
            return false;
        if (w == targetWidth && h == targetHeight)
            return false;
        targetWidth = w;
        targetHeight = h;
        if (offscreen) {
            device->wait_idle();
            device->destroy(offscreen);
            texture_desc td;
            td.format = targetFormat;
            td.width = w;
            td.height = h;
            td.usage = texture_usage::color_target | texture_usage::copy_src | texture_usage::sampled;
            td.name = "offscreen target";
            offscreen = device->create_texture(td);
            textureStates.erase(offscreen.value);
            return true;
        }
        if (!swapchain)
            return false;
        const bool ok = swapchain->resize(w, h);
        if (swapchain->width() && swapchain->height()) {
            targetWidth = swapchain->width();
            targetHeight = swapchain->height();
        }
        return ok;
    }

    bool Gpu::RecreateSurface(uint32_t w, uint32_t h) {
        if (offscreen || w == 0 || h == 0)
            return false;
        device->wait_idle();
        swapchain.reset();
        targetWidth = w;
        targetHeight = h;
        swapchain_desc sc;
        sc.window = settings.window;
        sc.width = w;
        sc.height = h;
        sc.image_count = std::max(2u, settings.framesInFlight + 1);
        sc.present = settings.present;
        sc.hdr = settings.hdr;
        sc.allow_sampling = true;
        swapchain = device->create_swapchain(sc);
        if (swapchain)
            targetFormat = swapchain->get_format();
        return static_cast<bool>(swapchain);
    }

    void Gpu::SetPresentMode(present_mode mode) {
        settings.present = mode;
        if (swapchain)
            swapchain->set_present_mode(mode);
    }

    bind_group_handle Gpu::TextureGroup(const TextureSet& set) {
        if (const auto it = textureGroups.find(set); it != textureGroups.end())
            return it->second;
        std::vector<bind_group_entry> entries;
        for (const texture_slot& slot : library.texture_slots()) {
            texture_handle tex = set.texture[slot.slot];
            if (!tex)
                tex = slot.array ? whiteArray : white2d;
            bind_group_entry t;
            t.binding = slot.slot * 2;
            t.texture.texture = tex;
            t.texture.view_dim = slot.array ? texture_dim::tex2d_array : texture_dim::tex2d;
            entries.push_back(t);
            bind_group_entry s;
            s.binding = slot.slot * 2 + 1;
            s.sampler = samplers[static_cast<size_t>(set.sampler[slot.slot])];
            entries.push_back(s);
        }
        bind_group_desc gd;
        gd.layout = textureLayout;
        gd.entries = entries;
        gd.name = "textures";
        const bind_group_handle group = device->create_bind_group(gd);
        textureGroups.emplace(set, group);
        return group;
    }

    void Gpu::ForgetTexture(texture_handle texture) {
        for (auto it = textureGroups.begin(); it != textureGroups.end();) {
            bool uses = false;
            for (const texture_handle& t : it->first.texture)
                uses |= t == texture;
            if (uses) {
                device->destroy(it->second);
                it = textureGroups.erase(it);
            } else {
                ++it;
            }
        }
        textureStates.erase(texture.value);
    }

    texture_handle Gpu::CreateSampled(texture_desc desc) {
        desc.usage = desc.usage | texture_usage::sampled | texture_usage::copy_dst;
        const texture_handle h = device->create_texture(desc);
        if (h)
            textureStates[h.value] = state::undefined;
        return h;
    }

    void Gpu::DestroyTexture(texture_handle texture) {
        if (!texture)
            return;
        // pending writes to a dying texture are dropped
        imageWrites.erase(std::remove_if(imageWrites.begin(), imageWrites.end(),
                                         [&](const ImageWrite& w) { return w.texture == texture; }),
                          imageWrites.end());
        ForgetTexture(texture);
        device->destroy(texture);
    }

    void Gpu::WriteBuffer(buffer_handle buffer, state resting, uint64_t byteOffset, const void* data,
                          uint64_t bytes) {
        if (!bytes || !buffer)
            return;
        const uint64_t at = AlignUp(pendingBytes.size(), 16);
        pendingBytes.resize(at + bytes);
        std::memcpy(pendingBytes.data() + at, data, bytes);
        bufferWrites.push_back({buffer, resting, byteOffset, bytes, at});
    }

    buffer_handle Gpu::CreateStatic(const void* data, uint64_t bytes, buffer_usage usage, const char* name) {
        buffer_desc bd;
        bd.size = AlignUp(bytes, 4);
        bd.usage = usage | buffer_usage::copy_dst;
        bd.memory = memory_class::gpu_only;
        bd.name = name;
        const buffer_handle h = device->create_buffer(bd);
        WriteBuffer(h, has(usage, buffer_usage::index) ? state::index_buffer : state::vertex_buffer, 0, data,
                    bytes);
        return h;
    }

    void Gpu::WriteTexture(texture_handle texture, uint32_t layer, uint32_t x, uint32_t y, uint32_t width,
                           uint32_t height, const void* data, uint32_t rowBytes, uint32_t mip) {
        const texture_desc* td = device->get_desc(texture);
        if (!td || !width || !height)
            return;
        const uint32_t packed = width * FormatBytes(td->format);
        const uint32_t srcPitch = rowBytes ? rowBytes : packed;
        const uint32_t pitch = static_cast<uint32_t>(AlignUp(packed, Caps().limits.copy_row_pitch_alignment));
        const uint64_t at =
            AlignUp(pendingBytes.size(), std::max<uint64_t>(16, Caps().limits.copy_offset_alignment));
        pendingBytes.resize(at + static_cast<uint64_t>(pitch) * height);
        for (uint32_t row = 0; row < height; row++)
            std::memcpy(pendingBytes.data() + at + static_cast<uint64_t>(row) * pitch,
                        static_cast<const uint8_t*>(data) + static_cast<uint64_t>(row) * srcPitch, packed);
        imageWrites.push_back({texture, mip, layer, x, y, width, height, at, pitch});
    }

    state Gpu::StateOf(texture_handle texture) const {
        const auto it = textureStates.find(texture.value);
        return it == textureStates.end() ? state::undefined : it->second;
    }

    void Gpu::SetState(texture_handle texture, conduit::state state) {
        textureStates[texture.value] = state;
    }

    void Gpu::Transition(command_list& list, texture_handle texture, state to) {
        const state from = StateOf(texture);
        if (from == to)
            return;
        texture_barrier b;
        b.texture = texture;
        b.before = from;
        b.after = to;
        list.barrier(b);
        textureStates[texture.value] = to;
    }

    void Gpu::Flush(command_list& list) {
        if (bufferWrites.empty() && imageWrites.empty()) {
            pendingBytes.clear();
            return;
        }
        const RingSlice staging =
            Ring(pendingBytes.size(), std::max<uint64_t>(256, Caps().limits.copy_offset_alignment));
        if (!staging.data) {
            bufferWrites.clear();
            imageWrites.clear();
            pendingBytes.clear();
            return;
        }
        std::memcpy(staging.data, pendingBytes.data(), pendingBytes.size());

        std::vector<buffer_barrier> toCopy, toUse;
        std::vector<texture_barrier> texToCopy, texToUse;
        bufferWrites.erase(
            std::remove_if(bufferWrites.begin(), bufferWrites.end(),
                           [&](const BufferWrite& w) { return device->get_desc(w.buffer) == nullptr; }),
            bufferWrites.end());
        imageWrites.erase(
            std::remove_if(imageWrites.begin(), imageWrites.end(),
                           [&](const ImageWrite& w) { return device->get_desc(w.texture) == nullptr; }),
            imageWrites.end());
        std::unordered_set<uint32_t> seenBuffers;
        seenBuffers.reserve(16);
        for (const BufferWrite& w : bufferWrites) {
            // one barrier pair per buffer however many ranges it has (a world load writes thousands of ranges)
            if (!seenBuffers.insert(w.buffer.value).second)
                continue;
            const auto known = bufferStates.find(w.buffer.value);
            toCopy.push_back(
                {w.buffer, known == bufferStates.end() ? state::undefined : known->second, state::copy_dst});
            toUse.push_back({w.buffer, state::copy_dst, w.state});
            bufferStates[w.buffer.value] = w.state;
        }
        std::vector<texture_handle> touched;
        std::unordered_set<uint32_t> seenTextures;
        for (const ImageWrite& w : imageWrites) {
            if (!seenTextures.insert(w.texture.value).second)
                continue;
            touched.push_back(w.texture);
            const state from = StateOf(w.texture);
            texture_barrier b;
            b.texture = w.texture;
            b.before = from;
            b.after = state::copy_dst;
            texToCopy.push_back(b);
            b.before = state::copy_dst;
            b.after = state::shader_read;
            texToUse.push_back(b);
        }
        list.barrier(texToCopy, toCopy);
        for (const BufferWrite& w : bufferWrites)
            list.copy_buffer(w.buffer, w.offset, staging.buffer, staging.offset + w.source, w.bytes);
        for (const ImageWrite& w : imageWrites) {
            buffer_texture_copy c;
            c.buffer_offset = staging.offset + w.source;
            c.buffer_row_pitch = w.rowBytes;
            c.mip = w.mip;
            c.layer = w.layer;
            c.x = w.x;
            c.y = w.y;
            c.width = w.width;
            c.height = w.height;
            list.copy_buffer_to_texture(w.texture, staging.buffer, std::span<const buffer_texture_copy>(&c, 1));
        }
        list.barrier(texToUse, toUse);
        for (const texture_handle& t : touched)
            textureStates[t.value] = state::shader_read;
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
            if (sources[i].buffer && device->get_desc(sources[i].buffer))
                total += (sources[i].bytes + 255) & ~255ull;
        }
        if (!total)
            return out;
        buffer_desc bd;
        bd.size = total;
        bd.usage = buffer_usage::copy_dst;
        bd.memory = memory_class::readback;
        bd.name = "snapshot";
        const buffer_handle readback = device->create_buffer(bd);
        if (!readback)
            return out;
        command_list* list = device->acquire_command_list(queue_type::graphics);
        Flush(*list);
        std::vector<buffer_barrier> toRead, back;
        for (const BufferSource& s : sources) {
            if (!s.buffer || !device->get_desc(s.buffer))
                continue;
            const auto known = bufferStates.find(s.buffer.value);
            const state from = known == bufferStates.end() ? s.resting : known->second;
            toRead.push_back({s.buffer, from, state::copy_src});
            back.push_back({s.buffer, state::copy_src, from});
        }
        list->barrier({}, toRead);
        for (size_t i = 0; i < sources.size(); i++)
            if (sources[i].buffer && device->get_desc(sources[i].buffer))
                list->copy_buffer(readback, at[i], sources[i].buffer, 0, sources[i].bytes);
        list->barrier({}, back);
        FlushRings();
        device->submit(queue_type::graphics, list);
        device->wait_idle();
        device->invalidate(readback, 0, total);
        if (const uint8_t* mapped = device->map(readback))
            for (size_t i = 0; i < sources.size(); i++)
                if (sources[i].buffer && sources[i].bytes && at[i] + sources[i].bytes <= total)
                    out[i].assign(mapped + at[i], mapped + at[i] + sources[i].bytes);
        device->destroy(readback);
        return out;
    }
}
