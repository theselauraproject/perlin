// ore features from Java's data: the right ores turn up in the right heights after decoration, they are repeatable, and they don't depend on which chunks are built together
#include "core/Config.hpp"
#include "test_util.hpp"
#include "world/BlockCatalog.hpp"
#include "world/vanilla/FeatureData.hpp"
#include "world/vanilla/VanillaTerrain.hpp"

#include <chrono>
#include <map>
#include <string>

using namespace perlin::server;

int main() {
    BlockCatalog::Instance().LoadBuiltin();
    const BlockCatalog& cat = BlockCatalog::Instance();
    ServerConfig config;
    config.seed = 4242;
    std::string error;
    auto backend = vanilla::VanillaCpuBackend::Create(config, error);
    if (!backend) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    CHECK(vanilla::kOreRuleCount >= 25);
    CHECK(backend->Features().RuleCount() == static_cast<size_t>(vanilla::kOreRuleCount));

    std::vector<ChunkPos> pos;
    for (int x = -2; x <= 2; x++)
        for (int z = -2; z <= 2; z++)
            pos.push_back({x, z});
    std::vector<ChunkColumn> cols(pos.size());
    const auto start = std::chrono::steady_clock::now();
    backend->Generate(pos, cols);
    std::printf("%zu chunks in %.0f ms\n", pos.size(),
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());

    // block id -> [count, lowest y, highest y]
    struct Stat {
        int count = 0, lo = 9999, hi = -9999;
    };
    std::map<std::string, Stat> stats;
    std::map<uint16_t, std::string> names;
    std::vector<ChunkColumn> decorated;
    if (backend->Decorates()) {
        for (int x = -1; x <= 1; x++)
            for (int z = -1; z <= 1; z++) {
                const ChunkColumn* region[9];
                for (int dz = -1; dz <= 1; dz++)
                    for (int dx = -1; dx <= 1; dx++)
                        region[(dz + 1) * 3 + (dx + 1)] = &cols[static_cast<size_t>((x + dx + 2) * 5 + (z + dz + 2))];
                ChunkColumn out = cols[static_cast<size_t>((x + 2) * 5 + (z + 2))].Clone();
                backend->Decorate({x, z}, region, out);
                decorated.push_back(std::move(out));
            }
    }
    for (const char* n : {"coal_ore", "iron_ore", "copper_ore", "gold_ore", "lapis_ore", "redstone_ore",
                          "diamond_ore", "deepslate_iron_ore", "deepslate_diamond_ore",
                          "deepslate_redstone_ore", "gravel", "granite", "diorite", "andesite", "tuff"})
        names[cat.DefaultBlock(n)] = n;
    for (auto& col : decorated.empty() ? cols : decorated)
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = kMinY; y < kMinY + kHeight; y++) {
                    auto it = names.find(col.Get(x, y, z));
                    if (it == names.end())
                        continue;
                    Stat& s = stats[it->second];
                    s.count++;
                    s.lo = std::min(s.lo, y);
                    s.hi = std::max(s.hi, y);
                }
    for (auto& [n, s] : stats)
        std::printf("%-24s %6d  y %d..%d\n", n.c_str(), s.count, s.lo, s.hi);
    for (const char* n : {"coal_ore", "iron_ore", "copper_ore", "gold_ore", "lapis_ore", "redstone_ore",
                          "diamond_ore", "granite", "diorite", "andesite", "tuff", "gravel"})
        CHECK(stats[n].count > 0);
    // the rules' height ranges (blobs reach a few blocks past the start)
    CHECK(stats["diamond_ore"].hi <= 16 + 8);
    CHECK(stats["deepslate_diamond_ore"].hi <= 16 + 8);
    CHECK(stats["redstone_ore"].hi <= 15 + 8);
    CHECK(stats["lapis_ore"].hi <= 64 + 8);
    CHECK(stats["tuff"].hi <= 0 + 8);
    CHECK(stats["deepslate_diamond_ore"].count > 0);
    CHECK(stats["diamond_ore"].lo >= -144 - 8);
    // stone variants below y 0 are deepslate: iron under 0 is the deepslate kind
    CHECK(stats["deepslate_iron_ore"].count > 0);

    // the same chunk built alone, or in another batch, is the same (terrain stage)
    std::vector<ChunkPos> one = {{1, -1}};
    std::vector<ChunkColumn> alone(1);
    backend->Generate(one, alone);
    size_t at = 0;
    for (size_t i = 0; i < pos.size(); i++)
        if (pos[i] == one[0])
            at = i;
    int differing = 0;
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++)
            for (int y = kMinY; y < kMinY + kHeight; y++)
                if (alone[0].Get(x, y, z) != cols[at].Get(x, y, z))
                    differing++;
    CHECK(differing == 0);

    // a different seed puts the ore somewhere else
    ServerConfig other = config;
    other.seed = 4243;
    auto b2 = vanilla::VanillaCpuBackend::Create(other, error);
    CHECK(b2 != nullptr);
    if (b2) {
        std::vector<ChunkColumn> c2(1);
        b2->Generate(one, c2);
        int same = 0, total = 0;
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = -64; y < 0; y++) {
                    total++;
                    same += c2[0].Get(x, y, z) == alone[0].Get(x, y, z);
                }
        CHECK(same < total);
    }
    if (g_failed)
        return 1;
    std::printf("vanilla_features_test ok\n");
    return 0;
}
