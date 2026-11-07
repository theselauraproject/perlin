// runs terrain_vanilla.slang compiled to c++ (slangc -target cpp) over chunks and compares each phase with the cpu reference, bit for bit:
//   densities (phase 1) against the density graph, columns (phase 2) against VanillaCpuBackend::BiomesOf / TopOf, blocks (phase 3) against VanillaSurface.
// this checks the kernel's algorithm, the table packing and the four-phase scheme without a gpu; it cannot check a driver's float behaviour
#include "kernel.inc"
#include "gpu/VanillaGpuProgram.hpp"
#include "world/BlockCatalog.hpp"
#include "world/vanilla/VanillaTerrain.hpp"
#include <algorithm>
#include <map>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
using namespace perlin::server;
using namespace perlin::server::vanilla;

int main(int argc, char** argv) {
    const int64_t seed = argc > 1 ? std::atoll(argv[1]) : 12345;
    BlockCatalog::Instance().LoadBuiltin();
    ServerConfig config;
    config.seed = static_cast<uint64_t>(seed);
    std::string err;
    auto cpu = VanillaCpuBackend::Create(config, err);
    if (!cpu) {
        std::printf("cpu: %s\n", err.c_str());
        return 1;
    }
    auto program = GpuProgram::Build(cpu->FinalDensity(), cpu->Climate().Roots(), err);
    if (!program) {
        std::printf("build: %s\n", err.c_str());
        return 1;
    }
    const std::vector<uint32_t> tables = PackTables(*program, cpu->Surface());
    const uint32_t K = static_cast<uint32_t>(program->interpolatedInputs.size());
    const std::vector<ChunkPos> pos = {{0, 0},         {-1, -1},          {37, 12},
                                       {-2048, 2047},  {100000, -100000}, {1600, 1600},
                                       {-3200, -1600}, {3200, 0}};
    const uint32_t count = static_cast<uint32_t>(pos.size());
    std::vector<Vector<int32_t, 2>> chunks;
    for (auto& p : pos) {
        Vector<int32_t, 2> v;
        v.x = p.x;
        v.y = p.z;
        chunks.push_back(v);
    }
    const size_t blocks = 16 * 16 * 384;
    const uint32_t columnBase = count * 1225 * K;
    std::vector<uint32_t> scratch(columnBase + static_cast<size_t>(count) * 256 * 2);
    std::vector<float> density(static_cast<size_t>(count) * blocks);
    Params_0 params{};
    GlobalParams_0 gp{};
    gp.params_0 = &params;
    gp.tables_0.data = const_cast<uint32_t*>(tables.data());
    gp.tables_0.count = tables.size();
    gp.chunks_0.data = chunks.data();
    gp.chunks_0.count = chunks.size();
    gp.corners_0.data = scratch.data();
    gp.corners_0.count = scratch.size();
    gp.density_0.data = density.data();
    gp.density_0.count = density.size();
    const auto run = [&](uint64_t threads) {
        ComputeVaryingInput in{};
        in.startGroupID = {0, 0, 0};
        in.endGroupID = {static_cast<uint32_t>((threads + 63) / 64), 1, 1};
        csMain(&in, nullptr, &gp);
    };
    auto t0 = std::chrono::steady_clock::now();
    params = {count, 0, 0, program->innerCount, program->root, K, columnBase, 0};
    run(static_cast<uint64_t>(count) * 1225);
    params = {count, 1, program->innerCount, program->outerCount, program->root, K, columnBase, 0};
    run(static_cast<uint64_t>(count) * blocks);
    const std::vector<float> densities = density; // phase 3 overwrites the densities with block ids
    params = {count, 2, 0, 0, program->root, K, columnBase, 0};
    run(static_cast<uint64_t>(count) * 256);
    params = {count, 3, 0, 0, program->root, K, columnBase, 0};
    run(static_cast<uint64_t>(count) * blocks);
    std::printf("kernel on the cpu: %.0f ms for %u chunks\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
                count);

    size_t badDensity = 0, checkedDensity = 0, badColumns = 0, columns = 0, badBlocks = 0, cells = 0;
    std::mt19937 rng(7);
    std::map<int, int> biomeSpread;
    for (uint32_t n = 0; n < count; n++) {
        // phase 1: densities against the graph
        {
            DfCacheScope cache;
            const int samples = n == 0 ? static_cast<int>(blocks) : 4000;
            for (int s = 0; s < samples; s++) {
                int x, z, y;
                if (n == 0) {
                    x = s / (16 * 384);
                    z = (s / 384) & 15;
                    y = s % 384;
                } else {
                    x = rng() & 15;
                    z = rng() & 15;
                    y = rng() % 384;
                }
                const float want = cpu->Density(pos[n].x * 16 + x, -64 + y, pos[n].z * 16 + z);
                const float got = densities[n * blocks + static_cast<size_t>((x * 16 + z) * 384 + y)];
                checkedDensity++;
                if (std::memcmp(&want, &got, 4) != 0 && !(want != want && got != got)) {
                    if (badDensity++ < 4)
                        std::printf("DENSITY chunk %u (%d,%d,%d): cpu %.9g kernel %.9g\n", n, x, -64 + y, z,
                                    want, got);
                }
            }
        }
        // phases 2 and 3 on the kernel's own densities, so a density difference cannot hide a stage difference
        std::array<uint8_t, 256> biomes;
        {
            DfCacheScope cache;
            cpu->BiomesOf(densities.data() + n * blocks, pos[n].x, pos[n].z, biomes.data());
        }
        for (int c = 0; c < 256; c++) {
            const int top =
                VanillaSurface::TopOf(densities.data() + n * blocks + static_cast<size_t>(c) * 384);
            const int gTop = static_cast<int>(scratch[columnBase + (static_cast<size_t>(n) * 256 + c) * 2]);
            const uint32_t gBiome = scratch[columnBase + (static_cast<size_t>(n) * 256 + c) * 2 + 1];
            columns++;
            biomeSpread[biomes[c]]++;
            if (gTop != top || gBiome != biomes[c]) {
                if (badColumns++ < 6)
                    std::printf("COLUMN chunk %u (%d,%d): cpu top %d biome %s, kernel top %d biome %u\n", n,
                                c / 16, c % 16, top, kBiomes[biomes[c]].java, gTop, gBiome);
            }
            const SurfaceMaterial& m = cpu->Surface().Materials()[biomes[c]];
            for (int y = 0; y < 384; y++) {
                const size_t at = n * blocks + static_cast<size_t>(c) * 384 + y;
                const uint16_t want = cpu->Surface().BlockAt(m, y, top, densities[at] > 0.0f);
                uint32_t got;
                std::memcpy(&got, &density[at], 4);
                cells++;
                if (got != want) {
                    if (badBlocks++ < 6)
                        std::printf("BLOCK chunk %u col (%d,%d) y %d: cpu %u kernel %u\n", n, c / 16, c % 16,
                                    y, want, got);
                }
            }
        }
    }
    std::printf("seed %lld: density %zu positions (%zu differ), columns %zu (%zu differ), blocks %zu (%zu "
                "differ), %zu biomes seen\n",
                static_cast<long long>(seed), checkedDensity, badDensity, columns, badColumns, cells,
                badBlocks, biomeSpread.size());
    return (badDensity || badColumns || badBlocks) ? 1 : 0;
}
