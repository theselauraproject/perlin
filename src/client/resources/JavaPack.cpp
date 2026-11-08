#include "client/resources/JavaPack.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>
#include <unordered_map>

namespace perlin::javapack {
    namespace {

        bool StartsWith(std::string_view s, std::string_view p) {
            return s.substr(0, p.size()) == p;
        }
        bool EndsWith(std::string_view s, std::string_view p) {
            return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
        }

        std::string StripExtension(std::string s) {
            for (const char* e : {".png", ".tga"})
                if (EndsWith(s, e)) {
                    s.resize(s.size() - 4);
                    break;
                }
            return s;
        }

        void Add(std::vector<Source>& out, std::string path, float x0 = 0, float y0 = 0, float x1 = 1,
                 float y1 = 1) {
            for (const Source& s : out)
                if (s.path == path && s.x0 == x0 && s.y0 == y0 && s.x1 == x1 && s.y1 == y1)
                    return;
            out.push_back({std::move(path), x0, y0, x1, y1});
        }

        void AddSheet(std::vector<Source>& out, const char* sheet, int x, int y, int w, int h) {
            Add(out, sheet, x / 256.0f, y / 256.0f, (x + w) / 256.0f, (y + h) / 256.0f);
        }

        struct Rect {
            int x, y, w, h;
        };

        void AddWidgets(std::vector<Source>& out, int x, int y, int w, int h) {
            AddSheet(out, "textures/gui/widgets.png", x, y, w, h);
            AddSheet(out, "textures/gui/gui.png", x, y, w, h);
        }

        const char* const kColors[] = {"white", "orange", "magenta", "light_blue", "yellow", "lime",
                                       "pink",  "gray",   "silver",  "cyan",       "purple", "blue",
                                       "brown", "green",  "red",     "black"};
        const char* const kWoods[] = {"oak", "spruce", "birch", "jungle", "acacia", "big_oak"};

        std::string ModernColor(const std::string& c) {
            return c == "silver" ? "light_gray" : c;
        }
        std::string ModernWood(const std::string& w) {
            return w == "big_oak" ? "dark_oak" : w;
        }

        bool Color(std::string_view s) {
            for (const char* c : kColors)
                if (s == c)
                    return true;
            return false;
        }
        bool Wood(std::string_view s) {
            for (const char* w : kWoods)
                if (s == w)
                    return true;
            return false;
        }

        const std::unordered_map<std::string, std::string>& LegacyBlocks() {
            static const std::unordered_map<std::string, std::string> map = {
                {"grass_side_carried", "grass_side"},
                {"fire_0", "fire_layer_0"},
                {"fire_1", "fire_layer_1"},
                {"leaves_oak_opaque", "leaves_oak"},
                {"leaves_spruce_opaque", "leaves_spruce"},
                {"leaves_birch_opaque", "leaves_birch"},
                {"leaves_jungle_opaque", "leaves_jungle"},
                {"leaves_acacia_opaque", "leaves_acacia"},
                {"leaves_big_oak_opaque", "leaves_big_oak"},
            };
            return map;
        }

