#include "client/ui/Hud.hpp"
#include "client/ui/GuiScale.hpp"
#include "client/ui/TenFont.hpp"

#include "client/ui/CreativeScreen.hpp"
#include "client/ui/DeathScreen.hpp"
#include "client/ui/DisconnectScreen.hpp"
#include "client/ui/HudRenderers.hpp"
#include "client/ui/InventoryScreen.hpp"
#include "client/ui/LoadingScreen.hpp"
#include "client/ui/PauseScreen.hpp"
#include "client/ui/McFont.hpp"
#include "client/ui/UiAssets.hpp"
#include "client/ui/JavaDefaults.hpp"
#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiTemplate.hpp"
#include "client/resources/ServerPacks.hpp"
#include "client/resources/TexturePack.hpp"
#include "client/resources/TextureLoader.hpp"

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include "util/Log.hpp"

namespace fs = std::filesystem;

namespace perlin::ui {
    namespace {
        double Now() {
            using Clock = std::chrono::steady_clock;
            static const auto start = Clock::now();
            return std::chrono::duration<double>(Clock::now() - start).count();
        }

        bool Write(const fs::path& path, const std::string& text) {
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            std::ofstream f(path, std::ios::binary);
            if (!f)
                return false;
            f << text;
            return true;
        }
    }

    std::string ExtractPackUi(const std::string& dataDir, const std::string& folder) {
        TexturePack& tp = TexturePack::Instance();
        const fs::path root = fs::path(dataDir) / "cache" / folder;
        std::error_code ec;
        fs::remove_all(root, ec);
        if (!tp.Active())
            return {};
        const std::vector<std::string> files = tp.ListFiles("ui", ".json");
        bool any = false;
        for (const std::string& rel : files) {
            const std::vector<std::string> layers = tp.ReadTextLayers(rel);
            if (layers.empty())
                continue;
            const fs::path out = root / rel;
            fs::create_directories(out.parent_path(), ec);
            std::ofstream f(out, std::ios::binary | std::ios::trunc);
            if (!f)
                continue;
            f << layers.front();
            any = true;
        }
        return any ? root.string() : std::string();
    }

    Hud::Hud() = default;
    Hud::~Hud() = default;

    void Hud::WriteDefaults() {
        const fs::path file = fs::path(dataDir) / "ui_defaults" / "_perlin_java.json";
        std::error_code ec;
        if (javaStyle)
            Write(file, kJavaHudOverrides);
        else
            fs::remove(file, ec);
    }

    void Hud::SetJavaStyle(bool java) {
        if (java == javaStyle && !dataDir.empty())
            return;
        javaStyle = java;
        if (dataDir.empty())
            return;
        WriteDefaults();
        reloadRequested = true;
    }

    bool Hud::Initialize(RenderContext& renderer, const std::string& vanilla, const std::string& data_dir) {
        dataDir = data_dir;
        vanillaRoot = vanilla;
        if (!backend.Initialize(renderer))
            return false;
        WriteDefaults();
        Log("ui", "font: {}", UiAssets::Instance().LoadFont(dataDir));
        std::error_code ec;
        fs::create_directories(fs::path(dataDir) / "ui", ec);
        {
            uint64_t version = 0;
            LoadUiTemplate(dataDir, "hud_screen.rml", DefaultHudTemplate(), version);
            LoadUiTemplate(dataDir, "chat_screen.rml", DefaultChatTemplate(), version);
            LoadUiTemplate(dataDir, "inventory_screen.rml", DefaultInventoryScreenTemplate(), version);
            LoadUiTemplate(dataDir, "pause_screen.rml", DefaultPauseScreenTemplate(), version);
            LoadUiTemplate(dataDir, "loading_screen.rml", DefaultLoadingScreenTemplate(), version);
            LoadUiTemplate(dataDir, "disconnect_screen.rml", DefaultDisconnectScreenTemplate(), version);
            LoadUiTemplate(dataDir, "creative_screen.rml", DefaultCreativeScreenTemplate(), version);
            LoadDeathScreenTemplate(dataDir, version);
            ExportScreenTemplates();
        }

        services.textWidth = [](const std::string& text, float s, bool ten) {
            return ten ? TenFont::Instance().Width(text, s) : McFont::Instance().Width(text, s);
        };
        services.lineHeight = [](float s, bool ten) {
            return ten ? TenFont::Instance().LineHeight(s) : static_cast<float>(McFont::kLineHeight) * s;
        };
        services.textureSize = [](const std::string& tex, int& w, int& h) {
            return UiAssets::Instance().ImageSize(tex, w, h);
        };
        services.localize = [](const std::string& key) { return UiAssets::Instance().Localize(key); };

        services.anim = &animHost;
        services.animDef = [this](const std::string& ref, const std::string& ns) -> Json {
            std::string full = ref;
            if (!full.empty() && full[0] == '@')
                full.erase(0, 1);
            if (!full.contains('.'))
                full = ns + "." + full;
            const auto cached = animCache.find(full);
            if (cached != animCache.end())
                return cached->second;
            BuildReport report;
            Json props = Json::MakeObject();
            if (auto node = BuildTree(library, full, EngineRootVariables(), report))
                props = node->props;
            animCache[full] = props;
            return props;
        };
        data = std::make_unique<HudData>(state);
        RegisterHudRenderers(emitter, state, services);
        return Rebuild();
    }

