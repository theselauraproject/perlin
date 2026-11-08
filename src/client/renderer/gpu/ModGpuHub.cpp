#include "client/renderer/gpu/ModGpuHub.hpp"

#include <perlin/gpu_stream.hpp>

#include <algorithm>
#include <cmath>

namespace perlin::gpu {
    namespace {
        constexpr uint8_t kFormatMax = 20;
        constexpr uint32_t kFormatBytes[kFormatMax + 1] = {0, 4, 1, 2, 4, 4, 4, 2, 4, 8, 4, 8, 12, 16, 4, 4, 4, 2, 4, 4, 8};
        constexpr size_t kMaxErrors = 32;
        constexpr size_t kMaxReadbacks = 64;
        constexpr uint64_t kMaxPackBytes = 64ull << 20;
        constexpr uint32_t kUsageBufferAll = 0x3F;
        constexpr uint32_t kUsageTextureAll = 0x3F;

        std::string LayerKey(const std::string& plugin, const std::string& layer) {
            return plugin + '\x1f' + layer;
        }
    }

    uint32_t ModFormatBytes(uint8_t format) {
        return format <= kFormatMax ? kFormatBytes[format] : 0;
    }

    bool ModFormatIsDepth(uint8_t format) {
        return format >= 17 && format <= 20;
    }

    ModGpuHub& ModGpuHub::Instance() {
        static ModGpuHub hub;
        return hub;
    }

    ModDeviceInfo ModGpuHub::Info() const {
        std::lock_guard lock(mutex);
        return info;
    }

    uint32_t ModGpuHub::Generation() const {
        std::lock_guard lock(mutex);
        return generation;
    }

    bool ModGpuHub::Queue(ModOp op) {
        if (ops.size() >= kModMaxPendingOps)
            return false;
        op.generation = generation;
        ops.push_back(std::move(op));
        return true;
    }

    HubStatus ModGpuHub::LoadPack(const std::string& plugin, const std::vector<uint8_t>& bytes, ModPackSummary& out) {
        ModDeviceInfo device;
        {
            std::lock_guard lock(mutex);
            if (!info.available)
                return HubStatus::Unavailable;
            device = info;
        }
        if (bytes.empty() || bytes.size() > kMaxPackBytes)
            return HubStatus::BadArguments;
        conduit::shader_library library;
        std::string error;
        if (!library.load(bytes, &error)) {
            Error(plugin, "shader pack: " + error);
            return HubStatus::BadArguments;
        }
        if (library.texture_slots().size() > kModTextureSlots) {
            Error(plugin, "shader pack has more than 48 texture slots");
            return HubStatus::BadArguments;
        }
        ModPackSummary summary;
        for (const conduit::texture_slot& s : library.texture_slots()) {
            if (s.slot >= kModTextureSlots) {
                Error(plugin, "shader pack texture slot " + std::to_string(s.slot) + " is out of range");
                return HubStatus::BadArguments;
            }
            summary.slots.push_back({s.name, s.slot, s.array, s.unfilterable});
        }
        for (const conduit::program_info& p : library.programs()) {
            if (p.uniform_bytes > kModMaxUniformBytes) {
                Error(plugin, "program " + p.name + " has a uniform block larger than 4096 bytes");
                return HubStatus::BadArguments;
            }
            ModProgram program;
            program.name = p.name;
            program.uniformBytes = p.uniform_bytes;
            for (const conduit::uniform_member& u : p.uniforms)
                program.uniforms.push_back({u.name, u.offset, u.bytes, u.elements});
            program.vertex = !library.code(p, conduit::shader_stage::vertex, device.target).empty();
            program.fragment = !library.code(p, conduit::shader_stage::fragment, device.target).empty();
            program.compute = !library.code(p, conduit::shader_stage::compute, device.target).empty();
            summary.programs.push_back(std::move(program));
        }
        std::lock_guard lock(mutex);
        if (!info.available)
            return HubStatus::Unavailable;
        PluginState& st = State(plugin);
        if (st.packs.size() >= kModMaxPacks)
            return HubStatus::Limit;
        summary.id = st.nextPack++;
        ModOp op;
        op.kind = ModOp::Kind::Pack;
        op.plugin = plugin;
        op.id = summary.id;
        op.bytes = std::make_shared<const std::vector<uint8_t>>(bytes);
        if (!Queue(std::move(op))) {
            st.nextPack--;
            return HubStatus::Unavailable;
        }
        st.packs[summary.id] = summary;
        out = std::move(summary);
        return HubStatus::Ok;
    }

