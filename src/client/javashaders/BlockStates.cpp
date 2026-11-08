#include "BlockStates.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace perlin::jshader {
    namespace {
        std::string_view StripNs(std::string_view n) {
            if (n.substr(0, 10) == "minecraft:")
                n.remove_prefix(10);
            return n;
        }

        bool EndsWith(std::string_view s, std::string_view suffix) {
            return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
        }
        bool StartsWith(std::string_view s, std::string_view prefix) {
            return s.substr(0, prefix.size()) == prefix;
        }

        const char* Bool(bool b) {
            return b ? "true" : "false";
        }

        struct Reader {
            const StateList& s;
            const std::string* Get(std::string_view key) const {
                for (const auto& kv : s)
                    if (kv.first == key)
                        return &kv.second;
                return nullptr;
            }
            bool Has(std::string_view key) const { return Get(key) != nullptr; }
            bool B(std::string_view key) const {
                const std::string* v = Get(key);
                return v && (*v == "1" || *v == "true");
            }
            int I(std::string_view key, int fallback = 0) const {
                const std::string* v = Get(key);
                if (!v)
                    return fallback;
                if (*v == "true")
                    return 1;
                if (*v == "false")
                    return 0;
                return std::atoi(v->c_str());
            }
            std::string S(std::string_view key, const char* fallback = "") const {
                const std::string* v = Get(key);
                return v ? *v : std::string(fallback);
            }
        };

        const char* RenameBlock(std::string_view n) {
            static const std::pair<const char*, const char*> table[] = {
                {"flowing_water", "water"},
                {"flowing_lava", "lava"},
                {"snow", "snow_block"},
                {"snow_layer", "snow"},
                {"grass", "grass_block"},
                {"tallgrass", "short_grass"},
                {"web", "cobweb"},
                {"waterlily", "lily_pad"},
                {"reeds", "sugar_cane"},
                {"deadbush", "dead_bush"},
                {"portal", "nether_portal"},
                {"mob_spawner", "spawner"},
                {"brick_block", "bricks"},
                {"nether_brick", "nether_bricks"},
                {"red_nether_brick", "red_nether_bricks"},
                {"end_bricks", "end_stone_bricks"},
                {"end_brick_stairs", "end_stone_brick_stairs"},
                {"prismarine_bricks_stairs", "prismarine_brick_stairs"},
                {"normal_stone_slab", "stone_slab"},
                {"normal_stone_stairs", "stone_stairs"},
                {"hardened_clay", "terracotta"},
                {"silver_glazed_terracotta", "light_gray_glazed_terracotta"},
                {"quartz_ore", "nether_quartz_ore"},
                {"magma", "magma_block"},
                {"slime", "slime_block"},
                {"noteblock", "note_block"},
                {"melon_block", "melon"},
                {"lit_pumpkin", "jack_o_lantern"},
                {"golden_rail", "powered_rail"},
                {"grass_path", "dirt_path"},
                {"dirt_with_roots", "rooted_dirt"},
                {"stonecutter_block", "stonecutter"},
                {"trip_wire", "tripwire"},
                {"wooden_door", "oak_door"},
                {"wooden_button", "oak_button"},
                {"wooden_pressure_plate", "oak_pressure_plate"},
                {"trapdoor", "oak_trapdoor"},
                {"fence_gate", "oak_fence_gate"},
                {"standing_sign", "oak_sign"},
                {"wall_sign", "oak_wall_sign"},
                {"darkoak_standing_sign", "dark_oak_sign"},
                {"darkoak_wall_sign", "dark_oak_wall_sign"},
                {"standing_banner", "white_banner"},
                {"wall_banner", "white_wall_banner"},
                {"bed", "red_bed"},
                {"skull", "skeleton_skull"},
                {"invisible_bedrock", "barrier"},
                {"glowingobsidian", "obsidian"},
                {"azalea_leaves_flowered", "flowering_azalea_leaves"},
                {"small_dripleaf_block", "small_dripleaf"},
                {"beetroot", "beetroots"},
                {"moving_block", "moving_piston"},
                {"piston_arm_collision", "piston_head"},
                {"sticky_piston_arm_collision", "piston_head"},
                {"undyed_shulker_box", "shulker_box"},
                {"waxed_copper", "waxed_copper_block"},
                {"frog_spawn", "frogspawn"},
                {"underwater_torch", "torch"},
                {"underwater_tnt", "tnt"},
                {"deprecated_anvil", "anvil"},
                {"frame", "item_frame"},
                {"glow_frame", "glow_item_frame"},
                {"daylight_detector_inverted", "daylight_detector"},
                {"lit_furnace", "furnace"},
                {"lit_blast_furnace", "blast_furnace"},
                {"lit_smoker", "smoker"},
                {"lit_redstone_lamp", "redstone_lamp"},
                {"lit_redstone_ore", "redstone_ore"},
                {"lit_deepslate_redstone_ore", "deepslate_redstone_ore"},
                {"unlit_redstone_torch", "redstone_torch"},
                {"powered_repeater", "repeater"},
                {"unpowered_repeater", "repeater"},
                {"powered_comparator", "comparator"},
                {"unpowered_comparator", "comparator"},
                {"cave_vines_body_with_berries", "cave_vines_plant"},
                {"cave_vines_head_with_berries", "cave_vines"},
                {"border_block", "barrier"},
                {"allow", "barrier"},
                {"deny", "barrier"},
            };
            for (const auto& e : table)
                if (n == e.first)
                    return e.second;
            return nullptr;
        }

        void Set(StateList& p, const char* key, std::string value) {
            for (auto& kv : p)
                if (kv.first == key) {
                    kv.second = std::move(value);
                    return;
                }
            p.emplace_back(key, std::move(value));
        }

        const char* kTrialStates[] = {"inactive",        "waiting_for_players",
                                      "active",          "waiting_for_reward_ejection",
                                      "ejecting_reward", "cooldown"};
        const char* kSculkPhases[] = {"inactive", "active", "cooldown"};
    }

    const std::string* JavaBlock::Find(std::string_view key) const {
        for (const auto& kv : props)
            if (kv.first == key)
                return &kv.second;
        return nullptr;
    }

    JavaBlock ToJavaBlock(std::string_view bedrockName, const StateList& states) {
        const std::string_view n = StripNs(bedrockName);
        const Reader r{states};
        JavaBlock out;
        if (const char* renamed = RenameBlock(n))
            out.name = renamed;
        else
            out.name = std::string(n);
        StateList& p = out.props;

        if (StartsWith(n, "lit_"))
            Set(p, "lit", "true");
        else if (n == "furnace" || n == "blast_furnace" || n == "smoker" || n == "redstone_lamp" ||
                 n == "redstone_ore" || n == "deepslate_redstone_ore")
            Set(p, "lit", "false");
        if (n == "unlit_redstone_torch")
            Set(p, "lit", "false");
        else if (n == "redstone_torch")
            Set(p, "lit", "true");
        if (n == "powered_repeater" || n == "powered_comparator")
            Set(p, "powered", "true");
        else if (n == "unpowered_repeater" || n == "unpowered_comparator")
            Set(p, "powered", "false");
        if (n == "daylight_detector_inverted")
            Set(p, "inverted", "true");
        else if (n == "daylight_detector")
            Set(p, "inverted", "false");
        if (n == "sticky_piston_arm_collision")
            Set(p, "type", "sticky");
        else if (n == "piston_arm_collision")
            Set(p, "type", "normal");
        if (n == "cave_vines_body_with_berries" || n == "cave_vines_head_with_berries")
            Set(p, "berries", "true");
        else if (n == "cave_vines")
            Set(p, "berries", "false");
        if (StartsWith(n, "light_block_")) {
            out.name = "light";
            Set(p, "level", std::string(n.substr(12)));
        }
        if (StartsWith(n, "colored_torch_"))
            out.name = "torch";
        if (n == "piston" || n == "sticky_piston")
            Set(p, "extended", "false");

        if (EndsWith(n, "_double_slab")) {
            std::string base(n.substr(0, n.size() - 12));
            if (base == "double_cut_copper")
                base = "cut_copper";
            else if (StartsWith(base, "waxed_") && EndsWith(base, "double_cut_copper"))
                base = base.substr(0, base.size() - 17) + "cut_copper";
            else if (EndsWith(base, "_double_cut_copper"))
                base = base.substr(0, base.size() - 17) + "cut_copper";
            else if (base == "normal_stone")
                base = "stone";
            else if (base == "stone_brick")
                base = "stone_brick";
            out.name = base + "_slab";
            Set(p, "type", "double");
        } else if (StartsWith(n, "double_cut_copper_slab") || EndsWith(n, "_double_cut_copper_slab")) {
            std::string base(n);
            const size_t at = base.find("double_");
            base.erase(at, 7);
            out.name = base;
            Set(p, "type", "double");
        } else if (EndsWith(out.name, "_slab") && r.Has("minecraft:vertical_half")) {
            Set(p, "type", r.S("minecraft:vertical_half") == "top" ? "top" : "bottom");
        }

        if (EndsWith(n, "_standing_sign") && n != "darkoak_standing_sign")
            out.name = std::string(n.substr(0, n.size() - 14)) + "_sign";

        if (r.Has("torch_facing_direction")) {
            const std::string f = r.S("torch_facing_direction");
            if (f != "top" && f != "unknown") {
                if (out.name == "torch")
                    out.name = "wall_torch";
                else if (out.name == "soul_torch")
                    out.name = "soul_wall_torch";
                else if (out.name == "redstone_torch")
                    out.name = "redstone_wall_torch";
                else if (out.name == "copper_torch")
                    out.name = "copper_wall_torch";
                Set(p, "facing", f);
            }
        }

        if (r.Has("upper_block_bit"))
            Set(p, "half", r.B("upper_block_bit") ? "upper" : "lower");

        if (r.Has("upside_down_bit"))
            Set(p, "half", r.B("upside_down_bit") ? "top" : "bottom");
        if (n == "seagrass") {
            const std::string t = r.S("sea_grass_type", "default");
            if (t != "default") {
                out.name = "tall_seagrass";
                Set(p, "half", t == "double_top" ? "upper" : "lower");
            }
        }
        if (n == "kelp")
            out.name = "kelp_plant";

        if (r.Has("lit"))
            Set(p, "lit", Bool(r.B("lit")));
        if (r.Has("extinguished"))
            Set(p, "lit", Bool(!r.B("extinguished")));
        if (r.Has("hanging"))
            Set(p, "hanging", Bool(r.B("hanging")));
        if (r.Has("ominous"))
            Set(p, "ominous", Bool(r.B("ominous")));
        if (r.Has("active"))
            Set(p, "active", Bool(r.B("active")));
        if (r.Has("vault_state"))
            Set(p, "vault_state", r.S("vault_state"));
        if (r.Has("creaking_heart_state"))
            Set(p, "creaking_heart_state", r.S("creaking_heart_state"));
        if (r.Has("trial_spawner_state"))
            Set(p, "trial_spawner_state", kTrialStates[std::clamp(r.I("trial_spawner_state"), 0, 5)]);
        if (r.Has("sculk_sensor_phase"))
            Set(p, "sculk_sensor_phase", kSculkPhases[std::clamp(r.I("sculk_sensor_phase"), 0, 2)]);
        if (r.Has("redstone_signal")) {
            const int s = std::clamp(r.I("redstone_signal"), 0, 15);
            Set(p, "power", std::to_string(s));
            if (EndsWith(out.name, "pressure_plate"))
                Set(p, "powered", Bool(s > 0));
        }
        if (r.Has("moisturized_amount"))
            Set(p, "moisture", std::to_string(r.I("moisturized_amount")));
        if (r.Has("output_subtract_bit"))
            Set(p, "mode", r.B("output_subtract_bit") ? "subtract" : "compare");
        if (r.Has("open_bit")) {
            if (out.name == "lever")
                Set(p, "powered", Bool(r.B("open_bit")));
            else
                Set(p, "open", Bool(r.B("open_bit")));
        }
        if (r.Has("button_pressed_bit"))
            Set(p, "powered", Bool(r.B("button_pressed_bit")));
        if (r.Has("rail_data_bit"))
            Set(p, "powered", Bool(r.B("rail_data_bit")));
        if (r.Has("powered_bit"))
            Set(p, "powered", Bool(r.B("powered_bit")));
        if (r.Has("end_portal_eye_bit"))
            Set(p, "eye", Bool(r.B("end_portal_eye_bit")));
        if (r.Has("respawn_anchor_charge"))
            Set(p, "charges", std::to_string(r.I("respawn_anchor_charge")));
        if (r.Has("height") && out.name == "snow")
            Set(p, "layers", std::to_string(std::clamp(r.I("height") + 1, 1, 8)));
        if (r.Has("candles"))
            Set(p, "candles", std::to_string(r.I("candles") + 1));
        if (r.Has("honey_level"))
            Set(p, "honey_level", std::to_string(r.I("honey_level")));
        if (r.Has("composter_fill_level")) {
            const int l = r.I("composter_fill_level");
            Set(p, "level", std::to_string(l));
        }
        if (r.Has("dead_bit") && out.name == "sea_pickle")
            Set(p, "waterlogged", Bool(!r.B("dead_bit")));
        if (r.Has("cluster_count") && out.name == "sea_pickle")
            Set(p, "pickles", std::to_string(r.I("cluster_count") + 1));
        if (r.Has("repeater_delay"))
            Set(p, "delay", std::to_string(r.I("repeater_delay") + 1));
        if (r.Has("pillar_axis"))
            Set(p, "axis", r.S("pillar_axis"));
        if (r.Has("minecraft:cardinal_direction"))
            Set(p, "facing", r.S("minecraft:cardinal_direction"));
        else if (r.Has("minecraft:facing_direction"))
            Set(p, "facing", r.S("minecraft:facing_direction"));
        if (r.Has("bamboo_leaf_size")) {
            const std::string l = r.S("bamboo_leaf_size");
            Set(p, "leaves", l == "large_leaves" ? "large" : l == "small_leaves" ? "small" : "none");
        }
        if (r.Has("age"))
            Set(p, "age", std::to_string(r.I("age")));
        if (r.Has("growth")) {
            const int g = std::clamp(r.I("growth"), 0, 7);

            const bool four = out.name == "beetroots" || out.name == "sweet_berry_bush" ||
                              out.name == "nether_wart" || out.name == "torchflower_crop";
            Set(p, "age", std::to_string(four ? g / 2 : g));
        }
        if (r.Has("propagule_stage"))
            Set(p, "age", std::to_string(r.I("propagule_stage")));

        if (out.name == "cauldron") {
            const int fill = r.I("fill_level");
            const std::string liquid = r.S("cauldron_liquid", "water");
            if (fill > 0) {
                if (liquid == "lava") {
                    out.name = "lava_cauldron";
                } else {
                    out.name = liquid == "powder_snow" ? "powder_snow_cauldron" : "water_cauldron";
                    Set(p, "level", std::to_string(std::clamp((fill + 1) / 2, 1, 3)));
                }
            } else {
                Set(p, "level", "0");
            }
        }

        if (!out.Find("waterlogged"))
            Set(p, "waterlogged", "false");
        if (out.name == "grass_block" || out.name == "podzol" || out.name == "mycelium")
            Set(p, "snowy", "false");
        return out;
    }

    JavaBlock ToJavaBlock(std::string_view encoded) {
        StateList states;
        size_t cut = encoded.find('|');
        const std::string_view name = encoded.substr(0, cut);
        while (cut != std::string_view::npos) {
            const size_t next = encoded.find('|', cut + 1);
            const std::string_view kv = encoded.substr(
                cut + 1, next == std::string_view::npos ? std::string_view::npos : next - cut - 1);
            const size_t e = kv.find('=');
            if (e != std::string_view::npos)
                states.emplace_back(std::string(kv.substr(0, e)), std::string(kv.substr(e + 1)));
            cut = next;
        }
        return ToJavaBlock(name, states);
    }

    std::string EncodeBedrockBlock(std::string_view name, const StateList& states) {
        std::string out(name);
        for (const auto& kv : states) {
            out += '|';
            out += kv.first;
            out += '=';
            out += kv.second;
        }
        return out;
    }

    std::string JavaEntityName(std::string_view bedrockName) {
        const std::string_view n = StripNs(bedrockName);
        static const std::pair<const char*, const char*> table[] = {
            {"xp_orb", "experience_orb"},
            {"xp_bottle", "experience_bottle"},
            {"ender_crystal", "end_crystal"},
            {"thrown_trident", "trident"},
            {"zombie_pigman", "zombified_piglin"},
            {"evocation_illager", "evoker"},
            {"evocation_fang", "evoker_fangs"},
            {"villager_v2", "villager"},
            {"zombie_villager_v2", "zombie_villager"},
            {"splash_potion", "potion"},
            {"lingering_potion", "potion"},
            {"fireworks_rocket", "firework_rocket"},
            {"fishing_hook", "fishing_bobber"},
            {"leash_knot", "leash_knot"},
            {"tnt_minecart", "tnt_minecart"},
            {"hopper_minecart", "hopper_minecart"},
            {"command_block_minecart", "command_block_minecart"},
            {"chest_boat", "oak_chest_boat"},
            {"boat", "oak_boat"},
            {"lightning_bolt", "lightning_bolt"},
            {"agent", "armor_stand"},
            {"npc", "villager"},
            {"wind_charge_projectile", "wind_charge"},
            {"breeze_wind_charge_projectile", "breeze_wind_charge"},
            {"ice_bomb", "snowball"},
            {"balloon", "armor_stand"},
        };
        for (const auto& e : table)
            if (n == e.first)
                return e.second;
        return std::string(n);
    }

    std::string JavaItemName(std::string_view bedrockName) {
        const std::string_view n = StripNs(bedrockName);
        static const std::pair<const char*, const char*> table[] = {
            {"lit_pumpkin", "jack_o_lantern"},
            {"web", "cobweb"},
            {"waterlily", "lily_pad"},
            {"reeds", "sugar_cane"},
            {"deadbush", "dead_bush"},
            {"slime", "slime_block"},
            {"magma", "magma_block"},
            {"noteblock", "note_block"},
            {"melon_block", "melon"},
            {"golden_rail", "powered_rail"},
            {"grass_path", "dirt_path"},
            {"snow_layer", "snow"},
            {"snow", "snow_block"},
            {"brick_block", "bricks"},
            {"nether_brick", "nether_bricks"},
            {"netherbrick", "nether_brick"},
            {"red_nether_brick", "red_nether_bricks"},
            {"end_bricks", "end_stone_bricks"},
            {"hardened_clay", "terracotta"},
            {"quartz_ore", "nether_quartz_ore"},
            {"wooden_door", "oak_door"},
            {"trapdoor", "oak_trapdoor"},
            {"fence_gate", "oak_fence_gate"},
            {"wooden_button", "oak_button"},
            {"wooden_pressure_plate", "oak_pressure_plate"},
            {"sign", "oak_sign"},
            {"frame", "item_frame"},
            {"glow_frame", "glow_item_frame"},
            {"fireworks", "firework_rocket"},
            {"firework_rocket", "firework_rocket"},
            {"bed", "red_bed"},
            {"skull", "skeleton_skull"},
            {"appleenchanted", "enchanted_golden_apple"},
            {"enchanted_golden_apple", "enchanted_golden_apple"},
            {"carrotonastick", "carrot_on_a_stick"},
            {"warped_fungus_on_a_stick", "warped_fungus_on_a_stick"},
            {"speckled_melon", "glistering_melon_slice"},
            {"melon_slice", "melon_slice"},
            {"fish", "cod"},
            {"cooked_fish", "cooked_cod"},
            {"clownfish", "tropical_fish"},
            {"muttoncooked", "cooked_mutton"},
            {"muttonraw", "mutton"},
            {"chorus_fruit_popped", "popped_chorus_fruit"},
            {"turtle_shell_piece", "turtle_scute"},
            {"scute", "turtle_scute"},
            {"lodestonecompass", "compass"},
            {"lodestone_compass", "compass"},
            {"spyglass", "spyglass"},
            {"record_13", "music_disc_13"},
            {"banner_pattern", "flower_banner_pattern"},
            {"boat", "oak_boat"},
            {"chest_boat", "oak_chest_boat"},
            {"emptymap", "map"},
            {"empty_map", "map"},
            {"filled_map", "filled_map"},
            {"redstone_wire", "redstone"},
            {"unpowered_repeater", "repeater"},
            {"unpowered_comparator", "comparator"},
            {"lit_redstone_lamp", "redstone_lamp"},
            {"unlit_redstone_torch", "redstone_torch"},
            {"azalea_leaves_flowered", "flowering_azalea_leaves"},
            {"dirt_with_roots", "rooted_dirt"},
            {"undyed_shulker_box", "shulker_box"},
        };
        for (const auto& e : table)
            if (n == e.first)
                return e.second;
        if (StartsWith(n, "record_"))
            return "music_disc_" + std::string(n.substr(7));
        return std::string(n);
    }

    bool BlockInTag(std::string_view tag, std::string_view b) {
        tag = StripNs(tag);
        auto any = [&](std::initializer_list<const char*> suffixes) {
            for (const char* s : suffixes)
                if (EndsWith(b, s))
                    return true;
            return false;
        };
        if (tag == "leaves")
            return EndsWith(b, "_leaves");
        if (tag == "logs" || tag == "logs_that_burn" || tag == "overworld_natural_logs")
            return any({"_log", "_wood", "_stem", "_hyphae"}) &&
                   (tag != "overworld_natural_logs" || (EndsWith(b, "_log") && !StartsWith(b, "stripped_")));
        if (tag == "planks")
            return EndsWith(b, "_planks");
        if (tag == "wool")
            return EndsWith(b, "_wool");
        if (tag == "wool_carpets")
            return EndsWith(b, "_carpet") && b != "moss_carpet" && b != "pale_moss_carpet";
        if (tag == "slabs")
            return EndsWith(b, "_slab");
        if (tag == "stairs")
            return EndsWith(b, "_stairs");
        if (tag == "walls")
            return EndsWith(b, "_wall") && !EndsWith(b, "_sign_wall");
        if (tag == "fences" || tag == "wooden_fences")
            return EndsWith(b, "_fence") && (tag == "fences" || b != "nether_brick_fence");
        if (tag == "fence_gates")
            return EndsWith(b, "_fence_gate");
        if (tag == "doors" || tag == "wooden_doors")
            return EndsWith(b, "_door") &&
                   (tag == "doors" || (b != "iron_door" && !EndsWith(b, "copper_door")));
        if (tag == "trapdoors" || tag == "wooden_trapdoors")
            return EndsWith(b, "_trapdoor") &&
                   (tag == "trapdoors" || (b != "iron_trapdoor" && !EndsWith(b, "copper_trapdoor")));
        if (tag == "buttons")
            return EndsWith(b, "_button");
        if (tag == "pressure_plates")
            return EndsWith(b, "_pressure_plate");
        if (tag == "saplings")
            return EndsWith(b, "_sapling") || b == "mangrove_propagule" || b == "azalea" ||
                   b == "flowering_azalea";
        if (tag == "small_flowers" || tag == "flowers")
            return b == "dandelion" || b == "poppy" || b == "blue_orchid" || b == "allium" ||
                   b == "azure_bluet" || EndsWith(b, "_tulip") || b == "oxeye_daisy" || b == "cornflower" ||
                   b == "lily_of_the_valley" || b == "wither_rose" || b == "torchflower" ||
                   b == "open_eyeblossom" || b == "closed_eyeblossom" ||
                   (tag == "flowers" &&
                    (b == "sunflower" || b == "lilac" || b == "peony" || b == "rose_bush" ||
                     b == "pitcher_plant" || b == "pink_petals" || b == "wildflowers" ||
                     b == "flowering_azalea_leaves" || b == "flowering_azalea" || b == "mangrove_propagule" ||
                     b == "cherry_leaves" || b == "chorus_flower" || b == "spore_blossom"));
        if (tag == "crops")
            return b == "wheat" || b == "carrots" || b == "potatoes" || b == "beetroots" ||
                   b == "melon_stem" || b == "pumpkin_stem" || b == "torchflower_crop" || b == "pitcher_crop";
        if (tag == "candles")
            return b == "candle" || (EndsWith(b, "_candle"));
        if (tag == "candle_cakes")
            return EndsWith(b, "candle_cake");
        if (tag == "beds")
            return EndsWith(b, "_bed");
        if (tag == "banners")
            return EndsWith(b, "_banner");
        if (tag == "signs" || tag == "standing_signs")
            return EndsWith(b, "_sign") && !EndsWith(b, "_hanging_sign") && !EndsWith(b, "_wall_sign");
        if (tag == "wall_signs")
            return EndsWith(b, "_wall_sign") && !EndsWith(b, "_wall_hanging_sign");
        if (tag == "all_signs")
            return EndsWith(b, "_sign");
        if (tag == "ceiling_hanging_signs")
            return EndsWith(b, "_hanging_sign") && !EndsWith(b, "_wall_hanging_sign");
        if (tag == "wall_hanging_signs")
            return EndsWith(b, "_wall_hanging_sign");
        if (tag == "all_hanging_signs")
            return EndsWith(b, "_hanging_sign");
        if (tag == "shulker_boxes")
            return EndsWith(b, "shulker_box");
        if (tag == "terracotta")
            return b == "terracotta" || (EndsWith(b, "_terracotta") && !EndsWith(b, "_glazed_terracotta"));
        if (tag == "coral_blocks")
            return EndsWith(b, "_coral_block") && !StartsWith(b, "dead_");
        if (tag == "corals")
            return (EndsWith(b, "_coral") || EndsWith(b, "_coral_fan")) && !StartsWith(b, "dead_");
        if (tag == "wall_corals")
            return EndsWith(b, "_coral_wall_fan") && !StartsWith(b, "dead_");
        if (tag == "ice")
            return b == "ice" || b == "packed_ice" || b == "blue_ice" || b == "frosted_ice";
        if (tag == "sand")
            return b == "sand" || b == "red_sand" || b == "suspicious_sand";
        if (tag == "dirt")
            return b == "dirt" || b == "grass_block" || b == "podzol" || b == "coarse_dirt" ||
                   b == "mycelium" || b == "rooted_dirt" || b == "moss_block" || b == "mud" ||
                   b == "muddy_mangrove_roots" || b == "pale_moss_block";
        if (tag == "base_stone_overworld")
            return b == "stone" || b == "granite" || b == "diorite" || b == "andesite" || b == "tuff" ||
                   b == "deepslate";
        if (tag == "base_stone_nether")
            return b == "netherrack" || b == "basalt" || b == "blackstone";
        if (tag == "stone_bricks")
            return b == "stone_bricks" || b == "mossy_stone_bricks" || b == "cracked_stone_bricks" ||
                   b == "chiseled_stone_bricks";
        if (tag == "nylium")
            return b == "crimson_nylium" || b == "warped_nylium";
        if (tag == "wart_blocks")
            return b == "nether_wart_block" || b == "warped_wart_block";
        if (tag == "rails")
            return b == "rail" || b == "powered_rail" || b == "detector_rail" || b == "activator_rail";
        if (tag == "fire")
            return b == "fire" || b == "soul_fire";
        if (tag == "campfires")
            return b == "campfire" || b == "soul_campfire";
        if (tag == "cauldrons")
            return EndsWith(b, "cauldron");
        if (tag == "climbable")
            return b == "ladder" || b == "vine" || b == "scaffolding" || b == "weeping_vines" ||
                   b == "weeping_vines_plant" || b == "twisting_vines" || b == "twisting_vines_plant" ||
                   b == "cave_vines" || b == "cave_vines_plant";
        if (tag == "replaceable_by_trees" || tag == "replaceable")
            return b == "short_grass" || b == "fern" || b == "dead_bush" || b == "vine" ||
                   b == "glow_lichen" || b == "sunflower" || b == "lilac" || b == "rose_bush" ||
                   b == "peony" || b == "tall_grass" || b == "large_fern" || b == "hanging_roots" ||
                   b == "pitcher_plant" || b == "water" || b == "seagrass" || b == "tall_seagrass" ||
                   b == "warped_roots" || b == "nether_sprouts" || b == "crimson_roots" ||
                   EndsWith(b, "_leaves");
        if (tag == "copper")
            return b.find("copper") != std::string_view::npos;
        if (tag == "anvil")
            return EndsWith(b, "anvil");
        if (tag == "glass" || tag == "impermeable")
            return b == "glass" || b == "tinted_glass" || EndsWith(b, "_stained_glass");
        if (tag == "glass_panes")
            return b == "glass_pane" || EndsWith(b, "_stained_glass_pane");
        if (tag == "lanterns")
            return EndsWith(b, "lantern") && b != "jack_o_lantern" && b != "sea_lantern";
        if (tag == "chains")
            return EndsWith(b, "chain");
        if (tag == "bars")
            return EndsWith(b, "_bars");
        if (tag == "beehives")
            return b == "beehive" || b == "bee_nest";
        if (tag == "portals")
            return b == "nether_portal" || b == "end_portal" || b == "end_gateway";
        if (tag == "snow")
            return b == "snow" || b == "snow_block" || b == "powder_snow";
        if (tag == "sword_efficient" || tag == "mineable/hoe")
            return EndsWith(b, "_leaves");
        return false;
    }

    namespace {

        bool ParseEntry(std::string_view entry, std::string& name,
                        std::map<std::string, std::string>& preds) {
            std::vector<std::string_view> parts;
            size_t start = 0;
            for (;;) {
                const size_t c = entry.find(':', start);
                parts.push_back(
                    entry.substr(start, c == std::string_view::npos ? std::string_view::npos : c - start));
                if (c == std::string_view::npos)
                    break;
                start = c + 1;
            }
            if (parts.empty() || parts[0].empty())
                return false;
            size_t statesStart = 1;
            std::string_view ns = "minecraft", path = parts[0];
            if (parts.size() >= 2 && !parts[1].contains('=')) {
                ns = parts[0];
                path = parts[1];
                statesStart = 2;
            }
            if (ns != "minecraft")
                return false;
            name = std::string(path);
            for (size_t i = statesStart; i < parts.size(); i++) {
                const size_t e = parts[i].find('=');
                if (e == std::string_view::npos || parts[i].find('=', e + 1) != std::string_view::npos)
                    continue;
                preds[std::string(parts[i].substr(0, e))] = std::string(parts[i].substr(e + 1));
            }
            return true;
        }
    }

    void BlockIdMatcher::Build(const std::vector<std::pair<int, std::vector<std::string>>>& ordered) {
        byName.clear();
        tags.clear();
        size_t order = 0;
        for (const auto& line : ordered)
            for (const std::string& raw : line.second) {
                if (raw.empty())
                    continue;
                if (raw[0] == '%' || raw[0] == '#') {
                    TagRule t;
                    std::string name;
                    std::string stripped = raw.substr(1);
                    stripped.erase(std::remove(stripped.begin(), stripped.end(), '%'), stripped.end());
                    if (!ParseEntry(stripped, name, t.predicates))
                        continue;
                    t.tag = name;
                    t.id = line.first;
                    tags.push_back(std::move(t));
                    continue;
                }
                Rule rule;
                std::string name;
                if (!ParseEntry(raw, name, rule.predicates))
                    continue;
                rule.id = line.first;
                rule.order = order++;
                byName[name].push_back(std::move(rule));
            }
    }

    bool BlockIdMatcher::Matches(const std::map<std::string, std::string>& predicates,
                                 const JavaBlock& block) {
        for (const auto& kv : predicates) {
            const std::string* actual = block.Find(kv.first);
            if (!actual)
                continue;
            if (*actual != kv.second)
                return false;
        }
        return true;
    }

    int BlockIdMatcher::Match(const JavaBlock& block) const {
        if (auto it = byName.find(block.name); it != byName.end())
            for (const Rule& r : it->second)
                if (Matches(r.predicates, block))
                    return r.id;
        for (const TagRule& t : tags)
            if (BlockInTag(t.tag, block.name) && Matches(t.predicates, block))
                return t.id;
        return -1;
    }

    int BlockIdMatcher::MatchName(std::string_view javaName) const {
        JavaBlock b;
        b.name = std::string(StripNs(javaName));
        return Match(b);
    }

    std::unordered_map<std::string, int>
    FlatIdMap(const std::vector<std::pair<int, std::vector<std::string>>>& ordered) {
        std::unordered_map<std::string, int> out;
        for (const auto& line : ordered)
            for (const std::string& raw : line.second) {
                if (raw.empty() || raw.contains('='))
                    continue;
                std::string_view n = raw;
                if (StartsWith(n, "minecraft:"))
                    n.remove_prefix(10);
                else if (n.contains(':'))
                    continue;
                out[std::string(n)] = line.first;
            }
        return out;
    }
}
