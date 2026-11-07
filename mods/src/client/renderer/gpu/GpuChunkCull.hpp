#pragma once
#include "client/renderer/Frustum.hpp"
#include "client/renderer/gpu/ChunkMeshStore.hpp"
#include "client/renderer/gpu/GpuCore.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace perlin::gpu {

    class GpuChunkCull {
    public:
        static constexpr std::array<RenderLayer, 3> kLayers = {RenderLayer::Solid, RenderLayer::Cutout, RenderLayer::CutoutCull};

        struct Bucket {
            uint32_t page, firstCommand, commands;
        };

        bool Init(Gpu& gpu);
        void Shutdown();
        bool Ready() const { return ready; }

        bool Prepare(const ChunkMeshStore& meshes, const Frustum& frustum, float camX, float camZ, int cullRadius);
        void Record(conduit::command_list& list);
        const std::vector<Bucket>& Buckets(RenderLayer layer) const;
        conduit::buffer_handle Commands() const { return commands; }
        static bool Handles(RenderLayer layer);

    private:
        struct Params {
            float planes[6][4];
            float camera[4];
            uint32_t recordCount;
            int32_t cullRadius;
            int32_t cameraChunkX;
            int32_t cameraChunkZ;
        };
        bool Grow(uint64_t records);

        Gpu* gpu = nullptr;
        bool ready = false;
        conduit::shader_handle shader;
        conduit::bind_layout_handle bindLayout;
        conduit::pipeline_layout_handle pipelineLayout;
        conduit::pipeline_handle pipeline;
        conduit::buffer_handle recordBuffer, commands;
        conduit::bind_group_handle group;
        uint64_t capacity = 0;
        std::vector<ChunkMeshStore::CullRecord> records;
        std::array<std::vector<Bucket>, 3> buckets;
        uint64_t builtGeneration = ~0ull, builtCave = ~0ull;
        bool uploadPending = false, dispatchPending = false;
        RingSlice upload;
        uint64_t uploadBytes = 0;
        Params params{};
    };
}
