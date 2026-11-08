#include "client/javashaders/Biomes.hpp"

#include <algorithm>
#include <map>

namespace perlin::jshader {
    const std::vector<JavaBiome>& JavaBiomes() {
        static const std::vector<JavaBiome> biomes = {
            {"the_void", 0, 0, 0.5f, 0.5f},
            {"plains", 5, 1, 0.8f, 0.4f},
            {"sunflower_plains", 5, 1, 0.8f, 0.4f},
            {"snowy_plains", 7, 1, 0.0f, 0.5f},
            {"ice_spikes", 5, 1, 0.0f, 0.5f},
            {"desert", 12, 0, 2.0f, 0.0f},
            {"swamp", 14, 1, 0.8f, 0.9f},
            {"mangrove_swamp", 14, 1, 0.8f, 0.9f},
            {"forest", 10, 1, 0.7f, 0.8f},
            {"flower_forest", 10, 1, 0.7f, 0.8f},
            {"birch_forest", 10, 1, 0.6f, 0.6f},
            {"dappled_forest", 10, 1, 0.6f, 0.6f},
            {"dark_forest", 10, 1, 0.7f, 0.8f},
            {"pale_garden", 10, 1, 0.7f, 0.8f},
            {"old_growth_birch_forest", 10, 1, 0.6f, 0.6f},
            {"old_growth_pine_taiga", 1, 1, 0.3f, 0.8f},
            {"old_growth_spruce_taiga", 1, 1, 0.25f, 0.8f},
            {"taiga", 1, 1, 0.25f, 0.8f},
            {"snowy_taiga", 1, 1, -0.5f, 0.4f},
            {"savanna", 5, 0, 2.0f, 0.0f},
            {"savanna_plateau", 5, 0, 2.0f, 0.0f},
            {"windswept_hills", 2, 1, 0.2f, 0.3f},
            {"windswept_gravelly_hills", 2, 1, 0.2f, 0.3f},
            {"windswept_forest", 2, 1, 0.2f, 0.3f},
            {"windswept_savanna", 5, 0, 2.0f, 0.0f},
            {"jungle", 3, 1, 0.95f, 0.9f},
            {"sparse_jungle", 3, 1, 0.95f, 0.8f},
            {"bamboo_jungle", 3, 1, 0.95f, 0.9f},
            {"badlands", 4, 0, 2.0f, 0.0f},
            {"eroded_badlands", 4, 0, 2.0f, 0.0f},
            {"wooded_badlands", 4, 0, 2.0f, 0.0f},
            {"meadow", 17, 1, 0.5f, 0.8f},
            {"cherry_grove", 17, 1, 0.5f, 0.8f},
            {"grove", 10, 1, -0.2f, 0.8f},
            {"snowy_slopes", 17, 1, -0.3f, 0.9f},
            {"frozen_peaks", 17, 1, -0.7f, 0.9f},
            {"jagged_peaks", 17, 1, -0.7f, 0.9f},
            {"stony_peaks", 17, 1, 1.0f, 0.3f},
            {"river", 13, 1, 0.5f, 0.5f},
            {"frozen_river", 13, 1, 0.0f, 0.5f},
            {"beach", 9, 1, 0.8f, 0.4f},
            {"snowy_beach", 9, 1, 0.05f, 0.3f},
            {"stony_shore", 5, 1, 0.2f, 0.3f},
            {"warm_ocean", 11, 1, 0.5f, 0.5f},
            {"lukewarm_ocean", 11, 1, 0.5f, 0.5f},
            {"deep_lukewarm_ocean", 11, 1, 0.5f, 0.5f},
            {"ocean", 11, 1, 0.5f, 0.5f},
            {"deep_ocean", 11, 1, 0.5f, 0.5f},
            {"cold_ocean", 11, 1, 0.5f, 0.5f},
            {"deep_cold_ocean", 11, 1, 0.5f, 0.5f},
            {"frozen_ocean", 11, 1, 0.0f, 0.5f},
            {"deep_frozen_ocean", 11, 1, 0.5f, 0.5f},
            {"mushroom_fields", 15, 1, 0.9f, 1.0f},
            {"dripstone_caves", 5, 1, 0.8f, 0.4f},
            {"lush_caves", 5, 1, 0.5f, 0.5f},
            {"deep_dark", 5, 1, 0.8f, 0.4f},
            {"sulfur_caves", 5, 1, 0.8f, 0.4f},
            {"nether_wastes", 16, 0, 2.0f, 0.0f},
            {"warped_forest", 16, 0, 2.0f, 0.0f},
            {"crimson_forest", 16, 0, 2.0f, 0.0f},
            {"soul_sand_valley", 16, 0, 2.0f, 0.0f},
            {"basalt_deltas", 16, 0, 2.0f, 0.0f},
            {"the_end", 8, 0, 0.5f, 0.5f},
            {"end_highlands", 8, 0, 0.5f, 0.5f},
            {"end_midlands", 8, 0, 0.5f, 0.5f},
            {"small_end_islands", 8, 0, 0.5f, 0.5f},
            {"end_barrens", 8, 0, 0.5f, 0.5f},
        };
        return biomes;
    }

