#include "rhi/metal/MetalInternal.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <tuple>

namespace rhi::metal {
    namespace {
        bool IsIntegerFormat(Format f) {
            return f == Format::R8Uint || f == Format::R16Uint || f == Format::R32Uint ||
                   f == Format::RG32Uint || f == Format::RGBA32Uint;
        }

        bool StorageCapable(Format f) {
            switch (f) {
            case Format::R8Unorm:
            case Format::RG8Unorm:
            case Format::RGBA8Unorm:
            case Format::BGRA8Unorm:
            case Format::R8Uint:
            case Format::R16Uint:
            case Format::R32Uint:
            case Format::RG32Uint:
            case Format::RGBA32Uint:
            case Format::R16Float:
            case Format::RG16Float:
            case Format::RGBA16Float:
            case Format::R32Float:
            case Format::RG32Float:
            case Format::RGBA32Float:
            case Format::RGB10A2Unorm:
            case Format::R11G11B10Float:
            case Format::RG16Unorm:
            case Format::RGBA16Unorm:
                return true;
            default:
                return false;
            }
        }

        // the shader packer writes the workgroup size of a compute shader as `// perlin:threads X Y Z`
        void ParseThreads(const std::string& src, uint32_t out[3]) {
            const size_t at = src.find("// perlin:threads ");
            if (at == std::string::npos)
                return;
            std::sscanf(src.c_str() + at, "// perlin:threads %u %u %u", &out[0], &out[1], &out[2]);
        }
    }

