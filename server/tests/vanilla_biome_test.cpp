// biome placement against Java's own answers (tools/javaref/biome_lookup_ref.txt: its parameter list searched by brute force), the surface layering rules,
// and a real stretch of world: biomes must vary and every column must have ground
#include "core/Config.hpp"
#include "test_util.hpp"
#include "world/BlockCatalog.hpp"
#include "world/vanilla/VanillaTerrain.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

using namespace perlin::server;
using namespace perlin::server::vanilla;

static int IndexOf(const std::string& javaName) {
    for (int i = 0; i < kBiomeCount; i++)
        if (javaName == kBiomes[i].java)
            return i;
    return -1;
}

static void TestLookup(const char* path) {
    std::ifstream in(path);
    CHECK(in.good());
    std::string line;
    int checked = 0, wrong = 0;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        int32_t t[6];
        std::string name;
        if (!(ls >> t[0] >> t[1] >> t[2] >> t[3] >> t[4] >> t[5] >> name))
            continue;
        const int want = IndexOf(name.substr(name.find(':') + 1));
        const int got = FindBiome(t);
        CHECK(got == FindBiomeLinear(t));
        checked++;
        if (want != got) {
            if (wrong++ < 5)
                std::printf("target (%d %d %d %d %d %d): java %s, ours %s\n", t[0], t[1], t[2], t[3], t[4],
                            t[5], name.c_str(), kBiomes[got].java);
        }
    }
    std::printf("biome lookup: %d samples, %d differ from Java's lookup\n", checked, wrong);
    CHECK(checked >= 29000);
    CHECK(wrong == 0);
}

static void TestSearchAgreesWithLinear() {
    uint64_t state = 0x9E3779B97F4A7C15ull;
    const auto next = [&] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    int differ = 0;
    for (int i = 0; i < 20000; i++) {
        int32_t t[6];
        for (int d = 0; d < 6; d++)
            t[d] = static_cast<int32_t>(next() % 24001) - 12000;
        if (i % 3 == 0)
            t[4] = 0;
        if (FindBiome(t) != FindBiomeLinear(t))
            differ++;
    }
    std::printf("indexed biome search: %d of 20000 random targets differ from the linear scan\n", differ);
    CHECK(differ == 0);
}

static void TestTable() {
    CHECK(kBiomePointCount == 7594);
    CHECK(kBiomeCount >= 50);
    std::set<std::string> seen;
    for (int i = 0; i < kBiomeCount; i++) {
        const BiomeMaterials& b = kBiomes[i];
        CHECK(seen.insert(b.java).second);
        CHECK(b.top[0] != 0 && b.mid[0] != 0 && b.foundation[0] != 0 && b.seaFloor[0] != 0 && b.sea[0] != 0);
        CHECK(b.seaFloorDepth >= 0 && b.seaFloorDepth <= 16);
    }
    // a few materials straight from bedrock-samples
    CHECK(std::string(kBiomes[IndexOf("desert")].top) == "minecraft:sand");
    CHECK(std::string(kBiomes[IndexOf("badlands")].top) == "minecraft:red_sand" &&
          std::string(kBiomes[IndexOf("badlands")].mid) == "minecraft:hardened_clay");
    CHECK(std::string(kBiomes[IndexOf("plains")].top) == "minecraft:grass_block");
    CHECK(std::string(kBiomes[IndexOf("mangrove_swamp")].top) == "minecraft:mud");
    CHECK(std::string(kBiomes[IndexOf("warm_ocean")].seaFloor) == "minecraft:sand" &&
          kBiomes[IndexOf("warm_ocean")].seaFloorDepth == 3);
}

static void TestLayers() {
    VanillaSurface s;
    const BlockCatalog& c = BlockCatalog::Instance();
    const SurfaceMaterial& plains = s.Materials()[static_cast<size_t>(IndexOf("plains"))];
    const SurfaceMaterial& warm = s.Materials()[static_cast<size_t>(IndexOf("warm_ocean"))];
    // a column of ground up to world y 80: grass, 3 dirt, then stone, deepslate under y 0, bedrock at the floor
    const int top = 80 - kMinY;
    CHECK(s.BlockAt(plains, top, top, true) == c.DefaultBlock("grass_block"));
    for (int d = 1; d <= kMidLayers; d++)
        CHECK(s.BlockAt(plains, top - d, top, true) == c.DefaultBlock("dirt"));
    CHECK(s.BlockAt(plains, top - kMidLayers - 1, top, true) == s.Stone());
    CHECK(s.BlockAt(plains, -10 - kMinY, top, true) == s.Deepslate());
    CHECK(s.BlockAt(plains, 0, top, true) == s.Bedrock());
    // air above the ground and water up to sea level
    CHECK(s.BlockAt(plains, 100 - kMinY, top, false) == 0);
    CHECK(s.BlockAt(plains, kSeaLevel - kMinY, top, false) == plains.sea);
    CHECK(s.BlockAt(plains, kSeaLevel + 1 - kMinY, top, false) == 0);
    // ground just under the water: grass turns to dirt; far under it the biome's sea floor material replaces the skin
    const int shallow = (kSeaLevel - 2) - kMinY;
    CHECK(s.BlockAt(plains, shallow, shallow, true) == s.Dirt());
    const int deep = (kSeaLevel - 20) - kMinY;
    CHECK(s.BlockAt(plains, deep, deep, true) == plains.seaFloor);
    // a warm ocean's floor starts only 3 blocks down, and is sand
    const int fourDown = (kSeaLevel - 4) - kMinY;
    CHECK(s.BlockAt(warm, fourDown, fourDown, true) == warm.seaFloor);
    CHECK(warm.seaFloor == c.DefaultBlock("sand"));
    CHECK(VanillaSurface::TopOf(std::vector<float>(kHeight, -1.0f).data()) == -1);
}

