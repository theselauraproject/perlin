// the gpu density kernel against the cpu graph: bit-identical densities, identical blocks, and a speed figure; (the second half needs a gpu with 64-bit shader floats)
#include "core/Config.hpp"
#include "gpu/GpuTerrain.hpp"
#include "gpu/VanillaGpuProgram.hpp"
#include "world/vanilla/VanillaSurface.hpp"
#include "test_util.hpp"
#include "world/BlockCatalog.hpp"
#include "world/vanilla/VanillaDimension.hpp"
#include "world/vanilla/VanillaTerrain.hpp"

#include <chrono>
#include <cstring>

using namespace perlin::server;

static double Ms(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}

static void TestProgram() {
    std::string error;
    ServerConfig config;
    config.seed = 12345;
    auto cpu = vanilla::VanillaCpuBackend::Create(config, error);
    CHECK(cpu != nullptr);
    if (!cpu)
        return;
    auto program = vanilla::GpuProgram::Build(cpu->FinalDensity(), cpu->Climate().Roots(), error);
    CHECK(program != nullptr);
    if (!program) {
        std::printf("program: %s\n", error.c_str());
        return;
    }
    std::printf("graph: %u nodes (%u at corners, %u per block, %zu for the climate), %zu interpolated "
                "inputs, %zu noise layers\n",
                program->nodeCount, program->innerCount, program->outerCount, program->climateOrder.size(),
                program->interpolatedInputs.size(),
                program->layers.size() / vanilla::GpuProgram::kLayerStride);
    CHECK(program->nodeCount <= vanilla::GpuProgram::kMaxNodes);
    CHECK(program->innerCount + program->outerCount == program->order.size());
    CHECK(!program->climateOrder.empty());
    // the packed buffer: header offsets ascend, and the biome table and materials sit where the header says
    const std::vector<uint32_t> packed = vanilla::PackTables(*program, cpu->Surface());
    for (int i = 1; i <= vanilla::GpuProgram::HMaterials; i++)
        CHECK(packed[static_cast<size_t>(i)] >= packed[static_cast<size_t>(i) - 1]);
    CHECK(packed[packed[vanilla::GpuProgram::HBiomes]] == static_cast<uint32_t>(vanilla::kBiomePointCount));
    CHECK(packed.size() ==
          packed[vanilla::GpuProgram::HMaterials] + 8 + static_cast<size_t>(vanilla::kBiomeCount) * 8);
    // nothing in the block phase may read a node only the corner phase computes
    std::vector<char> inOuter(program->nodeCount, 0);
    for (uint32_t i = 0; i < program->outerCount; i++)
        inOuter[program->order[program->innerCount + i]] = 1;
    CHECK(inOuter[program->root]);
}

static bool Gpu(int64_t seed, bool& ran) {
    ServerConfig config;
    config.generator = "vanilla";
    config.seed = static_cast<uint64_t>(seed);
    config.terrainBackend = "gpu";
    std::string note;
    std::unique_ptr<TerrainBackend> gpu = CreateGpuVanillaBackend(config, note);
    if (!gpu) {
        std::printf("no gpu path: %s\n", note.c_str());
        // the gate refusing the device is a result, not a skip
        if (note.contains("differs")) {
            std::fprintf(stderr, "FAIL gate: %s\n", note.c_str());
            g_failed++;
        }
        return false;
    }
    ran = true;
    std::printf("seed %lld: %s\n", static_cast<long long>(seed), note.c_str());
    std::string error;
    auto cpu = vanilla::VanillaCpuBackend::Create(config, error);
    CHECK(cpu != nullptr);
    if (!cpu)
        return true;

    const std::vector<ChunkPos> positions = {{0, 0},  {1, 0},    {0, 1},     {-1, -1},
                                             {5, -7}, {-30, 12}, {123, 456}, {-777, 31}};
    auto* density = dynamic_cast<DensityBackend*>(gpu.get());
    CHECK(density != nullptr);
    if (density) {
        std::vector<float> d;
        density->Density(positions, d);
        size_t bad = 0, checked = 0;
        for (size_t n = 0; n < positions.size(); n++) {
            vanilla::DfCacheScope cache;
            for (int s = 0; s < 6000; s++) {
                const int x = (s * 7) & 15, z = (s * 13 + s / 16) & 15,
                          y = (s * 37 + 11 * static_cast<int>(n)) % kHeight;
                const float want = cpu->Density(positions[n].x * 16 + x, kMinY + y, positions[n].z * 16 + z);
                const float got = d[n * 16 * 16 * kHeight + static_cast<size_t>((x * 16 + z) * kHeight + y)];
                checked++;
                if (std::memcmp(&want, &got, 4) != 0 && !(want != want && got != got)) {
                    if (bad++ < 3)
                        std::fprintf(stderr,
                                     "density differs chunk (%d,%d) block (%d,%d,%d): cpu %.9g gpu %.9g\n",
                                     positions[n].x, positions[n].z, x, kMinY + y, z, want, got);
                }
            }
        }
        std::printf("density: %zu positions compared, %zu differ\n", checked, bad);
        CHECK(bad == 0);
    }

    // whole columns, block for block
    std::vector<ChunkColumn> a(positions.size()), b(positions.size());
    auto t0 = std::chrono::steady_clock::now();
    gpu->Generate(positions, a);
    const double gpuMs = Ms(t0);
    t0 = std::chrono::steady_clock::now();
    cpu->Generate(positions, b);
    const double cpuMs = Ms(t0);
    size_t differing = 0;
    for (size_t n = 0; n < positions.size(); n++)
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = kMinY; y < kMinY + kHeight; y++)
                    if (a[n].Get(x, y, z) != b[n].Get(x, y, z)) {
                        if (differing++ < 6)
                            std::fprintf(stderr, "differs chunk (%d,%d) at %d %d %d: gpu %s cpu %s\n", positions[n].x, positions[n].z, x, y, z,
                                         BlockRegistry::Instance().Name(a[n].Get(x, y, z)).c_str(), BlockRegistry::Instance().Name(b[n].Get(x, y, z)).c_str());
                    }
    std::printf("%zu chunks: gpu %.0f ms, cpu %.0f ms (one thread); %zu blocks differ\n", positions.size(),
                gpuMs, cpuMs, differing);
    CHECK(differing == 0);

    // a bigger batch than one submit holds, to cover the split
    std::vector<ChunkPos> many;
    for (int i = 0; i < 40; i++)
        many.push_back({i - 20, (i * 7) % 11 - 5});
    std::vector<ChunkColumn> big(many.size()), ref(many.size());
    t0 = std::chrono::steady_clock::now();
    gpu->Generate(many, big);
    const double bigMs = Ms(t0);
    cpu->Generate(many, ref);
    size_t bigDiff = 0;
    for (size_t n = 0; n < many.size(); n++)
        for (int x = 0; x < 16; x += 3)
            for (int z = 0; z < 16; z += 3)
                for (int y = kMinY; y < kMinY + kHeight; y++)
                    if (big[n].Get(x, y, z) != ref[n].Get(x, y, z))
                        bigDiff++;
    std::printf("%zu chunks in %.0f ms on the gpu (%.1f ms each); %zu sampled blocks differ\n", many.size(),
                bigMs, bigMs / static_cast<double>(many.size()), bigDiff);
    CHECK(bigDiff == 0);
    return true;
}

