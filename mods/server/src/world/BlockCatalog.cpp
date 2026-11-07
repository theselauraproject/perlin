#include "world/BlockCatalog.hpp"

#include "core/Json.hpp"
#include "core/Log.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#ifndef PERLIN_SERVER_RESOURCE_DIR
#define PERLIN_SERVER_RESOURCE_DIR ""
#endif

namespace perlin::server {
    namespace fs = std::filesystem;

    BlockCatalog& BlockCatalog::Instance() {
        static BlockCatalog catalog;
        return catalog;
    }

    std::string BlockCatalog::FindFile(const std::string& dir, const std::string& name) {
        std::error_code ec;
        for (const std::string& base :
             {dir, dir.empty() ? std::string() : dir + "/resources", std::string("resources"),
              std::string(PERLIN_SERVER_RESOURCE_DIR), std::string("../resources")}) {
            if (base.empty())
                continue;
            const fs::path p = fs::path(base) / name;
            if (fs::is_regular_file(p, ec))
                return p.string();
        }
        return {};
    }

    const CatalogItem* BlockCatalog::ItemById(int16_t id) const {
        const auto it = byId.find(id);
        return it == byId.end() ? nullptr : &items[it->second];
    }
    const CatalogItem* BlockCatalog::ItemByBlockHash(int32_t hash) const {
        const auto it = byHash.find(hash);
        return it == byHash.end() ? nullptr : &items[it->second];
    }
    const CatalogItem* BlockCatalog::ItemByName(const std::string& name) const {
        const auto it = byName.find(name);
        return it == byName.end() ? nullptr : &items[it->second];
    }

    uint16_t BlockCatalog::RegistryId(int32_t hash) const {
        const auto it = states.find(hash);
        if (it == states.end())
            return BlockRegistry::kAir;
        return BlockRegistry::Instance().Intern(it->second.name, it->second.list);
    }

    uint16_t BlockCatalog::DefaultBlock(const std::string& name) const {
        const std::string full = !name.contains(':') ? "minecraft:" + name : name;
        if (const auto it = defaultByName.find(full); it != defaultByName.end())
            return RegistryId(it->second);
        return BlockRegistry::Instance().Intern(full);
    }

    namespace {
        std::string ValueText(const StateValue& v) {
            if (const bool* b = std::get_if<bool>(&v))
                return *b ? "true" : "false";
            if (const int32_t* i = std::get_if<int32_t>(&v))
                return std::to_string(*i);
            return std::get<std::string>(v);
        }
    }

    std::string BlockCatalog::StateText(uint16_t id) const {
        const BlockRegistry& r = BlockRegistry::Instance();
        const auto it = states.find(r.Hash(id));
        if (it == states.end())
            return r.Name(id) + "[]";
        std::string out = it->second.name + "[";
        bool first = true;
        for (const auto& [k, v] : it->second.list) {
            if (!first)
                out += ',';
            first = false;
            out += k + "=" + ValueText(v);
        }
        return out + "]";
    }

    uint16_t BlockCatalog::Resolve(std::string_view text) const {
        std::lock_guard lock(resolveMutex);
        const std::string key(text);
        if (const auto it = resolved.find(key); it != resolved.end())
            return it->second;
        if (hashesByName.empty())
            for (const auto& [hash, state] : states)
                hashesByName[state.name].push_back(hash);
        const size_t open = key.find('[');
        std::string name = key.substr(0, open);
        if (!name.contains(':'))
            name = "minecraft:" + name;
        std::vector<std::pair<std::string, std::string>> want;
        if (open != std::string::npos) {
            const std::string body = key.substr(open + 1, key.size() - open - 2);
            size_t a = 0;
            while (a < body.size()) {
                size_t b = body.find(',', a);
                if (b == std::string::npos)
                    b = body.size();
                const std::string part = body.substr(a, b - a);
                if (const size_t eq = part.find('='); eq != std::string::npos)
                    want.emplace_back(part.substr(0, eq), part.substr(eq + 1));
                a = b + 1;
            }
        }
        uint16_t id = 0;
        int bestScore = -1;
        int32_t bestHash = 0;
        const int32_t fallback = defaultByName.contains(name) ? defaultByName.at(name) : 0;
        if (const auto it = hashesByName.find(name); it != hashesByName.end()) {
            for (const int32_t hash : it->second) {
                const State& s = states.at(hash);
                int score = 0;
                for (const auto& [k, v] : want)
                    for (const auto& [sk, sv] : s.list)
                        if (sk == k && ValueText(sv) == v)
                            score += 2;
                if (hash == fallback)
                    score += 1;
                if (score > bestScore) {
                    bestScore = score;
                    bestHash = hash;
                }
            }
        }
        id = bestScore >= 0 ? RegistryId(bestHash) : DefaultBlock(name);
        resolved.emplace(key, id);
        return id;
    }

