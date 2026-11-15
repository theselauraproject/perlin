#pragma once
#include <string_view>

namespace perlin::frame {
        inline bool TakesItemUse(std::string_view type) {
            if (type == "minecraft:cushion")
                return true;
            if (type.contains("boat") || type.contains("raft"))
                return !type.contains("chest");
            if (type.contains("minecart"))
                return type == "minecraft:minecart";
            return type == "minecraft:horse" || type == "minecraft:donkey" || type == "minecraft:mule" ||
                   type == "minecraft:camel" || type == "minecraft:strider" || type == "minecraft:pig" ||
                   type == "minecraft:skeleton_horse" || type == "minecraft:zombie_horse";
        }

        inline bool InteractionTakesUse(std::string_view type, bool food) {
            if (TakesItemUse(type))
                return true;
            static constexpr std::string_view kTrading[] = {"minecraft:villager", "minecraft:villager_v2",
                                                            "minecraft:wandering_trader", "minecraft:npc",
                                                            "minecraft:armor_stand"};
            for (std::string_view t : kTrading)
                if (type == t)
                    return true;
            return food && type.starts_with("minecraft:");
        }

        inline bool IsInteractable(std::string_view type) {
            if (type == "minecraft:cushion")
                return true;
            if (type.contains("boat") || type.contains("raft"))
                return !type.contains("chest");
            if (type.contains("minecart"))
                return type == "minecraft:minecart";
            static constexpr std::string_view kInert[] = {"minecraft:item",
                                                          "minecraft:xp_orb",
                                                          "minecraft:arrow",
                                                          "minecraft:snowball",
                                                          "minecraft:egg",
                                                          "minecraft:ender_pearl",
                                                          "minecraft:fireball",
                                                          "minecraft:small_fireball",
                                                          "minecraft:thrown_trident",
                                                          "minecraft:splash_potion",
                                                          "minecraft:lingering_potion",
                                                          "minecraft:xp_bottle",
                                                          "minecraft:fishing_hook",
                                                          "minecraft:lightning_bolt",
                                                          "minecraft:area_effect_cloud",
                                                          "minecraft:falling_block",
                                                          "minecraft:tnt"};
            for (std::string_view inert : kInert)
                if (type == inert)
                    return false;
            return !type.empty() && type != "minecraft:player";
        }
}