        const std::unordered_map<std::string, std::string>& ModernBlocks() {
            static const std::unordered_map<std::string, std::string> map = {
                {"grass_top", "grass_block_top"},
                {"grass_side", "grass_block_side"},
                {"grass_side_carried", "grass_block_side"},
                {"grass_side_snowed", "grass_block_snow"},
                {"farmland_wet", "farmland_moist"},
                {"farmland_dry", "farmland"},
                {"furnace_front_off", "furnace_front"},
                {"brick", "bricks"},
                {"stonebrick", "stone_bricks"},
                {"stonebrick_mossy", "mossy_stone_bricks"},
                {"stonebrick_cracked", "cracked_stone_bricks"},
                {"stonebrick_carved", "chiseled_stone_bricks"},
                {"stone_granite", "granite"},
                {"stone_diorite", "diorite"},
                {"stone_andesite", "andesite"},
                {"stone_granite_smooth", "polished_granite"},
                {"stone_diorite_smooth", "polished_diorite"},
                {"stone_andesite_smooth", "polished_andesite"},
                {"cobblestone_mossy", "mossy_cobblestone"},
                {"sandstone_normal", "sandstone"},
                {"sandstone_carved", "chiseled_sandstone"},
                {"sandstone_smooth", "cut_sandstone"},
                {"red_sandstone_normal", "red_sandstone"},
                {"nether_brick", "nether_bricks"},
                {"quartz_block_lines", "quartz_pillar"},
                {"quartz_block_lines_top", "quartz_pillar_top"},
                {"quartz_block_chiseled", "chiseled_quartz_block"},
                {"quartz_block_chiseled_top", "chiseled_quartz_block_top"},
                {"reeds", "sugar_cane"},
                {"tallgrass", "short_grass"},
                {"flower_rose", "poppy"},
                {"flower_dandelion", "dandelion"},
                {"torch_on", "torch"},
                {"redstone_torch_on", "redstone_torch"},
                {"ice_packed", "packed_ice"},
                {"pumpkin_face_off", "carved_pumpkin"},
                {"pumpkin_face_on", "jack_o_lantern"},
                {"noteblock", "note_block"},
                {"web", "cobweb"},
                {"deadbush", "dead_bush"},
                {"waterlily", "lily_pad"},
                {"trapdoor", "oak_trapdoor"},
                {"door_wood_upper", "oak_door_top"},
                {"door_wood_lower", "oak_door_bottom"},
                {"door_iron_upper", "iron_door_top"},
                {"door_iron_lower", "iron_door_bottom"},
                {"rail_normal", "rail"},
                {"rail_golden", "powered_rail"},
                {"rail_detector", "detector_rail"},
                {"rail_activator", "activator_rail"},
                {"dropper_front_horizontal", "dropper_front"},
                {"dispenser_front_horizontal", "dispenser_front"},
                {"anvil_base", "anvil"},
                {"redstone_lamp_off", "redstone_lamp"},
                {"prismarine_rough", "prismarine"},
                {"prismarine_dark", "dark_prismarine"},
                {"slime", "slime_block"},
                {"fire_0", "fire_0"},
                {"fire_1", "fire_1"},
                {"leaves_oak_opaque", "oak_leaves"},
                {"leaves_spruce_opaque", "spruce_leaves"},
                {"leaves_birch_opaque", "birch_leaves"},
                {"leaves_jungle_opaque", "jungle_leaves"},
                {"leaves_acacia_opaque", "acacia_leaves"},
                {"leaves_big_oak_opaque", "dark_oak_leaves"},
                {"snow", "snow"},
                {"hardened_clay", "terracotta"},
                {"glass_pane_top", "glass_pane_top"},
                {"mob_spawner", "spawner"},
                {"tnt_side", "tnt_side"},
                {"cauldron_inner", "cauldron_inner"},
            };
            return map;
        }

        std::string ModernBlockRule(const std::string& name) {
            const auto afterPrefix = [&](std::string_view prefix) -> std::string_view {
                return StartsWith(name, prefix) ? std::string_view(name).substr(prefix.size())
                                                : std::string_view();
            };
            for (auto [prefix, suffix] : {std::pair<std::string_view, std::string_view>{"planks_", "_planks"},
                                          {"log_", "_log"},
                                          {"leaves_", "_leaves"},
                                          {"sapling_", "_sapling"}}) {
                std::string_view rest = afterPrefix(prefix);
                if (rest.empty())
                    continue;
                std::string tail;
                if (EndsWith(rest, "_top")) {
                    tail = "_top";
                    rest.remove_suffix(4);
                }
                if (Wood(rest))
                    return ModernWood(std::string(rest)) + std::string(suffix) + tail;
            }
            for (auto [prefix, suffix] :
                 {std::pair<std::string_view, std::string_view>{"wool_colored_", "_wool"},
                  {"glass_", "_stained_glass"},
                  {"hardened_clay_stained_", "_terracotta"},
                  {"concrete_powder_", "_concrete_powder"},
                  {"concrete_", "_concrete"},
                  {"glazed_terracotta_", "_glazed_terracotta"}}) {
                const std::string_view rest = afterPrefix(prefix);
                if (!rest.empty() && Color(rest))
                    return ModernColor(std::string(rest)) + std::string(suffix);
            }
            return {};
        }

