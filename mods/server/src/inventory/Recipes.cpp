#include "inventory/Recipes.hpp"

#include "world/BlockCatalog.hpp"

#include "protocol/Packets.hpp"

#include <algorithm>
#include <random>
#include <sstream>

namespace perlin::server {
    extern const char* const kRecipeText[];

    namespace {
        std::string Bare(const std::string& n) { return n.starts_with("minecraft:") ? n.substr(10) : n; }
        bool Ends(const std::string& s, const char* t) {
            const std::string e = t;
            return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
        }
        std::vector<std::string> Split(const std::string& s, char c) {
            std::vector<std::string> out;
            std::string part;
            std::istringstream in(s);
            while (std::getline(in, part, c))
                out.push_back(part);
            if (!s.empty() && s.back() == c)
                out.emplace_back();
            return out;
        }
        int16_t ItemId(const std::string& name) {
            const CatalogItem* item = BlockCatalog::Instance().ItemByName("minecraft:" + name);
            return item ? item->networkId : 0;
        }
    }

    Recipes& Recipes::Instance() {
        static Recipes r;
        return r;
    }

    bool Recipes::Matches(const std::string& ingredient, const std::string& item) {
        const std::string n = Bare(item);
        if (ingredient.empty())
            return n.empty();
        if (ingredient[0] != '#')
            return Bare(ingredient) == n;
        const std::string t = ingredient.substr(1);
        if (t == "planks")
            return Ends(n, "_planks");
        if (t == "logs")
            return n.contains("_log") || Ends(n, "_wood") || Ends(n, "_stem") || Ends(n, "_hyphae");
        if (t == "logs_that_burn")
            return (n.contains("_log") || Ends(n, "_wood")) && !n.contains("crimson") && !n.contains("warped");
        if (t == "coals")
            return n == "coal" || n == "charcoal";
        if (t == "stone_crafting_materials" || t == "stone_tool_materials")
            return n == "cobblestone" || n == "blackstone" || n == "cobbled_deepslate";
        if (t == "soul_fire_base_blocks")
            return n == "soul_sand" || n == "soul_soil";
        if (t == "wool")
            return Ends(n, "_wool");
        if (t == "wooden_slabs")
            return Ends(n, "_slab") && (n.contains("oak") || n.contains("spruce") || n.contains("birch") ||
                                        n.contains("jungle") || n.contains("acacia") || n.contains("cherry") ||
                                        n.contains("mangrove") || n.contains("bamboo") || n.contains("crimson") ||
                                        n.contains("warped") || n.contains("pale_oak")) && !n.contains("mosaic");
        if (t == "mushrooms_for_stew")
            return n == "brown_mushroom" || n == "red_mushroom";
        if (t == "metal_nuggets")
            return n == "iron_nugget" || n == "gold_nugget";
        if (t == "egg")
            return n == "egg" || n == "blue_egg" || n == "brown_egg";
        if (t == "is_pickaxe")
            return Ends(n, "_pickaxe");
        return false;
    }

    int Recipes::FuelTicks(const std::string& item) {
        const std::string n = Bare(item);
        if (n == "lava_bucket")
            return 20000;
        if (n == "coal_block")
            return 16000;
        if (n == "dried_kelp_block")
            return 4000;
        if (n == "blaze_rod")
            return 2400;
        if (n == "coal" || n == "charcoal")
            return 1600;
        if (Ends(n, "_boat") || Ends(n, "_raft"))
            return 1200;
        if (Ends(n, "_planks") || n.contains("_log") || Ends(n, "_wood") || Ends(n, "_fence") || Ends(n, "_fence_gate") ||
            n == "crafting_table" || n == "chest" || n == "trapped_chest" || n == "bookshelf" || n == "barrel" ||
            n == "jukebox" || n == "noteblock" || n == "lectern" || n == "composter" || Ends(n, "_stairs") && n.contains("oak") ||
            n == "bamboo_block" || n == "cartography_table" || n == "fletching_table" || n == "smithing_table" ||
            n == "loom" || n == "daylight_detector")
            return 300;
        if (Ends(n, "_slab") && (n.contains("oak") || n.contains("spruce") || n.contains("birch") || n.contains("jungle") ||
                                 n.contains("acacia") || n.contains("cherry") || n.contains("mangrove")))
            return 150;
        if (Ends(n, "_sword") && n.contains("wooden") || Ends(n, "_pickaxe") && n.contains("wooden") ||
            Ends(n, "_axe") && n.contains("wooden") || Ends(n, "_shovel") && n.contains("wooden") ||
            Ends(n, "_hoe") && n.contains("wooden") || n == "bow" || n == "fishing_rod" || n == "ladder" ||
            Ends(n, "_door") && !n.contains("iron") || Ends(n, "_trapdoor") && !n.contains("iron") || Ends(n, "_sign"))
            return 200;
        if (n == "stick" || Ends(n, "_sapling") || Ends(n, "_wool") || n == "bowl" || Ends(n, "_button") ||
            n == "dead_bush" || n == "azalea" || n == "mangrove_propagule")
            return 100;
        if (Ends(n, "_carpet"))
            return 67;
        if (n == "bamboo" || n == "scaffolding")
            return 50;
        return 0;
    }

