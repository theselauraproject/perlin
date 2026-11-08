#include "core/Config.hpp"
#include "world/BlockCatalog.hpp"
#include "world/vanilla/VanillaTerrain.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace perlin::server;

int main() {
    BlockCatalog::Instance().LoadBuiltin();
    ServerConfig config;
    config.seed = 12345;
    std::string error;
    auto backend = vanilla::VanillaCpuBackend::Create(config, error);
    if (!backend) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    std::vector<ChunkPos> pos = {{0, 0}, {1, 0}, {-3, 5}};
    std::vector<ChunkColumn> cols(pos.size());
    const auto start = std::chrono::steady_clock::now();
    backend->Generate(pos, cols);
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("%zu chunks in %.0f ms (%.0f ms each)\n", pos.size(), ms,
                ms / static_cast<double>(pos.size()));
    int bad = 0;
    for (size_t n = 0; n < pos.size(); n++) {

        for (int i = 0; i < 400; i++) {
            const int x = (i * 7) & 15, z = (i * 13) & 15, y = -60 + (i * 31) % 360;
            const bool solid = backend->Density(pos[n].x * 16 + x, y, pos[n].z * 16 + z) > 0.0f;
            const uint16_t b = cols[n].Get(x, y, z);
            if (solid && b == 0)
                bad++;
        }
        int height = -999;
        for (int y = 319; y >= -64; y--)
            if (cols[n].Get(8, y, 8) != 0 && cols[n].Get(8, y, 8) != cols[n].Get(0, 319, 0)) {
                height = y;
                break;
            }
        std::printf("chunk (%d,%d): top block at (8,8) y=%d\n", pos[n].x, pos[n].z, height);
    }
    if (bad > 40) {
        std::printf("%d solid positions came out as air\n", bad);
        return 1;
    }
    std::printf("vanilla_terrain_test ok\n");
    return 0;
}
