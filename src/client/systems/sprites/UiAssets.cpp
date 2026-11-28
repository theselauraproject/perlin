#include "client/systems/model/DialFrames.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "client/systems/sprites/ItemDisplay.hpp"
#include "client/systems/sprites/SpecialItemModels.hpp"
#include "client/systems/attachable/Geometry.hpp"
#include "client/systems/resources/ServerPacks.hpp"
#include "client/systems/resources/ServerBlocks.hpp"
#include "client/systems/i18n/Lang.hpp"
#include "client/systems/text/McFont.hpp"
#include "util/Paths.hpp"
#include "util/Resources.hpp"
#include "util/ResourcePaths.hpp"

#include "client/systems/blocks/BlockLibrary.hpp"
#include "client/systems/resources/TexturePack.hpp"
#include "util/JsonValue.hpp"

#include <algorithm>
#include <glm/glm.hpp>
#include <cctype>
#include <iostream>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <unordered_set>
#include "util/Log.hpp"
#include "util/Strings.hpp"
#include "util/FileIo.hpp"

namespace fs = std::filesystem;

namespace perlin::ui {
    namespace {
        bool Slurp(const std::string& path, std::string& out) {
            std::string contents;
            if (!file::Read(path, contents))
                return false;
            out = contents;
            return true;
        }
        std::string Pretty(const std::string& id) {
            std::string out;
            bool up = true;
            for (const char c : std::string(str::StripAnyNamespace(id))) {
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
        if (root != vanillaRoot)
            RootGeneration().fetch_add(1, std::memory_order_release);
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
            std::ranges::stable_sort(found,
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

    bool UiAssets::ModelTexture(const std::string& rel, Texture& out) {
        if (Image(rel, out) && out.width > 0)
            return true;
        return rel.find('/') == std::string::npos && LoadTexture(paths::DataDir() + "/assets/" + rel, out) &&
               out.width > 0;
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
                text = layers.front();
                found = true;
            }
        }
        if (found || Slurp(root + "/" + base + ".json", text)) {
            json::Value json;
            std::string error;
            if (json::Value::Parse(text, json, error) && json.IsObject()) {
                out.present = true;
                if (const json::Value* n = json.Find("nineslice_size")) {
                    out.nine = true;
                    if (n->IsNumber()) {
                        for (float& v : out.slice)
                            v = static_cast<float>(n->number);
                    } else if (n->IsArray() && n->array.size() >= 4) {
                        for (int i = 0; i < 4; i++)
                            out.slice[i] = static_cast<float>(n->array[i].NumberOr(0));
                    }
                }
                if (const json::Value* b = json.Find("base_size"); b && b->IsArray() && b->array.size() >= 2) {
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
        const std::string name = std::string(str::StripAnyNamespace(identifier));
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
        const std::string name = std::string(str::StripAnyNamespace(identifier));
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
            if (resources::Read(resources::path::kItemNames, data)) {
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
            json::Value json;
            std::string error;
            if (!json::Value::Parse(text, json, error) || !json.IsObject())
                continue;
            const json::Value* data = json.Find("texture_data");
            if (!data || !data->IsObject())
                continue;
            for (const auto& entry : data->object) {
                const json::Value* textures = entry.second.Find("textures");
                if (!textures)
                    continue;
                std::vector<std::string> paths;
                const auto add = [&](const json::Value& v) {
                    if (v.IsString())
                        paths.push_back(v.string);
                    else if (v.IsObject())
                        if (const json::Value* p = v.Find("path"); p && p->IsString())
                            paths.push_back(p->string);
                };
                if (textures->IsArray())
                    for (const json::Value& v : textures->array)
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

            if (t.width > 0 && t.height > t.width && t.height % t.width == 0 &&
                t.pixels.size() >= static_cast<size_t>(t.width) * t.width * 4) {
                t.pixels.resize(static_cast<size_t>(t.width) * t.width * 4);
                t.height = t.width;
            }
            if (name.starts_with("leather_")) {
                const uint32_t rgb = dyeColor >= 0 ? static_cast<uint32_t>(dyeColor) & 0xFFFFFFu : 0xA06540u;
                const float cr = ((rgb >> 16) & 255) / 255.0f, cg = ((rgb >> 8) & 255) / 255.0f,
                            cb = (rgb & 255) / 255.0f;
                // Bedrock's change-colour convention (as BakeColorMask does for worn armour): opaque pixels
                // are the dyed leather and take the colour; nearly transparent ones are the undyed overlay
                // (stitching, buckles) and are shown as they are, made opaque. Undyed leather is Java's
                // default #A06540.
                for (size_t i = 0; i + 3 < t.pixels.size(); i += 4) {
                    uint8_t* px = &t.pixels[i];
                    if (px[3] == 0)
                        continue;
                    if (px[3] >= 128) {
                        px[0] = static_cast<uint8_t>(std::clamp(px[0] * cr, 0.0f, 255.0f));
                        px[1] = static_cast<uint8_t>(std::clamp(px[1] * cg, 0.0f, 255.0f));
                        px[2] = static_cast<uint8_t>(std::clamp(px[2] * cb, 0.0f, 255.0f));
                    }
                    px[3] = 255;
                }
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

    bool UiAssets::BlocksReady() {
        std::lock_guard lock(mutex);
        return blocks && blocks->Ready();
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
        std::string name = std::string(str::StripAnyNamespace(id));
        if (name == "tallgrass")
            name = "short_grass";

        if (const size_t dialAt = key.find("!d"); dialAt != std::string::npos) {
            const int count = DialFrameCount(id);
            Texture atlas;
            if (count > 0 && FindImage(std::string(DialAtlas(id)), atlas) && atlas.width > 0 &&
                atlas.height >= atlas.width &&
                atlas.pixels.size() >= static_cast<size_t>(atlas.width) * atlas.height * 4) {
                const int frames = atlas.height / atlas.width;
                const int wanted = std::clamp(std::atoi(key.c_str() + dialAt + 2), 0, count - 1);
                const int frame = std::clamp(wanted * frames / count, 0, frames - 1);
                const size_t frameBytes = static_cast<size_t>(atlas.width) * atlas.width * 4;
                sprite.ok = true;
                sprite.w = sprite.h = atlas.width;
                sprite.rgba.assign(atlas.pixels.begin() + static_cast<std::ptrdiff_t>(frameBytes * frame),
                                   atlas.pixels.begin() + static_cast<std::ptrdiff_t>(frameBytes * (frame + 1)));
                auto inserted = items.emplace(key, std::move(sprite)).first;
                return &inserted->second;
            }
        }

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
            if (blocks && blocks->Ready() && reported.insert(id).second)
                Log("items", "no texture found for {}{}, using a generic icon", id,
                    (customItem ? " (icon '" + custom.icon + "')" : std::string()));
            MakeGenericIcon(id, sprite);
            if ((id.contains(':') && !id.starts_with("minecraft:")) || !blocks || !blocks->Ready())
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

    bool UiAssets::ItemMesh(const std::string& name, std::vector<attachable::Quad>& out, Texture& atlas) {
        out.clear();
        if (!blocks || !blocks->Ready())
            return false;
        const BlockModels& models = blocks->Models();
        // The inventory models are keyed by Java item id (Bedrock's snow is Java's snow_block, ...).
        const ModelEntry* entry = models.FindItem(itemdisplay::JavaItemName(str::StripAnyNamespace(name)));
        if (!entry)
            return false;

        // The atlas includes only the textures actually used by this item,
        // instead of allocating a full terrain atlas for every held model.
        std::vector<int> usedTiles;
        std::unordered_map<int, size_t> tileSlots;
        std::vector<const ModelQuad*> faces;
        for (const auto& group : entry->groups) {
            if (group.empty())
                continue;
            for (const ModelQuad& q : models.Variant(group.front().variant)) {
                // The held-item pipeline has no per-face biome tint yet. Keep
                // the existing tinted sprite path instead of drawing gray
                // grass/leaves simply because a Java mesh is available.
                if (q.tint >= 0)
                    return false;
                const int tile = q.tile ? q.tile
                                        : (q.tex != 0xffff && q.tex < models.tiles.size()
                                               ? models.tiles[q.tex] : 0);
                if (tile <= 0)
                    continue;
                if (!tileSlots.contains(tile)) {
                    tileSlots[tile] = usedTiles.size();
                    usedTiles.push_back(tile);
                }
                faces.push_back(&q);
            }
        }
        if (faces.empty() || usedTiles.empty())
            return false;
        const int tilePx = BlockLibrary::TilePx();
        if (tilePx <= 0 || usedTiles.size() > 64)
            return false;
        atlas.width = tilePx * static_cast<int>(usedTiles.size());
        atlas.height = tilePx;
        atlas.pixels.assign(static_cast<size_t>(atlas.width) * atlas.height * 4, 0);
        for (size_t slot = 0; slot < usedTiles.size(); ++slot) {
            std::vector<uint8_t> pixels;
            if (!blocks->TilePixels(usedTiles[slot], pixels) ||
                pixels.size() < static_cast<size_t>(tilePx) * tilePx * 4)
                return false;
            for (int y = 0; y < tilePx; ++y)
                std::copy_n(pixels.data() + static_cast<size_t>(y) * tilePx * 4,
                            static_cast<size_t>(tilePx) * 4,
                            atlas.pixels.data() + (static_cast<size_t>(y) * atlas.width + slot * tilePx) * 4);
        }
        out.reserve(faces.size());
        for (const ModelQuad* source : faces) {
            const int tile = source->tile ? source->tile
                                           : models.tiles[source->tex];
            const float slot = static_cast<float>(tileSlots.at(tile));
            const float columns = static_cast<float>(usedTiles.size());
            attachable::Quad q{};
            for (int i = 0; i < 4; ++i) {
                q.p[i] = glm::vec3(source->pos[i][0], source->pos[i][1], source->pos[i][2]);
                q.uv[i] = glm::vec2((slot + source->uv[i][0] / 16.0f) / columns,
                                    source->uv[i][1] / 16.0f);
            }
            const glm::vec3 n = glm::cross(q.p[1] - q.p[0], q.p[3] - q.p[0]);
            if (glm::dot(n, n) < 1.0e-12f)
                continue;
            q.n = glm::normalize(n);
            out.push_back(q);
        }
        return !out.empty();
    }

    namespace {
        // One textured quad for the software GUI rasteriser: model-space corners (0..1 block),
        // normalised UVs and the texture it samples.
        struct IconQuad {
            glm::vec3 p[4];
            glm::vec2 uv[4];
            glm::vec3 n{0.0f};
            const std::vector<uint8_t>* texture = nullptr;
            int texW = 0, texH = 0;
            glm::vec3 tint{1.0f};
            bool shade = true;
        };

        // Draws an item the way GuiRenderer does: a 16x16 slot is `size` pixels, the model is placed with
        // translate(8, 8) * scale(16, -16, 16) * ItemTransform.apply(gui) and depth tested. "side" lit
        // models take the two GUI 3D lights; "front" lit ones are lit flat from the viewer.
        // `pad` adds that many pixels around the slot on every side, for models that reach outside their
        // 16x16 slot in Java (the shield, banners, beds): the image is then (size + 2 * pad) square.
        bool RasterIcon(const std::vector<IconQuad>& quads, const glm::mat4& pose, bool frontLight, int size,
                        Texture& out, int pad = 0) {
            const int ss = 2;
            const int slot = size * ss;
            const int full = size + 2 * pad;
            const int big = full * ss;
            std::vector<uint8_t> color(static_cast<size_t>(big) * big * 4, 0);
            std::vector<float> depth(static_cast<size_t>(big) * big, -1e9f);
            const float half = static_cast<float>(big) * 0.5f;
            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(pose)));
            // Lighting.ITEMS_3D ("side") and ITEMS_FLAT ("front") from the Java client: the level's two
            // diffuse lights (0.2, 1, -0.7) and (-0.2, 1, 0.7) turned by the GUI's light matrices, expressed
            // here in the y-up space the icon is built in. Top faces come out brightest, the left face of a
            // block next and the right face darkest, as in Java's inventory.
            const glm::vec3 l0 = frontLight ? glm::vec3(-0.222519f, 0.171499f, 0.959726f)
                                            : glm::vec3(-0.933439f, 0.262695f, -0.244300f);
            const glm::vec3 l1 = frontLight ? glm::vec3(-0.215012f, 0.971825f, 0.096568f)
                                            : glm::vec3(-0.103571f, 0.976607f, 0.188446f);
            bool any = false;
            for (const IconQuad& q : quads) {
                if (!q.texture || q.texW <= 0 || q.texH <= 0 ||
                    q.texture->size() < static_cast<size_t>(q.texW) * q.texH * 4)
                    continue;
                float sx[4], sy[4], sz[4];
                for (int i = 0; i < 4; i++) {
                    const glm::vec4 v = pose * glm::vec4(q.p[i], 1.0f);
                    sx[i] = half + v.x * static_cast<float>(slot);
                    sy[i] = half - v.y * static_cast<float>(slot);
                    sz[i] = v.z;
                }
                glm::vec3 n = q.n;
                if (glm::dot(n, n) < 1e-12f)
                    n = glm::cross(q.p[1] - q.p[0], q.p[3] - q.p[0]);
                n = normalMatrix * n;
                const float nl = glm::length(n);
                if (nl < 1e-6f)
                    continue;
                n /= nl;
                if (n.z < 0.0f)
                    n = -n;
                float shade = 1.0f;
                if (q.shade)
                    shade = std::min(1.0f,
                                     (std::max(0.0f, glm::dot(n, l0)) + std::max(0.0f, glm::dot(n, l1))) * 0.6f + 0.4f);
                const glm::vec3 lit = q.tint * shade;
                const std::vector<uint8_t>& tex = *q.texture;
                const int tris[2][3] = {{0, 1, 2}, {0, 2, 3}};
                for (const auto& t : tris) {
                    const float x0 = sx[t[0]], y0 = sy[t[0]], x1 = sx[t[1]], y1 = sy[t[1]], x2 = sx[t[2]],
                                y2 = sy[t[2]];
                    const float det = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
                    if (std::fabs(det) < 1e-6f)
                        continue;
                    const int minX = std::max(0, static_cast<int>(std::floor(std::min({x0, x1, x2}))));
                    const int maxX = std::min(big - 1, static_cast<int>(std::ceil(std::max({x0, x1, x2}))));
                    const int minY = std::max(0, static_cast<int>(std::floor(std::min({y0, y1, y2}))));
                    const int maxY = std::min(big - 1, static_cast<int>(std::ceil(std::max({y0, y1, y2}))));
                    for (int y = minY; y <= maxY; y++)
                        for (int x = minX; x <= maxX; x++) {
                            const float px = static_cast<float>(x) + 0.5f, py = static_cast<float>(y) + 0.5f;
                            const float w1 = ((px - x0) * (y2 - y0) - (x2 - x0) * (py - y0)) / det;
                            const float w2 = ((x1 - x0) * (py - y0) - (px - x0) * (y1 - y0)) / det;
                            const float w0 = 1.0f - w1 - w2;
                            if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f)
                                continue;
                            const float z = w0 * sz[t[0]] + w1 * sz[t[1]] + w2 * sz[t[2]];
                            const size_t at = static_cast<size_t>(y) * big + x;
                            if (z < depth[at])
                                continue;
                            const float u = w0 * q.uv[t[0]].x + w1 * q.uv[t[1]].x + w2 * q.uv[t[2]].x;
                            const float v = w0 * q.uv[t[0]].y + w1 * q.uv[t[1]].y + w2 * q.uv[t[2]].y;
                            const int tx = std::clamp(static_cast<int>(u * static_cast<float>(q.texW)), 0, q.texW - 1);
                            const int ty = std::clamp(static_cast<int>(v * static_cast<float>(q.texH)), 0, q.texH - 1);
                            const uint8_t* src = &tex[(static_cast<size_t>(ty) * q.texW + tx) * 4];
                            if (src[3] < 8)
                                continue;
                            depth[at] = z;
                            uint8_t* dst = &color[at * 4];
                            dst[0] = static_cast<uint8_t>(std::min(255.0f, src[0] * lit.r));
                            dst[1] = static_cast<uint8_t>(std::min(255.0f, src[1] * lit.g));
                            dst[2] = static_cast<uint8_t>(std::min(255.0f, src[2] * lit.b));
                            dst[3] = src[3];
                            any = true;
                        }
                }
            }
            if (!any)
                return false;
            out.width = out.height = full;
            out.pixels.assign(static_cast<size_t>(full) * full * 4, 0);
            for (int y = 0; y < full; y++)
                for (int x = 0; x < full; x++) {
                    int sum[4] = {0, 0, 0, 0};
                    for (int dy = 0; dy < ss; dy++)
                        for (int dx = 0; dx < ss; dx++) {
                            const uint8_t* c = &color[(static_cast<size_t>(y * ss + dy) * big + x * ss + dx) * 4];
                            sum[0] += c[0] * c[3];
                            sum[1] += c[1] * c[3];
                            sum[2] += c[2] * c[3];
                            sum[3] += c[3];
                        }
                    uint8_t* dst = &out.pixels[(static_cast<size_t>(y) * full + x) * 4];
                    if (sum[3] > 0) {
                        dst[0] = static_cast<uint8_t>(sum[0] / sum[3]);
                        dst[1] = static_cast<uint8_t>(sum[1] / sum[3]);
                        dst[2] = static_cast<uint8_t>(sum[2] / sum[3]);
                        dst[3] = static_cast<uint8_t>(sum[3] / (ss * ss));
                    }
                }
            return true;
        }

        // The GUI pose of an inventory model: its own "gui" display transform when the Java table has one,
        // otherwise block/block.json's (30, 225, 0) at 0.625.
        glm::mat4 GuiPose(const itemdisplay::Entry* display) {
            if (display && display->found && display->gui.present)
                return itemdisplay::Pose(display->gui, false);
            itemdisplay::Transform block;
            block.present = true;
            block.rotation = {30.0f, 225.0f, 0.0f};
            block.scale = glm::vec3(0.625f);
            return itemdisplay::Pose(block, false);
        }

        // The Java item id of an icon key ("minecraft:bed;14@...": Bedrock id, aux and decorations).
        std::string IconBaseName(const std::string& key) {
            const size_t end = key.find_first_of(";@!#");
            int aux = -1;
            if (end != std::string::npos && key[end] == ';')
                aux = std::atoi(key.c_str() + end + 1);
            return itemdisplay::JavaItemName(str::StripAnyNamespace(key.substr(0, end)), aux);
        }
    }

    bool UiAssets::BakeSpecialIcon(const std::string& name, Texture& out) {
        const itemdisplay::Entry& display = itemdisplay::LookupJava(name, itemdisplay::Context::Gui);
        if (!display.found || display.kind != itemdisplay::Entry::Kind::Special)
            return false;
        std::vector<special::Part> parts;
        if (!special::Build(display, "minecraft:" + name, parts))
            return false;
        std::vector<Texture> textures(parts.size());
        std::vector<IconQuad> quads;
        for (size_t i = 0; i < parts.size(); i++) {
            if (!ModelTexture(parts[i].texture, textures[i]))
                continue;
            for (const attachable::Quad& q : parts[i].quads) {
                IconQuad iq;
                std::copy_n(q.p, 4, iq.p);
                std::copy_n(q.uv, 4, iq.uv);
                iq.n = q.n;
                iq.texture = &textures[i].pixels;
                iq.texW = textures[i].width;
                iq.texH = textures[i].height;
                iq.tint = parts[i].tint;
                quads.push_back(iq);
            }
        }
        // Drawn with a margin so a model that reaches outside its slot (the shield's top, a banner) is not
        // cut off; Canvas::Item recognises the size and places the margin around the slot.
        return RasterIcon(quads, GuiPose(&display), display.frontLight, kOverflowIconSlotPx, out,
                          (kOverflowIconPx - kOverflowIconSlotPx) / 2);
    }

    bool UiAssets::ItemIcon(const std::string& key, Texture& out) {
        const ItemSprite* s = Item(key);
        // The Java item model decides how the icon is drawn, whatever block state ("#runtime id") the
        // stack carries: a special renderer (shield, chest, bed, ...), the item's own inventory mesh
        // (a fence icon has rails, stairs face the Java way, a shelf shows its front), or a flat sprite
        // (lantern, chain, doors, ...). Custom (non-minecraft) items keep the pack's own icon.
        const size_t idEnd = key.find_first_of(";@!#");
        const bool vanilla = key.find(':') == std::string::npos || key.starts_with("minecraft:") ||
                             key.find(':') > idEnd;
        if (vanilla && key.find("!d") == std::string::npos) {
            const std::string base = IconBaseName(key);
            const itemdisplay::Entry& display = itemdisplay::LookupJava(base, itemdisplay::Context::Gui);
            if (display.found && display.kind == itemdisplay::Entry::Kind::Special && BakeSpecialIcon(base, out))
                return true;
            const bool blockModel = !display.found || display.kind == itemdisplay::Entry::Kind::Block;
            if (blockModel && blocks && blocks->Ready() && blocks->Models().FindItem(base) &&
                BakeModelIcon(base, 0, 64, out, true))
                return true;
            if (display.found && display.kind == itemdisplay::Entry::Kind::Flat && (!s || s->fromBlock)) {
                // Java draws it as a flat item, but no item texture was found under the Bedrock id:
                // try the Java texture's name and the Java id.
                const size_t slash = display.model.find_last_of('/');
                const std::string texture = slash == std::string::npos ? display.model : display.model.substr(slash + 1);
                ItemSprite flat;
                for (const std::string& candidate : {texture, base})
                    if (!candidate.empty() && FlatSprite(candidate, 0, flat) && flat.ok && !flat.rgba.empty()) {
                        out.width = flat.w;
                        out.height = flat.h;
                        out.pixels = std::move(flat.rgba);
                        return true;
                    }
            }
            // Saplings, flowers, grass, ...: Java's item is the block's own texture as a flat sprite, which
            // is what the non-cube block sprite already is (tinted like the plant).
            if (display.found && display.kind == itemdisplay::Entry::Kind::Flat && s && s->ok && !s->block &&
                !s->generic && !s->rgba.empty()) {
                out.width = s->w;
                out.height = s->h;
                out.pixels = s->rgba;
                return true;
            }
        }
        if (!s || s->fromBlock || s->generic) {
            const size_t hashAt = key.find('#');
            if (hashAt != std::string::npos && blocks && blocks->Ready()) {
                const size_t semi = key.rfind(';', hashAt);
                std::string name =
                    std::string(str::StripAnyNamespace(semi == std::string::npos ? key.substr(0, hashAt) : key.substr(0, semi)));
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

    bool UiAssets::BakeModelIcon(const std::string& name, int32_t hash, int size, Texture& out, bool inventory) {
        const BlockModels& bm = blocks->Models();
        if (!bm.Loaded())
            return false;
        const ModelEntry* entry = inventory ? bm.FindItem(name) : bm.Find(hash);
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
        glm::vec3 tint(1.0f);
        bool tinted = false;
        {
            auto set = [&](uint32_t rgb) {
                tint = glm::vec3(static_cast<float>((rgb >> 16) & 255), static_cast<float>((rgb >> 8) & 255),
                                 static_cast<float>(rgb & 255)) /
                       255.0f;
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
                tint = glm::vec3(app->tintR, app->tintG, app->tintB);
                tinted = true;
            }
        }
        const int tpx = BlockLibrary::TilePx();
        std::unordered_map<int, std::vector<uint8_t>> tiles;
        std::vector<IconQuad> iconQuads;
        iconQuads.reserve(quads.size());
        for (const ModelQuad* mq : quads) {
            const ModelQuad& q = *mq;
            const int tile = q.tile ? q.tile : ((q.tex != 0xFFFF && q.tex < bm.tiles.size()) ? bm.tiles[q.tex] : 0);
            if (!tile)
                continue;
            auto found = tiles.find(tile);
            if (found == tiles.end()) {
                std::vector<uint8_t> px;
                if (!blocks->TilePixels(tile, px))
                    px.clear();
                found = tiles.emplace(tile, std::move(px)).first;
            }
            IconQuad iq;
            for (int i = 0; i < 4; i++) {
                iq.p[i] = glm::vec3(q.pos[i][0], q.pos[i][1], q.pos[i][2]);
                iq.uv[i] = glm::vec2(q.uv[i][0], q.uv[i][1]) / 16.0f;
            }
            iq.texture = &found->second;
            iq.texW = iq.texH = tpx;
            iq.shade = q.shade != 0;
            if (tinted && q.tint >= 0 && !q.tile)
                iq.tint = tint;
            iconQuads.push_back(iq);
        }
        // An inventory model is drawn with its Java "gui" transform; a placed block state keeps the
        // generic block view.
        const itemdisplay::Entry* display =
            inventory ? &itemdisplay::LookupJava(name, itemdisplay::Context::Gui) : nullptr;
        return RasterIcon(iconQuads, GuiPose(display), display && display->found && display->frontLight, size,
                          out);
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
        // Java's GUI "side" lights (see RasterIcon).
        const float l0[3] = {-0.933439f, 0.262695f, -0.244300f};
        const float l1[3] = {-0.103571f, 0.976607f, 0.188446f};
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