    Device::Device(id<MTLDevice> dev, const DeviceOptions& opts, const AdapterInfo& info)
        : device(dev), options(opts) {
        caps.adapter = info;
        options.framesInFlight = std::clamp(options.framesInFlight, 1u, 4u);
        if (options.asyncPipelines) {
            // a concurrent queue: several pipelines compile at once on the efficiency and performance cores
            compileQueue = dispatch_queue_create(
                "perlin.metal.pipelines",
                dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_CONCURRENT, QOS_CLASS_USER_INITIATED, 0));
            compileGroup = dispatch_group_create();
        }
    }

    Device::~Device() {
        if (!device)
            return;
        WaitForCompiles();
        WaitIdle();
        lists.clear();
    }

    void Device::ReportError(const std::string& message) {
        std::lock_guard lock(errorMutex);
        std::fprintf(stderr, "[metal] %s\n", message.c_str());
    }

    NativeHandles Device::Native() const {
        NativeHandles n;
        n.api = Api::Metal;
        n.device = (__bridge void*)device;
        n.graphicsQueue = (__bridge void*)queue;
        return n;
    }

    bool Device::Init() {
        queue = [device newCommandQueue];
        if (!queue)
            return false;
#if TARGET_OS_OSX
        gDepth24Stencil8 = device.depth24Stencil8PixelFormatSupported;
#endif
        FillCaps();
        frameFence = CreateFence(0);
        return true;
    }

    void Device::FillCaps() {
        DeviceLimits& l = caps.limits;
        const bool apple3 = [device supportsFamily:MTLGPUFamilyApple3];
        const bool mac = [device supportsFamily:MTLGPUFamilyMac2];
        l.maxTexture2D = (apple3 || mac) ? 16384 : 8192;
        l.maxTextureLayers = 2048;
        l.maxBindGroups = kMaxBindGroups;
        l.maxPushConstantBytes = 0;
        l.maxSampledTexturesPerStage = 128;
        l.maxColorTargets = 8;
        l.uniformBufferAlignment = 256;
        l.storageBufferAlignment = 16;
        l.copyRowPitchAlignment = 1;
        l.copyOffsetAlignment = 16;
        l.maxBufferSize = device.maxBufferLength;
        l.maxAnisotropy = 16.0f;
        l.maxMsaaSamples = 1;
        for (const NSUInteger samples : {8u, 4u, 2u})
            if ([device supportsTextureSampleCount:samples]) {
                l.maxMsaaSamples = static_cast<uint32_t>(samples);
                break;
            }

        DeviceFeatures& f = caps.features;
        f.samplerAnisotropy = true;
        f.depthClipControl = true;
        f.drawIndirectFirstInstance = true;
        f.shaderFloat16 = true;
        f.unifiedMemory = device.hasUnifiedMemory;
        f.subgroupOps = apple3 || mac;
        f.rayTracing = device.supportsRaytracing;
        f.hdrOutput = true;
        f.tearing = true;
        // one queue, looped indirect draws, no gpu-side counts, no timestamps yet
        f.multiDrawIndirect = false;
        f.drawIndirectCount = false;
        f.asyncCompute = false;
        f.dedicatedCopyQueue = false;
        f.timestampQueries = false;
#if TARGET_OS_OSX
        f.textureCompressionBC = device.supportsBCTextureCompression;
#else
        f.textureCompressionBC = false;
#endif
        const bool bc = f.textureCompressionBC;

        for (uint32_t i = 1; i < static_cast<uint32_t>(Format::Count); i++) {
            const Format fmt = static_cast<Format>(i);
            if (ToPixelFormat(fmt) == MTLPixelFormatInvalid)
                continue;
            const FormatInfo& info = GetFormatInfo(fmt);
            FormatUse use = FormatUse::Sampled;
            if (info.compressed) {
                if (bc)
                    use = use | FormatUse::SampledLinear;
                else
                    use = FormatUse::None;
            } else if (info.depth) {
                use = use | FormatUse::SampledLinear | FormatUse::DepthTarget | FormatUse::Msaa;
            } else {
                const bool integer = IsIntegerFormat(fmt);
                if (!integer)
                    use = use | FormatUse::SampledLinear | FormatUse::Blendable | FormatUse::Msaa;
                use = use | FormatUse::ColorTarget;
                if (StorageCapable(fmt))
                    use = use | FormatUse::Storage;
            }
            caps.formats[i] = use;
        }
    }

    BufferHandle Device::CreateBuffer(const BufferDesc& desc) {
        Buffer b;
        b.desc = desc;
        b.name = desc.name ? desc.name : "";
        uint64_t size = AlignUp(std::max<uint64_t>(desc.size, 16), 16);
        if (Has(desc.usage, BufferUsage::Uniform))
            size = AlignUp(size, 256);
        MTLResourceOptions opts = MTLResourceStorageModePrivate;
        switch (desc.memory) {
        case MemoryClass::GpuOnly:
            break;
        case MemoryClass::Upload:
            opts = MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined;
            break;
        default:
            opts = MTLResourceStorageModeShared;
            break;
        }
        b.buffer = [device newBufferWithLength:size options:opts];
        if (!b.buffer)
            return {};
        if (!b.name.empty())
            b.buffer.label = [NSString stringWithUTF8String:b.name.c_str()];
        b.size = size;
        if (desc.memory != MemoryClass::GpuOnly) {
            b.contents = static_cast<uint8_t*>([b.buffer contents]);
            std::memset(b.contents, 0, size);
        }
        stats.bufferBytes += size;
        stats.liveBuffers++;
        const BufferHandle h = buffers.Alloc(std::move(b));
        buffers.Get(h)->desc.name = buffers.Get(h)->name.c_str();
        return h;
    }

    TextureHandle Device::CreateTexture(const TextureDesc& desc) {
        const MTLPixelFormat pf = ToPixelFormat(desc.format);
        if (pf == MTLPixelFormatInvalid) {
            ReportError("CreateTexture: unsupported format " + std::to_string(static_cast<int>(desc.format)));
            return {};
        }
        Texture t;
        t.desc = desc;
        t.desc.mips = (std::max)(1u, desc.mips);
        t.desc.samples = (std::max)(1u, desc.samples);
        t.name = desc.name ? desc.name : "";

        MTLTextureDescriptor* td = [MTLTextureDescriptor new];
        const bool ms = t.desc.samples > 1;
        td.textureType = ToTextureType(desc.dim, ms, false);
        td.pixelFormat = pf;
        td.width = desc.width;
        td.height = desc.height;
        td.depth = desc.dim == TextureDim::Tex3D ? (std::max)(1u, desc.depthOrLayers) : 1;
        td.arrayLength = desc.dim == TextureDim::Tex2DArray ? (std::max)(1u, desc.depthOrLayers) : 1;
        td.mipmapLevelCount = ms ? 1 : t.desc.mips;
        td.sampleCount = t.desc.samples;
        td.storageMode = MTLStorageModePrivate;
        MTLTextureUsage usage = MTLTextureUsageUnknown;
        if (Has(desc.usage, TextureUsage::Sampled))
            usage |= MTLTextureUsageShaderRead;
        if (Has(desc.usage, TextureUsage::Storage))
            usage |= MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        if (Has(desc.usage, TextureUsage::ColorTarget) || Has(desc.usage, TextureUsage::DepthTarget))
            usage |= MTLTextureUsageRenderTarget;
        // srgb and stencil views reinterpret the pixels
        if (!IsDepthFormat(desc.format) || HasStencil(desc.format))
            usage |= MTLTextureUsagePixelFormatView;
        td.usage = usage;
        t.texture = [device newTextureWithDescriptor:td];
        if (!t.texture)
            return {};
        if (!t.name.empty())
            t.texture.label = [NSString stringWithUTF8String:t.name.c_str()];

        uint64_t bytes = 0;
        for (uint32_t m = 0; m < t.desc.mips; m++)
            bytes += static_cast<uint64_t>(RowBytes(desc.format, MipExtent(desc.width, m))) *
                     RowBlocks(desc.format, MipExtent(desc.height, m)) * LayerCountOf(t.desc) *
                     DepthOf(t.desc);
        stats.textureBytes += bytes * t.desc.samples;
        stats.liveTextures++;
        const TextureHandle h = textures.Alloc(std::move(t));
        textures.Get(h)->desc.name = textures.Get(h)->name.c_str();
        return h;
    }

    SamplerHandle Device::CreateSampler(const SamplerDesc& desc) {
        Sampler s;
        s.desc = desc;
        MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
        sd.minFilter = ToMinMag(desc.min);
        sd.magFilter = ToMinMag(desc.mag);
        sd.mipFilter = ToMip(desc.mip);
        sd.sAddressMode = ToAddress(desc.u);
        sd.tAddressMode = ToAddress(desc.v);
        sd.rAddressMode = ToAddress(desc.w);
        sd.maxAnisotropy = static_cast<NSUInteger>(std::clamp(desc.maxAnisotropy, 1.0f, 16.0f));
        sd.lodMinClamp = desc.minLod;
        sd.lodMaxClamp = (std::min)(desc.maxLod, 1000.0f);
        if (desc.compare)
            sd.compareFunction = ToCompare(desc.compareOp);
#if TARGET_OS_OSX
        // metal has three border colours; pick the nearest. lodBias has no equivalent in the sampler descriptor
        if (desc.border[3] < 0.5f)
            sd.borderColor = MTLSamplerBorderColorTransparentBlack;
        else if (desc.border[0] + desc.border[1] + desc.border[2] < 1.5f)
            sd.borderColor = MTLSamplerBorderColorOpaqueBlack;
        else
            sd.borderColor = MTLSamplerBorderColorOpaqueWhite;
#endif
        s.state = [device newSamplerStateWithDescriptor:sd];
        if (!s.state)
            return {};
        return samplers.Alloc(std::move(s));
    }

    ShaderHandle Device::CreateShader(const ShaderDesc& desc) {
        if (desc.format != ShaderFormat::Msl) {
            ReportError("CreateShader: metal takes msl source");
            return {};
        }
        Shader s;
        s.stage = desc.stage;
        s.source.assign(reinterpret_cast<const char*>(desc.code.data()), desc.code.size());
        s.entry = desc.entry ? desc.entry : "main";
        s.iface = ReflectShader(ShaderFormat::Msl, s.source);
        if (desc.stage == ShaderStage::Compute)
            ParseThreads(s.source, s.threads);
        return shaders.Alloc(std::move(s));
    }

    BindLayoutHandle Device::CreateBindLayout(const BindLayoutDesc& desc) {
        BindLayout l;
        l.entries.assign(desc.entries.begin(), desc.entries.end());
        std::sort(l.entries.begin(), l.entries.end(),
                  [](const BindLayoutEntry& a, const BindLayoutEntry& b) { return a.binding < b.binding; });
        return bindLayouts.Alloc(std::move(l));
    }

    BindGroupHandle Device::CreateBindGroup(const BindGroupDesc& desc) {
        if (!bindLayouts.Get(desc.layout)) {
            ReportError("CreateBindGroup: unknown layout");
            return {};
        }
        BindGroup g;
        g.layout = desc.layout;
        g.entries.assign(desc.entries.begin(), desc.entries.end());
        stats.liveBindGroups++;
        return bindGroups.Alloc(std::move(g));
    }

    PipelineLayoutHandle Device::CreatePipelineLayout(const PipelineLayoutDesc& desc) {
        if (desc.pushConstantBytes)
            ReportError("CreatePipelineLayout: metal has no push constants here");
        PipelineLayout l;
        l.desc = desc;
        l.desc.name = nullptr;
        return pipelineLayouts.Alloc(std::move(l));
    }

    QuerySetHandle Device::CreateQuerySet(const QuerySetDesc& desc) {
        Query q;
        q.count = desc.count;
        return queries.Alloc(std::move(q));
    }

    void Device::Destroy(BufferHandle h) {
        Buffer b;
        if (!buffers.Free(h, &b))
            return;
        stats.bufferBytes -= b.size;
        stats.liveBuffers--;
    }

    void Device::Destroy(TextureHandle h) {
        Texture t;
        if (!textures.Free(h, &t))
            return;
        if (!t.external)
            stats.liveTextures--;
    }

    void Device::Destroy(SamplerHandle h) {
        samplers.Free(h);
    }
    void Device::Destroy(ShaderHandle h) {
        shaders.Free(h);
    }
    void Device::Destroy(BindLayoutHandle h) {
        bindLayouts.Free(h);
    }
    void Device::Destroy(BindGroupHandle h) {
        if (bindGroups.Free(h))
            stats.liveBindGroups--;
    }
    void Device::Destroy(PipelineLayoutHandle h) {
        pipelineLayouts.Free(h);
    }
    void Device::Destroy(PipelineHandle h) {
        if (pipelines.Free(h))
            stats.livePipelines--;
    }
    void Device::Destroy(QuerySetHandle h) {
        queries.Free(h);
    }
    void Device::Destroy(FenceHandle h) {
        fences.Free(h);
    }

    const BufferDesc* Device::GetDesc(BufferHandle h) const {
        const Buffer* b = buffers.Get(h);
        return b ? &b->desc : nullptr;
    }

    const TextureDesc* Device::GetDesc(TextureHandle h) const {
        const Texture* t = textures.Get(h);
        return t ? &t->desc : nullptr;
    }

    uint8_t* Device::Map(BufferHandle handle) {
        Buffer* b = buffers.Get(handle);
        return b ? b->contents : nullptr;
    }

    id<MTLTexture> Device::ViewOf(Texture& t, const SubresourceRange& range, TextureDim dim, Format format,
                                  bool stencil) {
        const SubresourceRange r = ResolveRange(range, t.desc);
        const uint32_t layers = LayerCountOf(t.desc);
        MTLPixelFormat pf = format == Format::Unknown ? ToPixelFormat(t.desc.format) : ToPixelFormat(format);
        if (stencil) {
            // the stencil plane of a combined format is its own pixel format
            pf = ToPixelFormat(t.desc.format) == MTLPixelFormatDepth32Float_Stencil8
                     ? MTLPixelFormatX32_Stencil8
                     : MTLPixelFormatInvalid;
#if TARGET_OS_OSX
            if (ToPixelFormat(t.desc.format) == MTLPixelFormatDepth24Unorm_Stencil8)
                pf = MTLPixelFormatX24_Stencil8;
#endif
        }
        const bool wholeMips = r.baseMip == 0 && r.mipCount == (std::max)(1u, t.desc.mips);
        const bool wholeLayers = r.baseLayer == 0 && r.layerCount == layers;
        const bool sameType =
            (dim == TextureDim::Tex2D && t.desc.dim == TextureDim::Tex2D) || dim == t.desc.dim;
        if (format == Format::Unknown && !stencil && wholeMips && wholeLayers && sameType)
            return t.texture;
        if (t.desc.samples > 1)
            return t.texture;

        ViewKey key;
        key.dim = static_cast<uint8_t>(dim);
        key.stencil = stencil ? 1 : 0;
        key.format = static_cast<uint16_t>(format);
        key.baseMip = r.baseMip;
        key.mipCount = r.mipCount;
        key.baseLayer = r.baseLayer;
        key.layerCount = r.layerCount;
        std::lock_guard lock(viewMutex);
        if (const auto it = t.views.find(key); it != t.views.end())
            return it->second;
        MTLTextureType type = ToTextureType(dim, false, false);
        if (dim == TextureDim::Tex2D && layers > 1)
            type = MTLTextureType2D;
        const NSRange levels = NSMakeRange(r.baseMip, r.mipCount);
        const NSRange slices = t.desc.dim == TextureDim::Tex3D ? NSMakeRange(0, 1)
                               : dim == TextureDim::Tex2D      ? NSMakeRange(r.baseLayer, 1)
                                                               : NSMakeRange(r.baseLayer, r.layerCount);
        id<MTLTexture> view = [t.texture newTextureViewWithPixelFormat:pf
                                                           textureType:type
                                                                levels:levels
                                                                slices:slices];
        if (view)
            t.views[key] = view;
        return view;
    }

    TextureHandle Device::CreateSurfaceTexture(Format format, uint32_t width, uint32_t height, bool sampled) {
        Texture t;
        t.desc.dim = TextureDim::Tex2D;
        t.desc.format = format;
        t.desc.width = width;
        t.desc.height = height;
        t.desc.usage = TextureUsage::ColorTarget | TextureUsage::CopySrc |
                       (sampled ? TextureUsage::Sampled : TextureUsage::None);
        t.external = true;
        t.name = "swapchain image";
        return textures.Alloc(std::move(t));
    }

    void Device::SetSurfaceTexture(TextureHandle handle, id<MTLTexture> texture) {
        Texture* t = textures.Get(handle);
        if (!t)
            return;
        std::lock_guard lock(viewMutex);
        t->views.clear();
        t->texture = texture;
    }

    void Device::ResizeSurfaceTexture(TextureHandle handle, uint32_t width, uint32_t height) {
        if (Texture* t = textures.Get(handle)) {
            t->desc.width = width;
            t->desc.height = height;
        }
    }

    // slang numbers samplers 2*slot+1, past metal's 16 sampler slots: they are compacted in declaration order
    std::string Device::PrepareStage(const Shader& sh, const PipelineLayoutDesc& layout, std::vector<StageSlot>& slots,
                                     std::string& error) {
        std::vector<const ShaderResource*> resources;
        for (const ShaderResource& r : sh.iface.resources)
            resources.push_back(&r);
        std::sort(resources.begin(), resources.end(), [](const ShaderResource* a, const ShaderResource* b) {
            return std::tie(a->set, a->binding) < std::tie(b->set, b->binding);
        });

        uint32_t nextSampler = 0;
        struct Edit {
            size_t begin, end;
            std::string text;
        };
        std::vector<Edit> edits;
        for (const ShaderResource* r : resources) {
            StageSlot s;
            s.set = r->set;
            s.binding = r->binding;
            s.kind = r->kind;
            s.dim = r->dim;
            s.index = r->nativeIndex;
            if (r->kind == ResourceKind::Sampler) {
                // metal has 16 sampler slots per stage: past that (java packs with many buffers) the rest share
                // the last one, which only costs them their own filtering mode
                s.index = nextSampler < 16 ? nextSampler++ : 15;
                edits.push_back(
                    {r->clauseBegin, r->clauseEnd, "[[sampler(" + std::to_string(s.index) + ")]]"});
            }
            if (r->set < kMaxBindGroups)
                if (const BindLayout* l = bindLayouts.Get(layout.sets[r->set])) {
                    uint32_t dynamic = 0;
                    for (const BindLayoutEntry& e : l->entries) {
                        if (e.binding == r->binding) {
                            if (e.type == BindingType::UniformBufferDynamic)
                                s.dynamicIndex = dynamic;
                            break;
                        }
                        if (e.type == BindingType::UniformBufferDynamic)
                            dynamic++;
                    }
                }
            slots.push_back(s);
        }

        std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.begin > b.begin; });
        std::string patched = sh.source;
        for (const Edit& e : edits)
            patched.replace(e.begin, e.end - e.begin, e.text);

        return patched;
    }

    id<MTLFunction> Device::LoadFunction(const std::string& patched, const std::string& entry, std::string& error) {
        id<MTLLibrary> lib = nil;
        {
            std::lock_guard lock(libraryMutex);
            if (const auto it = libraries.find(patched); it != libraries.end())
                lib = it->second;
        }
        if (!lib) {
            NSError* err = nil;
            MTLCompileOptions* opts = [MTLCompileOptions new];
            lib = [device newLibraryWithSource:[NSString stringWithUTF8String:patched.c_str()]
                                       options:opts
                                         error:&err];
            if (!lib) {
                error = err ? [[err localizedDescription] UTF8String] : "newLibraryWithSource failed";
                return nil;
            }
            std::lock_guard lock(libraryMutex);
            libraries[patched] = lib;
        }
        id<MTLFunction> function = [lib newFunctionWithName:[NSString stringWithUTF8String:entry.c_str()]];
        if (!function)
            error = "entry point " + entry + " not found";
        return function;
    }

    id<MTLFunction> Device::CompileStage(const Shader& sh, const PipelineLayoutDesc& layout,
                                         std::vector<StageSlot>& slots, std::string& error) {
        const std::string source = PrepareStage(sh, layout, slots, error);
        return source.empty() ? nil : LoadFunction(source, sh.entry, error);
    }

    id<MTLRenderPipelineState> Device::ReadyPipeline(const Pipeline& p) {
        if (!p.build)
            return p.render;
        PipelineBuild& b = *p.build;
        int state = b.state.load(std::memory_order_acquire);
        // the first draw that needs it may wait a moment: most compiles finish well inside a frame
        if (state == 0 && !b.waited.exchange(true)) {
            b.done.Wait(1, 4'000'000ull);
            state = b.state.load(std::memory_order_acquire);
        }
        if (state == 1)
            return b.render;
        if (state < 0 && !b.reported.exchange(true))
            ReportError(b.error);
        if (state == 0)
            stats.pipelinesPending++;
        return nil;
    }

    void Device::WaitForCompiles() {
        if (compileGroup)
            dispatch_group_wait(compileGroup, DISPATCH_TIME_FOREVER);
    }

    PipelineHandle Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
        const Shader* vs = shaders.Get(desc.vertex);
        const Shader* fs = shaders.Get(desc.fragment);
        const PipelineLayout* layout = pipelineLayouts.Get(desc.layout);
        if (!vs || !layout) {
            ReportError("CreateGraphicsPipeline: missing shader or layout");
            return {};
        }
        Pipeline p;
        std::string error;
        const std::string vsSource = PrepareStage(*vs, layout->desc, p.stages[kStageVertex], error);
        const std::string fsSource =
            !vsSource.empty() && fs ? PrepareStage(*fs, layout->desc, p.stages[kStageFragment], error) : std::string();
        if (vsSource.empty() || (fs && fsSource.empty())) {
            ReportError(std::string("shader compile failed (") + (desc.name ? desc.name : "?") +
                        "): " + error);
            return {};
        }

        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        if (desc.name)
            pd.label = [NSString stringWithUTF8String:desc.name];

        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        for (uint32_t i = 0; i < desc.attributeCount; i++) {
            const VertexAttributeDesc& a = desc.attributes[i];
            vd.attributes[a.location].format = ToVertexFormat(a.format);
            vd.attributes[a.location].offset = a.offset;
            vd.attributes[a.location].bufferIndex = kVertexBufferBase - a.stream;
        }
        for (uint32_t i = 0; i < desc.streamCount; i++) {
            const NSUInteger index = kVertexBufferBase - i;
            vd.layouts[index].stride = (std::max)(desc.streams[i].stride, 1u);
            vd.layouts[index].stepFunction = desc.streams[i].step == VertexStep::Instance
                                                 ? MTLVertexStepFunctionPerInstance
                                                 : MTLVertexStepFunctionPerVertex;
            vd.layouts[index].stepRate = 1;
        }
        if (desc.attributeCount)
            pd.vertexDescriptor = vd;

        for (uint32_t i = 0; i < desc.colorCount; i++) {
            const BlendTarget& t = desc.blend.targets[i];
            MTLRenderPipelineColorAttachmentDescriptor* ca = pd.colorAttachments[i];
            ca.pixelFormat = ToPixelFormat(desc.colorFormats[i]);
            ca.blendingEnabled = t.enable;
            if (t.enable) {
                ca.sourceRGBBlendFactor = ToBlendFactor(t.srcColor);
                ca.destinationRGBBlendFactor = ToBlendFactor(t.dstColor);
                ca.rgbBlendOperation = ToBlendOp(t.colorOp);
                ca.sourceAlphaBlendFactor = ToBlendFactor(t.srcAlpha);
                ca.destinationAlphaBlendFactor = ToBlendFactor(t.dstAlpha);
                ca.alphaBlendOperation = ToBlendOp(t.alphaOp);
            }
            // metal's mask bits run the other way round: red is the high bit
            MTLColorWriteMask mask = MTLColorWriteMaskNone;
            if (Has(t.writeMask, ColorMask::R))
                mask |= MTLColorWriteMaskRed;
            if (Has(t.writeMask, ColorMask::G))
                mask |= MTLColorWriteMaskGreen;
            if (Has(t.writeMask, ColorMask::B))
                mask |= MTLColorWriteMaskBlue;
            if (Has(t.writeMask, ColorMask::A))
                mask |= MTLColorWriteMaskAlpha;
            ca.writeMask = mask;
        }
        const bool hasDepth = desc.depthFormat != Format::Unknown;
        if (hasDepth) {
            pd.depthAttachmentPixelFormat = ToPixelFormat(desc.depthFormat);
            if (HasStencil(desc.depthFormat))
                pd.stencilAttachmentPixelFormat = ToPixelFormat(desc.depthFormat);
        }
        pd.rasterSampleCount = (std::max)(1u, desc.samples);
        pd.alphaToCoverageEnabled = desc.blend.alphaToCoverage;
        switch (desc.topology) {
        case Topology::LineList:
            pd.inputPrimitiveTopology = MTLPrimitiveTopologyClassLine;
            break;
        case Topology::PointList:
            pd.inputPrimitiveTopology = MTLPrimitiveTopologyClassPoint;
            break;
        default:
            pd.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
            break;
        }

        // the msl compile and the pipeline build are the slow part (tens of milliseconds for a shader pack program):
        // off the render thread when allowed, so a pipeline first needed mid-game costs a skipped draw, not a hitch
        const std::string name = desc.name ? desc.name : "?";
        const std::string vsEntry = vs->entry, fsEntry = fs ? fs->entry : std::string();
        auto build = std::make_shared<PipelineBuild>();
        const auto compile = [this, build, pd, vsSource, fsSource, vsEntry, fsEntry, name] {
            std::string why;
            id<MTLFunction> vsFn = LoadFunction(vsSource, vsEntry, why);
            id<MTLFunction> fsFn = vsFn && !fsSource.empty() ? LoadFunction(fsSource, fsEntry, why) : nil;
            if (vsFn && (fsSource.empty() || fsFn)) {
                pd.vertexFunction = vsFn;
                pd.fragmentFunction = fsFn;
                NSError* err = nil;
                build->render = [device newRenderPipelineStateWithDescriptor:pd error:&err];
                if (!build->render)
                    why = err ? [[err localizedDescription] UTF8String] : "unknown";
            }
            if (build->render) {
                build->state.store(1, std::memory_order_release);
            } else {
                build->error = "pipeline creation failed (" + name + "): " + why;
                build->state.store(-1, std::memory_order_release);
            }
            build->done.Raise(1);
        };
        if (options.asyncPipelines && compileQueue) {
            dispatch_group_async(compileGroup, compileQueue, ^{
              @autoreleasepool {
                  compile();
              }
            });
            p.build = build;
        } else {
            compile();
            if (!build->render) {
                ReportError(build->error);
                return {};
            }
            p.render = build->render;
        }

        const DepthStencilState& ds = desc.depthStencil;
        MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction =
            hasDepth && ds.depthTest ? ToCompare(ds.depthCompare) : MTLCompareFunctionAlways;
        dd.depthWriteEnabled = hasDepth && ds.depthWrite;
        if (hasDepth && ds.stencilTest) {
            const auto face = [&](const StencilFace& f) {
                MTLStencilDescriptor* sd = [MTLStencilDescriptor new];
                sd.stencilFailureOperation = ToStencilOp(f.fail);
                sd.depthFailureOperation = ToStencilOp(f.depthFail);
                sd.depthStencilPassOperation = ToStencilOp(f.pass);
                sd.stencilCompareFunction = ToCompare(f.compare);
                sd.readMask = ds.stencilReadMask;
                sd.writeMask = ds.stencilWriteMask;
                return sd;
            };
            dd.frontFaceStencil = face(ds.front);
            dd.backFaceStencil = face(ds.back);
        }
        p.depthStencil = [device newDepthStencilStateWithDescriptor:dd];

        p.cull = desc.raster.cull == CullMode::None   ? MTLCullModeNone
                 : desc.raster.cull == CullMode::Back ? MTLCullModeBack
                                                      : MTLCullModeFront;
        p.winding = desc.raster.frontFace == FrontFace::CounterClockwise ? MTLWindingCounterClockwise
                                                                         : MTLWindingClockwise;
        p.fill = desc.raster.wireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill;
        p.clip = desc.raster.depthClip ? MTLDepthClipModeClip : MTLDepthClipModeClamp;
        p.biasConstant = desc.raster.depthBiasConstant;
        p.biasSlope = desc.raster.depthBiasSlope;
        p.biasClamp = desc.raster.depthBiasClamp;
        p.primitive = ToPrimitive(desc.topology);
        p.streamCount = desc.streamCount;
        stats.livePipelines++;
        return pipelines.Alloc(std::move(p));
    }

    PipelineHandle Device::CreateComputePipeline(const ComputePipelineDesc& desc) {
        const Shader* cs = shaders.Get(desc.compute);
        const PipelineLayout* layout = pipelineLayouts.Get(desc.layout);
        if (!cs || !layout) {
            ReportError("CreateComputePipeline: missing shader or layout");
            return {};
        }
        Pipeline p;
        p.compute = true;
        std::string error;
        id<MTLFunction> fn = CompileStage(*cs, layout->desc, p.stages[kStageCompute], error);
        if (!fn) {
            ReportError(std::string("shader compile failed (") + (desc.name ? desc.name : "?") +
                        "): " + error);
            return {};
        }
        NSError* err = nil;
        p.comp = [device newComputePipelineStateWithFunction:fn error:&err];
        if (!p.comp) {
            ReportError(std::string("compute pipeline creation failed: ") +
                        (err ? [[err localizedDescription] UTF8String] : "unknown"));
            return {};
        }
        p.threads = MTLSizeMake(cs->threads[0], cs->threads[1], cs->threads[2]);
        stats.livePipelines++;
        return pipelines.Alloc(std::move(p));
    }

    FenceHandle Device::CreateFence(uint64_t initialValue) {
        Fence f;
        f.value->value.store(initialValue);
        return fences.Alloc(std::move(f));
    }

    uint64_t Device::FenceValue(FenceHandle fence) {
        Fence* f = fences.Get(fence);
        return f ? f->value->value.load() : 0;
    }

    void Device::SignalFence(FenceHandle fence, uint64_t value) {
        if (Fence* f = fences.Get(fence))
            f->value->Raise(value);
    }

    bool Device::WaitFence(FenceHandle fence, uint64_t value, uint64_t timeoutNs) {
        Fence* f = fences.Get(fence);
        return !f || f->value->Wait(value, timeoutNs);
    }

    void Device::SignalWhenDone(id<MTLCommandBuffer> after, FenceHandle fence, uint64_t value) {
        Fence* f = fences.Get(fence);
        if (!f)
            return;
        std::shared_ptr<Timeline> timeline = f->value;
        [after addCompletedHandler:^(id<MTLCommandBuffer>) {
          timeline->Raise(value);
        }];
    }

    void Device::BeginFrame() {
        frame++;
        const uint64_t behind = (std::max)(1u, options.framesInFlight);
        if (frame > behind)
            WaitFence(frameFence, frame - behind, 2'000'000'000ull);
    }

    // an empty buffer queued behind this frame's work raises the frame fence once all of it has finished
    void Device::EndFrame() {
        id<MTLCommandBuffer> cb = [queue commandBuffer];
        SignalWhenDone(cb, frameFence, frame);
        [cb commit];
    }

    void Device::WaitIdle() {
        if (!queue)
            return;
        id<MTLCommandBuffer> cb = [queue commandBuffer];
        [cb commit];
        [cb waitUntilCompleted];
    }

    MemoryBudget Device::QueryMemoryBudget() {
        MemoryBudget m;
        m.deviceBudget = device.recommendedMaxWorkingSetSize;
        m.deviceUsage = device.currentAllocatedSize;
        m.valid = true;
        return m;
    }

    CommandList* Device::AcquireCommandList(QueueType type) {
        std::lock_guard lock(listMutex);
        CmdList* list;
        if (!freeLists.empty()) {
            list = freeLists.back();
            freeLists.pop_back();
        } else {
            lists.push_back(std::make_unique<CmdList>(*this, type));
            list = lists.back().get();
        }
        list->Begin();
        return list;
    }

    void Device::Submit(QueueType, const SubmitDesc& desc) {
        id<MTLCommandBuffer> last = nil;
        for (rhi::CommandList* base : desc.lists) {
            id<MTLCommandBuffer> cb = static_cast<CmdList*>(base)->Finish();
            if (!cb)
                continue;
            if (last)
                [last commit];
            last = cb;
        }
        if (!last && !desc.signals.empty())
            last = [queue commandBuffer];
        if (last) {
            for (const FenceOp& op : desc.signals)
                SignalWhenDone(last, op.fence, op.value);
            [last commit];
        }
        {
            std::lock_guard lock(listMutex);
            for (rhi::CommandList* base : desc.lists)
                freeLists.push_back(static_cast<CmdList*>(base));
        }
        stats.submits++;
    }

    std::unique_ptr<rhi::Swapchain> Device::CreateSwapchain(const SwapchainDesc& desc) {
        return MakeSwapchain(*this, desc);
    }
}
