#include "core/Config.hpp"
#include "world/BlockCatalog.hpp"
#include "world/Blocks.hpp"
#include "world/vanilla/VanillaTerrain.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

using namespace perlin::server;

int main(int argc, char** argv) {
    if (argc < 5) {
        std::printf("usage: vanilla_chunk_dump <seed> <chunk x> <chunk z> <count>\n");
        return 2;
    }
    BlockCatalog::Instance().LoadBuiltin();
    ServerConfig config;
    config.seed = std::strtoll(argv[1], nullptr, 10);
    const int cx0 = std::atoi(argv[2]), cz0 = std::atoi(argv[3]), n = std::atoi(argv[4]);
    std::string error;
    auto backend = vanilla::VanillaCpuBackend::Create(config, error);
    if (!backend) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    if (std::string(argv[2]) == "locate") {
        for (int i = 0; i < vanilla::kBiomeCount; i++)
            if (std::string(argv[3]) == vanilla::kBiomes[i].java) {
                std::atomic<bool> cancel{false};
                int x, y, z;
                if (backend->LocateBiome(i, 0, 0, 20000, cancel, x, y, z))
                    std::printf("%d %d\n", x >> 4, z >> 4);
                return 0;
            }
        return 1;
    }
    if (argc > 5 && std::string(argv[5]) == "biomes") {
        std::printf("zoom %lld\n", static_cast<long long>(backend->ZoomSeed()));
        for (int qx = 956; qx <= 957; qx++)
            for (int qz = -943; qz <= -942; qz++)
                for (int qy = 20; qy <= 22; qy++)
                    std::printf("q %d %d %d %s\n", qx, qy, qz, vanilla::kBiomes[backend->NoiseBiome(qx, qy, qz)].java);
        for (int y = 90; y >= 55; y -= 4) {
            int32_t t[6];
            backend->Climate().Sample((cx0 >> 2) * 4, (y >> 2) * 4, (cz0 >> 2) * 4, t);
            std::printf("%d %s %d %d %d %d %d %d\n", y >> 2, vanilla::kBiomes[backend->NoiseBiome(cx0 >> 2, y >> 2, cz0 >> 2)].java, t[0], t[1], t[2], t[3], t[4], t[5]);
        }
        return 0;
    }
    static const std::unordered_map<std::string, std::string> kJava = {{"hardened_clay", "terracotta"}, {"snow", "snow_block"}};
    for (int cx = cx0; cx < cx0 + n; cx++)
        for (int cz = cz0; cz < cz0 + n; cz++) {
            std::vector<ChunkPos> pos = {{cx, cz}};
            std::vector<ChunkColumn> col(1);
            backend->Generate(pos, col);
            std::printf("C %d %d\n", cx, cz);
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++) {
                    std::string line, last;
                    int run = 0;
                    for (int y = -64; y < 320; y++) {
                        const uint16_t b = col[0].Get(x, y, z);
                        std::string name = b == 0 ? "air" : BlockRegistry::Instance().Name(b);
                        if (name.starts_with("minecraft:"))
                            name = name.substr(10);
                        if (const auto it = kJava.find(name); it != kJava.end())
                            name = it->second;
                        if (name == last) {
                            run++;
                            continue;
                        }
                        if (!last.empty())
                            line += last + "*" + std::to_string(run) + " ";
                        last = name;
                        run = 1;
                    }
                    line += last + "*" + std::to_string(run);
                    std::printf("%d %d %s\n", x, z, line.c_str());
                }
        }
    return 0;
}