    HubStatus ModGpuHub::CreateBuffer(const std::string& plugin, ModBufferDesc desc, uint32_t& id) {
        if (desc.size == 0)
            desc.size = desc.data.size();
        if (desc.size == 0 || desc.size > kModMaxBufferBytes || desc.data.size() > desc.size ||
            (desc.usage & ~kUsageBufferAll) != 0 || desc.usage == 0 || desc.name.size() > 64)
            return HubStatus::BadArguments;
        std::lock_guard lock(mutex);
        if (!info.available)
            return HubStatus::Unavailable;
        PluginState& st = State(plugin);
        if (st.buffers.size() >= kModMaxResources || st.bytes + desc.size > kModMaxTotalBytes)
            return HubStatus::Limit;
        ModOp op;
        op.kind = ModOp::Kind::Buffer;
        op.plugin = plugin;
        op.id = st.nextBuffer;
        const uint64_t size = desc.size;
        const uint32_t usage = desc.usage;
        op.buffer = std::move(desc);
        if (!Queue(std::move(op)))
            return HubStatus::Unavailable;
        id = st.nextBuffer++;
        st.buffers[id] = size;
        st.bufferUsage[id] = static_cast<uint8_t>(usage);
        st.bytes += size;
        return HubStatus::Ok;
    }

    HubStatus ModGpuHub::WriteBuffer(const std::string& plugin, uint32_t id, uint64_t offset, std::vector<uint8_t> data) {
        std::lock_guard lock(mutex);
        if (!info.available)
            return HubStatus::Unavailable;
        const auto p = plugins.find(plugin);
        if (p == plugins.end() || !p->second.buffers.contains(id))
            return HubStatus::NotFound;
        const uint64_t size = p->second.buffers[id];
        if (data.empty() || offset > size || data.size() > size - offset)
            return HubStatus::BadArguments;
        ModOp op;
        op.kind = ModOp::Kind::BufferWrite;
        op.plugin = plugin;
        op.id = id;
        op.offset = offset;
        op.bytes = std::make_shared<const std::vector<uint8_t>>(std::move(data));
        return Queue(std::move(op)) ? HubStatus::Ok : HubStatus::Unavailable;
    }

    HubStatus ModGpuHub::ReadBuffer(const std::string& plugin, uint32_t id, uint64_t offset, uint64_t size,
                                    uint32_t tag) {
        std::lock_guard lock(mutex);
        if (!info.available)
            return HubStatus::Unavailable;
        const auto p = plugins.find(plugin);
        if (p == plugins.end() || !p->second.buffers.contains(id))
            return HubStatus::NotFound;
        const uint64_t total = p->second.buffers[id];
        if (size == 0 || offset > total || size > total - offset || size > (64ull << 20))
            return HubStatus::BadArguments;
        ModOp op;
        op.kind = ModOp::Kind::BufferRead;
        op.plugin = plugin;
        op.id = id;
        op.offset = offset;
        op.size = size;
        op.tag = tag;
        return Queue(std::move(op)) ? HubStatus::Ok : HubStatus::Unavailable;
    }

