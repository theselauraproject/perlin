#include "client/ui/UiAssets.hpp"
#include "client/resources/ServerPacks.hpp"
#include "client/resources/ServerBlocks.hpp"
#include "client/i18n/Lang.hpp"
#include "client/ui/McFont.hpp"
#include "util/Resources.hpp"

#include "client/renderer/block/BlockLibrary.hpp"
#include "client/resources/TexturePack.hpp"
#include "client/ui/Json.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <sstream>
#include <unordered_set>
#include "util/Log.hpp"

namespace fs = std::filesystem;

namespace perlin::ui {
    namespace {
        bool Slurp(const std::string& path, std::string& out) {
            std::ifstream f(path, std::ios::binary);
            if (!f)
                return false;
            std::stringstream ss;
            ss << f.rdbuf();
            out = ss.str();
            return true;
        }

        std::string Short(const std::string& id) {
            const size_t colon = id.find(':');
            return colon == std::string::npos ? id : id.substr(colon + 1);
        }

        std::string Pretty(const std::string& id) {
            std::string out;
            bool up = true;
            for (const char c : Short(id)) {
                if (c == '_') {
                    out += ' ';
                    up = true;
                } else {
                    out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
                    up = false;
                }
            }
            return out;
        }

        std::vector<std::string> Words(const std::string& s) {
            std::vector<std::string> w;
            std::string cur;
            for (const char c : s) {
                if (c == '_') {
                    w.push_back(cur);
                    cur.clear();
                } else {
                    cur += c;
                }
            }
            w.push_back(cur);
            return w;
        }
    }

    UiAssets& UiAssets::Instance() {
        static UiAssets instance;
        return instance;
    }

    void UiAssets::Configure(const std::string& vanillaRoot, const BlockLibrary* blockLibrary) {
        std::lock_guard lock(mutex);
        root = vanillaRoot;
        blocks = blockLibrary;
        items.clear();
        itemTableLoaded = false;
        langLoaded = false;
        lang.clear();
        sidecars.clear();
    }

    namespace {
        void LoadTtfFallbacks(McFont& font, const std::string& dataDir) {
            namespace fs = std::filesystem;
            std::vector<fs::path> dirs = {fs::path(dataDir) / "fonts", fs::path(dataDir) / "font"};
            for (const std::string& pack : ServerPacks::Instance().Directories())
                dirs.push_back(fs::path(pack) / "font");
            std::error_code ec;
#ifdef _WIN32
            for (const char* root : {"C:/XboxGames/Minecraft for Windows/Content/data",
                                     "C:/XboxGames/Minecraft Preview for Windows/Content/data"})
                dirs.push_back(fs::path(root) / "fonts");
            if (const char* pf = std::getenv("ProgramFiles")) {
                const fs::path apps = fs::path(pf) / "WindowsApps";
                for (fs::directory_iterator it(apps, ec), end; !ec && it != end; it.increment(ec))
                    if (it->path().filename().string().starts_with("Microsoft.MinecraftUWP"))
                        dirs.push_back(it->path() / "data" / "fonts");
            }
#else
            if (const char* home = std::getenv("HOME")) {
                const fs::path versions = fs::path(home) / ".local/share/mcpelauncher/versions";
                for (fs::directory_iterator it(versions, ec), end; !ec && it != end; it.increment(ec))
                    dirs.push_back(it->path() / "assets" / "fonts");
            }
#endif
            std::vector<fs::path> found;
            for (const fs::path& dir : dirs) {
                std::error_code walk;
                for (fs::directory_iterator it(dir, walk), end; !walk && it != end; it.increment(walk)) {
                    std::string ext = it->path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (ext == ".ttf" || ext == ".otf")
                        found.push_back(it->path());
                }
            }
            const auto rank = [](const fs::path& path) {
                std::string n = path.filename().string();
                std::transform(n.begin(), n.end(), n.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (n.contains("mojangles"))
                    return 0;
                if (n.contains("minecraft"))
                    return 1;
                if (n.contains("unifont"))
                    return 3;
                return 2;
            };
            std::stable_sort(found.begin(), found.end(),
                             [&](const fs::path& a, const fs::path& b) { return rank(a) < rank(b); });
            for (const fs::path& path : found)
                if (font.AddTtf(path.string()))
                    Log("font", "fallback font {}", path.string());
        }
    }

    std::string UiAssets::LoadFont(const std::string& dataDir) {
        McFont& font = McFont::Instance();
        struct HexLoader {
            McFont& font;
            const std::string& dataDir;
            ~HexLoader() {
                LoadTtfFallbacks(font, dataDir);
                font.LoadUnihexFile(dataDir + "/fonts/unifont.hex");
            }
        } hexLoader{font, dataDir};
        auto apply = [&](const Texture& t) {
            return t.width > 0 && font.LoadSheet(t.pixels.data(), t.width, t.height);
        };
        Texture t;
        {
            std::lock_guard lock(mutex);
            for (const char* rel : {"font/default8.png", "textures/font/default8.png"})
                if (FindImage(rel, t) && apply(t))
                    return std::string("pack ") + rel;
        }
        for (const char* name : {"/font/default8.png", "/font/ascii.png"})
            if (LoadTexture(dataDir + name, t) && apply(t)) {
                if (std::string(name) == "/font/ascii.png") {
                    Texture accented, nonlatin;
                    const bool haveAccented = LoadTexture(dataDir + "/font/accented.png", accented);
                    const bool haveNonlatin = LoadTexture(dataDir + "/font/nonlatin_european.png", nonlatin);
                    font.LoadVanillaSheets(
                        {haveAccented ? accented.pixels.data() : nullptr, accented.width, accented.height},
                        {haveNonlatin ? nonlatin.pixels.data() : nullptr, nonlatin.width, nonlatin.height});
                }
                return "data dir" + std::string(name);
            }
        font.UseBuiltin();
        return "built-in face";
    }