    namespace {
        // blocks the game keeps for itself: old lit/unlit variants, world-edge markers, signs that only exist as placed blocks, education leftovers.
        // none of them are in the creative menu of a normal bedrock world
        bool IsTechnicalBlock(const std::string& n) {
            static const char* const exact[] = {"glowingobsidian",
                                                "netherreactor",
                                                "info_update",
                                                "info_update2",
                                                "reserved6",
                                                "invisible_bedrock",
                                                "moving_block",
                                                "allow",
                                                "deny",
                                                "border_block",
                                                "camera",
                                                "flowing_water",
                                                "flowing_lava",
                                                "unlit_redstone_torch",
                                                "lit_pumpkin",
                                                "wall_sign",
                                                "standing_sign",
                                                "standing_banner",
                                                "wall_banner",
                                                "piston_arm_collision",
                                                "sticky_piston_arm_collision",
                                                "unpowered_repeater",
                                                "powered_repeater",
                                                "unpowered_comparator",
                                                "powered_comparator",
                                                "lit_redstone_lamp",
                                                "lit_furnace",
                                                "lit_blast_furnace",
                                                "lit_smoker",
                                                "lit_redstone_ore",
                                                "lit_deepslate_redstone_ore",
                                                "water",
                                                "lava",
                                                "fire",
                                                "soul_fire",
                                                "portal",
                                                "end_portal",
                                                "end_gateway",
                                                "bubble_column",
                                                "light_block"};
            for (const char* e : exact)
                if (n == e)
                    return true;
            const auto ends = [&](const char* s) {
                const std::string x = s;
                return n.size() >= x.size() && n.compare(n.size() - x.size(), x.size(), x) == 0;
            };
            return ends("_wall_sign") || ends("_standing_sign");
        }
    }

    void BlockCatalog::BuildItems(const std::vector<std::string>& names,
                                  const std::unordered_map<std::string, int32_t>& defaultHash) {
        items.clear();
        byId.clear();
        byHash.clear();
        byName.clear();
        std::vector<std::string> sorted = names;
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        for (const std::string& name : sorted) {
            const auto it = defaultHash.find("minecraft:" + name);
            if (it == defaultHash.end() || name == "air" || IsTechnicalBlock(name))
                continue;
            CatalogItem item;
            item.name = "minecraft:" + name;
            item.networkId = static_cast<int16_t>(items.size() + 1);
            item.blockHash = it->second;
            byId[item.networkId] = items.size();
            byHash[item.blockHash] = items.size();
            byName[item.name] = items.size();
            items.push_back(std::move(item));
        }
    }

