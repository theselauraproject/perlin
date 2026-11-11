#include "client/PreviewWorld.hpp"
#include "util/Paths.hpp"
#include "client/resources/BlockBoxDefinition.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>
#include <iterator>
#include <map>
#include <regex>
#include <string>

#include "util/Resources.hpp"
#include "util/Log.hpp"

namespace perlin {
    namespace {
        class Ids {
        public:
            uint16_t Get(const std::string& name, const std::string& stateFragment = "") {
                const std::string key = name + "|" + stateFragment;
                if (const auto it = cache.find(key); it != cache.end())
                    return it->second;
                std::string stateKey, valueText;
                if (!stateFragment.empty()) {
                    const size_t q1 = stateFragment.find('"'), q2 = stateFragment.find('"', q1 + 1);
                    stateKey = stateFragment.substr(q1 + 1, q2 - q1 - 1);
                    const size_t comma = stateFragment.find(',', q2);
                    valueText = stateFragment.substr(comma + 1, stateFragment.rfind(']') - comma - 1);
                    valueText.erase(std::remove(valueText.begin(), valueText.end(), '"'), valueText.end());
                }
                const uint16_t id = BlockPalette::Instance().Intern(
                    BlockPalette::Instance().FindHash("minecraft:" + name, stateKey, valueText));
                cache[key] = id;
                return id;
            }

        private:
            std::map<std::string, uint16_t> cache;
        };