    bool UiAssets::FindImage(const std::string& rel, Texture& out) const {
        std::string base = rel;
        const bool hasExt = base.size() > 4 && base[base.size() - 4] == '.';
        const char* exts[] = {".png", ".tga"};
        auto& pack = TexturePack::Instance();
        // Bedrock content is authored on case-insensitive filesystems; a pack asking for
        // "textures/ui/White" means vanilla's "white.png" on Linux. Retry with a lowercased
        // basename when the exact path misses.
        std::string lowered;
        {
            const size_t slash = base.find_last_of("/\\");
            const size_t start = slash == std::string::npos ? 0 : slash + 1;
            lowered = base;
            bool changed = false;
            for (size_t i = start; i < lowered.size(); i++) {
                const char folded = static_cast<char>(std::tolower(static_cast<unsigned char>(lowered[i])));
                changed = changed || folded != lowered[i];
                lowered[i] = folded;
            }
            if (!changed)
                lowered.clear();
        }
        const auto tryBase = [&](const std::string& name) -> bool {
            for (const std::string& dir : ServerPacks::Instance().Directories()) {
                if (hasExt && LoadTexture(dir + "/" + name, out))
                    return true;
                if (!hasExt)
                    for (const char* e : exts)
                        if (LoadTexture(dir + "/" + name + e, out))
                            return true;
            }
            if (pack.Active()) {
                if (hasExt && pack.Load(name, out))
                    return true;
                for (const char* e : exts)
                    if (!hasExt && pack.Load(name + e, out))
                        return true;
            }
            if (root.empty())
                return false;
            if (hasExt)
                return LoadTexture(root + "/" + name, out);
            for (const char* e : exts)
                if (LoadTexture(root + "/" + name + e, out))
                    return true;
            return false;
        };
        if (tryBase(base))
            return true;
        if (!lowered.empty())
            return tryBase(lowered);
        return false;
    }

    bool UiAssets::Image(const std::string& rel, Texture& out) {
        std::lock_guard lock(mutex);
        return FindImage(rel, out);
    }

    bool UiAssets::ImageSize(const std::string& rel, int& w, int& h) {
        std::lock_guard lock(mutex);
        if (const auto it = sizeCache.find(rel); it != sizeCache.end()) {
            w = it->second.first;
            h = it->second.second;
            return w > 0 && h > 0;
        }
        std::string base = rel;
        if (base.size() < 5 || base.substr(base.size() - 4) != ".png")
            base += ".png";
        const bool layered =
            TexturePack::Instance().Active() || !ServerPacks::Instance().Directories().empty();
        if (!layered) {
            std::ifstream f(root + "/" + base, std::ios::binary);
            unsigned char b[24];
            if (f.read(reinterpret_cast<char*>(b), 24) && b[1] == 'P') {
                w = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19];
                h = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
                sizeCache[rel] = {w, h};
                return w > 0 && h > 0;
            }
        }
        Texture t;
        if (!FindImage(rel, t))
            return false;
        w = t.width;
        h = t.height;
        sizeCache[rel] = {w, h};
        return true;
    }

    TextureSidecar UiAssets::Sidecar(const std::string& rel) {
        std::lock_guard lock(mutex);
        // sidecar from the pack winning the image: server packs, resource pack, vanilla
        const uint64_t generation = (static_cast<uint64_t>(ServerPacks::Instance().Generation()) << 32) ^
                                    TexturePack::Instance().Generation();
        if (generation != sidecarGeneration) {
            sidecars.clear();
            sidecarGeneration = generation;
        }
        auto cached = sidecars.find(rel);
        if (cached != sidecars.end())
            return cached->second;
        TextureSidecar out;
        std::string base = rel;
        if (base.size() > 4 && base[base.size() - 4] == '.')
            base.resize(base.size() - 4);
        std::string text;
        bool found = false;
        for (const std::string& dir : ServerPacks::Instance().Directories())
            if (Slurp(dir + "/" + base + ".json", text)) {
                found = true;
                break;
            }
        if (!found && TexturePack::Instance().Active()) {
            const std::vector<std::string> layers = TexturePack::Instance().ReadTextLayers(base + ".json");
            if (!layers.empty()) {
                text = layers.front(); // same winner as ExtractPackUi
                found = true;
            }
        }
        if (found || Slurp(root + "/" + base + ".json", text)) {
            Json json;
            std::string error;
            if (Json::Parse(text, json, error) && json.IsObject()) {
                out.present = true;
                if (const Json* n = json.Find("nineslice_size")) {
                    out.nine = true;
                    if (n->IsNumber()) {
                        for (float& v : out.slice)
                            v = static_cast<float>(n->number);
                    } else if (n->IsArray() && n->array.size() >= 4) {
                        for (int i = 0; i < 4; i++)
                            out.slice[i] = static_cast<float>(n->array[i].NumberOr(0));
                    }
                }
                if (const Json* b = json.Find("base_size"); b && b->IsArray() && b->array.size() >= 2) {
                    out.baseW = static_cast<int>(b->array[0].NumberOr(0));
                    out.baseH = static_cast<int>(b->array[1].NumberOr(0));
                }
            }
        }
        sidecars[rel] = out;
        return out;
    }

    void UiAssets::LoadLang() {}

    std::string UiAssets::Localize(const std::string& key) {
        return i18n::Lang::Get().Tr(key);
    }

    std::string UiAssets::ItemName(const std::string& identifier, int aux) {
        const std::string name = Short(identifier);
        const bool splash = name == "splash_potion", linger = name == "lingering_potion";
        if (name == "potion" || splash || linger) {
            static const char* kBase[] = {
                "emptyPotion",    "mundane",     "mundane",        "thick",          "awkward",
                "nightVision",    "nightVision", "invisibility",   "invisibility",   "jump",
                "jump",           "jump",        "fireResistance", "fireResistance", "moveSpeed",
                "moveSpeed",      "moveSpeed",   "moveSlowdown",   "moveSlowdown",   "waterBreathing",
                "waterBreathing", "heal",        "heal",           "harm",           "harm",
                "poison",         "poison",      "poison",         "regeneration",   "regeneration",
                "regeneration",   "damageBoost", "damageBoost",    "damageBoost",    "weakness",
                "weakness",       "wither",      "turtleMaster",   "turtleMaster",   "turtleMaster",
                "slowFalling",    "slowFalling", "moveSlowdown",   "windCharged",    "weaving",
                "oozing",         "infested"};
            if (aux >= 0 && aux < static_cast<int>(sizeof(kBase) / sizeof(kBase[0]))) {
                std::string key = std::string("potion.") + kBase[aux];
                if (aux == 2)
                    key += ".extended";
                key += splash ? ".splash.name" : linger ? ".linger.name" : ".name";
                std::string text;
                if (i18n::Lang::Get().Find(key, text))
                    return text;
            }
        }
        return ItemName(identifier);
    }

