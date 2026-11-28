#include "client/systems/sprites/ItemDisplay.hpp"

#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "util/Json.hpp"
#include "util/ResourcePaths.hpp"
#include "util/Resources.hpp"

namespace perlin::itemdisplay {
    namespace {
        struct Table {
            std::unordered_map<std::string, Entry> rows[4];
            Entry missing;
        };

        Transform ReadTransform(json::View v) {
            Transform t;
            if (!v.isObject())
                return t;
            t.present = true;
            const auto vec = [](json::View a, glm::vec3 fallback) {
                if (!a.isArray() || a.size() < 3)
                    return fallback;
                return glm::vec3(static_cast<float>(a[0].real()), static_cast<float>(a[1].real()),
                                 static_cast<float>(a[2].real()));
            };
            t.rotation = vec(v["rotation"], glm::vec3(0.0f));
            t.translation = vec(v["translation"], glm::vec3(0.0f));
            t.scale = vec(v["scale"], glm::vec3(1.0f));
            return t;
        }

        std::string Bare(std::string s) {
            if (const size_t colon = s.find(':'); colon != std::string::npos)
                s.erase(0, colon + 1);
            return s;
        }

        Entry Build(json::View row, const std::vector<json::View>& styles) {
            Entry e;
            if (!row.isArray() || row.size() < 3)
                return e;
            const std::string kind = row[0].str();
            if (kind == "flat") {
                e.kind = Entry::Kind::Flat;
                e.frontLight = true;
            } else if (kind == "block") {
                e.kind = Entry::Kind::Block;
            } else {
                e.kind = Entry::Kind::Special;
                e.special = kind.starts_with("special:") ? kind.substr(8) : kind;
            }
            e.model = row[2].str();
            const size_t index = static_cast<size_t>(row[1].integer(-1));
            if (index < styles.size()) {
                const json::View s = styles[index];
                e.firstRight = ReadTransform(s["firstperson_righthand"]);
                e.firstLeft = ReadTransform(s["firstperson_lefthand"]);
                e.thirdRight = ReadTransform(s["thirdperson_righthand"]);
                e.thirdLeft = ReadTransform(s["thirdperson_lefthand"]);
                e.ground = ReadTransform(s["ground"]);
                e.head = ReadTransform(s["head"]);
                e.fixed = ReadTransform(s["fixed"]);
                e.gui = ReadTransform(s["gui"]);
                // Tables written before the generator resolved left hands per file.
                if (!e.firstLeft.present)
                    e.firstLeft = e.firstRight;
                if (!e.thirdLeft.present)
                    e.thirdLeft = e.thirdRight;
            }
            if (row.size() > 3 && row[3].isObject()) {
                const json::View extra = row[3];
                const json::View light = extra["light"];
                if (light.isString())
                    e.frontLight = light.str() == "front";
                const json::View local = extra["local"];
                if (local.isArray() && local.size() == 16) {
                    float m[16];
                    for (size_t i = 0; i < 16; i++)
                        m[i] = static_cast<float>(local[i].real());
                    e.local = glm::make_mat4(m);
                    e.hasLocal = true;
                }
                for (const auto [key, value] : extra["args"].members())
                    if (value.isString())
                        e.args[std::string(key)] = Bare(value.str());
            }
            e.found = true;
            return e;
        }

        const Table& Get() {
            static const Table table = [] {
                Table t;
                std::string text;
                if (!resources::Read(resources::path::kItemDisplay, text))
                    return t;
                const std::optional<json::Document> doc = json::Document::Parse(text, true);
                if (!doc)
                    return t;
                const json::View root = doc->root();
                std::vector<json::View> styles;
                for (json::View s : root["styles"].items())
                    styles.push_back(s);
                static const char* const kContexts[4] = {"hand", "ground", "gui", "hand_use"};
                for (const auto [name, row] : root["items"].members()) {
                    const json::View hand = row["hand"];
                    for (int c = 0; c < 4; c++) {
                        const json::View own = row[kContexts[c]];
                        t.rows[c][std::string(name)] = Build(own.isArray() ? own : hand, styles);
                    }
                }
                return t;
            }();
            return table;
        }
    }