    const std::vector<std::string>& BiomeCategoryNames() {
        static const std::vector<std::string> names = {
            "NONE",  "TAIGA",    "EXTREME_HILLS", "JUNGLE",   "MESA",       "PLAINS", "SAVANNA",
            "ICY",   "THE_END",  "BEACH",         "FOREST",   "OCEAN",      "DESERT", "RIVER",
            "SWAMP", "MUSHROOM", "NETHER",        "MOUNTAIN", "UNDERGROUND"};
        return names;
    }

    int JavaBiomeId(std::string_view name) {
        if (name.starts_with("minecraft:"))
            name.remove_prefix(10);
        static const std::map<std::string, std::string, std::less<>> bedrock = {
            {"ice_plains", "snowy_plains"},
            {"ice_plains_spikes", "ice_spikes"},
            {"swampland", "swamp"},
            {"swampland_mutated", "swamp"},
            {"roofed_forest", "dark_forest"},
            {"roofed_forest_mutated", "dark_forest"},
            {"birch_forest_mutated", "old_growth_birch_forest"},
            {"birch_forest_hills_mutated", "old_growth_birch_forest"},
            {"redwood_taiga_mutated", "old_growth_spruce_taiga"},
            {"redwood_taiga_hills_mutated", "old_growth_spruce_taiga"},
            {"mega_taiga", "old_growth_pine_taiga"},
            {"mega_taiga_hills", "old_growth_pine_taiga"},
            {"cold_taiga", "snowy_taiga"},
            {"cold_taiga_hills", "snowy_taiga"},
            {"cold_taiga_mutated", "snowy_taiga"},
            {"extreme_hills", "windswept_hills"},
            {"extreme_hills_edge", "windswept_hills"},
            {"extreme_hills_mutated", "windswept_gravelly_hills"},
            {"extreme_hills_plus_trees", "windswept_forest"},
            {"extreme_hills_plus_trees_mutated", "windswept_gravelly_hills"},
            {"savanna_mutated", "windswept_savanna"},
            {"savanna_plateau_mutated", "windswept_savanna"},
            {"jungle_edge", "sparse_jungle"},
            {"jungle_edge_mutated", "sparse_jungle"},
            {"jungle_mutated", "jungle"},
            {"bamboo_jungle_hills", "bamboo_jungle"},
            {"mesa", "badlands"},
            {"mesa_bryce", "eroded_badlands"},
            {"mesa_plateau", "badlands"},
            {"mesa_plateau_mutated", "badlands"},
            {"mesa_plateau_stone", "wooded_badlands"},
            {"mesa_plateau_stone_mutated", "wooded_badlands"},
            {"cold_beach", "snowy_beach"},
            {"stone_beach", "stony_shore"},
            {"legacy_frozen_ocean", "frozen_ocean"},
            {"mushroom_island", "mushroom_fields"},
            {"mushroom_island_shore", "mushroom_fields"},
            {"hell", "nether_wastes"},
            {"soulsand_valley", "soul_sand_valley"},
            {"flower_forest", "flower_forest"},
            {"sunflower_plains", "sunflower_plains"},
        };
        std::string key(name);
        if (const auto it = bedrock.find(key); it != bedrock.end())
            key = it->second;
        const auto find = [](std::string_view n) -> int {
            const auto& list = JavaBiomes();
            for (size_t i = 0; i < list.size(); i++)
                if (n == list[i].name)
                    return static_cast<int>(i);
            return -1;
        };
        if (const int id = find(key); id >= 0)
            return id;

        for (const char* suffix : {"_hills", "_mutated", "_edge", "_plateau"})
            if (key.ends_with(suffix))
                if (const int id = find(key.substr(0, key.size() - std::char_traits<char>::length(suffix)));
                    id >= 0)
                    return id;
        return 1;
    }

    int BiomePrecipitationAt(int id, double y) {
        const auto& list = JavaBiomes();
        if (id < 0 || id >= static_cast<int>(list.size()) || !list[static_cast<size_t>(id)].precipitation)
            return 0;
        double t = list[static_cast<size_t>(id)].temperature;
        if (y > 80.0)
            t -= (y - 80.0) * 0.05 / 40.0;
        return t < 0.15 ? 2 : 1;
    }
}
