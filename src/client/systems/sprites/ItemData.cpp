#include "client/systems/sprites/ItemData.hpp"
#include "util/Strings.hpp"

#include <cstring>
#include <string>

namespace perlin::ui {
    namespace {
        bool Starts(const std::string& n, const char* prefix) {
            return n.starts_with(prefix);
        }
        int Slot(const std::string& n) {
            if (n.ends_with("_helmet"))
                return 0;
            if (n.ends_with("_chestplate"))
                return 1;
            if (n.ends_with("_leggings"))
                return 2;
            if (n.ends_with("_boots"))
                return 3;
            return -1;
        }
    }

    int MaxDurability(const std::string& identifier) {
        const std::string n = std::string(str::StripNamespace(identifier));
        if (const int slot = Slot(n); slot >= 0) {
            static const int base[4] = {11, 16, 15, 13};
            if (n == "turtle_helmet")
                return 275;
            int mult = 0;
            if (Starts(n, "leather"))
                mult = 5;
            else if (Starts(n, "golden"))
                mult = 7;
            else if (Starts(n, "chainmail") || Starts(n, "iron"))
                mult = 15;
            else if (Starts(n, "diamond"))
                mult = 33;
            else if (Starts(n, "netherite"))
                mult = 37;
            else if (Starts(n, "copper"))
                mult = 11;
            return mult * base[slot];
        }
        const bool tool = n.ends_with("_sword") || n.ends_with("_pickaxe") || n.ends_with("_axe") || n.ends_with("_shovel") ||
                          n.ends_with("_hoe");
        if (tool) {
            if (Starts(n, "wooden"))
                return 59;
            if (Starts(n, "stone"))
                return 131;
            if (Starts(n, "iron"))
                return 250;
            if (Starts(n, "golden"))
                return 32;
            if (Starts(n, "diamond"))
                return 1561;
            if (Starts(n, "netherite"))
                return 2031;
            if (Starts(n, "copper"))
                return 190;
            return 0;
        }
        struct Row {
            const char* name;
            int uses;
        };
        static const Row rows[] = {{"bow", 384},
                                   {"crossbow", 465},
                                   {"fishing_rod", 64},
                                   {"shears", 238},
                                   {"flint_and_steel", 64},
                                   {"shield", 336},
                                   {"trident", 250},
                                   {"elytra", 432},
                                   {"carrot_on_a_stick", 25},
                                   {"warped_fungus_on_a_stick", 100},
                                   {"brush", 64},
                                   {"mace", 500},
                                   {"wolf_armor", 64}};
        for (const Row& r : rows)
            if (n == r.name)
                return r.uses;
        return 0;
    }

    int ArmorPoints(const std::string& identifier) {
        const std::string n = std::string(str::StripNamespace(identifier));
        const int slot = Slot(n);
        if (slot < 0)
            return 0;
        if (n == "turtle_helmet")
            return 2;
        static const int leather[4] = {1, 3, 2, 1}, golden[4] = {2, 5, 3, 1}, chain[4] = {2, 5, 4, 1},
                         iron[4] = {2, 6, 5, 2}, diamond[4] = {3, 8, 6, 3}, copper[4] = {2, 4, 3, 1};
        if (Starts(n, "leather"))
            return leather[slot];
        if (Starts(n, "golden"))
            return golden[slot];
        if (Starts(n, "chainmail"))
            return chain[slot];
        if (Starts(n, "iron"))
            return iron[slot];
        if (Starts(n, "diamond") || Starts(n, "netherite"))
            return diamond[slot];
        if (Starts(n, "copper"))
            return copper[slot];
        return 0;
    }

    bool IsFood(const std::string& full) {
        static const char* kFood[] = {"apple",
                                      "golden_apple",
                                      "enchanted_golden_apple",
                                      "bread",
                                      "cooked_beef",
                                      "beef",
                                      "cooked_porkchop",
                                      "porkchop",
                                      "cooked_chicken",
                                      "chicken",
                                      "cooked_mutton",
                                      "mutton",
                                      "cooked_rabbit",
                                      "rabbit",
                                      "cooked_cod",
                                      "cod",
                                      "cooked_salmon",
                                      "salmon",
                                      "tropical_fish",
                                      "pufferfish",
                                      "carrot",
                                      "golden_carrot",
                                      "potato",
                                      "baked_potato",
                                      "poisonous_potato",
                                      "beetroot",
                                      "beetroot_soup",
                                      "mushroom_stew",
                                      "rabbit_stew",
                                      "suspicious_stew",
                                      "melon_slice",
                                      "sweet_berries",
                                      "glow_berries",
                                      "cookie",
                                      "pumpkin_pie",
                                      "cake_slice",
                                      "dried_kelp",
                                      "rotten_flesh",
                                      "spider_eye",
                                      "chorus_fruit",
                                      "honey_bottle",
                                      "milk_bucket",
                                      "potion",
                                      "ominous_bottle"};
        const std::string n = full.starts_with("minecraft:") ? full.substr(10) : full;
        for (const char* f : kFood)
            if (n == f)
                return true;
        return false;
    }
}
