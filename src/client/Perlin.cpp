#include "client/PerlinInternal.hpp"
#include "client/update/Updater.hpp"
#include "network/Http.hpp"
#include "util/Log.hpp"

#ifdef __EMSCRIPTEN__
#include <cstdlib>
// clang-format off
EM_JS(char*, perlin_js_auth_relay, (), {
    var relay = Module["perlinAuthRelay"] || "";
    var size = lengthBytesUTF8(relay) + 1;
    var ptr = _malloc(size);
    stringToUTF8(relay, ptr, size);
    return ptr;
});
// clang-format on
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
        return {localCrack.active, localCrack.x, localCrack.y, localCrack.z, localCrack.progress};
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

    bool Perlin::Initialize(AppPlatform* p) {
        platform = p;
        ui::TextEdit::SetClipboard([this] { return platform ? platform->GetClipboard() : std::string(); },
                                   [this](const std::string& text) {
                                       if (platform)
                                           platform->SetClipboard(text);
                                   });
        options = Options::Load();
        runningRenderer = options.renderer;
        // headless runs (tests, control port) never touch the installation
        update::Start(options.autoUpdate && !std::getenv("PERLIN_NO_UPDATE") && !args.hidden);
#ifdef __EMSCRIPTEN__
        // the page's sign-in relay (perlin-config.json, ?relay=, or serve.py's own): see tools/web/auth-relay
        if (char* relay = perlin_js_auth_relay()) {
            http::SetWebRelay(relay);
            std::free(relay);
        }
#endif
        i18n::Lang::Get().Configure(std::string(), paths::DataDir(), options.language);
        // dev/test: PERLIN_TEST_SERVER_PACKS=dir1:dir2: extracted pack folders act as the server's packs
        if (const char* test = PERLIN_ENV("PERLIN_TEST_SERVER_PACKS")) {
            ServerPacks::Instance().SetRoot(paths::DataDir() + "/server_packs_extracted");
            std::stringstream list(test);
            for (std::string dir; std::getline(list, dir, ':');)
                if (!dir.empty())
                    ServerPacks::Instance().AddDirectory(dir);
        }
        SetupMods();
        mods::ModManager::Instance().SetApproved(options.approvedMods);
        mods::ModManager::Instance().SetDisabled(options.disabledMods);
        mods::ModManager::Instance().Start(paths::DataDir() + "/mods");
        if (!args.server.empty())
            options.server = args.server;
        if (!args.playerName.empty())
            options.playerName = args.playerName;
        if (args.logout) {
            xbox::Account::SignOut(paths::DataDir() + "/auth.json");
            options.onlineLogin = false;
            Log("auth", "saved Microsoft login removed");
        }
        if (args.login)
            options.onlineLogin = true;
        accounts.Load(paths::DataDir(), options.onlineLogin, options.playerName);
        if (args.playerName.empty() && !args.login && !args.logout)
            ApplyActiveAccount();
        onlineThisRun = options.onlineLogin && !args.offline;

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
        if (args.controlPort) {
            control = std::make_shared<ClientControl>();
            if (!control->Start(args.controlPort)) {
                std::cerr << "[control] could not bind localhost port " << args.controlPort << std::endl;
                return false;
            }
            if (args.hidden) {
                inputManager.SetControlActions({}, std::numeric_limits<uint64_t>::max());
                input.SetCaptured(true);
            }
            player.ObserveTicks([this](auto tick, const auto& state, const auto& movement) {
                TraceControlTick(tick, state, movement);
            });
            Log("control", "http://127.0.0.1:{}/?token={} (send X-Perlin-Token: {} with API requests)",
                args.controlPort, control->Token(), control->Token());
        }

        workers = std::make_unique<ThreadPool>(4);
        if (args.server.empty() && !PERLIN_ENV("PERLIN_PREVIEW")) {
            FetchMissingGamertags();
            RefreshFeatured(false);
        }
        StartAssetDownloads();
        if (!PERLIN_ENV("PERLIN_PREVIEW")) {
            if (args.server.empty())
                titleScreen.active = true;
            else
                Connect();
        }
        if (PERLIN_ENV("PERLIN_PREVIEW_TITLE"))
            titleScreen.active = true;

        RegisterScreens();
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
        workers->Submit([this] { BedrockSamples::Ensure(options.bedrockSamplesDir); });
        workers->Submit([this] {
            if (MojangFonts::Ensure(paths::DataDir()))
                fontsDownloaded = true;
        });
    }

    SkinConfig Perlin::MakeSkinConfig(const std::string& skinPath, const std::string& capePath) const {
        SkinConfig cfg;
        cfg.skinPath = skinPath;
        cfg.capePath = capePath;
        const auto it = options.skinSlim.find(skinPath);
        if (it != options.skinSlim.end())
            cfg.slimArms = it->second;
        else
            cfg.autoSlim = !skinPath.empty();
        return cfg;
    }

    void Perlin::Connect() {
        loading.done = false;
        loading.startAt = {};
        loading.readyArmed = false;
        loading.chunkBase = 0;
        lastDimId = -1;
        portalTime = 0.0f;
        std::string host;
        uint16_t port = 0;
        std::string serverText = options.server;
        bool secureSignaling = args.secure;
        if (serverText.starts_with("https://")) {
            serverText.erase(0, 8);
            secureSignaling = true;
        } else if (serverText.starts_with("http://")) {
            serverText.erase(0, 7);
        }
        while (!serverText.empty() && serverText.back() == '/')
            serverText.pop_back();
#ifdef __EMSCRIPTEN__
        if (!secureSignaling) {
            std::string bare = serverText.substr(0, serverText.find_first_of(":/"));
            const bool local = bare == "localhost" || bare == "127.0.0.1" || bare == "[::1]";
            if (!local) {
                // clang-format off
                secureSignaling = EM_ASM_INT({ return location.protocol === 'https:' ? 1 : 0; }) != 0;
                // clang-format on
            }
        }
#endif
        NetherNet::SetSecureSignaling(secureSignaling);
        if (!args.stun.empty())
            NetherNet::SetIceServers(args.stun);
        inSingleplayer = !friendLease && pendingSingleplayer != nullptr;
        if (friendLease) {
            host = friendLease->worldName.empty() ? "friend" : friendLease->worldName;
            port = 0;
        } else if (pendingSingleplayer) {
            host = "singleplayer";
            port = 1;
        } else if (!detail::ParseServer(serverText, host, port)) {
            Log("perlin", "invalid server address '{}'", options.server);
            return;
        }
        if (!friendLease && !pendingSingleplayer && secureSignaling &&
            serverText.rfind(':') == std::string::npos)
            port = 443;

        particleLevel = nullptr;
        const uint64_t gen = ++connectGeneration;
        if (session)
            retiredSessions.push_back(std::move(session));
        FinishReplayRecording("the connection was reset");
        session = std::make_unique<BedrockSession>();
        if (pendingSingleplayer)
            session->SetInjectedTransport(std::move(pendingSingleplayer));
        else if (!friendLease && versionOverride.protocol > 0 && versionOverride.server == options.server)
            session->SetProtocolOverride(versionOverride.protocol, versionOverride.version);
        session->SetRecordTap(replayCtl.NewSessionTap());
        if (connectsInFlight.load() == 0)
            std::erase_if(retiredSessions,
                          [](const std::unique_ptr<BedrockSession>& s) { return !s->Transferring(); });
        session->SetPacketHandler([this](uint32_t id, const std::vector<uint8_t>& body) {
            if (!modWantPackets.load() || body.size() > 65536)
                return;
            std::lock_guard lock(modMutex);
            if (modPackets.size() < 2000)
                modPackets.emplace_back(id, body);
        });
        session->SetPlayerName(options.playerName);
        session->SetTransport(args.transport.empty() ? options.transport : args.transport);
        session->SetFriendLease(friendLease);
        session->SetChunkRadius(options.renderDistance);
        session->SetMaxViewDistance(std::max(32, options.renderDistance));
        session->SetNearRadius(options.enableLods && options.renderDistance > LodStore::kRealChunks
                                   ? LodStore::kRealChunks
                                   : 1 << 20);

        SkinConfig skinConfig = MakeSkinConfig(args.skin.empty() ? options.skin : args.skin,
                                               args.cape.empty() ? options.cape : args.cape);
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
        session->SetPackDirectory(paths::DataDir() + "/server_packs");
        ServerPacks::Instance().SetRoot(paths::DataDir() + "/server_packs_extracted");

        account.reset();
        // a signed-in Microsoft account plays singleplayer as itself (gamertag, xuid, skin) like on a server. the
        // embedded server checks no Xbox login, so when the sign-in cannot be reached (offline, the browser without a
        // relay) the join goes on as an offline player with the account's name instead of failing
        if (onlineThisRun) {
            account = std::make_shared<xbox::Account>(ActiveAuthPath());
            session->SetAccount(account, ActiveAuthPath());
            if (inSingleplayer)
                if (const AccountEntry* active = accounts.Active(); active && !active->name.empty())
                    session->SetPlayerName(active->name);
        }
        const bool singleplayerJoin = inSingleplayer;
        connectsInFlight++;
        std::string nameAccountId;
        if (const AccountEntry* active = accounts.Active();
            account && active && active->microsoft && active->name.empty())
            nameAccountId = active->id;
        workers->Submit(
            [this, host, port, gen, s = session.get(), acct = account, nameAccountId, singleplayerJoin] {
                struct InFlight {
                    std::atomic<int>& n;
                    ~InFlight() { n--; }
                } inFlight{connectsInFlight};
                const auto current = [&] { return connectGeneration.load() == gen; };
                if (acct) {
                    std::string error;
                    const bool signedIn = acct->SignIn(
                        [this, &current](const std::string& url, const std::string& code) {
                            if (!current())
                                return;
                            Log("auth", "To sign in, open {} and enter the code {}", url, code);
                            platform->ShowSignInCode(url, code);
                            SetStatusHint(i18n::Tr("perlin.status.sign_in", {url, code}));
                        },
                        error);
                    if (!current())
                        return;
                    if (!signedIn && singleplayerJoin) {
                        Log("auth", "sign-in failed ({}): joining singleplayer offline", error);
                        s->SetAccount(nullptr);
                    } else if (!signedIn) {
                        Log("auth", "sign-in failed: {}", error);
                        SetStatusHint(i18n::Tr("perlin.status.sign_in_failed", {error}));
                        return;
                    }
                    SetStatusHint({});
                    if (signedIn && !nameAccountId.empty()) {
                        std::string gamertag, why;
                        if (acct->FetchGamertag(gamertag, why)) {
                            std::lock_guard lock(signIn.mutex);
                            signIn.pendingRenames.emplace_back(nameAccountId, gamertag);
                        }
                    }
                }
                const bool connected = s->Connect(host, port);
                if (!current()) {
                    if (connected)
                        s->Disconnect();
                    return;
                }
                if (connected)
                    sessionReady = true;
                else if (acct)
                    SetStatusHint(i18n::Tr("perlin.status.login_failed", {s->LastError()}));
            });
    }

    void Perlin::SetStatusHint(const std::string& text) {
        std::lock_guard lock(statusMutex);
        statusHint = text;
    }

    // same moment and length as the game's touch vibration (ClientInputCallbacks::destroyVibrate, 24 ms)
    void Perlin::VibrateBlockBroken() {
        if (options.padVibration && platform && input.PadConnected())
            platform->Vibrate(24);
    }

    void Perlin::Shutdown() {
        if (control)
            control->Stop();
        FinishReplayRecording("the game closed");
        renderRunning = false;
        if (renderThread.joinable())
            renderThread.join();
        modLive = nullptr;
        modLevel = nullptr;
        modHud = nullptr;
        mods::ModManager::Instance().Stop();
        for (int i = 0; i < 400 && modHttpActive.load() > 0; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (account)
            account->Cancel();
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
}
