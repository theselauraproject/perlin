#pragma once
#include "client/renderer/gpu/GpuCore.hpp"
#include "client/renderer/gpu/ModGpuHub.hpp"

#include <perlin/gpu_stream.hpp>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::gpu {
    class ModGpu {
    public:
        explicit ModGpu(Gpu& gpu);
        ~ModGpu();

        struct Screen {
            conduit::texture_handle color;
            uint32_t width = 0, height = 0;
            conduit::format format = conduit::format::bgra8_unorm;
        };

        void Start(const std::string& rendererName);
        void Shutdown();
        void BeginFrame(uint32_t screenWidth, uint32_t screenHeight);
        void Record(conduit::command_list& list, ModStage stage, const Screen* screen);
        void EndFrame();
        bool Active() const { return started; }

    private:
        struct Program {
            std::string name;
            conduit::shader_handle vertex, fragment, compute;
        };
        struct Pack {
            std::shared_ptr<const std::vector<uint8_t>> bytes;
            conduit::shader_library library;
            std::vector<Program> programs;
            conduit::bind_layout_handle textureLayout;
        };
        struct Buffer {
            conduit::buffer_handle handle;
            uint64_t size = 0;
            uint32_t usage = 0;
            conduit::state resting = conduit::state::copy_src;
            conduit::state current = conduit::state::copy_src;
        };
        struct Texture {
            conduit::texture_handle handle;
            ModTextureDesc desc;
            uint32_t width = 0, height = 0;
            conduit::format format = conduit::format::unknown;
            bool screenFormat = false;
        };
        struct Pipeline {
            conduit::pipeline_handle handle;
            conduit::pipeline_layout_handle layout;
            conduit::bind_layout_handle storageLayout;
            ModPipelineDesc desc;
            uint32_t uniformBytes = 0;
            bool compute = false;
            bool screenFormat = false;
            std::array<conduit::format, 8> colors{};
            uint32_t colorCount = 0;
            conduit::format depth = conduit::format::unknown;
            uint32_t streamCount = 0;
        };
        struct PluginResources {
            std::unordered_map<uint32_t, Pack> packs;
            std::unordered_map<uint32_t, Buffer> buffers;
            std::unordered_map<uint32_t, Texture> textures;
            std::unordered_map<uint32_t, conduit::sampler_handle> samplers;
            std::unordered_map<uint32_t, Pipeline> pipelines;
            std::set<std::string> reported;
        };
        struct PendingRead {
            std::string plugin;
            uint32_t tag = 0, id = 0;
            uint64_t size = 0;
            conduit::buffer_handle staging;
            uint64_t issuedFrame = 0;
            bool copied = false;
            uint64_t offset = 0;
        };
        struct PendingWrite {
            std::string plugin;
            uint32_t id = 0;
            uint64_t offset = 0;
            std::shared_ptr<const std::vector<uint8_t>> bytes;
        };
        struct GroupKey {
            std::vector<uint32_t> values;
            bool operator==(const GroupKey& o) const { return values == o.values; }
        };
        struct GroupKeyHash {
            size_t operator()(const GroupKey& k) const;
        };

        struct Run;
        struct Uses;

        void Apply(ModOp& op);
        void CreatePack(PluginResources& res, ModOp& op);
        void CreateBuffer(PluginResources& res, ModOp& op);
        void CreateTexture(PluginResources& res, uint32_t id, const ModTextureDesc& desc, const std::string& plugin);
        bool BuildTexture(Texture& t, uint32_t width, uint32_t height, const std::string& plugin, uint32_t id);
        void CreateSampler(PluginResources& res, ModOp& op);
        void CreatePipeline(PluginResources& res, uint32_t id, const ModPipelineDesc& desc, const std::string& plugin);
        bool BuildPipeline(PluginResources& res, Pipeline& p, const std::string& plugin);
        void DestroyPipeline(Pipeline& p);
        void DestroyTexture(PluginResources& res, Texture& t);
        void DestroyPack(Pack& p);
        void DropAll(PluginResources& res);
        void Report(const std::string& plugin, const std::string& message);
        void RunLayer(conduit::command_list& list, const ModLayer& layer, const Screen* screen);
        void Resize(uint32_t width, uint32_t height, conduit::format format);
        void FlushReads(conduit::command_list& list);
        void FlushWrites(conduit::command_list& list);
        void UseBuffer(conduit::command_list& list, const std::string& plugin, uint32_t id, Buffer& b,
                       conduit::state want);
        void CollectReads();
        conduit::format ToFormat(uint8_t format) const;
        void ForgetGroups(conduit::texture_handle texture);
        void RestoreBuffers(conduit::command_list& list);

        Gpu& gpu;
        bool started = false;
        uint32_t generation = 0;
        uint32_t screenW = 0, screenH = 0;
        conduit::format screenFormat = conduit::format::bgra8_unorm;
        std::map<std::string, PluginResources> plugins;
        std::vector<PendingRead> reads;
        std::unordered_map<GroupKey, conduit::bind_group_handle, GroupKeyHash> textureGroups;
        std::vector<conduit::bind_group_handle> frameGroups;
        std::unordered_map<GroupKey, conduit::bind_group_handle, GroupKeyHash> storageGroups;
        std::vector<std::pair<std::string, uint32_t>> touched;
        std::vector<PendingWrite> pending;
        uint64_t frameCounter = 0;
        uint64_t lastReportClear = 0;
    };
}
