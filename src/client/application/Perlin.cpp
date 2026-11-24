#include <filesystem>
#include <random>
#include <stb_image_write.h>
#include "client/application/PerlinInternal.hpp"
#include "client/application/LaunchOverrides.hpp"
#include "client/services/update/Updater.hpp"
#include "network/Http.hpp"
#include "util/Log.hpp"
#include "util/Platform.hpp"
#include "util/Resources.hpp"
#include "util/ResourcePaths.hpp"
#include "world/level/block/BlockPalette.hpp"

#ifdef __EMSCRIPTEN__
#include <cstdlib>
EM_JS(char*, perlin_js_auth_relay, (), {
    var relay = Module["perlinAuthRelay"] || "";
    var size = lengthBytesUTF8(relay) + 1;
    var ptr = _malloc(size);
    stringToUTF8(relay, ptr, size);
    return ptr;
});
#endif

namespace perlin {
    namespace {
        std::string FormatClock(int64_t day, int tickOfDay) {
            const int hours = (tickOfDay / 1000 + 6) % 24;
            const int minutes = (tickOfDay % 1000) * 60 / 1000;
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "Day %lld %02d:%02d", static_cast<long long>(day + 1),
                          hours, minutes);
            return buffer;
        }
    }

    LocalBreakState Perlin::LocalBreak() const {
        return {interaction.localCrack.active, interaction.localCrack.x, interaction.localCrack.y, interaction.localCrack.z, interaction.localCrack.progress};
    }

    bool Perlin::AcceptEula() {
        if (options.eulaAccepted)
            return true;
        if (args.acceptEula) {
            options.eulaAccepted = true;
            options.Save();
            return true;
        }
        if (!platform->ConfirmDialog("Minecraft EULA",
                                     "This software uses Minecraft assets under the Mojang EULA.\n\n"
                                     "Do you accept the Minecraft EULA?"))
            return false;
        options.eulaAccepted = true;
        options.Save();
        return true;
    }

    void Perlin::PlatformFramebufferSize(int& w, int& h) {
        platform->GetFramebufferSize(w, h);
    }

    void Perlin::PlatformSetClipboard(const std::string& text) {
        if (platform)
            platform->SetClipboard(text);
    }

    void Perlin::NotifyResourcesReloaded() {
        mods::ModManager::Instance().Queue(abi::Event::ResourcesReloaded, {});
    }

    void Perlin::BindClipboard() {
        ui::TextEdit::SetClipboard([this] { return platform ? platform->GetClipboard() : std::string(); },
                                   [this](const std::string& text) {
                                       if (platform)
                                           platform->SetClipboard(text);
                                   });
    }

    void Perlin::LoadConfiguration() {
        options = Options::Load();
        runningRenderer = notifiedRenderer = options.renderer;
        update::Start(options.autoUpdate && !std::getenv("PERLIN_NO_UPDATE") && !args.hidden);
#ifdef __EMSCRIPTEN__
        if (char* relay = perlin_js_auth_relay()) {
            http::SetWebRelay(relay);
            std::free(relay);
        }
#endif
        i18n::Lang::Get().Configure(std::string(), paths::DataDir(), options.language);
        if (const char* test = PERLIN_ENV("PERLIN_TEST_SERVER_PACKS")) {
            ServerPacks::Instance().SetRoot(paths::DataDir() + "/server_packs_extracted");
            std::stringstream list(test);
            for (std::string dir; std::getline(list, dir, ':');)
                if (!dir.empty())
                    ServerPacks::Instance().AddDirectory(dir);
        }
    }

    void Perlin::StartMods() {
        modHost.SetupMods();
        mods::ModManager::Instance().SetApproved(options.approvedMods);
        mods::ModManager::Instance().SetDisabled(options.disabledMods);
        mods::ModManager::Instance().Start(paths::DataDir() + "/mods");
    }

    bool Perlin::StartControl() {
        if (!args.controlPort)
            return true;
        if (!controlBridge.Start(static_cast<uint16_t>(args.controlPort))) {
            std::cerr << "[control] could not bind localhost port " << args.controlPort << std::endl;
            return false;
        }
        if (args.hidden) {
            inputManager.SetControlActions({}, std::numeric_limits<uint64_t>::max());
            input.SetCaptured(true);
        }
        player.ObserveTicks([this](auto tick, const auto& state, const auto& movement) {
            controlBridge.Trace(tick, state, movement);
        });
        Log("control", "http://127.0.0.1:{}/?token={} (send X-Perlin-Token: {} with API requests)", args.controlPort,
            controlBridge.Token(), controlBridge.Token());
        return true;
    }

    void Perlin::StartInitialScreen() {
        if (!PERLIN_ENV("PERLIN_PREVIEW")) {
            if (args.server.empty())
                titleScreen.active = true;
            else
                Connect();
        }
        if (PERLIN_ENV("PERLIN_PREVIEW_TITLE"))
            titleScreen.active = true;
    }

    bool Perlin::Initialize(AppPlatform* p) {
        platform = p;
        BindClipboard();
        LoadConfiguration();
        StartMods();
        LoadAccounts();

        const bool firstSetup = !options.eulaAccepted;
        if (!AcceptEula())
            return false;
        if (firstSetup && !args.offline && !PERLIN_ENV("PERLIN_PREVIEW")) {
            options.onlineLogin = true;
            onlineThisRun = true;
            options.Save();
        }
        PrewarmSignIn();

        renderer = platform->CreateRenderContext();
        if (!renderer)
            return false;
        if (!StartControl())
            return false;

        workers = std::make_unique<ThreadPool>(4, false, "perlin-work");
        if (args.server.empty() && !PERLIN_ENV("PERLIN_PREVIEW")) {
            Accounts().BackfillGamertags(Clock::now());
            serverBrowser.RefreshFeatured(false);
        }
        StartAssetDownloads();
        BuildDiscordService();
        RegisterScreens();
        StartInitialScreen();
        renderRunning = true;
#ifdef __EMSCRIPTEN__
        RenderThreadMain();
#else
        renderThread = std::thread(&Perlin::RenderThreadMain, this);
#endif
        return true;
    }

    void Perlin::StartAssetDownloads() {
        if (!BedrockSamples::Available(options.bedrockSamplesDir))
            assets::Samples().stage = assets::Stage::Checking;
        if (!MojangFonts::Available(paths::DataDir()))
            assets::Client().stage = assets::Stage::Checking;
        workers->Submit([this] {
            if (!BedrockSamples::Ensure(options.bedrockSamplesDir))
                return;
            BlockPalette& palette = BlockPalette::Instance();
            palette.LoadVanillaDefinitions(options.bedrockSamplesDir +
                                           "/repo/metadata/vanilladata_modules/mojang-blocks.json");
            if (!palette.SequenceLoaded()) {
                std::string table;
                if (resources::Read(resources::path::kBlockStates, table))
                    palette.LoadServerStatesText(table, "block_states.json");
                if (std::string versions; resources::Read(resources::path::kBlockVersions, versions))
                    palette.LoadVersionDeltas(versions);
            }
        });
        workers->Submit([this] {
            if (MojangFonts::Ensure(paths::DataDir()))
                fontsDownloaded = true;
        });
    }

    void Perlin::ResetLoadingForConnect() {
        loading.done = false;
        loading.startAt = {};
        loading.readyArmed = false;
        loading.chunkBase = 0;
        view.ResetForConnect();
    }

    bool Perlin::ResolveConnectTarget(std::string& host, uint16_t& port) {
        ServerText target = NormalizeServerText(options.server, args.secure);
        std::string& serverText = target.text;
        bool& secureSignaling = target.secure;
#ifdef __EMSCRIPTEN__
        if (!secureSignaling) {
            std::string bare = serverText.substr(0, serverText.find_first_of(":/"));
            const bool local = bare == "localhost" || bare == "127.0.0.1" || bare == "[::1]";
            if (!local) {
                secureSignaling = EM_ASM_INT({ return location.protocol === 'https:' ? 1 : 0; }) != 0;
            }
        }
#endif
        NetherNet::SetSecureSignaling(secureSignaling);
        if (!args.stun.empty())
            NetherNet::SetIceServers(args.stun);
        const std::shared_ptr<friendworld::Lease>& friendLease = serverBrowser.FriendLease();
        inSingleplayer = !friendLease && pendingSingleplayer != nullptr;
        if (friendLease) {
            host = friendLease->worldName.empty() ? "friend" : friendLease->worldName;
            port = 0;
        } else if (pendingSingleplayer) {
            host = "singleplayer";
            port = 1;
        } else if (!ParseServerAddress(serverText, host, port)) {
            Log("perlin", "invalid server address '{}'", options.server);
            return false;
        }
        port = DefaultPortFor(serverText, secureSignaling, friendLease || pendingSingleplayer, port);
        return true;
    }

    void Perlin::ConfigureSession() {
        const std::shared_ptr<friendworld::Lease>& friendLease = serverBrowser.FriendLease();
        session = std::make_unique<BedrockSession>();
        if (pendingSingleplayer)
            session->SetInjectedTransport(std::move(pendingSingleplayer));
        else if (!friendLease && versionOverride.protocol > 0 && versionOverride.server == options.server)
            session->SetProtocolOverride(versionOverride.protocol, versionOverride.version);
        session->SetRecordTap(replayCtl.NewSessionTap());
        session->SetPacketHandler([this](uint32_t id, const std::vector<uint8_t>& body) {
            if (!modHost.modWantPackets.load() || body.size() > 65536)
                return;
            std::lock_guard lock(modHost.modMutex);
            if (modHost.modPackets.size() < 2000)
                modHost.modPackets.emplace_back(id, body);
        });
        session->SetPlayerName(options.playerName);
        session->SetTransport(args.transport.empty() ? options.transport : args.transport);
        session->SetFriendLease(friendLease);
        session->SetChunkRadius(options.renderDistance);
        session->SetMaxViewDistance(std::max(32, options.renderDistance));

    }

    void Perlin::LoadSessionSkin() {
        SkinConfig skinConfig = MakeSkinConfig(args.skin.empty() ? options.skin : args.skin,
                                               args.cape.empty() ? options.cape : args.cape, options.skinSlim);
        if (args.slimArms) {
            skinConfig.slimArms = true;
            skinConfig.autoSlim = false;
        }
        LoadedSkin loadedSkin;
        std::string skinError;
        if (!LoadSkinConfig(skinConfig, loadedSkin, skinError)) {
            Log("perlin", "skin: {} - using the default skin", skinError);
            SetStatusHint(i18n::Tr("perlin.status.skin_failed", {skinError}));
            skinConfig.skinPath.clear();
            skinConfig.capePath.clear();
            LoadSkinConfig(skinConfig, loadedSkin, skinError);
        } else if (loadedSkin.hasSkin() || loadedSkin.hasCape()) {
            Log("perlin", "skin {}{}{}",
                (loadedSkin.hasSkin() ? skinConfig.skinPath : std::string("(default)")),
                (loadedSkin.hasCape() ? ", cape " + skinConfig.capePath : std::string()),
                (loadedSkin.slimArms ? ", slim arms" : ""));
        }
        session->SetSkin(std::move(loadedSkin));
    }

    void Perlin::AttachSessionAccount() {
        session->SetPackDirectory(paths::DataDir() + "/server_packs");
        ServerPacks::Instance().SetRoot(paths::DataDir() + "/server_packs_extracted");

        connectLogin = {};
        if (onlineThisRun) {
            connectLogin = Accounts().OpenConnectLogin();
            session->SetAccount(connectLogin.login->Native(), connectLogin.authPath);
            if (inSingleplayer)
                if (const services::AccountEntry* active = Accounts().Active(); active && !active->name.empty())
                    session->SetPlayerName(active->name);
        }
    }

    void Perlin::StartConnectWorker(const std::string& host, uint16_t port, uint64_t gen) {
        const bool singleplayerJoin = inSingleplayer;
        connectsInFlight++;
        workers->Submit(
            [this, host, port, gen, s = session.get(), connect = connectLogin, singleplayerJoin] {
                struct InFlight {
                    std::atomic<int>& n;
                    ~InFlight() { n--; }
                } inFlight{connectsInFlight};
                const auto current = [&] { return connectGeneration.load() == gen; };
                if (connect.login) {
                    const auto outcome = Accounts().SignInForConnect(
                        connect, [this, &current](const std::string& url, const std::string& code) {
                            if (!current())
                                return;
                            Log("auth", "To sign in, open {} and enter the code {}", url, code);
                            platform->ShowSignInCode(url, code);
                            SetStatusHint(i18n::Tr("perlin.status.sign_in", {url, code}));
                        });
                    if (!current())
                        return;
                    if (!outcome.signedIn && singleplayerJoin) {
                        Log("auth", "sign-in failed ({}): joining singleplayer offline", outcome.error);
                        s->SetAccount(nullptr);
                    } else if (!outcome.signedIn) {
                        Log("auth", "sign-in failed: {}", outcome.error);
                        SetStatusHint(i18n::Tr("perlin.status.sign_in_failed", {outcome.error}));
                        return;
                    }
                    SetStatusHint({});
                }
                const bool connected = s->Connect(host, port);
                if (!current()) {
                    if (connected)
                        s->Disconnect();
                    return;
                }
                if (connected)
                    sessionReady = true;
                else if (connect.login)
                    SetStatusHint(i18n::Tr("perlin.status.login_failed", {s->LastError()}));
            });
    }

    void Perlin::Connect() {
        ResetLoadingForConnect();
        std::string host;
        uint16_t port = 0;
        if (!ResolveConnectTarget(host, port))
            return;
        particleLevel = nullptr;
        const uint64_t gen = ++connectGeneration;
        if (session)
            retiredSessions.push_back(std::move(session));
        replayStudio.FinishRecording("the connection was reset");
        ConfigureSession();
        LoadSessionSkin();
        AttachSessionAccount();
        StartConnectWorker(host, port, gen);
    }

    void Perlin::CaptureCursor(bool captured) { platform->SetCursorCaptured(captured); }

    void Perlin::OpenUrl(const std::string& url) {
        if (platform)
            platform->OpenUrl(url);
    }

    bool Perlin::PickFile(const char* kind) { return platform && platform->PickFile(kind); }

    void Perlin::SetStatusHint(const std::string& text) {
        std::lock_guard lock(statusMutex);
        statusHint = text;
    }

    void Perlin::VibrateBlockBroken() {
        if (options.padVibration && platform && input.PadConnected())
            platform->Vibrate(24);
    }

    void Perlin::StopRenderThread() {
        replayStudio.FinishRecording("the game closed");
        renderRunning = false;
        if (renderThread.joinable())
            renderThread.join();
        modHost.modLive = nullptr;
        modHost.modLevel = nullptr;
        modHost.modHud = nullptr;
    }

    void Perlin::StopServices() {
        appServices.Stop();
        mods::ModManager::Instance().Stop();
        tasks.Shutdown();
    }

    void Perlin::OnBackground() {
        options.Save();
    }

    void Perlin::Shutdown() {
        controlBridge.Stop();
        StopRenderThread();
        StopServices();
        if (session)
            session->Disconnect();
        if (workers)
            workers->Stop();
        options.Save();
    }

    void Perlin::TakeScreenshot() {
        const std::string dir = paths::DataDir() + "/screenshots";
        paths::EnsureDir(dir);
        const std::time_t now = std::time(nullptr);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H.%M.%S", std::localtime(&now));
        renderer->RequestScreenshot(dir + "/" + stamp + ".bmp");
    }

    void Perlin::UpdateTitle(int fps) {
        std::string title = "Perlin";
        if (renderer && renderReady) {
            const std::string backend = renderer->RendererName();
            if (!backend.empty())
                title += " (" + backend + ")";
        }
        title += " | " + i18n::Tr("perlin.title.fps", {std::to_string(fps)});
        int64_t day = 0;
        int tick = 0;
        if (sessionReady && session && session->GetWorldTime(day, tick))
            title += " | " + FormatClock(day, tick);
        if (levelRenderer)
            title += " | " + i18n::Tr("perlin.title.chunks", {std::to_string(levelRenderer->MeshedChunks())});
        {
            std::lock_guard lock(statusMutex);
            if (!statusHint.empty())
                title += " | " + statusHint;
        }
        platform->SetTitle(title);
    }

    void Perlin::JoinWorld(const screen::WorldLaunch& world) {
        const auto quoted = [](const std::string& text) {
            std::string out = "\"";
            for (const unsigned char c : text) {
                if (c == '"' || c == '\\')
                    out += '\\';
                if (c < 0x20) {
                    char esc[8];
                    std::snprintf(esc, sizeof esc, "\\u%04x", c);
                    out += esc;
                    continue;
                }
                out += static_cast<char>(c);
            }
            return out + "\"";
        };
        const std::string dir = paths::DataDir() + "/singleplayer";
        std::string extra = ",\"world_folder\":" + quoted(world.folder) + ",\"level_name\":" + quoted(world.name);
        uint64_t seed = 0;
        int mode = -1;
        if (world.fresh) {
            extra += ",\"difficulty\":" + std::to_string(std::clamp(world.difficulty, 0, 3));
            extra += std::string(",\"allow_cheats\":") + (world.cheats ? "true" : "false");
            extra += std::string(",\"bonus_chest\":") + (world.bonusChest ? "true" : "false");
            extra += std::string(",\"generate_structures\":") + (world.structures ? "true" : "false");
            extra += std::string(",\"generator\":") + (world.flat ? "\"flat\"" : "\"vanilla\"");
            if (!world.seedText.empty()) {
                extra += ",\"seed_text\":" + quoted(world.seedText);
            } else {
                // a blank seed is a random one, as the game picks it
                std::random_device rd;
                do
                    seed = ((static_cast<uint64_t>(rd()) << 32) | rd()) & 0x7FFFFFFFFFFFFFFFull;
                while (seed == 0);
            }
            mode = world.gameMode;
        }
        worldIconPath = dir + "/worlds/" + world.folder + "/world_icon.jpeg";
        worldIconStage = 1;
        worldIconReadyAt = {};
        JoinSingleplayer(dir, seed, mode, true, extra);
    }

    void Perlin::UpdateWorldIcon() {
        // 2: the HUD is hidden; the capture waits two frames so no screen built with the HUD is still drawn
        if (worldIconStage >= 2 && worldIconStage < 4) {
            worldIconStage++;
            return;
        }
        if (worldIconStage == 4) {
            worldIconStage = 5;
            RequestWorldIcon();
            return;
        }
        if (worldIconStage == 5) {
            options.showHud = worldIconHud;
            worldIconStage = 6;
            return;
        }
        if (worldIconStage != 1 || !inSingleplayer || !sessionReady || !renderer || WorldCovered() ||
            screens.AnyCoveringOpen() || chat.IsOpen()) {
            if (worldIconStage == 1 && (!sessionReady || WorldCovered()))
                worldIconReadyAt = {};
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (worldIconReadyAt == std::chrono::steady_clock::time_point{})
            worldIconReadyAt = now;
        if (now - worldIconReadyAt < std::chrono::seconds(4))
            return;
        worldIconHud = options.showHud;
        options.showHud = false;
        worldIconStage = 2;
    }

    // a frame without the HUD and hand, cropped to 16:9 and kept as the world's JPEG preview
    void Perlin::RequestWorldIcon() {
        const std::string path = worldIconPath;
        renderer->RequestFrameCapture([path](uint32_t w, uint32_t h, uint32_t pitch, const uint8_t* bgra, bool) {
            if (!w || !h || !bgra)
                return;
            uint32_t cw = w, ch = w * 9 / 16;
            if (ch > h) {
                ch = h;
                cw = h * 16 / 9;
            }
            const uint32_t x0 = (w - cw) / 2, y0 = (h - ch) / 2;
            constexpr uint32_t kOutW = 384, kOutH = 216;
            std::vector<uint8_t> rgb(static_cast<size_t>(kOutW) * kOutH * 3);
            for (uint32_t y = 0; y < kOutH; y++)
                for (uint32_t x = 0; x < kOutW; x++) {
                    // box filter over the source pixels this output pixel covers
                    const uint32_t sx0 = x0 + x * cw / kOutW, sx1 = std::max(sx0 + 1, x0 + (x + 1) * cw / kOutW);
                    const uint32_t sy0 = y0 + y * ch / kOutH, sy1 = std::max(sy0 + 1, y0 + (y + 1) * ch / kOutH);
                    uint32_t sum[3] = {0, 0, 0}, n = 0;
                    for (uint32_t sy = sy0; sy < sy1 && sy < h; sy++)
                        for (uint32_t sx = sx0; sx < sx1 && sx < w; sx++, n++) {
                            const uint8_t* p = bgra + static_cast<size_t>(sy) * pitch + static_cast<size_t>(sx) * 4;
                            sum[0] += p[2];
                            sum[1] += p[1];
                            sum[2] += p[0];
                        }
                    uint8_t* o = &rgb[(static_cast<size_t>(y) * kOutW + x) * 3];
                    for (int c = 0; c < 3; c++)
                        o[c] = static_cast<uint8_t>(n ? sum[c] / n : 0);
                }
            const std::string temp = path + ".tmp";
            if (stbi_write_jpg(temp.c_str(), kOutW, kOutH, 3, rgb.data(), 90)) {
                std::error_code ec;
                std::filesystem::rename(temp, path, ec);
            }
        });
    }

    void Perlin::JoinSingleplayer(const std::string& worldDir, uint64_t seed, int gameMode, bool persistent,
                                  const std::string& extraConfig) {
#ifdef PERLIN_SINGLEPLAYER
        serverBrowser.ReleaseFriendLease();
        if (!singleplayerServer)
            singleplayerServer = std::make_unique<EmbeddedServer>();
        if (!singleplayerServer->Running()) {
            const std::string dir = worldDir.empty() ? paths::DataDir() + "/singleplayer" : worldDir;
            paths::EnsureDir(dir);

            for (const char* name : {resources::path::kBlockStates, resources::path::kBlockDestroy, resources::path::kExtraItems}) {
                std::string bytes;
                const std::string path = dir + "/" + std::filesystem::path(name).filename().string();
                std::error_code ec;
                if (resources::Read(name, bytes) && (!std::filesystem::exists(path, ec) ||
                                                     std::filesystem::file_size(path, ec) != bytes.size()))
                    std::ofstream(path, std::ios::binary) << bytes;
            }

#ifdef __EMSCRIPTEN__
            const std::string threads = ",\"max_threads\":2";
#else
            const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
#ifdef PERLIN_MOBILE
            const unsigned genThreads = 2u;
#else
            // Half the cores generate the world; the rest are left to the chunk meshers and the frame, so
            // meshing is not starved while the world loads.
            const unsigned genThreads = std::max(2u, cores / 2);
#endif
            const std::string threads = ",\"max_threads\":" + std::to_string(genThreads);
#endif
            std::string extra;
            if (seed)
                extra += ",\"seed\":" + std::to_string(static_cast<int64_t>(seed & 0x7FFFFFFFFFFFFFFFull));
            if (gameMode >= 0)
                extra += ",\"gamemode\":" + std::to_string(gameMode);
            if (!persistent)
                extra += ",\"world_storage\":\"none\"";
            extra += extraConfig;
            // The vanilla behavior pack (from the Bedrock Dedicated Server download) is kept once for every
            // world, beside them, rather than downloaded again for each new world.
            const std::string packDir = paths::DataDir() + "/perlite";
            paths::EnsureDir(packDir);
            const auto quoted = [](const std::string& text) {
                std::string out = "\"";
                for (const char c : text) {
                    if (c == '"' || c == '\\')
                        out += '\\';
                    out += c;
                }
                return out + "\"";
            };
            const std::string config =
                "{\"view_distance\":" + std::to_string(std::clamp(options.renderDistance, 2, 128)) + threads + extra +
                ",\"block_data_dir\":" + quoted(dir) + ",\"vanilla_pack_dir\":" + quoted(packDir) + "}";
            // Perlite sets up on its own thread (pack download, world load) while the loading screen shows
            // its progress; the loopback transport waits for it on the connect thread.
            singleplayerServer->StartAsync(dir, config);
        }
        pendingSingleplayer = singleplayerServer->Connect();
        if (!pendingSingleplayer) {
            SetStatusHint("Singleplayer could not open a connection.");
            return;
        }
        titleScreen.active = false;
        Connect();
#else
        SetStatusHint("This build has no singleplayer: it was built with PERLIN_SINGLEPLAYER=OFF.");
#endif
    }
}