        const std::unordered_map<std::string, std::string>& ModernItems() {
            static const std::unordered_map<std::string, std::string> map = {
                {"apple_golden", "golden_apple"},
                {"bucket_empty", "bucket"},
                {"bucket_water", "water_bucket"},
                {"bucket_lava", "lava_bucket"},
                {"bucket_milk", "milk_bucket"},
                {"bow_standby", "bow"},
                {"fishing_rod_uncast", "fishing_rod"},
                {"door_wood", "oak_door"},
                {"door_iron", "iron_door"},
                {"sign", "oak_sign"},
                {"boat", "oak_boat"},
                {"minecart_normal", "minecart"},
                {"book_normal", "book"},
                {"book_enchanted", "enchanted_book"},
                {"book_writable", "writable_book"},
                {"book_written", "written_book"},
                {"melon", "melon_slice"},
                {"fish_raw", "cod"},
                {"fish_cooked", "cooked_cod"},
                {"fish_salmon_raw", "salmon"},
                {"fish_salmon_cooked", "cooked_salmon"},
                {"beef_raw", "beef"},
                {"beef_cooked", "cooked_beef"},
                {"porkchop_raw", "porkchop"},
                {"porkchop_cooked", "cooked_porkchop"},
                {"chicken_raw", "chicken"},
                {"chicken_cooked", "cooked_chicken"},
                {"mutton_raw", "mutton"},
                {"mutton_cooked", "cooked_mutton"},
                {"rabbit_raw", "rabbit"},
                {"rabbit_cooked", "cooked_rabbit"},
                {"potato_baked", "baked_potato"},
                {"potato_poisonous", "poisonous_potato"},
                {"netherbrick", "nether_brick"},
                {"fireball", "fire_charge"},
                {"seeds_wheat", "wheat_seeds"},
                {"seeds_pumpkin", "pumpkin_seeds"},
                {"seeds_melon", "melon_seeds"},
                {"carrot_golden", "golden_carrot"},
                {"melon_speckled", "glistering_melon_slice"},
                {"map_empty", "map"},
                {"map_filled", "filled_map"},
                {"sugar", "sugar"},
                {"redstone", "redstone"},
                {"cauldron", "cauldron"},
                {"brewing_stand", "brewing_stand"},
                {"leather_horse_armor", "leather_horse_armor"},
                {"iron_horse_armor", "iron_horse_armor"},
                {"gold_horse_armor", "golden_horse_armor"},
            };
            return map;
        }

        std::string ModernItemRule(const std::string& name) {
            if (StartsWith(name, "wood_"))
                return "wooden_" + name.substr(5);
            if (StartsWith(name, "gold_") && name != "gold_ingot" && name != "gold_nugget")
                return "golden_" + name.substr(5);
            if (StartsWith(name, "dye_powder_")) {
                const std::string c = name.substr(11);
                if (Color(c))
                    return ModernColor(c) + "_dye";
            }
            if (StartsWith(name, "record_"))
                return "music_disc_" + name.substr(7);
            if (StartsWith(name, "bed_")) {
                const std::string c = name.substr(4);
                if (Color(c))
                    return ModernColor(c) + "_bed";
            }
            return {};
        }

        struct HudSprite {
            const char* name;
            const char* sheet;
            Rect rect;
            const char* sprite;
        };

