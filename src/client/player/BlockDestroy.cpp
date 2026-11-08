#include "client/player/BlockDestroy.hpp"

#include "util/Resources.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace perlin::destroy {
    namespace {
        const std::unordered_map<std::string, BlockInfo>& Table() {
            static const std::unordered_map<std::string, BlockInfo> table = [] {
                std::unordered_map<std::string, BlockInfo> out;
                std::string text;
                if (!resources::Read("block_destroy.tsv", text))
                    return out;
                size_t pos = 0;
                while (pos < text.size()) {
                    size_t end = text.find('\n', pos);
                    if (end == std::string::npos)
                        end = text.size();
                    const std::string line = text.substr(pos, end - pos);
                    pos = end + 1;
                    if (line.empty() || line[0] == '#')
                        continue;
                    size_t a = line.find('\t'), b = line.find('\t', a + 1), c = line.find('\t', b + 1);
                    if (a == std::string::npos || b == std::string::npos || c == std::string::npos)
                        continue;
                    BlockInfo info;
                    info.known = true;
                    info.hardness = std::strtof(line.substr(a + 1, b - a - 1).c_str(), nullptr);
                    const std::string tool = line.substr(b + 1, c - b - 1);
                    info.tool = tool == "pickaxe"  ? 1
                                : tool == "axe"    ? 2
                                : tool == "shovel" ? 3
                                : tool == "hoe"    ? 4
                                : tool == "shears" ? 5
                                : tool == "sword"  ? 6
                                                   : 0;
                    info.harvestLevel = std::atoi(line.substr(c + 1).c_str());
                    out[line.substr(0, a)] = info;
                }
                return out;
            }();
            return table;
        }

        bool Ends(const std::string& s, const char* suffix) {
            const std::string t(suffix);
            return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
        }
    }

    BlockInfo Lookup(const std::string& blockName) {
        std::string n = blockName;
        if (n.starts_with("minecraft:"))
            n.erase(0, 10);
        const auto& t = Table();
        if (const auto it = t.find(n); it != t.end())
            return it->second;
        return {};
    }

    float ProgressPerTick(const BlockInfo& block, const Conditions& c) {
        if (block.hardness < 0.0f)
            return 0.0f;
        if (c.creative || block.hardness == 0.0f)
            return 1.0f;
        std::string held = c.heldItem;
        if (held.starts_with("minecraft:"))
            held.erase(0, 10);
        int heldTool = 0;
        if (Ends(held, "_pickaxe"))
            heldTool = 1;
        else if (Ends(held, "_axe"))
            heldTool = 2;
        else if (Ends(held, "_shovel"))
            heldTool = 3;
        else if (Ends(held, "_hoe"))
            heldTool = 4;
        else if (held == "shears")
            heldTool = 5;
        else if (Ends(held, "_sword"))
            heldTool = 6;
        float speed = 1.0f;
        int level = 0;
        if (heldTool != 0 && heldTool != 5) {
            const auto is = [&](const char* m) { return held.starts_with(m); };
            if (is("wooden_")) {
                speed = 2;
                level = 1;
            } else if (is("stone_")) {
                speed = 4;
                level = 2;
            } else if (is("copper_")) {
                speed = 5;
                level = 2;
            } else if (is("iron_")) {
                speed = 6;
                level = 3;
            } else if (is("diamond_")) {
                speed = 8;
                level = 4;
            } else if (is("netherite_")) {
                speed = 9;
                level = 5;
            } else if (is("golden_")) {
                speed = 12;
                level = 1;
            }
        }
        float used = 1.0f;
        if (heldTool == 5 && block.tool == 5)
            used = 5.0f;
        else if (heldTool == 6 && block.tool == 6)
            used = 15.0f;
        else if (heldTool != 0 && heldTool == block.tool && heldTool != 5 && heldTool != 6)
            used = speed;
        else if (heldTool == 6 && block.tool == 0)
            used = 1.0f;
        if (used > 1.0f && c.efficiency > 0)
            used += static_cast<float>(c.efficiency * c.efficiency + 1);

        if (c.haste > 0)
            used *= (1.0f + 0.2f * static_cast<float>(c.haste)) *
                    static_cast<float>(std::pow(1.2, static_cast<double>(c.haste)));
        if (c.miningFatigue > 0)
            used *= static_cast<float>(std::pow(0.3, static_cast<double>(c.miningFatigue))) *
                    static_cast<float>(std::pow(0.7, static_cast<double>(c.miningFatigue)));
        if (c.inWater)
            used /= 5.0f;
        if (!c.onGround)
            used /= 5.0f;
        const bool canHarvest = block.harvestLevel == 0 || (heldTool == 1 && level >= block.harvestLevel);
        return used / block.hardness / (canHarvest ? 30.0f : 100.0f);
    }
}

namespace perlin::destroy {
    namespace {
        std::string Bare(const std::string& n) {
            return n.starts_with("minecraft:") ? n.substr(10) : n;
        }
        bool Has(const std::string& s, const char* part) {
            return s.contains(part);
        }
    }

    bool NeedsSupportBelow(const std::string& blockName) {
        const std::string n = Bare(blockName);
        static const char* kExact[] = {
            "short_grass",     "tall_grass",     "fern",           "large_fern",     "dead_bush",
            "deadbush",        "seagrass",       "lily_pad",       "waterlily",      "sugar_cane",
            "reeds",           "cactus",         "bamboo",         "bamboo_sapling", "sweet_berry_bush",
            "wheat",           "carrots",        "potatoes",       "beetroot",       "melon_stem",
            "pumpkin_stem",    "nether_wart",    "dandelion",      "poppy",          "blue_orchid",
            "allium",          "azure_bluet",    "oxeye_daisy",    "cornflower",     "lily_of_the_valley",
            "wither_rose",     "torchflower",    "pitcher_plant",  "pitcher_crop",   "torchflower_crop",
            "sunflower",       "lilac",          "rose_bush",      "peony",          "double_plant",
            "yellow_flower",   "red_flower",     "brown_mushroom", "red_mushroom",   "torch",
            "soul_torch",      "redstone_torch", "copper_torch",   "redstone_wire",  "rail",
            "golden_rail",     "detector_rail",  "activator_rail", "snow_layer",     "snow",
            "pink_petals",     "wildflowers",    "leaf_litter",    "firefly_bush",   "bush",
            "short_dry_grass", "tall_dry_grass", "cave_vines",     "hanging_roots",  "frogspawn",
            "sapling"};
        for (const char* e : kExact)
            if (n == e)
                return true;
        return Has(n, "_sapling") || Has(n, "_carpet") || Has(n, "_pressure_plate") || Has(n, "_tulip") ||
               Has(n, "_door") || Has(n, "_banner") || Has(n, "_sign") || Has(n, "_candle") ||
               n == "moss_carpet" || n == "pale_moss_carpet" || n == "repeater" ||
               n == "unpowered_repeater" || n == "powered_repeater" || n == "comparator" ||
               n == "unpowered_comparator" || n == "powered_comparator" || n == "powered_repeater";
    }

    bool BreaksWholeStack(const std::string& blockName) {
        const std::string n = Bare(blockName);
        return n == "sugar_cane" || n == "reeds" || n == "cactus" || n == "bamboo" || n == "tall_grass" ||
               n == "large_fern" || n == "sunflower" || n == "lilac" || n == "rose_bush" || n == "peony" ||
               n == "double_plant" || Has(n, "_door") || n == "tall_dry_grass" || n == "pitcher_plant";
    }
}