static void GpuDimension(int64_t seed, int dimension) {
    ServerConfig config;
    config.seed = static_cast<uint64_t>(seed);
    config.terrainBackend = "gpu";
    std::string note;
    std::unique_ptr<TerrainBackend> gpu = CreateGpuDimensionBackend(config, dimension, note);
    const char* label = dimension == kNether ? "nether" : "end";
    if (!gpu) {
        std::printf("%s: no gpu path: %s\n", label, note.c_str());
        if (note.contains("differs")) {
            std::fprintf(stderr, "FAIL %s gate: %s\n", label, note.c_str());
            g_failed++;
        }
        return;
    }
    std::printf("%s seed %lld: %s\n", label, static_cast<long long>(seed), note.c_str());
    std::string error;
    auto cpu = vanilla::VanillaDimensionBackend::Create(config, dimension, error);
    CHECK(cpu != nullptr);
    if (!cpu)
        return;
    const std::vector<ChunkPos> positions = {{0, 0}, {3, -2}, {-1, -1}, {40, -30}, {-60, 0}, {80, 20}, {-200, 150}, {300, -12}};
    std::vector<ChunkColumn> a(positions.size()), b(positions.size());
    auto t0 = std::chrono::steady_clock::now();
    gpu->Generate(positions, a);
    const double gpuMs = Ms(t0);
    t0 = std::chrono::steady_clock::now();
    cpu->Generate(positions, b);
    const double cpuMs = Ms(t0);
    size_t differing = 0, solid = 0;
    for (size_t n = 0; n < positions.size(); n++)
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = 0; y < 256; y++) {
                    if (b[n].Get(x, y, z) != 0)
                        solid++;
                    if (a[n].Get(x, y, z) != b[n].Get(x, y, z) && differing++ < 4)
                        std::fprintf(stderr, "%s differs chunk (%d,%d) at %d %d %d\n", label, positions[n].x, positions[n].z, x, y, z);
                    for (int qy = 0; qy < ChunkColumn::kQuartLayers && x < 4 && z < 4 && y == 0; qy++)
                        if (a[n].QuartBiome(x, qy, z) != b[n].QuartBiome(x, qy, z))
                            differing++;
                }
    std::printf("%s: %zu chunks, gpu %.0f ms, cpu %.0f ms (one thread); %zu solid, %zu differ\n", label, positions.size(), gpuMs,
                cpuMs, solid, differing);
    CHECK(differing == 0);
    CHECK(solid > 0);
}

int main() {
    BlockCatalog::Instance().LoadBuiltin();
    TestProgram();
    bool ran = false;
    // two seeds: the noise tables differ, the graph does not
    Gpu(12345, ran);
    if (ran)
        Gpu(-987654321, ran);
    if (ran) {
        GpuDimension(12345, kNether);
        GpuDimension(12345, kEnd);
        GpuDimension(7, kNether);
    }
    if (!ran && std::getenv("PS_REQUIRE_GPU")) {
        std::fprintf(stderr, "FAIL no gpu path and PS_REQUIRE_GPU is set\n");
        g_failed++;
    }
    if (g_failed) {
        std::printf("vanilla_gpu_test: %d failed\n", g_failed);
        return 1;
    }
    std::printf("vanilla_gpu_test ok%s\n", ran ? "" : " (cpu-only checks; no gpu path here)");
    return 0;
}
