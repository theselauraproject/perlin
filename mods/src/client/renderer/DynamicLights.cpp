#include "client/renderer/DynamicLights.hpp"

#include <algorithm>
#include <unordered_map>

#include "client/entity/EntityTracker.hpp"

namespace perlin::dynlight {
    int ItemLevel(const std::string& full) {
        static const std::unordered_map<std::string, int> kLevels = {{"torch", 14},
                                                                     {"soul_torch", 10},
                                                                     {"redstone_torch", 7},
                                                                     {"copper_torch", 14},
                                                                     {"lantern", 15},
                                                                     {"soul_lantern", 10},
                                                                     {"copper_lantern", 15},
                                                                     {"glowstone", 15},
                                                                     {"sea_lantern", 15},
                                                                     {"shroomlight", 15},
                                                                     {"lit_pumpkin", 15},
                                                                     {"jack_o_lantern", 15},
                                                                     {"lava_bucket", 15},
                                                                     {"campfire", 15},
                                                                     {"soul_campfire", 10},
                                                                     {"beacon", 15},
                                                                     {"end_rod", 14},
                                                                     {"ochre_froglight", 15},
                                                                     {"pearlescent_froglight", 15},
                                                                     {"verdant_froglight", 15},
                                                                     {"redstone_lamp", 15},
                                                                     {"magma", 3},
                                                                     {"magma_block", 3},
                                                                     {"enchanting_table", 7},
                                                                     {"brewing_stand", 1},
                                                                     {"crying_obsidian", 10},
                                                                     {"respawn_anchor", 15},
                                                                     {"sea_pickle", 6},
                                                                     {"candle", 3},
                                                                     {"glow_berries", 14},
                                                                     {"conduit", 15},
                                                                     {"copper_bulb", 15},
                                                                     {"exposed_copper_bulb", 12},
                                                                     {"weathered_copper_bulb", 8},
                                                                     {"oxidized_copper_bulb", 4},
                                                                     {"glow_lichen", 7},
                                                                     {"amethyst_cluster", 5},
                                                                     {"large_amethyst_bud", 4},
                                                                     {"medium_amethyst_bud", 2},
                                                                     {"small_amethyst_bud", 1},
                                                                     {"ender_chest", 7},
                                                                     {"sculk_catalyst", 6},
                                                                     {"glow_item_frame", 0},
                                                                     {"fire_charge", 0},
                                                                     {"blaze_rod", 0},
                                                                     {"lit_furnace", 13},
                                                                     {"lit_blast_furnace", 13},
                                                                     {"lit_smoker", 13},
                                                                     {"nether_portal", 11},
                                                                     {"portal", 11},
                                                                     {"end_portal_frame", 1},
                                                                     {"dragon_egg", 1},
                                                                     {"light_block", 15},
                                                                     {"light", 15},
                                                                     {"lava", 15},
                                                                     {"fire", 15},
                                                                     {"soul_fire", 10},
                                                                     {"cave_vines", 14},
                                                                     {"cave_vines_head_with_berries", 14},
                                                                     {"torchflower", 0},
                                                                     {"glowstone_dust", 0}};
        const std::string n = full.starts_with("minecraft:") ? full.substr(10) : full;
        if (const auto it = kLevels.find(n); it != kLevels.end())
            return it->second;
        if (n.size() > 7 && n.compare(n.size() - 7, 7, "_candle") == 0)
            return 3;
        return 0;
    }

    int EntityLevel(const entity::Entity& e) {
        if (e.Has(entity::flag::Onfire) && !e.Has(entity::flag::FireImmune))
            return 15;
        static const std::unordered_map<std::string, int> kMobs = {{"minecraft:blaze", 15},
                                                                   {"minecraft:magma_cube", 8},
                                                                   {"minecraft:glow_squid", 10},
                                                                   {"minecraft:fireball", 15},
                                                                   {"minecraft:small_fireball", 12},
                                                                   {"minecraft:dragon_fireball", 15},
                                                                   {"minecraft:wither_skull", 6},
                                                                   {"minecraft:lightning_bolt", 15},
                                                                   {"minecraft:allay", 0}};
        if (const auto it = kMobs.find(e.type); it != kMobs.end())
            return it->second;
        return 0;
    }

    void Collect(const entity::Entity& e, std::vector<Light>& out) {
        int level = EntityLevel(e);
        if (e.isItem)
            level = std::max(level, ItemLevel(e.itemName));
        else {
            level = std::max(level, ItemLevel(e.mainHand));
            level = std::max(level, ItemLevel(e.offHand));
        }
        if (level <= 0)
            return;
        const double height = e.isItem ? 0.25 : 1.0;
        out.push_back({e.pos + glm::dvec3(0.0, height, 0.0), static_cast<float>(level)});
    }

    void Select(std::vector<Light>& lights, const glm::dvec3& camera, size_t max) {
        std::sort(lights.begin(), lights.end(), [&](const Light& a, const Light& b) {
            const double sa = a.level - glm::distance(a.pos, camera),
                         sb = b.level - glm::distance(b.pos, camera);
            return sa > sb;
        });
        if (lights.size() > max)
            lights.resize(max);
    }

    float LevelAt(const std::vector<Light>& lights, const glm::dvec3& p) {
        float best = 0.0f;
        for (const Light& l : lights)
            best = std::max(best, l.level - static_cast<float>(glm::distance(l.pos, p)));
        return std::clamp(best, 0.0f, 15.0f);
    }
}