    void Hud::UnloadLevelUi() {
        if (probeLaunched) {
            changeProbe.wait();
            probeLaunched = false;
        }
        backend.UnloadDocuments();
        tree.reset();
        crosshairTree.reset();
        library = UiLibrary{};
        animHost = AnimHost{};
        animCache.clear();
        state = HudState{};
        lastRml.clear();
        lastSignature = lastIncomingSignature = 0;
        cursorValid = false;
        reloadRequested = true;
    }

    void Hud::Shutdown() {
        backend.Shutdown();
        tree.reset();
    }

    namespace {
        // font/glyph_XX.png: vanilla, then server packs low to high priority (first listed wins);
        // servers draw logos/icons in chat, titles, sidebars with these
        void LoadGlyphPages(const std::string& vanillaRoot) {
            McFont& font = McFont::Instance();
            font.ClearGlyphPages();
            std::vector<std::string> dirs{vanillaRoot};
            const std::vector<std::string> server = ServerPacks::Instance().Directories();
            for (size_t i = server.size(); i-- > 0;)
                dirs.push_back(server[i]);
            int loaded = 0;
            for (const std::string& dir : dirs) {
                std::error_code ec;
                for (const auto& entry : fs::directory_iterator(fs::path(dir) / "font", ec)) {
                    const std::string name = entry.path().filename().string();
                    unsigned page = 0;
                    if (name.size() != 12 || !name.starts_with("glyph_") || name.substr(8) != ".png" ||
                        std::sscanf(name.c_str() + 6, "%2x", &page) != 1)
                        continue;
                    Texture t;
                    if (!LoadTexture(entry.path().string(), t))
                        continue;
                    font.AddGlyphPage(page, t.pixels, t.width, t.height);
                    loaded++;
                }
            }
            if (loaded)
                Log("ui", "glyph pages: {} loaded", loaded);
        }
    }

    bool Hud::Rebuild() {
        if (probeLaunched) {
            changeProbe.wait();
            probeLaunched = false;
        }
        LoadGlyphPages(vanillaRoot);
        {
            std::vector<std::string> roots;
            const std::vector<std::string> server = ServerPacks::Instance().Directories();
            for (size_t i = server.size(); i-- > 0;)
                roots.push_back(server[i] + "/ui");
            const std::string packUi = ExtractPackUi(dataDir);
            if (!packUi.empty())
                roots.push_back(packUi + "/ui");
            roots.push_back(dataDir + "/ui");
            roots.push_back(dataDir + "/ui_defaults");
            roots.push_back(vanillaRoot + "/ui");
            library.SetRoots(std::move(roots));
        }
        if (!library.Scan()) {
            Log("ui", "no ui folder found under {}/ui (waiting for the Bedrock samples)", vanillaRoot);
            tree.reset();
            return false;
        }
        BuildReport report;
        auto built = BuildTree(library, "hud.hud_screen", EngineRootVariables(), report);
        for (const std::string& problem : library.Problems())
            Log("ui", "{}", problem);
        for (const std::string& missing : report.missingControls)
            Log("ui", "missing control {}", missing);
        if (!built) {
            Log("ui", "hud.hud_screen could not be built");
            return false;
        }
        tree = std::move(built);
        {
            BuildReport crossReport;
            crosshairTree =
                BuildTree(library, "hud_crosshair.hud_crosshair_screen", EngineRootVariables(), crossReport);
            cursorEmitter.Register("cursor_renderer", [this](const LNode& n, RmlWriter&) {
                float dx = 0, dy = 0;
                if (!FilterHudElement("cursor_renderer", dx, dy))
                    return;
                cursorX = std::floor(n.ax + dx + (n.w - 15.0f) * 0.5f);
                cursorY = std::floor(n.ay + dy + (n.h - 15.0f) * 0.5f);
                cursorValid = true;
            });
            cursorValid = false;
        }
        lastSignature = 0;
        lastRcss.clear();
        animCache.clear();
        animHost.recs.clear();
        animHost.destroyed.clear();
        rebuilds++;
        Log("ui", "hud built: {} controls from {} files", report.nodes, library.FileCount());
        return true;
    }