    bool BlockCatalog::Load(const std::string& blockStatesPath, const std::string& itemNamesPath,
                            std::string& error, const std::string& extraItemsPath) {
        std::ifstream states_in(blockStatesPath, std::ios::binary);
        if (!states_in) {
            error = "cannot read " + blockStatesPath;
            return false;
        }
        std::stringstream text;
        text << states_in.rdbuf();
        json::Value root;
        std::string parseError;
        if (!json::Parse(text.str(), root, &parseError) || !root.IsArray()) {
            error = blockStatesPath + ": " + (parseError.empty() ? "not an array" : parseError);
            return false;
        }
        states.clear();
        std::unordered_map<std::string, int32_t> defaultHash;
        states.reserve(root.items.size());
        for (const json::Value& entry : root.items) {
            const std::string name = entry[1].String();
            if (name.empty())
                continue;
            const int32_t hash = static_cast<int32_t>(entry[0].Int());
            State s;
            s.name = name;
            // keys arrive sorted in the file, the registry sorts again anyway
            for (const auto& [key, v] : entry[2].members) {
                const std::string type = v[0].String();
                if (type == "b")
                    s.list.emplace_back(key, StateValue(v[1].Int() != 0));
                else if (type == "i")
                    s.list.emplace_back(key, StateValue(static_cast<int32_t>(v[1].Int())));
                else
                    s.list.emplace_back(key, StateValue(v[1].String()));
            }
            // the first permutation listed is the default one
            defaultHash.emplace(name, hash);
            states.emplace(hash, std::move(s));
        }
        std::vector<std::string> names;
        std::ifstream map(itemNamesPath);
        std::string line;
        while (std::getline(map, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            // block_destroy.tsv: name, hardness, tool, level; hardness -1 marks technical blocks that never become items
            // (bedrock and barrier stay: creative players build with them)
            const size_t tab = line.find('\t');
            if (tab == std::string::npos)
                continue;
            const std::string name = line.substr(0, tab);
            const size_t next = line.find('\t', tab + 1);
            const std::string hardness =
                line.substr(tab + 1, next == std::string::npos ? std::string::npos : next - tab - 1);
            if (hardness == "-1" && name != "bedrock" && name != "barrier")
                continue;
            if (IsTechnicalBlock(name))
                continue;
            names.push_back(name);
        }
        if (names.empty()) {
            // no list of item names: every block that has a single state
            for (const auto& [n, h] : defaultHash)
                names.push_back(n.substr(10));
        }
        defaultByName = defaultHash;
        BuildItems(names, defaultHash);
        if (!extraItemsPath.empty()) {
            std::ifstream extra(extraItemsPath);
            std::string item;
            while (std::getline(extra, item)) {
                while (!item.empty() && (item.back() == '\r' || item.back() == ' '))
                    item.pop_back();
                if (item.empty() || item[0] == '#' || byName.contains("minecraft:" + item))
                    continue;
                CatalogItem entry;
                entry.name = "minecraft:" + item;
                entry.networkId = static_cast<int16_t>(items.size() + 1);
                entry.block = false;
                byId[entry.networkId] = items.size();
                byName[entry.name] = items.size();
                items.push_back(std::move(entry));
            }
        }
        fromFiles = true;
        return !items.empty();
    }

    void BlockCatalog::LoadBuiltin() {
        static const char* const kNames[] = {"stone",         "granite",
                                             "diorite",       "andesite",
                                             "cobblestone",   "mossy_cobblestone",
                                             "dirt",          "grass_block",
                                             "sand",          "gravel",
                                             "oak_planks",    "spruce_planks",
                                             "birch_planks",  "jungle_planks",
                                             "acacia_planks", "dark_oak_planks",
                                             "glass",         "bricks",
                                             "stone_bricks",  "sandstone",
                                             "obsidian",      "bedrock",
                                             "white_wool",    "orange_wool",
                                             "red_wool",      "blue_wool",
                                             "black_wool",    "gold_block",
                                             "iron_block",    "diamond_block",
                                             "emerald_block", "coal_block",
                                             "bookshelf",     "glowstone",
                                             "netherrack",    "snow",
                                             "clay",          "pumpkin",
                                             "melon_block",   "sea_lantern",
                                             "quartz_block",  "prismarine"};
        states.clear();
        std::unordered_map<std::string, int32_t> defaultHash;
        std::vector<std::string> names;
        for (const char* n : kNames) {
            const std::string full = std::string("minecraft:") + n;
            const int32_t hash = BlockNetworkHash(full, {});
            State s;
            s.name = full;
            states.emplace(hash, std::move(s));
            defaultHash.emplace(full, hash);
            names.push_back(n);
        }
        defaultByName = defaultHash;
        BuildItems(names, defaultHash);
        fromFiles = false;
    }
}