        const HudSprite kHud[] = {
            {"selected_hotbar_slot", "textures/gui/widgets.png", {0, 22, 24, 24}, "hud/hotbar_selection"},
            {"hotbar_start_cap", "textures/gui/widgets.png", {0, 0, 1, 22}, nullptr},
            {"hotbar_end_cap", "textures/gui/widgets.png", {181, 0, 1, 22}, nullptr},

            {"heart_background", "textures/gui/icons.png", {16, 0, 9, 9}, "hud/heart/container"},
            {"heart_blink", "textures/gui/icons.png", {25, 0, 9, 9}, "hud/heart/container_blinking"},
            {"heart", "textures/gui/icons.png", {52, 0, 9, 9}, "hud/heart/full"},
            {"heart_half", "textures/gui/icons.png", {61, 0, 9, 9}, "hud/heart/half"},
            {"heart_flash", "textures/gui/icons.png", {70, 0, 9, 9}, "hud/heart/full_blinking"},
            {"heart_flash_half", "textures/gui/icons.png", {79, 0, 9, 9}, "hud/heart/half_blinking"},
            {"poison_heart", "textures/gui/icons.png", {88, 0, 9, 9}, "hud/heart/poisoned_full"},
            {"poison_heart_half", "textures/gui/icons.png", {97, 0, 9, 9}, "hud/heart/poisoned_half"},
            {"poison_heart_flash",
             "textures/gui/icons.png",
             {106, 0, 9, 9},
             "hud/heart/poisoned_full_blinking"},
            {"poison_heart_flash_half",
             "textures/gui/icons.png",
             {115, 0, 9, 9},
             "hud/heart/poisoned_half_blinking"},
            {"wither_heart", "textures/gui/icons.png", {124, 0, 9, 9}, "hud/heart/withered_full"},
            {"wither_heart_half", "textures/gui/icons.png", {133, 0, 9, 9}, "hud/heart/withered_half"},
            {"wither_heart_flash",
             "textures/gui/icons.png",
             {142, 0, 9, 9},
             "hud/heart/withered_full_blinking"},
            {"wither_heart_flash_half",
             "textures/gui/icons.png",
             {151, 0, 9, 9},
             "hud/heart/withered_half_blinking"},
            {"absorption_heart", "textures/gui/icons.png", {160, 0, 9, 9}, "hud/heart/absorbing_full"},
            {"absorption_heart_half", "textures/gui/icons.png", {169, 0, 9, 9}, "hud/heart/absorbing_half"},
            {"freeze_heart", nullptr, {0, 0, 0, 0}, "hud/heart/frozen_full"},
            {"freeze_heart_half", nullptr, {0, 0, 0, 0}, "hud/heart/frozen_half"},
            {"freeze_heart_flash", nullptr, {0, 0, 0, 0}, "hud/heart/frozen_full_blinking"},
            {"freeze_heart_flash_half", nullptr, {0, 0, 0, 0}, "hud/heart/frozen_half_blinking"},

            {"armor_empty", "textures/gui/icons.png", {16, 9, 9, 9}, "hud/armor_empty"},
            {"armor_half", "textures/gui/icons.png", {25, 9, 9, 9}, "hud/armor_half"},
            {"armor_full", "textures/gui/icons.png", {34, 9, 9, 9}, "hud/armor_full"},

            {"bubble", "textures/gui/icons.png", {16, 18, 9, 9}, "hud/air"},
            {"bubble_pop", "textures/gui/icons.png", {25, 18, 9, 9}, "hud/air_bursting"},

            {"hunger_background", "textures/gui/icons.png", {16, 27, 9, 9}, "hud/food_empty"},
            {"hunger_blink", "textures/gui/icons.png", {25, 27, 9, 9}, "hud/food_empty"},
            {"hunger_effect_background", "textures/gui/icons.png", {133, 27, 9, 9}, "hud/food_empty_hunger"},
            {"hunger_full", "textures/gui/icons.png", {52, 27, 9, 9}, "hud/food_full"},
            {"hunger_half", "textures/gui/icons.png", {61, 27, 9, 9}, "hud/food_half"},
            {"hunger_effect_full", "textures/gui/icons.png", {88, 27, 9, 9}, "hud/food_full_hunger"},
            {"hunger_effect_half", "textures/gui/icons.png", {97, 27, 9, 9}, "hud/food_half_hunger"},

            {"experience_bar_empty",
             "textures/gui/icons.png",
             {0, 64, 182, 5},
             "hud/experience_bar_background"},
            {"experience_bar_full", "textures/gui/icons.png", {0, 69, 182, 5}, "hud/experience_bar_progress"},
            {"cross_hair", "textures/gui/icons.png", {0, 0, 15, 15}, "hud/crosshair"},
        };