    std::string UiAssets::ItemName(const std::string& identifier) {
        std::lock_guard lock(mutex);
        const std::string name = Short(identifier);
        std::string text;
        for (const char* prefix : {"item.", "tile."})
            if (i18n::Lang::Get().Find(std::string(prefix) + identifier + ".name", text))
                return text;
        CustomItemDef custom;
        if (ServerBlocks::Instance().Item(identifier, custom) && !custom.displayName.empty()) {
            if (i18n::Lang::Get().Find(custom.displayName, text))
                return text;
            return custom.displayName;
        }
        for (const char* prefix : {"item.", "tile."})
            if (i18n::Lang::Get().Find(std::string(prefix) + name + ".name", text))
                return text;
        static const std::unordered_map<std::string, std::string> legacy = [] {
            std::unordered_map<std::string, std::string> out;
            std::string data;
            if (resources::Read("item_names.map", data)) {
                size_t pos = 0;
                while (pos < data.size()) {
                    size_t end = data.find('\n', pos);
                    if (end == std::string::npos)
                        end = data.size();
                    const std::string line = data.substr(pos, end - pos);
                    pos = end + 1;
                    if (const size_t eq = line.find('='); eq != std::string::npos)
                        out[line.substr(0, eq)] = line.substr(eq + 1);
                }
            }
            return out;
        }();
        if (const auto it = legacy.find(name); it != legacy.end() && i18n::Lang::Get().Find(it->second, text))
            return text;
        return Pretty(identifier);
    }

    void UiAssets::LoadItemTextureTable() {
        const uint32_t generation = ServerPacks::Instance().Generation();
        if (itemTableLoaded && generation == itemTableGeneration)
            return;
        if (itemTableLoaded) {
            items.clear();
            itemTextures.clear();
            stemIndexBuilt = false;
        }
        itemTableLoaded = true;
        itemTableGeneration = generation;
        std::vector<std::string> files = {root + "/textures/item_texture.json"};
        for (const std::string& dir : ServerPacks::Instance().Directories())
            files.push_back(dir + "/textures/item_texture.json");
        for (const std::string& file : files) {
            std::string text;
            if (!Slurp(file, text))
                continue;
            Json json;
            std::string error;
            if (!Json::Parse(text, json, error) || !json.IsObject())
                continue;
            const Json* data = json.Find("texture_data");
            if (!data || !data->IsObject())
                continue;
            for (const auto& entry : data->object) {
                const Json* textures = entry.second.Find("textures");
                if (!textures)
                    continue;
                std::vector<std::string> paths;
                const auto add = [&](const Json& v) {
                    if (v.IsString())
                        paths.push_back(v.string);
                    else if (v.IsObject())
                        if (const Json* p = v.Find("path"); p && p->IsString())
                            paths.push_back(p->string);
                };
                if (textures->IsArray())
                    for (const Json& v : textures->array)
                        add(v);
                else
                    add(*textures);
                if (!paths.empty())
                    itemTextures[entry.first] = std::move(paths);
            }
        }
    }

