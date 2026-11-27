// CPU visibility/submission benchmark; the null backend does not measure GPU rasterization.
#ifndef VISIBILITY_CANDIDATE
#define VISIBILITY_CANDIDATE 1
#endif

#include "client/systems/renderer/gpu/ChunkMeshStore.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace perlin::gpu {
    struct ChunkMeshStoreTestAccess {
        static size_t Capacity(const ChunkMeshStore& store) {
            size_t bytes = 0;
#if VISIBILITY_CANDIDATE
            for (const auto& selection : store.selections) {
                bytes += selection.visible.capacity() * sizeof(ChunkMeshStore::Visible);
                bytes += selection.sections.capacity() * sizeof(const ChunkMeshStore::Section*);
                for (const auto& run : selection.runs)
                    bytes += run.runs.capacity() * sizeof(ChunkMeshStore::Run);
            }
#else
            bytes = store.visible.capacity() * sizeof(ChunkMeshStore::Visible) +
                    store.visibleSections.capacity() * sizeof(const ChunkMeshStore::Section*);
#endif
            return bytes;
        }
    };
}

namespace {
    using namespace perlin;
    void Mesh(Tessellator& tess, int copies) {
        tess.Begin();
        for (int sy = 0; sy < 8; sy++) {
            tess.EnterSection(sy);
            tess.AddVisibility(sy, 0x7FFF);
            for (int layer = 0; layer < kRenderLayerCount; layer++) {
                tess.SetLayer(static_cast<RenderLayer>(layer));
                for (int i = 0; i < copies; i++) {
                    const float x = static_cast<float>(i * 2), y = static_cast<float>(sy * 16 + 1);
                    const float z = static_cast<float>(layer * 2);
                    const float positions[4][3] = {{x,y,z},{x+1,y,z},{x+1,y+1,z},{x,y+1,z}};
                    const float colors[4][3] = {{1,1,1},{1,1,1},{1,1,1},{1,1,1}};
                    const float uv[4][2] = {{0,0},{1,0},{1,1},{0,1}};
                    tess.AddQuad(positions, colors, uv, false, 0.f, 15.f);
                }
            }
        }
        tess.End();
    }
}

int main(int argc, char** argv) {
    using namespace perlin;
    if (argc < 2) return 2;
    std::ifstream file(argv[1], std::ios::binary);
    const std::vector<uint8_t> pack{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    gpu::GpuSettings settings;
    settings.api = conduit::api_from_id("null");
    settings.width = settings.height = 64;
    settings.offscreen = true;
    gpu::Gpu gpu;
    std::string error;
    if (!gpu.Init(settings, pack, &error)) {
        std::printf("FAIL %s\n", error.c_str());
        return 1;
    }
    const int radius = argc > 2 ? std::atoi(argv[2]) : 24;
    const int frames = argc > 3 ? std::atoi(argv[3]) : 500;
    Tessellator meshes[2];
    Mesh(meshes[0], 1);
    Mesh(meshes[1], 2);
    uint64_t checksum = 0, version = 1;
    for (int mode = 0; mode < 3; mode++) {
        gpu::ChunkMeshStore store;
        store.Init(gpu);
        for (int z = -radius; z <= radius; z++)
            for (int x = -radius; x <= radius; x++)
                store.Update(false, x, z, 16, meshes[0], version++);
        std::vector<gpu::ChunkMeshStore::Run> runs;
        const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < frames; frame++) {
            const glm::vec3 eye(.23f + (mode == 1 ? frame * .07f : 0.f), 24.5f,
                                .37f + (mode == 1 ? frame * -.03f : 0.f));
            const auto projection = glm::perspectiveLH_ZO(glm::radians(70.f), 16.f / 9.f, .05f, 4096.f);
            const auto view = glm::lookAtLH(eye, eye + glm::vec3(1, .05f, 1), glm::vec3(0,1,0));
            const auto frustum = Frustum::FromViewProjection(projection * view);
            if (mode == 2)
                store.Update(false, 0, 0, 16, meshes[frame & 1], version++);
            store.Select(frustum, eye.x, eye.y, eye.z, radius, false, frame / 60.0);
            for (int layer = 0; layer < kRenderLayerCount; layer++) {
                store.CollectRuns(static_cast<RenderLayer>(layer), layer >= 3, runs);
                for (const auto& run : runs)
                    checksum += run.first + run.count + run.page;
            }
        }
        const double ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now() - start).count();
        const size_t retained = gpu::ChunkMeshStoreTestAccess::Capacity(store);
        store.Clear();
        std::printf("BENCH %s chunks=%d frames=%d total_ms=%.3f per_frame_us=%.3f cache_bytes=%zu reset_bytes=%zu\n",
                    mode == 0 ? "stationary" : mode == 1 ? "moving" : "mesh_churn", (2*radius+1)*(2*radius+1), frames,
                    ms, ms * 1000 / frames, retained, gpu::ChunkMeshStoreTestAccess::Capacity(store));
        store.Shutdown();
    }
    gpu.Shutdown();
    std::printf("BENCH checksum=%llu\n", static_cast<unsigned long long>(checksum));
}
