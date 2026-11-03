#include "core/Config.hpp"
#include "world/BlockCatalog.hpp"
#include "world/ChunkManager.hpp"
#include "world/vanilla/VanillaTerrain.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <thread>

using namespace perlin::server;

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "resources";
    std::string error;
    if (!BlockCatalog::Instance().Load(dir + "/block_states.json", dir + "/block_destroy.tsv", error)) {
        std::printf("block data: %s\n", error.c_str());
        return 1;
    }
    ServerConfig config;
    config.seed = argc > 2 ? std::strtoll(argv[2], nullptr, 10) : 12345;
    auto backend = vanilla::VanillaCpuBackend::Create(config, error);
    if (!backend || !backend->Decorates()) {
        std::printf("no decorating backend: %s\n", error.c_str());
        return 1;
    }
    const int radius = argc > 3 ? std::atoi(argv[3]) : 4;
    int sx = 0, sy = 0, sz = 0;
    backend->FindSpawn(sx, sy, sz);
    const int ox = argc > 4 ? std::atoi(argv[4]) : sx >> 4, oz = argc > 5 ? std::atoi(argv[5]) : sz >> 4;
    std::printf("around chunk (%d, %d)\n", ox, oz);
    ChunkManager chunks(std::move(backend));
    const auto start = std::chrono::steady_clock::now();
    int missing = 1;
    while (missing) {
        missing = 0;
        for (int x = -radius; x < radius; x++)
            for (int z = -radius; z < radius; z++)
                if (!chunks.Get({ox + x, oz + z}))
                    missing++;
        chunks.Pump();
        if (missing)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const int count = 4 * radius * radius;
    std::printf("%d decorated chunks in %.0f ms (%.1f ms each)\n", count, ms, ms / count);
    std::map<std::string, int> names;
    const int size = radius * 32;
    std::vector<uint8_t> image(static_cast<size_t>(size * size * 3));
    BlockRegistry& reg = BlockRegistry::Instance();
    for (int x = -radius * 16; x < radius * 16; x++)
        for (int z = -radius * 16; z < radius * 16; z++) {
            const int wx = ox * 16 + x, wz = oz * 16 + z;
            int y = 319;
            for (; y > -64; y--)
                if (chunks.GetBlock(wx, y, wz) != 0)
                    break;
            const std::string top = reg.Name(chunks.GetBlock(wx, y, wz));
            for (int yy = y; yy > y - 12 && yy > -64; yy--) {
                const std::string n = reg.Name(chunks.GetBlock(wx, yy, wz));
                if (n != "minecraft:stone" && n != "minecraft:dirt" && n != "minecraft:water" && n != "minecraft:air")
                    names[n]++;
            }
            uint8_t r = 120, g = 120, b = 120;
            if (top.contains("leaves"))
                r = 30, g = 110, b = 30;
            else if (top.contains("log"))
                r = 100, g = 70, b = 40;
            else if (top == "minecraft:grass_block")
                r = 100, g = 170, b = 70;
            else if (top.contains("grass") || top.contains("fern") || top.contains("bush"))
                r = 140, g = 200, b = 90;
            else if (top.contains("water"))
                r = 50, g = 80, b = 200;
            else if (top.contains("sand"))
                r = 220, g = 210, b = 150;
            else if (top.contains("snow"))
                r = 250, g = 250, b = 250;
            else if (top.contains("ice"))
                r = 160, g = 190, b = 250;
            else if (top.contains("litter"))
                r = 170, g = 120, b = 50;
            else if (top.contains("flower") || top.contains("tulip") || top.contains("poppy") || top.contains("dandelion") ||
                     top.contains("daisy") || top.contains("orchid") || top.contains("allium") || top.contains("bluet"))
                r = 230, g = 60, b = 160;
            const size_t at = static_cast<size_t>(((z + radius * 16) * size + (x + radius * 16)) * 3);
            image[at] = r;
            image[at + 1] = g;
            image[at + 2] = b;
        }
    if (const char* path = std::getenv("DECORATION_MAP")) {
        std::ofstream f(path, std::ios::binary);
        f << "P6\n" << size << " " << size << "\n255\n";
        f.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    }
    int logs = 0, leaves = 0, plants = 0;
    for (const auto& [n, c] : names) {
        if (n.contains("_log") || n.contains("_wood"))
            logs += c;
        if (n.contains("leaves"))
            leaves += c;
        if (n.contains("grass") && n != "minecraft:grass_block" || n.contains("flower") || n.contains("fern") ||
            n.contains("bush") || n.contains("litter") || n.contains("poppy") || n.contains("dandelion"))
            plants += c;
    }
    std::vector<std::pair<int, std::string>> sorted;
    for (const auto& [n, c] : names)
        sorted.emplace_back(c, n);
    std::sort(sorted.rbegin(), sorted.rend());
    for (size_t i = 0; i < sorted.size() && i < 40; i++)
        std::printf("  %6d %s\n", sorted[i].first, sorted[i].second.c_str());
    std::printf("logs %d, leaves %d, plants %d\n", logs, leaves, plants);
    if (logs == 0 || leaves == 0 || plants == 0) {
        std::printf("FAIL: no vegetation\n");
        return 1;
    }
    std::printf("decoration test passed\n");
    return 0;
}
