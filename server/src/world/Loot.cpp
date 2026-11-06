#include "world/Loot.hpp"

#include "core/Json.hpp"
#include "world/BlockCatalog.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace perlin::server {
    namespace {
        std::string Bare(std::string n) {
            if (n.starts_with("minecraft:"))
                n.erase(0, 10);
            return n;
        }

        std::string StateText(const StateValue& v) {
            if (const bool* b = std::get_if<bool>(&v))
                return *b ? "1" : "0";
            if (const int32_t* i = std::get_if<int32_t>(&v))
                return std::to_string(*i);
            return std::get<std::string>(v);
        }

        bool FindState(const StateList& list, const std::string& key, std::string& out) {
            for (const auto& [k, v] : list)
                if (k == key) {
                    out = StateText(v);
                    return true;
                }
            return false;
        }

        int StateInt(const StateList& list, const std::string& key, int fallback) {
            std::string v;
            if (!FindState(list, key, v))
                return fallback;
            if (v == "true")
                return 1;
            if (v == "false")
                return 0;
            return std::atoi(v.c_str());
        }

        const std::unordered_map<std::string, std::string>& BedrockToJava() {
            static const std::unordered_map<std::string, std::string> m = {
                {"beetroot", "beetroots"},
                {"bed", "white_bed"},
                {"brick_block", "bricks"},
                {"cave_vines_body_with_berries", "cave_vines_plant"},
                {"cave_vines_head_with_berries", "cave_vines"},
                {"deadbush", "dead_bush"},
                {"dirt_with_roots", "rooted_dirt"},
                {"end_brick_stairs", "end_stone_brick_stairs"},
                {"end_bricks", "end_stone_bricks"},
                {"fence_gate", "oak_fence_gate"},
                {"frog_spawn", "frogspawn"},
                {"golden_rail", "powered_rail"},
                {"grass_path", "dirt_path"},
                {"hardened_clay", "terracotta"},
                {"lit_blast_furnace", "blast_furnace"},
                {"lit_deepslate_redstone_ore", "deepslate_redstone_ore"},
                {"lit_furnace", "furnace"},
                {"lit_pumpkin", "jack_o_lantern"},
                {"lit_redstone_lamp", "redstone_lamp"},
                {"lit_redstone_ore", "redstone_ore"},
                {"lit_smoker", "smoker"},
                {"magma", "magma_block"},
                {"melon_block", "melon"},
                {"mob_spawner", "spawner"},
                {"nether_brick", "nether_bricks"},
                {"normal_stone_slab", "stone_slab"},
                {"normal_stone_double_slab", "stone_slab"},
                {"normal_stone_stairs", "stone_stairs"},
                {"stone_stairs", "cobblestone_stairs"},
                {"noteblock", "note_block"},
                {"powered_comparator", "comparator"},
                {"unpowered_comparator", "comparator"},
                {"powered_repeater", "repeater"},
                {"unpowered_repeater", "repeater"},
                {"prismarine_bricks_stairs", "prismarine_brick_stairs"},
                {"quartz_ore", "nether_quartz_ore"},
                {"red_nether_brick", "red_nether_bricks"},
                {"reeds", "sugar_cane"},
                {"silver_glazed_terracotta", "light_gray_glazed_terracotta"},
                {"slime", "slime_block"},
                {"small_dripleaf_block", "small_dripleaf"},
                {"snow_layer", "snow"},
                {"snow", "snow_block"},
                {"standing_sign", "oak_sign"},
                {"wall_sign", "oak_sign"},
                {"darkoak_standing_sign", "dark_oak_sign"},
                {"darkoak_wall_sign", "dark_oak_sign"},
                {"standing_banner", "white_banner"},
                {"wall_banner", "white_banner"},
                {"trapdoor", "oak_trapdoor"},
                {"wooden_door", "oak_door"},
                {"wooden_button", "oak_button"},
                {"wooden_pressure_plate", "oak_pressure_plate"},
                {"trip_wire", "tripwire"},
                {"unlit_redstone_torch", "redstone_torch"},
                {"underwater_torch", "torch"},
                {"undyed_shulker_box", "shulker_box"},
                {"waterlily", "lily_pad"},
                {"web", "cobweb"},
                {"waxed_copper", "waxed_copper_block"},
                {"azalea_leaves_flowered", "flowering_azalea_leaves"},
                {"double_cut_copper_slab", "cut_copper_slab"},
                {"brick_double_slab", "brick_slab"},
                {"nether_brick_double_slab", "nether_brick_slab"},
                {"end_stone_brick_double_slab", "end_stone_brick_slab"},
                {"stone_block_slab", "smooth_stone_slab"},
                {"daylight_detector_inverted", "daylight_detector"},
                {"glowingobsidian", "obsidian"},
                {"frame", "item_frame"},
                {"glow_frame", "glow_item_frame"},
                {"chemistry_table", "crafting_table"},
                {"stonecutter_block", "stonecutter"},
                {"invisible_bedrock", "barrier"},
                {"kelp", "kelp"},
                {"seagrass", "seagrass"},
            };
            return m;
        }

        const std::unordered_map<std::string, std::string>& JavaToBedrockItem() {
            static const std::unordered_map<std::string, std::string> m = {
                {"bricks", "brick_block"},
                {"cobblestone_stairs", "stone_stairs"},
                {"end_stone_brick_stairs", "end_brick_stairs"},
                {"end_stone_bricks", "end_bricks"},
                {"flowering_azalea_leaves", "azalea_leaves_flowered"},
                {"jack_o_lantern", "lit_pumpkin"},
                {"light_gray_glazed_terracotta", "silver_glazed_terracotta"},
                {"magma_block", "magma"},
                {"melon", "melon_block"},
                {"nether_bricks", "nether_brick"},
                {"nether_quartz_ore", "quartz_ore"},
                {"note_block", "noteblock"},
                {"oak_button", "wooden_button"},
                {"oak_fence_gate", "fence_gate"},
                {"oak_pressure_plate", "wooden_pressure_plate"},
                {"oak_trapdoor", "trapdoor"},
                {"oak_door", "wooden_door"},
                {"red_nether_bricks", "red_nether_brick"},
                {"rooted_dirt", "dirt_with_roots"},
                {"shulker_box", "undyed_shulker_box"},
                {"slime_block", "slime"},
                {"small_dripleaf", "small_dripleaf_block"},
                {"snow_block", "snow"},
                {"stone_slab", "normal_stone_slab"},
                {"smooth_stone_slab", "stone_block_slab"},
                {"terracotta", "hardened_clay"},
                {"waxed_copper_block", "waxed_copper"},
                {"cobweb", "web"},
                {"sugar_cane", "sugar_cane"},
                {"lily_pad", "waterlily"},
                {"dead_bush", "deadbush"},
                {"dirt_path", "grass_path"},
                {"powered_rail", "golden_rail"},
                {"spawner", "mob_spawner"},
                {"frogspawn", "frog_spawn"},
                {"item_frame", "frame"},
                {"glow_item_frame", "glow_frame"},
                {"redstone", "redstone"},
                {"prismarine_brick_stairs", "prismarine_bricks_stairs"},
            };
            return m;
        }

        struct Harvest {
            float hardness = 0;
            std::string tool;
            int level = 0;
        };

        const std::unordered_map<std::string, Harvest>& HarvestTable() {
            static const std::unordered_map<std::string, Harvest> table = [] {
                std::unordered_map<std::string, Harvest> t;
                const std::string path = BlockCatalog::FindFile({}, "block_destroy.tsv");
                std::ifstream in(path);
                std::string line;
                while (std::getline(in, line)) {
                    if (line.empty() || line[0] == '#')
                        continue;
                    std::istringstream cols(line);
                    std::string name, hardness, tool, level;
                    if (!std::getline(cols, name, '\t') || !std::getline(cols, hardness, '\t') ||
                        !std::getline(cols, tool, '\t'))
                        continue;
                    std::getline(cols, level, '\t');
                    Harvest h;
                    h.hardness = static_cast<float>(std::atof(hardness.c_str()));
                    h.tool = tool == "-" ? std::string() : tool;
                    h.level = std::atoi(level.c_str());
                    t[name] = h;
                }
                return t;
            }();
            return table;
        }

        const std::unordered_map<std::string, std::string>& Smelted() {
            static const std::unordered_map<std::string, std::string> m = {
                {"beef", "cooked_beef"},       {"porkchop", "cooked_porkchop"}, {"chicken", "cooked_chicken"},
                {"mutton", "cooked_mutton"},   {"rabbit", "cooked_rabbit"},     {"cod", "cooked_cod"},
                {"salmon", "cooked_salmon"},   {"potato", "baked_potato"},      {"kelp", "dried_kelp"},
                {"raw_iron", "iron_ingot"},    {"raw_gold", "gold_ingot"},      {"raw_copper", "copper_ingot"}};
            return m;
        }

        std::vector<const json::Value*> Many(const json::Value& v) {
            std::vector<const json::Value*> out;
            if (v.IsArray()) {
                for (const auto& e : v.items)
                    out.push_back(&e);
            } else if (!v.IsNull()) {
                out.push_back(&v);
            }
            return out;
        }
    }

    struct LootTables::Impl {
        std::unordered_map<std::string, json::Value> tables;
    };

    LootTables& LootTables::Instance() {
        static LootTables instance;
        return instance;
    }

    LootTables::LootTables() : impl(new Impl) {
        for (int i = 0; i < kLootSourceCount; i++) {
            json::Value v;
            if (json::Parse(kLootSources[i].json, v))
                impl->tables.emplace(kLootSources[i].key, std::move(v));
        }
    }

    bool LootTables::Has(const std::string& key) const {
        return impl->tables.contains(key);
    }

    LootBlock LootTables::ViewOf(const std::string& bedrockName, const StateList& states) {
        LootBlock b;
        b.bedrock = Bare(bedrockName);
        b.states = states;
        std::string n = b.bedrock;
        const auto& renames = BedrockToJava();
        if (const auto it = renames.find(n); it != renames.end()) {
            b.java = it->second;
            return b;
        }
        if (n.ends_with("_double_slab")) {
            b.java = n.substr(0, n.size() - 12) + "_slab";
            if (b.java == "darkoak_slab")
                b.java = "dark_oak_slab";
            return b;
        }
        if (n.starts_with("double_") && n.ends_with("_slab")) {
            b.java = n.substr(7);
            return b;
        }
        if (n.find("_double_cut_copper_slab") != std::string::npos) {
            b.java = n;
            b.java.replace(b.java.find("_double_cut_copper_slab"), 23, "_cut_copper_slab");
            return b;
        }
        if (n.ends_with("_standing_sign") || n.ends_with("_wall_sign")) {
            b.java = n.substr(0, n.rfind(n.ends_with("_standing_sign") ? "_standing_sign" : "_wall_sign")) + "_sign";
            return b;
        }
        if (n.ends_with("_wall_hanging_sign")) {
            b.java = n.substr(0, n.size() - 18) + "_hanging_sign";
            return b;
        }
        if (n.ends_with("_coral_wall_fan")) {
            b.java = n.substr(0, n.size() - 9) + "_fan";
            return b;
        }
        if (n.starts_with("lit_"))
            n = n.substr(4);
        b.java = n;
        return b;
    }

    std::string LootTables::JavaEntityName(const std::string& bedrockId) {
        std::string n = Bare(bedrockId);
        static const std::unordered_map<std::string, std::string> m = {
            {"evocation_illager", "evoker"},     {"zombie_pigman", "zombified_piglin"},
            {"villager_v2", "villager"},         {"zombie_villager_v2", "zombie_villager"},
            {"tropicalfish", "tropical_fish"},   {"snow_golem", "snow_golem"},
            {"mooshroom", "mooshroom"},          {"elder_guardian", "elder_guardian"}};
        if (const auto it = m.find(n); it != m.end())
            return it->second;
        return n;
    }

    std::string LootTables::BedrockItemName(const std::string& javaName) {
        const std::string n = Bare(javaName);
        const auto& catalog = BlockCatalog::Instance();
        const auto& m = JavaToBedrockItem();
        if (const auto it = m.find(n); it != m.end())
            if (catalog.ItemByName("minecraft:" + it->second))
                return "minecraft:" + it->second;
        if (n.ends_with("_bed") && catalog.ItemByName("minecraft:bed"))
            return "minecraft:bed";
        if (n.ends_with("_banner") && catalog.ItemByName("minecraft:banner"))
            return "minecraft:banner";
        if (catalog.ItemByName("minecraft:" + n))
            return "minecraft:" + n;
        return {};
    }

    int LootTables::ToolTier(const std::string& toolName) {
        const std::string n = Bare(toolName);
        if (n.starts_with("wooden_") || n.starts_with("golden_"))
            return 1;
        if (n.starts_with("stone_") || n.starts_with("copper_"))
            return 2;
        if (n.starts_with("iron_"))
            return 3;
        if (n.starts_with("diamond_") || n.starts_with("netherite_"))
            return 4;
        return 0;
    }

    float LootTables::Hardness(const std::string& bedrockBlock) {
        const auto& t = HarvestTable();
        const auto it = t.find(Bare(bedrockBlock));
        return it == t.end() ? 1.0f : it->second.hardness;
    }

    bool LootTables::CanHarvest(const std::string& bedrockBlock, const std::string& toolName) {
        const std::string block = Bare(bedrockBlock);
        const std::string tool = Bare(toolName);
        if (block == "snow" || block == "snow_layer")
            return tool.ends_with("_shovel");
        if (block == "web")
            return tool.ends_with("_sword") || tool == "shears";
        const auto& t = HarvestTable();
        const auto it = t.find(block);
        if (it == t.end() || it->second.level <= 0)
            return true;
        if (it->second.tool.empty())
            return true;
        if (!tool.ends_with("_" + it->second.tool))
            return false;
        return ToolTier(tool) >= it->second.level;
    }

    int LootTables::BlockXp(const std::string& javaBlock, int silkTouch, std::mt19937& rng) {
        if (silkTouch > 0)
            return 0;
        const std::string n = Bare(javaBlock);
        const auto range = [&](int lo, int hi) {
            return lo + static_cast<int>(rng() % static_cast<unsigned>(hi - lo + 1));
        };
        if (n == "coal_ore" || n == "deepslate_coal_ore")
            return range(0, 2);
        if (n == "diamond_ore" || n == "deepslate_diamond_ore" || n == "emerald_ore" || n == "deepslate_emerald_ore")
            return range(3, 7);
        if (n == "lapis_ore" || n == "deepslate_lapis_ore" || n == "nether_quartz_ore")
            return range(2, 5);
        if (n == "redstone_ore" || n == "deepslate_redstone_ore")
            return range(1, 5);
        if (n == "nether_gold_ore")
            return range(0, 1);
        if (n == "sculk")
            return 1;
        if (n == "sculk_catalyst" || n == "sculk_sensor" || n == "calibrated_sculk_sensor" || n == "sculk_shrieker")
            return 5;
        if (n == "spawner")
            return range(15, 43);
        return 0;
    }

    int LootTables::MobXp(const std::string& bedrockId, bool baby, bool hostile, std::mt19937& rng) {
        const std::string n = JavaEntityName(bedrockId);
        static const std::unordered_map<std::string, int> fixed = {
            {"blaze", 10},         {"guardian", 10},     {"elder_guardian", 10},  {"evoker", 10},
            {"breeze", 10},        {"piglin_brute", 20}, {"ravager", 20},         {"wither", 50},
            {"ender_dragon", 500}, {"slime", 1},         {"magma_cube", 2},       {"villager", 0},
            {"wandering_trader", 0}, {"iron_golem", 0},  {"snow_golem", 0},       {"armor_stand", 0},
            {"allay", 0},          {"bat", 0},           {"npc", 0},              {"copper_golem", 0},
            {"mannequin", 0},      {"creaking", 0}};
        if (n == "zombie" || n == "husk" || n == "drowned" || n == "zombie_villager")
            return baby ? 12 : 5;
        if (const auto it = fixed.find(n); it != fixed.end())
            return it->second;
        if (hostile)
            return 5;
        if (baby)
            return 0;
        return 1 + static_cast<int>(rng() % 3);
    }

    namespace {
        struct Roller {
            const std::unordered_map<std::string, json::Value>& tables;
            LootContext& ctx;
            int depth = 0;

            std::mt19937& Rng() { return *ctx.rng; }
            float Unit() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(Rng()); }
            int IntIn(int lo, int hi) {
                if (hi < lo)
                    return lo;
                return lo + static_cast<int>(Rng()() % static_cast<unsigned>(hi - lo + 1));
            }

            float Float(const json::Value& v) {
                if (v.kind == json::Value::Kind::Number)
                    return static_cast<float>(v.number);
                if (!v.IsObject())
                    return 0;
                const std::string type = v["type"].String("minecraft:uniform");
                if (type == "minecraft:constant")
                    return static_cast<float>(v["value"].Number());
                if (type == "minecraft:binomial") {
                    const int n = Int(v["n"]);
                    const float p = Float(v["p"]);
                    int hits = 0;
                    for (int i = 0; i < n; i++)
                        if (Unit() < p)
                            hits++;
                    return static_cast<float>(hits);
                }
                const float lo = Float(v["min"]), hi = Float(v["max"]);
                return lo + Unit() * (hi - lo);
            }

            int Int(const json::Value& v) {
                if (v.kind == json::Value::Kind::Number)
                    return static_cast<int>(std::lround(v.number));
                if (!v.IsObject())
                    return 0;
                const std::string type = v["type"].String("minecraft:uniform");
                if (type == "minecraft:uniform")
                    return IntIn(Int(v["min"]), Int(v["max"]));
                return static_cast<int>(std::lround(Float(v)));
            }

            int Enchant(const std::string& id) const {
                const std::string n = Bare(id);
                if (n == "fortune")
                    return ctx.fortune;
                if (n == "looting")
                    return ctx.looting;
                if (n == "silk_touch")
                    return ctx.silkTouch;
                return 0;
            }

            bool ItemMatches(const json::Value& items, const std::string& name) {
                for (const json::Value* it : Many(items)) {
                    if (!it->IsString())
                        continue;
                    const std::string want = it->text;
                    if (want.starts_with("#")) {
                        const auto tag = tables.find("tag/" + Bare(want.substr(1)));
                        if (tag != tables.end())
                            for (const auto& v : tag->second["values"].items)
                                if (Bare(v.String()) == Bare(name))
                                    return true;
                        continue;
                    }
                    if (Bare(want) == Bare(name))
                        return true;
                }
                return false;
            }

            bool ToolPredicate(const json::Value& p) {
                if (!p["items"].IsNull() && !ItemMatches(p["items"], ctx.tool))
                    return false;
                const json::Value& enchants = p["predicates"]["minecraft:enchantments"];
                for (const auto& e : enchants.items) {
                    const int level = Enchant(e["enchantments"].String());
                    const int need = static_cast<int>(e["levels"]["min"].Int(1));
                    if (level < need)
                        return false;
                }
                return true;
            }

            std::string JavaState(const LootBlock& b, const std::string& key) {
                const StateList& s = b.states;
                const std::string& n = b.bedrock;
                std::string v;
                if (key == "age") {
                    if (n == "beetroot") {
                        const int g = StateInt(s, "growth", 0);
                        return std::to_string(g >= 7 ? 3 : std::min(2, g / 2));
                    }
                    if (n == "mangrove_propagule")
                        return std::to_string(StateInt(s, "propagule_stage", 0));
                    if (FindState(s, "age", v))
                        return v;
                    return std::to_string(StateInt(s, "growth", 0));
                }
                if (key == "half")
                    return StateInt(s, "upper_block_bit", 0) ? "upper" : "lower";
                if (key == "part")
                    return StateInt(s, "head_piece_bit", 0) ? "head" : "foot";
                if (key == "type") {
                    if (n.contains("double_slab") || n.starts_with("double_"))
                        return "double";
                    if (FindState(s, "minecraft:vertical_half", v))
                        return v;
                    return "bottom";
                }
                if (key == "candles")
                    return std::to_string(StateInt(s, "candles", 0) + 1);
                if (key == "pickles")
                    return std::to_string(StateInt(s, "cluster_count", 0) + 1);
                if (key == "layers")
                    return std::to_string(StateInt(s, "height", 0) + 1);
                if (key == "flower_amount" || key == "segment_amount")
                    return std::to_string(std::min(4, StateInt(s, "growth", 0) + 1));
                if (key == "berries")
                    return n.contains("with_berries") ? "true" : "false";
                if (key == "level")
                    return std::to_string(StateInt(s, "composter_fill_level", 0));
                if (key == "unstable")
                    return StateInt(s, "explode_bit", 0) ? "true" : "false";
                if (key == "cracked")
                    return "false";
                if (key == "bottom")
                    return StateInt(s, "upper_block_bit", 0) ? "false" : "true";
                static const std::pair<const char*, int> kFaces[] = {{"down", 1}, {"up", 2},    {"south", 4},
                                                                     {"west", 8}, {"north", 16}, {"east", 32}};
                for (const auto& [face, bit] : kFaces)
                    if (key == face) {
                        const int bits = StateInt(s, "multi_face_direction_bits", 0);
                        return (bits & bit) ? "true" : "false";
                    }
                if (FindState(s, key, v))
                    return v;
                return {};
            }

            bool BlockMatches(const LootBlock& b, const json::Value& blocks, const json::Value& state) {
                if (!blocks.IsNull()) {
                    bool hit = false;
                    for (const json::Value* it : Many(blocks))
                        if (it->IsString() && Bare(it->text) == b.java)
                            hit = true;
                    if (!hit)
                        return false;
                }
                for (const auto& [key, want] : state.members) {
                    const std::string have = JavaState(b, key);
                    if (want.IsString()) {
                        if (have != want.text)
                            return false;
                    } else if (want.kind == json::Value::Kind::Number) {
                        if (std::atoi(have.c_str()) != static_cast<int>(want.number))
                            return false;
                    } else if (want.IsObject()) {
                        const int h = std::atoi(have.c_str());
                        if (!want["min"].IsNull() && h < static_cast<int>(want["min"].Number()))
                            return false;
                        if (!want["max"].IsNull() && h > static_cast<int>(want["max"].Number()))
                            return false;
                    }
                }
                return true;
            }

            bool EntityPredicate(const std::string& who, const json::Value& p) {
                if (who == "this") {
                    for (const auto& [key, value] : p.members) {
                        if (key == "minecraft:flags") {
                            if (!value["is_on_fire"].IsNull() && value["is_on_fire"].Bool() != ctx.onFire)
                                return false;
                            if (!value["is_baby"].IsNull() && value["is_baby"].Bool() != ctx.baby)
                                return false;
                        } else if (key == "minecraft:components") {
                            const json::Value& color = value["minecraft:sheep/color"];
                            if (!color.IsNull() && color.String() != "white")
                                return false;
                        } else if (key == "minecraft:type_specific/sheep") {
                            if (value["sheared"].Bool(false))
                                return false;
                        } else if (key == "minecraft:type_specific/cube_mob") {
                            const json::Value& size = value["size"];
                            if (size.kind == json::Value::Kind::Number && ctx.cubeSize != static_cast<int>(size.number))
                                return false;
                            if (size.IsObject()) {
                                if (!size["min"].IsNull() && ctx.cubeSize < static_cast<int>(size["min"].Number()))
                                    return false;
                                if (!size["max"].IsNull() && ctx.cubeSize > static_cast<int>(size["max"].Number()))
                                    return false;
                            }
                        } else if (key == "minecraft:entity_type") {
                            if (!value.IsString() || Bare(value.text) != Bare(ctx.attackerEntity))
                                return false;
                        } else {
                            return false;
                        }
                    }
                    return true;
                }
                if (ctx.attackerType.empty())
                    return false;
                for (const auto& [key, value] : p.members) {
                    if (key == "minecraft:entity_type") {
                        if (!value.IsString())
                            return false;
                        const std::string want = value.text;
                        if (want.starts_with("#") || Bare(want) != Bare(ctx.attackerType))
                            return false;
                    } else if (key == "minecraft:equipment") {
                        const json::Value& hand = value["mainhand"];
                        if (!hand["items"].IsNull() && !ItemMatches(hand["items"], ctx.tool))
                            return false;
                        if (!hand["predicates"]["minecraft:enchantments"].IsNull())
                            return false;
                    } else {
                        return false;
                    }
                }
                return true;
            }

            bool Condition(const json::Value& c) {
                if (c.IsString()) {
                    const auto it = tables.find("predicate/" + Bare(c.text));
                    return it != tables.end() && Condition(it->second);
                }
                const std::string type = Bare(c["type"].String());
                if (type == "survives_explosion")
                    return ctx.explosionRadius <= 0 || Unit() <= 1.0f / ctx.explosionRadius;
                if (type == "match_block")
                    return BlockMatches(ctx.block, c["blocks"], c["state"]);
                if (type == "match_tool")
                    return ToolPredicate(c["predicate"]);
                if (type == "any_of") {
                    for (const auto& t : c["terms"].items)
                        if (Condition(t))
                            return true;
                    return false;
                }
                if (type == "all_of") {
                    for (const auto& t : c["terms"].items)
                        if (!Condition(t))
                            return false;
                    return true;
                }
                if (type == "inverted")
                    return !Condition(c["term"]);
                if (type == "table_bonus") {
                    const auto& chances = c["chances"].items;
                    if (chances.empty())
                        return false;
                    const int level = std::clamp(Enchant(c["enchantment"].String()), 0,
                                                 static_cast<int>(chances.size()) - 1);
                    return Unit() < static_cast<float>(chances[static_cast<size_t>(level)].Number());
                }
                if (type == "killed_by_player")
                    return ctx.killedByPlayer;
                if (type == "random_chance")
                    return Unit() < Float(c["chance"]);
                if (type == "random_chance_with_enchanted_bonus") {
                    const int level = Enchant(c["enchantment"].String());
                    if (level <= 0)
                        return Unit() < Float(c["unenchanted_chance"]);
                    const json::Value& e = c["enchanted_chance"];
                    float chance = 0;
                    if (e.IsObject() && Bare(e["type"].String()) == "linear")
                        chance = static_cast<float>(e["base"].Number() + e["per_level_above_first"].Number() * (level - 1));
                    else
                        chance = Float(e);
                    return Unit() < chance;
                }
                if (type == "entity_properties")
                    return EntityPredicate(c["entity"].String("this"), c["predicate"]);
                if (type == "damage_source_properties") {
                    const json::Value& p = c["predicate"];
                    const json::Value& src = p["source_entity"]["minecraft:entity_type"];
                    if (src.IsString())
                        return Bare(src.text) == Bare(ctx.attackerType);
                    return false;
                }
                if (type == "location_check") {
                    if (!ctx.neighbor)
                        return true;
                    const LootBlock other = ctx.neighbor(static_cast<int>(c["offsetX"].Int()),
                                                         static_cast<int>(c["offsetY"].Int()),
                                                         static_cast<int>(c["offsetZ"].Int()));
                    const json::Value& b = c["predicate"]["block"];
                    if (b.IsNull())
                        return true;
                    return BlockMatches(other, b["blocks"], b["state"]);
                }
                return false;
            }

            bool Conditions(const json::Value& holder) {
                for (const json::Value* c : Many(holder["condition"]))
                    if (!Condition(*c))
                        return false;
                return true;
            }

            void Modify(LootDrop& d, const json::Value& holder) {
                for (const json::Value* mp : Many(holder["modifier"])) {
                    const json::Value& m = *mp;
                    if (!Conditions(m))
                        continue;
                    const std::string type = Bare(m["type"].String());
                    if (type == "set_count") {
                        const int n = Int(m["count"]);
                        d.count = m["add"].Bool(false) ? d.count + n : n;
                    } else if (type == "enchanted_count_increase") {
                        const int level = Enchant(m["enchantment"].String());
                        if (level > 0) {
                            d.count += static_cast<int>(std::lround(Float(m["count"]) * static_cast<float>(level)));
                            if (!m["limit"].IsNull() && m["limit"].Int() > 0)
                                d.count = std::min(d.count, static_cast<int>(m["limit"].Int()));
                        }
                    } else if (type == "apply_bonus") {
                        const int level = Enchant(m["enchantment"].String());
                        const std::string formula = Bare(m["formula"].String());
                        if (formula == "ore_drops") {
                            if (level > 0)
                                d.count *= std::max(0, IntIn(0, level + 1) - 1) + 1;
                        } else if (formula == "uniform_bonus_count") {
                            d.count += IntIn(0, static_cast<int>(m["parameters"]["bonusMultiplier"].Number(1)) * level);
                        } else if (formula == "binomial_with_bonus_count") {
                            const int trials = static_cast<int>(m["parameters"]["extra"].Int()) + level;
                            const float p = static_cast<float>(m["parameters"]["probability"].Number());
                            for (int i = 0; i < trials; i++)
                                if (Unit() < p)
                                    d.count++;
                        }
                    } else if (type == "explosion_decay") {
                        if (ctx.explosionRadius > 0) {
                            int kept = 0;
                            for (int i = 0; i < d.count; i++)
                                if (Unit() <= 1.0f / ctx.explosionRadius)
                                    kept++;
                            d.count = kept;
                        }
                    } else if (type == "limit_count") {
                        const json::Value& l = m["limit"];
                        if (l.kind == json::Value::Kind::Number) {
                            d.count = std::min(d.count, static_cast<int>(l.number));
                        } else {
                            if (!l["min"].IsNull())
                                d.count = std::max(d.count, Int(l["min"]));
                            if (!l["max"].IsNull())
                                d.count = std::min(d.count, Int(l["max"]));
                        }
                    } else if (type == "furnace_smelt") {
                        const auto it = Smelted().find(Bare(d.item));
                        if (it != Smelted().end())
                            d.item = "minecraft:" + it->second;
                    }
                }
            }

            bool Expand(const json::Value& e, std::vector<const json::Value*>& out) {
                if (!Conditions(e))
                    return false;
                const std::string type = Bare(e["type"].String());
                if (type == "alternatives") {
                    for (const auto& child : e["children"].items)
                        if (Expand(child, out))
                            return true;
                    return false;
                }
                if (type == "group") {
                    for (const auto& child : e["children"].items)
                        Expand(child, out);
                    return true;
                }
                if (type == "sequence") {
                    for (const auto& child : e["children"].items)
                        if (!Expand(child, out))
                            break;
                    return true;
                }
                out.push_back(&e);
                return true;
            }

            void Generate(const json::Value& e, std::vector<LootDrop>& out) {
                const std::string type = Bare(e["type"].String());
                if (type == "item") {
                    LootDrop d;
                    d.item = e["name"].String();
                    d.count = 1;
                    Modify(d, e);
                    out.push_back(d);
                } else if (type == "loot_table") {
                    const json::Value& v = e["value"];
                    if (v.IsString() && depth < 8) {
                        depth++;
                        const std::string key = v.text.substr(v.text.find(':') + 1);
                        if (const auto it = tables.find(key); it != tables.end()) {
                            std::vector<LootDrop> nested;
                            Table(it->second, nested);
                            for (LootDrop& d : nested) {
                                Modify(d, e);
                                out.push_back(d);
                            }
                        }
                        depth--;
                    }
                }
            }

            void Table(const json::Value& t, std::vector<LootDrop>& out) {
                const size_t start = out.size();
                for (const auto& pool : t["pools"].items) {
                    if (!Conditions(pool))
                        continue;
                    const int rolls = pool["rolls"].IsNull() ? 1 : Int(pool["rolls"]);
                    const size_t poolStart = out.size();
                    for (int r = 0; r < rolls; r++) {
                        std::vector<const json::Value*> candidates;
                        for (const auto& entry : pool["entries"].items)
                            Expand(entry, candidates);
                        if (candidates.empty())
                            continue;
                        int total = 0;
                        for (const json::Value* c : candidates)
                            total += static_cast<int>(std::max<int64_t>(0, (*c)["weight"].Int(1)));
                        if (total <= 0)
                            continue;
                        int pick = IntIn(0, total - 1);
                        for (const json::Value* c : candidates) {
                            pick -= static_cast<int>(std::max<int64_t>(0, (*c)["weight"].Int(1)));
                            if (pick < 0) {
                                Generate(*c, out);
                                break;
                            }
                        }
                    }
                    for (size_t i = poolStart; i < out.size(); i++)
                        Modify(out[i], pool);
                }
                for (size_t i = start; i < out.size(); i++)
                    Modify(out[i], t);
            }
        };
    }

    std::vector<LootDrop> LootTables::Roll(const std::string& key, LootContext& ctx) const {
        std::vector<LootDrop> out;
        const auto it = impl->tables.find(key);
        if (it == impl->tables.end() || !ctx.rng)
            return out;
        Roller r{impl->tables, ctx};
        r.Table(it->second, out);
        std::vector<LootDrop> merged;
        for (LootDrop& d : out) {
            if (d.count <= 0)
                continue;
            const std::string name = BedrockItemName(d.item);
            if (name.empty())
                continue;
            d.item = name;
            bool joined = false;
            for (LootDrop& m : merged)
                if (m.item == d.item) {
                    m.count += d.count;
                    joined = true;
                    break;
                }
            if (!joined)
                merged.push_back(d);
        }
        return merged;
    }
}
