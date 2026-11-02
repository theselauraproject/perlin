#include "client/PerlinInternal.hpp"
#include "util/Log.hpp"
#include "client/update/Updater.hpp"

namespace perlin {
    namespace {
        std::string SkinDollKey(const std::string& skinPath, const std::string& capePath, bool showCape,
                                float headYaw = 0.0f, float headPitch = 0.0f);
        // sample data for PERLIN_SHOW_SCREEN: check screens without joining a world
        ui::ScreenData ForcedScreenData(const std::string& reference, const std::string& playerName) {
            if (reference == "server_form.third_party_server_screen") {
                // long form shaped like a game selector: title flag + many buttons
                ui::ScreenData d;
                // PERLIN_TEST_FORM=<file>: replay a captured form (ui_generated/last_form.json) instead of the sample
                if (const char* file = PERLIN_ENV("PERLIN_TEST_FORM")) {
                    std::ifstream in(file, std::ios::binary);
                    std::stringstream ss;
                    ss << in.rdbuf();
                    ui::Json form;
                    std::string err;
                    if (ui::Json::Parse(ss.str(), form, err) && form.IsObject()) {
                        d.Set("#title_text", ui::Json(form.Find("title") ? form.Find("title")->StringOr("")
                                                                         : std::string()));
                        d.Set("#form_text", ui::Json(form.Find("content") ? form.Find("content")->StringOr("")
                                                                          : std::string()));
                        const ui::Json* list =
                            form.Find("elements") ? form.Find("elements") : form.Find("buttons");
                        int i = 0;
                        if (list && list->IsArray())
                            for (const ui::Json& e : list->array) {
                                const ui::Json* image = e.Find("image");
                                const ui::Json* data = image ? image->Find("data") : nullptr;
                                d.Collection("form_buttons")
                                    .push_back({{"#form_button_text",
                                                 ui::Json(e.Find("text") ? e.Find("text")->StringOr("")
                                                                         : std::string())},
                                                {"#form_button_texture",
                                                 ui::Json(data ? data->StringOr("") : std::string())}});
                                d.FactoryItems("buttons").push_back({"button", {}, "form_buttons", i++});
                            }
                        d.FactoryItems("server_form_factory").push_back({"long_form", {}, {}, -1});
                        return d;
                    }
                }
                const char* title = PERLIN_ENV("PERLIN_TEST_FORM_TITLE");
                d.Set("#title_text", std::string(title ? title : "&game_selectorSelect a game"));
                d.Set("#form_text", std::string(""));
                static const char* kGames[] = {"Lobby",         "SkyWars",        "EggWars",      "Bed Wars",
                                               "Tower Defence", "Lucky Islands",  "Free For All", "Parkour",
                                               "Block Wars",    "Survival Games", "Duels",        "Skyblock"};
                for (int i = 0; i < 30; i++) {
                    const std::string name = kGames[i % 12];
                    d.Collection("form_buttons")
                        .push_back({{"#form_button_text",
                                     ui::Json(name + "\n" + std::to_string(100 + i * 37) + " playing")},
                                    {"#form_button_texture", ui::Json(std::string())}});
                    d.FactoryItems("buttons").push_back({"button", {}, "form_buttons", i});
                }
                d.FactoryItems("server_form_factory").push_back({"long_form", {}, {}, -1});
                return d;
            }
            if (reference == "hud.hud_screen") {
                // sidebar sample shaped like a lobby board, private-use glyphs included
                ui::ScreenData d;
                d.Set("#hud_visible", true);
                d.Set("#scoreboard_sidebar_visible", true);
                d.Set("#objective_sidebar_name", std::string(PERLIN_ENV("PERLIN_TEST_SIDEBAR_TITLE")
                                                                 ? PERLIN_ENV("PERLIN_TEST_SIDEBAR_TITLE")
                                                                 : "CubeCraft"));
                const std::vector<std::pair<std::string, int>> rows = {
                    {"\u00a7bUser: \u00a7f" + playerName, 8},
                    {"\u00a7bVIP Level: \u00a7f15", 7},
                    {"\u00a7bLevel: \u00a7f17", 6},
                    {"\u00a7bLobby: \u00a7fEU #11", 5},
                    {"\u00a7bPlayers: \u00a7f7,183", 4},
                    {" ", 3},
                    {"\u00a77"
                     "28/10/26 (de0ax)",
                     2},
                    {"\u00a76cubecraft.net", 1}};
                d.Set("#scoreboard_sidebar_size", static_cast<double>(rows.size()));
                for (const auto& [name, score] : rows) {
                    d.Collection("scoreboard_players").push_back({{"#player_name_sidebar", ui::Json(name)}});
                    d.Collection("scoreboard_scores")
                        .push_back({{"#player_score_sidebar", ui::Json(std::to_string(score))}});
                }
                return d;
            }
            if (reference == "pause.pause_screen") {
                ui::PauseModel pm;
                pm.playerName = playerName;
                pm.listTitle = "Player List";
                pm.players = {{playerName, 2, true}, {"Steve", 1, false}, {"Alex", 0, false}};
                return ui::PauseScreenData(pm);
            }
            return {};
        }
        std::string ServersPath() {
            return paths::DataDir() + "/servers.json";
        }
        void LoadServers(std::vector<ui::ServerEntry>& out, const std::string& fallback) {
            out.clear();
            std::ifstream in(ServersPath(), std::ios::binary);
            if (!in) {
                if (!fallback.empty() && fallback != "127.0.0.1:19132")
                    out.push_back({"Minecraft Server", fallback});
                return;
            }
            std::stringstream ss;
            ss << in.rdbuf();
            ui::Json json;
            std::string error;
            if (!ui::Json::Parse(ss.str(), json, error))
                return;
            const ui::Json* list = json.Find("servers");
            if (!list || !list->IsArray())
                return;
            for (const ui::Json& entry : list->array) {
                const ui::Json* name = entry.Find("name");
                const ui::Json* ip = entry.Find("ip");
                if (ip && ip->IsString() && !ip->string.empty())
                    out.push_back({name ? name->StringOr("") : std::string(), ip->string});
            }
        }
        void SaveServers(const std::vector<ui::ServerEntry>& servers) {
            ui::Json list = ui::Json::MakeArray();
            for (const ui::ServerEntry& s : servers) {
                ui::Json entry = ui::Json::MakeObject();
                entry.Set("name", ui::Json(s.name));
                entry.Set("ip", ui::Json(s.address));
                list.Push(std::move(entry));
            }
            ui::Json root = ui::Json::MakeObject();
            root.Set("servers", std::move(list));
            std::error_code ec;
            std::filesystem::create_directories(paths::DataDir(), ec);
            std::ofstream(ServersPath(), std::ios::binary) << root.Dump(2);
        }
        // renders the skin as bedrock's paper doll (3d, facing slightly left) into a ui texture once per skin/cape;
        // returns its key for the json ui paper_doll_renderer
        std::string SkinDollKey(const std::string& skinPath, const std::string& capePath, bool showCape,
                                float headYaw, float headPitch) {
            // angles snap to 3 degree steps: a moving cursor reuses renders; only the latest kept
            const int qy = static_cast<int>(std::lround(headYaw / 3.0f)) * 3,
                      qp = static_cast<int>(std::lround(headPitch / 3.0f)) * 3;
            const std::string key =
                "doll:" +
                std::to_string(std::hash<std::string>{}(skinPath + "|" + capePath + (showCape ? "c" : "n"))) +
                ":" + std::to_string(qy) + ":" + std::to_string(qp);
            ui::UiAssets& assets = ui::UiAssets::Instance();
            static std::string lastKey;
            if (assets.HasHead(key))
                return key;
            if (!lastKey.empty())
                assets.EraseHead(lastKey);
            lastKey = key;
            LoadedSkin loaded;
            if (!skinPath.empty()) {
                SkinConfig cfg;
                cfg.skinPath = skinPath;
                cfg.capePath = capePath;
                std::string err;
                if (!LoadSkinConfig(cfg, loaded, err))
                    loaded = {};
            }
            Texture steve;
            if (loaded.skinRgba.empty()) {
                if (!assets.Image("textures/entity/steve.png", steve) || steve.width < 64)
                    return {};
            }
            ui::SkinView v;
            v.skin = loaded.skinRgba.empty() ? &steve.pixels : &loaded.skinRgba;
            v.skinW = loaded.skinRgba.empty() ? steve.width : loaded.skinWidth;
            v.skinH = loaded.skinRgba.empty() ? steve.height : loaded.skinHeight;
            v.slim = ui::GuessSlimSkin(*v.skin, v.skinW, v.skinH);
            if (showCape && !loaded.capeRgba.empty()) {
                v.cape = &loaded.capeRgba;
                v.capeW = loaded.capeWidth;
                v.capeH = loaded.capeHeight;
            }
            v.yaw = -20.0f;
            v.pitch = 0.0f;
            v.headYaw = static_cast<float>(qy);
            v.headPitch = static_cast<float>(qp);
            assets.SetHead(key, ui::RenderSkin3D(v, 180, 300));
            return key;
        }
        std::string SkinHeadKey(const std::string& skinPath) {
            const std::string key = "skinhead:" + std::to_string(std::hash<std::string>{}(skinPath));
            ui::UiAssets& assets = ui::UiAssets::Instance();
            if (assets.HasHead(key))
                return key;
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            if (!skinPath.empty()) {
                SkinConfig cfg;
                cfg.skinPath = skinPath;
                LoadedSkin loaded;
                std::string err;
                if (LoadSkinConfig(cfg, loaded, err)) {
                    rgba = std::move(loaded.skinRgba);
                    w = loaded.skinWidth;
                    h = loaded.skinHeight;
                }
            }
            if (rgba.empty()) {
                Texture steve;
                if (!assets.Image("textures/entity/steve.png", steve) || steve.width < 64)
                    return {};
                rgba = std::move(steve.pixels);
                w = steve.width;
                h = steve.height;
            }
            const int sc = std::max(1, w / 64);
            Texture t;
            t.width = t.height = 8;
            t.pixels.assign(8 * 8 * 4, 255);
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    const auto px = [&](int ox, int oy, uint8_t out[4]) {
                        const size_t i =
                            (static_cast<size_t>((oy + y) * sc + sc / 2) * static_cast<size_t>(w) +
                             static_cast<size_t>((ox + x) * sc + sc / 2)) *
                            4;
                        for (int c = 0; c < 4; c++)
                            out[c] = i + 3 < rgba.size() ? rgba[i + c] : 255;
                    };
                    uint8_t face[4], hat[4];
                    px(8, 8, face);
                    px(40, 8, hat);
                    if (h < 32)
                        hat[3] = 0;
                    uint8_t* o = &t.pixels[static_cast<size_t>(y * 8 + x) * 4];
                    const float a = hat[3] / 255.0f;
                    for (int c = 0; c < 3; c++)
                        o[c] = static_cast<uint8_t>(face[c] * (1.0f - a) + hat[c] * a);
                    o[3] = 255;
                }
            assets.SetHead(key, std::move(t));
            return key;
        }
    }

    void Perlin::LeaveToServerList(ui::Hud& hud) {
        Log("perlin", "left {}, back to the server list", options.server);
        FinishReplayRecording("you left the server");
        connectGeneration++;
        if (account)
            account->Cancel();
        SetStatusHint({});
        sessionReady = false;
        if (session) {
            if (connectsInFlight.load() == 0 && !session->Transferring()) {
                session->Disconnect();
                session->SetFriendLease(nullptr);
            }
            retiredSessions.push_back(std::move(session));
        }
        ReleaseFriendLease();
        particleLevel = nullptr;
        hud.Overlay().ClearOverlay();
        disconnect.shown = disconnect.textMode = disconnect.leftDown = false;
        disconnect.signature.clear();
        loading.shown = loading.textMode = loading.leftDown = false;
        loading.signature.clear();
        loading.startAt = {};
        loading.readyArmed = false;
        pause.shown = inventory.shown = death.shown = form.shown = false;
        death.textMode = false;
        pause.signature.clear();
        death.signature.clear();
        if (chat.IsOpen())
            chat.Close();
        levelUnloadPending = true;
        titleScreen.active = true;
        titleScreen.returnToServers = !inSingleplayer;
        inSingleplayer = false;
    }

    void Perlin::DrawTitlePanorama(int fbW, int fbH) {
        const auto now = Clock::now();
        const uint64_t packStamp =
            (TexturePack::Instance().Generation() << 32) ^ ServerPacks::Instance().Generation();
        if (packStamp != panorama.packStamp) {
            panorama.packStamp = packStamp;
            for (uint32_t& id : panorama.textures) {
                if (id)
                    renderer->UiDestroyTexture(id);
                id = 0;
            }
            panorama.ready = false;
            panorama.retryAt = {};
        }
        if (!panorama.ready) {
            if (now < panorama.retryAt)
                return;
            panorama.retryAt = now + std::chrono::seconds(2);
            int loaded = 0;
            for (int i = 0; i < 6; i++) {
                if (panorama.textures[i])
                    continue;
                Texture t;
                if (ui::UiAssets::Instance().Image("textures/ui/panorama_" + std::to_string(i), t) &&
                    t.width > 0)
                    panorama.textures[i] =
                        renderer->UiCreateTexture(t.width, t.height, t.pixels.data(), true);
            }
            for (uint32_t id : panorama.textures)
                loaded += id != 0;
            panorama.ready = loaded == 6;
            if (!panorama.ready)
                return;
            panorama.clock = now;
        }
        const double seconds = std::chrono::duration<double>(now - panorama.clock).count();
        ui::PanoramaFace faces[6];
        ui::BuildPanorama(ui::PanoramaViewAt(seconds), fbW, fbH, faces);
        std::vector<UiVertex> vertices;
        for (int i = 0; i < 6; i++) {
            const ui::PanoramaFace& f = faces[i];
            if (f.indices.empty())
                continue;
            vertices.clear();
            vertices.reserve(f.vertices.size());
            for (const ui::PanoramaVertex& v : f.vertices)
                vertices.push_back({v.x, v.y, 0.0f, 0xFFFFFFFFu, v.u, v.v});
            const uint32_t mesh =
                renderer->UiCreateFrameMesh(vertices.data(), static_cast<uint32_t>(vertices.size()),
                                            f.indices.data(), static_cast<uint32_t>(f.indices.size()));
            if (!mesh)
                continue;
            UiDraw draw;
            draw.mesh = mesh;
            draw.texture = panorama.textures[i];
            draw.premultiplied = true;
            renderer->UiSubmit(UiLayer::Gui, draw);
            renderer->UiDestroyMesh(mesh);
        }
    }

    bool Perlin::UpdateAssetScreen(ui::Hud& hud, int fbW, int fbH) {
        assets::Job* jobs[2] = {&assets::Samples(), &assets::Client()};
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_DOWNLOAD") != nullptr;
        bool busy = false, failed = false;
        for (assets::Job* j : jobs) {
            busy = busy || assets::Busy(*j);
            failed = failed || j->stage.load() == assets::Stage::Failed;
        }
        if (failed && PERLIN_ENV("PERLIN_PREVIEW_TITLE"))
            asset.skip = true;
        if (asset.skip || (!busy && !failed && !preview)) {
            if (asset.shown) {
                asset.shown = false;
                asset.signature.clear();
                hud.Overlay().ClearOverlay();
                titleScreen.shown = false;
            }
            return false;
        }
        const auto now = Clock::now();
        if (!asset.shown) {
            asset.shown = true;
            asset.openedAt = now;
            asset.sampleAt = now;
            asset.leftDown = true;
            asset.signature.clear();
            for (int i = 0; i < 2; i++) {
                asset.lastBytes[i] = jobs[i]->bytes.done.load();
                asset.speed[i] = 0.0;
            }
            input.SetTextMode(true);
        }
        const double dt = std::chrono::duration<double>(now - asset.sampleAt).count();
        if (dt >= 0.25) {
            for (int i = 0; i < 2; i++) {
                const uint64_t b = jobs[i]->bytes.done.load();
                const double inst =
                    b >= asset.lastBytes[i] ? static_cast<double>(b - asset.lastBytes[i]) / dt : 0.0;
                asset.speed[i] = asset.speed[i] * 0.6 + inst * 0.4;
                asset.lastBytes[i] = b;
            }
            asset.sampleAt = now;
        }

        const auto mb = [](uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); };
        ui::DownloadScreenText t;
        t.title = "Downloading game assets";
        t.subtitle = "This only happens the first time Perlin starts.";
        const char* labels[2] = {"Minecraft resources", "Fonts and menu art"};
        int indeterminate = 0;
        for (int i = 0; i < 2; i++) {
            assets::Job& j = *jobs[i];
            ui::DownloadRow row;
            row.label = labels[i];
            char buf[128];
            const uint64_t total = j.bytes.total.load(), done = j.bytes.done.load();
            switch (preview ? assets::Stage::Downloading : j.stage.load()) {
            case assets::Stage::Idle:
            case assets::Stage::Checking:
                row.status = "Connecting...";
                indeterminate++;
                break;
            case assets::Stage::Downloading:
                if (preview) {
                    std::snprintf(buf, sizeof(buf), "%.1f / %.1f MB  -  %.1f MB/s", 61.3, 147.0, 24.8);
                    row.status = buf;
                    row.progress = 61.3f / 147.0f;
                } else if (total > 0) {
                    std::snprintf(buf, sizeof(buf), "%.1f / %.1f MB  -  %.1f MB/s", mb(done), mb(total),
                                  mb(static_cast<uint64_t>(asset.speed[i])));
                    row.status = buf;
                    row.progress = static_cast<float>(static_cast<double>(done) / static_cast<double>(total));
                } else if (j.filesTotal.load() > 0) {
                    std::snprintf(buf, sizeof(buf), "%llu / %llu files",
                                  static_cast<unsigned long long>(j.filesDone.load()),
                                  static_cast<unsigned long long>(j.filesTotal.load()));
                    row.status = buf;
                    row.progress =
                        static_cast<float>(j.filesDone.load()) / static_cast<float>(j.filesTotal.load());
                } else {
                    row.status = "Downloading...";
                    indeterminate++;
                }
                break;
            case assets::Stage::Extracting:
                if (j.filesTotal.load() > 0) {
                    std::snprintf(buf, sizeof(buf), "Unpacking %llu / %llu files",
                                  static_cast<unsigned long long>(j.filesDone.load()),
                                  static_cast<unsigned long long>(j.filesTotal.load()));
                    row.status = buf;
                    row.progress =
                        static_cast<float>(j.filesDone.load()) / static_cast<float>(j.filesTotal.load());
                } else {
                    row.status = "Unpacking...";
                    indeterminate++;
                }
                break;
            case assets::Stage::Done:
                row.status = "Done";
                row.progress = 1.0f;
                break;
            case assets::Stage::Failed:
                row.status = "Failed";
                row.progress = 1.0f;
                row.failed = true;
                break;
            }
            t.rows.push_back(row);
        }
        if (failed && !busy) {
            t.subtitle = "Some downloads failed. Check your internet connection.";
            t.retry = "Retry";
            t.skip = "Continue anyway";
        }

        const auto [scale, guiW, guiH] = detail::OverlayViewport(hud, fbW, fbH);
        const double time = std::chrono::duration<double>(now - asset.openedAt).count();
        std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "|" + t.subtitle;
        for (const ui::DownloadRow& r : t.rows)
            signature += "|" + r.status + "|" + std::to_string(static_cast<int>(r.progress * 1000.0f));
        if (indeterminate)
            signature += "|" + std::to_string(static_cast<int>(time * 30.0));
        if (signature != asset.signature || !hud.Overlay().HasOverlay()) {
            asset.signature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildDownloadScreenRml(t, guiW, guiH, time), error))
                Log("ui", "download screen: {}", error);
        }
        const glm::vec2 pointer = detail::PointerOrPreview(input);
        float mx = pointer.x, my = pointer.y;
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (!down && asset.leftDown && now - asset.openedAt > std::chrono::milliseconds(300)) {
            const std::string hover = hud.Overlay().OverlayHover();
            if (hover == ui::kDownloadRetryId) {
                Log("ui", "download screen: retry");
                StartAssetDownloads();
            } else if (hover == ui::kDownloadSkipId) {
                asset.skip = true;
            }
        }
        asset.leftDown = down;
        return true;
    }

    bool Perlin::UpdateTitleScreen(ui::Hud& hud, int fbW, int fbH) {
        const auto now = Clock::now();
        if (titleScreen.active && titleScreen.options) {
            if (titleScreen.shown) {
                hud.Overlay().ClearOverlay();
                titleScreen.shown = false;
                titleScreen.signature.clear();
            }
            DrawTitlePanorama(fbW, fbH);
            return false;
        }
        if (!titleScreen.active) {
            if (titleScreen.shown) {
                hud.Overlay().ClearOverlay();
                titleScreen.shown = false;
                titleScreen.signature.clear();
                if (!chat.IsOpen())
                    input.SetTextMode(false);
            }
            titleScreen.leftDown = false;
            return false;
        }
        if (UpdateAssetScreen(hud, fbW, fbH))
            return true;
        if (!titleScreen.shown) {
            titleScreen.shown = true;
            titleScreen.openedAt = now;
            titleScreen.leftDown = true;
            titleScreen.signature.clear();
            title.page = ui::TitlePage::Main;
            title.selected = -1;
            title.scroll = 0;
            LoadServers(title.servers, options.server);
            title.version = std::string("Perlin ") + PERLIN_VERSION + " (" + update::BuildLabel() + ")";
            if (titleScreen.returnToServers) {
                titleScreen.returnToServers = false;
                title.page = ui::TitlePage::Servers;
                title.tab = ui::ServerTab::Saved;
                for (size_t i = 0; i < title.servers.size(); i++)
                    if (title.servers[i].address == options.server)
                        title.selected = static_cast<int>(i) + title.savedOffset;
            }
            if (titleScreen.returnToReplays) {
                titleScreen.returnToReplays = false;
                title.page = ui::TitlePage::Replays;
                title.replaySelected = -1;
                title.replayScroll = 0;
                RefreshReplayList();
            }
            title.copyright = "Not affiliated with Mojang AB.";
            if (const char* p = PERLIN_ENV("PERLIN_PREVIEW_TITLE")) {
                const std::string page = p;
                if (page == "servers" || page == "edit" || page == "direct") {
                    if (title.servers.empty())
                        title.servers = {{"Fozzy Minecraft Server", "23.109.60.29:25565"},
                                         {"Minecraft Server", "play.example.net:19132"}};
                    title.page = page == "servers" ? ui::TitlePage::Servers
                                 : page == "edit"  ? ui::TitlePage::Edit
                                                   : ui::TitlePage::Direct;
                    title.selected = 0;
                    title.name = title.servers[0].name;
                    title.address = title.servers[0].address;
                    title.focus = 1;
                } else if (page == "accounts" && accounts.Entries().empty()) {
                    accounts.AddMicrosoft("Steve4821", "accounts/preview.json");
                    accounts.AddOffline("Alex");
                    accounts.AddOffline("BuilderBob");
                    title.page = ui::TitlePage::Accounts;
                } else if (page == "accounts") {
                    title.page = ui::TitlePage::Accounts;
                } else if (page == "mods" || page == "wardrobe") {
                    titleScreen.openView = page == "mods" ? 4 : 5;
                    titleScreen.options = true;
                }
            }
            input.SetTextMode(true);
        }

        const auto [scale, guiW, guiH] = detail::OverlayViewport(hud, fbW, fbH);

        const auto commit = [&] {
            if (title.address.empty())
                return;
            if (title.page == ui::TitlePage::Direct) {
                JoinServer(title.address);
                return;
            }
            if (title.page == ui::TitlePage::AddRealm) {
                AddRealm(title.address);
                return;
            }
            const ui::ServerEntry entry{title.name.empty() ? "Minecraft Server" : title.name, title.address};
            if (title.adding || title.SavedIndex() < 0) {
                title.servers.push_back(entry);
                title.selected = static_cast<int>(title.servers.size()) - 1 + title.savedOffset;
            } else {
                title.servers[static_cast<size_t>(title.SavedIndex())] = entry;
            }
            SaveServers(title.servers);
            title.page = ui::TitlePage::Servers;
            title.tab = ui::ServerTab::Saved;
            {
                std::lock_guard lock(pingMutex);
                pingResults.erase(entry.address);
            }
            PingServers({entry.address});
        };
        const auto join = [&](int row) {
            const std::vector<ui::ServerEntry>& list = title.Shown();
            if (row < 0 || row >= static_cast<int>(list.size()))
                return;
            const ui::ServerEntry& e = list[static_cast<size_t>(row)];
            switch (e.kind) {
            case ui::ServerEntry::Kind::Header:
                break;
            case ui::ServerEntry::Kind::Realm:
                JoinRealm(e.realmId);
                break;
            case ui::ServerEntry::Kind::Friend:
                JoinFriend(e);
                break;
            case ui::ServerEntry::Kind::Lan:
                if (e.xuid.starts_with("lan:"))
                    JoinLanGame(e);
                else
                    JoinServer(e.address);
                break;
            default:
                JoinServer(e.address);
                break;
            }
        };
        const auto back = [&] {
            switch (title.page) {
            case ui::TitlePage::Servers:
                title.page = ui::TitlePage::Main;
                break;
            case ui::TitlePage::Edit:
            case ui::TitlePage::Direct:
            case ui::TitlePage::AddRealm:
                title.page = ui::TitlePage::Servers;
                break;
            case ui::TitlePage::Accounts:
                title.page = ui::TitlePage::Main;
                break;
            case ui::TitlePage::Replays:
                title.page = ui::TitlePage::Main;
                break;
            case ui::TitlePage::ReplayRename:
            case ui::TitlePage::ReplayDelete:
                title.page = ui::TitlePage::Replays;
                break;
            case ui::TitlePage::AccountName:
                title.page = ui::TitlePage::Accounts;
                break;
            case ui::TitlePage::AccountSignIn:
                CancelAccountSignIn();
                title.page = ui::TitlePage::Accounts;
                break;
            default:
                break;
            }
        };
        const auto addOffline = [&] {
            std::string name = title.accountName;
            while (!name.empty() && name.back() == ' ')
                name.pop_back();
            if (name.empty())
                return;
            accounts.AddOffline(name);
            ApplyActiveAccount();
            title.page = ui::TitlePage::Accounts;
        };
        const auto edit = [&](bool adding) {
            title.adding = adding;
            title.page = ui::TitlePage::Edit;
            title.focus = 0;
            if (adding) {
                title.name = "Minecraft Server";
                title.address.clear();
            } else {
                const ui::ServerEntry& s =
                    title.servers[static_cast<size_t>(std::max(0, title.SavedIndex()))];
                title.name = s.name;
                title.address = s.address;
            }
        };

        for (const auto& t : inputManager.TakeTyped()) {
            if (!titleScreen.active)
                break;
            const bool fields = title.page == ui::TitlePage::Edit || title.page == ui::TitlePage::Direct ||
                                title.page == ui::TitlePage::AddRealm;
            if (!t.character && t.key == Key::Escape) {
                back();
            } else if (title.page == ui::TitlePage::Replays || title.page == ui::TitlePage::ReplayRename ||
                       title.page == ui::TitlePage::ReplayDelete) {
                TitleReplayTyped(t);
            } else if (title.page == ui::TitlePage::AccountName) {
                ui::TextLimits limits{16};
                limits.accept = [](uint32_t cp) { return cp != ' '; };
                if (!t.character && t.key == Key::Enter)
                    addOffline();
                else
                    ui::TextEdit::Focused().Apply(t, title.accountName, limits);
            } else if (fields) {
                std::string& field = title.page == ui::TitlePage::Direct ||
                                             title.page == ui::TitlePage::AddRealm || title.focus == 1
                                         ? title.address
                                         : title.name;
                if (!t.character && t.key == Key::Tab && title.page == ui::TitlePage::Edit)
                    title.focus ^= 1;
                else if (!t.character && t.key == Key::Enter)
                    commit();
                else
                    ui::TextEdit::Focused().Apply(t, field, ui::TextLimits{120});
            } else if (title.page == ui::TitlePage::Servers) {
                const int count = static_cast<int>(title.Shown().size());
                if (!t.character && t.key == Key::Enter)
                    join(title.selected);
                else if (!t.character && t.key == Key::Down && count)
                    title.selected = std::min(count - 1, title.selected + 1);
                else if (!t.character && t.key == Key::Up && count)
                    title.selected = std::max(0, title.selected - 1);
            }
        }
        if (!titleScreen.active)
            return false;

        if (title.page == ui::TitlePage::Servers) {
            const int rows = ui::TitleVisibleRows(guiH);
            const int maxScroll = std::max(0, static_cast<int>(title.Shown().size()) - rows);
            if (const float wheel = inputManager.Scroll(); wheel != 0.0f)
                title.scroll -= wheel > 0 ? 1 : -1;
            if (title.selected >= 0) {
                title.scroll = std::min(title.scroll, title.selected);
                title.scroll = std::max(title.scroll, title.selected - rows + 1);
            }
            title.scroll = std::clamp(title.scroll, 0, maxScroll);
        }

        if (title.page == ui::TitlePage::Replays) {
            const int rows = ui::TitleVisibleRows(guiH);
            const int maxScroll = std::max(0, static_cast<int>(title.replays.size()) - rows);
            if (const float wheel = inputManager.Scroll(); wheel != 0.0f)
                title.replayScroll -= wheel > 0 ? 1 : -1;
            if (title.replaySelected >= 0) {
                title.replayScroll = std::min(title.replayScroll, title.replaySelected);
                title.replayScroll = std::max(title.replayScroll, title.replaySelected - rows + 1);
            }
            title.replayScroll = std::clamp(title.replayScroll, 0, maxScroll);
        }

        if (title.page == ui::TitlePage::Accounts) {
            const int maxScroll =
                std::max(0, static_cast<int>(title.accounts.size()) - ui::TitleAccountRows(guiH));
            if (const float wheel = inputManager.Scroll(); wheel != 0.0f)
                title.accountScroll -= wheel > 0 ? 1 : -1;
            title.accountScroll = std::clamp(title.accountScroll, 0, maxScroll);
        }

        if (connectsInFlight.load() == 0)
            UnloadServerPacks();

        if (title.page == ui::TitlePage::Main || title.page == ui::TitlePage::Accounts ||
            title.page == ui::TitlePage::AccountName || title.page == ui::TitlePage::AccountSignIn)
            DrawTitlePanorama(fbW, fbH);

        {
            std::lock_guard lock(signIn.mutex);
            for (const auto& [id, gamertag] : signIn.pendingRenames)
                accounts.Rename(id, gamertag);
            signIn.pendingRenames.clear();
            if (signIn.state == 2) {
                accounts.AddMicrosoft(signIn.gamertag, signIn.authFile);
                signIn.account.reset();
                signIn.state = 0;
                ApplyActiveAccount();
                if (title.page == ui::TitlePage::AccountSignIn)
                    title.page = ui::TitlePage::Accounts;
            }
            title.signInUrl = signIn.url;
            title.signInCode = signIn.code;
            title.signInStatus = signIn.status;
        }
        title.accounts.clear();
        for (const AccountEntry& e : accounts.Entries()) {
            const std::string head = SkinHeadKey(
                e.skinKnown ? e.skin : (e.id == accounts.ActiveId() ? options.skin : std::string()));
            title.accounts.push_back({e.id, e.name, e.microsoft, e.id == accounts.ActiveId(), head});
        }
        title.accountHead = SkinHeadKey(options.skin);
        if (const AccountEntry* a = accounts.Active())
            title.accountUser = a->name.empty() ? std::string("Microsoft account") : a->name;
        else
            title.accountUser = options.playerName;
        title.accountLabel = "Logged in as " + title.accountUser;
        title.oreUi = false;
        title.playerName = title.accountUser;
        if (title.oreUi && title.page == ui::TitlePage::Main) {
            const std::string doll = SkinDollKey(options.skin, options.cape, options.skinParts.cape);
            title.dollSrc = doll.empty() ? std::string() : "mchead:" + doll;
        }
        if (title.page == ui::TitlePage::Accounts && titleScreen.lastPage != ui::TitlePage::Accounts &&
            Clock::now() - signIn.gamertagFetchAt > std::chrono::seconds(30) && !PERLIN_ENV("PERLIN_PREVIEW"))
            FetchMissingGamertags();

        if (title.page == ui::TitlePage::Servers && titleScreen.lastPage != ui::TitlePage::Servers &&
            titleScreen.lastPage != ui::TitlePage::Edit && titleScreen.lastPage != ui::TitlePage::Direct &&
            titleScreen.lastPage != ui::TitlePage::AddRealm) {
            std::vector<std::string> addresses;
            addresses.reserve(title.servers.size());
            for (const ui::ServerEntry& e : title.servers)
                addresses.push_back(e.address);
            PingServers(addresses);
            if (!PERLIN_ENV("PERLIN_PREVIEW"))
                RefreshFeatured(false);
        }
        titleScreen.lastPage = title.page;
        {
            std::string joinAddress;
            std::shared_ptr<friendworld::Lease> joinedFriend;
            {
                std::lock_guard lock(tabMutex);
                joinedFriend = std::move(friendLeaseReady);
                title.featured = tabFeatured;
                if (tabDirty) {
                    title.lan = tabLan;
                    title.realms = tabRealms;
                    tabDirty = false;
                }
                {
                    std::vector<ui::ServerEntry> sections[3];
                    for (const ui::ServerEntry& f : tabFriends)
                        sections[f.hosting ? 0 : (f.offline ? 2 : 1)].push_back(f);
                    title.friends.clear();
                    static const char* const kNames[3] = {"Hosting", "Online", "Offline"};
                    if (!tabFriends.empty())
                        for (int i = 0; i < 3; i++) {
                            ui::ServerEntry head;
                            head.kind = ui::ServerEntry::Kind::Header;
                            head.name =
                                std::string(kNames[i]) + " [" + std::to_string(sections[i].size()) + "]";
                            head.realmId = i;
                            title.friends.push_back(head);
                            if (title.friendsOpen[i])
                                title.friends.insert(title.friends.end(), sections[i].begin(),
                                                     sections[i].end());
                        }
                }
                title.tabMessage = title.tab == ui::ServerTab::Lan      ? tabLanMsg
                                   : title.tab == ui::ServerTab::Realms ? tabRealmsMsg
                                                                        : tabFriendsMsg;
                title.joinMessage = tabJoinMsg;
                if (realmJoinReady) {
                    realmJoinReady = false;
                    joinAddress = realmJoinAddress;
                    tabJoinMsg.clear();
                }
            }
            if (joinedFriend) {
                ReleaseFriendLease();
                friendLease = std::move(joinedFriend);
                SetJoinMessage({});
                titleScreen.active = false;
                Connect();
            }
            if (!joinAddress.empty())
                JoinServer(joinAddress);
            if (realmAddedFlag.exchange(false)) {
                title.page = ui::TitlePage::Servers;
                title.tab = ui::ServerTab::Realms;
                title.selected = -1;
                RefreshTab(ui::ServerTab::Realms);
            }
        }
        {
            std::lock_guard lock(pingMutex);
            for (ui::ServerEntry& e : title.servers) {
                const auto it = pingResults.find(e.address);
                const ui::ServerEntry blank{};
                const ui::ServerEntry& r = it == pingResults.end() ? blank : it->second;
                e.status = r.status;
                e.motd = r.motd;
                e.version = r.version;
                e.players = r.players;
                e.maxPlayers = r.maxPlayers;
                e.pingMs = r.pingMs;
            }
        }
        if (featuredPing.exchange(false)) {
            std::vector<std::string> addresses;
            {
                std::lock_guard lock(pingMutex);
                for (const ui::ServerEntry& e : title.featured) {
                    pingResults.erase(e.address);
                    addresses.push_back(e.address);
                }
            }
            if (!addresses.empty())
                PingServers(addresses);
        }
        {
            std::lock_guard lock(pingMutex);
            for (ui::ServerEntry& e : title.featured) {
                const auto it = pingResults.find(e.address);
                const ui::ServerEntry blank{};
                const ui::ServerEntry& r = it == pingResults.end() ? blank : it->second;
                e.status = r.status;
                e.version = r.version;
                e.players = r.players;
                e.maxPlayers = r.maxPlayers;
                e.pingMs = r.pingMs;
            }
        }
        {
            title.savedView.clear();
            if (!title.featured.empty()) {
                ui::ServerEntry head;
                head.kind = ui::ServerEntry::Kind::Header;
                head.name = "Featured Servers [" + std::to_string(title.featured.size()) + "]";
                head.realmId = 3;
                title.savedView.push_back(head);
                if (title.friendsOpen[3])
                    title.savedView.insert(title.savedView.end(), title.featured.begin(),
                                           title.featured.end());
            }
            const int offset = static_cast<int>(title.savedView.size());
            if (offset != title.savedOffset) {
                if (title.tab == ui::ServerTab::Saved && title.selected >= title.savedOffset)
                    title.selected += offset - title.savedOffset;
                title.savedOffset = offset;
            }
            title.savedView.insert(title.savedView.end(), title.servers.begin(), title.servers.end());
        }
        title.nativePanorama = panorama.ready;
        if (!logoBedrock && now >= logoRetryAt) {
            logoRetryAt = now + std::chrono::seconds(2);
            for (const char* path : {"textures/ui/title"}) {
                Texture t;
                if (ui::UiAssets::Instance().Image(path, t) && t.width > 0 && t.height > 0) {
                    logoBedrock = true;
                    title.logoSrc = path;
                    title.logoW = 256;
                    title.logoH = std::max(1, static_cast<int>(256.0 * t.height / t.width + 0.5));
                    if (title.logoH > 64) {
                        title.logoW = std::max(1, static_cast<int>(64.0 * t.width / t.height + 0.5));
                        title.logoH = 64;
                    }
                    break;
                }
            }
        }

        const int64_t ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        title.caret = (ms / 500) % 2 == 0;
        title.spinner = static_cast<int>(ms / 250);
        std::string signature =
            (title.oreUi ? "ore:" + title.dollSrc + "|" : std::string()) + std::to_string(guiW) + "x" +
            std::to_string(guiH) + "|" + std::to_string(static_cast<int>(title.page)) + "|" +
            std::to_string(title.selected) + "|" + std::to_string(title.scroll) + "|" +
            std::to_string(title.focus) + (title.nativePanorama ? "|np" : "|sp") + "|" + title.logoSrc;
        {
            // the update line: progress while downloading, a button once there is something to do
            using update::State;
            const update::Status u = update::Current();
            title.updateAction = u.state == State::Ready || u.state == State::Available;
            title.updateText =
                u.state == State::Downloading
                    ? u.message + " " + std::to_string(static_cast<int>(u.progress * 100.0f)) + "%"
                : title.updateAction || u.state == State::Failed ? u.message
                                                                 : std::string();
        }
        signature += "|" + title.updateText + (title.updateAction ? "!" : "");
        signature += "|" + title.accountLabel + "|" + title.accountUser + "|" + title.accountHead;
        signature += "|" + std::to_string(title.accountScroll);
        if (title.page == ui::TitlePage::Accounts)
            for (const ui::AccountRow& a : title.accounts)
                signature += "|" + a.id + a.name + (a.active ? "*" : "") + a.headKey;
        else if (title.page == ui::TitlePage::AccountName)
            signature += "|" + title.accountName + (title.caret ? "c" : "n");
        else if (title.page == ui::TitlePage::AccountSignIn)
            signature += "|" + title.signInUrl + "|" + title.signInCode + "|" + title.signInStatus;
        if (title.page == ui::TitlePage::Replays || title.page == ui::TitlePage::ReplayRename ||
            title.page == ui::TitlePage::ReplayDelete) {
            signature += "|r" + std::to_string(title.replaySelected) + "|" +
                         std::to_string(title.replayScroll) + "|" + std::to_string(title.replaySort) +
                         (title.replaySortDesc ? "d" : "a") + "|" + title.replayMessage + "|" +
                         title.replayName + (title.caret ? "c" : "n") + "|" +
                         std::to_string(title.replays.size());
            for (const ui::ReplayEntry& r : title.replays)
                signature += "|" + r.file + r.name + r.line1;
        }
        if (title.page == ui::TitlePage::Edit || title.page == ui::TitlePage::Direct ||
            title.page == ui::TitlePage::AddRealm)
            signature += "|" + title.joinMessage + "|" + title.name + "|" + title.address + "|" +
                         (title.caret ? "c" : "n") + (title.adding ? "a" : "e");
        else if (title.page == ui::TitlePage::Servers) {
            signature += "|t" + std::to_string(static_cast<int>(title.tab)) + "|" + title.tabMessage + "|" +
                         title.joinMessage;
            signature += "|" + std::to_string(title.Shown().size()) + (title.friendsOpen[0] ? "H" : "h") +
                         (title.friendsOpen[1] ? "O" : "o") + (title.friendsOpen[2] ? "F" : "f") +
                         (title.friendsOpen[3] ? "S" : "s");
            for (const ui::ServerEntry& s : title.Shown())
                signature += "|" + s.name + "|" + s.address + "|" +
                             std::to_string(static_cast<int>(s.status)) + s.motd + "|" +
                             std::to_string(s.players) + "/" + std::to_string(s.maxPlayers) + "|" +
                             std::to_string(s.pingMs) + s.right + s.note + (s.offline ? "o" : "") +
                             (s.status == ui::ServerEntry::Status::Pinging ? std::to_string(title.spinner & 3)
                                                                           : std::string());
            if (title.Shown().empty())
                signature += "|" + std::to_string(title.spinner & 3);
        }
        // dev hook: PERLIN_SHOW_SCREEN=pause.pause_screen draws any json ui screen here, to check layouts
        const char* forcedScreen = PERLIN_ENV("PERLIN_SHOW_SCREEN");
        ui::ScreenData forcedData;
        if (forcedScreen) {
            ui::JsonScreens& screens = ui::JsonScreens::Instance();
            screens.Warm(ServerPacks::Instance().Directories());
            screens.WaitReady(5000);
            forcedData = ForcedScreenData(forcedScreen, options.playerName);
            float testYaw = 0.0f, testPitch = 0.0f;
            if (const char* head = PERLIN_ENV("PERLIN_DOLL_HEAD"))
                std::sscanf(head, "%f,%f", &testYaw, &testPitch);
            screens.SetPaperDoll(
                SkinDollKey(options.skin, options.cape, options.skinParts.cape, testYaw, testPitch));
            signature = "forced:" + std::string(forcedScreen) + std::to_string(screens.Generation()) +
                        forcedData.Signature();
        }
        if (signature != titleScreen.signature || !hud.Overlay().HasOverlay()) {
            titleScreen.signature = signature;
            std::string error, rml;
            if (forcedScreen) {
                if (!ui::JsonScreens::Instance().Build(forcedScreen, forcedData, guiW, guiH, 0.0f, rml) ||
                    !hud.Overlay().SetOverlay(rml, error))
                    Log("screens", "forced {} failed: {}", forcedScreen, error);
            } else if (!hud.Overlay().SetOverlay(ui::BuildTitleRml(title, guiW, guiH), error)) {
                Log("ui", "title screen: {}", error);
            }
        }
        const glm::vec2 pointer = detail::PointerOrPreview(input);
        float mx = pointer.x, my = pointer.y;
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (!down && titleScreen.leftDown && now - titleScreen.openedAt > std::chrono::milliseconds(150)) {
            const std::string hover = hud.Overlay().OverlayHover();
            const auto rowOf = [&](const char* prefix) {
                const std::string p = prefix;
                return hover.compare(0, p.size(), p) == 0 ? std::atoi(hover.c_str() + p.size()) : -1;
            };
            if (TitleReplayClick(hover)) {
            } else if (hover == ui::kTitleAccounts) {
                title.page = ui::TitlePage::Accounts;
            } else if (hover == ui::kAccountDone) {
                title.page = ui::TitlePage::Main;
            } else if (hover == ui::kAccountAddOffline) {
                title.accountName.clear();
                title.page = ui::TitlePage::AccountName;
            } else if (hover == ui::kAccountAddMicrosoft) {
                StartAccountSignIn();
                title.page = ui::TitlePage::AccountSignIn;
            } else if (hover == ui::kAccountNameDone) {
                addOffline();
            } else if (hover == ui::kAccountCancel) {
                back();
            } else if (const int rm = rowOf(ui::kAccountRemovePrefix); rm >= 0) {
                if (rm < static_cast<int>(title.accounts.size())) {
                    accounts.Remove(title.accounts[static_cast<size_t>(rm)].id);
                    ApplyActiveAccount();
                }
            } else if (const int acc = rowOf(ui::kAccountRowPrefix); acc >= 0) {
                if (acc < static_cast<int>(title.accounts.size()) &&
                    accounts.SetActive(title.accounts[static_cast<size_t>(acc)].id))
                    ApplyActiveAccount();
            } else if (hover == ui::kTitleSingle) {
                JoinSingleplayer();
            } else if (hover == ui::kTitleUpdate) {
                if (update::Act())
                    platform->isRunning = false;
            } else if (hover == ui::kTitleMulti) {
                title.page = ui::TitlePage::Servers;
                title.tab = ui::ServerTab::Lan;
                title.selected = -1;
                title.scroll = 0;
                SetJoinMessage({});
                RefreshTab(title.tab);
            } else if (hover == ui::kTitleOptions) {
                titleScreen.openView = 1;
                titleScreen.options = true;
            } else if (hover == ui::kTitleMods) {
                titleScreen.openView = 4;
                titleScreen.options = true;
            } else if (hover == ui::kTitleWardrobe) {
                titleScreen.openView = 5;
                titleScreen.options = true;
            } else if (hover == ui::kTitleQuit) {
                platform->isRunning = false;
            } else if (hover == ui::kServerCancel || hover == ui::kEditCancel) {
                back();
            } else if (hover == ui::kServerJoin) {
                join(title.selected);
            } else if (hover == ui::kServerDirect) {
                title.page = ui::TitlePage::Direct;
                title.address = options.server;
            } else if (hover == ui::kServerAddRealm) {
                title.page = ui::TitlePage::AddRealm;
                title.address.clear();
                SetJoinMessage({});
            } else if (hover == ui::kServerAdd) {
                edit(true);
            } else if (hover == ui::kServerEdit) {
                edit(false);
            } else if (hover == ui::kServerDelete) {
                if (title.SavedIndex() >= 0) {
                    title.servers.erase(title.servers.begin() + title.SavedIndex());
                    title.selected = -1;
                    SaveServers(title.servers);
                }
            } else if (const int tb = rowOf(ui::kServerTabPrefix); tb >= 0 && tb < 4) {
                title.tab = static_cast<ui::ServerTab>(tb);
                title.selected = -1;
                title.scroll = 0;
                SetJoinMessage({});
                RefreshTab(title.tab);
            } else if (hover == ui::kServerRefresh && title.tab != ui::ServerTab::Saved) {
                SetJoinMessage({});
                RefreshTab(title.tab);
            } else if (hover == ui::kServerRefresh) {
                RefreshFeatured(true);
                LoadServers(title.servers, options.server);
                title.selected = -1;
                std::vector<std::string> addresses;
                {
                    std::lock_guard lock(pingMutex);
                    for (const ui::ServerEntry& e : title.servers) {
                        pingResults.erase(e.address);
                        addresses.push_back(e.address);
                    }
                }
                PingServers(addresses);
            } else if (hover == ui::kEditName) {
                title.focus = 0;
            } else if (hover == ui::kEditAddress) {
                title.focus = 1;
            } else if (hover == ui::kEditDone) {
                commit();
            } else if (const int go = rowOf(ui::kServerJoinPrefix); go >= 0) {
                join(go);
            } else if (const int row = rowOf(ui::kServerRowPrefix); row >= 0) {
                const std::vector<ui::ServerEntry>& shown = title.Shown();
                if (row < static_cast<int>(shown.size()) &&
                    shown[static_cast<size_t>(row)].kind == ui::ServerEntry::Kind::Header) {
                    title.friendsOpen[std::clamp<long long>(shown[static_cast<size_t>(row)].realmId, 0, 3)] ^=
                        true;
                    title.selected = -1;
                } else {
                    if (row == titleScreen.clickRow &&
                        now - titleScreen.clickAt < std::chrono::milliseconds(400))
                        join(row);
                    title.selected = row;
                    titleScreen.clickRow = row;
                    titleScreen.clickAt = now;
                }
            }
        }
        titleScreen.leftDown = down;
        return titleScreen.active || titleScreen.shown;
    }

    bool Perlin::UpdateLoadingScreen(ui::Hud& hud, int fbW, int fbH) {
        if (leaseJoinFailed.exchange(false)) {
            hud.Overlay().ClearOverlay();
            loading.shown = loading.textMode = loading.leftDown = false;
            loading.signature.clear();
            titleScreen.active = true;
            return false;
        }
        if (leaseJoining) {
            std::shared_ptr<friendworld::Lease> lease;
            std::string address;
            {
                std::lock_guard lock(tabMutex);
                lease = std::move(friendLeaseReady);
                if (realmJoinReady) {
                    realmJoinReady = false;
                    address = realmJoinAddress;
                }
            }
            if (lease) {
                leaseJoining = false;
                SetStatusHint({});
                ReleaseFriendLease();
                friendLease = std::move(lease);
                Connect();
            } else if (!address.empty()) {
                leaseJoining = false;
                SetStatusHint({});
                JoinServer(address);
            }
        }
        if (titleScreen.active || titleScreen.shown)
            return false;
        const auto now = Clock::now();
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_LOADING") != nullptr;
        const auto tr = [](const char* key, const char* fallback, const std::vector<std::string>& args = {}) {
            std::string s = args.empty() ? i18n::Tr(key) : i18n::Tr(key, args);
            if (s == key || s.empty()) {
                s = fallback;
                for (size_t i = 0; i < args.size(); i++) {
                    const std::string token = "%" + std::to_string(i + 1);
                    for (size_t at = s.find(token); at != std::string::npos; at = s.find(token, at))
                        s.replace(at, token.size(), args[i]);
                }
            }
            return s;
        };
        ui::LoadingScreenText t;
        t.title = tr("progressScreen.title.connectingExternal", "Connecting to external server");
        if (!replayCtl.Playing())
            t.warning = "Perlin is not yet stable, and may get you kicked/banned by server anticheats, due "
                        "to movement and other inaccuracies";
        bool show = false;
        const bool transferring = session && session->Transferring();
        if (preview) {
            show = true;
            t.message = tr("progressScreen.title.downloading", "Downloading packs %1", {"3/25"});
            t.progress = 0.4f;
            t.detail = "40%  -  8.7 / 21.7 MB  -  262 KB/s";
        } else if (PERLIN_ENV("PERLIN_PREVIEW") && !session) {
            show = false;
        } else if (!session || (!sessionReady && session->GetState() != BedrockSession::State::Failed) ||
                   transferring) {
            show = true;
            if (transferring)
                loading.done = false;
            const BedrockSession::State st =
                session ? session->GetState() : BedrockSession::State::Disconnected;
            const auto progress = session ? session->PackProgress() : PackDownloader::Progress{};
            std::string hint;
            {
                std::lock_guard lock(statusMutex);
                hint = statusHint;
            }
            if (!hint.empty()) {
                t.message = tr("connect.authorizing", "Logging in...");
                t.detail = hint;
            } else if (!session || st == BedrockSession::State::Disconnected) {
                t.message = tr("progressScreen.message.resourceLoading", "Loading Resources");
            } else if (st == BedrockSession::State::Connecting) {
                t.message = onlineThisRun ? tr("connect.authorizing", "Logging in...")
                                          : tr("progressScreen.message.locating", "Locating server");
            } else if (st == BedrockSession::State::ResourcePacks) {
                if (progress.total > 0 && progress.done < progress.total) {
                    const int pct = progress.bytesTotal > 0
                                        ? static_cast<int>(100.0 * static_cast<double>(progress.bytes) /
                                                           static_cast<double>(progress.bytesTotal))
                                        : 0;
                    t.message =
                        tr("progressScreen.title.downloading", "Downloading packs %1",
                           {std::to_string(progress.done + 1) + "/" + std::to_string(progress.total)});
                    t.progress =
                        progress.bytesTotal > 0
                            ? static_cast<float>(progress.bytes) / static_cast<float>(progress.bytesTotal)
                            : static_cast<float>(progress.done) / static_cast<float>(progress.total);
                    static uint64_t rateBytes = 0;
                    static auto sampleAt = now;
                    static double rate = 0.0;
                    if (progress.bytes < rateBytes) {
                        rateBytes = progress.bytes;
                        sampleAt = now;
                        rate = 0.0;
                    }
                    if (const double dt = std::chrono::duration<double>(now - sampleAt).count(); dt >= 2.0) {
                        rate = static_cast<double>(progress.bytes - rateBytes) / dt;
                        rateBytes = progress.bytes;
                        sampleAt = now;
                    }
                    const auto mb = [](uint64_t b) {
                        char s[32];
                        std::snprintf(s, sizeof(s), "%.1f", static_cast<double>(b) / (1024.0 * 1024.0));
                        return std::string(s);
                    };
                    t.detail = std::to_string(pct) + "%  -  " + mb(progress.bytes) + " / " +
                               mb(progress.bytesTotal) + " MB";
                    if (rate > 0.0)
                        t.detail += "  -  " + std::to_string(static_cast<int>(rate / 1024.0)) + " KB/s";
                } else {
                    t.message = tr("progressScreen.title.applyingPacks", "Loading resource packs");
                }
            } else {
                t.message = tr("connect.connecting", "Connecting to the server...");
            }
        } else if (sessionReady && !loading.done) {
            show = true;
            if (!loading.startAt.time_since_epoch().count())
                loading.startAt = now;
            const int chunks = session->ChunksReceived();
            const int freshChunks = chunks - loading.chunkBase;
            const bool assets = levelRenderer && levelRenderer->Blocks().Complete();
            const bool enough = freshChunks >= 9 && assets;
            if (enough) {
                if (!loading.readyArmed) {
                    loading.readyArmed = true;
                    loading.readyAt = now;
                }
            } else {
                loading.readyArmed = false;
            }
            const bool timeout = now - loading.startAt > std::chrono::seconds(5);
            if (loading.readyArmed || timeout) {
                loading.done = true;
                show = false;
            } else {
                t.message = tr("progressScreen.message.building", "Building terrain");
                const float fraction = std::min(1.0f, static_cast<float>(freshChunks) / 49.0f);
                t.progress = std::min(0.98f, fraction);
            }
        }
        if (!show) {
            if (loading.shown) {
                hud.Overlay().ClearOverlay();
                loading.shown = false;
                loading.signature.clear();
                loading.startAt = {};
                loading.readyArmed = false;
            }
            if (loading.textMode) {
                loading.textMode = false;
                if (!chat.IsOpen())
                    input.SetTextMode(false);
            }
            loading.leftDown = false;
            return false;
        }
        t.cancel = tr("gui.cancel", "Cancel");
        if (t.progress >= 0.0f && t.detail.empty())
            t.detail = std::to_string(static_cast<int>(std::clamp(t.progress, 0.0f, 1.0f) * 100.0f)) + "%";
        if (!input.TextMode())
            input.SetTextMode(true);
        loading.textMode = true;
        if (!loading.shown) {
            loading.openedAt = now;
            loading.leftDown = true;
            loading.shown = true;
            pause.shown = inventory.shown = death.shown = false;
            pause.signature.clear();
            death.signature.clear();
        }
        if (loading.tmpl.Refresh("loading_screen.rml", ui::DefaultLoadingScreenTemplate(),
                                 std::chrono::milliseconds(400), now))
            loading.signature.clear();
        const auto [scale, guiW, guiH] = detail::OverlayViewport(hud, fbW, fbH);
        const double time = std::chrono::duration<double>(now.time_since_epoch()).count();
        const std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "|" + t.title +
                                      "|" + t.message + "|" + t.detail + "|" + t.cancel;
        bool modHidden = false;
        {
            std::lock_guard lock(modHooks.mutex);
            modHidden = !modHooks.loadingHidden.empty();
        }
        // a mod hid the loading screen: connection carries on underneath, nothing drawn over it
        if (modHidden) {
            if (hud.Overlay().HasOverlay())
                hud.Overlay().ClearOverlay();
            loading.signature.clear();
        } else if (signature != loading.signature || !hud.Overlay().HasOverlay()) {
            loading.signature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildLoadingScreenRml(loading.tmpl.text, t, guiW, guiH, time),
                                          error)) {
                Log("ui", "loading_screen.rml: {}", error);
                loading.tmpl.text = ui::DefaultLoadingScreenTemplate();
                hud.Overlay().SetOverlay(ui::BuildLoadingScreenRml(loading.tmpl.text, t, guiW, guiH, time),
                                         error);
            }
        }
        const glm::vec2 pointer = detail::PointerOrPreview(input);
        float mx = pointer.x, my = pointer.y;
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (!down && loading.leftDown && !preview &&
            now - loading.openedAt > std::chrono::milliseconds(300) &&
            hud.Overlay().OverlayHover() == ui::kLoadingCancelId) {
            Log("ui", "loading screen: cancel");
            LeaveToServerList(hud);
            return true;
        }
        loading.leftDown = down;
        return true;
    }

    bool Perlin::UpdateDisconnectScreen(ui::Hud& hud, int fbW, int fbH) {
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_DISCONNECT") != nullptr;
        const bool failed = preview || (session && session->GetState() == BedrockSession::State::Failed &&
                                        !session->Transferring());
        const auto now = Clock::now();
        if (!failed) {
            if (disconnect.shown) {
                hud.Overlay().ClearOverlay();
                disconnect.shown = false;
                disconnect.signature.clear();
            }
            if (disconnect.textMode) {
                disconnect.textMode = false;
                if (!chat.IsOpen())
                    input.SetTextMode(false);
            }
            disconnect.leftDown = false;
            return false;
        }
        if (!disconnect.shown) {
            disconnect.shown = true;
            disconnect.openedAt = now;
            pause.shown = inventory.shown = death.shown = false;
            pause.signature.clear();
            death.signature.clear();
            Log("ui", "disconnect screen shown");
        }
        if (!disconnect.textMode) {
            disconnect.textMode = true;
            input.SetTextMode(true);
        }

        ui::DisconnectScreenText t;
        t.rejoin = i18n::Tr("perlin.disconnect.rejoin");
        t.quit = i18n::Tr("menu.disconnect");
        BedrockSession::DisconnectInfo info;
        std::string lastError;
        if (preview) {
            info.happened = true;
            info.reason = 55;
            const char kind = PERLIN_ENV("PERLIN_PREVIEW_DISCONNECT")[0];
            if (kind == 'c' || kind == 's') {
                info.outdated = kind == 'c' ? 1 : 2;
                info.serverVersion = kind == 'c' ? "1.26.70" : "1.21.50";
            }
            info.message =
                PERLIN_ENV("PERLIN_PREVIEW_DISCONNECT")[0] == 'b'
                    ? "You are banned from this server.\nReason: Cheating (fly hacks)\nExpires in 29d 23h 59m"
                    : "Kicked by an operator";
            if (info.outdated)
                info.message.clear();
        } else {
            info = session->GetDisconnectInfo();
            lastError = session->LastError();
        }
        // the server advertises an older version (the hive's ping says 1.26.14 while it lets newer clients in):
        // offer to join announcing that version, or do it straight away when the player said not to ask again
        const bool olderServer = !preview && info.outdated == 2 && info.serverProtocol > 0 &&
                                 info.serverProtocol < kBedrockProtocol && !inSingleplayer;
        const bool overrideTried =
            versionOverride.protocol == info.serverProtocol && versionOverride.server == options.server;
        const auto joinAsServerVersion = [&] {
            versionOverride.server = options.server;
            versionOverride.protocol = info.serverProtocol;
            versionOverride.version = info.serverVersion;
            if (versionOverride.dontAsk && !options.versionMismatchNoAsk) {
                options.versionMismatchNoAsk = true;
                options.Save();
            }
            Log("perlin", "joining {} again as protocol {} ({})", options.server, info.serverProtocol,
                info.serverVersion);
            hud.Overlay().ClearOverlay();
            disconnect.shown = false;
            disconnect.signature.clear();
            sessionReady = false;
            Connect();
        };
        if (olderServer && !overrideTried && options.versionMismatchNoAsk) {
            joinAsServerVersion();
            return true;
        }
        std::string message = ui::StripFormatting(info.message);
        if (!message.empty() && !message.contains(' ') && message.contains('.')) {
            const std::string translated = i18n::Tr(message);
            if (translated != message)
                message = translated;
        }
        if (info.outdated != 0) {
            const bool clientOld = info.outdated == 1;
            t.title = i18n::Tr(clientOld ? "perlin.disconnect.outdated_client"
                                         : "perlin.disconnect.outdated_server");
            t.reason = i18n::Tr(clientOld ? "perlin.disconnect.outdated_client_reason"
                                          : "perlin.disconnect.outdated_server_reason");
            const std::string theirs = !info.serverVersion.empty() ? info.serverVersion
                                       : info.serverProtocol > 0
                                           ? "protocol " + std::to_string(info.serverProtocol)
                                           : "?";
            t.reason +=
                "\n\n" + i18n::Tr("perlin.disconnect.versions", {theirs, std::string(kBedrockVersion)});
            if (!message.empty())
                t.reason += "\n" + message;
            if (olderServer && !overrideTried) {
                t.title =
                    detail::TrOr("perlin.disconnect.older_server", "This server advertises an older version");
                const std::vector<std::string> args = {theirs, std::to_string(info.serverProtocol),
                                                       std::string(kBedrockVersion),
                                                       std::to_string(kBedrockProtocol)};
                t.reason = i18n::Tr("perlin.disconnect.older_server_reason", args);
                if (t.reason == "perlin.disconnect.older_server_reason")
                    t.reason =
                        "The server says it runs Minecraft " + args[0] + " (protocol " + args[1] +
                        "); Perlin speaks " + args[2] + " (" + args[3] +
                        "). Many servers, The Hive among them, still let newer clients in.\n\nPerlin can "
                        "join announcing " +
                        args[0] +
                        ". Things that changed between the two versions may not work. Are you sure?";
                t.rejoin = detail::TrOr("perlin.disconnect.join_anyway", "Join Anyway");
                t.extra = std::string(versionOverride.dontAsk ? "[x] " : "[  ] ") +
                          detail::TrOr("perlin.disconnect.dont_ask", "Don't ask again");
            } else if (olderServer) {
                t.reason += "\n\n" + detail::TrOr("perlin.disconnect.older_server_failed",
                                                  "Joining as the server's version did not work either.");
            }
        } else if (info.happened && info.connectionLost) {
            t.title = i18n::Tr("perlin.disconnect.lost");
            t.reason = i18n::Tr("perlin.disconnect.lost_reason");
            if (!message.empty())
                t.reason += "\n" + message;
        } else if (info.happened) {
            const bool ban = ui::LooksLikeBan(message);
            t.title = i18n::Tr(ban ? "perlin.disconnect.banned" : "perlin.disconnect.title");
            t.reason = message.empty() ? i18n::Tr("perlin.disconnect.no_reason") : message;
            switch (info.reason) {
            case 44:
                t.reason += "\n\n" + i18n::Tr("perlin.disconnect.hint.id_conflict");
                break;
            case 8:
            case 35:
                t.reason += "\n\n" + i18n::Tr("perlin.disconnect.hint.version");
                break;
            case 46:
                t.reason += "\n\n" + i18n::Tr("perlin.disconnect.hint.login");
                break;
            default:
                break;
            }
            if (info.reason >= 0)
                t.code = i18n::Tr("perlin.disconnect.code", {std::to_string(info.reason)});
        } else {
            t.title = i18n::Tr("perlin.disconnect.failed");
            t.reason = lastError.empty() ? i18n::Tr("perlin.disconnect.no_reason") : lastError;
        }

        if (disconnect.tmpl.Refresh("disconnect_screen.rml", ui::DefaultDisconnectScreenTemplate(),
                                    std::chrono::milliseconds(400), now, "disconnect screen"))
            disconnect.signature.clear();
        const auto [scale, guiW, guiH] = detail::OverlayViewport(hud, fbW, fbH);
        const std::string signature =
            std::to_string(guiW) + "x" + std::to_string(guiH) + "|" + t.title + "|" + t.reason + "|" + t.code;
        if (signature != disconnect.signature || !hud.Overlay().HasOverlay()) {
            disconnect.signature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildDisconnectScreenRml(disconnect.tmpl.text, t, guiW, guiH),
                                          error)) {
                Log("ui", "disconnect_screen.rml: {}", error);
                disconnect.tmpl.text = ui::DefaultDisconnectScreenTemplate();
                hud.Overlay().SetOverlay(ui::BuildDisconnectScreenRml(disconnect.tmpl.text, t, guiW, guiH),
                                         error);
            }
        }
        const glm::vec2 pointer = detail::PointerOrPreview(input);
        float mx = pointer.x, my = pointer.y;
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (!down && disconnect.leftDown && now - disconnect.openedAt > std::chrono::milliseconds(300) &&
            !preview) {
            const std::string hover = hud.Overlay().OverlayHover();
            if (hover == ui::kDisconnectExtraId) {
                versionOverride.dontAsk = !versionOverride.dontAsk;
                disconnect.signature.clear();
            } else if (hover == ui::kDisconnectRejoinId && olderServer && !overrideTried) {
                joinAsServerVersion();
                return true;
            } else if (hover == ui::kDisconnectRejoinId) {
                Log("ui", "disconnect screen: rejoin");
                hud.Overlay().ClearOverlay();
                disconnect.shown = false;
                disconnect.signature.clear();
                if (sessionReady && session && session->RequestReconnect()) {
                } else {
                    sessionReady = false;
                    Connect();
                }
            } else if (hover == ui::kDisconnectQuitId) {
                LeaveToServerList(hud);
            }
        }
        disconnect.leftDown = down;
        return true;
    }
}