    void Recipes::Load() {
        if (loaded)
            return;
        loaded = true;
        std::string text;
        for (int i = 0; kRecipeText[i]; i++)
            text += kRecipeText[i];
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            const std::vector<std::string> f = Split(line, '|');
            if (f.empty())
                continue;
            const auto parseOut = [&](const std::string& o, std::string& name, int& count, int& data) {
                const std::vector<std::string> p = Split(o, ':');
                name = p.size() > 0 ? p[0] : "";
                count = p.size() > 1 ? std::max(1, std::atoi(p[1].c_str())) : 1;
                data = p.size() > 2 ? std::atoi(p[2].c_str()) : 0;
            };
            if ((f[0] == "S" && f.size() == 7) || (f[0] == "L" && f.size() == 5)) {
                CraftRecipe r;
                r.shaped = f[0] == "S";
                bool ok = true;
                if (r.shaped) {
                    r.width = std::atoi(f[3].c_str());
                    r.height = std::atoi(f[4].c_str());
                    for (const std::string& c : Split(f[5], ',')) {
                        if (!c.empty() && c[0] != '#' && !ItemId(c))
                            ok = false;
                        r.input.push_back({c, 1});
                    }
                    parseOut(f[6], r.output, r.outputCount, r.outputData);
                } else {
                    for (const std::string& c : Split(f[3], ',')) {
                        const size_t star = c.find('*');
                        RecipeIngredient in{c.substr(0, star), star == std::string::npos ? 1 : std::atoi(c.c_str() + star + 1)};
                        if (!in.item.empty() && in.item[0] != '#' && !ItemId(in.item))
                            ok = false;
                        for (int k = 0; k < std::max(1, in.count); k++)
                            r.input.push_back({in.item, 1});
                    }
                    parseOut(f[4], r.output, r.outputCount, r.outputData);
                }
                r.outputId = ItemId(r.output);
                if (!ok || !r.outputId || r.input.size() > 9)
                    continue;
                r.netId = static_cast<uint32_t>(crafting.size() + 1);
                crafting.push_back(std::move(r));
            } else if (f[0] == "F" && f.size() == 4) {
                SmeltRecipe s;
                for (const std::string& t : Split(f[1], ',')) {
                    s.furnace |= t == "furnace";
                    s.blast |= t == "blast_furnace";
                    s.smoker |= t == "smoker";
                    s.campfire |= t == "campfire" || t == "soul_campfire";
                }
                s.input = f[2];
                int data = 0;
                parseOut(f[3], s.output, s.outputCount, data);
                s.outputId = ItemId(s.output);
                if (s.outputId)
                    smelting.push_back(std::move(s));
            }
        }
        proto::CraftingDataPacket p;
        std::mt19937_64 rng(42);
        const auto ingredient = [](const RecipeIngredient& in) {
            proto::RecipeIngredient r;
            if (in.item.empty()) {
                r.type = proto::RecipeIngredientType::Invalid;
                return r;
            }
            r.type = proto::RecipeIngredientType::Valid;
            r.count = 1;
            if (in.item[0] == '#') {
                r.descriptorType = "item_tag";
                r.tag = "minecraft:" + in.item.substr(1);
            } else {
                r.descriptorType = "name";
                r.name = "minecraft:" + in.item;
                r.metadata = 32767;
            }
            return r;
        };
        for (const CraftRecipe& r : crafting) {
            proto::ItemLegacy out;
            out.networkId = r.outputId;
            out.count = static_cast<uint16_t>(r.outputCount);
            out.metadata = static_cast<uint32_t>(r.outputData);
            if (const CatalogItem* item = BlockCatalog::Instance().ItemById(r.outputId); item && item->block)
                out.blockRuntimeId = item->blockHash;
            net::Uuid uuid;
            for (auto& b : uuid.bytes)
                b = static_cast<uint8_t>(rng());
            if (r.shaped) {
                proto::ShapedRecipe s;
                s.recipeId = "minecraft:" + r.output + "_" + std::to_string(r.netId);
                s.width = r.width;
                s.height = r.height;
                for (const auto& in : r.input)
                    s.input.push_back(ingredient(in));
                s.output.push_back(out);
                s.uuid = uuid;
                s.block = "crafting_table";
                s.networkId = r.netId;
                p.shapedRecipes.push_back(std::move(s));
            } else {
                proto::ShapelessRecipe s;
                s.recipeId = "minecraft:" + r.output + "_" + std::to_string(r.netId);
                for (const auto& in : r.input)
                    s.input.push_back(ingredient(in));
                s.output.push_back(out);
                s.uuid = uuid;
                s.block = "crafting_table";
                s.networkId = r.netId;
                p.shapelessRecipes.push_back(std::move(s));
            }
        }
        p.clearRecipes = true;
        net::EncodeChecked(p, body);
    }

    const CraftRecipe* Recipes::ByNetId(uint32_t id) const {
        if (id == 0 || id > crafting.size())
            return nullptr;
        return &crafting[id - 1];
    }

    const SmeltRecipe* Recipes::Smelt(const std::string& item, int station) const {
        const std::string n = Bare(item);
        for (const SmeltRecipe& s : smelting) {
            if (!Matches(s.input, n))
                continue;
            if ((station == 0 && s.furnace) || (station == 1 && s.blast) || (station == 2 && s.smoker))
                return &s;
        }
        return nullptr;
    }
}