        float Hash(int x, int z) {
            uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u;
            h = (h ^ (h >> 13)) * 1274126177u;
            return static_cast<float>((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
        }

        float Noise(float x, float z) {
            const int xi = static_cast<int>(std::floor(x)), zi = static_cast<int>(std::floor(z));
            const float fx = x - xi, fz = z - zi;
            const float ux = fx * fx * (3 - 2 * fx), uz = fz * fz * (3 - 2 * fz);
            const float a = Hash(xi, zi), b = Hash(xi + 1, zi), c = Hash(xi, zi + 1),
                        d = Hash(xi + 1, zi + 1);
            return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uz;
        }

        int Height(int x, int z) {
            const float n = Noise(x * 0.018f, z * 0.018f) * 0.62f + Noise(x * 0.05f, z * 0.05f) * 0.28f +
                            Noise(x * 0.13f, z * 0.13f) * 0.10f;
            return 52 + static_cast<int>(n * 34.0f);
        }
    }

    std::vector<std::pair<int32_t, std::string>> PreviewBlockList() {
        std::vector<std::pair<int32_t, std::string>> blocks;
        for (auto& entry : BlockPalette::Instance().OneStatePerName())
            if (entry.second.starts_with("minecraft:"))
                blocks.push_back(std::move(entry));
        return blocks;
    }

    namespace {
        bool BuildGallery(Level& level, int radius, const char* spec) {
            int page = 0, columns = 12, rows = 7;
            std::sscanf(spec, "%d,%d,%d", &page, &columns, &rows);
            std::vector<std::pair<int32_t, std::string>> blocks = PreviewBlockList();
            std::vector<std::string> partners;
            if (const char* only = std::getenv("PERLIN_PREVIEW_ONLY")) {
                std::vector<std::pair<int32_t, std::string>> picked;
                std::string list = only;
                for (size_t from = 0; from <= list.size();) {
                    size_t comma = list.find(',', from);
                    if (comma == std::string::npos)
                        comma = list.size();
                    std::string item = list.substr(from, comma - from), partner, overrides;
                    if (const size_t bar = item.find('|'); bar != std::string::npos) {
                        partner = item.substr(bar + 1);
                        item = item.substr(0, bar);
                    }
                    if (const size_t br = item.find('['); br != std::string::npos) {
                        overrides = item.substr(br + 1, item.find(']', br) == std::string::npos
                                                            ? std::string::npos
                                                            : item.find(']', br) - br - 1);
                        item = item.substr(0, br);
                    }
                    for (const auto& b : blocks)
                        if (b.second == "minecraft:" + item) {
                            auto chosen = b;
                            if (!overrides.empty()) {
                                BlockPalette& pal = BlockPalette::Instance();
                                auto states = pal.States(pal.Intern(b.first));
                                for (size_t at = 0; at <= overrides.size();) {
                                    size_t semi = overrides.find(';', at);
                                    if (semi == std::string::npos)
                                        semi = overrides.size();
                                    const std::string kv = overrides.substr(at, semi - at);
                                    if (const size_t eq = kv.find('='); eq != std::string::npos)
                                        for (auto& st : states)
                                            if (st.first == kv.substr(0, eq)) {
                                                const std::string val = kv.substr(eq + 1);
                                                if (st.second.kind == BlockPalette::StateValue::String)
                                                    st.second.s = val;
                                                else
                                                    st.second.i = std::atoi(val.c_str());
                                            }
                                    at = semi + 1;
                                }
                                std::sort(states.begin(), states.end(),
                                          [](const auto& x, const auto& y) { return x.first < y.first; });
                                chosen.first = BlockPalette::HashState(b.second, states);
                            }
                            picked.push_back(chosen);
                            partners.push_back(partner);
                            break;
                        }
                    from = comma + 1;
                }
                blocks = picked;
                page = 0;
            }
            for (int cx = -radius; cx < radius; cx++)
                for (int cz = -radius; cz < radius; cz++)
                    level.EnsureChunk(cx, cz);
            Ids ids;
            const uint16_t stone = ids.Get("stone"), bedrock = ids.Get("bedrock");
            const int span = radius * 16;
            for (int x = -span; x < span; x++)
                for (int z = -span; z < span; z++) {
                    level.SetBlock(x, -64, z, bedrock);
                    for (int y = -63; y <= 60; y++)
                        level.SetBlock(x, y, z, stone);
                }
            const int perPage = columns * rows;
            std::string names;
            for (int i = 0; i < perPage; i++) {
                const size_t index = static_cast<size_t>(page) * perPage + i;
                if (index >= blocks.size())
                    break;
                const int col = i % columns, row = i / columns;
                const int x = col * 3, z = row * 3;
                level.SetBlock(x, 61, z, BlockPalette::Instance().Intern(blocks[index].first));
                if (index < partners.size() && !partners[index].empty())
                    for (const auto& b : blocks)
                        (void)b;
                if (index < partners.size() && !partners[index].empty()) {
                    const int32_t partner =
                        partners[index] == "same"
                            ? blocks[index].first
                            : BlockPalette::Instance().FindHash("minecraft:" + partners[index], "", "");
                    level.SetBlock(x + 1, 61, z, BlockPalette::Instance().Intern(partner));
                }
                names += std::to_string(row) + "," + std::to_string(col) + " " + blocks[index].second + "\n";
            }
            std::cout << "[gallery] page " << page << " of " << (blocks.size() + perPage - 1) / perPage
                      << " (" << blocks.size() << " blocks)\n"
                      << names << std::flush;
            return true;
        }
    }

    bool BuildPreviewWorld(Level& level, int radius) {
        if (!BlockPalette::Instance().NamesLoaded())
            return false;
        if (const char* path = std::getenv("PERLIN_PREVIEW_BOX_FILE")) {
            std::ifstream file(path);
            const std::string text((std::istreambuf_iterator<char>(file)), {});
            ui::Json definitions;
            std::string error;
            if (!ui::Json::Parse(text, definitions, error) || !definitions.IsObject()) {
                std::cerr << "[preview] invalid box fixture: " << error << '\n';
                return false;
            }
            for (const auto& [name, components] : definitions.object) {
                CustomBlockCollision collision;
                collision.base = ParseBlockBoxComponent(components.Find("minecraft:collision_box"));
                collision.selection = ParseBlockBoxComponent(components.Find("minecraft:selection_box"));
                ServerBlocks::Instance().SetCollision(name, std::move(collision));
            }
        }
        if (const char* path = std::getenv("PERLIN_PREVIEW_SCENE")) {
            std::ifstream file(path);
            const std::string text((std::istreambuf_iterator<char>(file)), {});
            ui::Json scene;
            std::string error;
            if (!ui::Json::Parse(text, scene, error) || !scene.IsArray())
                return false;
            Ids ids;
            for (const auto& box : scene.array) {
                const auto* name = box.Find("block");
                const auto* from = box.Find("from");
                const auto* to = box.Find("to");
                if (!name || !name->IsString() || !from || !to || from->array.size() != 3 ||
                    to->array.size() != 3)
                    return false;
                const uint16_t id = name->string == "air" ? 0 : ids.Get(name->string);
                for (int cz = static_cast<int>(std::floor(from->array[2].number / 16));
                     cz <= static_cast<int>(std::floor(to->array[2].number / 16)); ++cz)
                    for (int cx = static_cast<int>(std::floor(from->array[0].number / 16));
                         cx <= static_cast<int>(std::floor(to->array[0].number / 16)); ++cx)
                        level.EnsureChunk(cx, cz);
                for (int y = static_cast<int>(from->array[1].number);
                     y <= static_cast<int>(to->array[1].number); ++y)
                    for (int z = static_cast<int>(from->array[2].number);
                         z <= static_cast<int>(to->array[2].number); ++z)
                        for (int x = static_cast<int>(from->array[0].number);
                             x <= static_cast<int>(to->array[0].number); ++x)
                            level.SetBlock(x, y, z, id);
            }
            return true;
        }
        if (const char* g = std::getenv("PERLIN_PREVIEW_GALLERY"))
            return BuildGallery(level, radius, g);
        Ids ids;
        const uint16_t stone = ids.Get("stone"), dirt = ids.Get("dirt"), grass = ids.Get("grass_block"),
                       sand = ids.Get("sand"), water = ids.Get("water", "\"liquid_depth\":[\"i\",0]"),
                       log = ids.Get("oak_log", "\"pillar_axis\":[\"s\",\"y\"]"),
                       leaves = ids.Get("oak_leaves"), planks = ids.Get("oak_planks"),
                       glow = ids.Get("glowstone"), bedrock = ids.Get("bedrock"),
                       cobble = ids.Get("cobblestone"), snow = ids.Get("snow"), glass = ids.Get("glass"),
                       torch = ids.Get("torch", "\"torch_facing_direction\":[\"s\",\"top\"]"),
                       lava = ids.Get("lava", "\"liquid_depth\":[\"i\",0]");
        constexpr int kSea = 62;
        auto set = [&](int x, int y, int z, uint16_t id) { level.SetBlock(x, y, z, id); };

        for (int cx = -radius; cx < radius; cx++)
            for (int cz = -radius; cz < radius; cz++)
                level.EnsureChunk(cx, cz);

        const int span = radius * 16;
        for (int x = -span; x < span; x++)
            for (int z = -span; z < span; z++) {
                const int h = Height(x, z);
                set(x, -64, z, bedrock);
                for (int y = -63; y <= h; y++) {
                    uint16_t id = stone;
                    if (y == h)
                        id = h <= kSea + 1 ? sand : grass;
                    else if (y >= h - 3)
                        id = h <= kSea + 1 ? sand : dirt;
                    set(x, y, z, id);
                }
                for (int y = h + 1; y <= kSea; y++)
                    set(x, y, z, water);
            }

        for (int x = 8; x <= 30; x++)
            for (int z = -6; z <= 6; z++)
                for (int y = 30; y <= 36; y++)
                    if ((z * z) / 36.0f + ((y - 33) * (y - 33)) / 12.0f < 1.0f)
                        set(x, y, z, 0);
        for (int x = 12; x <= 28; x += 4)
            set(x, 31, 4, torch);
        for (int x = 20; x <= 24; x++)
            for (int z = -3; z <= 0; z++)
                set(x, 29, z, lava);
        for (int y = 30; y <= 33; y++)
            set(20, y, 0, stone);
        set(19, 32, 0, ids.Get("torch", "\"torch_facing_direction\":[\"s\",\"west\"]"));
        set(21, 32, 0, ids.Get("torch", "\"torch_facing_direction\":[\"s\",\"east\"]"));
        set(20, 32, -1, ids.Get("torch", "\"torch_facing_direction\":[\"s\",\"north\"]"));
        set(20, 32, 1, ids.Get("torch", "\"torch_facing_direction\":[\"s\",\"south\"]"));

        for (int x = -span + 4; x < span - 4; x += 3)
            for (int z = -span + 4; z < span - 4; z += 3) {
                const int tx = x + static_cast<int>(Hash(x, z) * 3),
                          tz = z + static_cast<int>(Hash(z, x) * 3);
                const int h = Height(tx, tz);
                if (h <= kSea + 2 || Hash(tx * 7, tz * 13) > 0.16f ||
                    (std::abs(tx) < 12 && std::abs(tz) < 12))
                    continue;
                const int trunk = 4 + static_cast<int>(Hash(tx, tz + 5) * 3);
                for (int y = h + 1; y <= h + trunk; y++)
                    set(tx, y, tz, log);
                for (int dy = trunk - 2; dy <= trunk + 1; dy++) {
                    const int r = dy > trunk - 1 ? 1 : 2;
                    for (int dx = -r; dx <= r; dx++)
                        for (int dz = -r; dz <= r; dz++)
                            if ((std::abs(dx) + std::abs(dz) < r * 2 || Hash(tx + dx, tz + dz) > 0.5f) &&
                                level.GetBlock(tx + dx, h + dy, tz + dz) == 0)
                                set(tx + dx, h + dy, tz + dz, leaves);
                }
            }

        const int padY = Height(0, 0);
        for (int x = -7; x <= 7; x++)
            for (int z = -7; z <= 7; z++) {
                for (int y = padY + 1; y <= padY + 8; y++)
                    set(x, y, z, 0);
                set(x, padY, z, grass);
            }
        for (int x = -4; x <= 4; x++)
            for (int z = -4; z <= 4; z++) {
                set(x, padY, z, planks);
                const bool wall = std::abs(x) == 4 || std::abs(z) == 4;
                for (int y = padY + 1; y <= padY + 3; y++)
                    if (wall)
                        set(x, y, z,
                            (x == 4 && z == 0 && y <= padY + 2)             ? 0
                            : (y == padY + 2 && z == -4 && std::abs(x) < 2) ? glass
                                                                            : cobble);
                set(x, padY + 4, z, planks);
            }
        set(0, padY + 3, 0, glow);
        set(-3, padY + 1, -3, torch);
        if (std::getenv("PERLIN_PREVIEW_GLASSWALL")) {
            const uint16_t purple = ids.Get("purple_stained_glass"), tinted = ids.Get("tinted_glass");
            Log("preview", "glass wall pad y={}", padY);
            for (int x = -3; x <= 3; x++)
                for (int y = padY + 1; y <= padY + 4; y++)
                    set(x, y, -6, x >= 2 ? purple : (x <= -3 ? tinted : glass));
            for (int x = -3; x <= 1; x++)
                set(x, padY + 5, -6, glass);
        }
        return true;
    }
}