static void TestWorld() {
    ServerConfig config;
    config.seed = 4242;
    std::string error;
    auto backend = VanillaCpuBackend::Create(config, error);
    CHECK(backend != nullptr);
    if (!backend)
        return;
    std::map<int, int> columns;
    std::vector<float> density(static_cast<size_t>(16 * 16 * kHeight));
    int noGround = 0;
    // a spread of chunks, far apart so climate differs
    for (int cx = -3200; cx <= 3200; cx += 1600)
        for (int cz = -3200; cz <= 3200; cz += 1600) {
            DfCacheScope cache;
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++)
                    for (int y = 0; y < kHeight; y++)
                        density[static_cast<size_t>((x * 16 + z) * kHeight + y)] =
                            backend->Density(cx * 16 + x, kMinY + y, cz * 16 + z);
            std::array<uint8_t, 256> biomes;
            backend->BiomesOf(density.data(), cx, cz, biomes.data());
            for (int i = 0; i < 256; i++) {
                columns[biomes[static_cast<size_t>(i)]]++;
                if (VanillaSurface::TopOf(density.data() + static_cast<size_t>(i) * kHeight) < 0)
                    noGround++;
            }
        }
    std::printf("25 far-apart chunks: %zu different biomes, e.g.", columns.size());
    int shown = 0;
    for (const auto& [b, n] : columns)
        if (shown++ < 8)
            std::printf(" %s(%d)", kBiomes[b].java, n);
    std::printf("\n");
    CHECK(columns.size() >= 4);
    CHECK(noGround == 0);
}

static void TestLocate() {
    ServerConfig config;
    config.seed = 4242;
    std::string error;
    auto backend = VanillaCpuBackend::Create(config, error);
    CHECK(backend != nullptr);
    if (!backend)
        return;
    std::atomic<bool> never{false};
    CHECK(backend->HasBiomes());
    // biomes that are common should turn up close by, and the biome of the column found must really be the one asked for
    int found = 0;
    for (const char* name :
         {"ocean", "plains", "forest", "desert", "taiga", "swamp", "savanna", "river", "beach"}) {
        const int biome = IndexOf(name);
        CHECK(biome >= 0);
        int x = 0, y = 0, z = 0;
        const auto began = std::chrono::steady_clock::now();
        const bool ok = backend->LocateBiome(biome, 100, -200, 3200, never, x, y, z);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
        if (!ok) {
            std::printf("locate %s: none within 3200 blocks (%.0f ms)\n", name, ms);
            continue;
        }
        found++;
        const int actual = backend->ColumnBiome(x, z);
        std::printf("locate %s: [%d, %d, %d], %.0f blocks away, column biome %s (%.0f ms)\n", name, x, y, z,
                    std::hypot(static_cast<double>(x - 100), static_cast<double>(z + 200)),
                    kBiomes[actual].java, ms);
        CHECK(actual == biome);
    }
    CHECK(found >= 5);
    // a search that is cancelled stops, and one for a biome that is not in the data finds nothing
    std::atomic<bool> stop{true};
    int x = 0, y = 0, z = 0;
    CHECK(!backend->LocateBiome(IndexOf("desert"), 0, 0, 6400, stop, x, y, z));
    CHECK(!backend->LocateBiome(-1, 0, 0, 100, never, x, y, z));
}

int main(int argc, char** argv) {
    BlockCatalog::Instance().LoadBuiltin();
    TestTable();
    TestSearchAgreesWithLinear();
    TestLayers();
    if (argc > 1)
        TestLookup(argv[1]);
    TestWorld();
    TestLocate();
    if (g_failed) {
        std::printf("vanilla_biome_test: %d failed\n", g_failed);
        return 1;
    }
    std::printf("vanilla_biome_test ok\n");
    return 0;
}
