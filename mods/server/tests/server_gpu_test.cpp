// the gpu terrain backend against the cpu reference, on whatever graphics device the machine has
// (software vulkan counts: point VK_ICD_FILENAMES at one to run this on a machine without a gpu)
#include "test_util.hpp"

#include "gpu/GpuTerrain.hpp"
#include "world/Blocks.hpp"

#include <chrono>

using namespace perlin::server;

int main() {
    ServerConfig config;
    config.generator = "flat";
    // "gpu" accepts a software device; "auto" would not
    config.terrainBackend = "gpu";
    config.seed = 0x1234567890abcdefull;
    config.flatLayers = {{"minecraft:bedrock", 1},
                         {"minecraft:stone", 5},
                         {"minecraft:dirt", 3},
                         {"minecraft:grass_block", 1}};
    std::string note;
    std::unique_ptr<TerrainBackend> gpu = CreateGpuTerrainBackend(config, note);
    if (!gpu) {
        std::printf("server_gpu_test skipped: %s\n", note.c_str());
        return 0;
    }
    std::printf("backend: %s\n", gpu->Name());
    FlatCpuBackend cpu(config);

    std::string detail;
    CHECK(BackendsAgree(*gpu, cpu, detail));
    if (!detail.empty())
        std::fprintf(stderr, "differs: %s\n", detail.c_str());

    // more than one batch, negative coordinates, a batch of one, the whole column compared block by block
    std::vector<ChunkPos> positions;
    for (int x = -5; x <= 6; x++)
        for (int z = -4; z <= 4; z++)
            positions.push_back({x, z});
    std::vector<ChunkColumn> a(positions.size()), b(positions.size());
    const auto t0 = std::chrono::steady_clock::now();
    gpu->Generate(positions, a);
    const auto t1 = std::chrono::steady_clock::now();
    cpu.Generate(positions, b);
    const auto t2 = std::chrono::steady_clock::now();
    size_t wrong = 0;
    for (size_t n = 0; n < positions.size(); n++)
        for (int y = kMinY; y < kMinY + kHeight; y++)
            for (int z = 0; z < 16; z++)
                for (int x = 0; x < 16; x++)
                    if (a[n].Get(x, y, z) != b[n].Get(x, y, z))
                        wrong++;
    CHECK(wrong == 0);
    std::printf("%zu chunks: gpu %.1f ms, cpu %.1f ms\n", positions.size(),
                std::chrono::duration<double, std::milli>(t1 - t0).count(),
                std::chrono::duration<double, std::milli>(t2 - t1).count());

    std::vector<ChunkPos> one = {{7, 7}};
    std::vector<ChunkColumn> single(1);
    gpu->Generate(one, single);
    CHECK(single[0].Get(3, -64, 3) == BlockRegistry::Instance().Find("minecraft:bedrock"));
    CHECK(single[0].Get(3, -59, 3) == BlockRegistry::Instance().Find("minecraft:stone"));
    CHECK(single[0].Get(3, -55, 3) == BlockRegistry::Instance().Find("minecraft:grass_block"));
    CHECK(single[0].Get(3, -54, 3) == 0);

    if (g_failed) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::puts("server_gpu_test ok");
    return 0;
}