        bool Hud(const std::string& name, std::vector<Source>& out) {
            if (StartsWith(name, "panorama_") && name.size() == 10 && name[9] >= '0' && name[9] <= '5') {
                Add(out, "textures/gui/title/background/" + name + ".png");
                return true;
            }
            if (StartsWith(name, "hotbar_") && name.size() == 8 && name[7] >= '0' && name[7] <= '8') {
                const int slot = name[7] - '0';
                Add(out, "textures/gui/sprites/hud/hotbar.png", (1 + 20 * slot) / 182.0f, 0,
                    (21 + 20 * slot) / 182.0f, 1);
                AddWidgets(out, 1 + 20 * slot, 0, 20, 22);
                return true;
            }
            if (name == "hotbar_start_cap" || name == "hotbar_end_cap") {
                const bool start = name == "hotbar_start_cap";
                Add(out, "textures/gui/sprites/hud/hotbar.png", start ? 0.0f : 181 / 182.0f, 0,
                    start ? 1 / 182.0f : 1.0f, 1);
                AddWidgets(out, start ? 0 : 181, 0, 1, 22);
                return true;
            }
            if (StartsWith(name, "ping_anim_") && name.size() == 11 && name[10] >= '0' && name[10] <= '4') {
                AddSheet(out, "textures/gui/icons.png", 10, 176 + 8 * (name[10] - '0'), 10, 8);
                return true;
            }
            if (StartsWith(name, "ping_") && name.size() == 6 && name[5] >= '0' && name[5] <= '5') {
                const int level = name[5] - '0';
                AddSheet(out, "textures/gui/icons.png", 0, 176 + 8 * level, 10, 8);
                return true;
            }
            if (StartsWith(name, "screen_")) {
                const std::string n = name.substr(7);
                if (n == "inventory")
                    AddSheet(out, "textures/gui/container/inventory.png", 0, 0, 176, 166);
                else if (n == "crafting_table")
                    AddSheet(out, "textures/gui/container/crafting_table.png", 0, 0, 176, 166);
                else if (n == "horse")
                    AddSheet(out, "textures/gui/container/horse.png", 0, 0, 176, 166);
                else if (n == "horse_slot")
                    AddSheet(out, "textures/gui/container/horse.png", 7, 83, 18, 18);
                else if (n == "chest")
                    AddSheet(out, "textures/gui/container/generic_54.png", 0, 0, 176, 222);
                else if (n == "furnace" || n == "blast_furnace" || n == "smoker") {
                    AddSheet(out, (std::string("textures/gui/container/") + n + ".png").c_str(), 0, 0, 176, 166);

                    AddSheet(out, "textures/gui/container/furnace.png", 0, 0, 176, 166);
                } else if (n == "creative_items" || n == "creative_search" || n == "creative_survival") {
                    const std::string tab = n == "creative_items"    ? "tab_items"
                                            : n == "creative_search" ? "tab_item_search"
                                                                     : "tab_inventory";
                    AddSheet(out, ("textures/gui/container/creative_inventory/" + tab + ".png").c_str(), 0, 0, 195,
                             136);
                } else if (n == "recipe_book")
                    AddSheet(out, "textures/gui/recipe_book.png", 1, 1, 147, 166);
                else if (n == "menu_field") {
                    Add(out, "textures/gui/sprites/widget/text_field.png");
                } else if (n == "menu_field_hi") {
                    Add(out, "textures/gui/sprites/widget/text_field_highlighted.png");
                } else if (n == "menu_scroller") {
                    Add(out, "textures/gui/sprites/widget/scroller.png");
                } else if (n == "menu_bg") {
                    Add(out, "textures/gui/menu_background.png");
                    Add(out, "textures/gui/options_background.png");
                } else if (n == "menu_btn") {
                    Add(out, "textures/gui/sprites/widget/button.png");
                    AddWidgets(out, 0, 66, 200, 20);
                } else if (n == "menu_btn_hi") {
                    Add(out, "textures/gui/sprites/widget/button_highlighted.png");
                    AddWidgets(out, 0, 86, 200, 20);
                } else if (n == "menu_btn_off") {
                    Add(out, "textures/gui/sprites/widget/button_disabled.png");
                    AddWidgets(out, 0, 46, 200, 20);
                } else if (n == "inv_effect_background") {
                    Add(out, "textures/gui/sprites/container/inventory/effect_background.png");
                    AddSheet(out, "textures/gui/container/inventory.png", 0, 166, 120, 32);
                } else if (n == "inv_effect_background_ambient") {
                    Add(out, "textures/gui/sprites/container/inventory/effect_background_ambient.png");
                } else
                    return false;
                return true;
            }
            for (const HudSprite& s : kHud) {
                if (name != s.name)
                    continue;
                if (s.sprite)
                    Add(out, std::string("textures/gui/sprites/") + s.sprite + ".png");
                if (s.sheet && s.rect.w > 0) {
                    AddSheet(out, s.sheet, s.rect.x, s.rect.y, s.rect.w, s.rect.h);
                    if (std::string_view(s.sheet) == "textures/gui/widgets.png")
                        AddSheet(out, "textures/gui/gui.png", s.rect.x, s.rect.y, s.rect.w, s.rect.h);
                }
                return true;
            }
            return false;
        }

