#include "client/ui/JsonScreen.hpp"

#include "client/i18n/Lang.hpp"
#include "client/resources/TexturePack.hpp"
#include "client/ui/Hud.hpp"
#include "client/ui/TenFont.hpp"
#include "client/ui/HudData.hpp"
#include "client/ui/McFont.hpp"
#include "client/ui/UiAssets.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cctype>
#include <map>
#include <thread>
#include "util/Log.hpp"

namespace perlin::ui {
    namespace {
        const char* kScreenTemplate =
            "<rml>\n<head>\n<title>screen</title>\n<style>\n"
            "body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; "
            "padding: 0; background: transparent; }\n"
            "mcimg, mcitem { display: block; position: absolute; }\n"
            "{{chat_style}}{{rcss}}\n</style>\n</head>\n<body>\n{{chat}}{{hud}}</body>\n</rml>\n";

        std::string Digest(const Json& v) {
            return v.Dump();
        }

        // our patches over vanilla, in ui_defaults (below every pack, above vanilla); "modifications" merge into
        // vanilla controls instead of replacing; a pack redefining the same control wins
        const char* kScreenDefaults = R"JSON({
  "namespace": "pause",
  "perlin_unused": { "type": "panel" }
}
)JSON";
    }

    bool ParseJsonButton(const std::string& hoverId, JsonButton& out) {
        const std::string prefix = kJsonButtonPrefix;
        if (hoverId.compare(0, prefix.size(), prefix) != 0)
            return false;
        std::string rest = hoverId.substr(prefix.size());
        out.index = -1;
        if (const size_t colon = rest.rfind(':'); colon != std::string::npos) {
            const std::string tail = rest.substr(colon + 1);
            if (!tail.empty() &&
                std::all_of(tail.begin(), tail.end(), [](unsigned char c) { return std::isdigit(c); })) {
                out.index = std::stoi(tail);
                rest.resize(colon);
            }
        }
        out.id = std::move(rest);
        return !out.id.empty();
    }

    bool ScreenData::Global(const std::string& name, Json& out) const {
        if (auto it = globals.find(name); it != globals.end()) {
            out = it->second;
            return true;
        }
        return fallback && fallback(name, out);
    }

    int ScreenData::CollectionSize(const std::string& collection) const {
        auto it = collections.find(collection);
        return it == collections.end() ? 0 : static_cast<int>(it->second.size());
    }

    bool ScreenData::CollectionValue(const std::string& collection, int index, const std::string& binding,
                                     Json& out) const {
        auto it = collections.find(collection);
        if (it == collections.end() || index < 0 || index >= static_cast<int>(it->second.size()))
            return false;
        const Row& row = it->second[static_cast<size_t>(index)];
        auto v = row.find(binding);
        if (v == row.end())
            return false;
        out = v->second;
        return true;
    }

    std::vector<FactoryItem> ScreenData::Factory(const std::string& name) const {
        auto it = factories.find(name);
        return it == factories.end() ? std::vector<FactoryItem>{} : it->second;
    }

    std::string ScreenData::Signature() const {
        // ordered: digest independent of hash map iteration order
        std::string out;
        std::map<std::string, const Json*> g;
        for (const auto& [k, v] : globals)
            g[k] = &v;
        for (const auto& [k, v] : g)
            out += k + "=" + Digest(*v) + ";";
        std::map<std::string, const std::vector<Row>*> c;
        for (const auto& [k, v] : collections)
            c[k] = &v;
        for (const auto& [k, rows] : c) {
            out += "[" + k + "]";
            for (const Row& row : *rows) {
                std::map<std::string, const Json*> r;
                for (const auto& [rk, rv] : row)
                    r[rk] = &rv;
                for (const auto& [rk, rv] : r)
                    out += rk + "=" + Digest(*rv) + ",";
                out += "|";
            }
        }
        for (const auto& [k, items] : factories) {
            out += "{" + k + "}";
            for (const FactoryItem& f : items)
                out += f.id + "/" + f.collection + "/" + std::to_string(f.index) + ",";
        }
        return out;
    }

    JsonScreens& JsonScreens::Instance() {
        static JsonScreens screens;
        return screens;
    }

    JsonScreens::JsonScreens() {
        services.textWidth = [](const std::string& text, float s, bool ten) {
            return ten ? TenFont::Instance().Width(text, s) : McFont::Instance().Width(text, s);
        };
        services.lineHeight = [](float s, bool ten) {
            return ten ? TenFont::Instance().LineHeight(s) : static_cast<float>(McFont::kLineHeight) * s;
        };
        services.textureSize = [](const std::string& tex, int& w, int& h) {
            return UiAssets::Instance().ImageSize(tex, w, h);
        };
        services.localize = [](const std::string& k) { return UiAssets::Instance().Localize(k); };
        services.anim = &anim;
        services.animDef = [this](const std::string& ref, const std::string& ns) -> Json {
            std::string full = ref;
            if (!full.empty() && full[0] == '@')
                full.erase(0, 1);
            if (!full.contains('.'))
                full = ns + "." + full;
            if (auto cached = animCache.find(full); cached != animCache.end())
                return cached->second;
            BuildReport report;
            Json props = Json::MakeObject();
            if (auto node = BuildTree(library, full, EngineRootVariables(), report))
                props = node->props;
            animCache[full] = props;
            return props;
        };
        emitter.SetTemplates(kScreenTemplate, std::string());
        emitter.Register("inventory_item_renderer", [this](const LNode& n, RmlWriter& w) {
            const auto table = items.find(n.collection);
            if (table == items.end() || n.index < 0 || n.index >= static_cast<int>(table->second.size()))
                return;
            const HudItem& item = table->second[static_cast<size_t>(n.index)];
            if (!item.Empty())
                w.Item(n.ax, n.ay, n.w, n.h, n.layer, item.Key(), n.alpha, item.enchanted);
        });
        emitter.Register("paper_doll_renderer", [this](const LNode& n, RmlWriter& w) {
            if (paperDoll.empty())
                return;
            // game frames the doll with room around it: ~3/4 of the control's height, on its bottom edge, centred
            const float aspect = 240.0f / 400.0f;
            float h = n.h * 0.78f, wd = h * aspect;
            if (wd > n.w) {
                wd = n.w;
                h = wd / aspect;
            }
            dollX = n.ax + (n.w - wd) * 0.5f;
            dollY = n.ay + n.h - h;
            dollW = wd;
            dollH = h;
            ImageStyle style;
            style.id = "paper_doll";
            w.Image(dollX, dollY, wd, h, n.layer, "mchead:" + paperDoll, style);
        });
    }

    JsonScreens::~JsonScreens() {
        if (worker.joinable())
            worker.join();
    }

    void JsonScreens::Reset() {
        if (worker.joinable())
            worker.join();
        std::lock_guard lock(mutex);
        state = 0;
        key.clear();
        trees.clear();
        failed.clear();
        overriddenNs.clear();
        library = UiLibrary{};
        anim = AnimHost{};
        animCache.clear();
        items.clear();
        paperDoll.clear();
        dollX = dollY = dollW = dollH = 0;
        generation++;
    }

    void JsonScreens::Warm(const std::vector<std::string>& serverPackDirs) {
        const std::string data = paths::DataDir();
        std::string wanted;
        for (const std::string& d : serverPackDirs)
            wanted += d + "|";
        wanted += data + "|" + UiAssets::Instance().VanillaRoot() + "|" +
                  std::to_string(TexturePack::Instance().Generation());
        // hot reload: rescan when a json ui file in the stack is edited, added or removed (checked every 2 s)
        if (state.load() == 2 && wanted == key) {
            static auto lastProbe = std::chrono::steady_clock::now();
            const auto now = std::chrono::steady_clock::now();
            if (now - lastProbe < std::chrono::seconds(2))
                return;
            lastProbe = now;
            {
                std::lock_guard lock(mutex);
                if (!library.Changed())
                    return;
            }
            Log("screens", "ui files changed, reloading");
            key.clear();
        }
        if (state.load() == 1 || (state.load() != 0 && wanted == key))
            return;
        if (worker.joinable())
            worker.join();
        key = wanted;
        state = 1;

        // highest priority first, same stack the hud reads
        std::vector<std::string> roots;
        for (size_t i = serverPackDirs.size(); i-- > 0;)
            roots.push_back(serverPackDirs[i] + "/ui");
        if (const std::string packUi = ExtractPackUi(data, "pack_ui_screens"); !packUi.empty())
            roots.push_back(packUi + "/ui");
        roots.push_back(data + "/ui");
        roots.push_back(data + "/ui_defaults");
        roots.push_back(UiAssets::Instance().VanillaRoot() + "/ui");
        {
            std::error_code ec;
            std::filesystem::create_directories(data + "/ui_defaults", ec);
            std::ofstream(data + "/ui_defaults/_perlin_screens.json", std::ios::binary | std::ios::trunc)
                << kScreenDefaults;
        }

        worker = std::thread([this, roots]() {
            std::lock_guard lock(mutex);
            library.SetRoots(roots);
            trees.clear();
            failed.clear();
            animCache.clear();
            anim.recs.clear();
            if (!library.Scan()) {
                Log("screens", "no ui folder yet (waiting for the Bedrock samples)");
                state = 3;
                return;
            }
            generation++;
            state = 2;
            Log("screens", "ui stack ready: {} files, {} namespaces, {} roots", library.FileCount(),
                library.NamespaceCount(), roots.size());
        });
    }

    uint64_t JsonScreens::Generation() const {
        return generation.load() * 1000003ull + i18n::Lang::Get().Version();
    }

    bool JsonScreens::WaitReady(int timeoutMs) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (state.load() == 1 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return Ready();
    }

    const Node* JsonScreens::Tree(const std::string& reference, const Vars* rootVars) {
        std::string key = reference;
        Vars vars = EngineRootVariables();
        if (rootVars) {
            std::map<std::string, std::string> ordered;
            for (const auto& [name, value] : *rootVars) {
                ordered[name] = value.Dump();
                vars[name] = value;
            }
            for (const auto& [name, value] : ordered)
                key += "|" + name + "=" + value;
        }
        if (auto it = trees.find(key); it != trees.end())
            return it->second.get();
        if (failed.contains(key))
            return nullptr;
        if (trees.size() > 64)
            trees.clear(); // titles change per container; keep the cache bounded
        BuildReport report;
        auto built = BuildTree(library, reference, vars, report);
        if (!built) {
            Log("screens", "{} could not be built", reference);
            failed.insert(key);
            return nullptr;
        }
        std::cout << "[screens] built " << reference << ": " << report.nodes << " controls";
        if (!report.missingControls.empty())
            std::cout << ", " << report.missingControls.size() << " missing references";
        std::cout << std::endl;
        for (const std::string& m : report.missingControls)
            Log("screens", "  missing {}", m);
        const Node* raw = built.get();
        trees[key] = std::move(built);
        return raw;
    }

    bool JsonScreens::Overridden(const std::string& ns) const {
        if (state.load() != 2)
            return false;
        // last three roots are data/ui, data/ui_defaults, vanilla: anything before is a pack
        const size_t count = library.RootCount();
        return count > 3 && library.DefinedBefore(ns, count - 3);
    }

    namespace {
        void DumpTreeText(const LNode& n, int depth, std::string& out);
    }

    bool JsonScreens::Build(const std::string& reference, const ScreenData& data, int guiW, int guiH,
                            float scroll, std::string& rml, const Vars* rootVars) {
        if (state.load() != 2)
            return false;
        std::lock_guard lock(mutex);
        const Node* tree = Tree(reference, rootVars);
        if (!tree)
            return false;
        using Clock = std::chrono::steady_clock;
        static const auto start = Clock::now();
        anim.now = std::chrono::duration<double>(Clock::now() - start).count();
        const auto root =
            EvaluateTree(*tree, data, services, static_cast<float>(guiW), static_cast<float>(guiH));
        if (!root)
            return false;
        emitter.SetScroll(scroll);
        dollW = 0;
        if (std::getenv("PERLIN_TEST_TREE"))
            std::printf("[build] t=%.2f animating=%d rootAlpha=%.2f kids=%zu\n", anim.now,
                        anim.animating ? 1 : 0, root->alpha, root->kids.size());
        // dev hook: PERLIN_TEST_TREE=<file> writes the laid-out control tree (same shape as screen_dump's)
        if (const char* treeDump = std::getenv("PERLIN_TEST_TREE")) {
            std::string text;
            DumpTreeText(*root, 0, text);
            std::ofstream out(treeDump, std::ios::binary);
            out << text;
        }
        rml = emitter.Emit(*root, static_cast<float>(guiW), static_cast<float>(guiH), std::string(), false);
        return true;
    }

    namespace {
        void DumpTreeText(const LNode& n, int depth, std::string& out) {
            const std::string pad(static_cast<size_t>(depth) * 2, ' ');
            char line[512];
            std::snprintf(line, sizeof(line), "%s%s [%s] abs=(%.1f,%.1f) %.1fx%.1f%s%s\n", pad.c_str(),
                          n.id.c_str(), n.type.c_str(), n.ax, n.ay, n.w, n.h, n.visible ? "" : " HIDDEN",
                          n.text.empty() ? "" : (" text=" + n.text).c_str());
            out += line;
            for (const auto& k : n.kids)
                DumpTreeText(*k, depth + 1, out);
        }
    }
}
