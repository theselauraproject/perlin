#include "core/Config.hpp"
#include "world/BlockCatalog.hpp"
#include "world/vanilla/VanillaTerrain.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace perlin::server;

int main(int argc, char** argv) {
    BlockCatalog::Instance().LoadBuiltin();
    ServerConfig config;
    config.seed = argc > 1 ? std::strtoll(argv[1], nullptr, 10) : 12345;
    const int radius = argc > 2 ? std::atoi(argv[2]) : 3;
    std::string error;
    auto backend = vanilla::VanillaCpuBackend::Create(config, error);
    if (!backend) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    std::vector<ChunkPos> pos;
    for (int x = -radius; x < radius; x++)
        for (int z = -radius; z < radius; z++)
            pos.push_back({x, z});
    std::vector<ChunkColumn> cols(pos.size());
    const auto start = std::chrono::steady_clock::now();
    backend->Generate(pos, cols);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    double decorateMs = 0;
    int decorated = 0;
    if (backend->Decorates()) {
        const int side = radius * 2;
        const auto at = [&](int x, int z) -> ChunkColumn& { return cols[static_cast<size_t>((x + radius) * side + (z + radius))]; };
        const auto d0 = std::chrono::steady_clock::now();
        for (int x = -radius + 1; x < radius - 1; x++)
            for (int z = -radius + 1; z < radius - 1; z++) {
                const ChunkColumn* region[9];
                for (int dx = -1; dx <= 1; dx++)
                    for (int dz = -1; dz <= 1; dz++)
                        region[(dz + 1) * 3 + (dx + 1)] = &at(x + dx, z + dz);
                ChunkColumn out = at(x, z).Clone();
                backend->Decorate({x, z}, region, out);
                decorated++;
            }
        decorateMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - d0).count();
    }
    std::printf("decorated %d chunks in %.0f ms (%.1f ms each)\n", decorated, decorateMs,
                decorated ? decorateMs / decorated : 0.0);
    const BlockCatalog& c = BlockCatalog::Instance();
    const uint16_t water = c.DefaultBlock("water"), lava = c.DefaultBlock("lava");
    long air = 0, wet = 0, hot = 0;
    uint64_t hash = 1469598103934665603ull;
    for (const ChunkColumn& col : cols)
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = -64; y < 320; y++)
                    hash = (hash ^ col.Get(x, y, z)) * 1099511628211ull;
    for (const ChunkColumn& col : cols)
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = -50; y < 30; y++) {
                    const uint16_t b = col.Get(x, y, z);
                    air += b == 0;
                    wet += b == water;
                    hot += b == lava;
                }
    std::printf("%zu chunks in %.0f ms (%.1f ms each); y -50..29: air %ld water %ld lava %ld hash %016llx\n", pos.size(), ms,
                ms / static_cast<double>(pos.size()), air, wet, hot, static_cast<unsigned long long>(hash));
    return 0;
}