        void AddBlockOrItem(std::vector<Source>& out, const char* kind, const std::string& name) {
            const bool block = std::string_view(kind) == "block";
            const auto dirs = {std::string("textures/") + kind + "s/", std::string("textures/") + kind + "/"};
            const auto& legacy = LegacyBlocks();
            std::vector<std::string> names{name};
            if (block) {
                if (const auto it = legacy.find(name); it != legacy.end())
                    names.push_back(it->second);
                if (const auto it = ModernBlocks().find(name); it != ModernBlocks().end())
                    names.push_back(it->second);
                const std::string rule = ModernBlockRule(name);
                if (!rule.empty())
                    names.push_back(rule);
            } else {
                if (const auto it = ModernItems().find(name); it != ModernItems().end())
                    names.push_back(it->second);
                const std::string rule = ModernItemRule(name);
                if (!rule.empty())
                    names.push_back(rule);
            }
            for (const std::string& n : names)
                for (const std::string& dir : dirs)
                    Add(out, dir + n + ".png");
        }
    }

    std::vector<Source> Sources(const std::string& bedrockPath) {
        std::vector<Source> out;
        const std::string path = StripExtension(str::Lower(bedrockPath));
        const auto after = [&](std::string_view prefix) {
            return StartsWith(path, prefix) ? path.substr(prefix.size()) : std::string();
        };

        if (const std::string n = after("textures/blocks/"); !n.empty()) {
            AddBlockOrItem(out, "block", n);
        } else if (const std::string n2 = after("textures/items/"); !n2.empty()) {
            AddBlockOrItem(out, "item", n2);
        } else if (const std::string n3 = after("textures/ui/"); !n3.empty()) {

            if (StartsWith(n3, "panorama_"))
                Add(out, "textures/gui/title/background/" + n3 + ".png");
            else
                Hud(n3, out);
        } else if (path == "font/default8" || path == "textures/font/default8") {
            Add(out, "textures/font/ascii.png");
        } else if (const std::string n4 = after("textures/models/armor/"); !n4.empty()) {
            const size_t us = n4.rfind('_');
            if (us != std::string::npos && us + 2 == n4.size() && (n4.back() == '1' || n4.back() == '2')) {
                std::string material = n4.substr(0, us);
                if (material == "cloth")
                    material = "leather";
                Add(out, "textures/models/armor/" + material + "_layer_" + n4.back() + ".png");
            }
        } else if (path == "textures/gui/icons" || path == "textures/gui/widgets") {
            Add(out, path + ".png");
            if (path == "textures/gui/widgets")
                Add(out, "textures/gui/gui.png");
        }
        return out;
    }