    bool UiAssets::FlatSprite(const std::string& name, int aux, ItemSprite& out, int64_t dyeColor,
                              const std::string& iconKey) {
        LoadItemTextureTable();
        std::vector<std::string> candidates;
        if (!iconKey.empty())
            candidates.push_back(iconKey);
        candidates.push_back(name);
        const auto replaceWord = [&](const std::string& from, const std::string& to) {
            std::string s;
            for (const std::string& w : Words(name)) {
                if (!s.empty())
                    s += '_';
                s += w == from ? to : w;
            }
            return s;
        };
        candidates.push_back(name + "_standby");
        candidates.push_back(replaceWord("golden", "gold"));
        candidates.push_back(replaceWord("wooden", "wood"));
        {
            static const std::pair<const char*, const char*> kAlias[] = {
                {"beef", "beef_raw"},
                {"porkchop", "porkchop_raw"},
                {"chicken", "chicken_raw"},
                {"mutton", "mutton_raw"},
                {"cod", "fish"},
                {"cooked_cod", "cooked_fish"},
                {"tropical_fish", "clownfish"},
                {"enchanted_golden_apple", "apple_golden"},
                {"melon_slice", "melon"},
                {"potion", "potion_bottle_drinkable"},
                {"glass_bottle", "potion_bottle_empty"},
                {"milk_bucket", "bucket"},
                {"totem_of_undying", "totem"},
                {"fire_charge", "fireball"},
                {"firework_rocket", "fireworks"},
                {"splash_potion", "potion_bottle_splash"},
                {"lingering_potion", "potion_bottle_lingering"},
                {"oak_sign", "sign"},
                {"popped_chorus_fruit", "chorus_fruit_popped"},
                {"sugar_cane", "reeds"},
                {"book", "book_normal"},
                {"writable_book", "book_writable"},
                {"written_book", "book_written"},
                {"enchanted_book", "book_enchanted"},
                {"compass", "compass_item"},
                {"clock", "clock_item"},
                {"recovery_compass", "recovery_compass_item"},
                {"turtle_scute", "turtle_shell_piece"}};
            if (name.size() > 4 && name.compare(name.size() - 4, 4, "_dye") == 0) {
                std::string color = name.substr(0, name.size() - 4);
                if (color == "light_gray")
                    color = "silver";
                const bool fresh =
                    color == "black" || color == "brown" || color == "blue" || color == "white";
                candidates.push_back("dye_powder_" + color + (fresh ? "_new" : ""));
            }
            if (name == "glow_ink_sac")
                candidates.push_back("dye_powder_glow");
            if (name.starts_with("music_disc_"))
                candidates.push_back("record_" + name.substr(11));
            for (const auto& [from, to] : kAlias)
                if (name == from)
                    candidates.push_back(to);
        }
        {
            const auto w = Words(name);
            if (w.size() >= 2) {
                std::string swapped = w.back();
                for (size_t i = 0; i + 1 < w.size(); i++)
                    swapped += "_" + w[i];
                candidates.push_back(swapped);
                std::string rotated = w[1];
                for (size_t i = 2; i < w.size(); i++)
                    rotated += "_" + w[i];
                candidates.push_back(rotated + "_" + w[0]);
            }
        }
        if (name.size() > 5 && name.compare(name.size() - 5, 5, "_door") == 0) {
            const std::string kind = name.substr(0, name.size() - 5);
            candidates.push_back("door_" + (kind == "oak" ? std::string("wood") : kind));
        }
        for (const std::string& c : candidates) {
            std::string path;
            auto it = itemTextures.find(c);
            if (it != itemTextures.end() && it->second.size() == 1) {
                path = it->second[0];
            } else if (it != itemTextures.end() && c != name) {
                const auto& paths = it->second;
                path = paths[static_cast<size_t>(std::clamp(aux, 0, static_cast<int>(paths.size()) - 1))];
            } else {
                path = "textures/items/" + c;
            }
            Texture t;
            if (!FindImage(path, t) && !FindImage("textures/items/spawn_eggs/" + c, t)) {
                if (it == itemTextures.end() ||
                    !FindImage(it->second[static_cast<size_t>(
                                   std::clamp(aux, 0, static_cast<int>(it->second.size()) - 1))],
                               t))
                    continue;
            }
            // animated item textures (compass, clock, ingots) are frames stacked in one tall strip: showing the
            // whole strip stretches it into a single huge sprite, so the icon is its first frame
            if (t.width > 0 && t.height > t.width && t.height % t.width == 0 &&
                t.pixels.size() >= static_cast<size_t>(t.width) * t.width * 4) {
                t.pixels.resize(static_cast<size_t>(t.width) * t.width * 4);
                t.height = t.width;
            }
            if (name == "shield" && t.width >= 64 && t.height >= 64) {
                const int scale = t.width / 64;
                std::vector<uint8_t> icon(16 * 16 * 4, 0);
                const int fw = 8, fh = 14, ox = 4, oy = 1;
                for (int y = 0; y < fh; y++)
                    for (int x = 0; x < fw; x++) {
                        const int sx = (1 + x * 12 / fw) * scale, sy = (1 + y * 22 / fh) * scale;
                        const uint8_t* src = &t.pixels[(static_cast<size_t>(sy) * t.width + sx) * 4];
                        uint8_t* dst = &icon[(static_cast<size_t>(oy + y) * 16 + ox + x) * 4];
                        const bool edge = x == 0 || y == 0 || x == fw - 1 || y == fh - 1;
                        for (int c = 0; c < 3; c++)
                            dst[c] = edge ? static_cast<uint8_t>(src[c] * 6 / 10) : src[c];
                        dst[3] = 255;
                    }
                out.ok = true;
                out.block = false;
                out.w = out.h = 16;
                out.rgba = std::move(icon);
                out.handheld = false;
                return true;
            }
            if (name.starts_with("leather_") && name != "leather_horse_armor") {
                const uint32_t rgb = dyeColor >= 0 ? static_cast<uint32_t>(dyeColor) & 0xFFFFFFu : 0xA06540u;
                const float cr = ((rgb >> 16) & 255) / 255.0f, cg = ((rgb >> 8) & 255) / 255.0f,
                            cb = (rgb & 255) / 255.0f;
                bool anyMask = false;
                for (size_t i = 3; i < t.pixels.size(); i += 4)
                    if (t.pixels[i] != 0 && t.pixels[i] < 16)
                        anyMask = true;
                for (size_t i = 0; i + 3 < t.pixels.size(); i += 4) {
                    uint8_t* px = &t.pixels[i];
                    const bool mask = px[3] != 0 && px[3] < 16;
                    if (mask)
                        px[3] = 255;
                    if (dyeColor < 0 && anyMask)
                        continue;
                    if (px[3] == 0 || (anyMask && !mask))
                        continue;
                    float l = (px[0] * 0.299f + px[1] * 0.587f + px[2] * 0.114f) / 255.0f;
                    if (mask)
                        l = std::min(1.0f, l / 0.42f);
                    px[0] = static_cast<uint8_t>(std::clamp(l * cr * 255.0f, 0.0f, 255.0f));
                    px[1] = static_cast<uint8_t>(std::clamp(l * cg * 255.0f, 0.0f, 255.0f));
                    px[2] = static_cast<uint8_t>(std::clamp(l * cb * 255.0f, 0.0f, 255.0f));
                }
            } else if (name.starts_with("leather_")) {
                for (size_t i = 3; i < t.pixels.size(); i += 4)
                    if (t.pixels[i] != 0 && t.pixels[i] < 16)
                        t.pixels[i] = 255;
            }
            out.ok = true;
            out.block = false;
            out.w = t.width;
            out.h = t.height;
            out.rgba = std::move(t.pixels);
            const bool tool = name.contains("sword") || name.contains("pickaxe") || name.contains("_axe") ||
                              name.contains("shovel") || name.contains("hoe") || name == "stick" ||
                              name.starts_with("bow") || name.starts_with("crossbow") ||
                              name.contains("rod") || name == "trident" || name == "mace" ||
                              name == "shears" || name == "carrot_on_a_stick" || name == "blaze_rod" ||
                              name == "bone";
            out.handheld = tool;
            return true;
        }
        return false;
    }