    std::string Hud::ReadUserRcss() const {
        std::ifstream f(dataDir + "/ui/hud.rcss", std::ios::binary);
        if (!f)
            return {};
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    int Hud::Update(const HudState& incoming, int fbW, int fbH, int guiScaleOption) {
        if (!backend.Ready())
            return 1;
        const uint64_t incomingSignature = HudSignature(incoming);
        const bool stateChanged = incomingSignature != lastIncomingSignature;
        lastIncomingSignature = incomingSignature;
        if (stateChanged)
            state = incoming;

        scale = JavaGuiScale(fbW, fbH, guiScaleOption);
        const float newW = std::floor(static_cast<float>(fbW) / scale),
                    newH = std::floor(static_cast<float>(fbH) / scale);
        const bool resized = newW != guiW || newH != guiH || fbW != lastFbW || fbH != lastFbH;
        guiW = newW;
        guiH = newH;
        state.guiW = guiW;
        state.guiH = guiH;
        if (resized)
            lastIncomingSignature = 0;
        lastFbW = fbW;
        lastFbH = fbH;

        const double now = Now();
        bool rebuilt = false;
        if (reloadRequested || now - sinceCheck > 3.0) {
            sinceCheck = now;
            std::string freshRcss = ReadUserRcss();
            if (freshRcss != rcssCache) {
                rcssCache = std::move(freshRcss);
                lastSignature = 0;
            }
            bool libraryChanged = false;
            if (reloadRequested) {
                if (probeLaunched) {
                    changeProbe.wait();
                    probeLaunched = false;
                }
            } else if (probeLaunched) {
                if (changeProbe.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    libraryChanged = changeProbe.get();
                    probeLaunched = false;
                }
            } else {
                changeProbe = std::async(std::launch::async, [this] { return library.Changed(); });
                probeLaunched = true;
            }
            if (reloadRequested || libraryChanged) {
                reloadRequested = false;
                UiAssets::Instance().ClearItemCache();
                UiAssets::Instance().LoadFont(dataDir);
                backend.ReloadTextures();
                Rebuild();
                rebuilt = true;
            }
        }
        if (!tree)
            return scale;
        if (now - templateCheck > 2.0 || templatesStale) {
            templateCheck = now;
            uint64_t vh = 0, vc = 0;
            std::string hudT = LoadUiTemplate(dataDir, "hud_screen.rml", DefaultHudTemplate(), vh);
            std::string chatT = LoadUiTemplate(dataDir, "chat_screen.rml", DefaultChatTemplate(), vc);
            if (vh != hudTemplateVersion || vc != chatTemplateVersion || templatesStale) {
                hudTemplateVersion = vh;
                chatTemplateVersion = vc;
                emitter.SetElements(ParseElementMap(hudT));
                emitter.SetTemplates(std::move(hudT), std::move(chatT));
                lastSignature = 0;
                templatesStale = false;
                Log("ui", "loaded {}/ui/hud_screen.rml and chat_screen.rml", dataDir);
            }
        }

        if (crosshairTree && (!cursorValid || resized || rebuilt)) {
            cursorValid = false;
            UiServices crossServices = services;
            crossServices.anim = nullptr;
            auto crossEvaluated = EvaluateTree(*crosshairTree, *data, crossServices, guiW, guiH);
            cursorEmitter.Emit(*crossEvaluated, guiW, guiH, std::string(), false);
        }
        if (!cursorValid) {
            cursorX = std::floor((guiW - 15.0f) * 0.5f);
            cursorY = std::floor((guiH - 15.0f) * 0.5f);
            cursorValid = true;
        }

        animHost.now = now;
        const bool animTick = animHost.animating && now - lastAnimEval >= 1.0 / 60.0;
        const std::string& rcss = rcssCache;
        const uint64_t signature = stateChanged || resized || rebuilt ? HudSignature(state) : lastSignature;
        if (!rebuilt && !resized && !animTick && signature == lastSignature && rcss == lastRcss &&
            fontVersion == McFont::Instance().Version() && filterVersion == HudFilterVersion()) {
            backend.Update();
            return scale;
        }
        lastSignature = signature;
        lastAnimEval = now;
        lastRcss = rcss;
        fontVersion = McFont::Instance().Version();
        filterVersion = HudFilterVersion();

        auto evaluated = EvaluateTree(*tree, *data, services, guiW, guiH);
        lastRml = emitter.Emit(*evaluated, guiW, guiH, rcss, false);
        backend.Resize(static_cast<int>(guiW), static_cast<int>(guiH), scale);
        std::string error;
        if (!backend.SetDocument(lastRml, error)) {
            Log("ui", "{}", error);
            if (!emitterDefault) {
                emitterDefault = true;
                emitter.SetTemplates(std::string(), std::string());
                lastRml = emitter.Emit(*evaluated, guiW, guiH, std::string(), false);
                backend.SetDocument(lastRml, error);
            }
        } else {
            emitterDefault = false;
        }
        if (!dumped || rebuilt) {
            dumped = true;
            Write(fs::path(dataDir) / "ui_generated" / "hud.rml", lastRml);
            for (const std::string& u : emitter.UnknownRenderers())
                Log("ui", "custom renderer not implemented: {}", u);
        }
        backend.Update();
        return scale;
    }

    void Hud::UpdateOverlay(int fbW, int fbH, int guiScaleOption) {
        if (!backend.Ready())
            return;
        const int s = JavaGuiScale(fbW, fbH, guiScaleOption);
        backend.Resize(static_cast<int>(std::floor(static_cast<float>(fbW) / static_cast<float>(s))),
                       static_cast<int>(std::floor(static_cast<float>(fbH) / static_cast<float>(s))), s);
        backend.Update();
    }

    void Hud::Render() {
        if (!backend.Ready() || (!tree && !backend.HasOverlay()))
            return;
        backend.Render();
    }
}