    namespace {
        struct Piece {
            std::string path;

            int sx, sy, sw, sh;

            int dx, dy, dw, dh;
        };

        int ScaleFor(const Texture& sheet, int baseWidth) {
            return std::max(1, static_cast<int>(std::lround(static_cast<double>(sheet.width) / baseWidth)));
        }

        void Blit(const Texture& src, int sx, int sy, int sw, int sh, Texture& dst, int dx, int dy, int dw,
                  int dh) {
            if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
                return;
            for (int y = 0; y < dh; y++)
                for (int x = 0; x < dw; x++) {
                    const int px = std::clamp(sx + x * sw / dw, 0, src.width - 1);
                    const int py = std::clamp(sy + y * sh / dh, 0, src.height - 1);
                    const int ox = dx + x, oy = dy + y;
                    if (ox < 0 || oy < 0 || ox >= dst.width || oy >= dst.height)
                        continue;
                    std::copy_n(src.pixels.begin() + (static_cast<size_t>(py) * src.width + px) * 4, 4,
                                dst.pixels.begin() + (static_cast<size_t>(oy) * dst.width + ox) * 4);
                }
        }

        bool Assemble(const std::vector<Piece>& pieces, int baseW, int baseH, const SheetLoader& load,
                      Texture& out) {
            Texture result;
            bool any = false;
            int k = 0;
            std::vector<Texture> sheets(pieces.size());
            for (size_t i = 0; i < pieces.size(); i++)
                if (load(pieces[i].path, sheets[i]) && sheets[i].width > 0 && sheets[i].height > 0) {
                    const int sheetBase = pieces[i].sw ? 256 : std::max(1, pieces[i].dw);
                    k = std::max(k, ScaleFor(sheets[i], sheetBase));
                }
            if (!k)
                return false;
            result.width = baseW * k;
            result.height = baseH * k;
            result.pixels.assign(static_cast<size_t>(result.width) * result.height * 4, 0);
            for (size_t i = 0; i < pieces.size(); i++) {
                const Piece& p = pieces[i];
                const Texture& sheet = sheets[i];
                if (sheet.width <= 0)
                    continue;
                int sx = 0, sy = 0, sw = sheet.width, sh = sheet.height;
                if (p.sw) {
                    const double u = sheet.width / 256.0, v = sheet.height / 256.0;
                    sx = static_cast<int>(std::lround(p.sx * u));
                    sy = static_cast<int>(std::lround(p.sy * v));
                    sw = static_cast<int>(std::lround(p.sw * u));
                    sh = static_cast<int>(std::lround(p.sh * v));
                }
                Blit(sheet, sx, sy, sw, sh, result, p.dx * k, p.dy * k, p.dw * k, p.dh * k);
                any = true;
            }
            if (!any)
                return false;
            out = std::move(result);
            return true;
        }
    }