    std::string JavaItemName(std::string_view name, int aux) {
        if (name.starts_with("minecraft:"))
            name.remove_prefix(10);
        static const char* const kDyes[16] = {"white", "orange", "magenta", "light_blue", "yellow", "lime",
                                              "pink",  "gray",   "light_gray", "cyan",     "purple", "blue",
                                              "brown", "green",  "red",     "black"};
        if (aux >= 0 && aux < 16) {
            if (name == "bed")
                return std::string(kDyes[aux]) + "_bed";
            // Bedrock numbers banner colours in reverse: 0 is black, 15 white.
            if (name == "banner")
                return std::string(kDyes[15 - aux]) + "_banner";
        }
        if (name == "skull") {
            static const char* const kSkulls[7] = {"skeleton_skull", "wither_skeleton_skull", "zombie_head",
                                                   "player_head",    "creeper_head",          "dragon_head",
                                                   "piglin_head"};
            return kSkulls[aux >= 0 && aux < 7 ? aux : 0];
        }
        if (name.starts_with("light_block"))
            return "light";
        static const std::unordered_map<std::string_view, std::string_view> kRenamed = {
            // Same id, different item: Bedrock's is the Java item on the right.
            {"snow", "snow_block"},
            {"stone_stairs", "cobblestone_stairs"},
            {"nether_brick", "nether_bricks"},
            {"chain", "iron_chain"},
            // Bedrock-only ids.
            {"snow_layer", "snow"},
            {"normal_stone_stairs", "stone_stairs"},
            {"normal_stone_slab", "stone_slab"},
            {"stone_block_slab", "smooth_stone_slab"},
            {"netherbrick", "nether_brick"},
            {"red_nether_brick", "red_nether_bricks"},
            {"brick_block", "bricks"},
            {"end_bricks", "end_stone_bricks"},
            {"end_brick_stairs", "end_stone_brick_stairs"},
            {"prismarine_bricks_stairs", "prismarine_brick_stairs"},
            {"hardened_clay", "terracotta"},
            {"silver_glazed_terracotta", "light_gray_glazed_terracotta"},
            {"magma", "magma_block"},
            {"slime", "slime_block"},
            {"melon_block", "melon"},
            {"lit_pumpkin", "jack_o_lantern"},
            {"mob_spawner", "spawner"},
            {"quartz_ore", "nether_quartz_ore"},
            {"noteblock", "note_block"},
            {"golden_rail", "powered_rail"},
            {"frame", "item_frame"},
            {"glow_frame", "glow_item_frame"},
            {"web", "cobweb"},
            {"waterlily", "lily_pad"},
            {"deadbush", "dead_bush"},
            {"reeds", "sugar_cane"},
            {"wooden_door", "oak_door"},
            {"wooden_button", "oak_button"},
            {"wooden_pressure_plate", "oak_pressure_plate"},
            {"trapdoor", "oak_trapdoor"},
            {"fence_gate", "oak_fence_gate"},
            {"grass_path", "dirt_path"},
            {"dirt_with_roots", "rooted_dirt"},
            {"frog_spawn", "frogspawn"},
            {"azalea_leaves_flowered", "flowering_azalea_leaves"},
            {"stonecutter_block", "stonecutter"},
            {"undyed_shulker_box", "shulker_box"},
            {"waxed_copper", "waxed_copper_block"},
            {"standing_sign", "oak_sign"},
            {"darkoak_standing_sign", "dark_oak_sign"},
            {"crimson_standing_sign", "crimson_sign"},
            {"warped_standing_sign", "warped_sign"},
            {"empty_map", "map"},
            {"lodestone_compass", "compass"},
            {"scute", "turtle_scute"},
            {"turtle_shell_piece", "turtle_scute"},
            {"small_dripleaf_block", "small_dripleaf"},
            {"bamboo_sapling", "bamboo"},
            {"carrots", "carrot"},
            {"potatoes", "potato"},
            {"cocoa", "cocoa_beans"},
            {"sweet_berry_bush", "sweet_berries"},
            {"pitcher_crop", "pitcher_pod"},
            {"torchflower_crop", "torchflower_seeds"},
            {"melon_stem", "melon_seeds"},
            {"pumpkin_stem", "pumpkin_seeds"},
            {"trip_wire", "string"},
            {"redstone_wire", "redstone"},
            {"powder_snow", "powder_snow_bucket"},
            {"cave_vines", "glow_berries"},
            {"grass", "grass_block"},
            {"tallgrass", "short_grass"},
            {"yellow_flower", "dandelion"},
            {"red_flower", "poppy"},
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
            {"daylight_detector_inverted", "daylight_detector"},
            {"appleenchanted", "enchanted_golden_apple"},
            {"chorus_fruit_popped", "popped_chorus_fruit"},
            {"speckled_melon", "glistering_melon_slice"},
            {"fireball", "fire_charge"},
            {"clownfish", "tropical_fish"},
            {"fish", "cod"},
            {"cooked_fish", "cooked_cod"},
            {"horsearmorleather", "leather_horse_armor"},
            {"horsearmoriron", "iron_horse_armor"},
            {"horsearmorgold", "golden_horse_armor"},
            {"horsearmordiamond", "diamond_horse_armor"},
            {"carrotonastick", "carrot_on_a_stick"},
            {"bed", "red_bed"},
            {"banner", "white_banner"},
        };
        const auto it = kRenamed.find(name);
        return std::string(it == kRenamed.end() ? name : it->second);
    }

    const Entry& Lookup(const std::string& name, int aux, Context context) {
        const Table& t = Get();
        const auto& map = t.rows[static_cast<int>(context)];
        const auto it = map.find(JavaItemName(name, aux));
        return it == map.end() ? t.missing : it->second;
    }

    const Entry& LookupJava(const std::string& javaName, Context context) {
        const Table& t = Get();
        const auto& map = t.rows[static_cast<int>(context)];
        const auto it = map.find(javaName);
        return it == map.end() ? t.missing : it->second;
    }
}