    namespace {
        void Multiply(std::vector<uint8_t>& rgba, float r, float g, float b) {
            for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
                rgba[i] = static_cast<uint8_t>(std::clamp(rgba[i] * r, 0.0f, 255.0f));
                rgba[i + 1] = static_cast<uint8_t>(std::clamp(rgba[i + 1] * g, 0.0f, 255.0f));
                rgba[i + 2] = static_cast<uint8_t>(std::clamp(rgba[i + 2] * b, 0.0f, 255.0f));
            }
        }
    }

    void UiAssets::BuildStemIndex() {
        stemIndexBuilt = true;
        stemIndex.clear();
        namespace fs = std::filesystem;
        for (const std::string& dir : ServerPacks::Instance().Directories()) {
            std::error_code ec;
            const fs::path base = fs::path(dir) / "textures";
            for (const char* sub : {"items", "blocks"}) {
                for (fs::recursive_directory_iterator
                         it(base / sub, fs::directory_options::skip_permission_denied, ec),
                     end;
                     !ec && it != end; it.increment(ec)) {
                    if (!it->is_regular_file(ec))
                        continue;
                    const std::string name = it->path().filename().string();
                    constexpr std::string_view kSet = ".texture_set.json";
                    std::string stem;
                    size_t extSize = 0;
                    if (name.size() > kSet.size() &&
                        name.compare(name.size() - kSet.size(), kSet.size(), kSet) == 0) {
                        stem = name.substr(0, name.size() - kSet.size());
                        extSize = kSet.size();
                    } else {
                        const std::string ext = it->path().extension().string();
                        if (ext != ".png" && ext != ".tga")
                            continue;
                        stem = it->path().stem().string();
                        extSize = ext.size();
                    }
                    std::string rel = fs::relative(it->path(), dir, ec).generic_string();
                    rel.resize(rel.size() - extSize);
                    stemIndex.emplace(std::move(stem), std::move(rel));
                }
            }
        }
    }

    std::string UiAssets::FindByStem(const std::string& stem) {
        if (!stemIndexBuilt)
            BuildStemIndex();
        const auto it = stemIndex.find(stem);
        return it == stemIndex.end() ? std::string() : it->second;
    }

    namespace {
        void MakeGenericIcon(const std::string& name, ItemSprite& out) {
            uint32_t h = 2166136261u;
            for (char c : name)
                h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
            const float hue = static_cast<float>(h % 360) / 60.0f;
            const auto channel = [&](float n) {
                const float k = std::fmod(n + hue, 6.0f);
                return 0.35f + 0.45f * (1.0f - std::max(0.0f, std::min({k, 4.0f - k, 1.0f})));
            };
            const float r = channel(5.0f), g = channel(3.0f), b = channel(1.0f);
            out.w = out.h = 16;
            out.rgba.assign(16 * 16 * 4, 0);
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++) {
                    const int dx = std::abs(x * 2 - 15), dy = std::abs(y * 2 - 15);
                    const int d = (dx + dy) / 2;
                    if (d > 7)
                        continue;
                    const bool edge = d == 7;
                    const float shade =
                        edge ? 0.45f : (y < 8 ? 1.15f : 0.85f) - 0.02f * static_cast<float>(d);
                    uint8_t* px = &out.rgba[static_cast<size_t>(y * 16 + x) * 4];
                    px[0] = static_cast<uint8_t>(std::min(1.0f, r * shade) * 255.0f);
                    px[1] = static_cast<uint8_t>(std::min(1.0f, g * shade) * 255.0f);
                    px[2] = static_cast<uint8_t>(std::min(1.0f, b * shade) * 255.0f);
                    px[3] = 255;
                }
            out.ok = true;
            out.generic = true;
        }
    }

    const ItemSprite* UiAssets::Item(const std::string& key) {
        std::lock_guard lock(mutex);
        if (itemTableLoaded && itemTableGeneration != ServerPacks::Instance().Generation()) {
            items.clear();
            itemTextures.clear();
            itemTableLoaded = false;
            stemIndexBuilt = false;
        }
        auto it = items.find(key);
        if (it != items.end()) {
            // a server block's icon made before its textures were built: make it again once they are
            if (it->second.generic && blocks && blocks->Ready() && unbuiltIcons.contains(key) &&
                blocks->LookByName(key.substr(0, key.find_first_of(";@"))) != nullptr) {
                unbuiltIcons.erase(key);
                items.erase(it);
                it = items.end();
            }
        }
        if (it != items.end())
            return it->second.ok ? &it->second : nullptr;
        ItemSprite sprite;
        const size_t semi = key.rfind(';');
        const std::string id = semi == std::string::npos ? key : key.substr(0, semi);
        const int aux = semi == std::string::npos ? 0 : std::atoi(key.c_str() + semi + 1);
        const size_t atAt = key.find('@');
        const int64_t dye = atAt == std::string::npos
                                ? -1
                                : static_cast<int64_t>(std::strtoul(key.c_str() + atAt + 1, nullptr, 10));
        std::string name = Short(id);
        if (name == "tallgrass")
            name = "short_grass";

        CustomItemDef custom;
        const bool customItem = ServerBlocks::Instance().Item(id, custom);
        std::string iconKey = customItem ? custom.icon : std::string();
        if (iconKey.empty() && id.contains(':') && !id.starts_with("minecraft:"))
            iconKey = id;
        bool flat = FlatSprite(name, aux, sprite, dye, iconKey);
        if (!flat) {
            std::vector<std::string> stems;
            if (customItem && !custom.icon.empty())
                stems.push_back(custom.icon);
            stems.push_back(name);
            if (customItem && !custom.icon.empty()) {
                const size_t slash = custom.icon.find_last_of('/');
                if (slash != std::string::npos)
                    stems.push_back(custom.icon.substr(slash + 1));
            }
            for (const std::string& stem : stems) {
                const std::string rel = FindByStem(stem);
                Texture t;
                if (!rel.empty() && FindImage(rel, t) && t.width > 0) {
                    sprite.ok = true;
                    sprite.w = t.width;
                    sprite.h = t.height;
                    sprite.rgba = std::move(t.pixels);
                    flat = true;
                    break;
                }
            }
        }
        if (!flat && blocks && blocks->Ready()) {
            const BlockAppearance* a = id.contains(':') ? blocks->LookByName(id) : nullptr;
            if (!a)
                a = blocks->LookByName("minecraft:" + name);
            if (a) {
                const int px = BlockLibrary::TilePx();
                sprite.tile = px;
                sprite.fromBlock = true;
                const bool cube =
                    a->shape == BlockAppearance::Shape::Cube || a->shape == BlockAppearance::Shape::Wall;
                std::vector<uint8_t> top, side;
                if (blocks->TilePixels(a->top, top) && blocks->TilePixels(a->side, side)) {
                    float tr = a->tintR, tg = a->tintG, tb = a->tintB;
                    const bool unsetTint = tr >= 0.999f && tg >= 0.999f && tb >= 0.999f;
                    bool tintTop = a->tintTop, tintSide = a->tintSide;
                    if (unsetTint) {
                        auto set = [&](uint32_t rgb) {
                            tr = ((rgb >> 16) & 255) / 255.0f;
                            tg = ((rgb >> 8) & 255) / 255.0f;
                            tb = (rgb & 255) / 255.0f;
                        };
                        if (name.contains("spruce_leaves")) {
                            set(0x619961);
                            tintTop = tintSide = true;
                        } else if (name.contains("birch_leaves")) {
                            set(0x80A755);
                            tintTop = tintSide = true;
                        } else if (name.contains("_leaves") && !name.contains("cherry") &&
                                   !name.contains("azalea")) {
                            set(0x48B518);
                            tintTop = tintSide = true;
                        } else if (a->overlayTile != 0) {
                            set(0x79C05A);
                            tintTop = true;
                        } else if (name == "short_grass" || name == "tallgrass" || name == "fern" ||
                                   name == "large_fern" || name == "tall_grass" || name == "grass") {
                            set(0x79C05A);
                            tintTop = tintSide = true;
                        } else if (name == "vine" || name == "lily_pad") {
                            set(0x48B518);
                            tintTop = tintSide = true;
                        }
                    }
                    if (tintTop)
                        Multiply(top, tr, tg, tb);
                    if (a->overlayTile != 0) {
                        std::vector<uint8_t> ov;
                        if (blocks->TilePixels(a->overlayTile, ov) && ov.size() == side.size()) {
                            if (unsetTint)
                                Multiply(ov, tr, tg, tb);
                            else
                                Multiply(ov, a->tintR, a->tintG, a->tintB);
                            for (size_t i = 0; i + 3 < ov.size(); i += 4) {
                                const float al = ov[i + 3] / 255.0f;
                                if (al <= 0)
                                    continue;
                                for (int c = 0; c < 3; c++)
                                    side[i + c] =
                                        static_cast<uint8_t>(side[i + c] * (1 - al) + ov[i + c] * al);
                                side[i + 3] = 255;
                            }
                        }
                        tintSide = false;
                    }
                    if (!a->carriedTex.empty()) {
                        Texture carried;
                        if (FindImage(a->carriedTex, carried) && carried.width == px &&
                            carried.height == px) {
                            side = carried.pixels;
                            if (a->hasCarriedRGB)
                                Multiply(side, a->carriedR, a->carriedG, a->carriedB);
                        }
                    } else if (tintSide) {
                        Multiply(side, tr, tg, tb);
                    }
                    if (cube) {
                        sprite.ok = true;
                        sprite.block = true;
                        sprite.top = std::move(top);
                        sprite.left = side;
                        sprite.right = std::move(side);
                    } else {
                        sprite.ok = true;
                        sprite.block = false;
                        sprite.w = sprite.h = px;
                        sprite.rgba =
                            a->shape == BlockAppearance::Shape::Flat ? std::move(top) : std::move(side);
                        if (tintSide || tintTop)
                            Multiply(sprite.rgba, tr, tg, tb);
                    }
                }
            }
        }
        if (!sprite.ok) {
            static std::unordered_set<std::string> reported;
            if (reported.insert(id).second)
                Log("items", "no texture found for {}{}, using a generic icon", id,
                    (customItem ? " (icon '" + custom.icon + "')" : std::string()));
            MakeGenericIcon(id, sprite);
            if (id.contains(':') && !id.starts_with("minecraft:"))
                unbuiltIcons.insert(key);
        }
        auto inserted = items.emplace(key, std::move(sprite)).first;
        return &inserted->second;
    }

    void UiAssets::ClearItemCache() {
        std::lock_guard lock(mutex);
        items.clear();
        itemTextures.clear();
        sidecars.clear();
        sizeCache.clear();
        stemIndex.clear();
        stemIndexBuilt = false;
        itemTableLoaded = false;
        generation++;
    }

    void UiAssets::ClearLevelCache() {
        ClearItemCache();
        std::lock_guard lock(mutex);
        // featured server logos belong to the menu, whose rows survive level exits
        std::erase_if(heads, [](const auto& entry) { return !entry.first.starts_with("fs:"); });
        lang.clear();
        langLoaded = false;
    }

    bool UiAssets::HasHead(const std::string& key) {
        std::lock_guard lock(mutex);
        return heads.contains(key);
    }
    void UiAssets::SetHead(const std::string& key, Texture head) {
        std::lock_guard lock(mutex);
        heads[key] = std::move(head);
    }
    void UiAssets::EraseHead(const std::string& key) {
        std::lock_guard lock(mutex);
        heads.erase(key);
    }
    bool UiAssets::Head(const std::string& key, Texture& out) {
        std::lock_guard lock(mutex);
        const auto it = heads.find(key);
        if (it == heads.end())
            return false;
        out = it->second;
        return true;
    }

    namespace {
        void CenterIconPixels(Texture& t) {
            if (t.width <= 0 || t.height <= 0 ||
                t.pixels.size() < static_cast<size_t>(t.width) * t.height * 4)
                return;
            int x0 = t.width, x1 = -1, y0 = t.height, y1 = -1;
            for (int y = 0; y < t.height; y++)
                for (int x = 0; x < t.width; x++)
                    if (t.pixels[(static_cast<size_t>(y) * t.width + x) * 4 + 3] > 8) {
                        x0 = std::min(x0, x);
                        x1 = std::max(x1, x);
                        y0 = std::min(y0, y);
                        y1 = std::max(y1, y);
                    }
            if (x1 < 0)
                return;
            const int dx = (t.width - 1 - x1 - x0) / 2, dy = (t.height - 1 - y1 - y0) / 2;
            if (dx == 0 && dy == 0)
                return;
            std::vector<uint8_t> shifted(t.pixels.size(), 0);
            for (int y = 0; y < t.height; y++)
                for (int x = 0; x < t.width; x++) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= t.width || ny >= t.height)
                        continue;
                    std::copy_n(&t.pixels[(static_cast<size_t>(y) * t.width + x) * 4], 4,
                                &shifted[(static_cast<size_t>(ny) * t.width + nx) * 4]);
                }
            t.pixels = std::move(shifted);
        }
    }

    bool UiAssets::ItemIcon(const std::string& key, Texture& out) {
        const ItemSprite* s = Item(key);
        if (!s || s->fromBlock || s->generic) {
            const size_t hashAt = key.find('#');
            if (hashAt != std::string::npos && blocks && blocks->Ready()) {
                const size_t semi = key.rfind(';', hashAt);
                std::string name =
                    Short(semi == std::string::npos ? key.substr(0, hashAt) : key.substr(0, semi));
                if (name == "tallgrass")
                    name = "short_grass";
                const int32_t hash =
                    static_cast<int32_t>(std::strtoul(key.c_str() + hashAt + 1, nullptr, 10));
                if (BakeModelIcon(name, hash, 64, out)) {
                    CenterIconPixels(out);
                    return true;
                }
            }
        }
        if (!s) {
            out.width = out.height = 16;
            out.pixels.assign(16 * 16 * 4, 255);
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++) {
                    const bool pink = ((x / 8) + (y / 8)) % 2 == 0;
                    uint8_t* p = &out.pixels[(y * 16 + x) * 4];
                    p[0] = pink ? 248 : 0;
                    p[1] = 0;
                    p[2] = pink ? 248 : 0;
                    p[3] = 255;
                }
            return true;
        }
        if (s->block) {
            BakeCubeIcon(*s, 64, out);
            CenterIconPixels(out);
            return true;
        }
        out.width = s->w;
        out.height = s->h;
        out.pixels = s->rgba;
        return true;
    }

    bool UiAssets::BakeModelIcon(const std::string& name, int32_t hash, int size, Texture& out) {
        const BlockModels& bm = blocks->Models();
        if (!bm.Loaded())
            return false;
        const ModelEntry* entry = bm.Find(hash);
        if (!entry)
            return false;
        std::vector<const ModelQuad*> quads;
        if (entry->direct) {
            for (const ModelQuad& q : entry->quads)
                quads.push_back(&q);
        } else {
            for (const auto& group : entry->groups)
                if (!group.empty())
                    for (const ModelQuad& q : bm.Variant(group[0].variant))
                        quads.push_back(&q);
        }
        if (quads.empty())
            return false;
        const BlockAppearance* app = blocks->LookByName("minecraft:" + name);
        float tr = 1, tg = 1, tb = 1;
        bool tinted = false;
        {
            auto set = [&](uint32_t rgb) {
                tr = ((rgb >> 16) & 255) / 255.0f;
                tg = ((rgb >> 8) & 255) / 255.0f;
                tb = (rgb & 255) / 255.0f;
                tinted = true;
            };
            if (name.contains("spruce_leaves"))
                set(0x619961);
            else if (name.contains("birch_leaves"))
                set(0x80A755);
            else if (name.contains("_leaves") && !name.contains("cherry") && !name.contains("azalea") &&
                     !name.contains("pale"))
                set(0x48B518);
            else if (name == "short_grass" || name == "tallgrass" || name == "fern" || name == "large_fern" ||
                     name == "tall_grass" || name == "grass_block" || name == "grass" ||
                     name == "grass_path" || name == "sugar_cane" || name == "reeds")
                set(0x79C05A);
            else if (name == "vine" || name == "lily_pad")
                set(0x48B518);
            else if (app && (app->tintTop || app->tintSide)) {
                tr = app->tintR;
                tg = app->tintG;
                tb = app->tintB;
                tinted = true;
            }
        }
        const int ss = 2;
        const int big = size * ss;
        std::vector<uint8_t> color(static_cast<size_t>(big) * big * 4, 0);
        std::vector<float> depth(static_cast<size_t>(big) * big, -1e9f);
        std::unordered_map<int, std::vector<uint8_t>> tiles;
        const float ry = 225.0f * 3.14159265f / 180.0f, rx = 30.0f * 3.14159265f / 180.0f;
        const auto rotate = [&](const float* p, float* r) {
            const float x1 = p[0] * std::cos(ry) + p[2] * std::sin(ry);
            const float z1 = -p[0] * std::sin(ry) + p[2] * std::cos(ry);
            r[0] = x1;
            r[1] = p[1] * std::cos(rx) - z1 * std::sin(rx);
            r[2] = p[1] * std::sin(rx) + z1 * std::cos(rx);
        };
        const float scale = 10.0f * static_cast<float>(big) / 16.0f;
        const float centre = static_cast<float>(big) * 0.5f;
        const float l0[3] = {0.2f / 1.237f, 1.0f / 1.237f, -0.7f / 1.237f};
        const float l1[3] = {-0.2f / 1.237f, 1.0f / 1.237f, 0.7f / 1.237f};
        int ymin = big, ymax = -1, xmin = big, xmax = -1;
        struct Projected {
            float sx[4], sy[4], sz[4];
        };
        std::vector<Projected> projected(quads.size());
        for (size_t qi = 0; qi < quads.size(); qi++) {
            const ModelQuad& q = *quads[qi];
            for (int i = 0; i < 4; i++) {
                const float p[3] = {q.pos[i][0] - 0.5f, q.pos[i][1] - 0.5f, q.pos[i][2] - 0.5f};
                float r[3];
                rotate(p, r);
                projected[qi].sx[i] = centre + r[0] * scale;
                projected[qi].sy[i] = centre - r[1] * scale;
                projected[qi].sz[i] = r[2];
            }
        }
        for (size_t qi = 0; qi < quads.size(); qi++) {
            const ModelQuad& q = *quads[qi];
            const Projected& P = projected[qi];
            int tile = q.tile ? q.tile : ((q.tex != 0xFFFF && q.tex < bm.tiles.size()) ? bm.tiles[q.tex] : 0);
            if (!tile)
                continue;
            auto found = tiles.find(tile);
            if (found == tiles.end()) {
                std::vector<uint8_t> px;
                if (!blocks->TilePixels(tile, px))
                    px.clear();
                found = tiles.emplace(tile, std::move(px)).first;
            }
            const std::vector<uint8_t>& tex = found->second;
            const int tpx = BlockLibrary::TilePx();
            if (tex.size() < static_cast<size_t>(tpx) * tpx * 4)
                continue;
            float e1[3], e2[3];
            for (int c = 0; c < 3; c++) {
                e1[c] = q.pos[1][c] - q.pos[0][c];
                e2[c] = q.pos[3][c] - q.pos[0][c];
            }
            float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                          e1[0] * e2[1] - e1[1] * e2[0]};
            const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (nl < 1e-6f)
                continue;
            for (float& c : n)
                c /= nl;
            float rn[3];
            rotate(n, rn);
            if (rn[2] < 0)
                for (float& c : rn)
                    c = -c;
            const auto dot = [&](const float* l) {
                return std::max(0.0f, rn[0] * l[0] + rn[1] * l[1] + rn[2] * l[2]);
            };
            const float shade = q.shade ? std::min(1.0f, (dot(l0) + dot(l1)) * 0.6f + 0.4f) : 1.0f;
            const bool useTint = tinted && q.tint >= 0 && !q.tile;
            const int tris[2][3] = {{0, 1, 2}, {0, 2, 3}};
            for (const auto& t : tris) {
                const float x0 = P.sx[t[0]], y0 = P.sy[t[0]], x1 = P.sx[t[1]], y1 = P.sy[t[1]],
                            x2 = P.sx[t[2]], y2 = P.sy[t[2]];
                const float det = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
                if (std::fabs(det) < 1e-6f)
                    continue;
                const int minX = std::max(0, static_cast<int>(std::floor(std::min({x0, x1, x2}))));
                const int maxX = std::min(big - 1, static_cast<int>(std::ceil(std::max({x0, x1, x2}))));
                const int minY = std::max(0, static_cast<int>(std::floor(std::min({y0, y1, y2}))));
                const int maxY = std::min(big - 1, static_cast<int>(std::ceil(std::max({y0, y1, y2}))));
                for (int y = minY; y <= maxY; y++)
                    for (int x = minX; x <= maxX; x++) {
                        const float px = x + 0.5f, py = y + 0.5f;
                        const float w1 = ((px - x0) * (y2 - y0) - (x2 - x0) * (py - y0)) / det;
                        const float w2 = ((x1 - x0) * (py - y0) - (px - x0) * (y1 - y0)) / det;
                        const float w0 = 1.0f - w1 - w2;
                        if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f)
                            continue;
                        const float z = w0 * P.sz[t[0]] + w1 * P.sz[t[1]] + w2 * P.sz[t[2]];
                        const float u = w0 * q.uv[t[0]][0] + w1 * q.uv[t[1]][0] + w2 * q.uv[t[2]][0];
                        const float v = w0 * q.uv[t[0]][1] + w1 * q.uv[t[1]][1] + w2 * q.uv[t[2]][1];
                        const int tx = std::clamp(static_cast<int>(u / 16.0f * tpx), 0, tpx - 1);
                        const int ty = std::clamp(static_cast<int>(v / 16.0f * tpx), 0, tpx - 1);
                        const uint8_t* src = &tex[(static_cast<size_t>(ty) * tpx + tx) * 4];
                        if (src[3] < 8)
                            continue;
                        const size_t at = static_cast<size_t>(y) * big + x;
                        if (z < depth[at])
                            continue;
                        depth[at] = z;
                        uint8_t* dst = &color[at * 4];
                        float r = src[0] * shade, g = src[1] * shade, b = src[2] * shade;
                        if (useTint) {
                            r *= tr;
                            g *= tg;
                            b *= tb;
                        }
                        dst[0] = static_cast<uint8_t>(std::min(255.0f, r));
                        dst[1] = static_cast<uint8_t>(std::min(255.0f, g));
                        dst[2] = static_cast<uint8_t>(std::min(255.0f, b));
                        dst[3] = src[3];
                        xmin = std::min(xmin, x);
                        xmax = std::max(xmax, x);
                        ymin = std::min(ymin, y);
                        ymax = std::max(ymax, y);
                    }
            }
        }
        if (xmax < 0)
            return false;
        out.width = out.height = size;
        out.pixels.assign(static_cast<size_t>(size) * size * 4, 0);
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) {
                int sum[4] = {0, 0, 0, 0};
                for (int dy = 0; dy < ss; dy++)
                    for (int dx = 0; dx < ss; dx++) {
                        const uint8_t* c = &color[(static_cast<size_t>(y * ss + dy) * big + x * ss + dx) * 4];
                        sum[0] += c[0] * c[3];
                        sum[1] += c[1] * c[3];
                        sum[2] += c[2] * c[3];
                        sum[3] += c[3];
                    }
                uint8_t* dst = &out.pixels[(static_cast<size_t>(y) * size + x) * 4];
                if (sum[3] > 0) {
                    dst[0] = static_cast<uint8_t>(sum[0] / sum[3]);
                    dst[1] = static_cast<uint8_t>(sum[1] / sum[3]);
                    dst[2] = static_cast<uint8_t>(sum[2] / sum[3]);
                    dst[3] = static_cast<uint8_t>(sum[3] / (ss * ss));
                }
            }
        return true;
    }

    void BakeCubeIcon(const ItemSprite& sprite, int size, Texture& out) {
        out.width = out.height = size;
        out.pixels.assign(static_cast<size_t>(size) * size * 4, 0);
        struct Face {
            float n[3];
            float o[3];
            float u[3];
            float v[3];
            int which;
        };
        const Face faces[] = {
            {{0, 1, 0}, {-.5f, .5f, -.5f}, {1, 0, 0}, {0, 0, 1}, 0},
            {{0, -1, 0}, {-.5f, -.5f, .5f}, {1, 0, 0}, {0, 0, -1}, 0},
            {{0, 0, 1}, {-.5f, .5f, .5f}, {1, 0, 0}, {0, -1, 0}, 1},
            {{0, 0, -1}, {.5f, .5f, -.5f}, {-1, 0, 0}, {0, -1, 0}, 1},
            {{1, 0, 0}, {.5f, .5f, .5f}, {0, 0, -1}, {0, -1, 0}, 2},
            {{-1, 0, 0}, {-.5f, .5f, -.5f}, {0, 0, 1}, {0, -1, 0}, 2},
        };
        const float ry = 225.0f * 3.14159265f / 180.0f, rx = 30.0f * 3.14159265f / 180.0f;
        const auto rotate = [&](const float* p, float* r) {
            const float x1 = p[0] * std::cos(ry) + p[2] * std::sin(ry);
            const float z1 = -p[0] * std::sin(ry) + p[2] * std::cos(ry);
            r[0] = x1;
            r[1] = p[1] * std::cos(rx) - z1 * std::sin(rx);
            r[2] = p[1] * std::sin(rx) + z1 * std::cos(rx);
        };
        const float scale = 10.0f * static_cast<float>(size) / 16.0f;
        const float centre = static_cast<float>(size) * 0.5f;
        const float l0[3] = {0.2f / 1.237f, 1.0f / 1.237f, -0.7f / 1.237f};
        const float l1[3] = {-0.2f / 1.237f, 1.0f / 1.237f, 0.7f / 1.237f};
        const int tile = sprite.tile;
        for (const Face& f : faces) {
            float n[3];
            rotate(f.n, n);
            if (n[2] <= 0.001f)
                continue;
            const auto dot = [&](const float* l) {
                return std::max(0.0f, n[0] * l[0] + n[1] * l[1] + n[2] * l[2]);
            };
            const float shade = std::min(1.0f, (dot(l0) + dot(l1)) * 0.6f + 0.4f);
            float o[3], u[3], v[3], tmp[3];
            rotate(f.o, o);
            for (int i = 0; i < 3; i++)
                tmp[i] = f.o[i] + f.u[i];
            rotate(tmp, u);
            for (int i = 0; i < 3; i++)
                tmp[i] = f.o[i] + f.v[i];
            rotate(tmp, v);
            const float ox = centre + o[0] * scale, oy = centre - o[1] * scale;
            const float ux = centre + u[0] * scale - ox, uy = centre - u[1] * scale - oy;
            const float vx = centre + v[0] * scale - ox, vy = centre - v[1] * scale - oy;
            const float det = ux * vy - uy * vx;
            if (std::fabs(det) < 1e-4f)
                continue;
            const std::vector<uint8_t>& image =
                f.which == 0 ? sprite.top : (f.which == 1 ? sprite.left : sprite.right);
            if (image.size() < static_cast<size_t>(tile) * tile * 4)
                continue;
            for (int y = 0; y < size; y++)
                for (int x = 0; x < size; x++) {
                    const float px = x + 0.5f - ox, py = y + 0.5f - oy;
                    const float a = (px * vy - py * vx) / det;
                    const float b = (ux * py - uy * px) / det;
                    if (a < 0 || a >= 1 || b < 0 || b >= 1)
                        continue;
                    const int tx = std::min(tile - 1, static_cast<int>(a * tile));
                    const int ty = std::min(tile - 1, static_cast<int>(b * tile));
                    const uint8_t* src = &image[(static_cast<size_t>(ty) * tile + tx) * 4];
                    if (src[3] == 0)
                        continue;
                    uint8_t* dst = &out.pixels[(static_cast<size_t>(y) * size + x) * 4];
                    dst[0] = static_cast<uint8_t>(src[0] * shade);
                    dst[1] = static_cast<uint8_t>(src[1] * shade);
                    dst[2] = static_cast<uint8_t>(src[2] * shade);
                    dst[3] = src[3];
                }
        }
    }
}