    bool Compose(const std::string& bedrockPath, const SheetLoader& load, Texture& out) {
        const std::string path = StripExtension(str::Lower(bedrockPath));
        if (!StartsWith(path, "textures/ui/screen_"))
            return false;
        const std::string n = path.substr(19);
        std::vector<Piece> pieces;
        int w = 0, h = 0;
        if (n == "furnace_progress") {
            w = 24;
            h = 30;
            pieces = {{"textures/gui/container/furnace.png", 176, 0, 14, 14, 0, 0, 14, 14},
                      {"textures/gui/container/furnace.png", 176, 14, 24, 16, 0, 14, 24, 16}};
        } else if (n == "creative_misc") {
            w = 24;
            h = 15;
            pieces = {{"textures/gui/container/creative_inventory/tab_items.png", 232, 0, 12, 15, 0, 0, 12, 15},
                      {"textures/gui/container/creative_inventory/tab_items.png", 244, 0, 12, 15, 12, 0, 12, 15}};
        } else if (n == "creative_tabs") {
            w = 26 * 7;
            h = 32 * 4;
            for (int row = 0; row < 4; row++)
                for (int col = 0; col < 7; col++)
                    pieces.push_back({"textures/gui/container/creative_inventory/tabs.png", 28 * col, 32 * row, 27,
                                      32, 26 * col, 32 * row, 26, 32});
        } else if (n == "hud_effect") {
            w = 48;
            h = 24;
            pieces = {{"textures/gui/sprites/hud/effect_background.png", 0, 0, 0, 0, 0, 0, 24, 24},
                      {"textures/gui/sprites/hud/effect_background_ambient.png", 0, 0, 0, 0, 24, 0, 24, 24}};
        } else if (n == "slot_icons") {
            w = 80;
            h = 16;
            const char* names[] = {"helmet", "chestplate", "leggings", "boots", "shield"};
            for (int i = 0; i < 5; i++)
                pieces.push_back({std::string("textures/items/empty_armor_slot_") + names[i] + ".png", 0, 0, 0, 0,
                                  16 * i, 0, 16, 16});
        } else if (n == "menu_logo") {
            Texture sheet;
            if (!load("textures/gui/title/minecraft.png", sheet) || sheet.width <= 0)
                return false;

            if (sheet.width != sheet.height) {
                out = std::move(sheet);
                return true;
            }

            w = 310;
            h = 78;
            pieces = {{"textures/gui/title/minecraft.png", 0, 0, 155, 44, 0, 17, 155, 44},
                      {"textures/gui/title/minecraft.png", 0, 45, 155, 44, 155, 17, 155, 44}};
        } else {
            return false;
        }
        return Assemble(pieces, w, h, load, out);
    }

    bool Crop(const Texture& sheet, const Source& source, Texture& out) {
        if (sheet.width <= 0 || sheet.height <= 0 ||
            sheet.pixels.size() < static_cast<size_t>(sheet.width) * sheet.height * 4)
            return false;
        if (source.Whole()) {
            out = sheet;
            return true;
        }
        const int x0 = std::clamp(static_cast<int>(std::lround(source.x0 * sheet.width)), 0, sheet.width);
        const int y0 = std::clamp(static_cast<int>(std::lround(source.y0 * sheet.height)), 0, sheet.height);
        const int x1 = std::clamp(static_cast<int>(std::lround(source.x1 * sheet.width)), 0, sheet.width);
        const int y1 = std::clamp(static_cast<int>(std::lround(source.y1 * sheet.height)), 0, sheet.height);
        if (x1 <= x0 || y1 <= y0)
            return false;
        Texture result;
        result.width = x1 - x0;
        result.height = y1 - y0;
        result.pixels.resize(static_cast<size_t>(result.width) * result.height * 4);
        for (int y = 0; y < result.height; y++)
            std::copy_n(sheet.pixels.begin() + (static_cast<size_t>(y0 + y) * sheet.width + x0) * 4,
                        static_cast<size_t>(result.width) * 4,
                        result.pixels.begin() + static_cast<size_t>(y) * result.width * 4);
        out = std::move(result);
        return true;
    }
}
