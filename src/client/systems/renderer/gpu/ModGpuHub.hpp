#pragma once
#include <conduit/shader_library.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::gpu {
    enum class HubStatus : uint8_t { Ok, BadArguments, Unavailable, NotFound, Limit };

    enum class ModStage : uint8_t { Pre = 0, UnderHud = 1, Overlay = 2 };
    enum class ModResource : uint8_t { Pack = 0, Buffer = 1, Texture = 2, Sampler = 3, Pipeline = 4 };

    constexpr uint32_t kModMaxPacks = 8;
    constexpr uint32_t kModMaxResources = 4096;
    constexpr uint32_t kModMaxTextureSide = 8192;
    constexpr uint64_t kModMaxBufferBytes = 268435456;
    constexpr uint64_t kModMaxTotalBytes = 1073741824;
    constexpr uint32_t kModMaxLayers = 16;
    constexpr uint32_t kModMaxStreamBytes = 8388608;
    constexpr uint32_t kModMaxUniformBytes = 4096;
    constexpr uint32_t kModTextureSlots = 48;
    constexpr uint32_t kModMaxPendingOps = 8192;

    struct ModDeviceInfo {
        bool available = false;
        std::string api, adapter, vendor;
        bool discrete = false;
        uint8_t targetFormat = 6;
        uint32_t width = 0, height = 0;
        uint32_t maxTexture2d = 0, maxTextureLayers = 0, maxMsaa = 1, maxColorTargets = 1;
        uint64_t maxBufferBytes = 0;
        bool compute = false, multiDrawIndirect = false, float16 = false, int64 = false, subgroups = false,
             sceneCopy = false;
        std::string shaderTarget;
        conduit::shader_target target = conduit::shader_target::spirv;
    };

    struct ModUniform {
        std::string name;
        uint32_t offset = 0, bytes = 0, elements = 1;
    };
    struct ModProgram {
        std::string name;
        uint32_t uniformBytes = 0;
        std::vector<ModUniform> uniforms;
        bool vertex = false, fragment = false, compute = false;
    };
    struct ModSlot {
        std::string name;
        uint32_t slot = 0;
        bool array = false, unfilterable = false;
    };
    struct ModPackSummary {
        uint32_t id = 0;
        std::vector<ModProgram> programs;
        std::vector<ModSlot> slots;
    };

    struct ModBufferDesc {
        uint64_t size = 0;
        uint32_t usage = 0;
        std::vector<uint8_t> data;
        std::string name;
    };

    struct ModTextureDesc {
        uint32_t width = 1, height = 1, layers = 1, mips = 1, samples = 1;
        uint8_t format = 4;
        uint32_t usage = 0;
        uint8_t dim = 0;
        bool screenSized = false;
        float scale = 1.0f;
        std::vector<uint8_t> data;
        std::string name;
    };

    struct ModTextureWrite {
        uint32_t layer = 0, mip = 0, x = 0, y = 0, width = 0, height = 0;
        std::vector<uint8_t> data;
    };

    struct ModSamplerDesc {
        uint8_t min = 1, mag = 1, mip = 1, u = 2, v = 2, w = 2;
        float anisotropy = 1, lodBias = 0, minLod = 0, maxLod = 1000;
        bool compare = false;
        uint8_t compareOp = 3;
        uint32_t border = 0;
    };

    struct ModStreamDesc {
        uint32_t stride = 0;
        bool instance = false;
    };
    struct ModAttributeDesc {
        uint32_t location = 0, stream = 0;
        uint8_t format = 13;
        uint32_t offset = 0;
    };
    struct ModStorageBinding {
        uint32_t binding = 0;
        uint8_t kind = 0;
    };
    struct ModBlendDesc {
        bool enabled = false;
        uint8_t srcColor = 1, dstColor = 0, colorOp = 0, srcAlpha = 1, dstAlpha = 0, alphaOp = 0;
        uint32_t writeMask = 15;
    };

    struct ModPipelineDesc {
        uint32_t pack = 0;
        std::string program;
        std::vector<ModStreamDesc> streams;
        std::vector<ModAttributeDesc> attributes;
        uint8_t topology = 0, cull = 0, frontFace = 0;
        float depthBiasConstant = 0, depthBiasSlope = 0;
        bool depthTest = false, depthWrite = false;
        uint8_t depthCompare = 3;
        std::vector<ModBlendDesc> blend;
        std::vector<uint8_t> colorFormats;
        uint8_t depthFormat = 0;
        uint32_t samples = 1;
        std::vector<ModStorageBinding> storage;
        std::string name;
        bool compute = false;
    };

    struct ModOp {
        enum class Kind : uint8_t {
            Pack,
            Buffer,
            BufferWrite,
            BufferRead,
            Texture,
            TextureWrite,
            Sampler,
            Pipeline,
            Destroy,
            DropPlugin
        } kind = Kind::DropPlugin;
        std::string plugin;
        uint32_t id = 0;
        uint32_t generation = 0;
        ModResource resource = ModResource::Buffer;
        std::shared_ptr<const std::vector<uint8_t>> bytes;
        uint64_t offset = 0, size = 0;
        uint32_t tag = 0;
        ModBufferDesc buffer;
        ModTextureDesc texture;
        ModTextureWrite textureWrite;
        ModSamplerDesc sampler;
        ModPipelineDesc pipeline;
    };

    struct ModLayer {
        std::string plugin, name;
        ModStage stage = ModStage::Overlay;
        int32_t z = 0;
        bool once = false;
        std::shared_ptr<const std::vector<uint8_t>> stream;
    };

    struct ModReadback {
        std::string plugin;
        uint32_t tag = 0, id = 0;
        bool ok = false;
        std::vector<uint8_t> data;
    };

    class ModGpuHub {
    public:
        static ModGpuHub& Instance();

        ModDeviceInfo Info() const;

        HubStatus LoadPack(const std::string& plugin, const std::vector<uint8_t>& bytes, ModPackSummary& out);
        HubStatus CreateBuffer(const std::string& plugin, ModBufferDesc desc, uint32_t& id);
        HubStatus WriteBuffer(const std::string& plugin, uint32_t id, uint64_t offset, std::vector<uint8_t> data);
        HubStatus ReadBuffer(const std::string& plugin, uint32_t id, uint64_t offset, uint64_t size, uint32_t tag);
        HubStatus CreateTexture(const std::string& plugin, ModTextureDesc desc, uint32_t& id);
        HubStatus WriteTexture(const std::string& plugin, uint32_t id, ModTextureWrite write);
        HubStatus CreateSampler(const std::string& plugin, const ModSamplerDesc& desc, uint32_t& id);
        HubStatus CreatePipeline(const std::string& plugin, ModPipelineDesc desc, uint32_t& id);
        HubStatus Destroy(const std::string& plugin, ModResource kind, uint32_t id);
        HubStatus Submit(const std::string& plugin, const std::string& layer, ModStage stage, int32_t z, bool once,
                         std::vector<uint8_t> stream);
        HubStatus ClearLayer(const std::string& plugin, const std::string& layer);

        std::vector<std::string> TakeErrors(const std::string& plugin);
        void TakeReadbacks(std::vector<ModReadback>& out);
        void TakeResets(std::vector<std::pair<std::string, uint32_t>>& out);
        void DropPlugin(const std::string& plugin);
        bool HasPlugin(const std::string& plugin) const;
        std::vector<std::string> Plugins() const;

        void RendererUp(const ModDeviceInfo& info);
        void RendererDown();
        void SetScreen(uint32_t width, uint32_t height, uint8_t targetFormat);
        uint32_t Generation() const;
        void Drain(std::vector<ModOp>& out);
        void Layers(ModStage stage, std::vector<ModLayer>& out);
        void Error(const std::string& plugin, std::string message);
        void PushReadback(ModReadback readback);
        bool AnyLayers() const;

    private:
        struct PluginState {
            uint32_t nextPack = 1, nextBuffer = 1, nextTexture = 1, nextSampler = 1, nextPipeline = 1;
            std::unordered_map<uint32_t, ModPackSummary> packs;
            std::unordered_map<uint32_t, uint64_t> buffers, textures;
            std::set<uint32_t> samplers, pipelines;
            std::unordered_map<uint32_t, uint8_t> bufferUsage;
            uint64_t bytes = 0;
            std::vector<std::string> errors;
        };

        PluginState& State(const std::string& plugin) { return plugins[plugin]; }
        bool Queue(ModOp op);
        void ForgetLocked(const std::string& plugin);

        mutable std::mutex mutex;
        ModDeviceInfo info;
        uint32_t generation = 1;
        std::map<std::string, PluginState> plugins;
        std::vector<ModOp> ops;
        std::map<std::string, ModLayer> layers;
        std::vector<ModReadback> readbacks;
        std::vector<std::pair<std::string, uint32_t>> resets;
    };

    uint32_t ModFormatBytes(uint8_t format);
    bool ModFormatIsDepth(uint8_t format);
}