    HubStatus ModGpuHub::CreateTexture(const std::string& plugin, ModTextureDesc desc, uint32_t& id) {
        if (desc.format == 0 || desc.format > kFormatMax || desc.dim > 3 || desc.name.size() > 64 ||
            (desc.usage & ~kUsageTextureAll) != 0 || desc.usage == 0 || desc.layers == 0 || desc.layers > 2048 ||
            desc.mips == 0 || desc.mips > 16 || (desc.samples != 1 && desc.samples != 2 && desc.samples != 4 && desc.samples != 8))
            return HubStatus::BadArguments;
        if (desc.dim == 2 && desc.layers != 6)
            return HubStatus::BadArguments;
        if (ModFormatIsDepth(desc.format) && (desc.usage & (1u << 3)) == 0 && (desc.usage & (1u << 0)) == 0)
            return HubStatus::BadArguments;
        if ((desc.usage & (1u << 3)) && !ModFormatIsDepth(desc.format))
            return HubStatus::BadArguments;
        if ((desc.usage & (1u << 2)) && ModFormatIsDepth(desc.format))
            return HubStatus::BadArguments;
        if (desc.screenSized) {
            if (!(desc.scale > 0.0f && desc.scale <= 4.0f) || desc.dim != 0 || desc.layers != 1)
                return HubStatus::BadArguments;
        } else if (desc.width == 0 || desc.height == 0 || desc.width > kModMaxTextureSide ||
                   desc.height > kModMaxTextureSide)
            return HubStatus::BadArguments;
        ModDeviceInfo device;
        {
            std::lock_guard lock(mutex);
            device = info;
        }
        const uint32_t bpp = desc.format == 1 ? 4 : ModFormatBytes(desc.format);
        uint32_t w = desc.width, h = desc.height;
        if (desc.screenSized) {
            w = std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(std::max(device.width, 1u) * desc.scale)));
            h = std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(std::max(device.height, 1u) * desc.scale)));
        }
        const uint64_t bytes =
            static_cast<uint64_t>(w) * h * desc.layers * bpp * desc.samples * (desc.mips > 1 ? 2 : 1);
        if (!desc.data.empty() && !desc.screenSized &&
            desc.data.size() != static_cast<uint64_t>(desc.width) * desc.height * bpp)
            return HubStatus::BadArguments;
        if (!desc.data.empty() && desc.screenSized)
            return HubStatus::BadArguments;
        std::lock_guard lock(mutex);
        if (!info.available)
            return HubStatus::Unavailable;
        PluginState& st = State(plugin);
        if (st.textures.size() >= kModMaxResources || st.bytes + bytes > kModMaxTotalBytes)
            return HubStatus::Limit;
        ModOp op;
        op.kind = ModOp::Kind::Texture;
        op.plugin = plugin;
        op.id = st.nextTexture;
        op.texture = std::move(desc);
        if (!Queue(std::move(op)))
            return HubStatus::Unavailable;
        id = st.nextTexture++;
        st.textures[id] = bytes;
        st.bytes += bytes;
        return HubStatus::Ok;
    }

    HubStatus ModGpuHub::WriteTexture(const std::string& plugin, uint32_t id, ModTextureWrite write) {
        std::lock_guard lock(mutex);
        if (!info.available)
            return HubStatus::Unavailable;
        const auto p = plugins.find(plugin);
        if (p == plugins.end() || !p->second.textures.contains(id))
            return HubStatus::NotFound;
        if (write.width == 0 || write.height == 0 || write.width > kModMaxTextureSide ||
            write.height > kModMaxTextureSide || write.data.empty() || write.data.size() > kModMaxBufferBytes)
            return HubStatus::BadArguments;
        ModOp op;
        op.kind = ModOp::Kind::TextureWrite;
        op.plugin = plugin;
        op.id = id;
        op.textureWrite = std::move(write);
        return Queue(std::move(op)) ? HubStatus::Ok : HubStatus::Unavailable;
    }

    HubStatus ModGpuHub::CreateSampler(const std::string& plugin, const ModSamplerDesc& desc, uint32_t& id) {
        if (desc.min > 1 || desc.mag > 1 || desc.mip > 1 || desc.u > 3 || desc.v > 3 || desc.w > 3 || desc.compareOp > 7 ||
            !std::isfinite(desc.anisotropy) || !std::isfinite(desc.lodBias) || !std::isfinite(desc.minLod) ||
            !std::isfinite(desc.maxLod) || desc.anisotropy < 1.0f || desc.anisotropy > 16.0f)
            return HubStatus::BadArguments;
        std::lock_guard lock(mutex);
        if (!info.available)
            return HubStatus::Unavailable;
        PluginState& st = State(plugin);
        if (st.samplers.size() >= kModMaxResources)
            return HubStatus::Limit;
        ModOp op;
        op.kind = ModOp::Kind::Sampler;
        op.plugin = plugin;
        op.id = st.nextSampler;
        op.sampler = desc;
        if (!Queue(std::move(op)))
            return HubStatus::Unavailable;
        id = st.nextSampler++;
        st.samplers.insert(id);
        return HubStatus::Ok;
    }

    HubStatus ModGpuHub::CreatePipeline(const std::string& plugin, ModPipelineDesc desc, uint32_t& id) {
        if (desc.streams.size() > 8 || desc.attributes.size() > 16 || desc.blend.size() > 8 ||
            desc.colorFormats.size() > 8 || desc.topology > 3 || desc.cull > 2 || desc.frontFace > 1 ||
            desc.depthCompare > 7 || desc.depthFormat > kFormatMax || desc.name.size() > 64 ||
            desc.storage.size() > 16 || !std::isfinite(desc.depthBiasConstant) || !std::isfinite(desc.depthBiasSlope) ||
            (desc.samples != 1 && desc.samples != 2 && desc.samples != 4 && desc.samples != 8))
            return HubStatus::BadArguments;
        for (uint8_t f : desc.colorFormats)
            if (f == 0 || f > kFormatMax || ModFormatIsDepth(f))
                return HubStatus::BadArguments;
        if (desc.depthFormat != 0 && !ModFormatIsDepth(desc.depthFormat))
            return HubStatus::BadArguments;
        for (const ModAttributeDesc& a : desc.attributes)
            if (a.stream >= desc.streams.size() || a.format == 0 || a.format > kFormatMax || ModFormatIsDepth(a.format) ||
                a.location >= 16)
                return HubStatus::BadArguments;
        for (const ModStreamDesc& s : desc.streams)
            if (s.stride == 0 || s.stride > 2048)
                return HubStatus::BadArguments;
        for (const ModBlendDesc& b : desc.blend)
            if (b.srcColor > 10 || b.dstColor > 10 || b.srcAlpha > 10 || b.dstAlpha > 10 || b.colorOp > 4 ||
                b.alphaOp > 4 || b.writeMask > 15)
                return HubStatus::BadArguments;
        for (const ModStorageBinding& s : desc.storage)
            if (s.kind > 2 || s.binding > 15)
                return HubStatus::BadArguments;
        if (desc.colorFormats.empty() && !desc.compute)
            desc.colorFormats.push_back(1);
        std::lock_guard lock(mutex);
        if (!info.available)
            return HubStatus::Unavailable;
        PluginState& st = State(plugin);
        const auto pack = st.packs.find(desc.pack);
        if (pack == st.packs.end())
            return HubStatus::NotFound;
        const ModProgram* program = nullptr;
        for (const ModProgram& p : pack->second.programs)
            if (p.name == desc.program)
                program = &p;
        if (!program) {
            State(plugin).errors.push_back("pipeline: no program named " + desc.program);
            return HubStatus::NotFound;
        }
        if (desc.compute ? !program->compute : !(program->vertex && program->fragment)) {
            if (st.errors.size() < kMaxErrors)
                st.errors.push_back("pipeline: program " + desc.program + " has no " +
                                    (desc.compute ? "compute" : "vertex and fragment") + " stage for this renderer");
            return HubStatus::BadArguments;
        }
        if (desc.compute && !info.compute)
            return HubStatus::Unavailable;
        if (st.pipelines.size() >= kModMaxResources)
            return HubStatus::Limit;
        ModOp op;
        op.kind = ModOp::Kind::Pipeline;
        op.plugin = plugin;
        op.id = st.nextPipeline;
        op.pipeline = std::move(desc);
        if (!Queue(std::move(op)))
            return HubStatus::Unavailable;
        id = st.nextPipeline++;
        st.pipelines.insert(id);
        return HubStatus::Ok;
    }

    HubStatus ModGpuHub::Destroy(const std::string& plugin, ModResource kind, uint32_t id) {
        std::lock_guard lock(mutex);
        const auto p = plugins.find(plugin);
        if (p == plugins.end())
            return HubStatus::NotFound;
        PluginState& st = p->second;
        switch (kind) {
        case ModResource::Pack:
            if (!st.packs.erase(id))
                return HubStatus::NotFound;
            break;
        case ModResource::Buffer: {
            const auto it = st.buffers.find(id);
            if (it == st.buffers.end())
                return HubStatus::NotFound;
            st.bytes -= std::min(st.bytes, it->second);
            st.buffers.erase(it);
            st.bufferUsage.erase(id);
            break;
        }
        case ModResource::Texture: {
            const auto it = st.textures.find(id);
            if (it == st.textures.end())
                return HubStatus::NotFound;
            st.bytes -= std::min(st.bytes, it->second);
            st.textures.erase(it);
            break;
        }
        case ModResource::Sampler:
            if (!st.samplers.erase(id))
                return HubStatus::NotFound;
            break;
        case ModResource::Pipeline:
            if (!st.pipelines.erase(id))
                return HubStatus::NotFound;
            break;
        default:
            return HubStatus::BadArguments;
        }
        ModOp op;
        op.kind = ModOp::Kind::Destroy;
        op.plugin = plugin;
        op.id = id;
        op.resource = kind;
        Queue(std::move(op));
        return HubStatus::Ok;
    }

    HubStatus ModGpuHub::Submit(const std::string& plugin, const std::string& layer, ModStage stage, int32_t z,
                                bool once, std::vector<uint8_t> stream) {
        if (layer.empty() || layer.size() > 64 || stream.size() > kModMaxStreamBytes ||
            static_cast<uint8_t>(stage) > 2)
            return HubStatus::BadArguments;
        stream::Reader reader(stream);
        if (!reader.valid())
            return HubStatus::BadArguments;
        std::lock_guard lock(mutex);
        if (!info.available)
            return HubStatus::Unavailable;
        const std::string key = LayerKey(plugin, layer);
        if (!layers.contains(key)) {
            uint32_t mine = 0;
            for (const auto& l : layers)
                mine += l.second.plugin == plugin ? 1 : 0;
            if (mine >= kModMaxLayers)
                return HubStatus::Limit;
        }
        ModLayer l;
        l.plugin = plugin;
        l.name = layer;
        l.stage = stage;
        l.z = z;
        l.once = once;
        l.stream = std::make_shared<const std::vector<uint8_t>>(std::move(stream));
        layers[key] = std::move(l);
        State(plugin);
        return HubStatus::Ok;
    }

    HubStatus ModGpuHub::ClearLayer(const std::string& plugin, const std::string& layer) {
        std::lock_guard lock(mutex);
        return layers.erase(LayerKey(plugin, layer)) ? HubStatus::Ok : HubStatus::NotFound;
    }

    std::vector<std::string> ModGpuHub::TakeErrors(const std::string& plugin) {
        std::lock_guard lock(mutex);
        const auto p = plugins.find(plugin);
        if (p == plugins.end())
            return {};
        std::vector<std::string> out;
        out.swap(p->second.errors);
        return out;
    }

    void ModGpuHub::Error(const std::string& plugin, std::string message) {
        std::lock_guard lock(mutex);
        PluginState& st = State(plugin);
        if (st.errors.size() >= kMaxErrors)
            st.errors.erase(st.errors.begin());
        if (message.size() > 300)
            message.resize(300);
        st.errors.push_back(std::move(message));
    }

    void ModGpuHub::PushReadback(ModReadback readback) {
        std::lock_guard lock(mutex);
        if (readbacks.size() >= kMaxReadbacks)
            readbacks.erase(readbacks.begin());
        readbacks.push_back(std::move(readback));
    }

    void ModGpuHub::TakeReadbacks(std::vector<ModReadback>& out) {
        std::lock_guard lock(mutex);
        for (ModReadback& r : readbacks)
            out.push_back(std::move(r));
        readbacks.clear();
    }

    void ModGpuHub::TakeResets(std::vector<std::pair<std::string, uint32_t>>& out) {
        std::lock_guard lock(mutex);
        for (auto& r : resets)
            out.push_back(std::move(r));
        resets.clear();
    }

    void ModGpuHub::ForgetLocked(const std::string& plugin) {
        for (auto it = layers.begin(); it != layers.end();)
            it = it->second.plugin == plugin ? layers.erase(it) : std::next(it);
        plugins.erase(plugin);
    }

    void ModGpuHub::DropPlugin(const std::string& plugin) {
        std::lock_guard lock(mutex);
        if (!plugins.contains(plugin) && std::none_of(layers.begin(), layers.end(), [&](const auto& l) {
                return l.second.plugin == plugin;
            }))
            return;
        ForgetLocked(plugin);
        ModOp op;
        op.kind = ModOp::Kind::DropPlugin;
        op.plugin = plugin;
        op.generation = generation;
        ops.push_back(std::move(op));
    }

    bool ModGpuHub::HasPlugin(const std::string& plugin) const {
        std::lock_guard lock(mutex);
        return plugins.contains(plugin);
    }

    std::vector<std::string> ModGpuHub::Plugins() const {
        std::lock_guard lock(mutex);
        std::vector<std::string> out;
        for (const auto& p : plugins)
            out.push_back(p.first);
        for (const auto& l : layers)
            if (!plugins.contains(l.second.plugin))
                out.push_back(l.second.plugin);
        return out;
    }

    void ModGpuHub::RendererUp(const ModDeviceInfo& device) {
        std::lock_guard lock(mutex);
        info = device;
        info.available = true;
    }

    void ModGpuHub::RendererDown() {
        std::lock_guard lock(mutex);
        generation++;
        for (const auto& p : plugins)
            resets.emplace_back(p.first, generation);
        plugins.clear();
        layers.clear();
        ops.clear();
        readbacks.clear();
        info.available = false;
    }

    void ModGpuHub::SetScreen(uint32_t width, uint32_t height, uint8_t targetFormat) {
        std::lock_guard lock(mutex);
        info.width = width;
        info.height = height;
        info.targetFormat = targetFormat;
    }

    void ModGpuHub::Drain(std::vector<ModOp>& out) {
        std::lock_guard lock(mutex);
        out.swap(ops);
        ops.clear();
    }

    bool ModGpuHub::AnyLayers() const {
        std::lock_guard lock(mutex);
        return !layers.empty();
    }

    void ModGpuHub::Layers(ModStage stage, std::vector<ModLayer>& out) {
        std::lock_guard lock(mutex);
        for (auto it = layers.begin(); it != layers.end();) {
            if (it->second.stage != stage) {
                ++it;
                continue;
            }
            out.push_back(it->second);
            it = it->second.once ? layers.erase(it) : std::next(it);
        }
        std::stable_sort(out.begin(), out.end(), [](const ModLayer& a, const ModLayer& b) {
            if (a.z != b.z)
                return a.z < b.z;
            if (a.plugin != b.plugin)
                return a.plugin < b.plugin;
            return a.name < b.name;
        });
    }
}
