#include "client/renderer/gpu/ModGpu.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace perlin::gpu {
    namespace {
        using conduit::format;

        constexpr format kFormats[21] = {
            format::unknown,   format::unknown,         format::r8_unorm,        format::rg8_unorm,
            format::rgba8_unorm, format::rgba8_srgb,    format::bgra8_unorm,     format::r16_float,
            format::rg16_float,  format::rgba16_float,  format::r32_float,       format::rg32_float,
            format::rgb32_float, format::rgba32_float,  format::r32_uint,        format::rgb10a2_unorm,
            format::r11g11b10_float, format::d16_unorm, format::d32_float,       format::d24_unorm_s8_uint,
            format::d32_float_s8_uint};

        uint8_t SchemaFormat(format f) {
            for (uint8_t i = 2; i <= 20; i++)
                if (kFormats[i] == f)
                    return i;
            return 1;
        }

        constexpr uint32_t kUniformMax = 4096;
        constexpr uint32_t kMaxReportedPerPlugin = 256;

        uint64_t AlignUp64(uint64_t v, uint64_t a) {
            return (v + a - 1) / a * a;
        }

        conduit::state RestingFor(uint32_t usage) {
            conduit::state s = conduit::state::undefined;
            if (usage & (1u << 0))
                s = s | conduit::state::vertex_buffer;
            if (usage & (1u << 1))
                s = s | conduit::state::index_buffer;
            if (usage & (1u << 3))
                s = s | conduit::state::indirect_argument;
            if (usage & (1u << 2))
                s = s | conduit::state::shader_read;
            return s == conduit::state::undefined ? conduit::state::copy_src : s;
        }

        conduit::buffer_usage ToBufferUsage(uint32_t u) {
            conduit::buffer_usage r = conduit::buffer_usage::copy_src | conduit::buffer_usage::copy_dst;
            if (u & (1u << 0))
                r = r | conduit::buffer_usage::vertex;
            if (u & (1u << 1))
                r = r | conduit::buffer_usage::index;
            if (u & (1u << 2))
                r = r | conduit::buffer_usage::storage;
            if (u & (1u << 3))
                r = r | conduit::buffer_usage::indirect;
            return r;
        }

        conduit::texture_usage ToTextureUsage(uint32_t u, bool copies) {
            conduit::texture_usage r = conduit::texture_usage::none;
            if (u & (1u << 0))
                r = r | conduit::texture_usage::sampled;
            if (u & (1u << 1))
                r = r | conduit::texture_usage::storage;
            if (u & (1u << 2))
                r = r | conduit::texture_usage::color_target;
            if (u & (1u << 3))
                r = r | conduit::texture_usage::depth_target;
            if (u & (1u << 4))
                r = r | conduit::texture_usage::copy_src;
            if (u & (1u << 5))
                r = r | conduit::texture_usage::copy_dst;
            if (copies)
                r = r | conduit::texture_usage::copy_src | conduit::texture_usage::copy_dst;
            return r;
        }

        conduit::texture_dim ToDim(uint8_t d) {
            switch (d) {
            case 1:
                return conduit::texture_dim::tex2d_array;
            case 2:
                return conduit::texture_dim::cube;
            case 3:
                return conduit::texture_dim::tex3d;
            default:
                return conduit::texture_dim::tex2d;
            }
        }

        std::array<float, 4> UnpackArgb(uint32_t c) {
            return {((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, ((c >> 24) & 255) / 255.0f};
        }

        constexpr uint32_t kUsageSampled = 1u << 0, kUsageStorage = 1u << 1, kUsageColor = 1u << 2,
                           kUsageDepth = 1u << 3;
        constexpr uint32_t kBufVertex = 1u << 0, kBufIndex = 1u << 1, kBufStorage = 1u << 2, kBufIndirect = 1u << 3;
    }

    size_t ModGpu::GroupKeyHash::operator()(const GroupKey& k) const {
        uint64_t h = 1469598103934665603ull;
        for (uint32_t v : k.values) {
            h ^= v;
            h *= 1099511628211ull;
        }
        return static_cast<size_t>(h);
    }

    ModGpu::ModGpu(Gpu& g) : gpu(g) {}

    ModGpu::~ModGpu() {
        Shutdown();
    }

    conduit::format ModGpu::ToFormat(uint8_t f) const {
        if (f == 1)
            return gpu.TargetFormat();
        return f <= 20 ? kFormats[f] : format::unknown;
    }

    void ModGpu::Start(const std::string& rendererName) {
        if (started)
            return;
        ModDeviceInfo info;
        const conduit::device_caps& caps = gpu.Caps();
        info.api = conduit::api_id(gpu.Api());
        info.adapter = caps.adapter.name;
        info.vendor = conduit::vendor_name(caps.adapter.vendor_id);
        info.discrete = caps.adapter.type == conduit::adapter_type::discrete;
        info.targetFormat = SchemaFormat(gpu.TargetFormat());
        info.width = gpu.TargetWidth();
        info.height = gpu.TargetHeight();
        info.maxTexture2d = caps.limits.max_texture2d;
        info.maxTextureLayers = caps.limits.max_texture_layers;
        info.maxMsaa = caps.limits.max_msaa_samples;
        info.maxColorTargets = std::min<uint32_t>(caps.limits.max_color_targets, 4);
        info.maxBufferBytes = std::min<uint64_t>(caps.limits.max_buffer_size, kModMaxBufferBytes);
        info.compute = true;
        info.multiDrawIndirect = caps.features.multi_draw_indirect;
        info.float16 = caps.features.shader_float16;
        info.int64 = caps.features.shader_int64;
        info.subgroups = caps.features.subgroup_ops;
        info.sceneCopy = true;
        info.target = gpu.ShaderTarget();
        static const char* kTargets[] = {"spirv", "dxil", "hlsl", "msl", "wgsl"};
        info.shaderTarget = kTargets[static_cast<size_t>(info.target)];
        (void)rendererName;
        ModGpuHub::Instance().RendererUp(info);
        generation = ModGpuHub::Instance().Generation();
        screenW = gpu.TargetWidth();
        screenH = gpu.TargetHeight();
        screenFormat = gpu.TargetFormat();
        started = true;
    }

    void ModGpu::Shutdown() {
        if (!started)
            return;
        started = false;
        conduit::device& dev = gpu.Dev();
        dev.wait_idle();
        for (auto& [name, res] : plugins)
            DropAll(res);
        plugins.clear();
        for (PendingRead& r : reads)
            if (r.staging)
                dev.destroy(r.staging);
        reads.clear();
        for (auto& [key, group] : textureGroups)
            dev.destroy(group);
        textureGroups.clear();
        storageGroups.clear();
        ModGpuHub::Instance().RendererDown();
    }

    void ModGpu::Report(const std::string& plugin, const std::string& message) {
        PluginResources& res = plugins[plugin];
        if (res.reported.size() >= kMaxReportedPerPlugin || !res.reported.insert(message).second)
            return;
        ModGpuHub::Instance().Error(plugin, message);
    }

    void ModGpu::DestroyPipeline(Pipeline& p) {
        conduit::device& dev = gpu.Dev();
        if (p.handle)
            dev.destroy(p.handle);
        if (p.layout)
            dev.destroy(p.layout);
        if (p.storageLayout)
            dev.destroy(p.storageLayout);
        p = {};
    }

    void ModGpu::ForgetGroups(conduit::texture_handle texture) {
        conduit::device& dev = gpu.Dev();
        for (auto it = textureGroups.begin(); it != textureGroups.end();) {
            bool uses = false;
            const auto& v = it->first.values;
            for (size_t i = 1; i + 2 < v.size(); i += 3)
                uses |= v[i] == texture.value;
            if (uses) {
                dev.destroy(it->second);
                it = textureGroups.erase(it);
            } else {
                ++it;
            }
        }
        gpu.ForgetTexture(texture);
    }

    void ModGpu::DestroyTexture(PluginResources&, Texture& t) {
        if (t.handle) {
            ForgetGroups(t.handle);
            gpu.Dev().destroy(t.handle);
        }
        t.handle = {};
    }

    void ModGpu::DestroyPack(Pack& p) {
        conduit::device& dev = gpu.Dev();
        for (auto it = textureGroups.begin(); it != textureGroups.end();) {
            if (!it->first.values.empty() && it->first.values[0] == p.textureLayout.value) {
                dev.destroy(it->second);
                it = textureGroups.erase(it);
            } else {
                ++it;
            }
        }
        for (Program& prog : p.programs) {
            for (conduit::shader_handle s : {prog.vertex, prog.fragment, prog.compute})
                if (s)
                    dev.destroy(s);
        }
        if (p.textureLayout)
            dev.destroy(p.textureLayout);
        p.programs.clear();
        p.textureLayout = {};
    }

    void ModGpu::DropAll(PluginResources& res) {
        conduit::device& dev = gpu.Dev();
        for (auto& [id, p] : res.pipelines)
            DestroyPipeline(p);
        res.pipelines.clear();
        for (auto& [id, t] : res.textures)
            DestroyTexture(res, t);
        res.textures.clear();
        for (auto& [id, b] : res.buffers)
            if (b.handle)
                dev.destroy(b.handle);
        res.buffers.clear();
        for (auto& [id, s] : res.samplers) {
            for (auto it = textureGroups.begin(); it != textureGroups.end();) {
                const auto& v = it->first.values;
                bool uses = false;
                for (size_t i = 2; i < v.size(); i += 3)
                    uses |= v[i] == s.value;
                if (uses) {
                    dev.destroy(it->second);
                    it = textureGroups.erase(it);
                } else {
                    ++it;
                }
            }
            if (s)
                dev.destroy(s);
        }
        res.samplers.clear();
        for (auto& [id, p] : res.packs)
            DestroyPack(p);
        res.packs.clear();
    }

    void ModGpu::Resize(uint32_t width, uint32_t height, conduit::format fmt) {
        screenW = width;
        screenH = height;
        screenFormat = fmt;
        ModGpuHub::Instance().SetScreen(width, height, SchemaFormat(fmt));
        for (auto& [name, res] : plugins) {
            for (auto& [id, t] : res.textures) {
                if (!t.desc.screenSized)
                    continue;
                const uint32_t w =
                    std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(width * t.desc.scale)));
                const uint32_t h =
                    std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(height * t.desc.scale)));
                const conduit::format wanted = t.screenFormat ? fmt : t.format;
                if (t.handle && t.width == w && t.height == h && t.format == wanted)
                    continue;
                DestroyTexture(res, t);
                BuildTexture(t, w, h, name, id);
            }
            for (auto& [id, p] : res.pipelines) {
                if (!p.screenFormat || p.compute)
                    continue;
                const ModPipelineDesc desc = p.desc;
                const bool screen = p.screenFormat;
                DestroyPipeline(p);
                p.desc = desc;
                p.screenFormat = screen;
                BuildPipeline(res, p, name);
            }
        }
    }

    void ModGpu::BeginFrame(uint32_t width, uint32_t height) {
        if (!started)
            return;
        ModGpuHub& hub = ModGpuHub::Instance();
        frameCounter++;
        if (frameCounter - lastReportClear > 3600) {
            lastReportClear = frameCounter;
            for (auto& [name, res] : plugins)
                res.reported.clear();
        }
        for (auto& [key, group] : storageGroups)
            (void)group;
        storageGroups.clear();
        const conduit::format fmt = gpu.TargetFormat();
        if (width != screenW || height != screenH || fmt != screenFormat)
            Resize(width, height, fmt);
        std::vector<ModOp> ops;
        hub.Drain(ops);
        for (ModOp& op : ops) {
            if (op.generation != generation)
                continue;
            Apply(op);
        }
        CollectReads();
    }

    void ModGpu::Apply(ModOp& op) {
        conduit::device& dev = gpu.Dev();
        if (op.kind == ModOp::Kind::DropPlugin) {
            const auto it = plugins.find(op.plugin);
            if (it != plugins.end()) {
                dev.wait_idle();
                DropAll(it->second);
                plugins.erase(it);
            }
            for (auto r = reads.begin(); r != reads.end();) {
                if (r->plugin == op.plugin) {
                    dev.destroy(r->staging);
                    r = reads.erase(r);
                } else {
                    ++r;
                }
            }
            pending.erase(std::remove_if(pending.begin(), pending.end(),
                                         [&](const PendingWrite& w) { return w.plugin == op.plugin; }),
                          pending.end());
            return;
        }
        PluginResources& res = plugins[op.plugin];
        switch (op.kind) {
        case ModOp::Kind::Pack:
            CreatePack(res, op);
            break;
        case ModOp::Kind::Buffer:
            CreateBuffer(res, op);
            break;
        case ModOp::Kind::BufferWrite: {
            const auto it = res.buffers.find(op.id);
            if (it == res.buffers.end() || !op.bytes)
                break;
            pending.push_back({op.plugin, op.id, op.offset, op.bytes});
            break;
        }
        case ModOp::Kind::BufferRead: {
            const auto it = res.buffers.find(op.id);
            if (it == res.buffers.end() || op.offset + op.size > it->second.size) {
                Report(op.plugin, "GpuBufferRead: bad buffer or range");
                break;
            }
            conduit::buffer_desc bd;
            bd.size = AlignUp64(op.size, 4);
            bd.usage = conduit::buffer_usage::copy_dst;
            bd.memory = conduit::memory_class::readback;
            bd.name = "mod readback";
            PendingRead r;
            r.plugin = op.plugin;
            r.tag = op.tag;
            r.id = op.id;
            r.size = op.size;
            r.offset = op.offset;
            r.staging = dev.create_buffer(bd);
            if (!r.staging) {
                Report(op.plugin, "GpuBufferRead: could not create the readback buffer");
                break;
            }
            reads.push_back(std::move(r));
            break;
        }
        case ModOp::Kind::Texture:
            CreateTexture(res, op.id, op.texture, op.plugin);
            break;
        case ModOp::Kind::TextureWrite: {
            const auto it = res.textures.find(op.id);
            if (it == res.textures.end() || !it->second.handle)
                break;
            Texture& t = it->second;
            const ModTextureWrite& w = op.textureWrite;
            const conduit::texture_desc* td = dev.get_desc(t.handle);
            if (!td || conduit::is_depth_format(td->format) || td->samples > 1) {
                Report(op.plugin, "GpuTextureWrite: depth and multisampled textures cannot be written from the CPU");
                break;
            }
            const uint32_t bpp = conduit::get_format_info(td->format).bytes_per_block;
            const uint32_t mw = std::max(1u, td->width >> w.mip), mh = std::max(1u, td->height >> w.mip);
            if (w.mip >= td->mips || w.layer >= td->depth_or_layers || w.x + w.width > mw || w.y + w.height > mh ||
                w.data.size() != static_cast<uint64_t>(w.width) * w.height * bpp) {
                Report(op.plugin, "GpuTextureWrite: region or data size does not match the texture");
                break;
            }
            gpu.WriteTexture(t.handle, w.layer, w.x, w.y, w.width, w.height, w.data.data(), 0, w.mip);
            break;
        }
        case ModOp::Kind::Sampler:
            CreateSampler(res, op);
            break;
        case ModOp::Kind::Pipeline:
            CreatePipeline(res, op.id, op.pipeline, op.plugin);
            break;
        case ModOp::Kind::Destroy:
            switch (op.resource) {
            case ModResource::Pack:
                if (const auto it = res.packs.find(op.id); it != res.packs.end()) {
                    DestroyPack(it->second);
                    res.packs.erase(it);
                }
                break;
            case ModResource::Buffer:
                if (const auto it = res.buffers.find(op.id); it != res.buffers.end()) {
                    dev.destroy(it->second.handle);
                    res.buffers.erase(it);
                    pending.erase(std::remove_if(pending.begin(), pending.end(),
                                                 [&](const PendingWrite& w) {
                                                     return w.plugin == op.plugin && w.id == op.id;
                                                 }),
                                  pending.end());
                    touched.erase(std::remove(touched.begin(), touched.end(), std::make_pair(op.plugin, op.id)),
                                  touched.end());
                }
                break;
            case ModResource::Texture:
                if (const auto it = res.textures.find(op.id); it != res.textures.end()) {
                    DestroyTexture(res, it->second);
                    res.textures.erase(it);
                }
                break;
            case ModResource::Sampler:
                if (const auto it = res.samplers.find(op.id); it != res.samplers.end()) {
                    for (auto g = textureGroups.begin(); g != textureGroups.end();) {
                        const auto& v = g->first.values;
                        bool uses = false;
                        for (size_t i = 2; i < v.size(); i += 3)
                            uses |= v[i] == it->second.value;
                        if (uses) {
                            dev.destroy(g->second);
                            g = textureGroups.erase(g);
                        } else {
                            ++g;
                        }
                    }
                    dev.destroy(it->second);
                    res.samplers.erase(it);
                }
                break;
            case ModResource::Pipeline:
                if (const auto it = res.pipelines.find(op.id); it != res.pipelines.end()) {
                    DestroyPipeline(it->second);
                    res.pipelines.erase(it);
                }
                break;
            }
            break;
        default:
            break;
        }
    }

    void ModGpu::CreatePack(PluginResources& res, ModOp& op) {
        conduit::device& dev = gpu.Dev();
        Pack pack;
        std::string error;
        pack.bytes = op.bytes;
        if (!op.bytes || !pack.library.load(*op.bytes, &error)) {
            Report(op.plugin, "shader pack: " + error);
            return;
        }
        const conduit::shader_target target = gpu.ShaderTarget();
        for (const conduit::program_info& p : pack.library.programs()) {
            Program prog;
            prog.name = p.name;
            for (conduit::shader_stage stage :
                 {conduit::shader_stage::vertex, conduit::shader_stage::fragment, conduit::shader_stage::compute}) {
                const auto code = pack.library.code(p, stage, target);
                if (code.empty())
                    continue;
                conduit::shader_desc sd;
                sd.stage = stage;
                sd.format = conduit::shader_library::format_for(target);
                sd.code = code;
                sd.entry = conduit::shader_library::entry_for(stage, target);
                sd.name = p.name.c_str();
                const conduit::shader_handle h = dev.create_shader(sd);
                (stage == conduit::shader_stage::vertex     ? prog.vertex
                 : stage == conduit::shader_stage::fragment ? prog.fragment
                                                            : prog.compute) = h;
            }
            pack.programs.push_back(std::move(prog));
        }
        std::vector<conduit::bind_layout_entry> entries;
        for (const conduit::texture_slot& s : pack.library.texture_slots()) {
            entries.push_back({s.slot * 2, conduit::binding_type::sampled_texture,
                               conduit::shader_stage::fragment | conduit::shader_stage::vertex | conduit::shader_stage::compute,
                               1, s.array ? conduit::texture_dim::tex2d_array : conduit::texture_dim::tex2d,
                               !s.unfilterable});
            entries.push_back({s.slot * 2 + 1, conduit::binding_type::sampler,
                               conduit::shader_stage::fragment | conduit::shader_stage::vertex | conduit::shader_stage::compute,
                               1, conduit::texture_dim::tex2d, !s.unfilterable});
        }
        conduit::bind_layout_desc ld;
        ld.entries = entries;
        ld.name = "mod textures";
        pack.textureLayout = dev.create_bind_layout(ld);
        if (!pack.textureLayout) {
            Report(op.plugin, "could not create the texture bind layout for the shader pack");
            DestroyPack(pack);
            return;
        }
        res.packs[op.id] = std::move(pack);
    }

    void ModGpu::CreateBuffer(PluginResources& res, ModOp& op) {
        conduit::device& dev = gpu.Dev();
        conduit::buffer_desc bd;
        bd.size = AlignUp64(op.buffer.size, 4);
        bd.usage = ToBufferUsage(op.buffer.usage);
        bd.memory = conduit::memory_class::gpu_only;
        bd.name = op.buffer.name.empty() ? "mod buffer" : op.buffer.name.c_str();
        if (bd.size > gpu.Caps().limits.max_buffer_size) {
            Report(op.plugin, "buffer is larger than the device allows");
            return;
        }
        Buffer b;
        b.handle = dev.create_buffer(bd);
        if (!b.handle) {
            Report(op.plugin, "could not create a buffer");
            return;
        }
        b.size = op.buffer.size;
        b.usage = op.buffer.usage;
        b.resting = RestingFor(op.buffer.usage);
        b.current = conduit::state::undefined;
        res.buffers[op.id] = b;
        if (!op.buffer.data.empty())
            pending.push_back({op.plugin, op.id, 0,
                               std::make_shared<const std::vector<uint8_t>>(std::move(op.buffer.data))});
    }

    bool ModGpu::BuildTexture(Texture& t, uint32_t width, uint32_t height, const std::string& plugin, uint32_t id) {
        conduit::device& dev = gpu.Dev();
        const ModTextureDesc& d = t.desc;
        const conduit::format fmt = ToFormat(d.format);
        const conduit::device_caps& caps = gpu.Caps();
        const conduit::format_use use = caps.format_support(fmt);
        if (fmt == conduit::format::unknown) {
            Report(plugin, "texture format is not available");
            return false;
        }
        if ((d.usage & kUsageSampled) && !conduit::has(use, conduit::format_use::sampled)) {
            Report(plugin, "this device cannot sample the texture format");
            return false;
        }
        if ((d.usage & kUsageColor) && !conduit::has(use, conduit::format_use::color_target)) {
            Report(plugin, "this device cannot render to the texture format");
            return false;
        }
        if ((d.usage & kUsageDepth) && !conduit::has(use, conduit::format_use::depth_target)) {
            Report(plugin, "this device cannot use the texture format as a depth target");
            return false;
        }
        if ((d.usage & kUsageStorage) && !conduit::has(use, conduit::format_use::storage)) {
            Report(plugin, "this device cannot write the texture format from shaders");
            return false;
        }
        if (d.samples > 1 && d.samples > caps.limits.max_msaa_samples) {
            Report(plugin, "this device does not support that many samples");
            return false;
        }
        if (width > caps.limits.max_texture2d || height > caps.limits.max_texture2d ||
            d.layers > caps.limits.max_texture_layers) {
            Report(plugin, "texture is larger than the device allows");
            return false;
        }
        conduit::texture_desc td;
        td.dim = ToDim(d.dim);
        td.format = fmt;
        td.width = width;
        td.height = height;
        td.depth_or_layers = d.layers;
        td.mips = d.mips;
        td.samples = d.samples;
        td.usage = ToTextureUsage(d.usage, !conduit::is_depth_format(fmt) && d.samples == 1);
        td.name = d.name.empty() ? "mod texture" : d.name.c_str();
        t.handle = dev.create_texture(td);
        if (!t.handle) {
            Report(plugin, "could not create a texture");
            return false;
        }
        gpu.SetState(t.handle, conduit::state::undefined);
        t.width = width;
        t.height = height;
        t.format = fmt;
        (void)id;
        return true;
    }

    void ModGpu::CreateTexture(PluginResources& res, uint32_t id, const ModTextureDesc& desc, const std::string& plugin) {
        Texture t;
        t.desc = desc;
        t.desc.data.clear();
        t.screenFormat = desc.format == 1;
        uint32_t w = desc.width, h = desc.height;
        if (desc.screenSized) {
            w = std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(std::max(screenW, 1u) * desc.scale)));
            h = std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(std::max(screenH, 1u) * desc.scale)));
        }
        if (!BuildTexture(t, w, h, plugin, id)) {
            t.handle = {};
            res.textures[id] = std::move(t);
            return;
        }
        if (!desc.data.empty()) {
            const uint32_t bpp = conduit::get_format_info(t.format).bytes_per_block;
            if (!conduit::is_depth_format(t.format) && desc.samples == 1 &&
                desc.data.size() == static_cast<uint64_t>(w) * h * bpp)
                gpu.WriteTexture(t.handle, 0, 0, 0, w, h, desc.data.data());
            else
                Report(plugin, "initial texture data does not match the texture");
        }
        res.textures[id] = std::move(t);
    }

    void ModGpu::CreateSampler(PluginResources& res, ModOp& op) {
        const ModSamplerDesc& s = op.sampler;
        conduit::sampler_desc sd;
        sd.min = static_cast<conduit::filter>(s.min);
        sd.mag = static_cast<conduit::filter>(s.mag);
        sd.mip = static_cast<conduit::filter>(s.mip);
        sd.u = static_cast<conduit::address_mode>(s.u);
        sd.v = static_cast<conduit::address_mode>(s.v);
        sd.w = static_cast<conduit::address_mode>(s.w);
        sd.max_anisotropy = std::min(s.anisotropy, std::max(1.0f, gpu.Caps().limits.max_anisotropy));
        sd.lod_bias = s.lodBias;
        sd.min_lod = s.minLod;
        sd.max_lod = s.maxLod;
        sd.compare = s.compare;
        sd.compare_op = static_cast<conduit::compare_op>(s.compareOp);
        sd.border = UnpackArgb(s.border);
        const conduit::sampler_handle h = gpu.Dev().create_sampler(sd);
        if (!h) {
            Report(op.plugin, "could not create a sampler");
            return;
        }
        res.samplers[op.id] = h;
    }

    bool ModGpu::BuildPipeline(PluginResources& res, Pipeline& p, const std::string& plugin) {
        conduit::device& dev = gpu.Dev();
        const ModPipelineDesc& d = p.desc;
        const auto packIt = res.packs.find(d.pack);
        if (packIt == res.packs.end()) {
            Report(plugin, "pipeline: the shader pack was destroyed");
            return false;
        }
        Pack& pack = packIt->second;
        const Program* program = nullptr;
        const conduit::program_info* info = nullptr;
        for (size_t i = 0; i < pack.programs.size(); i++)
            if (pack.programs[i].name == d.program) {
                program = &pack.programs[i];
                info = &pack.library.programs()[i];
            }
        if (!program) {
            Report(plugin, "pipeline: no program named " + d.program);
            return false;
        }
        p.compute = d.compute;
        p.uniformBytes = info->uniform_bytes;
        p.streamCount = static_cast<uint32_t>(d.streams.size());

        if (!d.storage.empty()) {
            std::vector<conduit::bind_layout_entry> entries;
            for (const ModStorageBinding& s : d.storage) {
                conduit::bind_layout_entry e;
                e.binding = s.binding;
                e.type = s.kind == 0   ? conduit::binding_type::storage_buffer
                         : s.kind == 1 ? conduit::binding_type::storage_buffer_rw
                                       : conduit::binding_type::storage_texture_rw;
                e.stages = d.compute ? conduit::shader_stage::compute : conduit::shader_stage::all_graphics;
                if (s.kind == 2)
                    e.filterable = false;
                entries.push_back(e);
            }
            conduit::bind_layout_desc ld;
            ld.entries = entries;
            ld.name = "mod storage";
            p.storageLayout = dev.create_bind_layout(ld);
            if (!p.storageLayout) {
                Report(plugin, "pipeline: could not create the storage bind layout");
                return false;
            }
        }
        conduit::pipeline_layout_desc pl;
        pl.sets[0] = gpu.UniformLayout();
        pl.sets[1] = pack.textureLayout;
        pl.sets[2] = p.storageLayout;
        pl.name = "mod pipeline";
        p.layout = dev.create_pipeline_layout(pl);
        if (!p.layout) {
            Report(plugin, "pipeline: could not create the pipeline layout");
            DestroyPipeline(p);
            return false;
        }
        const std::string name = d.name.empty() ? d.program : d.name;
        if (d.compute) {
            if (!program->compute) {
                Report(plugin, "pipeline: the program has no compute stage");
                DestroyPipeline(p);
                return false;
            }
            conduit::compute_pipeline_desc cd;
            cd.compute = program->compute;
            cd.layout = p.layout;
            cd.name = name.c_str();
            p.handle = dev.create_compute_pipeline(cd);
        } else {
            if (!program->vertex || !program->fragment) {
                Report(plugin, "pipeline: the program has no vertex and fragment stage");
                DestroyPipeline(p);
                return false;
            }
            conduit::graphics_pipeline_desc gd;
            gd.vertex = program->vertex;
            gd.fragment = program->fragment;
            gd.layout = p.layout;
            for (size_t i = 0; i < d.streams.size(); i++)
                gd.streams[i] = {d.streams[i].stride, d.streams[i].instance ? conduit::vertex_step::instance
                                                                            : conduit::vertex_step::vertex};
            gd.stream_count = static_cast<uint32_t>(d.streams.size());
            for (size_t i = 0; i < d.attributes.size(); i++) {
                const conduit::format af = ToFormat(d.attributes[i].format);
                if (af == conduit::format::unknown || d.attributes[i].format == 1) {
                    Report(plugin, "pipeline: a vertex attribute has an unusable format");
                    DestroyPipeline(p);
                    return false;
                }
                gd.attributes[i] = {d.attributes[i].location, d.attributes[i].stream, af, d.attributes[i].offset};
            }
            gd.attribute_count = static_cast<uint32_t>(d.attributes.size());
            gd.topology = static_cast<conduit::topology>(d.topology);
            gd.raster.cull = static_cast<conduit::cull_mode>(d.cull);
            gd.raster.front_face = static_cast<conduit::front_face>(d.frontFace);
            gd.raster.depth_bias_constant = d.depthBiasConstant;
            gd.raster.depth_bias_slope = d.depthBiasSlope;
            gd.depth_stencil.depth_test = d.depthTest;
            gd.depth_stencil.depth_write = d.depthWrite;
            gd.depth_stencil.depth_compare = static_cast<conduit::compare_op>(d.depthCompare);
            p.screenFormat = false;
            for (size_t i = 0; i < d.colorFormats.size(); i++) {
                gd.color_formats[i] = ToFormat(d.colorFormats[i]);
                p.colors[i] = gd.color_formats[i];
                p.screenFormat |= d.colorFormats[i] == 1;
                conduit::blend_target& bt = gd.blend.targets[i];
                if (i < d.blend.size()) {
                    const ModBlendDesc& b = d.blend[i];
                    bt.enable = b.enabled;
                    bt.src_color = static_cast<conduit::blend_factor>(b.srcColor);
                    bt.dst_color = static_cast<conduit::blend_factor>(b.dstColor);
                    bt.color_op = static_cast<conduit::blend_op>(b.colorOp);
                    bt.src_alpha = static_cast<conduit::blend_factor>(b.srcAlpha);
                    bt.dst_alpha = static_cast<conduit::blend_factor>(b.dstAlpha);
                    bt.alpha_op = static_cast<conduit::blend_op>(b.alphaOp);
                    bt.write_mask = static_cast<conduit::color_mask>(b.writeMask);
                }
            }
            gd.color_count = static_cast<uint32_t>(d.colorFormats.size());
            p.colorCount = gd.color_count;
            gd.depth_format = d.depthFormat ? ToFormat(d.depthFormat) : conduit::format::unknown;
            p.depth = gd.depth_format;
            gd.samples = d.samples;
            gd.name = name.c_str();
            p.handle = dev.create_graphics_pipeline(gd);
        }
        if (!p.handle) {
            Report(plugin, "pipeline: the device rejected the pipeline");
            DestroyPipeline(p);
            return false;
        }
        return true;
    }

    void ModGpu::CreatePipeline(PluginResources& res, uint32_t id, const ModPipelineDesc& desc,
                                const std::string& plugin) {
        Pipeline p;
        p.desc = desc;
        if (!BuildPipeline(res, p, plugin)) {
            p.handle = {};
        }
        res.pipelines[id] = std::move(p);
    }

    void ModGpu::CollectReads() {
        conduit::device& dev = gpu.Dev();
        for (auto it = reads.begin(); it != reads.end();) {
            if (!it->copied || dev.frame_number() < it->issuedFrame + gpu.Frames() + 1) {
                ++it;
                continue;
            }
            ModReadback rb;
            rb.plugin = it->plugin;
            rb.tag = it->tag;
            rb.id = it->id;
            dev.invalidate(it->staging, 0, it->size);
            if (const uint8_t* mapped = dev.map(it->staging)) {
                rb.ok = true;
                rb.data.assign(mapped, mapped + it->size);
            }
            ModGpuHub::Instance().PushReadback(std::move(rb));
            dev.destroy(it->staging);
            it = reads.erase(it);
        }
    }

    void ModGpu::EndFrame() {}
}
