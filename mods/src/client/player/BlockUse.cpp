#include "client/player/BlockUse.hpp"

#include <algorithm>
#include <string_view>
#include <unordered_map>

namespace perlin::blockuse {
    namespace {
        std::string_view Bare(const std::string& name) {
            std::string_view v = name;
            if (v.starts_with("minecraft:"))
                v.remove_prefix(10);
            return v;
        }
        bool Ends(std::string_view v, std::string_view suffix) {
            return v.size() >= suffix.size() && v.substr(v.size() - suffix.size()) == suffix;
        }
        bool OneOf(std::string_view v, std::initializer_list<std::string_view> names) {
            return std::find(names.begin(), names.end(), v) != names.end();
        }
    }

    bool IsReplaceable(const std::string& name) {
        const std::string_view v = Bare(name);
        return v.empty() ||
               OneOf(v, {"air",          "short_grass",    "tallgrass",    "red_shrub",      "tall_grass",
                         "fern",         "large_fern",     "deadbush",     "dead_bush",      "vine",
                         "glow_lichen",  "seagrass",       "water",        "flowing_water",  "lava",
                         "flowing_lava", "fire",           "soul_fire",    "structure_void", "crimson_roots",
                         "warped_roots", "nether_sprouts", "hanging_roots"});
    }

    bool IsReplaceableCell(bool loaded, uint16_t id, const std::string& name) {
        return loaded && (id == 0 || (!name.empty() && IsReplaceable(name)));
    }

    bool IsUpperSlabPlacement(int face, float clickY) {
        return face == 0 || (face >= 2 && face <= 5 && clickY > 0.5f);
    }

    bool IsUpsideDownStairPlacement(int face, float clickY, bool current) {
        if (face == 0)
            return true;
        // top face or lower half keeps the item's own bit
        if (face == 1 || clickY <= 0.5f)
            return current;
        return true;
    }

    int StairDirection(float yaw) {
        // (yaw * 4.0f 0x40800000) / 360.0f 0x43B40000 + 0.5f 0x3F000000, floored: truncate, minus one when above
        const float turns = yaw * 4.0f / 360.0f + 0.5f;
        const int truncated = static_cast<int>(turns);
        const int quarter = (truncated - (static_cast<float>(truncated) > turns ? 1 : 0)) & 3;
        static constexpr int kDirection[4] = {2, 1, 3, 0};
        return kDirection[quarter];
    }

    int PillarAxis(int face) {
        // table 0x14F41DAE0 for faces 2..5; down/up stay 0
        static constexpr int kAxis[4] = {2, 2, 1, 1};
        return face >= 2 && face <= 5 ? kAxis[face - 2] : 0;
    }

    bool IsInteractive(const std::string& name) {
        const std::string_view v = Bare(name);
        if (v == "iron_door" || v == "iron_trapdoor")
            return false;
        if (v == "lever" || Ends(v, "_door") || Ends(v, "_trapdoor") || Ends(v, "_button") ||
            Ends(v, "fence_gate"))
            return true;
        if (Ends(v, "chest") || Ends(v, "shulker_box") || Ends(v, "_bed") || v == "bed")
            return true;
        return OneOf(v, {"crafting_table",
                         "furnace",
                         "lit_furnace",
                         "blast_furnace",
                         "lit_blast_furnace",
                         "smoker",
                         "lit_smoker",
                         "barrel",
                         "anvil",
                         "chipped_anvil",
                         "damaged_anvil",
                         "enchanting_table",
                         "brewing_stand",
                         "hopper",
                         "dropper",
                         "dispenser",
                         "crafter",
                         "loom",
                         "stonecutter_block",
                         "grindstone",
                         "cartography_table",
                         "smithing_table",
                         "beacon",
                         "noteblock",
                         "unpowered_repeater",
                         "powered_repeater",
                         "unpowered_comparator",
                         "powered_comparator",
                         "daylight_detector",
                         "daylight_detector_inverted",
                         "bell",
                         "lectern",
                         "composter",
                         "cake",
                         "jukebox"});
    }

    Result Classify(const std::string& clickedName, const std::string& neighbourName, int clicked[3],
                    int face, bool holdsItem, bool holdsBlock, bool sneaking) {
        static const int kOff[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
        const int f = std::clamp(face, 0, 5);
        Result r;
        const bool clickedReplaceable = IsReplaceable(clickedName);
        for (int i = 0; i < 3; i++)
            r.dest[i] = clickedReplaceable ? clicked[i] : clicked[i] + kOff[f][i];
        if (IsInteractive(clickedName) && !(sneaking && holdsItem)) {
            r.use = Use::Interact;
            return r;
        }
        if (holdsBlock && (clickedReplaceable || IsReplaceable(neighbourName)))
            r.use = Use::Place;
        return r;
    }

    int VanillaUseTicks(const std::string& itemName, bool food) {
        // vanilla items carry no components on the wire: the game's own item classes
        static const std::unordered_map<std::string_view, int> kVanilla = {
            {"bow", 72000},      {"crossbow", 25},     {"trident", 72000},
            {"spyglass", 1200},  {"goat_horn", 140},   {"potion", 32},
            {"milk_bucket", 32}, {"honey_bottle", 40}, {"ominous_bottle", 32},
        };
        if (const auto it = kVanilla.find(Bare(itemName)); it != kVanilla.end())
            return it->second;
        return food ? 32 : 0;
    }
}
