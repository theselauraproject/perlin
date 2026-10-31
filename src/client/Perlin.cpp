#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#endif
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include "client/CameraEffects.hpp"
#include "rhi/Device.hpp"
#include "client/entity/BlockEntityView.hpp"
#include "client/ui/ItemTooltip.hpp"
#include "client/SampleProfiler.hpp"
#include "util/Env.hpp"
#include "client/Zoom.hpp"
#include "client/Perlin.hpp"
#include "util/Relaunch.hpp"
#include "client/resources/ServerPacks.hpp"
#include "util/Resources.hpp"
#include <fstream>
#include "client/ui/ServerLayer.hpp"
#include "client/i18n/Lang.hpp"
#include "client/multiplayer/SkinLoader.hpp"
#include "client/mods/ModManager.hpp"
#include "client/resources/ShaderPack.hpp"
#include "client/javashaders/PackSource.hpp"
#include "client/resources/PackInfo.hpp"
#include "client/resources/TextureLoader.hpp"
#include "client/resources/TexturePack.hpp"
#include "client/resources/BundledShaderPack.hpp"
#include "client/resources/AssetJobs.hpp"
#include "network/XboxAuth.hpp"
#include "network/RakNet.hpp"
#include "client/renderer/chunk/ChunkMesher.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/renderer/Lightmap.hpp"
#include "client/renderer/DynamicLights.hpp"
#include "client/input/Keys.hpp"
#include "client/renderer/Environment.hpp"
#include "client/renderer/Fog.hpp"
#include "client/player/BlockCollision.hpp"
#include "client/player/BlockDestroy.hpp"
#include "client/player/BlockUse.hpp"
#include "client/player/Bridging.hpp"
#include "client/player/HeldBuild.hpp"
#include "client/ui/DeathScreen.hpp"
#include "client/ui/BedScreen.hpp"
#include "client/ui/CreditsScreen.hpp"
#include "client/ui/InventoryScreen.hpp"
#include "client/ui/CreativeScreen.hpp"
#include "client/ui/DisconnectScreen.hpp"
#include "client/ui/LoadingScreen.hpp"
#include "client/resources/ServerBlocks.hpp"
#include "network/Friends.hpp"
#include "network/Http.hpp"
#include "network/LanDiscovery.hpp"
#include "network/RakNet.hpp"
#include "network/Realms.hpp"
#include "network/Featured.hpp"
#include "client/ui/TitleScreen.hpp"
#include "client/ui/TextEdit.hpp"
#include "client/ui/TitlePanorama.hpp"
#include "client/ui/UiLayout.hpp"
#include "client/ui/PauseScreen.hpp"
#include "client/ui/JsonScreen.hpp"
#include "client/ui/ScreenModels.hpp"
#include "client/resources/MojangFonts.hpp"
#include "client/ui/SettingInfo.hpp"
#include "client/ui/McFont.hpp"
#include "client/ui/SkinPreview.hpp"
#include "client/ui/UiTemplate.hpp"
#include "client/player/DamageEffect.hpp"
#include "client/PreviewWorld.hpp"
#include "client/renderer/chunk/LodStore.hpp"
#include "client/MemoryTrim.hpp"
#include "client/resources/BedrockSamples.hpp"
#include "world/level/lighting/ChunkLighting.hpp"
#include "client/ui/DebugScreen.hpp"
#include "client/ui/GuiScale.hpp"
#include "client/input/TouchState.hpp"
#include "client/input/TouchControls.hpp"
#include "client/ui/ItemData.hpp"
#include "client/ui/HudRenderers.hpp"
#include "util/SystemInfo.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "client/ui/UiAssets.hpp"
#include "util/Paths.hpp"
#include "util/Utf8.hpp"
#include "client/platform/web/WebLoop.hpp"
#include "network/NetherNet.hpp"

#include "client/player/Interaction.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#include <functional>
#include <optional>
#include <map>
#include <cctype>
#include <chrono>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <filesystem>
#include <ctime>
#include <iostream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define PERLIN_WEB_PERSIST static
#else
#define PERLIN_WEB_PERSIST
#endif

#ifndef PERLIN_VERSION
#define PERLIN_VERSION "1.0.0"
#endif

namespace perlin {
    namespace {
        uint8_t CachedLight(Level& level, int bx, int by, int bz, std::chrono::steady_clock::time_point now) {
            struct Band {
                ChunkLighting::Result lit;
                std::chrono::steady_clock::time_point at{};
                uint64_t counter = 0;
            };
            static std::unordered_map<uint64_t, Band> bands;
            static const Level* owner = nullptr;
            if (owner != &level || bands.size() > 256) {
                bands.clear();
                owner = &level;
            }
            const int cx = bx >> 4, cz = bz >> 4, band = by >> 4;
            const uint64_t key = (static_cast<uint64_t>(cx & 0xFFFFF) << 28) | (static_cast<uint64_t>(cz & 0xFFFFF) << 8) |
                                 static_cast<uint64_t>((band + 128) & 0xFF);
            Band& b = bands[key];
            const uint64_t counter = level.changeCounter.load(std::memory_order_relaxed);
            const auto age = now - b.at;
            if (b.at == std::chrono::steady_clock::time_point{} ||
                (counter != b.counter && age > std::chrono::milliseconds(400)) || age > std::chrono::seconds(20)) {
                b.at = now;
                b.counter = counter;
                ChunkLighting::Compute(level, {cx, cz}, band * 16 - 8, band * 16 + 23, b.lit);
            }
            return b.lit.At(bx & 15, by, bz & 15);
        }
    }

    namespace {
        struct FrameProfile {
            static constexpr int kCount = 10;
            double ms[kCount] = {};
            double worst[kCount] = {};
            double last[kCount] = {};
            std::vector<double> frameTimes;
            int frames = 0;
            bool enabled = PERLIN_ENV("PERLIN_PROFILE") != nullptr;
            const char* names[kCount] = {"logic", "level update", "level render", "entities", "hand", "hud", "end frame", "frame", "particles", "weather"};
        } gProfile;
        struct ProfScope {
            int id;
            std::chrono::steady_clock::time_point start;
            explicit ProfScope(int i) : id(i) {
                if (gProfile.enabled)
                    start = std::chrono::steady_clock::now();
            }
            ~ProfScope() {
                if (gProfile.enabled) {
                    const double d = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                    gProfile.ms[id] += d;
                    gProfile.last[id] += d;
                }
            }
        };
    }

    bool gAdvancedTooltips = false;

    namespace {
        AimHit gAim;
        uint32_t gSelfSwing = 0;
        struct LocalCrack { bool active = false; int x = 0, y = 0, z = 0, stage = 0; float progress = 0.0f; };
        LocalCrack gLocalCrack;
        struct BreakState { bool breaking = false; int x = 0, y = 0, z = 0, face = 1; float progress = 0.0f; };
        BreakState gBreak;

        heldbuild::HeldBuild gHeldBuild;

        void ApplyBedPose(Level& level, entity::Entity& e) {
            BlockPalette& pal = BlockPalette::Instance();
            double bestD = 1e9;
            for (int dy = 0; dy >= -1; dy--)
                for (int dz = -2; dz <= 2; dz++)
                    for (int dx = -2; dx <= 2; dx++) {
                        const int x = static_cast<int>(std::floor(e.pos.x)) + dx, y = static_cast<int>(std::floor(e.pos.y + 0.3)) + dy,
                                  z = static_cast<int>(std::floor(e.pos.z)) + dz;
                        bool loaded = false;
                        const uint16_t id = level.GetBlock(x, y, z, loaded);
                        if (!loaded || id == 0 || pal.Name(id) != "minecraft:bed")
                            continue;
                        const int dir = pal.StateInt(id, "direction", 0) & 3;
                        const bool head = pal.StateInt(id, "head_piece_bit", 0) != 0;
                        static const int kStep[4][2] = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}};
                        const int hx = head ? x : x + kStep[dir][0], hz = head ? z : z + kStep[dir][1];
                        const double cx = hx + 0.5, cz = hz + 0.5;
                        const double d = (cx - e.pos.x) * (cx - e.pos.x) + (cz - e.pos.z) * (cz - e.pos.z);
                        if (d >= bestD)
                            continue;
                        bestD = d;
                        e.pos = {cx - kStep[dir][0] * 1.3, y + 0.5625, cz - kStep[dir][1] * 1.3};
                        e.yaw = static_cast<float>(dir * 90 + 180);
                        e.headYaw = e.yaw;
                        e.pitch = 0.0f;
                    }
        }
        bool showChunkBorders = false;
        bool showHitboxes = false;
        bool softPaused = false;
        int switcherIndex = -1;
    }

    LocalBreakState GetLocalBreakState() {
        return {gLocalCrack.active, gLocalCrack.x, gLocalCrack.y, gLocalCrack.z, gLocalCrack.progress};
    }
}

namespace perlin {
    namespace {
        using Clock = std::chrono::steady_clock;
        uint64_t MsNow() {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
        }

        constexpr float kMaxFrameDelta = 0.1f;
        constexpr float kDefaultFrameDelta = 0.016f;
        constexpr float kNoonTick = 6000.0f;
        constexpr float kHorizonHeight = 63.0f;
        constexpr float kFlatHorizonHeight = -64.0f;
        constexpr auto kTitleInterval = std::chrono::milliseconds(500);
        constexpr auto kAnimationStep = std::chrono::milliseconds(200);

        bool ParseServer(const std::string& raw, std::string& host, uint16_t& port) {
            const auto first = raw.find_first_not_of(" \t\r\n");
            const std::string text = first == std::string::npos ? std::string() : raw.substr(first, raw.find_last_not_of(" \t\r\n") - first + 1);
            host = text;
            port = 19132;
            std::string portText;
            if (!text.empty() && text[0] == '[') {
                const auto close = text.find(']');
                if (close == std::string::npos)
                    return false;
                host = text.substr(1, close - 1);
                if (close + 1 == text.size())
                    return !host.empty();
                if (text[close + 1] != ':')
                    return false;
                portText = text.substr(close + 2);
            } else {
                const auto colon = text.rfind(':');
                if (colon == std::string::npos || text.find(':') != colon)
                    return !host.empty();
                host = text.substr(0, colon);
                portText = text.substr(colon + 1);
            }
            try {
                const int value = std::stoi(portText);
                if (value <= 0 || value > 65535)
                    return false;
                port = static_cast<uint16_t>(value);
            } catch (...) {
                return false;
            }
            return !host.empty();
        }

        float StepWeather(float level, float target) {
            if (level < target)
                return std::min(target, level + 0.01f);
            return std::max(target, level - 0.01f);
        }

        std::string FormatClock(int64_t day, int tickOfDay) {
            const int hours = (tickOfDay / 1000 + 6) % 24;
            const int minutes = (tickOfDay % 1000) * 60 / 1000;
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "Day %lld %02d:%02d", static_cast<long long>(day + 1),
                          hours, minutes);
            return buffer;
        }
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
            std::cout << "[auth] saved Microsoft login removed" << std::endl;
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
            std::cout << "[control] http://127.0.0.1:" << args.controlPort << "/?token=" << control->Token()
                      << " (send X-Perlin-Token: " << control->Token() << " with API requests)" << std::endl;
        }

        workers = std::make_unique<ThreadPool>(4);
        if (args.server.empty() && !PERLIN_ENV("PERLIN_PREVIEW")) {
            FetchMissingGamertags();
            RefreshFeatured(false);
        }
        StartAssetDownloads();
        if (!PERLIN_ENV("PERLIN_PREVIEW")) {
            if (args.server.empty())
                titleActive = true;
            else
                Connect();
        }
        if (PERLIN_ENV("PERLIN_PREVIEW_TITLE"))
            titleActive = true;

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
        loadingDone = false;
        loadingStartAt = {};
        loadingReadyArmed = false;
        loadingChunkBase = 0;
        lastDimId = -1;
        portalTime = 0.0f;
        std::string host;
        uint16_t port = 0;
        std::string serverText = options.server;
        bool secureSignaling = args.secure;
        if (serverText.rfind("https://", 0) == 0) {
            serverText.erase(0, 8);
            secureSignaling = true;
        } else if (serverText.rfind("http://", 0) == 0) {
            serverText.erase(0, 7);
        }
        while (!serverText.empty() && serverText.back() == '/')
            serverText.pop_back();
#ifdef __EMSCRIPTEN__
        if (!secureSignaling) {
            std::string bare = serverText.substr(0, serverText.find_first_of(":/"));
            const bool local = bare == "localhost" || bare == "127.0.0.1" || bare == "[::1]";
            if (!local && EM_ASM_INT({ return location.protocol === 'https:' ? 1 : 0; }))
                secureSignaling = true;
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
        } else if (!ParseServer(serverText, host, port)) {
            std::cout << "[perlin] invalid server address '" << options.server << "'" << std::endl;
            return;
        }
        if (!friendLease && !pendingSingleplayer && secureSignaling && serverText.rfind(':') == std::string::npos)
            port = 443;

        particleLevel = nullptr;
        const uint64_t gen = ++connectGeneration;
        if (session)
            retiredSessions.push_back(std::move(session));
        FinishReplayRecording("the connection was reset");
        session = std::make_unique<BedrockSession>();
        if (pendingSingleplayer)
            session->SetInjectedTransport(std::move(pendingSingleplayer));
        session->SetRecordTap(replayCtl.NewSessionTap());
        if (connectsInFlight.load() == 0)
            std::erase_if(retiredSessions, [](const std::unique_ptr<BedrockSession>& s) { return !s->Transferring(); });
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
        session->SetNearRadius(options.enableLods && options.renderDistance > LodStore::kRealChunks ? LodStore::kRealChunks : 1 << 20);

        SkinConfig skinConfig = MakeSkinConfig(args.skin.empty() ? options.skin : args.skin, args.cape.empty() ? options.cape : args.cape);
        if (args.slimArms) {
            skinConfig.slimArms = true;
            skinConfig.autoSlim = false;
        }
        LoadedSkin loadedSkin;
        std::string skinError;
        if (!LoadSkinConfig(skinConfig, loadedSkin, skinError)) {
            std::cout << "[perlin] skin: " << skinError << " - using the default skin" << std::endl;
            SetStatusHint(i18n::Tr("perlin.status.skin_failed", {skinError}));
            skinConfig.skinPath.clear();
            skinConfig.capePath.clear();
            LoadSkinConfig(skinConfig, loadedSkin, skinError);
        } else if (loadedSkin.hasSkin() || loadedSkin.hasCape()) {
            std::cout << "[perlin] skin " << (loadedSkin.hasSkin() ? skinConfig.skinPath : std::string("(default)"))
                      << (loadedSkin.hasCape() ? ", cape " + skinConfig.capePath : std::string())
                      << (loadedSkin.slimArms ? ", slim arms" : "") << std::endl;
        }
        session->SetSkin(std::move(loadedSkin));
        session->SetPackDirectory(paths::DataDir() + "/server_packs");
        ServerPacks::Instance().SetRoot(paths::DataDir() + "/server_packs_extracted");

        account.reset();
        if (onlineThisRun) {
            account = std::make_shared<xbox::Account>(ActiveAuthPath());
            session->SetAccount(account, ActiveAuthPath());
        }
        connectsInFlight++;
        std::string nameAccountId;
        if (const AccountEntry* active = accounts.Active(); account && active && active->microsoft && active->name.empty())
            nameAccountId = active->id;
        workers->Submit([this, host, port, gen, s = session.get(), acct = account, nameAccountId] {
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
                        std::cout << "[auth] To sign in, open " << url << " and enter the code " << code << std::endl;
                        platform->ShowSignInCode(url, code);
                        SetStatusHint(i18n::Tr("perlin.status.sign_in", {url, code}));
                    },
                    error);
                if (!current())
                    return;
                if (!signedIn) {
                    std::cout << "[auth] sign-in failed: " << error << std::endl;
                    SetStatusHint(i18n::Tr("perlin.status.sign_in_failed", {error}));
                    return;
                }
                SetStatusHint({});
                if (!nameAccountId.empty()) {
                    std::string gamertag, why;
                    if (acct->FetchGamertag(gamertag, why)) {
                        std::lock_guard lock(signInMutex);
                        pendingRenames.emplace_back(nameAccountId, gamertag);
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

    namespace {
        std::string TreeStamp(const std::string& path) {
            std::error_code ec;
            uint64_t h = 1469598103934665603ull;
            const auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
            if (!std::filesystem::exists(path, ec))
                return "none";
            if (!std::filesystem::is_directory(path, ec)) {
                mix(static_cast<uint64_t>(std::filesystem::last_write_time(path, ec).time_since_epoch().count()));
                mix(std::filesystem::file_size(path, ec));
                return std::to_string(h);
            }
            for (std::filesystem::recursive_directory_iterator it(path, std::filesystem::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file(ec))
                    continue;
                mix(static_cast<uint64_t>(it->last_write_time(ec).time_since_epoch().count()));
                mix(it->file_size(ec));
            }
            return std::to_string(h);
        }
    }

    void Perlin::ApplyTexturePacks() {
        {
            std::string error;
            // mod-shipped packs sit under the player's own, by absolute path
            std::vector<std::string> stack = options.texturePacks;
            {
                std::lock_guard lock(modHooks.mutex);
                for (const auto& pack : modHooks.packs)
                    stack.push_back(pack.second);
            }
            TexturePack::Instance().OpenStack(paths::DataDir() + "/texturepacks", stack, error);
            if (!error.empty())
                std::cout << "[textures] " << error << " (using default textures)" << std::endl;
        }
        if (levelRenderer)
            levelRenderer->ReloadResources();
        entityRenderer.ClearCaches();
        ui::UiAssets::Instance().ClearItemCache();
        if (hud)
            hud->Reload();
        hotTexStamp.clear();
        mods::ModManager::Instance().Queue(abi::Event::ResourcesReloaded, {});
    }

    // same moment and length as the game's touch vibration (ClientInputCallbacks::destroyVibrate, 24 ms)
    void Perlin::VibrateBlockBroken() {
        if (options.padVibration && platform && input.PadConnected())
            platform->Vibrate(24);
    }

    void Perlin::UnloadServerPacks(bool leavingLevel) {
        if (!leavingLevel) {
            if (ServerPacks::Instance().HasServerPacks())
                levelUnloadPending = true;
            return;
        }
        std::cout << "[packs] unloading the server's resource packs" << std::endl;
        // finish readers before freeing the definitions and textures they reference
        formUi.Reset();
        chestUi.Reset();
        ui::JsonScreens::Instance().Reset();
        if (hud)
            hud->UnloadLevelUi();
        if (levelRenderer) {
            levelRenderer->Stop();
            levelRenderer.reset();
        }
        entityRenderer.ClearCaches();
        handRenderer.UnloadLevelResources();
        ServerPacks::Instance().Clear();
        ServerBlocks::Instance().Reset();
        ServerBlocks::Instance().ClearItems();
        if (renderer) {
            renderer->UnloadLevelResources();
            levelRenderer = std::make_unique<LevelRenderer>(*renderer, options);
        }
        auto& assets = ui::UiAssets::Instance();
        const std::string vanilla = assets.VanillaRoot();
        assets.ClearLevelCache();
        assets.Configure(vanilla, levelRenderer ? &levelRenderer->Blocks() : nullptr);
        ui::McFont::Instance().UnloadLevelResources();
        i18n::Lang::Get().Invalidate();
        audioEngine.StopAll();
        sounds.Configure({});
        formModel = {};
        formEngine = formEngineFailed = false;
        formSignature.clear();
        hudState = {};
        gHeldBuild.Stop();
    }

    void Perlin::CommitTexturePacks() {
        if (!packPendingInit)
            return;
        packPendingInit = false;
        if (packPending == options.texturePacks)
            return;
        options.texturePacks = packPending;
        options.Save();
        ApplyTexturePacks();
    }

    namespace {
        std::vector<ShaderSetting> gPackSettings;
        std::string gPackSettingsKey = "\x01";
        const std::vector<ShaderSetting>& CachedPackSettings(const std::string& key) {
            if (gPackSettingsKey != key) {
                gPackSettingsKey = key;
                gPackSettings = ShaderPack::Instance().Settings();
            }
            return gPackSettings;
        }
        std::vector<ShaderPreset> gPackPresets;
        std::string gPackPresetsKey = "\x01";
        const std::vector<ShaderPreset>& CachedPackPresets(const std::string& key) {
            if (gPackPresetsKey != key) {
                gPackPresetsKey = key;
                gPackPresets = ShaderPack::Instance().Presets();
            }
            return gPackPresets;
        }
    }

    void Perlin::ApplyShaderPack() {
        ShaderPack& pack = ShaderPack::Instance();
        pack.Close();
        gPackSettingsKey = "\x01";
        gPackPresetsKey = "\x01";
        javaPackPending.clear();
        if (!options.shaderPack.empty()) {
            const std::string full = paths::DataDir() + "/shaderpacks/" + options.shaderPack;
            if (jshader::LooksLikeJavaPack(full)) {
                javaPackPending = full;
            } else {
                std::string error;
                if (!pack.Open(paths::DataDir() + "/shaderpacks", options.shaderPack, error))
                    std::cout << "[shaders] " << error << " - using the built-in shaders" << std::endl;
            }
        }
        if (javaPackPending.empty() && renderer && renderer->JavaShaderPackActive()) {
            renderer->ClearJavaShaderPack();
            if (levelRenderer)
                levelRenderer->RemeshAll();
        }
        if (renderer)
            renderer->ReloadShaders();
        hotShaderStamp.clear();
    }

    void Perlin::PollJavaPack(BedrockSession* live) {
        if (!renderer)
            return;
        static bool startupChecked = false;
        if (!startupChecked) {
            startupChecked = true;
            if (!options.shaderPack.empty()) {
                const std::string full = paths::DataDir() + "/shaderpacks/" + options.shaderPack;
                if (jshader::LooksLikeJavaPack(full))
                    javaPackPending = full;
            }
        }
        if (!javaPackPending.empty() && levelRenderer && levelRenderer->Blocks().Complete()) {
            std::string error;
            const std::string path = javaPackPending;
            renderer->SetBlockNameLookup([](uint16_t id) { return BlockPalette::Instance().Name(id); });
            if (renderer->SetJavaShaderPack(path, jshader::ReadJavaPackOptions(path), error)) {
                javaPackPending.clear();
                levelRenderer->RemeshAll();
                std::cout << "[javapack] loaded " << path << std::endl;
            } else if (error.find("atlas is not loaded") == std::string::npos) {
                javaPackPending.clear();
                std::cout << "[javapack] " << error << std::endl;
                chat.AddMessage("Shader pack failed: " + error, hudAnim.ticks);
            }
        }
        if (renderer->JavaShaderPackActive() && live) {
            const int dim = std::clamp(live->DimensionId(), 0, 2);
            if (dim != javaPackDimension) {
                javaPackDimension = dim;
                renderer->SetJavaShaderPackDimension(dim == 1 ? "world-1" : dim == 2 ? "world1" : "world0");
                if (levelRenderer)
                    levelRenderer->RemeshAll();
            }
        }
    }

    void Perlin::PollHotReload() {
        if (!options.hotReload)
            return;
        const auto now = Clock::now();
        if (now - hotPollAt < std::chrono::milliseconds(500))
            return;
        hotPollAt = now;
        const auto scan = [](std::vector<std::string> texturePacks, std::string shaderPack) {
            HotStamps out;
            for (const std::string& name : texturePacks)
                out.tex += TreeStamp(paths::DataDir() + "/texturepacks/" + name) + ";";
            out.shader = "none";
            if (!shaderPack.empty()) {
                const std::string root = paths::DataDir() + "/shaderpacks/" + shaderPack;
                std::error_code isDir;
                if (std::filesystem::is_directory(root, isDir)) {
                    out.shader.clear();
                    for (const char* sub : {"dxbc", "glsl", "spirv", "essl", "metal", "pack.json"})
                        out.shader += TreeStamp(root + "/" + sub) + ";";
                } else {
                    out.shader = TreeStamp(root);
                }
            }
            return out;
        };
        HotStamps stamps;
#if defined(__EMSCRIPTEN__)
        stamps = scan(options.texturePacks, options.shaderPack);
#else
        if (!hotScan.valid()) {
            hotScan = std::async(std::launch::async, scan, options.texturePacks, options.shaderPack);
            return;
        }
        if (hotScan.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        stamps = hotScan.get();
#endif
        const std::string& tex = stamps.tex;
        if (hotTexStamp.empty())
            hotTexStamp = tex;
        else if (tex != hotTexStamp) {
            hotTexStamp = tex;
            std::cout << "[hot-reload] texture packs changed" << std::endl;
            ApplyTexturePacks();
        }
        const std::string& shader = stamps.shader;
        if (hotShaderStamp.empty())
            hotShaderStamp = shader;
        else if (shader != hotShaderStamp) {
            hotShaderStamp = shader;
            std::cout << "[hot-reload] shader pack changed" << std::endl;
            ApplyShaderPack();
        }
        std::string report;
        {
            std::lock_guard lock(statusMutex);
            report.swap(shaderBuildReport);
        }
        if (!report.empty())
            chat.AddMessage(report, hudAnim.ticks);
    }

    void Perlin::Shutdown() {
        if (control) control->Stop();
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

    namespace {
        void RayTargets(Level& level, glm::vec3 origin, glm::vec3 dir, double reach, ui::DebugInfo& info) {
            BlockPalette& palette = BlockPalette::Instance();
            static std::unordered_map<uint16_t, int> kind;
            const auto classify = [&](uint16_t id) {
                const auto it = kind.find(id);
                if (it != kind.end())
                    return it->second;
                const std::string name = palette.Name(id);
                int k = 2;
                if (name == "minecraft:air" || name == "minecraft:cave_air" || name == "minecraft:void_air" || name.empty())
                    k = 0;
                else if (name == "minecraft:water" || name == "minecraft:flowing_water" || name == "minecraft:lava" ||
                         name == "minecraft:flowing_lava")
                    k = 1;
                return kind[id] = k;
            };
            const auto describe = [&](ui::DebugInfo::Target& t, int x, int y, int z, uint16_t id) {
                t.valid = true;
                t.x = x;
                t.y = y;
                t.z = z;
                t.name = palette.Name(id);
                for (const auto& [key, value] : palette.States(id)) {
                    std::string k = key;
                    if (k.rfind("minecraft:", 0) == 0)
                        k.erase(0, 10);
                    std::string v;
                    switch (value.kind) {
                    case BlockPalette::StateValue::Bool: v = value.i ? "true" : "false"; break;
                    case BlockPalette::StateValue::Int: v = std::to_string(value.i); break;
                    default: v = value.s; break;
                    }
                    t.properties.emplace_back(k, v);
                }
            };
            int x = static_cast<int>(std::floor(origin.x)), y = static_cast<int>(std::floor(origin.y)),
                z = static_cast<int>(std::floor(origin.z));
            const int stepX = dir.x > 0 ? 1 : -1, stepY = dir.y > 0 ? 1 : -1, stepZ = dir.z > 0 ? 1 : -1;
            const auto inv = [](float d) { return d != 0.0f ? std::fabs(1.0f / d) : 1e30f; };
            const float dx = inv(dir.x), dy = inv(dir.y), dz = inv(dir.z);
            const auto firstBoundary = [](float o, int cell, int step, float delta) {
                return (step > 0 ? (static_cast<float>(cell) + 1.0f - o) : (o - static_cast<float>(cell))) * delta;
            };
            float tx = firstBoundary(origin.x, x, stepX, dx), ty = firstBoundary(origin.y, y, stepY, dy),
                  tz = firstBoundary(origin.z, z, stepZ, dz);
            float t = 0.0f;
            while (t <= reach) {
                bool loaded = true;
                const uint16_t id = level.GetBlock(x, y, z, loaded);
                if (!loaded)
                    return;
                const int k = classify(id);
                if (k == 2) {
                    describe(info.block, x, y, z, id);
                    return;
                }
                if (k == 1 && !info.fluid.valid)
                    describe(info.fluid, x, y, z, id);
                if (tx < ty && tx < tz) {
                    t = tx;
                    tx += dx;
                    x += stepX;
                } else if (ty < tz) {
                    t = ty;
                    ty += dy;
                    y += stepY;
                } else {
                    t = tz;
                    tz += dz;
                    z += stepZ;
                }
            }
        }
    }

    void Perlin::UpdateServerLayer(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        static bool shown = false;
        const bool allowed = live && options.showHud && hudState.visible && !deathShown && !disconnectShown && !loadingShown;
        if (!allowed) {
            if (shown) {
                hud.Overlay().ClearServerLayer();
                shown = false;
                serverLayerSignature.clear();
            }
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!toastText.empty() && now - toastAt > std::chrono::seconds(4))
            toastText.clear();
        ui::ServerLayerData data;
        if (toastText.empty()) {
            BedrockSession::ServerToast t;
            if (live->TakeToast(t) && !DispatchModToast(t.title, t.message)) {
                toastText = t.title + "\n" + t.message;
                toastAt = now;
                data.toastTitle = t.title;
                data.toastMessage = t.message;
                lastToastTitle = t.title;
                lastToastMessage = t.message;
            }
        }
        if (!toastText.empty()) {
            data.toast = true;
            data.toastTitle = lastToastTitle;
            data.toastMessage = lastToastMessage;
        }
        const bool jsonBossBars = hud.Ready() && hud.Overrides("hud");
        if (!jsonBossBars)
            for (const auto& b : live->BossBars())
                data.bars.push_back({b.title, b.progress, b.color, b.overlay});
        // vanilla stacks all bars, no 4-cap
        if (data.bars.size() > 8)
            data.bars.resize(8);
        const auto sb = live->Scoreboard();
        if (sb.visible && !pauseShown && !(hud.Ready() && hud.Overrides("scoreboard"))) {
            data.scoreboard = true;
            data.scoreboardTitle = sb.title;
            for (const auto& r : sb.rows)
                data.rows.emplace_back(r.name, r.score);
        }
        if (data.Empty()) {
            if (shown) {
                hud.Overlay().ClearServerLayer();
                shown = false;
                serverLayerSignature.clear();
            }
            return;
        }
        const int w = static_cast<int>(hud.GuiWidth()), h = static_cast<int>(hud.GuiHeight());
        const std::string signature = ui::ServerLayerSignature(data, w, h);
        if (shown && signature == serverLayerSignature)
            return;
        serverLayerSignature = signature;
        std::string error;
        shown = hud.Overlay().SetServerLayer(ui::BuildServerLayerRml(data, w, h), error);
        if (!shown)
            std::cout << "[ui] " << error << std::endl;
    }

    void Perlin::UpdateFormScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        const auto now = Clock::now();
        const auto closeForm = [&]() {
            hud.Overlay().ClearOverlay();
            formShown = false;
            formSignature.clear();
            formLeftDown = false;
            if (!chat.IsOpen())
                input.SetTextMode(false);
        };
        // PERLIN_PREVIEW_FORM=<form.json>: open a captured form without a server; answers go to the log
        const char* previewForm = live ? nullptr : PERLIN_ENV("PERLIN_PREVIEW_FORM");
        if (!live && !previewForm) {
            if (formShown)
                closeForm();
            return;
        }
        const auto respond = [&](const std::string* response) {
            if (live)
                live->SendFormResponse(formModel.id, response, false);
            else
                std::cout << "[form] preview answer: " << (response ? *response : std::string("closed")) << std::endl;
        };
        {
            static auto lastWarm = Clock::time_point{};
            if (now - lastWarm > std::chrono::seconds(2)) {
                lastWarm = now;
                formUi.Warm(ServerPacks::Instance().Directories());
            }
        }
        if (live && live->TakeFormClosed() && formShown)
            closeForm();
        if (formShown && !formEngine && formUi.Ready() && !formEngineFailed)
            formEngine = true;
        if (!formShown) {
            if (deathShown || invShown || pauseShown || chat.IsOpen())
                return;
            BedrockSession::ServerForm sf;
            if (live && !live->TakeServerForm(sf))
                return;
            if (!live) {
                static bool previewOpened = false;
                if (previewOpened)
                    return;
                previewOpened = true;
                std::ifstream in(previewForm, std::ios::binary);
                sf.id = 1;
                sf.json.assign(std::istreambuf_iterator<char>(in), {});
            }
            if (!ui::ParseForm(sf.id, sf.json, formModel)) {
                std::cout << "[form] cannot parse form " << sf.id << std::endl;
                if (live)
                    live->SendFormResponse(sf.id, nullptr, false);
                return;
            }
            // PERLIN_PREVIEW_FORM_CLICKS="t:3,d:6": clicks applied as if made, to check each control's states
            if (!live)
                if (const char* clicks = PERLIN_ENV("PERLIN_PREVIEW_FORM_CLICKS")) {
                    std::stringstream list(clicks);
                    std::string ignored;
                    // "-" clicks on nothing
                    for (std::string id; std::getline(list, id, ',');)
                        if (!ui::FormDismissDropdown(formModel, id == "-" ? std::string() : id))
                            ui::FormClick(formModel, id, ignored);
                }
            std::cout << "[form] opened form " << sf.id << " '" << formModel.title << "'" << std::endl;
            {
                // kept to replay a server's form layout offline
                std::error_code ec;
                std::filesystem::create_directories(paths::DataDir() + "/ui_generated", ec);
                std::ofstream(paths::DataDir() + "/ui_generated/last_form.json", std::ios::binary | std::ios::trunc) << sf.json;
                // each distinct form also kept under its title: every server menu replayable later
                std::string name;
                for (const char c : ui::StripFormatting(formModel.title))
                    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
                        name += c;
                    else if (!name.empty() && name.back() != '_')
                        name += '_';
                if (name.size() > 60)
                    name.resize(60);
                if (!name.empty()) {
                    std::filesystem::create_directories(paths::DataDir() + "/ui_generated/forms", ec);
                    std::ofstream(paths::DataDir() + "/ui_generated/forms/" + name + ".json", std::ios::binary | std::ios::trunc) << sf.json;
                }
            }
            formUi.Warm(ServerPacks::Instance().Directories());
            formUi.WaitReady(150);
            // forms draw with the game's own json ui; the built-in layout only if those files fail
            formEngineFailed = false;
            formEngine = formUi.Ready();
            formShown = true;
            formScroll = 0.0f;
            formContentH = formViewH = 0.0f;
            formSignature.clear();
            formOpenedAt = now;
            formLeftDown = true;
            input.SetTextMode(true);
            CenterCursorForGui();
        }
        bool cancel = false;
        for (const auto& t : inputManager.TakeTyped()) {
            if (!t.character && t.key == Key::Escape)
                cancel = true;
            else
                ui::FormEdit(formModel, t);
        }
        formScroll = std::max(0.0f, formScroll - inputManager.Scroll() * 20.0f);
        if (cancel) {
            respond(nullptr);
            closeForm();
            return;
        }
        // server ui still loading: wait for its layout, don't flash ours first
        if (!formEngine && formUi.Building() && now - formOpenedAt < std::chrono::seconds(3))
            return;
        const int scale = hud.OverlayScale();
        const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
        const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));
        formScroll = std::clamp(formScroll, 0.0f, std::max(0.0f, formContentH - formViewH));
        const std::string signature = std::to_string(formModel.id) + "|" + std::to_string(formModel.focus) + "|" + std::to_string(formModel.openDropdown) + "|" +
                                      ui::FormCustomResponse(formModel) + "|" + std::to_string(ui::FormImageVersion()) + "|" +
                                      std::to_string(guiW) + "x" + std::to_string(guiH) + "|" + std::to_string(static_cast<int>(formScroll)) + "|" +
                                      (formEngine ? "e" : "b");
        // server pack menus slide in and cards fade; animations only advance on re-evaluation:
        // rebuild at frame rate while the last build reported motion
        static std::chrono::steady_clock::time_point lastFormAnim{};
        if (formEngine && formUi.Animating() && now - lastFormAnim >= std::chrono::milliseconds(80)) {
            formSignature.clear();
            lastFormAnim = now;
        }
        if (signature != formSignature || !hud.Overlay().HasOverlay()) {
            formSignature = signature;
            std::string formRml, error;
            float contentH = 0.0f, viewH = 0.0f;
            if (formEngine && !formUi.Build(formModel, guiW, guiH, formScroll, formRml, contentH, viewH)) {
                std::cout << "[form] json ui build failed: " << formUi.Problem() << ", using the built-in layout" << std::endl;
                formEngine = false;
                formEngineFailed = true;
            }
            if (!formEngine)
                formRml = ui::BuildFormRml(formModel, guiW, guiH, formScroll, contentH, viewH);
            formContentH = contentH;
            formViewH = viewH;
            {
                // kept to inspect a server menu's built layout offline
                std::error_code ec;
                std::filesystem::create_directories(paths::DataDir() + "/ui_generated", ec);
                std::ofstream(paths::DataDir() + "/ui_generated/last_form.rml", std::ios::binary | std::ios::trunc) << formRml;
            }
            if (!hud.Overlay().SetOverlay(formRml, error)) {
                std::cout << "[form] " << error << std::endl;
                if (formEngine) {
                    formEngine = false;
                    formEngineFailed = true;
                    formSignature.clear();
                    return;
                }
                respond(nullptr);
                closeForm();
                return;
            }
        }
        hud.Overlay().MouseMove(input.MouseX() / static_cast<float>(scale), input.MouseY() / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
        const bool down = input.IsMouseDown(0);
        {
            static int dragSlider = -1;
            if (!down) {
                dragSlider = -1;
            } else {
                const int hoverSlider = ui::FormSliderAt(hud.Overlay().OverlayHover());
                if (dragSlider < 0 && hoverSlider >= 0 && !formLeftDown)
                    dragSlider = hoverSlider;
                if (dragSlider >= 0)
                    ui::FormDrag(formModel, dragSlider, input.MouseX() / static_cast<float>(scale));
            }
        }
        if (!down && formLeftDown && now - formOpenedAt > std::chrono::milliseconds(200)) {
            const std::string hover = hud.Overlay().OverlayHover();
            if (hover == ui::kFormCloseId) {
                respond(nullptr);
                closeForm();
                return;
            }
            if (ui::FormDismissDropdown(formModel, hover)) {
                formLeftDown = down;
                return;
            }
            std::string response;
            if (!hover.empty() && ui::FormClick(formModel, hover, response)) {
                respond(&response);
                closeForm();
                return;
            }
            // a click on another control leaves the text box
            if (!hover.empty() && hover.rfind("i:", 0) != 0)
                formModel.focus = -1;
        }
        formLeftDown = down;
    }

    void Perlin::UpdateBedScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        const bool asleep = live && live->Sleeping() && !deathShown && !pauseShown && !invShown && !formShown && !chat.IsOpen();
        if (!asleep) {
            if (bedShown) {
                bedShown = false;
                bedLeftDown = false;
                bedSignature.clear();
                hud.Overlay().ClearOverlay();
                if (!chat.IsOpen())
                    input.SetTextMode(false);
            }
            return;
        }
        if (!bedShown) {
            bedShown = true;
            bedLeftDown = input.IsMouseDown(0);
            input.SetTextMode(true);
        }
        const int scale = hud.OverlayScale();
        const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
        const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));
        std::string label = i18n::Tr("multiplayer.stopSleeping");
        if (label.empty() || label == "multiplayer.stopSleeping")
            label = "Leave Bed";
        const std::string signature = label + "|" + std::to_string(guiW) + "x" + std::to_string(guiH);
        if (signature != bedSignature || !hud.Overlay().HasOverlay()) {
            bedSignature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildBedScreenRml(label, guiW, guiH), error))
                std::cout << "[ui] bed screen: " << error << std::endl;
        }
        hud.Overlay().MouseMove(input.MouseX() / static_cast<float>(scale), input.MouseY() / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
        const bool down = input.IsMouseDown(0);
        bool leave = inputManager.Pressed(Action::Pause);
        for (const auto& t : inputManager.TakeTyped())
            if (!t.character && t.key == Key::Escape)
                leave = true;
        if (!down && bedLeftDown && hud.Overlay().OverlayHover() == ui::kBedLeaveId)
            leave = true;
        bedLeftDown = down;
        if (leave)
            live->SendStopSleeping();
    }

    void Perlin::UpdateCreditsScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        // PERLIN_PREVIEW_CREDITS: replay credits from the top when they finish
        const bool preview = !live && PERLIN_ENV("PERLIN_PREVIEW_CREDITS") != nullptr;
        if (!live && !preview) {
            creditsShown = false;
            return;
        }
        if ((preview || live->TakeCreditsRequest()) && !creditsShown) {
            creditsShown = true;
            creditsScroll = 0;
            creditsLast = std::chrono::steady_clock::now();
            input.SetTextMode(true);
        }
        if (!creditsShown)
            return;
        const int scale = hud.OverlayScale();
        const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
        const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));
        const auto now = std::chrono::steady_clock::now();
        // about one gui pixel per tick, like the java end poem
        creditsScroll += std::chrono::duration<double>(now - creditsLast).count() * 20.0;
        creditsLast = now;
        // document only changes when the text moves a whole pixel
        const std::string signature = std::to_string(static_cast<int>(creditsScroll)) + "|" + std::to_string(guiW) + "x" + std::to_string(guiH);
        if (signature != creditsSignature || !hud.Overlay().HasOverlay()) {
            creditsSignature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildCreditsScreenRml(guiW, guiH, static_cast<int>(creditsScroll), live ? live->PlayerName() : options.playerName), error))
                std::cout << "[ui] credits screen: " << error << std::endl;
        }
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
        bool done = creditsScroll > guiH + ui::CreditsHeight();
        for (const auto& t : inputManager.TakeTyped())
            if (!t.character && t.key == Key::Escape)
                done = true;
        if (done) {
            creditsShown = false;
            hud.Overlay().ClearOverlay();
            if (!chat.IsOpen())
                input.SetTextMode(false);
            if (live)
                live->SendCreditsFinished();
        }
    }

    void Perlin::UpdateDeathScreen(ui::Hud& hud, BedrockSession* live, const ui::HudState& state, int fbW, int fbH) {
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_DEATH") != nullptr;
        bool dead = preview || (live && state.mode != ui::GameMode::Spectator && (state.health <= 0.0f || live->DeathPending()));
        if (live && live->DeathPending() && state.health > 0.0f && deathShown && Clock::now() - deathAt > std::chrono::seconds(2))
            live->ClearDeath();
        const auto now = Clock::now();
        if (dead && deathRespawnSent) {
            if (now - deathSentAt < std::chrono::seconds(3))
                dead = false;
            else
                deathRespawnSent = false;
        } else if (!dead) {
            deathRespawnSent = false;
        }
        if (!dead) {
            if (deathShown) {
                hud.Overlay().ClearOverlay();
                deathShown = false;
                deathSignature.clear();
                platform->SetCursorCaptured(true);
            }
            if (deathTextMode) {
                deathTextMode = false;
                if (!chat.IsOpen())
                    input.SetTextMode(false);
            }
            deathLeftDown = false;
            return;
        }
        if (!deathShown) {
            deathShown = true;
            deathAt = now;
            platform->SetCursorCaptured(false);
            std::cout << "[ui] death screen shown: health=" << state.health << " pending=" << (live && live->DeathPending())
                      << " cause='" << (live ? live->DeathCause() : std::string()) << "' hud=" << state.visible << std::endl;
        }
        if (!deathTextMode) {
            deathTextMode = true;
            input.SetTextMode(true);
        }

        const bool buttons = now - deathAt >= std::chrono::seconds(1);
        ui::DeathScreenText t;
        t.title = i18n::Tr("perlin.death.title");
        t.cause = live ? live->DeathCause() : std::string();
        t.respawn = i18n::Tr("perlin.death.respawn");
        t.titleScreen = i18n::Tr("perlin.death.title_screen");
        t.buttonsEnabled = buttons;

        if (now - deathCheckAt > std::chrono::milliseconds(400) || deathTemplate.empty()) {
            deathCheckAt = now;
            uint64_t version = 0;
            std::string text = ui::LoadDeathScreenTemplate(paths::DataDir(), version);
            if (version != deathTemplateVersion || deathTemplate.empty()) {
                deathTemplateVersion = version;
                deathTemplate = std::move(text);
                deathSignature.clear();
                std::cout << "[ui] death screen: loaded " << paths::DataDir() << "/ui/death_screen.rml" << std::endl;
            }
        }

        const int scale = hud.OverlayScale();
        const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
        const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));
        const std::string signature = t.title + "|" + t.cause + "|" + std::to_string(guiW) + "x" + std::to_string(guiH) + "|" +
                                      (buttons ? "1" : "0") + "|" + t.respawn + t.titleScreen;
        if (signature != deathSignature || !hud.Overlay().HasOverlay()) {
            deathSignature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildDeathScreenRml(deathTemplate, t, guiW, guiH), error)) {
                std::cout << "[ui] death_screen.rml: " << error << std::endl;
                deathTemplate = ui::DefaultDeathScreenTemplate();
                hud.Overlay().SetOverlay(ui::BuildDeathScreenRml(deathTemplate, t, guiW, guiH), error);
            }
        }
        float mx = input.MouseX(), my = input.MouseY();
        if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))
            std::sscanf(m, "%f,%f", &mx, &my);
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (PERLIN_ENV("PERLIN_PREVIEW_MOUSE") && !deathHoverLogged) {
            deathHoverLogged = true;
            std::cout << "[ui] death screen hover: '" << hud.Overlay().OverlayHover() << "'" << std::endl;
        }
        if (!down && deathLeftDown && buttons && !preview) {
            const std::string hover = hud.Overlay().OverlayHover();
            std::cout << "[ui] death screen click on '" << hover << "'" << std::endl;
            if (hover == ui::kDeathRespawnId && live) {
                live->RequestRespawn();
                deathRespawnSent = true;
                deathSentAt = now;
            }
        }
        deathLeftDown = down;
    }

    void Perlin::FillDebug(ui::HudState& s, BedrockSession* live, Level* level) {
        s.debug = debugOn && s.visible;
        if (!s.debug)
            return;
        const auto now = Clock::now();
        if (live && now - rateAt >= std::chrono::seconds(1)) {
            txRate = static_cast<int>(live->PacketsSent() - rateTx);
            rxRate = static_cast<int>(live->PacketsReceived() - rateRx);
            rateTx = live->PacketsSent();
            rateRx = live->PacketsReceived();
            rateAt = now;
        }
        ui::DebugInfo d;
        d.version = std::string("Perlin ") + PERLIN_VERSION + " (bedrock " + kBedrockVersion + "/vanilla)";
        d.fps = fpsShown;
        d.maxFps = options.maxFps;
        d.vsync = options.vsync;
        d.fancyGraphics = options.fancyGraphics;
        const GraphicsSettings gfx = GraphicsSettings::From(options);
        d.clouds = gfx.clouds;
        d.fancyClouds = gfx.fancyClouds;
        d.biomeBlend = gfx.biomeBlendRadius;
        d.tx = txRate;
        d.rx = rxRate;
        d.renderDistance = options.renderDistance;
        d.simulationDistance = options.renderDistance;
        if (levelRenderer) {
            d.chunksRendered = static_cast<int>(levelRenderer->MeshedChunks());
            d.pendingChunks = static_cast<int>(levelRenderer->PendingMeshes());
            d.pendingUploads = static_cast<int>(levelRenderer->MeshesInFlight());
            d.availableBuffers = static_cast<int>(levelRenderer->MaxMeshesInFlight() - std::min(levelRenderer->MaxMeshesInFlight(), levelRenderer->MeshesInFlight()));
        }
        if (level) {
            std::lock_guard lock(level->mutex);
            d.chunksLoaded = static_cast<int>(level->chunks.size());
        }
        static const char* kDimensions[] = {"minecraft:overworld", "minecraft:the_nether", "minecraft:the_end"};
        if (live)
            d.dimension = kDimensions[std::clamp(live->DimensionId(), 0, 2)];

        const glm::vec3 eye = camera.EyePosition();
        d.x = camera.x;
        d.y = camera.y - 1.62;
        d.z = camera.z;
        d.yaw = camera.yaw - 90.0f;
        d.pitch = -camera.pitch;
        d.skyLight = dbgSky;
        d.blockLight = dbgBlock;
        d.compiler = sysinfo::Compiler();
        d.memUsed = sysinfo::ProcessMemory();
        d.memPeak = sysinfo::ProcessPeakMemory();
        d.memTotal = sysinfo::TotalMemory();
        d.cpuThreads = sysinfo::CpuThreads();
        static const std::string cpu = sysinfo::CpuName();
        d.cpuName = cpu;
        int w = 0, h = 0;
        platform->GetFramebufferSize(w, h);
        d.displayW = w;
        d.displayH = h;
        if (renderer) {
            d.gpuLine1 = renderer->RendererName();
            d.displayVendor = renderer->GpuVendor();
            d.gpuLine2 = renderer->GpuDescription();
        }
        if (level) {
            const int bx = static_cast<int>(std::floor(d.x)), bz = static_cast<int>(std::floor(d.z));
            bool loaded = false;
            level->GetBlock(bx, static_cast<int>(std::floor(d.y)), bz, loaded);
            d.inLoadedChunk = loaded;
            if (loaded) {
                if (now - heightAt > std::chrono::milliseconds(500)) {
                    heightAt = now;
                    BlockPalette& palette = BlockPalette::Instance();
                    heightSurface = level->FindBlockBelow(bx, bz, 319, [&](uint16_t id) { return palette.Name(id) != "minecraft:air"; }) + 1;
                }
                d.heightWorldSurface = d.heightMotionBlocking = heightSurface;
                d.biome = levelRenderer ? levelRenderer->Tints().NameForId(level->GetBiome(bx, static_cast<int>(std::floor(d.y)), bz))
                                        : "minecraft:plains";
                if (d.biome.rfind("minecraft:", 0) != 0)
                    d.biome = "minecraft:" + d.biome;
            }
            RayTargets(*level, eye, camera.Direction(), 20.0, d);
        }
        static std::vector<std::string> cachedLeft, cachedRight;
        static auto cachedAt = Clock::time_point{};
        if (cachedLeft.empty() || now - cachedAt > std::chrono::milliseconds(100)) {
            cachedAt = now;
            cachedLeft.clear();
            cachedRight.clear();
            ui::BuildDebugLines(d, cachedLeft, cachedRight);
        }
        s.debugLeft = cachedLeft;
        s.debugRight = cachedRight;
        ApplyModDebugLines(s.debugLeft, s.debugRight);
    }

    void Perlin::UpdateAudio(BedrockSession* live, double now, float dt) {
        static bool opened = false, hooked = false;
        const std::string& root = ui::UiAssets::Instance().VanillaRoot();
        if (!opened) {
            opened = true;
            if (!audioEngine.Open())
                std::cout << "[audio] running without an output device" << std::endl;
        }
        if (!hooked) {
            hooked = true;
            sounds.SetBlockNamer([](int32_t runtimeId) {
                BlockPalette& palette = BlockPalette::Instance();
                return palette.Name(palette.InternRuntime(runtimeId));
            });
            sounds.SetSubtitleHook([this](const std::string& sound, const glm::dvec3& position, float volume, bool global) {
                if (options.sound.subtitles)
                    subtitles.Add(sound, position, volume, global, static_cast<double>(std::chrono::duration<double>(
                                                                         std::chrono::steady_clock::now().time_since_epoch()).count()));
            });
        }
        if (!root.empty())
            sounds.Configure(root, ServerPacks::Instance().Directories());
        audio::Volumes volumes;
        const SoundSettings& v = options.sound;
        volumes.v = {v.master, v.music, v.record, v.weather, v.block, v.hostile, v.neutral, v.player, v.ambient, v.voice};
        audioEngine.SetVolumes(volumes);
        audioEngine.SetListener({camera.x, camera.y, camera.z}, camera.yaw - 90.0f);

        static double nextMusic = 0;
        if (live && options.sound.music > 0.0f) {
            if (nextMusic == 0)
                nextMusic = now + 10.0 + (std::rand() % 2000) / 100.0;
            if (now >= nextMusic) {
                const std::string event = live->GameType() == 1 ? "music.game.creative" : "music.game";
                const bool started = sounds.PlayNamed(event, {camera.x, camera.y, camera.z}, 1.0f, 1.0f, false);
                nextMusic = now + (started ? 60.0 + (std::rand() % 12000) / 100.0 + 120.0 : 300.0);
            }
        }

        if (live) {
            for (const BedrockSession::SoundRequest& r : live->TakeSounds()) {
                const glm::dvec3 at(r.x, r.y, r.z);
                NoteModSound(r.name, r.x, r.y, r.z, r.volume, r.pitch);
                switch (r.kind) {
                case BedrockSession::SoundRequest::Kind::Named:
                    sounds.PlayNamed(r.name, at, r.volume, r.pitch, !r.global);
                    break;
                case BedrockSession::SoundRequest::Kind::Level:
                    sounds.PlayLevelSound(r.name, at, r.extra, r.entityType, r.baby, r.global);
                    break;
                case BedrockSession::SoundRequest::Kind::Stop:
                    if (r.stopAll)
                        audioEngine.StopAll();
                    else
                        sounds.Stop(r.name);
                    break;
                }
            }

            const physics::State& st = player.State();
            static physics::State last = st;
            static double walked = 0, nextStep = 1;
            static bool haveLast = false;
            if (!haveLast) {
                last = st;
                haveLast = true;
            }
            Level& level = live->NetLevel();
            const auto blockUnder = [&]() -> std::string {
                for (double dy : {0.2, 0.6, 1.1}) {
                    bool loaded = false;
                    const uint16_t id = level.GetBlock(static_cast<int>(std::floor(st.x)), static_cast<int>(std::floor(st.y - dy)),
                                                       static_cast<int>(std::floor(st.z)), loaded);
                    if (!loaded)
                        continue;
                    const std::string name = BlockPalette::Instance().Name(id);
                    if (name != "minecraft:air" && !sounds.BlockSoundType(name).empty())
                        return name;
                }
                return {};
            };
            const glm::dvec3 feet(st.x, st.y, st.z);
            if (hudState.mode != ui::GameMode::Spectator) {
                static double swimClock = 0;
                swimClock += dt;
                if (st.inWater && !last.inWater && std::fabs(st.vy) > 0.1)
                    sounds.PlayEntityEvent("minecraft:player", "splash", feet);
                else if (st.inWater && swimClock > 0.45 && std::hypot(st.x - last.x, st.z - last.z) > 0.03) {
                    swimClock = 0;
                    sounds.PlayEntityEvent("minecraft:player", "swim", feet);
                }
                if (st.onGround && !last.onGround && !st.inWater) {
                    const std::string under = blockUnder();
                    if (!under.empty())
                        sounds.PlayBlockEvent(under, last.y - st.y > 3.0 ? "fall" : "land", feet);
                } else if (!st.onGround && last.onGround && st.y > last.y && !st.inWater) {
                    const std::string under = blockUnder();
                    if (!under.empty())
                        sounds.PlayBlockEvent(under, "jump", feet);
                }
                if (st.onGround && !st.inWater && !st.onClimbable && !player.Sneaking()) {
                    walked += std::hypot(st.x - last.x, st.z - last.z) * 0.6;
                    if (walked > nextStep) {
                        nextStep = std::floor(walked) + 1.0;
                        const std::string under = blockUnder();
                        if (!under.empty())
                            sounds.PlayBlockEvent(under, "step", feet);
                    }
                }
            }
            last = st;
        }

        hudState.subtitles.clear();
        if (options.sound.subtitles) {
            const double clockNow = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            for (const audio::Subtitles::Line& l : subtitles.Visible({camera.x, camera.y, camera.z}, camera.yaw - 90.0f, clockNow))
                hudState.subtitles.push_back({l.text, l.direction, l.brightness});
        } else {
            subtitles.Clear();
        }
        if (PERLIN_ENV("PERLIN_PREVIEW_BORDERS"))
            showChunkBorders = true;
        if (PERLIN_ENV("PERLIN_PREVIEW_SUBTITLES")) {
            hudState.subtitles = {{"Cow moos", -1, 1.0f}, {"Footsteps", 0, 0.8f}, {"Zombie groans", 1, 0.5f}, {"Explosion", 1, 0.3f}};
        }
        (void)dt;
        (void)now;
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

    namespace {
        bool IsAmmoFor(const std::string& weapon, const std::string& name) {
            if (name == "minecraft:arrow" || name == "minecraft:spectral_arrow" || name == "minecraft:tipped_arrow")
                return true;
            return weapon == "minecraft:crossbow" && name == "minecraft:firework_rocket";
        }

        bool WeaponNeedsAmmo(const ui::HudState& st, const std::string& weapon) {
            return (weapon == "minecraft:bow" || weapon == "minecraft:crossbow") && st.Survival();
        }

        bool HasAmmo(const ui::HudState& st, BedrockSession* live, const std::string& weapon) {
            if (!WeaponNeedsAmmo(st, weapon))
                return true;
            if (!st.offhand.Empty() && IsAmmoFor(weapon, st.offhand.name))
                return true;
            if (live) {
                const auto inv = live->GetInventory();
                for (const auto& it : inv.hotbar)
                    if (!it.Empty() && IsAmmoFor(weapon, it.name))
                        return true;
                for (const auto& it : inv.main)
                    if (!it.Empty() && IsAmmoFor(weapon, it.name))
                        return true;
                return false;
            }
            for (const auto& it : st.hotbar)
                if (!it.Empty() && IsAmmoFor(weapon, it.name))
                    return true;
            return false;
        }

        ui::HudItem ToHudItem(const BedrockSession::ItemStackInfo& info) {
            ui::HudItem item;
            if (info.Empty())
                return item;
            item.name = info.name;
            item.count = info.count;
            item.blockRuntimeId = info.blockRuntimeId;
            item.enchanted = info.enchanted;
            item.customName = info.customName;
            item.hasColor = info.hasColor;
            item.color = info.color;
            item.nbtTags = info.nbtTags;
            item.enchants = info.enchants;
            item.lore = info.lore;
            item.maxDamage = ui::MaxDurability(info.name);
            item.aux = item.maxDamage > 0 ? std::clamp(info.damage, 0, item.maxDamage) : info.aux;
            return item;
        }
    }

    namespace {
        bool IsInteractable(const std::string& type) {
            if (type == "minecraft:cushion")
                return true;
            if (type.find("boat") != std::string::npos || type.find("raft") != std::string::npos)
                return type.find("chest") == std::string::npos;
            if (type.find("minecart") != std::string::npos)
                return type == "minecraft:minecart";
            return type == "minecraft:horse" || type == "minecraft:donkey" || type == "minecraft:mule" || type == "minecraft:camel" ||
                   type == "minecraft:strider" || type == "minecraft:pig" || type == "minecraft:skeleton_horse" || type == "minecraft:zombie_horse";
        }
    }

    void Perlin::UpdateInventoryScreen(ui::Hud& hud, BedrockSession* live, const ui::HudState& state, int fbW, int fbH) {
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_INVENTORY") != nullptr;
        const auto now = Clock::now();
        if (live)
            live->InvPump();
        const bool canOpen = (live || preview) && !deathShown && !formShown && !pauseShown && !chat.IsOpen() && state.mode != ui::GameMode::Spectator;
        const bool workbenchOpened = live && live->TakeWorkbenchOpened();
        const bool horseOpened = live && live->TakeHorseOpened();
        const bool chestOpened = live && live->TakeChestOpened();
        if (!invShown && (workbenchOpened || horseOpened || chestOpened) && !deathShown) {
            if (chestOpened) {
                chestUi.Warm(ServerPacks::Instance().Directories());
                chestUi.WaitReady(150);
            }
            invShown = true;
            invOpenedAt = now;
            invPicked = -1;
            invSignature.clear();
            inputManager.TakeTyped();
            input.SetTextMode(true);
            CenterCursorForGui();
        }
        if (!invShown && canOpen && (inputManager.Pressed(Action::Inventory) || (preview && !invPreviewOpened))) {
            invPreviewOpened = true;
            invShown = true;
            invOpenedAt = now;
            invPicked = -1;
            invSignature.clear();
            inputManager.TakeTyped();
            input.SetTextMode(true);
            CenterCursorForGui();
            if (live) {
                if (live->RidingUid() != 0 && live->OpenMountInventory())
                    invMountAsk = now;
                else {
                    live->OpenInventory();
                    invMountAsk = std::chrono::steady_clock::time_point{};
                }
            }
        }
        if (invShown && live && invMountAsk != std::chrono::steady_clock::time_point{} && now - invMountAsk > std::chrono::milliseconds(450)) {
            invMountAsk = std::chrono::steady_clock::time_point{};
            if (live->RidingUid() != 0 && !live->HorseOpen())
                live->OpenInventory();
        }
        if (!invShown)
            return;
        bool close = false;
        invWorkbench = live ? live->WorkbenchOpen() : (preview && PERLIN_ENV("PERLIN_PREVIEW_WORKBENCH") != nullptr);
        invHorse = live && live->HorseOpen();
        const char* previewChest = preview ? PERLIN_ENV("PERLIN_PREVIEW_CHEST") : nullptr;
        invChest = (live && live->ChestOpen()) || previewChest;
        static bool bookInit = false;
        if (preview && !bookInit && PERLIN_ENV("PERLIN_PREVIEW_BOOK")) {
            bookInit = true;
            invBook = true;
        }
        const bool creativeMode = (live || preview) && state.mode == ui::GameMode::Creative && !invWorkbench && !invHorse && !invChest;
        const bool searching = creativeMode && creativeTab == ui::kCreativeSearchTab;
        if (deathShown || (live && live->TakeServerClosedInventory()))
            close = true;
        if (invCloseClicked) {
            invCloseClicked = false;
            close = true;
        }
        const auto typedIs = [&](const InputManager::Typed& t, Action a) {
            const Binding& b = options.GetKey(a);
            return t.character && b.device == Binding::Device::Keyboard && std::toupper(t.key) == b.code;
        };
        for (const auto& t : inputManager.TakeTyped()) {
            if (now - invOpenedAt < std::chrono::milliseconds(150))
                continue;
            if (!t.character && t.key == Key::Escape)
                close = true;
            else if (searching && ui::TextEdit::Focused().Apply(t, creativeSearch, ui::TextLimits{50})) {
            }
            else if (typedIs(t, Action::Inventory) && !searching)
                close = true;
            else if (typedIs(t, Action::Drop) && live && invHoverAddr >= 0)
                live->InvDropSlot(invHoverAddr, t.ctrl);
            else if (live && invHoverAddr >= 0) {
                for (int h = 0; h < 9; h++)
                    if (typedIs(t, static_cast<Action>(static_cast<int>(Action::Hotbar1) + h))) {
                        live->InvHotbarKey(invHoverAddr, h);
                        break;
                    }
            }
        }
        if (close) {
            if (live)
                live->CloseInventory();
            hud.Overlay().ClearOverlay();
            invShown = false;
            invHoverId.clear();
            invHover = -1;
            invHoverAddr = -1;
            if (!chat.IsOpen() && !deathShown)
                input.SetTextMode(false);
            return;
        }

        if (creativeMode || (preview && PERLIN_ENV("PERLIN_PREVIEW_CREATIVE") && !invWorkbench)) {
            UpdateCreativeScreen(hud, live, state, fbW, fbH);
            return;
        }

        if (PERLIN_ENV("PERLIN_PREVIEW_ADV"))
            gAdvancedTooltips = true;
        ui::InventoryView v;
        v.advancedTooltips = gAdvancedTooltips;
        if (live) {
            const auto inv = live->GetInventory();
            for (int i = 0; i < 9; i++)
                v.slots[i] = ToHudItem(inv.hotbar[i]);
            for (int i = 0; i < 27; i++)
                v.slots[9 + i] = ToHudItem(inv.main[i]);
            for (int i = 0; i < 4; i++) {
                v.armor[i] = ToHudItem(inv.armor[i]);
                v.craft[i] = ToHudItem(inv.craft[i]);
            }
            for (int i = 0; i < 9; i++)
                v.work[i] = ToHudItem(inv.work[i]);
            v.horse = inv.horseOpen;
            v.horseChest = inv.horseChestSlots;
            for (int i = 0; i < 17; i++)
                v.horseItems[static_cast<size_t>(i)] = ToHudItem(inv.horse[i]);
            if (inv.chestOpen) {
                v.chestRows = inv.chestSlots > 27 ? 6 : 3;
                for (int i = 0; i < 54; i++)
                    v.chestItems[static_cast<size_t>(i)] = ToHudItem(inv.chest[i]);
                v.title = i18n::Tr(v.chestRows == 6 && inv.chestTitle == "container.chest" ? "container.chestDouble" : inv.chestTitle);
                invChestScreen = ui::ChestScreenName(inv.chestTitle, v.chestRows);
            }
            v.offhand = ToHudItem(inv.offhand);
            v.output = ToHudItem(inv.output);
            v.cursor = ToHudItem(inv.cursor);
            v.selectedHotbar = inv.selected;
        } else {
            for (int i = 0; i < 9; i++)
                v.slots[i] = state.hotbar[i];
            v.slots[9].name = "minecraft:cobblestone"; v.slots[9].count = 64;
            v.slots[10].name = "minecraft:oak_planks"; v.slots[10].count = 12;
            v.slots[20].name = "minecraft:diamond"; v.slots[20].count = 3;
            for (int i = 0; i < 4; i++)
                v.armor[i] = state.armorItems[i];
            v.selectedHotbar = state.selectedSlot;
            if (previewChest) {
                v.chestRows = std::atoi(previewChest) > 27 ? 6 : 3;
                static const char* const kLoot[] = {"minecraft:iron_ingot", "minecraft:bread", "minecraft:diamond_sword", "minecraft:oak_log", "minecraft:gold_ingot"};
                for (int i = 0; i < v.chestRows * 9; i += 4) {
                    v.chestItems[static_cast<size_t>(i)].name = kLoot[(i / 4) % 5];
                    v.chestItems[static_cast<size_t>(i)].count = i % 3 == 0 ? 1 : 16 + i;
                }
                v.title = i18n::Tr(v.chestRows == 6 ? "container.chestDouble" : "container.chest");
                invChestScreen = ui::ChestScreenName("container.chest", v.chestRows);
            }
        }

        v.effects = state.effects;
        if (invDragButton != 0 && invDragSlots.size() >= 2 && !v.cursor.Empty()) {
            const auto viewItem = [&](int a) -> ui::HudItem* {
                if (a < 36) return &v.slots[static_cast<size_t>(a)];
                if (a < 40) return &v.armor[static_cast<size_t>(a - 36)];
                if (a == 40) return &v.offhand;
                if (a < 45) return &v.craft[static_cast<size_t>(a - 41)];
                if (a >= 47 && a < 56) return &v.work[static_cast<size_t>(a - 47)];
                if (a >= ui::kInvAddrChest && a < ui::kInvAddrChest + 54) return &v.chestItems[static_cast<size_t>(a - ui::kInvAddrChest)];
                return nullptr;
            };
            std::vector<ui::HudItem*> targets;
            for (int a : invDragSlots)
                if (ui::HudItem* it = viewItem(a); it && (it->Empty() || (it->name == v.cursor.name && it->aux == v.cursor.aux)))
                    targets.push_back(it);
            if (targets.size() >= 2) {
                const int total = v.cursor.count;
                const int each = invDragButton == 2 ? 1 : std::max(1, total / static_cast<int>(targets.size()));
                int left = total;
                for (ui::HudItem* it : targets) {
                    const int n = std::min(each, left);
                    if (n <= 0) break;
                    if (it->Empty()) {
                        const ui::HudItem proto = v.cursor;
                        *it = proto;
                        it->count = n;
                    } else
                        it->count += n;
                    left -= n;
                }
                v.cursor.count = left;
                if (left <= 0)
                    v.cursor = ui::HudItem{};
            }
        }
        v.workbench = invWorkbench;
        v.bookOpen = invBook;
        if (invBook && now - invRecipeAt > std::chrono::milliseconds(300)) {
            invRecipeAt = now;
            std::vector<BedrockSession::RecipeEntry> all;
            if (live) {
                all = live->GetRecipes(invWorkbench ? 3 : 2);
            } else {
                static const char* const kNames[] = {"minecraft:oak_planks", "minecraft:stick", "minecraft:crafting_table", "minecraft:torch", "minecraft:wooden_pickaxe", "minecraft:chest", "minecraft:furnace", "minecraft:bread"};
                for (int i = 0; i < 26; i++) {
                    BedrockSession::RecipeEntry e;
                    e.index = i;
                    e.craftable = i % 3 != 0;
                    e.out.name = kNames[i % 8];
                    e.out.count = i % 5 == 0 ? 4 : 1;
                    all.push_back(e);
                }
            }
            std::stable_sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.craftable && !b.craftable; });
            invRecipeAll = std::move(all);
        }
        if (invBook) {
            constexpr int perPage = ui::kBookCols * ui::kBookRows;
            v.bookPages = std::max(1, static_cast<int>((invRecipeAll.size() + perPage - 1) / perPage));
            invBookPage = std::clamp(invBookPage, 0, v.bookPages - 1);
            v.bookPage = invBookPage;
            v.recipes.clear();
            for (int n = 0; n < perPage; n++) {
                const size_t at = static_cast<size_t>(invBookPage * perPage + n);
                if (at >= invRecipeAll.size())
                    break;
                ui::RecipeCard c;
                c.out = ToHudItem(invRecipeAll[at].out);
                c.craftable = invRecipeAll[at].craftable;
                c.index = invRecipeAll[at].index;
                v.recipes.push_back(std::move(c));
            }
        }

        if (now - invCheckAt > std::chrono::milliseconds(2000) || invTemplate.empty()) {
            invCheckAt = now;
            uint64_t version = 0;
            std::string text = ui::LoadUiTemplate(paths::DataDir(), "inventory_screen.rml", ui::DefaultInventoryScreenTemplate(), version);
            if (version != invTemplateVersion || invTemplate.empty()) {
                invTemplateVersion = version;
                invTemplate = std::move(text);
                invSignature.clear();
                std::cout << "[ui] inventory screen: loaded " << paths::DataDir() << "/ui/inventory_screen.rml" << std::endl;
            }
        }

        const int scale = hud.OverlayScale();
        const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
        const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));
        float mx = input.MouseX(), my = input.MouseY();
        if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))
            std::sscanf(m, "%f,%f", &mx, &my);
        const float gx = mx / static_cast<float>(scale), gy = my / static_cast<float>(scale);
        invMouseX = gx;
        invMouseY = gy;
        if (hud.Overlay().HasOverlay()) {
            hud.Overlay().MouseMove(gx, gy);
            hud.UpdateOverlay(fbW, fbH, options.guiScale);
            invHoverId = hud.Overlay().OverlayHover();
            invHoverAddr = ui::InventoryAddrFromId(invHoverId);
        }
        v.hovered = invHoverAddr;
        v.hoveredId = invHoverId;
        v.mouseX = gx;
        v.mouseY = gy;

        std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "|" + std::to_string(invHoverAddr) + invHoverId + (invBook ? "B" : "b") + std::to_string(invBookPage) + (invWorkbench ? "W" : "w") + std::to_string(v.recipes.size());
        for (const auto& e : v.effects)
            signature += std::to_string(e.id) + ":" + std::to_string(e.amplifier) + ":" + std::to_string(e.ticks / 20) + ";";
        const auto sig = [&](const ui::HudItem& it) { signature += it.name + ":" + std::to_string(it.count) + ":" + std::to_string(it.aux) + (it.enchanted ? "g" : "") + std::to_string(it.enchants.size()) + (it.hasColor ? std::to_string(it.color) : "") + ";"; };
        for (const auto& it : v.slots) sig(it);
        for (const auto& it : v.armor) sig(it);
        for (const auto& it : v.craft) sig(it);
        for (const auto& it : v.work) sig(it);
        if (v.chestRows) {
            chestUi.Warm(ServerPacks::Instance().Directories());
            signature += "K" + std::to_string(v.chestRows) + v.title + (chestUi.Ready() ? "J" : "j");
            for (const auto& it : v.chestItems) sig(it);
        }
        if (v.horse) { signature += "H" + std::to_string(v.horseChest); for (const auto& it : v.horseItems) sig(it); }
        for (const auto& c : v.recipes) { signature += c.craftable ? "c" : "n"; sig(c.out); }
        sig(v.offhand);
        sig(v.output);
        sig(v.cursor);
        if (signature != invSignature || !hud.Overlay().HasOverlay()) {
            invSignature = signature;
            std::string error;
            std::string chestRml;
            invJsonPanel = v.chestRows > 0 && chestUi.Build(v, invChestScreen, guiW, guiH, chestRml, invPanel);
            if (v.chestRows > 0 && !invJsonPanel && chestUi.Problem() != invChestProblem) {
                invChestProblem = chestUi.Problem();
                if (!invChestProblem.empty())
                    std::cout << "[chest] json ui: " << invChestProblem << ", using the built-in layout" << std::endl;
            }
            if (invJsonPanel && !hud.Overlay().SetOverlay(chestRml, error)) {
                std::cout << "[chest] " << error << ", using the built-in layout" << std::endl;
                invJsonPanel = false;
            }
            if (!invJsonPanel && !hud.Overlay().SetOverlay(ui::BuildInventoryScreenRml(invTemplate, v, guiW, guiH), error)) {
                std::cout << "[ui] inventory_screen.rml: " << error << std::endl;
                invTemplate = ui::DefaultInventoryScreenTemplate();
                hud.Overlay().SetOverlay(ui::BuildInventoryScreenRml(invTemplate, v, guiW, guiH), error);
            }
            hud.Overlay().MouseMove(gx, gy);
        }
        hud.Overlay().MoveOverlayElement("cursor", static_cast<int>(gx) - 8, static_cast<int>(gy) - 8);
        {
            int tx, ty;
            ui::PlaceTooltip(static_cast<int>(gx), static_cast<int>(gy), guiW, guiH, tx, ty);
            hud.Overlay().MoveOverlayElement("tip", tx, ty);
        }
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool left = input.IsMouseDown(Mouse::Left), right = input.IsMouseDown(Mouse::Right);
        const bool fresh = now - invOpenedAt > std::chrono::milliseconds(150);
        const bool leftEdge = left && !invLeftDown, rightEdge = right && !invRightDown;
        if (live && fresh) {
            const bool slotTarget = invHoverAddr >= 0 && invHoverAddr != ui::kInvAddrOutput;
            if (invDragButton == 0 && (leftEdge || rightEdge) && !v.cursor.Empty() && slotTarget && !input.IsKeyDown(Key::Shift)) {
                invDragButton = leftEdge ? 1 : 2;
                invDragSlots.assign(1, invHoverAddr);
                invSignature.clear();
            } else if (leftEdge || rightEdge) {
                const int px = ui::InventoryPanelX(v, guiW), panelH = ui::InventoryPanelH(v), py = (guiH - panelH) / 2;
                const bool outside = invJsonPanel ? gx < invPanel[0] || gy < invPanel[1] || gx > invPanel[0] + invPanel[2] || gy > invPanel[1] + invPanel[3]
                                                  : gx < px || gy < py || gx > px + ui::kInvPanelW || gy > py + panelH;
                const bool isRight = !leftEdge;
                const bool overBook = invBook && gx >= (guiW - 147) / 2 - 86 && gx < (guiW - 147) / 2 + 61;
                if (invHoverId == "close") {
                    invCloseClicked = true;
                } else if (invHoverId == "rb") {
                    invBook = !invBook;
                    invRecipeAt = {};
                } else if (invHoverId == "rp") {
                    invBookPage--;
                } else if (invHoverId == "rn") {
                    invBookPage++;
                } else if (invBook && invHoverId.size() > 1 && invHoverId[0] == 'r' && std::isdigit(static_cast<unsigned char>(invHoverId[1]))) {
                    const size_t n = static_cast<size_t>(std::atoi(invHoverId.c_str() + 1));
                    if (n < v.recipes.size())
                        live->InvFillRecipe(v.recipes[n].index);
                } else if (invHoverAddr >= 0) {
                    if (DispatchModSlotClick(invHoverAddr, isRight, input.IsKeyDown(Key::Shift))) {
                    } else if (input.IsKeyDown(Key::Shift))
                        live->InvShiftClick(invHoverAddr);
                    else
                        live->InvClick(invHoverAddr, isRight);
                } else if (outside && !overBook) {
                    live->InvDropCursor(!isRight);
                }
                invSignature.clear();
            }
            if (invDragButton != 0) {
                const bool held = invDragButton == 1 ? left : right;
                if (held) {
                    if (slotTarget && std::find(invDragSlots.begin(), invDragSlots.end(), invHoverAddr) == invDragSlots.end()) {
                        invDragSlots.push_back(invHoverAddr);
                        invSignature.clear();
                    }
                } else {
                    if (invDragSlots.size() >= 2)
                        live->InvSpread(invDragSlots, invDragButton == 2 ? 1 : 0);
                    else if (invDragSlots.size() == 1)
                        live->InvClick(invDragSlots[0], invDragButton == 2);
                    invDragButton = 0;
                    invDragSlots.clear();
                    invSignature.clear();
                }
            }
        } else if (!left && !right) {
            invDragButton = 0;
            invDragSlots.clear();
        }
        invLeftDown = left;
        invRightDown = right;
    }

    namespace {
        std::string SkinDollKey(const std::string& skinPath, const std::string& capePath, bool showCape, float headYaw = 0.0f,
                                float headPitch = 0.0f);
    }

    void Perlin::UpdatePauseScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_PAUSE") != nullptr;
        const auto now = Clock::now();
        const bool canOpen = (live || preview) && !deathShown && !formShown && !invShown && !chat.IsOpen() && !bedShown;
        if (modOpenView >= 0 && canOpen) {
            if (!pauseShown) {
                pauseShown = true;
                pauseOpenedAt = now;
                pauseLeftDown = true;
                inputManager.TakeTyped();
                input.SetTextMode(true);
                CenterCursorForGui();
            }
            pauseView = modOpenView;
            optionsPage = modOpenPage;
            optionsSub = 0;
            if (pauseView == 5)
                wardInit = false;
            if (optionsPage == 11 && !modSettingsFor.empty())
                modSelected = modSettingsFor;
            modOpenView = -1;
            modOpenPage = 0;
            pauseSignature.clear();
            optionsSignature.clear();
            hud.Overlay().ClearOverlay();
        }
        if (!pauseShown && titleOptions && titleActive) {
            pauseShown = true;
            pauseView = titleOpenView;
            titleOpenView = 1;
            if (pauseView == 5)
                wardInit = false;
            optionsPage = 0;
            optionsSub = 0;
            pauseOpenedAt = now;
            pauseSignature.clear();
            optionsSignature.clear();
            pauseLeftDown = true;
            inputManager.TakeTyped();
            input.SetTextMode(true);
        } else if (!pauseShown && canOpen && (inputManager.Pressed(Action::Pause) || (preview && !pausePreviewOpened))) {
            pausePreviewOpened = true;
            pauseShown = true;
            pauseView = PERLIN_ENV("PERLIN_PREVIEW_OPTIONS") ? (PERLIN_ENV("PERLIN_PREVIEW_OPTIONS")[0] == 0x77 ? 3 : PERLIN_ENV("PERLIN_PREVIEW_OPTIONS")[0] == 0x6d ? 4 : PERLIN_ENV("PERLIN_PREVIEW_OPTIONS")[0] == 0x64 ? 5 : 1) : 0;
            optionsPage = PERLIN_ENV("PERLIN_PREVIEW_OPTIONS") ? std::atoi(PERLIN_ENV("PERLIN_PREVIEW_OPTIONS")) : 0;
            pauseOpenedAt = now;
            pauseSignature.clear();
            inputManager.TakeTyped();
            input.SetTextMode(true);
            CenterCursorForGui();
        } else if (!pauseShown && !live && !preview && inputManager.Pressed(Action::Pause)) {
            platform->SetCursorCaptured(false);
        }
        if (pauseView != 5 || !pauseShown)
            wardInit = false;
        if (!pauseShown)
            return;
        bool resume = deathShown || invShown || chat.IsOpen();
        if (bindCapture < 0)
        for (const auto& t : inputManager.TakeTyped()) {
            if (pauseView == 4) {
                ui::TextEdit::Focused().Apply(t, modSearch, ui::TextLimits{40});
            }
            if (!t.character && t.key == Key::Escape && now - pauseOpenedAt > std::chrono::milliseconds(100)) {
                if (pauseView == 1 && optionsPage == 11) {
                    mods::ModManager::Instance().SettingsClosed(modSettingsFor);
                    pauseView = 4;
                    optionsPage = 0;
                    optionsSub = 0;
                    optionsSignature.clear();
                    hud.Overlay().ClearOverlay();
                } else if (pauseView == 1 && optionsPage != 0) {
                    CommitTexturePacks();
                    options.Save();
                    shaderPendingInit = false;
                    optionsPage = optionsPage == 5 ? 3 : optionsPage == 10 ? 8 : 0;
                    optionsSub = 0;
                    kbScroll = 0;
                    optionsSignature.clear();
                    hud.Overlay().ClearOverlay();
                } else if (pauseView != 0) {
                    options.Save();
                    pauseView = 0;
                    optionsSignature.clear();
                    pauseSignature.clear();
                    hud.Overlay().ClearOverlay();
                } else {
                    resume = true;
                }
            }
        }
        if ((titleOptions || replayCtl.Playing()) && pauseShown && pauseView == 0)
            resume = true;
        if (resume) {
            if (titleOptions) {
                titleOptions = false;
                titleSignature.clear();
            }
            pauseView = 0;
            hud.Overlay().ClearOverlay();
            pauseShown = false;
            if (!chat.IsOpen() && !deathShown && !invShown)
                input.SetTextMode(false);
            return;
        }

        if (now - pauseCheckAt > std::chrono::milliseconds(400) || pauseTemplate.empty()) {
            pauseCheckAt = now;
            uint64_t version = 0;
            std::string text = ui::LoadUiTemplate(paths::DataDir(), "pause_screen.rml", ui::DefaultPauseScreenTemplate(), version);
            if (version != pauseTemplateVersion || pauseTemplate.empty()) {
                pauseTemplateVersion = version;
                pauseTemplate = std::move(text);
                pauseSignature.clear();
                std::cout << "[ui] pause screen: loaded " << paths::DataDir() << "/ui/pause_screen.rml" << std::endl;
            }
        }
        const auto tr = [](const char* key, const char* fallback) {
            std::string v = i18n::Tr(key);
            return v == key ? std::string(fallback) : v;
        };
        const int scale = hud.OverlayScale();
        const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
        const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));
        float mx = input.MouseX(), my = input.MouseY();
        if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))
            std::sscanf(m, "%f,%f", &mx, &my);
        bool down = input.IsMouseDown(0);
        if (const char* spec = PERLIN_ENV("PERLIN_PREVIEW_CLICK")) {
            const double elapsed = std::chrono::duration<double>(now - pauseOpenedAt).count();
            std::string rest = spec;
            size_t pos = 0;
            while (pos < rest.size()) {
                size_t end = rest.find(',', pos);
                if (end == std::string::npos)
                    end = rest.size();
                double at = 0;
                float cx = 0, cy = 0;
                if (std::sscanf(rest.substr(pos, end - pos).c_str(), "%lf:%f:%f", &at, &cx, &cy) == 3 && elapsed >= at && elapsed < at + 0.25) {
                    mx = cx;
                    my = cy;
                    down = true;
                }
                pos = end + 1;
            }
        }
        const bool clickReady = now - pauseOpenedAt > std::chrono::milliseconds(150);

        if (pauseView == 1 && optionsPage == 5) {
            struct Cat {
                const char* name;
                std::vector<std::pair<Action, const char*>> items;
            };
            static const Cat kCats[] = {
                {"Movement", {{Action::Jump, "Jump"}, {Action::Sneak, "Sneak"}, {Action::Sprint, "Sprint"}, {Action::Left, "Strafe Left"},
                              {Action::Right, "Strafe Right"}, {Action::Back, "Walk Backwards"}, {Action::Forward, "Walk Forwards"}}},
                {"Gameplay", {{Action::Attack, "Attack/Destroy"}, {Action::PickBlock, "Pick Block"}, {Action::Use, "Use Item/Place Block"}}},
                {"Inventory", {{Action::Drop, "Drop Selected Item"}, {Action::Hotbar1, "Hotbar Slot 1"}, {Action::Hotbar2, "Hotbar Slot 2"},
                               {Action::Hotbar3, "Hotbar Slot 3"}, {Action::Hotbar4, "Hotbar Slot 4"}, {Action::Hotbar5, "Hotbar Slot 5"},
                               {Action::Hotbar6, "Hotbar Slot 6"}, {Action::Hotbar7, "Hotbar Slot 7"}, {Action::Hotbar8, "Hotbar Slot 8"},
                               {Action::Hotbar9, "Hotbar Slot 9"}, {Action::Inventory, "Open/Close Inventory"}}},
                {"Multiplayer", {{Action::PlayerList, "List Players"}, {Action::Chat, "Open Chat"}, {Action::Command, "Open Command"}}},
                {"Miscellaneous", {{Action::DebugScreen, "Debug Screen"}, {Action::ToggleHud, "Hide GUI"}, {Action::Screenshot, "Take Screenshot"},
                                   {Action::CyclePerspective, "Toggle Perspective"}, {Action::Zoom, "Zoom"}, {Action::Fullscreen, "Toggle Fullscreen"}}}};
            const auto keyText = [](const Binding& b) {
                std::string v = b.Serialize();
                for (const char* prefix : {"key.keyboard.", "key.mouse.", "pad."}) {
                    const std::string pre = prefix;
                    if (v.rfind(pre, 0) == 0)
                        v = v.substr(pre.size());
                }
                if (v == "left" || v == "right" || v == "middle")
                    v += " button";
                else if (v == "shift" || v == "control" || v == "alt")
                    v = "left " + v;
                if (v.empty())
                    return std::string("Not bound");
                if (v.size() == 1)
                    v[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(v[0])));
                else {
                    bool word = true;
                    for (char& c : v) {
                        if (c == '_')
                            c = ' ';
                        if (word && c >= 'a' && c <= 'z')
                            c = static_cast<char>(c - 32);
                        word = c == ' ';
                    }
                }
                return v;
            };
            const KeyBindings defaults = bindDevice == 1 ? DefaultPadBindings() : DefaultBindings();
            std::vector<ui::KeybindRow> rows;
            std::vector<int> rowAction;
            for (const Cat& c : kCats) {
                ui::KeybindRow h;
                h.header = true;
                h.label = c.name;
                rows.push_back(h);
                rowAction.push_back(-1);
                for (const auto& it : c.items) {
                    ui::KeybindRow r;
                    r.label = it.second;
                    const Binding& b = BindingOf(it.first);
                    const bool waiting = bindCapture == static_cast<int>(it.first);
                    bool conflict = false;
                    for (size_t o = 0; o < kActionCount; o++)
                        if (o != static_cast<size_t>(it.first) && b.device != Binding::Device::None && (bindDevice == 1 ? options.padKeys[o] : options.keys[o]) == b &&
                            !((it.first == Action::Attack || it.first == Action::Look) && static_cast<Action>(o) == Action::Look) &&
                            !(it.first == Action::Look))
                            conflict = true;
                    r.value = waiting ? "> " + keyText(b) + " <" : conflict ? "[ " + keyText(b) + " ]" : keyText(b);
                    r.color = waiting ? 2 : conflict ? 1 : 0;
                    r.canReset = !(b == defaults[static_cast<size_t>(it.first)]);
                    rows.push_back(r);
                    rowAction.push_back(static_cast<int>(it.first));
                }
            }
            const std::vector<mods::PluginKey> pluginKeys = bindDevice == 1 ? std::vector<mods::PluginKey>{} : mods::ModManager::Instance().Keys();
            std::string lastPlugin;
            for (size_t k = 0; k < pluginKeys.size(); k++) {
                const mods::PluginKey& pk = pluginKeys[k];
                if (pk.plugin != lastPlugin) {
                    lastPlugin = pk.plugin;
                    ui::KeybindRow h;
                    h.header = true;
                    h.label = pk.plugin;
                    rows.push_back(h);
                    rowAction.push_back(-1);
                }
                ui::KeybindRow r;
                r.label = pk.label;
                const Binding b = Binding::Parse(pk.binding);
                const bool waiting = bindCapture == 1000 + static_cast<int>(k);
                r.value = waiting ? "> " + keyText(b) + " <" : keyText(b);
                r.color = waiting ? 2 : 0;
                r.canReset = false;
                rows.push_back(r);
                rowAction.push_back(1000 + static_cast<int>(k));
            }
            const ui::KeybindLayout kl = ui::ComputeKeybindLayout(static_cast<int>(rows.size()), guiW, guiH);
            if (bindCapture < 0)
                kbScroll = std::clamp(kbScroll - inputManager.Scroll() * 20.0f, 0.0f, kl.maxScroll);
            else
                kbScroll = std::clamp(kbScroll, 0.0f, kl.maxScroll);
            std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "k" + std::to_string(static_cast<int>(kbScroll));
            for (const ui::KeybindRow& r : rows)
                signature += "|" + r.value + (r.canReset ? "r" : "");
            if (signature != optionsSignature || !hud.Overlay().HasOverlay()) {
                optionsSignature = signature;
                std::string error;
                if (!hud.Overlay().SetOverlay(ui::BuildKeybindsScreenRml(rows, bindDevice == 1 ? std::string("Controller Binds") : tr("controls.keybinds.title", "Key Binds"), tr("gui.done", "Done"), tr("controls.reset", "Reset"), kbScroll, guiW, guiH), error))
                    std::cout << "[ui] keybinds screen: " << error << std::endl;
            }
            const float gx = mx / static_cast<float>(scale), gy = my / static_cast<float>(scale);
            hud.Overlay().MouseMove(gx, gy);
            hud.UpdateOverlay(fbW, fbH, options.guiScale);
            input.SetPadCapture(bindCapture >= 0 && bindDevice == 1);
            if (bindCapture >= 0) {
                bool captured = false;
                if (bindDevice == 1 && bindCapture < 1000 && Clock::now() - bindStartedAt > std::chrono::milliseconds(300))
                    for (int code = 0; code < 40 && !captured; code++)
                        if (input.IsPadDown(code)) {
                            SetBindingOf(static_cast<Action>(bindCapture), Binding::PadButton(code));
                            captured = true;
                        }
                for (const auto& t : inputManager.TakeTyped()) {
                    if (t.character || Clock::now() - bindStartedAt < std::chrono::milliseconds(120))
                        continue;
                    if (captured)
                        continue;
                    if (t.key != Key::Escape && bindDevice == 1)
                        { captured = true; continue; }
                    if (t.key != Key::Escape) {
                        if (bindCapture >= 1000) {
                            if (const size_t k = static_cast<size_t>(bindCapture - 1000); k < pluginKeys.size())
                                mods::ModManager::Instance().SetKeyBinding(pluginKeys[k].plugin, pluginKeys[k].id, Binding::KeyboardKey(t.key).Serialize());
                        } else
                            SetBindingOf(static_cast<Action>(bindCapture), Binding::KeyboardKey(t.key));
                    }
                    captured = true;
                }
                for (int b = 1; b <= 2 && !captured && bindDevice == 0; b++)
                    if (input.IsMouseDown(b) && Clock::now() - bindStartedAt > std::chrono::milliseconds(120)) {
                        if (bindCapture >= 1000) {
                            if (const size_t k = static_cast<size_t>(bindCapture - 1000); k < pluginKeys.size())
                                mods::ModManager::Instance().SetKeyBinding(pluginKeys[k].plugin, pluginKeys[k].id, Binding::MouseButton(b).Serialize());
                        } else
                            SetBindingOf(static_cast<Action>(bindCapture), Binding::MouseButton(b));
                        captured = true;
                    }
                if (captured) {
                    bindCapture = -1;
                    options.Save();
                    optionsSignature.clear();
                }
                pauseLeftDown = down;
                return;
            }
            const auto inside = [&](float x, float y, float w, float h) { return gx >= x && gx < x + w && gy >= y && gy < y + h; };
            if (down && !pauseLeftDown && clickReady) {
                if (inside(kl.doneX, kl.doneY, kl.doneW, 20.0f)) {
                    options.Save();
                    optionsPage = 3;
                    kbScroll = 0;
                    optionsSignature.clear();
                    hud.Overlay().ClearOverlay();
                } else if (gy >= kl.listTop && gy < kl.listBottom) {
                    for (int i = 0; i < static_cast<int>(rows.size()); i++) {
                        const int action = rowAction[static_cast<size_t>(i)];
                        if (action < 0)
                            continue;
                        const float y = kl.RowY(i, kbScroll);
                        if (inside(kl.keyX, y, kl.keyW, 20.0f)) {
                            bindCapture = action;
                            bindStartedAt = Clock::now();
                            optionsSignature.clear();
                            break;
                        }
                        if (inside(kl.resetX, y, kl.resetW, 20.0f) && rows[static_cast<size_t>(i)].canReset) {
                            SetBindingOf(static_cast<Action>(action), defaults[static_cast<size_t>(action)]);
                            options.Save();
                            optionsSignature.clear();
                            break;
                        }
                    }
                }
            }
            pauseLeftDown = down;
            return;
        }

        if (pauseView == 5) {
            const float gx = mx / static_cast<float>(scale), gy = my / static_cast<float>(scale);
            const auto inside = [&](float x, float y, float w, float h) { return gx >= x && gx < x + w && gy >= y && gy < y + h; };
            const bool click = down && !pauseLeftDown && clickReady;
            if (!wardInit) {
                wardInit = true;
                wardSkin = options.skin;
                wardCape = options.cape;
                wardSlim = options.skinSlim;
                wardPreviewKey.clear();
                wardSkinOpen = true;
                wardCapeOpen = true;
            }
            namespace fsys = std::filesystem;
            const std::string skinDir = paths::DataDir() + "/skins", capeDir = paths::DataDir() + "/capes";
            std::error_code ec;
            fsys::create_directories(skinDir, ec);
            fsys::create_directories(capeDir, ec);
            if (now - wardListedAt > std::chrono::milliseconds(1000)) {
                wardListedAt = now;
                const auto pngs = [](const std::string& dir) {
                    std::vector<std::string> out;
                    std::error_code e;
                    for (const auto& entry : fsys::directory_iterator(dir, e))
                        if (entry.is_regular_file(e) && entry.path().extension() == ".png")
                            out.push_back(entry.path().generic_string());
                    std::sort(out.begin(), out.end());
                    return out;
                };
                wardSkins = pngs(skinDir);
                wardCapes = pngs(capeDir);
            }
            const auto stampOf = [](const std::string& path) {
                std::error_code e;
                return std::to_string(static_cast<unsigned long long>(fsys::file_size(path, e))) + ":" + std::to_string(static_cast<long long>(fsys::last_write_time(path, e).time_since_epoch().count()));
            };
            const auto stem = [](const std::string& path) { return fsys::path(path).stem().string(); };
            const auto slimOf = [&](const std::string& path) {
                if (const auto it = wardSlim.find(path); it != wardSlim.end())
                    return it->second;
                if (const auto it = wardSlimGuess.find(path); it != wardSlimGuess.end())
                    return it->second;
                return false;
            };
            const auto defaultSkin = [&](Texture& t) { return ui::UiAssets::Instance().Image("textures/entity/steve.png", t); };
            const auto renderCard = [&](const std::string& key, const std::vector<unsigned char>* skin, int sw, int sh, bool slim, const std::vector<unsigned char>* cape, int cw, int ch, float yaw) {
                ui::SkinView v;
                v.skin = skin;
                v.skinW = sw;
                v.skinH = sh;
                v.cape = cape;
                v.capeW = cw;
                v.capeH = ch;
                v.slim = slim;
                v.yaw = yaw;
                ui::UiAssets::Instance().SetHead(key, ui::RenderSkin3D(v, 108, 156));
            };
            const auto ensureSkin = [&](const std::string& path) {
                const std::string stamp = stampOf(path);
                auto& slot = wardStamp["skin:" + path];
                if (slot == stamp)
                    return;
                slot = stamp;
                SkinConfig cfg;
                cfg.skinPath = path;
                LoadedSkin loaded;
                std::string err;
                if (!LoadSkinConfig(cfg, loaded, err))
                    return;
                wardSlimGuess[path] = DetectSlimSkin(loaded.skinRgba, loaded.skinWidth, loaded.skinHeight);
                renderCard("skin:" + path, &loaded.skinRgba, loaded.skinWidth, loaded.skinHeight, slimOf(path), nullptr, 0, 0, -32.0f);
            };
            Texture steve;
            const bool haveSteve = defaultSkin(steve);
            const auto ensureCape = [&](const std::string& path) {
                const std::string stamp = stampOf(path);
                auto& slot = wardStamp["cape:" + path];
                if (slot == stamp || !haveSteve)
                    return;
                slot = stamp;
                Texture t;
                if (LoadTexture(path, t))
                    renderCard("cape:" + path, &steve.pixels, steve.width, steve.height, false, &t.pixels, t.width, t.height, 180.0f);
            };
            if (wardStamp.find("default") == wardStamp.end() && haveSteve) {
                wardStamp["default"] = "1";
                renderCard("skin:default", &steve.pixels, steve.width, steve.height, false, nullptr, 0, 0, -32.0f);
                renderCard("cape:none", &steve.pixels, steve.width, steve.height, false, nullptr, 0, 0, 180.0f);
            }
            const auto commit = [this, live]() {
                options.skin = wardSkin;
                options.cape = wardCape;
                options.skinSlim = wardSlim;
                accounts.SetSkin(accounts.ActiveId(), options.skin, options.cape);
                SkinConfig cfg = MakeSkinConfig(options.skin, options.cape);
                LoadedSkin loaded;
                std::string err;
                if (!LoadSkinConfig(cfg, loaded, err)) {
                    std::cout << "[wardrobe] " << err << std::endl;
                    options.Save();
                    return;
                }
                options.slimArms = loaded.slimArms;
                options.Save();
                if (live) {
                    live->SetSkin(std::move(loaded));
                    live->SendSkinUpdate();
                }
            };
            std::vector<ui::WardrobeCard> skinCards, capeCards;
            std::vector<std::string> skinPaths, capePaths;
            const std::string defaultName = tr("perlin.wardrobe.default", "Default");
            skinCards.push_back({defaultName, "mchead:skin:default", wardSkin.empty()});
            skinPaths.push_back("");
            for (const std::string& path : wardSkins) {
                ensureSkin(path);
                skinCards.push_back({stem(path), "mchead:skin:" + path, wardSkin == path});
                skinPaths.push_back(path);
            }
            capeCards.push_back({tr("perlin.wardrobe.noCape", "No Cape"), "mchead:cape:none", wardCape.empty()});
            capePaths.push_back("");
            for (const std::string& path : wardCapes) {
                ensureCape(path);
                capeCards.push_back({stem(path), "mchead:cape:" + path, wardCape == path});
                capePaths.push_back(path);
            }
            if (!wardSkin.empty())
                ensureSkin(wardSkin);
            ui::WardrobeLayout l = ui::ComputeWardrobeLayout(static_cast<int>(skinCards.size()), static_cast<int>(capeCards.size()), wardSkinOpen, wardCapeOpen, wardLine, guiW, guiH);
            if (gx >= l.listX && gx < l.listX + l.listW + 6.0f && gy >= l.listTop && gy < l.listBottom) {
                const float wheel = inputManager.Scroll();
                if (wheel > 0.0f)
                    wardLine--;
                else if (wheel < 0.0f)
                    wardLine++;
            }
            wardLine = std::clamp(wardLine, 0, l.maxFirst);
            if (wardLine != l.firstLine)
                l = ui::ComputeWardrobeLayout(static_cast<int>(skinCards.size()), static_cast<int>(capeCards.size()), wardSkinOpen, wardCapeOpen, wardLine, guiW, guiH);
            const bool overPreview = inside(l.prevX, l.prevY, l.prevW, l.prevH);
            if (click && overPreview) {
                wardDrag = true;
                wardDragX = gx;
                wardDragY = gy;
            }
            if (!down)
                wardDrag = false;
            if (wardDrag) {
                wardYaw += (gx - wardDragX) * 1.6f;
                wardPitch = std::clamp(wardPitch + (gy - wardDragY) * 1.2f, -85.0f, 85.0f);
                wardDragX = gx;
                wardDragY = gy;
            }
            wardYaw = std::fmod(wardYaw, 360.0f);
            const bool slimNow = slimOf(wardSkin);
            const std::string previewKey = wardSkin + "|" + wardCape + "|" + std::to_string(slimNow) + "|" + std::to_string(static_cast<int>(wardYaw)) + "|" +
                                           std::to_string(static_cast<int>(wardPitch)) + "|" + wardStamp["skin:" + wardSkin] + "|" + wardStamp["cape:" + wardCape] + "|" +
                                           std::to_string(options.skinParts.cape) + std::to_string(options.skinParts.jacket) + std::to_string(options.skinParts.leftSleeve) +
                                           std::to_string(options.skinParts.rightSleeve) + std::to_string(options.skinParts.leftPants) + std::to_string(options.skinParts.rightPants) +
                                           std::to_string(options.skinParts.hat) + (wardDrag ? "d" : "s");
            if (previewKey != wardPreviewKey) {
                wardPreviewKey = previewKey;
                SkinConfig cfg;
                cfg.skinPath = wardSkin;
                cfg.capePath = wardCape;
                const bool changedSource = wardPreviewSource != wardSkin + "|" + wardCape + "|" + wardStamp["skin:" + wardSkin] + "|" + wardStamp["cape:" + wardCape];
                if (changedSource) {
                    wardPreviewSource = wardSkin + "|" + wardCape + "|" + wardStamp["skin:" + wardSkin] + "|" + wardStamp["cape:" + wardCape];
                    wardPreviewLoaded = {};
                    std::string err;
                    if (!LoadSkinConfig(cfg, wardPreviewLoaded, err))
                        wardPreviewLoaded = {};
                }
                {
                    const LoadedSkin& loaded = wardPreviewLoaded;
                    ui::UiAssets::Instance().EraseHead("ward:cur" + std::to_string(wardPreviewSerial));
                    wardPreviewSerial++;
                    ui::SkinView v;
                    v.skin = wardSkin.empty() ? &steve.pixels : &loaded.skinRgba;
                    v.skinW = wardSkin.empty() ? steve.width : loaded.skinWidth;
                    v.skinH = wardSkin.empty() ? steve.height : loaded.skinHeight;
                    v.cape = loaded.capeRgba.empty() || !options.skinParts.cape ? nullptr : &loaded.capeRgba;
                    v.capeW = loaded.capeWidth;
                    v.capeH = loaded.capeHeight;
                    v.slim = slimNow;
                    v.yaw = wardYaw;
                    v.pitch = wardPitch;
                    v.jacket = options.skinParts.jacket;
                    v.leftSleeve = options.skinParts.leftSleeve;
                    v.rightSleeve = options.skinParts.rightSleeve;
                    v.leftPants = options.skinParts.leftPants;
                    v.rightPants = options.skinParts.rightPants;
                    v.hat = options.skinParts.hat;
                    const float q = wardDrag ? 1.5f : 3.0f;
                    ui::UiAssets::Instance().SetHead("ward:cur" + std::to_string(wardPreviewSerial),
                                                     ui::RenderSkin3D(v, static_cast<int>(l.prevW * q), static_cast<int>(l.prevH * q)));
                }
            }
            ui::WardrobeText wt;
            wt.title = tr("perlin.wardrobe", "Wardrobe");
            wt.skins = tr("perlin.wardrobe.skins", "Skins");
            wt.capes = tr("perlin.wardrobe.capes", "Capes");
            wt.addSkin = "    " + tr("perlin.wardrobe.addSkin", "Add Skin");
            wt.addCape = "    " + tr("perlin.wardrobe.addCape", "Add Cape");
            wt.slim = tr("perlin.wardrobe.slim", "Slim Arms");
            wt.slimOn = tr("options.on", "ON");
            wt.slimOff = tr("options.off", "OFF");
            wt.done = tr("gui.done", "Done");
            wt.dropHint = tr("perlin.wardrobe.drop", "Drop .png files here to add skins and capes");
            wt.dragHint = tr("perlin.wardrobe.drag", "Drag to rotate");
            const std::string previewName = wardSkin.empty() ? defaultName : stem(wardSkin);
            const std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "w" + std::to_string(l.firstLine) + "|" + previewKey + "|" + std::to_string(wardPreviewSerial) +
                                          "|" + std::to_string(skinCards.size()) + "," + std::to_string(capeCards.size()) + "|" + wardSkin + wardCape + std::to_string(wardSkinOpen) +
                                          std::to_string(wardCapeOpen);
            if (signature != optionsSignature || !hud.Overlay().HasOverlay()) {
                optionsSignature = signature;
                std::string error;
                if (!hud.Overlay().SetOverlay(ui::BuildWardrobeRml(skinCards, capeCards, wardSkinOpen, wardCapeOpen, wardLine, "mchead:ward:cur" + std::to_string(wardPreviewSerial), previewName,
                                                                   slimNow, !wardSkin.empty(), !wardCape.empty(), wt, guiW, guiH),
                                              error))
                    std::cout << "[ui] wardrobe screen: " << error << std::endl;
            }
            hud.Overlay().MouseMove(gx, gy);
            hud.UpdateOverlay(fbW, fbH, options.guiScale);
            if (click && !overPreview) {
                const auto folder = [&](const std::string& dir) {
                    std::error_code e;
                    platform->OpenUrl("file://" + fsys::absolute(dir, e).generic_string());
                };
                const auto removeFile = [&](std::string& current) {
                    std::error_code e;
                    fsys::rename(current, current + ".removed", e);
                    if (options.skin == current)
                        options.skin.clear();
                    if (options.cape == current)
                        options.cape.clear();
                    wardSlim.erase(current);
                    options.skinSlim.erase(current);
                    options.Save();
                    current.clear();
                    wardListedAt = {};
                    wardPreviewKey.clear();
                };
                if (inside(l.doneX, l.doneY, l.doneW, 20.0f)) {
                    commit();
                    wardInit = false;
                    pauseView = 0;
                    pauseSignature.clear();
                    optionsSignature.clear();
                    hud.Overlay().ClearOverlay();
                } else if (inside(l.slimX, l.slimY, l.slimW, 20.0f)) {
                    wardSlim[wardSkin] = !slimNow;
                    wardPreviewKey.clear();
                } else {
                    for (const ui::WardrobeLine& line : l.lines) {
                        const bool isSkin = line.kind < 2;
                        if (line.kind % 2 == 0) {
                            if (inside(l.AddX(), line.y, l.addW, l.headH))
                                if (!platform->PickFile(isSkin ? "skin" : "cape"))
                                    folder(isSkin ? skinDir : capeDir);
                            else if (inside(l.TrashX(), line.y, l.trashW, l.headH)) {
                                if (isSkin && !wardSkin.empty())
                                    removeFile(wardSkin);
                                else if (!isSkin && !wardCape.empty())
                                    removeFile(wardCape);
                            } else if (inside(l.listX, line.y, l.ToggleW(), l.headH)) {
                                bool& open = isSkin ? wardSkinOpen : wardCapeOpen;
                                open = !open;
                            }
                        } else {
                            const int count = static_cast<int>(isSkin ? skinCards.size() : capeCards.size());
                            for (int c = 0; c < l.cols; c++) {
                                const int i = line.row * l.cols + c;
                                if (i >= count)
                                    break;
                                if (!inside(l.CardX(i), line.y, l.cardW, l.cardH))
                                    continue;
                                if (isSkin) {
                                    wardSkin = skinPaths[static_cast<size_t>(i)];
                                } else {
                                    wardCape = capePaths[static_cast<size_t>(i)];
                                }
                                wardPreviewKey.clear();
                                break;
                            }
                        }
                    }
                }
            }
            pauseLeftDown = down;
            return;
        }

        if ((pauseView == 1 && (optionsPage == 6 || optionsPage == 7 || optionsPage == 8)) || pauseView == 4) {
            const float gx = mx / static_cast<float>(scale), gy = my / static_cast<float>(scale);
            const auto inside = [&](float x, float y, float w, float h) { return gx >= x && gx < x + w && gy >= y && gy < y + h; };
            const bool click = down && !pauseLeftDown && clickReady;
            const float wheel = inputManager.Scroll() * 20.0f;
            const auto lower = [](std::string s) {
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                return s;
            };
            const auto openFolder = [&](const std::string& dir) {
                if (platform->PickFile("pack"))
                    return;
                std::error_code e;
                std::filesystem::create_directories(dir, e);
                platform->OpenUrl("file://" + std::filesystem::absolute(dir, e).generic_string());
            };
            const auto leave = [&]() {
                CommitTexturePacks();
                options.Save();
                shaderPendingInit = false;
                if (pauseView == 4)
                    pauseView = 0;
                else
                    optionsPage = 0;
                optionsSignature.clear();
                pauseSignature.clear();
                hud.Overlay().ClearOverlay();
            };
            const auto submit = [&](const std::string& rml, const char* label) {
                std::string error;
                if (!hud.Overlay().SetOverlay(rml, error))
                    std::cout << "[ui] " << label << " screen: " << error << std::endl;
            };
            const auto listPacks = [&](const std::string& dir, bool allowMcpack) {
                if (now - packFoundAt < std::chrono::milliseconds(1000) && packMetaStampDir == dir)
                    return;
                packFoundAt = now;
                packMetaStampDir = dir;
                packFoundCache.clear();
                std::error_code e;
                std::filesystem::create_directories(dir, e);
                for (const auto& entry : std::filesystem::directory_iterator(dir, e)) {
                    const std::string ext = lower(entry.path().extension().string());
                    if (entry.is_directory(e) || ext == ".zip" || (allowMcpack && ext == ".mcpack"))
                        packFoundCache.push_back(entry.path().filename().string());
                }
                std::sort(packFoundCache.begin(), packFoundCache.end());
            };
            const auto packMeta = [&](const std::string& dir, const std::string& name) -> const PackMeta& {
                const std::string path = dir + "/" + name;
                std::error_code e;
                std::string stamp = std::to_string(std::filesystem::is_directory(path, e) ? 0 : std::filesystem::file_size(path, e)) + ":" +
                                    std::to_string(static_cast<long long>(std::filesystem::last_write_time(path, e).time_since_epoch().count()));
                auto& slot = packMetaCache[path];
                if (slot.stamp != stamp) {
                    slot.stamp = stamp;
                    slot.meta = ReadPackMeta(path);
                    if (slot.meta.hasIcon)
                        ui::UiAssets::Instance().SetHead("pack:" + path, slot.meta.icon);
                }
                return slot.meta;
            };
            const auto packIcon = [&](const std::string& dir, const std::string& name, const PackMeta& meta, const char* fallback) {
                return meta.hasIcon ? "mchead:pack:" + dir + "/" + name : std::string(fallback);
            };
            if (pauseView == 1 && optionsPage == 7) {
                const std::string dir = paths::DataDir() + "/texturepacks";
                listPacks(dir, true);
                if (!packPendingInit) {
                    packPendingInit = true;
                    packPending = options.texturePacks;
                }
                std::vector<std::string> existing;
                for (const std::string& name : packPending)
                    if (std::find(packFoundCache.begin(), packFoundCache.end(), name) != packFoundCache.end())
                        existing.push_back(name);
                packPending = existing;
                std::vector<ui::PackEntry> avail, sel;
                std::vector<std::string> availNames;
                for (size_t i = 0; i < packPending.size(); i++) {
                    const std::string& name = packPending[i];
                    const PackMeta& m = packMeta(dir, name);
                    ui::PackEntry e;
                    e.name = EnsureUtf8(m.name.empty() ? name : m.name);
                    e.desc = m.description;
                    e.icon = packIcon(dir, name, m, "textures/blocks/stone");
                    e.canUnselect = true;
                    e.canUp = i > 0;
                    e.canDown = true;
                    sel.push_back(e);
                }
                {
                    ui::PackEntry e;
                    e.name = tr("perlin.packs.defaultName", "Default");
                    e.desc = tr("perlin.packs.defaultDesc", "The default look and feel of Minecraft (built-in)");
                    e.icon = "textures/blocks/grass_side_carried";
                    sel.push_back(e);
                }
                for (const std::string& name : packFoundCache) {
                    if (std::find(packPending.begin(), packPending.end(), name) != packPending.end())
                        continue;
                    const PackMeta& m = packMeta(dir, name);
                    ui::PackEntry e;
                    e.name = EnsureUtf8(m.name.empty() ? name : m.name);
                    e.desc = m.description;
                    e.icon = packIcon(dir, name, m, "textures/blocks/stone");
                    e.canSelect = true;
                    avail.push_back(e);
                    availNames.push_back(name);
                }
                const ui::PackScreenLayout l = ui::ComputePackScreenLayout(static_cast<int>(avail.size()), static_cast<int>(sel.size()), guiW, guiH);
                const bool overLeft = inside(l.leftX, l.top, l.panelW, l.bottom - l.top);
                const bool overRight = inside(l.rightX, l.top, l.panelW, l.bottom - l.top);
                if (overLeft)
                    packScrollA -= wheel;
                else if (overRight)
                    packScrollS -= wheel;
                packScrollA = std::clamp(packScrollA, 0.0f, l.maxScrollA);
                packScrollS = std::clamp(packScrollS, 0.0f, l.maxScrollB);
                std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "p" + std::to_string(static_cast<int>(packScrollA)) + "," +
                                        std::to_string(static_cast<int>(packScrollS));
                for (const auto& e : avail)
                    signature += "|" + e.name + e.desc + e.icon;
                signature += "#";
                for (const auto& e : sel)
                    signature += "|" + e.name + e.desc + e.icon + (e.canUp ? "u" : "");
                if (signature != optionsSignature || !hud.Overlay().HasOverlay()) {
                    optionsSignature = signature;
                    submit(ui::BuildPackScreenRml(avail, sel, tr("perlin.packs.title", "Resource Packs"),
                                                  tr("perlin.packs.hint", "Drag and drop files into this window to add packs"),
                                                  tr("perlin.packs.available", "Available"), tr("perlin.packs.selected", "Selected"),
                                                  tr("perlin.packs.openFolder", "Open Pack Folder"), tr("gui.done", "Done"), packScrollA, packScrollS, guiW, guiH),
                           "resource packs");
                }
                hud.Overlay().MouseMove(gx, gy);
                hud.UpdateOverlay(fbW, fbH, options.guiScale);
                if (click) {
                    const float sbA = l.maxScrollA > 0 ? 6.0f : 0.0f, sbS = l.maxScrollB > 0 ? 6.0f : 0.0f;
                    bool changed = false;
                    if (inside(l.openX, l.btnY, l.btnW, 20.0f)) {
                        openFolder(dir);
                    } else if (inside(l.doneX, l.btnY, l.btnW, 20.0f)) {
                        leave();
                        pauseLeftDown = down;
                        return;
                    } else if (gy >= l.top && gy < l.bottom) {
                        for (int i = 0; i < static_cast<int>(packPending.size()) && !changed; i++) {
                            const float y = l.RowY(i, packScrollS);
                            const float ix = l.rightX + 4.0f, iy = y + 1.0f;
                            if (!inside(ix, iy, 32.0f, 32.0f) || !inside(l.rightX, l.top, l.panelW - sbS, l.bottom - l.top))
                                continue;
                            const float jx = gx - ix, jy = gy - iy;
                            if (jx < 16.0f) {
                                packPending.erase(packPending.begin() + i);
                            } else if (jy < 16.0f) {
                                if (i > 0)
                                    std::swap(packPending[static_cast<size_t>(i)], packPending[static_cast<size_t>(i - 1)]);
                            } else if (i + 1 < static_cast<int>(packPending.size())) {
                                std::swap(packPending[static_cast<size_t>(i)], packPending[static_cast<size_t>(i + 1)]);
                            }
                            changed = true;
                        }
                        for (int i = 0; i < static_cast<int>(avail.size()) && !changed; i++)
                            if (inside(l.leftX + 4.0f, l.RowY(i, packScrollA) + 1.0f, 32.0f, 32.0f) && inside(l.leftX, l.top, l.panelW - sbA, l.bottom - l.top)) {
                                packPending.insert(packPending.begin(), availNames[static_cast<size_t>(i)]);
                                changed = true;
                            }
                    }
                    if (changed)
                        optionsSignature.clear();
                }
                pauseLeftDown = down;
                return;
            }
            if (pauseView == 1 && optionsPage == 6) {
                const auto langs = i18n::Lang::Get().Languages();
                std::vector<std::string> rows;
                int selectedRow = -1;
                for (size_t i = 0; i < langs.size(); i++) {
                    rows.push_back(langs[i].name.empty() ? langs[i].code : langs[i].name);
                    if (langs[i].code == options.language)
                        selectedRow = static_cast<int>(i);
                }
                const ui::ShaderScreenLayout l = ui::ComputeShaderScreenLayout(static_cast<int>(rows.size()), guiW, guiH);
                shaderScroll = std::clamp(shaderScroll - wheel, 0.0f, l.maxScroll);
                std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "l" + std::to_string(static_cast<int>(shaderScroll)) + "|" +
                                        std::to_string(selectedRow) + "|" + std::to_string(ui::McFont::Instance().Version()) + "|";
                for (const auto& r : rows)
                    signature += r + ";";
                if (signature != optionsSignature || !hud.Overlay().HasOverlay()) {
                    optionsSignature = signature;
                    submit(ui::BuildLanguageScreenRml(rows, selectedRow, tr("options.language", "Language"),
                                                      tr("perlin.language.subtitle", "Language translations may not be 100% accurate"), tr("gui.done", "Done"),
                                                      shaderScroll, guiW, guiH),
                           "language");
                }
                hud.Overlay().MouseMove(gx, gy);
                hud.UpdateOverlay(fbW, fbH, options.guiScale);
                if (click) {
                    const float doneX = l.colX + (l.colW - 200.0f) / 2.0f;
                    if (inside(doneX, l.row1Y + 12.0f, 200.0f, 20.0f)) {
                        leave();
                        pauseLeftDown = down;
                        return;
                    }
                    if (gy >= l.listTop && gy < l.listBottom && gx >= l.colX && gx < l.colX + l.colW)
                        for (int i = 0; i < static_cast<int>(rows.size()); i++) {
                            if (!inside(l.colX, l.RowY(i, shaderScroll), l.colW, 18.0f))
                                continue;
                            options.language = langs[static_cast<size_t>(i)].code;
                            i18n::Lang::Get().Configure(ui::UiAssets::Instance().VanillaRoot(), paths::DataDir(), options.language);
                            options.Save();
                            pauseSignature.clear();
                            optionsSignature.clear();
                            break;
                        }
                }
                pauseLeftDown = down;
                return;
            }
            if (pauseView == 1 && optionsPage == 8) {
                const std::string dir = paths::DataDir() + "/shaderpacks";
                listPacks(dir, false);
                if (std::find(packFoundCache.begin(), packFoundCache.end(), kBundledPackName) == packFoundCache.end())
                    packFoundCache.insert(packFoundCache.begin(), kBundledPackName);
                if (!shaderPendingInit) {
                    shaderPendingInit = true;
                    shaderPendingPack = options.shaderPack;
                    shaderPendingEnabled = !options.shaderPack.empty();
                }
                std::vector<std::string> rows;
                rows.push_back(tr("perlin.shaders.title", "Shaders") + ": " + (shaderPendingEnabled ? tr("perlin.shaders.enabled", "Enabled") : tr("perlin.shaders.disabled", "Disabled")));
                int selectedRow = -1;
                for (size_t i = 0; i < packFoundCache.size(); i++) {
                    rows.push_back(packFoundCache[i]);
                    if (shaderPendingEnabled && packFoundCache[i] == shaderPendingPack)
                        selectedRow = static_cast<int>(i) + 1;
                }
                const ui::ShaderScreenLayout l = ui::ComputeShaderScreenLayout(static_cast<int>(rows.size()) + 1, guiW, guiH);
                shaderScroll = std::clamp(shaderScroll - wheel, 0.0f, l.maxScroll);
                const std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "s" + std::to_string(static_cast<int>(shaderScroll)) + "|" +
                                              std::to_string(selectedRow) + "|" + std::to_string(shaderPendingEnabled) + "|" + [&] {
                                                  std::string s;
                                                  for (const auto& r : rows)
                                                      s += r + ";";
                                                  return s;
                                              }();
                if (signature != optionsSignature || !hud.Overlay().HasOverlay()) {
                    optionsSignature = signature;
                    submit(ui::BuildShaderScreenRml(rows, selectedRow, 0, tr("perlin.shaderPacks", "Shader Packs"), tr("perlin.shaders.select", "Select"),
                                                    "+ " + tr("perlin.shaders.drag", "Drag and Drop Shader Packs to add"),
                                                    tr("perlin.shaders.openFolder", "Open Shader Pack Folder..."),
                                                    tr("perlin.shaders.settings", "Shader Pack Settings..."), tr("perlin.shaders.cancel", "Cancel"),
                                                    tr("perlin.shaders.apply", "Apply"), tr("gui.done", "Done"), shaderScroll, guiW, guiH),
                           "shader packs");
                }
                hud.Overlay().MouseMove(gx, gy);
                hud.UpdateOverlay(fbW, fbH, options.guiScale);
                const auto apply = [&]() {
                    options.shaderPack = shaderPendingEnabled ? shaderPendingPack : std::string();
                    options.Save();
                    ApplyShaderPack();
                };
                if (click) {
                    if (inside(l.openX, l.row1Y, l.btnW1, 20.0f)) {
                        openFolder(dir);
                    } else if (inside(l.settingsX, l.row1Y, l.btnW1, 20.0f)) {
                        apply();
                        shaderPendingInit = false;
                        optionsPage = 10;
                        optionsSub = 0;
                        optionsSignature.clear();
                        hud.Overlay().ClearOverlay();
                    } else if (inside(l.cancelX, l.row2Y, l.thirdW, 20.0f)) {
                        leave();
                        pauseLeftDown = down;
                        return;
                    } else if (inside(l.applyX, l.row2Y, l.thirdW, 20.0f)) {
                        apply();
                    } else if (inside(l.doneX, l.row2Y, l.thirdW, 20.0f)) {
                        apply();
                        leave();
                        pauseLeftDown = down;
                        return;
                    } else if (gy >= l.listTop && gy < l.listBottom && gx >= l.colX && gx < l.colX + l.colW) {
                        for (int i = 0; i < static_cast<int>(rows.size()); i++) {
                            if (!inside(l.colX, l.RowY(i, shaderScroll), l.colW, 18.0f))
                                continue;
                            if (i == 0) {
                                shaderPendingEnabled = !shaderPendingEnabled;
                                if (shaderPendingEnabled && shaderPendingPack.empty() && !packFoundCache.empty())
                                    shaderPendingPack = packFoundCache.front();
                                if (shaderPendingEnabled && shaderPendingPack.empty())
                                    shaderPendingEnabled = false;
                            } else {
                                shaderPendingPack = packFoundCache[static_cast<size_t>(i - 1)];
                                shaderPendingEnabled = true;
                            }
                            optionsSignature.clear();
                            break;
                        }
                    }
                }
                pauseLeftDown = down;
                return;
            }
            const std::string modDir = paths::DataDir() + "/mods";
            auto& mm = mods::ModManager::Instance();
            const std::vector<mods::ModInfo> all = mm.List();
            ui::ModEntry unused;
            (void)unused;
            if (modIconStamp.find("*default") == modIconStamp.end()) {
                modIconStamp["*default"] = "1";
                Texture t;
                t.width = t.height = 16;
                t.pixels.assign(16 * 16 * 4, 0);
                for (int y = 0; y < 16; y++)
                    for (int x = 0; x < 16; x++) {
                        unsigned char* p = &t.pixels[static_cast<size_t>((y * 16 + x) * 4)];
                        const bool bar = (y == 4 || y == 5 || y == 8 || y == 9 || y == 12 || y == 13) && x >= 3 && x <= 12;
                        const bool dot = (x == 2 || x == 3) && (y == 4 || y == 5 || y == 8 || y == 9 || y == 12 || y == 13);
                        p[0] = bar || dot ? 255 : 14;
                        p[1] = bar || dot ? 255 : 82;
                        p[2] = bar || dot ? 255 : 255;
                        p[3] = 255;
                    }
                ui::UiAssets::Instance().SetHead("mod:default", t);
            }
            const std::string needle = lower(modSearch);
            std::vector<const mods::ModInfo*> shown;
            for (const mods::ModInfo& info : all) {
                if (!needle.empty() && lower(info.displayName).find(needle) == std::string::npos && lower(info.name).find(needle) == std::string::npos &&
                    lower(info.description).find(needle) == std::string::npos)
                    continue;
                shown.push_back(&info);
            }
            const auto badgeFor = [&](const mods::ModInfo& info, int& color) {
                const std::string state = info.needsApproval ? std::string("approval") : info.enabled ? info.state : std::string("disabled");
                color = state == "running" ? 2 : state == "disabled" ? 3 : state == "loading" ? 0 : 1;
                if (state == "running")
                    return tr("perlin.mods.running", "Running");
                if (state == "disabled")
                    return tr("perlin.mods.disabled", "Disabled");
                if (state == "approval")
                    return tr("perlin.mods.approval", "Needs approval");
                if (state == "loading")
                    return tr("perlin.mods.loading", "Loading");
                if (state == "unresponsive")
                    return tr("perlin.mods.stuck", "Stuck");
                return tr("perlin.mods.error", "Error");
            };
            const auto iconFor = [&](const mods::ModInfo& info) {
                if (info.iconPath.empty())
                    return std::string("mchead:mod:default");
                std::error_code e;
                const std::string stamp = std::to_string(static_cast<long long>(std::filesystem::last_write_time(info.iconPath, e).time_since_epoch().count()));
                std::string& seen = modIconStamp["mod:" + info.name];
                if (seen != stamp) {
                    seen = stamp;
                    Texture t;
                    if (LoadTexture(info.iconPath, t))
                        ui::UiAssets::Instance().SetHead("mod:" + info.name, t);
                }
                return "mchead:mod:" + info.name;
            };
            int selectedIndex = -1;
            for (size_t i = 0; i < shown.size(); i++)
                if (shown[i]->name == modSelected)
                    selectedIndex = static_cast<int>(i);
            if (selectedIndex < 0 && !shown.empty()) {
                selectedIndex = 0;
                modSelected = shown[0]->name;
            }
            std::vector<ui::ModEntry> entries;
            for (const mods::ModInfo* info : shown) {
                ui::ModEntry e;
                e.name = info->displayName.empty() ? info->name : info->displayName;
                e.desc = info->description.empty() ? tr("perlin.mods.noDescription", "A WebAssembly mod.") : info->description;
                e.badge = badgeFor(*info, e.badgeColor);
                e.icon = iconFor(*info);
                entries.push_back(e);
            }
            ui::ModDetail detail;
            if (selectedIndex >= 0) {
                const mods::ModInfo& info = *shown[static_cast<size_t>(selectedIndex)];
                detail.valid = true;
                detail.name = entries[static_cast<size_t>(selectedIndex)].name;
                detail.badge = entries[static_cast<size_t>(selectedIndex)].badge;
                detail.badgeColor = entries[static_cast<size_t>(selectedIndex)].badgeColor;
                detail.icon = entries[static_cast<size_t>(selectedIndex)].icon;
                detail.version = info.version.empty() ? std::string() : "v" + info.version;
                detail.author = info.author.empty() ? std::string() : tr("perlin.mods.by", "By ") + info.author;
                detail.toggleEnabled = info.enabled;
                detail.toggleLabel = info.enabled ? tr("perlin.mods.disable", "Disable")
                                     : info.needsApproval ? tr("perlin.mods.allow", "Allow & Enable")
                                                          : tr("perlin.mods.enable", "Enable");
                detail.reloadLabel = tr("perlin.mods.reload", "Reload");
                detail.hasSettings = info.hasSettings && info.enabled;
                detail.settingsLabel = tr("perlin.mods.settings", "Settings");
                std::string body = info.description.empty() ? tr("perlin.mods.noDescription", "A WebAssembly mod.") : info.description;
                if (info.requested) {
                    std::string asks;
                    const auto add = [&](abi::Permission p, const char* text) {
                        if (info.requested & static_cast<uint32_t>(p))
                            asks += std::string(asks.empty() ? "" : ", ") + text;
                    };
                    add(abi::Permission::Network, "use the internet");
                    add(abi::Permission::Files, "copy, move and download files");
                    add(abi::Permission::Control, "act for you (chat, commands, movement, options)");
                    add(abi::Permission::Packets, "send raw game packets");
                    add(abi::Permission::Native, "read the game window and graphics-device handles");
                    body += "\n\n" + std::string(info.needsApproval ? "This mod asks to: " : "Allowed to: ") + asks + ".";
                    if (info.needsApproval)
                        body += " It stays off until you press Allow & Enable.";
                }
                if (!info.error.empty())
                    body += "\n\n" + tr("perlin.mods.errorLabel", "Error") + ": " + info.error;
                body += "\n\n" + tr("perlin.mods.file", "File") + ": " + info.name + ".wasm (" + std::to_string(info.size) + " bytes" +
                        (info.arch.empty() ? std::string() : info.arch == "wasm64" ? ", 64-bit" : ", 32-bit") + ")";
                if (!info.hooks.empty()) {
                    body += "\n" + tr("perlin.mods.hooks", "Hooks") + ": ";
                    for (size_t i = 0; i < info.hooks.size(); i++)
                        body += (i ? ", " : "") + info.hooks[i];
                }
                if (!info.permissions.empty()) {
                    body += "\n" + tr("perlin.mods.permissions", "Permissions") + ": ";
                    for (size_t i = 0; i < info.permissions.size(); i++)
                        body += (i ? ", " : "") + info.permissions[i];
                }
                {
                    std::string commandsText;
                    for (const mods::PluginCommand& c : mm.Commands())
                        if (c.plugin == info.name)
                            commandsText += (commandsText.empty() ? "/" : ", /") + c.name;
                    if (!commandsText.empty())
                        body += "\n" + tr("perlin.mods.commands", "Commands") + ": " + commandsText;
                    std::string keysText;
                    for (const mods::PluginKey& k : mm.Keys())
                        if (k.plugin == info.name)
                            keysText += (keysText.empty() ? "" : ", ") + k.label + " [" + Binding::Parse(k.binding).Serialize().substr(Binding::Parse(k.binding).Serialize().rfind('.') + 1) + "]";
                    if (!keysText.empty())
                        body += "\n" + tr("perlin.mods.keys", "Keys") + ": " + keysText;
                }
                body += "\n" + tr("perlin.mods.loads", "Loads") + ": " + std::to_string(info.loads);
                if (!info.website.empty())
                    body += "\n\n" + info.website;
                detail.body = body;
            }
            const ui::ModScreenLayout l = ui::ComputeModScreenLayout(static_cast<int>(entries.size()), detail, guiW, guiH);
            if (inside(l.rightX, l.detailTextTop, l.rightW, l.detailTextBottom - l.detailTextTop))
                modDetailScroll -= wheel;
            else if (inside(l.leftX, l.listTop, l.leftW, l.listBottom - l.listTop))
                modScroll -= wheel;
            modScroll = std::clamp(modScroll, 0.0f, l.maxScroll);
            modDetailScroll = std::clamp(modDetailScroll, 0.0f, l.maxDetailScroll);
            const bool caret = (std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() / 500) % 2 == 0;
            std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "m" + std::to_string(static_cast<int>(modScroll)) + "," +
                                    std::to_string(static_cast<int>(modDetailScroll)) + "|" + modSearch + (caret ? "_" : "") + "|" + modSelected + "|" + std::to_string(std::hash<std::string>()(detail.body));
            for (const auto& e : entries)
                signature += "|" + e.name + e.badge + e.desc;
            signature += "#" + detail.body + detail.toggleLabel;
            if (signature != optionsSignature || !hud.Overlay().HasOverlay()) {
                optionsSignature = signature;
                const std::string showing = tr("perlin.mods.showing", "Showing ") + std::to_string(entries.size()) + (entries.size() == 1 ? tr("perlin.mods.mod", " mod") : tr("perlin.mods.mods", " mods"));
                submit(ui::BuildModScreenRml(entries, selectedIndex, detail, tr("perlin.mods.title", "Mods"), modSearch, caret, showing,
                                             tr("perlin.mods.search", "Search..."), tr("perlin.mods.openFolder", "Open Mods Folder"), tr("gui.done", "Done"),
                                             modScroll, modDetailScroll, guiW, guiH),
                       "mods");
            }
            hud.Overlay().MouseMove(gx, gy);
            hud.UpdateOverlay(fbW, fbH, options.guiScale);
            if (click) {
                if (inside(l.doneX, l.btnY, l.btnW, 20.0f)) {
                    leave();
                    pauseLeftDown = down;
                    return;
                }
                if (inside(l.openX, l.btnY, l.btnW, 20.0f)) {
                    openFolder(modDir);
                } else if (detail.valid && inside(l.toggleX, l.detailBtnY - 1.0f, l.toggleW, 20.0f)) {
                    ToggleMod(modSelected);
                    optionsSignature.clear();
                } else if (detail.valid && detail.toggleEnabled && inside(l.reloadX, l.detailBtnY - 1.0f, l.reloadW, 20.0f)) {
                    mm.ReloadMod(modSelected);
                    optionsSignature.clear();
                } else if (detail.valid && detail.hasSettings && inside(l.settingsX, l.detailBtnY - 1.0f, l.settingsW, 20.0f)) {
                    modSettingsFor = modSelected;
                    pauseView = 1;
                    optionsPage = 11;
                    optionsSub = 0;
                    optionsSignature.clear();
                    hud.Overlay().ClearOverlay();
                } else if (gy >= l.listTop && gy < l.listBottom && gx >= l.leftX && gx < l.leftX + l.leftW) {
                    for (int i = 0; i < static_cast<int>(shown.size()); i++)
                        if (inside(l.leftX, l.RowY(i, modScroll), l.leftW - (l.maxScroll > 0 ? 8.0f : 0.0f), l.rowH - 2.0f)) {
                            modSelected = shown[static_cast<size_t>(i)]->name;
                            modDetailScroll = 0;
                            optionsSignature.clear();
                            break;
                        }
                }
            }
            pauseLeftDown = down;
            return;
        }

        if (pauseView == 1 || pauseView == 3 || pauseView == 4) {
            struct OptRow {
                std::string label, value;
                float fraction;
                std::function<void(float, bool)> apply;
                bool header = false;
                bool locked = false;
                bool wide = false;
                int tab = 0;
                std::string icon;
            };
            modRowTips.clear();
            static const int kGuiScales[] = {0, 1, 2, 3, 4};
            const auto percent = [](float v) { return std::to_string(static_cast<int>(std::lround(v * 100.0f))) + "%"; };
            const auto onOff = [&](bool on) { return on ? tr("options.on", "ON") : tr("options.off", "OFF"); };
            const auto snap = [](float f, float steps) { return std::round(std::clamp(f, 0.0f, 1.0f) * steps) / steps; };
            std::vector<OptRow> model;
            std::string title = tr("options.title", "Options");
            const auto nav = [&](const std::string& label, int page) {
                model.push_back({label, "", -1.0f, [this, page](float, bool click) {
                                     if (click) {
                                         optionsPage = page;
                                         optionsSub = 0;
                                         optionsSignature.clear();
                                         bindCapture = -1;
                                     }
                                 }});
            };
            const auto toggle = [&](const std::string& label, bool& ref) {
                model.push_back({label, onOff(ref), -1.0f, [&ref](float, bool click) { if (click) ref = !ref; }});
            };
            const auto slider = [&](const std::string& label, const std::string& value, float fraction, std::function<void(float)> set) {
                model.push_back({label, value, fraction, [set](float f, bool) { set(f); }});
            };
            const auto choice = [&](const std::string& label, std::string& ref, std::vector<std::pair<std::string, std::string>> values) {
                std::string shown = ref;
                for (const auto& v : values)
                    if (v.first == ref)
                        shown = v.second;
                model.push_back({label, shown, -1.0f, [&ref, values](float, bool click) {
                                     if (!click)
                                         return;
                                     size_t at = 0;
                                     for (size_t i = 0; i < values.size(); i++)
                                         if (values[i].first == ref)
                                             at = i;
                                     ref = values[(at + 1) % values.size()].first;
                                 }});
            };
            const auto volume = [&](const char* key, const char* fallback, float& ref) {
                slider(tr(key, fallback), ref <= 0.001f ? tr("options.off", "OFF") : percent(ref), ref, [&ref, snap](float f) { ref = snap(f, 100.0f); });
            };
            if (pauseView == 3) {
                title = tr("perlin.worldOptions", "World Options");
                const BedrockSession::WorldSettings ws = live ? live->World() : BedrockSession::WorldSettings{};
                const int perm = live ? live->OwnPermission() : 2;
                const bool isOp = perm >= 2;
                const bool cheats = isOp && (!live || ws.commandsEnabled);
                const double nowSec = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
                const auto ruleDefault = [](const std::string& k) {
                    if (k == "playerssleepingpercentage")
                        return 100;
                    if (k == "spawnradius")
                        return 10;
                    if (k == "randomtickspeed")
                        return 1;
                    for (const char* off : {"showcoordinates", "showdaysplayed", "doimmediaterespawn", "keepinventory", "locatorbar"})
                        if (k == off)
                            return 0;
                    return 1;
                };
                const auto ruleValue = [this, ws, nowSec, ruleDefault](const std::string& k) {
                    const auto o = worldOverride.find(k);
                    if (o != worldOverride.end() && nowSec - o->second.second < 2.0)
                        return o->second.first;
                    const auto it = ws.rules.find(k);
                    return it == ws.rules.end() ? ruleDefault(k) : it->second;
                };
                const auto run = [live](const std::string& cmd) {
                    if (live)
                        live->SendCommand(cmd);
                };
                const auto setRule = [this, run, nowSec](const std::string& k, int v, bool boolean) {
                    worldOverride[k] = {v, nowSec};
                    run("gamerule " + k + " " + (boolean ? (v != 0 ? "true" : "false") : std::to_string(v)));
                };
                const auto head = [&](const std::string& text) {
                    OptRow r{text, "", -1.0f, [](float, bool) {}};
                    r.header = true;
                    model.push_back(r);
                };
                const auto cyc = [&](const std::string& label, const std::vector<std::string>& names, int at, bool enabled, std::function<void(int)> pick) {
                    const int safe = std::clamp(at, 0, static_cast<int>(names.size()) - 1);
                    model.push_back({label, names[static_cast<size_t>(safe)], -1.0f, [=](float, bool click) {
                                         if (click && enabled)
                                             pick((safe + 1) % static_cast<int>(names.size()));
                                     }});
                    model.back().locked = !enabled;
                };
                const auto rule = [&](const std::string& label, const std::string& key, bool enabled) {
                    const bool on = ruleValue(key) != 0;
                    model.push_back({label, onOff(on), -1.0f, [=](float, bool click) {
                                         if (click && enabled)
                                             setRule(key, on ? 0 : 1, true);
                                     }});
                    model.back().locked = !enabled;
                };
                const auto ruleSlider = [&](const std::string& label, const std::string& key, int maxValue, const char* suffix, bool enabled) {
                    const int v = std::clamp(ruleValue(key), 0, maxValue);
                    model.push_back({label, std::to_string(v) + suffix, static_cast<float>(v) / static_cast<float>(maxValue), [=](float f, bool) {
                                         if (enabled)
                                             setRule(key, static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * static_cast<float>(maxValue))), false);
                                     }});
                };
                head("Game");
                static const char* const kDiff[] = {"peaceful", "easy", "normal", "hard"};
                cyc(tr("options.difficulty", "Difficulty"), {"Peaceful", "Easy", "Normal", "Hard"}, ws.difficulty, isOp,
                    [=](int i) { run(std::string("difficulty ") + kDiff[i]); });
                static const char* const kModes[] = {"survival", "creative", "adventure"};
                cyc("Default Mode", {"Survival", "Creative", "Adventure"}, ws.worldGameMode, isOp,
                    [=](int i) { run(std::string("defaultgamemode ") + kModes[i]); });
                static const char* const kPersonal[] = {"survival", "creative", "adventure", "spectator"};
                cyc("Your Game Mode", {"Survival", "Creative", "Adventure", "Spectator"}, live ? live->GameType() : 0, isOp,
                    [=](int i) { run(std::string("gamemode ") + kPersonal[i]); });
                head("World");
                rule("Friendly Fire", "pvp", isOp);
                rule("Show Coordinates", "showcoordinates", isOp);
                rule("Player Waypoints", "locatorbar", isOp);
                rule("Show Days Played", "showdaysplayed", isOp);
                rule("Fire Spreads", "dofiretick", isOp);
                rule("Recipes Unlocking", "recipesunlock", isOp);
                rule("TNT Explodes", "tntexplodes", isOp);
                rule("Respawn Blocks Explode", "respawnblocksexplode", isOp);
                rule("Mob Loot", "domobloot", isOp);
                rule("Natural Regeneration", "naturalregeneration", isOp);
                rule("Tile Drops", "dotiledrops", isOp);
                rule("Immediate Respawn", "doimmediaterespawn", isOp);
                ruleSlider("Skip Night By Sleeping", "playerssleepingpercentage", 100, "%", isOp);
                ruleSlider("Respawn Radius", "spawnradius", 128, " blocks", isOp);
                head("Cheats");
                {
                    const auto co = worldOverride.find("commandsenabled");
                    const bool on = (co != worldOverride.end() && nowSec - co->second.second < 3.0) ? co->second.first != 0 : (!live || ws.commandsEnabled);
                    model.push_back({"Activate Cheats", onOff(on), -1.0f, [=](float, bool click) {
                                         if (click && isOp) {
                                             worldOverride["commandsenabled"] = {on ? 0 : 1, nowSec};
                                             if (live)
                                                 live->SendSettingsCommand(std::string("changesetting allow-cheats ") + (on ? "false" : "true"));
                                             else
                                                 run(std::string("changesetting allow-cheats ") + (on ? "false" : "true"));
                                         }
                                     }});
                    model.back().locked = !isOp;
                }
                rule("Normal Day/Night Cycle", "dodaylightcycle", cheats);
                {
                    const bool always = ruleValue("dodaylightcycle") == 0;
                    model.push_back({"Always Day", onOff(always), -1.0f, [=](float, bool click) {
                                         if (click && cheats) {
                                             worldOverride["dodaylightcycle"] = {always ? 1 : 0, nowSec};
                                             run(always ? "alwaysday false" : "alwaysday true");
                                         }
                                     }});
                    model.back().locked = !cheats;
                }
                rule("Keep Inventory", "keepinventory", cheats);
                rule("Mob Spawn Normally", "domobspawning", cheats);
                rule("Mob Griefing", "mobgriefing", cheats);
                rule("Entities Drop Loot", "doentitydrops", cheats);
                rule("Normal Weather Cycle", "doweathercycle", cheats);
                rule("Command Blocks Enabled", "commandblocksenabled", cheats);
                ruleSlider("Tick Speed", "randomtickspeed", 100, "", cheats);
                model.push_back({"Reset Tick Speed", "", -1.0f, [=](float, bool click) {
                                     if (click && cheats)
                                         setRule("randomtickspeed", 1, false);
                                 }});
            } else if (pauseView == 4) {
                title = tr("perlin.mods", "Mods");
                auto& mm = mods::ModManager::Instance();
                model.push_back({"Reload All Mods", "", -1.0f, [&mm](float, bool click) {
                                     if (click)
                                         mm.ReloadAll();
                                 }});
                toggle("Hot Reload On Save", options.hotReload);
                const auto infos = mm.List();
                if (infos.empty())
                    model.push_back({"No .wasm mods in", mm.Directory(), -1.0f, [](float, bool) {}});
                for (const mods::ModInfo& info : infos) {
                    const std::string name = info.name;
                    std::string state = info.enabled ? info.state : std::string("disabled");
                    if (!info.error.empty())
                        state += " - " + info.error.substr(0, 24);
                    model.push_back({name, state, -1.0f, [this, name](float, bool click) {
                                         if (!click)
                                             return;
                                         ToggleMod(name);
                                     }});
                }
            } else if (optionsPage == 0) {
                slider(tr("options.fov", "FOV"), std::to_string(static_cast<int>(std::lround(options.fov))), (options.fov - 30.0f) / 80.0f,
                       [this](float f) { options.fov = std::round(30.0f + std::clamp(f, 0.0f, 1.0f) * 80.0f); });
                model.back().wide = true;
                nav(tr("perlin.wardrobe.skinCustomization", "Skin Customization..."), 9);
                nav(tr("options.sounds.title", "Music & Sounds..."), 2);
                nav(tr("options.videoTitle", "Video Settings..."), 1);
                nav(tr("options.controls", "Controls..."), 3);
                nav(tr("options.language", "Language..."), 6);
                nav(tr("perlin.options.hand", "Hand & Camera..."), 4);
                nav(tr("resourcePack.title", "Resource Packs..."), 7);
                nav(tr("perlin.shaderPacks", "Shader Packs..."), 8);
            } else if (optionsPage == 1) {
                title = tr("options.videoTitle", "Video Settings");
                toggle(tr("options.graphics.fancy", "Fancy Graphics"), options.fancyGraphics);
                if (!options.enableLods && options.renderDistance > LodStore::kRealChunks) {
                    options.renderDistance = LodStore::kRealChunks;
                    if (live) {
                        live->SetChunkRadius(options.renderDistance);
                        live->SetNearRadius(1 << 20);
                        live->RequestChunkRadiusNow();
                    }
                }
                const int maxDistance = options.enableLods ? 256 : LodStore::kRealChunks;
                const auto distanceToSlider = [maxDistance](int d) {
                    if (maxDistance <= LodStore::kRealChunks)
                        return static_cast<float>(d - 2) / static_cast<float>(maxDistance - 2);
                    return d <= LodStore::kRealChunks ? 0.5f * static_cast<float>(d - 2) / static_cast<float>(LodStore::kRealChunks - 2)
                                                      : 0.5f + 0.5f * static_cast<float>(d - LodStore::kRealChunks) / static_cast<float>(maxDistance - LodStore::kRealChunks);
                };
                slider(tr("options.renderDistance", "Render Distance"), std::to_string(options.renderDistance) + " " + tr("options.chunks", "chunks"),
                       distanceToSlider(options.renderDistance), [this, live, maxDistance](float f) {
                           f = std::clamp(f, 0.0f, 1.0f);
                           int distance;
                           if (maxDistance <= LodStore::kRealChunks)
                               distance = 2 + static_cast<int>(std::lround(f * static_cast<float>(maxDistance - 2)));
                           else if (f <= 0.5f)
                               distance = 2 + static_cast<int>(std::lround(f * 2.0f * static_cast<float>(LodStore::kRealChunks - 2)));
                           else
                               distance = LodStore::kRealChunks + static_cast<int>(std::lround((f - 0.5f) * 2.0f * static_cast<float>(maxDistance - LodStore::kRealChunks)));
                           if (distance != options.renderDistance) {
                               options.renderDistance = distance;
                               if (live) {
                                   live->SetChunkRadius(options.renderDistance);
                                   live->SetMaxViewDistance(std::max(32, options.renderDistance));
                                   live->SetNearRadius(options.enableLods && options.renderDistance > LodStore::kRealChunks ? LodStore::kRealChunks : 1 << 20);
                                   live->RequestChunkRadiusNow();
                               }
                           }
                       });
                toggle("Enable LODs", options.enableLods);
                if (live) {
                    const int wantNear = options.enableLods && options.renderDistance > LodStore::kRealChunks ? LodStore::kRealChunks : 1 << 20;
                    if (live->NearRadius() != wantNear) {
                        live->SetNearRadius(wantNear);
                        live->SetMaxViewDistance(std::max(32, options.renderDistance));
                        live->RequestChunkRadiusNow();
                    }
                }
                slider(tr("options.framerateLimit", "Max Framerate"),
                       options.maxFps == 0 ? tr("options.framerateLimit.max", "Unlimited") : std::to_string(options.maxFps) + " fps",
                       options.maxFps == 0 ? 1.0f : static_cast<float>(options.maxFps - 10) / 250.0f, [this](float f) {
                           const int steps = static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 25.0f));
                           options.maxFps = steps >= 25 ? 0 : 10 + steps * 10;
                       });
                toggle(tr("options.vsync", "VSync"), options.vsync);
                toggle(tr("options.fullscreen", "Fullscreen"), options.fullscreen);
                model.push_back({std::string("GUI Scale"), options.guiScale == 0 ? tr("options.guiScale.auto", "Auto") : std::to_string(options.guiScale), -1.0f,
                                 [this](float, bool click) {
                                     if (!click)
                                         return;
                                     size_t at = 0;
                                     for (size_t i = 0; i < std::size(kGuiScales); i++)
                                         if (kGuiScales[i] == options.guiScale)
                                             at = i;
                                     options.guiScale = kGuiScales[(at + 1) % std::size(kGuiScales)];
                                 }});
                slider(tr("options.gamma", "Brightness"),
                       options.gamma <= 0.01f ? tr("options.gamma.min", "Moody") : options.gamma >= 0.99f ? tr("options.gamma.max", "Bright") : percent(options.gamma),
                       options.gamma, [this, snap](float f) { options.gamma = snap(f, 100.0f); });
                slider(tr("options.fov", "FOV"), std::to_string(static_cast<int>(std::lround(options.fov))), (options.fov - 30.0f) / 80.0f,
                       [this](float f) { options.fov = std::round(30.0f + std::clamp(f, 0.0f, 1.0f) * 80.0f); });
                slider(tr("options.motionBlur", "Motion Blur"), options.motionBlur <= 0.001f ? tr("options.off", "OFF") : percent(options.motionBlur), options.motionBlur,
                       [this, snap](float f) { options.motionBlur = snap(f, 100.0f); });
                slider(tr("options.accessibility.menu_background_blurriness", "Menu Blur"), std::to_string(options.menuBlur), static_cast<float>(options.menuBlur) / 10.0f,
                       [this](float f) { options.menuBlur = static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 10.0f)); });
                toggle(tr("options.entityShadows", "Entity Shadows"), options.shadows);
                toggle(tr("options.fovEffectScale", "FOV Effects"), options.fovEffects);
                slider(tr("options.damageTilt", "Damage Tilt"), percent(static_cast<float>(options.damageTilt) / 100.0f), static_cast<float>(options.damageTilt) / 100.0f,
                       [this](float f) { options.damageTilt = static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 100.0f)); });
                toggle(tr("perlin.options.damageTint", "Damage Tint"), options.damageTint);
                slider("Cloud Range", std::to_string(options.cloudRange) + " " + tr("options.chunks", "chunks"), static_cast<float>(options.cloudRange - 2) / 126.0f,
                       [this](float f) { options.cloudRange = 2 + static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 126.0f)); });
                toggle("Better Grass", options.betterGrass);
                {
                    std::vector<std::pair<std::string, std::string>> apis = {{"auto", "Auto"}};
                    for (rhi::Api api : rhi::CompiledApis())
                        apis.push_back({rhi::ApiId(api), rhi::ApiName(api)});
                    size_t at = 0;
                    for (size_t i = 0; i < apis.size(); i++)
                        if (apis[i].first == options.renderer)
                            at = i;
                    const size_t last = apis.size() - 1;
                    slider("Renderer", apis[at].second, last ? static_cast<float>(at) / static_cast<float>(last) : 0.0f,
                           [this, apis, last](float f) {
                               options.renderer = apis[static_cast<size_t>(std::lround(std::clamp(f, 0.0f, 1.0f) * static_cast<float>(last)))].first;
                           });
                }
                choice("Glass", options.glass, {{"normal", "Normal"}, {"connected", "Connected"}, {"connectedClear", "Connected Clear"}, {"clear", "Clear"}});
                {
                    static const char* const kQuality[5] = {"Off", "Low", "Medium", "High", "Ultra"};
                    slider("Shadow Quality", kQuality[std::clamp(options.shadowQuality, 0, 4)], static_cast<float>(options.shadowQuality) / 4.0f,
                           [this](float f) { options.shadowQuality = static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 4.0f)); });
                }
                slider("Shadow Distance", options.shadowDistance == 0 ? std::string("Auto") : std::to_string(options.shadowDistance),
                       static_cast<float>(options.shadowDistance) / 256.0f,
                       [this](float f) { options.shadowDistance = static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 64.0f)) * 4; });
                toggle("Mob & Player Shadows", options.shadowEntities);
                slider("Bloom", options.bloom <= 0.001f ? tr("options.off", "OFF") : percent(options.bloom), options.bloom,
                       [this, snap](float f) { options.bloom = snap(f, 20.0f); });
                slider("Held Item Light", std::to_string(options.heldLight), static_cast<float>(options.heldLight) / 15.0f,
                       [this](float f) { options.heldLight = static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 15.0f)); });
                choice("Dynamic Lights", options.dynamicLights, {{"off", "Off"}, {"fast", "Fast"}, {"fancy", "Fancy"}});
                toggle("Third Person Crosshair", options.thirdPersonCrosshair);
                toggle("Third Person Nametag", options.thirdPersonNametag);
                {
                    OptRow h{"Zoom", "", -1.0f, [](float, bool) {}};
                    h.header = true;
                    model.push_back(h);
                }
                auto& z = options.zoom;
                slider("Zoom Level", percent(z.level), (z.level - z.minLevel) / std::max(0.001f, z.maxLevel - z.minLevel),
                       [this](float f) { auto& zz = options.zoom; zz.level = zz.minLevel + std::round(std::clamp(f, 0.0f, 1.0f) * 100.0f) / 100.0f * (zz.maxLevel - zz.minLevel); });
                slider("Zoom In Time", std::to_string(static_cast<int>(std::lround(z.inSeconds * 1000.0f))) + " ms", z.inSeconds / 2.0f,
                       [this](float f) { options.zoom.inSeconds = std::round(std::clamp(f, 0.0f, 1.0f) * 100.0f) / 50.0f; });
                slider("Zoom Out Time", std::to_string(static_cast<int>(std::lround(z.outSeconds * 1000.0f))) + " ms", z.outSeconds / 2.0f,
                       [this](float f) { options.zoom.outSeconds = std::round(std::clamp(f, 0.0f, 1.0f) * 100.0f) / 50.0f; });
                choice("Zoom Easing", z.easing, {{"linear", "Linear"}, {"cubic", "Cubic"}, {"quint", "Quint"}, {"sine", "Sine"}, {"smooth", "Smooth"}});
                slider("Zoom Scroll Step", percent(z.scrollStep), z.scrollStep / 0.9f,
                       [this](float f) { options.zoom.scrollStep = std::round(std::clamp(f, 0.0f, 1.0f) * 90.0f) / 100.0f; });
                slider("Zoom Smoothing", std::to_string(static_cast<int>(std::lround(z.smoothing))), z.smoothing / 30.0f,
                       [this](float f) { options.zoom.smoothing = std::round(std::clamp(f, 0.0f, 1.0f) * 30.0f); });
                toggle("Zoom Reset On Press", options.zoom.resetOnPress);
                toggle("Cinematic Camera", options.zoom.cinematic);
                toggle("Scale Sensitivity", options.zoom.scaleSensitivity);
                {
                    OptRow h{"Advanced", "", -1.0f, [](float, bool) {}};
                    h.header = true;
                    model.push_back(h);
                }
                {
                    static bool lastSupersampling = false;
                    static std::string lastUpscaler = "off";
                    if (options.rgss && options.upscaler != "off") {
                        if (options.rgss && !lastSupersampling)
                            options.upscaler = "off";
                        else
                            options.rgss = false;
                    }
                    lastSupersampling = options.rgss;
                    lastUpscaler = options.upscaler;
                }
                if (options.upscaler == "off")
                    toggle(tr("options.rgss", "Supersampling"), options.rgss);
                choice("Upscaler", options.upscaler, {{"off", "Off"}, {"bilinear", "Bilinear"}, {"bicubic", "Bicubic"}, {"fsr1", "FSR 1.0"}, {"temporal", "Temporal"}});
                if (options.upscaler != "off") {
                    slider("Upscale Quality", std::to_string(options.upscaleCustom) + "%", static_cast<float>(options.upscaleCustom - 25) / 75.0f,
                           [this](float f) { options.upscaleCustom = 25 + static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 75.0f)); });
                    slider("Target FPS", options.upscaleTargetFps <= 0 ? tr("options.off", "OFF") : std::to_string(options.upscaleTargetFps),
                           static_cast<float>(options.upscaleTargetFps) / 360.0f,
                           [this](float f) {
                               const int v = static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 72.0f)) * 5;
                               options.upscaleTargetFps = v < 20 ? 0 : v;
                           });
                    slider("Upscale Sharpness", options.upscaleSharpness <= 0 ? tr("options.off", "OFF") : std::to_string(options.upscaleSharpness) + "%",
                           static_cast<float>(options.upscaleSharpness) / 100.0f,
                           [this](float f) { options.upscaleSharpness = static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 100.0f)); });
                }
                {
                    const bool autoGen = options.frameGen == "auto";
                    const int gen = autoGen ? 5 : options.frameGen == "4x" ? 4 : options.frameGen == "3x" ? 3 : options.frameGen == "2x" ? 2 : 1;
                    const std::string label = autoGen ? "Auto (" + (fgActive > 1 ? std::to_string(fgActive) + "x" : tr("options.off", "OFF")) + ")"
                                                      : gen == 1 ? tr("options.off", "OFF") : std::to_string(gen) + "x";
                    slider("Frame Generation", label, static_cast<float>(gen - 1) / 4.0f,
                           [this](float f) {
                               const int g = 1 + static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 4.0f));
                               options.frameGen = g == 5 ? "auto" : g == 4 ? "4x" : g == 3 ? "3x" : g == 2 ? "2x" : "off";
                           });
                }
                {
                    OptRow h{"Developer", "", -1.0f, [](float, bool) {}};
                    h.header = true;
                    model.push_back(h);
                }
                toggle("Hot Reload On Save", options.hotReload);
            } else if (optionsPage == 7) {
                title = tr("resourcePack.title", "Resource Packs");
                const std::string dir = paths::DataDir() + "/texturepacks";
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                {
                    OptRow h{"Active (top wins)", "", -1.0f, [](float, bool) {}};
                    h.header = true;
                    model.push_back(h);
                }
                if (options.texturePacks.empty())
                    model.push_back({"Default textures", "", -1.0f, [](float, bool) {}});
                for (size_t i = 0; i < options.texturePacks.size(); i++) {
                    const std::string name = options.texturePacks[i];
                    model.push_back({"Raise: " + name, "", -1.0f, [this, i](float, bool click) {
                                         if (click && i > 0) {
                                             std::swap(options.texturePacks[i], options.texturePacks[i - 1]);
                                             options.Save();
                                             ApplyTexturePacks();
                                         }
                                     }});
                    model.push_back({"Remove: " + name, "", -1.0f, [this, i](float, bool click) {
                                         if (click && i < options.texturePacks.size()) {
                                             options.texturePacks.erase(options.texturePacks.begin() + static_cast<long>(i));
                                             options.Save();
                                             ApplyTexturePacks();
                                         }
                                     }});
                }
                {
                    OptRow h{"Available", "", -1.0f, [](float, bool) {}};
                    h.header = true;
                    model.push_back(h);
                }
                std::vector<std::string> found;
                for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                    const std::string ext = entry.path().extension().string();
                    if (entry.is_directory(ec) || ext == ".zip" || ext == ".mcpack")
                        found.push_back(entry.path().filename().string());
                }
                std::sort(found.begin(), found.end());
                for (const std::string& name : found) {
                    if (std::find(options.texturePacks.begin(), options.texturePacks.end(), name) != options.texturePacks.end())
                        continue;
                    model.push_back({name, "Add", -1.0f, [this, name](float, bool click) {
                                         if (click) {
                                             options.texturePacks.insert(options.texturePacks.begin(), name);
                                             options.Save();
                                             ApplyTexturePacks();
                                         }
                                     }});
                }
                model.push_back({"Folder", dir, -1.0f, [](float, bool) {}});
            } else if (optionsPage == 8) {
                title = tr("perlin.shaderPacks", "Shader Packs");
                const std::string dir = paths::DataDir() + "/shaderpacks";
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                model.push_back({"Built-in Shaders", options.shaderPack.empty() ? "Active" : "Use", -1.0f, [this](float, bool click) {
                                     if (click) {
                                         options.shaderPack.clear();
                                         options.Save();
                                         ApplyShaderPack();
                                     }
                                 }});
                std::vector<std::string> found;
                for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                    const std::string ext = entry.path().extension().string();
                    if (entry.is_directory(ec) || ext == ".zip")
                        found.push_back(entry.path().filename().string());
                }
                std::sort(found.begin(), found.end());
                for (const std::string& name : found)
                    model.push_back({name, options.shaderPack == name ? "Active" : "Use", -1.0f, [this, name](float, bool click) {
                                         if (click) {
                                             options.shaderPack = name;
                                             options.Save();
                                             ApplyShaderPack();
                                         }
                                     }});
                model.push_back({"Folder", dir, -1.0f, [](float, bool) {}});
            } else if (optionsPage == 2) {
                title = tr("options.sounds.title", "Music & Sounds");
                volume("soundCategory.master", "Master Volume", options.sound.master);
                volume("soundCategory.music", "Music", options.sound.music);
                volume("soundCategory.record", "Jukebox/Note Blocks", options.sound.record);
                volume("soundCategory.weather", "Weather", options.sound.weather);
                volume("soundCategory.block", "Blocks", options.sound.block);
                volume("soundCategory.hostile", "Hostile Creatures", options.sound.hostile);
                volume("soundCategory.neutral", "Friendly Creatures", options.sound.neutral);
                volume("soundCategory.player", "Players", options.sound.player);
                volume("soundCategory.ambient", "Ambient/Environment", options.sound.ambient);
                volume("soundCategory.voice", "Voice/Speech", options.sound.voice);
                toggle(tr("options.showSubtitles", "Show Subtitles"), options.sound.subtitles);
            } else if (optionsPage == 3) {
                title = tr("options.controls", "Controls");
                static const char* kTabs[3] = {"Keyboard", "Controller", "Touch"};
                for (int t = 0; t < 3; t++) {
                    OptRow tab{kTabs[t], "", -1.0f, [this, t](float, bool click) {
                                   if (click && controlsTab != t) {
                                       controlsTab = t;
                                       optionsSignature.clear();
                                   }
                               }};
                    tab.tab = t + 1;
                    tab.locked = controlsTab == t;
                    model.push_back(tab);
                }
                const auto bindsRow = [&](const std::string& label, int device) {
                    model.push_back({label, "", -1.0f, [this, device](float, bool click) {
                                         if (click) {
                                             bindDevice = device;
                                             optionsPage = 5;
                                             optionsSub = 0;
                                             optionsSignature.clear();
                                             bindCapture = -1;
                                         }
                                     }});
                };
                if (controlsTab == 0) {
                    slider(tr("options.sensitivity", "Sensitivity"), percent(options.mouseSensitivity / 0.15f), options.mouseSensitivity / 0.30f,
                           [this](float f) { options.mouseSensitivity = std::round(std::clamp(f, 0.0f, 1.0f) * 200.0f) / 100.0f * 0.15f; });
                    toggle(tr("options.invertMouse", "Invert Mouse"), options.invertMouse);
                    toggle(tr("perlin.options.centerCursor", "Center Cursor"), options.centerCursor);
                    choice(tr("key.sprint", "Sprint"), options.sprintMode, {{"hold", tr("options.key.hold", "Hold")}, {"toggle", tr("options.key.toggle", "Toggle")}});
                    choice(tr("key.sneak", "Sneak"), options.sneakMode, {{"hold", tr("options.key.hold", "Hold")}, {"toggle", tr("options.key.toggle", "Toggle")}});
                    bindsRow(tr("controls.keybinds", "Key Binds..."), 0);
                } else if (controlsTab == 1) {
                    model.push_back({"Gamepad", input.PadConnected() ? "Connected" : "Not connected", -1.0f, [](float, bool) {}});
                    model.back().locked = true;
                    slider("Look Sensitivity", percent(options.padSensitivity), options.padSensitivity / 2.0f,
                           [this](float f) { options.padSensitivity = std::round(std::clamp(f, 0.05f, 1.0f) * 200.0f) / 100.0f; });
                    slider("Stick Dead Zone", percent(options.padDeadzone), options.padDeadzone / 0.6f,
                           [this](float f) { options.padDeadzone = std::round(std::clamp(f, 0.0f, 1.0f) * 60.0f) / 100.0f; });
                    toggle("Invert Look Y", options.padInvertY);
                    toggle("Vibration", options.padVibration);
                    bindsRow("Controller Binds...", 1);
                } else {
                    slider("Look Sensitivity", percent(options.touchSensitivity), options.touchSensitivity / 2.0f,
                           [this](float f) { options.touchSensitivity = std::round(std::clamp(f, 0.05f, 1.0f) * 200.0f) / 100.0f; });
                    slider("Button Size", percent(options.touchScale), (options.touchScale - 0.5f) / 1.3f,
                           [this](float f) { options.touchScale = std::round((0.5f + std::clamp(f, 0.0f, 1.0f) * 1.3f) * 20.0f) / 20.0f; });
                    slider("Button Opacity", percent(options.touchOpacity), (options.touchOpacity - 0.2f) / 0.8f,
                           [this](float f) { options.touchOpacity = std::round((0.2f + std::clamp(f, 0.0f, 1.0f) * 0.8f) * 20.0f) / 20.0f; });
                    OptRow edit{"Edit Touch Layout...", "", -1.0f, [this](float, bool click) {
                                    if (click) {
                                        touchEditing = true;
                                        touchEditHidden = false;
                                        options.Save();
                                        optionsSignature.clear();
                                    }
                                }};
                    edit.wide = true;
                    model.push_back(edit);
                    OptRow reset{"Reset Touch Layout", "", -1.0f, [this](float, bool click) {
                                     if (click) {
                                         options.touchLayout.clear();
                                         options.Save();
                                     }
                                 }};
                    reset.wide = true;
                    model.push_back(reset);
                }
            } else if (optionsPage == 11) {
                auto& mm = mods::ModManager::Instance();
                std::string modTitle = modSettingsFor;
                for (const mods::ModInfo& mi : mm.List())
                    if (mi.name == modSettingsFor && !mi.displayName.empty())
                        modTitle = mi.displayName;
                title = modTitle + " - " + tr("perlin.mods.settings", "Settings");
                const std::string owner = modSettingsFor;
                const auto iconOf = [&](const abi::SettingDef& def) -> std::string {
                    if (def.icon.rfind("item:", 0) == 0)
                        return def.icon;
                    if (def.icon.rfind("texture:", 0) == 0)
                        return def.icon.substr(8);
                    if (def.icon.rfind("file:", 0) == 0) {
                        const std::string rel = def.icon.substr(5);
                        if (rel.empty() || rel.find("..") != std::string::npos || rel[0] == '/' || rel[0] == '\\')
                            return {};
                        const std::string full = (std::filesystem::path(mm.Directory()) / (owner + ".data") / rel).string();
                        const std::string key = "modset:" + owner + ":" + rel;
                        std::error_code ec;
                        const std::string stamp = std::to_string(static_cast<long long>(std::filesystem::last_write_time(full, ec).time_since_epoch().count()));
                        std::string& seen = modIconStamp[key];
                        if (seen != stamp) {
                            seen = stamp;
                            Texture t;
                            if (LoadTexture(full, t))
                                ui::UiAssets::Instance().SetHead(key, t);
                        }
                        return ui::UiAssets::Instance().HasHead(key) ? "mchead:" + key : std::string();
                    }
                    return {};
                };
                const std::vector<abi::SettingDef> defs = mm.Settings(owner);
                for (const abi::SettingDef& def : defs) {
                    const size_t before = model.size();
                    const std::string label = def.label.empty() ? def.id : def.label;
                    const auto withUnit = [&](std::string v) { return def.unit.empty() ? v : v + " " + def.unit; };
                    switch (def.kind) {
                    case abi::SettingKind::Header: {
                        OptRow h{label, "", -1.0f, [](float, bool) {}};
                        h.header = true;
                        model.push_back(h);
                        break;
                    }
                    case abi::SettingKind::Label: {
                        model.push_back({label, def.value, -1.0f, [](float, bool) {}});
                        model.back().locked = true;
                        break;
                    }
                    case abi::SettingKind::Toggle: {
                        const bool on = def.value == "true" || def.value == "1";
                        model.push_back({label, onOff(on), -1.0f, [owner, id = def.id, on](float, bool click) {
                                             if (click)
                                                 mods::ModManager::Instance().ChangeSetting(owner, id, on ? "false" : "true");
                                         }});
                        break;
                    }
                    case abi::SettingKind::Integer:
                    case abi::SettingKind::Number: {
                        const double lo = def.min, hi = std::max(def.max, def.min + 1.0e-9);
                        const double cur = std::clamp(std::atof(def.value.c_str()), lo, hi);
                        const bool whole = def.kind == abi::SettingKind::Integer;
                        char buf[48];
                        if (whole)
                            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(std::llround(cur)));
                        else
                            std::snprintf(buf, sizeof(buf), def.step > 0 && def.step < 0.095 ? "%.2f" : "%.1f", cur);
                        model.push_back({label, withUnit(buf), static_cast<float>((cur - lo) / (hi - lo)),
                                         [owner, id = def.id, lo, hi, whole, step = def.step](float f, bool) {
                                             double v = lo + std::clamp(static_cast<double>(f), 0.0, 1.0) * (hi - lo);
                                             if (step > 0)
                                                 v = lo + std::round((v - lo) / step) * step;
                                             if (whole)
                                                 v = std::round(v);
                                             v = std::clamp(v, lo, hi);
                                             char out[48];
                                             std::snprintf(out, sizeof(out), whole ? "%.0f" : "%.6g", v);
                                             mods::ModManager::Instance().ChangeSetting(owner, id, out);
                                         }});
                        break;
                    }
                    case abi::SettingKind::Choice: {
                        size_t at = 0;
                        for (size_t i = 0; i < def.choices.size(); i++)
                            if (def.choices[i] == def.value)
                                at = i;
                        const std::string shown = at < def.choiceLabels.size() && !def.choiceLabels[at].empty() ? def.choiceLabels[at] : def.value;
                        model.push_back({label, shown, -1.0f, [owner, id = def.id, next = def.choices.empty() ? std::string() : def.choices[(at + 1) % def.choices.size()]](float, bool click) {
                                             if (click)
                                                 mods::ModManager::Instance().ChangeSetting(owner, id, next);
                                         }});
                        break;
                    }
                    case abi::SettingKind::Button:
                        model.push_back({label, "", -1.0f, [owner, id = def.id](float, bool click) {
                                             if (click)
                                                 mods::ModManager::Instance().ChangeSetting(owner, id, "pressed");
                                         }});
                        break;
                    }
                    if (model.size() > before && !model.back().header) {
                        model.back().icon = iconOf(def);
                        modRowTips.resize(model.size());
                        modRowTips[model.size() - 1] = def.description;
                    }
                }
                if (defs.empty()) {
                    OptRow h{tr("perlin.mods.noSettings", "This mod has no settings"), "", -1.0f, [](float, bool) {}};
                    h.header = true;
                    model.push_back(h);
                } else {
                    model.push_back({tr("perlin.mods.resetSettings", "Reset to Defaults"), "", -1.0f, [owner](float, bool click) {
                                         if (click)
                                             mods::ModManager::Instance().ResetSettings(owner);
                                     }});
                }
            } else if (optionsPage == 10 && renderer && renderer->JavaShaderPackActive()) {
                title = tr("perlin.shaders.settings.title", "Shader Pack Settings");
                static std::map<std::string, std::string> javaEdits;
                static std::string javaEditsPack;
                static bool javaDirty = false;
                if (javaEditsPack != options.shaderPack) {
                    javaEditsPack = options.shaderPack;
                    javaEdits.clear();
                    javaDirty = false;
                }
                const std::string packPath = paths::DataDir() + "/shaderpacks/" + options.shaderPack;
                model.push_back({javaDirty ? "Apply Changes (reloads shaders)" : "Reload Shaders", "", -1.0f, [this, packPath](float, bool click) {
                                     if (!click)
                                         return;
                                     javaDirty = false;
                                     javaPackPending = packPath;
                                 }});
                model.push_back({"Reset All to Defaults", "", -1.0f, [this, packPath](float, bool click) {
                                     if (!click)
                                         return;
                                     javaEdits.clear();
                                     jshader::WriteJavaPackOptions(packPath, {});
                                     javaDirty = true;
                                     javaPackPending = packPath;
                                 }});
                std::string lastGroup = "\x01";
                for (const JavaPackOption& o : renderer->JavaShaderPackOptions()) {
                    if (o.group != lastGroup) {
                        lastGroup = o.group;
                        OptRow h{o.group.empty() ? std::string("Shader Options") : o.group, "", -1.0f, [](float, bool) {}};
                        h.header = true;
                        model.push_back(h);
                    }
                    const auto edited = javaEdits.find(o.name);
                    const std::string current = edited != javaEdits.end() ? edited->second : o.current;
                    std::string shown = current;
                    if (o.toggle)
                        shown = current == "true" ? tr("options.on", "ON") : tr("options.off", "OFF");
                    model.push_back({o.label, shown, -1.0f, [this, o, current, packPath](float, bool click) {
                                         if (!click || o.values.empty())
                                             return;
                                         size_t at = 0;
                                         for (size_t i = 0; i < o.values.size(); i++)
                                             if (o.values[i] == current)
                                                 at = i;
                                         javaEdits[o.name] = o.values[(at + 1) % o.values.size()];
                                         auto all = jshader::ReadJavaPackOptions(packPath);
                                         for (const auto& kv : javaEdits)
                                             all[kv.first] = kv.second;
                                         jshader::WriteJavaPackOptions(packPath, all);
                                         javaDirty = true;
                                     }});
                    if (!o.comment.empty()) {
                        modRowTips.resize(model.size());
                        modRowTips[model.size() - 1] = o.comment;
                    }
                }
            } else if (optionsPage == 10) {
                title = tr("perlin.shaders.settings.title", "Shader Pack Settings");
                const std::vector<ShaderSetting>& list = CachedPackSettings(options.shaderPack);
                const std::vector<ShaderPreset>& presets = CachedPackPresets(options.shaderPack);
                const auto settingValue = [&](const ShaderSetting& st) {
                    const auto found = options.shaderSettings.find(st.id);
                    return found == options.shaderSettings.end() ? st.def : std::clamp(found->second, st.min, st.max);
                };
                const auto engineValue = [&](const std::string& key) -> double {
                    if (key == "shadowQuality")
                        return options.shadowQuality;
                    if (key == "shadowDistance")
                        return options.shadowDistance;
                    if (key == "shadowEntities")
                        return options.shadowEntities ? 1.0 : 0.0;
                    if (key == "bloom")
                        return options.bloom;
                    return 0.0;
                };
                if (!presets.empty()) {
                    int current = -1;
                    for (size_t p = 0; p < presets.size() && current < 0; p++) {
                        bool match = true;
                        for (const auto& [key, v] : presets[p].engine)
                            match = match && std::fabs(engineValue(key) - v) < 0.01;
                        for (const auto& [key, v] : presets[p].settings)
                            for (const ShaderSetting& st : list)
                                if (st.id == key)
                                    match = match && std::fabs(settingValue(st) - std::clamp(v, st.min, st.max)) < 0.01;
                        if (match)
                            current = static_cast<int>(p);
                    }
                    model.push_back({tr("perlin.shaders.preset", "Preset"), current < 0 ? tr("perlin.shaders.preset.custom", "Custom") : presets[current].name, -1.0f,
                                     [this, &presets, current](float, bool click) {
                                         if (!click || presets.empty())
                                             return;
                                         const ShaderPreset& p = presets[(current + 1) % static_cast<int>(presets.size())];
                                         for (const auto& [key, v] : p.settings)
                                             options.shaderSettings[key] = v;
                                         for (const auto& [key, v] : p.engine) {
                                             if (key == "shadowQuality")
                                                 options.shadowQuality = std::clamp(static_cast<int>(std::lround(v)), 0, 4);
                                             else if (key == "shadowDistance")
                                                 options.shadowDistance = std::clamp(static_cast<int>(std::lround(v)), 0, 256);
                                             else if (key == "shadowEntities")
                                                 options.shadowEntities = v >= 0.5;
                                             else if (key == "bloom")
                                                 options.bloom = std::clamp(static_cast<float>(v), 0.0f, 1.0f);
                                         }
                                     }});
                }
                if (!list.empty()) {
                    OptRow h{tr("perlin.shaders.settings.engine", "Rendering"), "", -1.0f, [](float, bool) {}};
                    h.header = true;
                    model.push_back(h);
                    static const char* const kQuality[5] = {"Off", "Low", "Medium", "High", "Ultra"};
                    slider("Shadow Quality", kQuality[std::clamp(options.shadowQuality, 0, 4)], static_cast<float>(options.shadowQuality) / 4.0f,
                           [this](float f) { options.shadowQuality = static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 4.0f)); });
                    slider("Shadow Distance", options.shadowDistance == 0 ? std::string("Auto") : std::to_string(options.shadowDistance),
                           static_cast<float>(options.shadowDistance) / 256.0f,
                           [this](float f) { options.shadowDistance = static_cast<int>(std::lround(std::clamp(f, 0.0f, 1.0f) * 64.0f)) * 4; });
                    toggle("Mob & Player Shadows", options.shadowEntities);
                    slider("Bloom", options.bloom <= 0.001f ? tr("options.off", "OFF") : percent(options.bloom), options.bloom,
                           [this, snap](float f) { options.bloom = snap(f, 20.0f); });
                }
                std::string lastGroup;
                for (const ShaderSetting& st : list) {
                    if (!st.group.empty() && st.group != lastGroup) {
                        lastGroup = st.group;
                        OptRow h{st.group, "", -1.0f, [](float, bool) {}};
                        h.header = true;
                        model.push_back(h);
                    }
                    const double value = settingValue(st);
                    if (st.toggle) {
                        model.push_back({st.label, value >= 0.5 ? tr("options.on", "ON") : tr("options.off", "OFF"), -1.0f,
                                         [this, id = st.id, value](float, bool click) {
                                             if (click)
                                                 options.shaderSettings[id] = value >= 0.5 ? 0.0 : 1.0;
                                         }});
                        continue;
                    }
                    char buf[48];
                    const long stepIndex = std::lround((value - st.min) / st.step);
                    if (!st.labels.empty() && stepIndex >= 0 && stepIndex < static_cast<long>(st.labels.size()))
                        std::snprintf(buf, sizeof(buf), "%s", st.labels[static_cast<size_t>(stepIndex)].c_str());
                    else
                        std::snprintf(buf, sizeof(buf), st.step < 0.095 ? "%.2f" : st.step < 0.95 ? "%.1f" : "%.0f", value);
                    slider(st.label, buf, static_cast<float>((value - st.min) / std::max(1.0e-9, st.max - st.min)),
                           [this, st](float f) {
                               const double raw = st.min + std::clamp(static_cast<double>(f), 0.0, 1.0) * (st.max - st.min);
                               options.shaderSettings[st.id] = std::clamp(st.min + std::round((raw - st.min) / st.step) * st.step, st.min, st.max);
                           });
                }
                if (list.empty()) {
                    OptRow h{tr("perlin.shaders.settings.none", "This shader pack has no settings"), "", -1.0f, [](float, bool) {}};
                    h.header = true;
                    model.push_back(h);
                }
            } else if (optionsPage == 9) {
                title = tr("options.skinCustomisation.title", "Skin Customization");
                toggle(tr("options.modelPart.cape", "Cape"), options.skinParts.cape);
                choice("Cape Physics", options.capePhysics, {{"noshoulder", "No Shoulder"}, {"shoulder", "Shoulder"}, {"off", "Off"}});
                toggle(tr("options.modelPart.jacket", "Jacket"), options.skinParts.jacket);
                toggle(tr("options.modelPart.left_sleeve", "Left Sleeve"), options.skinParts.leftSleeve);
                toggle(tr("options.modelPart.right_sleeve", "Right Sleeve"), options.skinParts.rightSleeve);
                toggle(tr("options.modelPart.left_pants_leg", "Left Pants Leg"), options.skinParts.leftPants);
                toggle(tr("options.modelPart.right_pants_leg", "Right Pants Leg"), options.skinParts.rightPants);
                toggle(tr("options.modelPart.hat", "Hat"), options.skinParts.hat);
                toggle("3D Skins", options.skins3d);
                toggle("Bedrock Bridging", options.bedrockBridging);
                model.push_back({tr("perlin.wardrobe", "Wardrobe..."), "", -1.0f, [this, &hud](float, bool click) {
                                     if (click) {
                                         options.Save();
                                         pauseView = 5;
                                         optionsSignature.clear();
                                         hud.Overlay().ClearOverlay();
                                     }
                                 }});
            } else if (optionsPage == 4) {
                title = tr("perlin.options.hand", "Hand & Camera");
                toggle(tr("options.showHand", "Show Hand"), options.hand.show);
                toggle(tr("options.viewBobbing", "View Bobbing"), options.hand.viewBobbing);
                toggle(tr("perlin.options.bobCamera", "Camera Bobbing"), options.hand.bobCamera);
                toggle(tr("perlin.options.bobHand", "Hand Bobbing"), options.hand.bobHand);
                toggle(tr("perlin.options.swing", "Swing Animation"), options.hand.swing);
                toggle(tr("perlin.options.equip", "Equip Animation"), options.hand.equipAnimation);
                toggle(tr("perlin.options.sway", "Hand Sway"), options.hand.sway);
                toggle(tr("options.mainHand.left", "Left Handed"), options.hand.leftHanded);
                toggle(tr("perlin.options.offhand", "Show Offhand"), options.hand.showOffhand);
                toggle(tr("perlin.options.thirdCrosshair", "Third Person Crosshair"), options.thirdPersonCrosshair);
                toggle(tr("perlin.options.thirdNametag", "Third Person Nametag"), options.thirdPersonNametag);
                slider(tr("perlin.options.handScale", "Hand Size"), percent(options.hand.scale), (options.hand.scale - 0.5f) / 1.5f,
                       [this](float f) { options.hand.scale = 0.5f + std::round(std::clamp(f, 0.0f, 1.0f) * 150.0f) / 100.0f; });
                const auto fieldSlider = [&](const std::string& label, float& ref, float lo, float hi, float step, const char* unit) {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), step < 0.5f ? "%.2f%s" : "%.0f%s", ref, unit);
                    slider(label, buf, (ref - lo) / (hi - lo), [&ref, lo, hi, step](float f) {
                        ref = std::round((lo + std::clamp(f, 0.0f, 1.0f) * (hi - lo)) / step) * step;
                        if (std::fabs(ref) < step * 0.5f && lo < 0.0f)
                            ref = 0.0f;
                    });
                };
                {
                    OptRow h{tr("perlin.options.handCustom", "Hand Position"), "", -1.0f, [](float, bool) {}};
                    h.header = true;
                    model.push_back(h);
                }
                auto& hs = options.hand;
                fieldSlider("Hand X", hs.armX, -1.0f, 1.0f, 0.01f, "");
                fieldSlider("Hand Y", hs.armY, -1.0f, 1.0f, 0.01f, "");
                fieldSlider("Hand Z", hs.armZ, -1.0f, 1.0f, 0.01f, "");
                fieldSlider("Hand Pitch", hs.armRotX, -180.0f, 180.0f, 1.0f, "\xC2\xB0");
                fieldSlider("Hand Yaw", hs.armRotY, -180.0f, 180.0f, 1.0f, "\xC2\xB0");
                fieldSlider("Hand Roll", hs.armRotZ, -180.0f, 180.0f, 1.0f, "\xC2\xB0");
                fieldSlider("Hand Scale", hs.armScale, 0.25f, 2.0f, 0.01f, "");
                {
                    OptRow h{tr("perlin.options.itemCustom", "Held Item Position"), "", -1.0f, [](float, bool) {}};
                    h.header = true;
                    model.push_back(h);
                }
                fieldSlider("Item X", hs.itemX, -1.0f, 1.0f, 0.01f, "");
                fieldSlider("Item Y", hs.itemY, -1.0f, 1.0f, 0.01f, "");
                fieldSlider("Item Z", hs.itemZ, -1.0f, 1.0f, 0.01f, "");
                fieldSlider("Item Pitch", hs.itemRotX, -180.0f, 180.0f, 1.0f, "\xC2\xB0");
                fieldSlider("Item Yaw", hs.itemRotY, -180.0f, 180.0f, 1.0f, "\xC2\xB0");
                fieldSlider("Item Roll", hs.itemRotZ, -180.0f, 180.0f, 1.0f, "\xC2\xB0");
                fieldSlider("Item Scale", hs.itemScale, 0.25f, 2.0f, 0.01f, "");
                model.push_back({tr("perlin.options.handReset", "Reset Hand Position"), "", -1.0f, [this](float, bool click) {
                                     if (click) {
                                         HandSettings d;
                                         auto& h = options.hand;
                                         h.armX = d.armX; h.armY = d.armY; h.armZ = d.armZ; h.armRotX = d.armRotX; h.armRotY = d.armRotY; h.armRotZ = d.armRotZ; h.armScale = d.armScale;
                                         h.itemX = d.itemX; h.itemY = d.itemY; h.itemZ = d.itemZ; h.itemRotX = d.itemRotX; h.itemRotY = d.itemRotY; h.itemRotZ = d.itemRotZ; h.itemScale = d.itemScale;
                                         options.Save();
                                     }
                                 }});
            } else {
                title = bindDevice == 1 ? std::string("Controller Binds") : tr("controls.keybinds.title", "Key Binds");
                static const std::pair<Action, const char*> kBinds[] = {
                    {Action::Forward, "Forward"}, {Action::Left, "Left"}, {Action::Back, "Back"}, {Action::Right, "Right"},
                    {Action::Jump, "Jump"}, {Action::Sneak, "Sneak"}, {Action::Sprint, "Sprint"}, {Action::Attack, "Attack"},
                    {Action::Use, "Use Item"}, {Action::PickBlock, "Pick Block"}, {Action::Inventory, "Inventory"},
                    {Action::Drop, "Drop Item"}, {Action::Hotbar1, "Hotbar 1"},
                    {Action::Hotbar2, "Hotbar 2"}, {Action::Hotbar3, "Hotbar 3"}, {Action::Hotbar4, "Hotbar 4"},
                    {Action::Hotbar5, "Hotbar 5"}, {Action::Hotbar6, "Hotbar 6"}, {Action::Hotbar7, "Hotbar 7"},
                    {Action::Hotbar8, "Hotbar 8"}, {Action::Hotbar9, "Hotbar 9"}, {Action::Chat, "Chat"},
                    {Action::Command, "Command"}, {Action::PlayerList, "Player List"}, {Action::Screenshot, "Screenshot"},
                    {Action::CyclePerspective, "Perspective"}, {Action::ToggleHud, "Hide GUI"}, {Action::DebugScreen, "Debug Screen"},
                    {Action::Zoom, "Zoom"}};
                constexpr int kPerPage = 12;
                const int total = static_cast<int>(std::size(kBinds));
                const int pages = (total + kPerPage - 1) / kPerPage;
                optionsSub = std::clamp(optionsSub, 0, pages - 1);
                for (int i = optionsSub * kPerPage; i < std::min(total, (optionsSub + 1) * kPerPage); i++) {
                    const Action a = kBinds[i].first;
                    const std::string name = kBinds[i].second;
                    const bool waiting = bindCapture == static_cast<int>(a);
                    std::string value = waiting ? "> ? <" : BindingOf(a).Serialize();
                    for (const char* prefix : {"key.keyboard.", "key.mouse.", "pad."}) {
                        const std::string pre = prefix;
                        if (value.rfind(pre, 0) == 0)
                            value = value.substr(pre.size());
                    }
                    if (value == "left" || value == "right" || value == "middle")
                        value += " button";
                    if (!waiting && !value.empty()) {
                        bool word = true;
                        for (char& c : value) {
                            if (word && c >= 'a' && c <= 'z')
                                c = static_cast<char>(c - 32);
                            word = c == ' ' || c == '.' || c == '_';
                            if (c == '_')
                                c = ' ';
                        }
                    }
                    if (value.empty())
                        value = "Not bound";
                    model.push_back({name, value, -1.0f, [this, a](float, bool click) {
                                         if (click) {
                                             bindCapture = static_cast<int>(a);
                                             bindStartedAt = Clock::now();
                                             optionsSignature.clear();
                                         }
                                     }});
                }
                model.push_back({tr("controls.resetAll", "Reset Keys"), "", -1.0f, [this](float, bool click) {
                                     if (click) {
                                         if (bindDevice == 1)
                                             options.padKeys = DefaultPadBindings();
                                         else
                                             options.keys = DefaultBindings();
                                         bindCapture = -1;
                                     }
                                 }});
                model.push_back({tr("perlin.options.page", "Page") + " " + std::to_string(optionsSub + 1) + "/" + std::to_string(pages), "", -1.0f,
                                 [this, pages](float, bool click) {
                                     if (click) {
                                         optionsSub = (optionsSub + 1) % pages;
                                         bindCapture = -1;
                                         optionsSignature.clear();
                                     }
                                 }});
            }

            std::vector<ui::OptionsRow> view;
            for (const OptRow& r : model) {
                ui::OptionsRow row{r.label, r.value, r.fraction};
                row.header = r.header;
                row.locked = r.locked;
                row.wide = r.wide;
                row.tab = r.tab;
                ui::SettingInfo info;
                if (!r.header && ui::FindSettingInfo(r.label, info)) {
                    row.tip = info.description;
                    row.impact = info.impact;
                }
                row.icon = r.icon;
                if (optionsPage == 11 && pauseView == 1) {
                    const size_t idx = view.size();
                    if (idx < modRowTips.size())
                        row.tip = modRowTips[idx];
                }
                view.push_back(row);
            }
            const int scrollKey = pauseView * 100 + optionsPage;
            if (scrollKey != optScrollKey) {
                optScrollKey = scrollKey;
                optScroll = 0.0f;
            }
            const ui::OptionsLayout probe = ui::ComputeOptionsLayout(view, guiW, guiH, 0.0f);
            if (probe.scrolling && bindCapture < 0)
                optScroll = std::clamp(optScroll - inputManager.Scroll() * 20.0f, 0.0f, probe.maxScroll);
            else if (!probe.scrolling)
                optScroll = 0.0f;
            const ui::OptionsLayout layout = ui::ComputeOptionsLayout(view, guiW, guiH, optScroll);
            const float gx = mx / static_cast<float>(scale), gy = my / static_cast<float>(scale);
            int hoverRow = -1;
            for (int i = 0; i < layout.count; i++)
                if (layout.visible[static_cast<size_t>(i)] && !view[static_cast<size_t>(i)].header && gx >= layout.x[i] && gx < layout.x[i] + layout.rowW[static_cast<size_t>(i)] &&
                    gy >= layout.y[i] && gy < layout.y[i] + layout.h && gy >= layout.listTop - 2.0f && gy < layout.listBottom) {
                    hoverRow = i;
                    break;
                }
            if (hoverRow != optHoverRow) {
                optHoverRow = hoverRow;
                optHoverSince = Clock::now();
            }
            const bool showTip = hoverRow >= 0 && Clock::now() - optHoverSince > std::chrono::milliseconds(350);
            std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + title + std::to_string(static_cast<int>(optScroll)) + (showTip ? "T" + std::to_string(hoverRow) : "");
            for (const ui::OptionsRow& r : view)
                signature += "|" + r.label + r.value;
            if (signature != optionsSignature || !hud.Overlay().HasOverlay()) {
                optionsSignature = signature;
                std::string error;
                if (!hud.Overlay().SetOverlay(ui::BuildOptionsScreenRml(view, title, tr("gui.done", "Done"), optScroll, guiW, guiH, showTip ? hoverRow : -1), error))
                    std::cout << "[ui] options screen: " << error << std::endl;
            }
            hud.Overlay().MouseMove(gx, gy);
            hud.UpdateOverlay(fbW, fbH, options.guiScale);
            const auto inside = [&](float x, float y, float w, float h) { return gx >= x && gx < x + w && gy >= y && gy < y + h; };
            const auto sliderFraction = [&](int row) { return (gx - layout.x[row] - 4.0f) / (layout.rowW[static_cast<size_t>(row)] - 8.0f); };
            input.SetPadCapture(bindCapture >= 0 && bindDevice == 1);
            if (bindCapture >= 0) {
                bool captured = false;
                if (bindDevice == 1 && Clock::now() - bindStartedAt > std::chrono::milliseconds(300))
                    for (int code = 0; code < 40 && !captured; code++)
                        if (input.IsPadDown(code)) {
                            SetBindingOf(static_cast<Action>(bindCapture), Binding::PadButton(code));
                            captured = true;
                        }
                for (const auto& t : inputManager.TakeTyped()) {
                    if (t.character || Clock::now() - bindStartedAt < std::chrono::milliseconds(120))
                        continue;
                    if (captured)
                        continue;
                    if (t.key != Key::Escape && bindDevice == 0)
                        SetBindingOf(static_cast<Action>(bindCapture), Binding::KeyboardKey(t.key));
                    captured = true;
                }
                for (int b = 1; b <= 2 && !captured && bindDevice == 0; b++)
                    if (input.IsMouseDown(b) && Clock::now() - bindStartedAt > std::chrono::milliseconds(120)) {
                        SetBindingOf(static_cast<Action>(bindCapture), Binding::MouseButton(b));
                        captured = true;
                    }
                if (captured) {
                    bindCapture = -1;
                    bindWasCapturing = true;
                    options.Save();
                    optionsSignature.clear();
                }
                pauseLeftDown = down;
                return;
            }
            bindWasCapturing = false;
            if (down && !pauseLeftDown && clickReady) {
                for (int i = 0; i < layout.count; i++)
                    if (layout.visible[static_cast<size_t>(i)] && !model[static_cast<size_t>(i)].header && inside(layout.x[i], layout.y[i], layout.rowW[static_cast<size_t>(i)], layout.h)) {
                        const OptRow& row = model[static_cast<size_t>(i)];
                        if (row.fraction >= 0.0f) {
                            optionsDrag = i;
                            row.apply(sliderFraction(i), true);
                        } else {
                            row.apply(0.0f, true);
                        }
                        break;
                    }
            } else if (down && optionsDrag >= 0 && optionsDrag < static_cast<int>(model.size())) {
                model[static_cast<size_t>(optionsDrag)].apply(sliderFraction(optionsDrag), false);
            }
            if (!down && pauseLeftDown) {
                if (optionsDrag >= 0 || clickReady) {
                    if (inside(layout.doneX, layout.doneY, layout.doneW, layout.h) && optionsDrag < 0) {
                        options.Save();
                        if (pauseView == 1 && optionsPage == 11) {
                            mods::ModManager::Instance().SettingsClosed(modSettingsFor);
                            pauseView = 4;
                            optionsPage = 0;
                            optionsSub = 0;
                        } else if (pauseView == 1 && optionsPage != 0) {
                            optionsPage = optionsPage == 5 ? 3 : optionsPage == 10 ? 8 : 0;
                            optionsSub = 0;
                        } else {
                            pauseView = 0;
                            pauseSignature.clear();
                        }
                        optionsSignature.clear();
                        hud.Overlay().ClearOverlay();
                    }
                }
                if (optionsDrag >= 0)
                    options.Save();
                optionsDrag = -1;
            }
            pauseLeftDown = down;
            return;
        }

        if (titleOptions) {
            titleOptions = false;
            titleSignature.clear();
            pauseView = 0;
            pauseShown = false;
            hud.Overlay().ClearOverlay();
            return;
        }


        ui::PauseScreenText t;
        t.title = i18n::Tr("menu.game");
        t.back = i18n::Tr("menu.returnToGame");
        t.options = i18n::Tr("menu.options");
        t.world = tr("perlin.worldOptions", "World Options");
        t.mods = tr("perlin.mods", "Mods");
        t.quit = i18n::Tr("menu.disconnect");
        t.advancements = tr("gui.advancements", "Advancements");
        t.stats = tr("gui.stats", "Statistics");
        t.feedback = tr("menu.sendFeedback", "Give Feedback");
        t.bugs = tr("menu.reportBugs", "Report Bugs");
        t.lan = tr("menu.shareToLan", "Open to LAN");
        t.players = tr("gui.playerList", "Player List");
        t.showReplay = live != nullptr && !preview && !live->IsReplay();
        t.replayRecording = replayCtl.Recording();
        t.replayStart = tr("perlin.replay.start", "Start Replay");
        t.replayStop = tr("perlin.replay.stop", "Stop Replay");
        t.showPanel = pausePlayers && live;
        if (t.showPanel) {
            t.panelTitle = t.players;
            t.roleOperator = tr("permissions.level.operator", "Operator");
            t.roleMember = tr("permissions.level.member", "Member");
            t.roleVisitor = tr("permissions.level.visitor", "Visitor");
            auto list = live->PlayerList();
            std::stable_sort(list.begin(), list.end(), [](const BedrockSession::TabPlayer& a, const BedrockSession::TabPlayer& b) {
                if (a.perm != b.perm)
                    return a.perm > b.perm;
                return a.name < b.name;
            });
            for (const BedrockSession::TabPlayer& p : list)
                t.panel.push_back({p.name, p.perm});
        }
        std::string panelSig;
        for (const auto& r : t.panel)
            panelSig += r.name + ":" + std::to_string(r.perm) + ",";
        const std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + (t.showPanel ? "P" : "p") + panelSig + (t.showReplay ? (t.replayRecording ? "R" : "r") : "") + t.replayStart + t.replayStop + t.title + t.back + t.options + t.quit + t.advancements + t.stats + t.feedback + t.bugs + t.lan + t.players + t.world + t.mods;
        if (signature != pauseSignature || !hud.Overlay().HasOverlay()) {
            pauseSignature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildPauseScreenRml(pauseTemplate, t, guiW, guiH), error)) {
                std::cout << "[ui] pause_screen.rml: " << error << std::endl;
                pauseTemplate = ui::DefaultPauseScreenTemplate();
                hud.Overlay().SetOverlay(ui::BuildPauseScreenRml(pauseTemplate, t, guiW, guiH), error);
            }
        }
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
        if (!down && pauseLeftDown && clickReady) {
            const std::string hover = hud.Overlay().OverlayHover();
            if (hover == ui::kPauseResumeId) {
                hud.Overlay().ClearOverlay();
                pauseShown = false;
                input.SetTextMode(false);
            } else if (hover == ui::kPauseReplayStartId && live && !replayCtl.Recording()) {
                ToggleReplayRecording(true);
                pauseSignature.clear();
            } else if (hover == ui::kPauseReplayStopId && live && replayCtl.Recording()) {
                ToggleReplayRecording(false);
                pauseSignature.clear();
            } else if (hover == ui::kPauseQuitId && !preview) {
                LeaveToServerList(hud);
            } else if (hover == ui::kPauseFeedbackId) {
                platform->OpenUrl("https://github.com/theselauraproject/perlin/issues");
            } else if (hover == ui::kPausePlayersId) {
                pausePlayers = !pausePlayers;
            } else if (hover == ui::kPauseBugsId) {
                platform->OpenUrl("https://github.com/theselauraproject/perlin/issues/new?labels=bug");
            } else if (hover == ui::kPauseWardrobeId) {
                pauseView = 5;
                optionsSignature.clear();
                hud.Overlay().ClearOverlay();
            } else if (hover == ui::kPauseModsId) {
                pauseView = 4;
                optionsSignature.clear();
                hud.Overlay().ClearOverlay();
            } else if (hover == ui::kPauseWorldId) {
                pauseView = 3;
                optionsSignature.clear();
                hud.Overlay().ClearOverlay();
            } else if (hover == ui::kPauseOptionsId) {
                pauseView = 1;
                optionsPage = 0;
                optionsSub = 0;
                optionsSignature.clear();
                hud.Overlay().ClearOverlay();
            }
        }
        pauseLeftDown = down;
    }

    namespace {
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
                        d.Set("#title_text", ui::Json(form.Find("title") ? form.Find("title")->StringOr("") : std::string()));
                        d.Set("#form_text", ui::Json(form.Find("content") ? form.Find("content")->StringOr("") : std::string()));
                        const ui::Json* list = form.Find("elements") ? form.Find("elements") : form.Find("buttons");
                        int i = 0;
                        if (list && list->IsArray())
                            for (const ui::Json& e : list->array) {
                                const ui::Json* image = e.Find("image");
                                const ui::Json* data = image ? image->Find("data") : nullptr;
                                d.Collection("form_buttons").push_back({{"#form_button_text", ui::Json(e.Find("text") ? e.Find("text")->StringOr("") : std::string())},
                                                                        {"#form_button_texture", ui::Json(data ? data->StringOr("") : std::string())}});
                                d.FactoryItems("buttons").push_back({"button", {}, "form_buttons", i++});
                            }
                        d.FactoryItems("server_form_factory").push_back({"long_form", {}, {}, -1});
                        return d;
                    }
                }
                const char* title = PERLIN_ENV("PERLIN_TEST_FORM_TITLE");
                d.Set("#title_text", std::string(title ? title : "&game_selectorSelect a game"));
                d.Set("#form_text", std::string(""));
                static const char* kGames[] = {"Lobby", "SkyWars", "EggWars", "Bed Wars", "Tower Defence", "Lucky Islands", "Free For All",
                                               "Parkour", "Block Wars", "Survival Games", "Duels", "Skyblock"};
                for (int i = 0; i < 30; i++) {
                    const std::string name = kGames[i % 12];
                    d.Collection("form_buttons").push_back({{"#form_button_text", ui::Json(name + "\n" + std::to_string(100 + i * 37) + " playing")},
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
                d.Set("#objective_sidebar_name", std::string(PERLIN_ENV("PERLIN_TEST_SIDEBAR_TITLE") ? PERLIN_ENV("PERLIN_TEST_SIDEBAR_TITLE") : "CubeCraft"));
                const std::vector<std::pair<std::string, int>> rows = {
                    {"\u00a7bUser: \u00a7f" + playerName, 8}, {"\u00a7bVIP Level: \u00a7f15", 7}, {"\u00a7bLevel: \u00a7f17", 6},
                    {"\u00a7bLobby: \u00a7fEU #11", 5}, {"\u00a7bPlayers: \u00a7f7,183", 4}, {" ", 3},
                    {"\u00a77" "28/10/26 (de0ax)", 2}, {"\u00a76cubecraft.net", 1}};
                d.Set("#scoreboard_sidebar_size", static_cast<double>(rows.size()));
                for (const auto& [name, score] : rows) {
                    d.Collection("scoreboard_players").push_back({{"#player_name_sidebar", ui::Json(name)}});
                    d.Collection("scoreboard_scores").push_back({{"#player_score_sidebar", ui::Json(std::to_string(score))}});
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

        std::string ServersPath() { return paths::DataDir() + "/servers.json"; }

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
    }

    namespace {
        // renders the skin as bedrock's paper doll (3d, facing slightly left) into a ui texture once per skin/cape;
        // returns its key for the json ui paper_doll_renderer
        std::string SkinDollKey(const std::string& skinPath, const std::string& capePath, bool showCape, float headYaw, float headPitch) {
            // angles snap to 3 degree steps: a moving cursor reuses renders; only the latest kept
            const int qy = static_cast<int>(std::lround(headYaw / 3.0f)) * 3, qp = static_cast<int>(std::lround(headPitch / 3.0f)) * 3;
            const std::string key = "doll:" + std::to_string(std::hash<std::string>{}(skinPath + "|" + capePath + (showCape ? "c" : "n"))) + ":" +
                                    std::to_string(qy) + ":" + std::to_string(qp);
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
                        const size_t i = (static_cast<size_t>((oy + y) * sc + sc / 2) * static_cast<size_t>(w) + static_cast<size_t>((ox + x) * sc + sc / 2)) * 4;
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

    void Perlin::FetchMissingGamertags() {
        if (!workers)
            return;
        gamertagFetchAt = std::chrono::steady_clock::now();
        for (const AccountEntry& e : accounts.Entries()) {
            if (!e.microsoft || !e.name.empty())
                continue;
            workers->Submit([this, id = e.id, path = accounts.AuthPath(e)] {
                xbox::Account acct(path);
                std::string error, gamertag;
                if (!acct.HasSavedLogin() || !acct.SignInSaved(error) || !acct.FetchGamertag(gamertag, error)) {
                    if (!error.empty())
                        std::cout << "[auth] couldn't learn an account's gamertag: " << error << std::endl;
                    return;
                }
                std::lock_guard lock(signInMutex);
                pendingRenames.emplace_back(id, gamertag);
            });
        }
    }

    void Perlin::ApplyActiveAccount() {
        const AccountEntry* a = accounts.Active();
        if (!a)
            return;
        options.onlineLogin = a->microsoft;
        if (!a->microsoft)
            options.playerName = a->name;
        onlineThisRun = options.onlineLogin && !args.offline;
        if (a->skinKnown && (a->skin != options.skin || a->cape != options.cape)) {
            options.skin = a->skin;
            options.cape = a->cape;
            SkinConfig cfg = MakeSkinConfig(options.skin, options.cape);
            LoadedSkin loaded;
            std::string err;
            if (LoadSkinConfig(cfg, loaded, err))
                options.slimArms = loaded.slimArms;
            else
                std::cout << "[accounts] skin: " << err << std::endl;
            wardInit = false;
        } else if (!a->skinKnown) {
            accounts.SetSkin(a->id, options.skin, options.cape);
        }
        options.Save();
        PrewarmSignIn();
    }

    // the Xbox Live / PlayFab / Minecraft token round trips take seconds: do them while the player is in the menus
    void Perlin::PrewarmSignIn() {
        if (!onlineThisRun || PERLIN_ENV("PERLIN_PREVIEW"))
            return;
        auto account = std::make_shared<xbox::Account>(ActiveAuthPath());
        if (account->HasSavedLogin())
            BedrockSession::PrewarmLogin(std::move(account), ActiveAuthPath());
    }

    std::string Perlin::ActiveAuthPath() const {
        const AccountEntry* a = accounts.Active();
        return a && a->microsoft ? accounts.AuthPath(*a) : paths::DataDir() + "/auth.json";
    }

    void Perlin::StartAccountSignIn() {
        CancelAccountSignIn();
        std::lock_guard lock(signInMutex);
        signInAuthFile = accounts.NewAuthFile();
        signInAccount = std::make_shared<xbox::Account>(paths::DataDir() + "/" + signInAuthFile);
        signInUrl.clear();
        signInCode.clear();
        signInStatus.clear();
        signInGamertag.clear();
        signInState = 1;
        workers->Submit([this, acct = signInAccount] {
            std::string error, gamertag;
            const bool ok = acct->SignIn(
                [this, acct](const std::string& url, const std::string& code) {
                    std::lock_guard lock(signInMutex);
                    if (signInAccount != acct)
                        return;
                    signInUrl = url;
                    signInCode = code;
                    signInStatus = "Waiting for you to sign in...";
                    platform->ShowSignInCode(url, code);
                },
                error);
            const bool named = ok && acct->FetchGamertag(gamertag, error);
            std::lock_guard lock(signInMutex);
            if (signInAccount != acct)
                return;
            if (ok) {
                signInGamertag = named ? gamertag : std::string();
                signInState = 2;
            } else {
                signInStatus = "Sign-in failed: " + error;
                signInState = 3;
            }
        });
    }

    void Perlin::CancelAccountSignIn() {
        std::lock_guard lock(signInMutex);
        if (!signInAccount)
            return;
        signInAccount->Cancel();
        signInAccount.reset();
        if (signInState != 2 && !signInAuthFile.empty()) {
            std::error_code ec;
            std::filesystem::remove(paths::DataDir() + "/" + signInAuthFile, ec);
        }
        signInState = 0;
    }

    namespace {
        std::string StripFormatting(const std::string& text) {
            std::string out;
            for (size_t i = 0; i < text.size(); i++) {
                if (static_cast<unsigned char>(text[i]) == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xA7) {
                    i += 2;
                    continue;
                }
                out += text[i];
            }
            return out;
        }
    }

    void Perlin::PingServers(const std::vector<std::string>& addresses) {
        for (const std::string& address : addresses) {
            {
                std::lock_guard lock(pingMutex);
                ui::ServerEntry& r = pingResults[address];
                if (r.status == ui::ServerEntry::Status::Pinging)
                    continue;
                r.status = ui::ServerEntry::Status::Pinging;
            }
            workers->Submit([this, address] {
                ui::ServerEntry r;
                std::string host, ad, error, text = address;
                uint16_t port = 19132;
                for (const char* scheme : {"https://", "http://"})
                    if (text.rfind(scheme, 0) == 0)
                        text.erase(0, std::char_traits<char>::length(scheme));
                const auto start = Clock::now();
                const bool parsed = ParseServer(text, host, port);
                const bool ok = parsed && RakNet::Ping(host, port, ad, error, 2500);
                NetherNetServerInfo info;
                const bool nether = parsed && !ok && NetherNet::Probe(host, port, info, error, 3);
                r.pingMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
                r.status = ok || nether ? ui::ServerEntry::Status::Online : ui::ServerEntry::Status::Offline;
                if (nether) {
                    r.motd = StripFormatting(info.name);
                    r.version = info.version;
                    r.players = info.players;
                    r.maxPlayers = info.maxPlayers;
                }
                if (ok) {
                    std::vector<std::string> f;
                    size_t from = 0;
                    for (size_t at; (at = ad.find(';', from)) != std::string::npos; from = at + 1)
                        f.push_back(ad.substr(from, at - from));
                    f.push_back(ad.substr(from));
                    if (f.size() > 1)
                        r.motd = StripFormatting(f[1]);
                    if (f.size() > 3)
                        r.version = f[3];
                    if (f.size() > 5) {
                        r.players = std::atoi(f[4].c_str());
                        r.maxPlayers = std::atoi(f[5].c_str());
                    }
                }
                std::lock_guard lock(pingMutex);
                pingResults[address] = std::move(r);
            });
        }
    }

    void Perlin::JoinSingleplayer() {
#ifdef PERLIN_SINGLEPLAYER
        ReleaseFriendLease();
        if (!singleplayerServer)
            singleplayerServer = std::make_unique<EmbeddedServer>();
        if (!singleplayerServer->Running()) {
            const std::string dir = paths::DataDir() + "/singleplayer";
            paths::EnsureDir(dir);
            // the server reads its block tables from files: write the copies the client carries
            for (const char* name : {"block_states.json", "block_destroy.tsv", "extra_items.txt"}) {
                std::string bytes;
                const std::string path = dir + "/" + name;
                std::error_code ec;
                if (resources::Read(name, bytes) && (!std::filesystem::exists(path, ec) || std::filesystem::file_size(path, ec) != bytes.size()))
                    std::ofstream(path, std::ios::binary) << bytes;
            }
#if defined(_WIN32)
            const char* library = "perlin_server.dll";
#elif defined(__APPLE__)
            const char* library = "libperlin_server.dylib";
#else
            const char* library = "libperlin_server.so";
#endif
            const std::string config = "{\"view_distance\":" + std::to_string(std::clamp(options.renderDistance, 2, 32)) + ",\"block_data_dir\":\"" + dir + "\"}";
            std::string error;
            if (!singleplayerServer->Start(paths::ExeDir() + "/" + library, dir, config, error)) {
                std::cout << "[singleplayer] " << error << std::endl;
                SetStatusHint("Singleplayer could not start: " + error);
                return;
            }
        }
        pendingSingleplayer = singleplayerServer->Connect();
        if (!pendingSingleplayer) {
            SetStatusHint("Singleplayer could not open a connection.");
            return;
        }
        titleActive = false;
        Connect();
#else
        SetStatusHint("This build has no singleplayer: it was built without PERLIN_BUILD_SERVER.");
#endif
    }

    void Perlin::JoinServer(const std::string& address) {
        if (address.empty())
            return;
        ReleaseFriendLease();
        options.server = address;
        options.Save();
        titleActive = false;
        Connect();
    }

    void Perlin::SetJoinMessage(const std::string& text) {
        if (leaseJoining) {
            SetStatusHint(text);
            return;
        }
        std::lock_guard lock(tabMutex);
        tabJoinMsg = text;
    }

    void Perlin::BeginLeaseJoin(const std::string& text) {
        leaseJoining = true;
        SetStatusHint(text);
        titleActive = false;
    }

    void Perlin::FailLeaseJoin(const std::string& text) {
        const bool loading = leaseJoining.exchange(false);
        SetStatusHint({});
        {
            std::lock_guard lock(tabMutex);
            tabJoinMsg = text;
        }
        if (loading)
            leaseJoinFailed = true;
    }

    void Perlin::RefreshTab(ui::ServerTab tab) {
        using T = ui::ServerTab;
        const bool microsoft = accounts.Active() && accounts.Active()->microsoft;
        const std::string authPath = ActiveAuthPath();
        if (tab == T::Lan) {
            if (lanBusy.exchange(true))
                return;
            {
                std::lock_guard lock(tabMutex);
                tabLan.clear();
                tabLanMsg = "Scanning for games on your local network";
                tabDirty = true;
            }
            std::thread([this] {
                std::vector<RakNet::LanServer> found;
                std::string error;
                RakNet::Discover(found, error, 2000);
                std::vector<ui::ServerEntry> rows;
                std::set<std::string> discoveredIps;
                {
                    std::string lanError;
                    std::shared_ptr<lan::Session> session;
                    {
                        std::lock_guard lock(lanSessionMutex);
                        if (!lanSession || !lanSession->Alive())
                            lanSession = lan::Session::Open(lanError);
                        session = lanSession;
                    }
                    if (session) {
                        for (int i = 0; i < 4; i++) {
                            session->Scan();
                            std::this_thread::sleep_for(std::chrono::milliseconds(400));
                        }
                        for (const lan::Host& h : session->Hosts()) {
                            ui::ServerEntry e;
                            e.kind = ui::ServerEntry::Kind::Lan;
                            e.xuid = "lan:" + std::to_string(h.networkId);
                            e.address = h.ip + ":" + std::to_string(h.port);
                            e.name = h.name.empty() ? "LAN Game" : h.name;
                            e.right = std::to_string(h.players) + "/" + std::to_string(h.maxPlayers);
                            e.motd = h.level;
                            e.version = h.version;
                            e.note = h.ip + (h.version.empty() ? "" : "  -  " + h.version);
                            discoveredIps.insert(h.ip);
                            rows.push_back(std::move(e));
                        }
                    } else if (!lanError.empty()) {
                        std::cout << "[lan] discovery unavailable: " << lanError << std::endl;
                    }
                }
                for (const RakNet::LanServer& f : found) {
                    if (discoveredIps.count(f.host))
                        continue;
                    std::vector<std::string> parts;
                    size_t a = 0;
                    for (size_t i = 0; i <= f.advertisement.size(); i++)
                        if (i == f.advertisement.size() || f.advertisement[i] == ';') {
                            parts.push_back(f.advertisement.substr(a, i - a));
                            a = i + 1;
                        }
                    ui::ServerEntry e;
                    e.kind = ui::ServerEntry::Kind::Lan;
                    e.address = f.host + ":" + std::to_string(f.port);
                    e.name = parts.size() > 1 && !parts[1].empty() ? parts[1] : "LAN Game";
                    std::string plain;
                    for (size_t i = 0; i < e.name.size(); i++) {
                        if (static_cast<unsigned char>(e.name[i]) == 0xC2 && i + 2 < e.name.size() && static_cast<unsigned char>(e.name[i + 1]) == 0xA7) {
                            i += 2;
                            continue;
                        }
                        plain += e.name[i];
                    }
                    e.name = plain;
                    e.right = parts.size() > 5 ? parts[4] + "/" + parts[5] : "";
                    e.motd = parts.size() > 7 ? parts[7] : "";
                    e.note = e.address + (parts.size() > 3 ? "  -  " + parts[3] : "");
                    rows.push_back(std::move(e));
                }
                {
                    std::vector<std::string> hosts{"127.0.0.1"};
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
                    if (ifaddrs* list = nullptr; getifaddrs(&list) == 0) {
                        for (ifaddrs* a = list; a; a = a->ifa_next) {
                            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || (a->ifa_flags & IFF_LOOPBACK) || !(a->ifa_flags & IFF_UP))
                                continue;
                            const uint32_t ip = ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr);
                            for (uint32_t last = 1; last < 255; last++) {
                                const uint32_t host = (ip & 0xFFFFFF00u) | last;
                                char text[32];
                                std::snprintf(text, sizeof text, "%u.%u.%u.%u", host >> 24, (host >> 16) & 255, (host >> 8) & 255, host & 255);
                                hosts.push_back(text);
                            }
                        }
                        freeifaddrs(list);
                    }
#endif
                    std::sort(hosts.begin(), hosts.end());
                    hosts.erase(std::unique(hosts.begin(), hosts.end()), hosts.end());
                    std::mutex foundMutex;
                    std::atomic<size_t> next{0};
                    const auto worker = [&] {
                        for (size_t i = next++; i < hosts.size(); i = next++) {
                            NetherNetServerInfo info;
                            std::string why;
                            if (discoveredIps.count(hosts[i]))
                                continue;
                            if (!NetherNet::Probe(hosts[i], 19132, info, why, 1))
                                continue;
                            ui::ServerEntry e;
                            e.kind = ui::ServerEntry::Kind::Lan;
                            e.address = hosts[i] + ":19132";
                            e.name = info.name.empty() ? "LAN Game" : info.name;
                            e.right = std::to_string(info.players) + "/" + std::to_string(info.maxPlayers);
                            e.motd = info.level;
                            e.note = e.address + (info.version.empty() ? "" : "  -  " + info.version);
                            std::lock_guard lock(foundMutex);
                            if (std::none_of(rows.begin(), rows.end(), [&](const ui::ServerEntry& r) { return r.address == e.address; }))
                                rows.push_back(std::move(e));
                        }
                    };
                    std::vector<std::thread> pool;
                    for (int i = 0; i < 48; i++)
                        pool.emplace_back(worker);
                    for (std::thread& t : pool)
                        t.join();
                }
                std::lock_guard lock(tabMutex);
                tabLan = std::move(rows);
                tabLanMsg = tabLan.empty() ? "No games found on your local network" + (error.empty() ? std::string() : " (" + error + ")") : "";
                tabDirty = true;
                lanBusy = false;
            }).detach();
            return;
        }
        if (tab == T::Saved) {
            RefreshFeatured(false);
            return;
        }
        if (tab != T::Realms && tab != T::Friends)
            return;
        const bool forRealms = tab == T::Realms;
        std::atomic<bool>& busy = forRealms ? realmsBusy : friendsBusy;
        if (!microsoft) {
            std::lock_guard lock(tabMutex);
            (forRealms ? tabRealms : tabFriends).clear();
            (forRealms ? tabRealmsMsg : tabFriendsMsg) = std::string("Sign in with a Microsoft account to see your ") + (forRealms ? "Realms" : "friends");
            tabDirty = true;
            return;
        }
        if (busy.exchange(true))
            return;
        {
            std::lock_guard lock(tabMutex);
            (forRealms ? tabRealms : tabFriends).clear();
            (forRealms ? tabRealmsMsg : tabFriendsMsg) = forRealms ? "Loading your Realms..." : "Loading your friends...";
            tabDirty = true;
        }
        std::thread([this, forRealms, authPath, &busy] {
            xbox::Account acct(authPath);
            std::string error;
            std::vector<ui::ServerEntry> rows;
            bool ok = acct.SignInSaved(error);
            if (ok && forRealms) {
                std::vector<realms::Realm> list;
                ok = realms::List(acct, list, error);
                for (const realms::Realm& r : list) {
                    ui::ServerEntry e;
                    e.kind = ui::ServerEntry::Kind::Realm;
                    e.name = r.name.empty() ? "Realm" : r.name;
                    e.realmId = r.id;
                    const bool open = r.state == "OPEN" && !r.expired;
                    e.offline = !open;
                    e.right = r.expired ? "Expired" : (r.state == "OPEN" ? "Open" : "Closed");
                    e.motd = !r.owner.empty() ? "Owned by " + r.owner : (r.member ? "Invited" : "Your Realm");
                    e.note = r.motd;
                    rows.push_back(std::move(e));
                }
            } else if (ok) {
                std::vector<friends::Friend> list;
                ok = friends::List(acct, list, error);
                {
                    std::atomic<size_t> next{0};
                    const auto fetch = [&] {
                        for (size_t i = next++; i < list.size(); i = next++) {
                            const friends::Friend& f = list[i];
                            const std::string key = "gp:" + f.xuid;
                            if (f.pictureUrl.empty() || ui::UiAssets::Instance().HasHead(key))
                                continue;
                            const http::Response pic = http::GetUrl(f.pictureUrl + (f.pictureUrl.find('?') == std::string::npos ? "?w=64&h=64" : ""), {}, 8);
                            Texture t;
                            if (pic.status == 200 && LoadTextureMemory(reinterpret_cast<const unsigned char*>(pic.body.data()), pic.body.size(), t))
                                ui::UiAssets::Instance().SetHead(key, std::move(t));
                        }
                    };
                    std::vector<std::thread> pool;
                    for (int i = 0; i < 6; i++)
                        pool.emplace_back(fetch);
                    for (std::thread& t : pool)
                        t.join();
                }
                for (const friends::Friend& f : list) {
                    ui::ServerEntry e;
                    e.kind = ui::ServerEntry::Kind::Friend;
                    e.name = f.gamertag;
                    e.xuid = f.xuid;
                    e.hosting = f.hosting;
                    e.offline = !f.online;
                    if (ui::UiAssets::Instance().HasHead("gp:" + f.xuid))
                        e.icon = "gp:" + f.xuid;
                    e.right = f.hosting ? "Hosting" : (f.playingMinecraft ? "In Minecraft" : (f.online ? "Online" : "Offline"));
                    e.motd = f.hosting && !f.worldName.empty() ? f.worldName : f.presence;
                    e.note = f.hosting ? std::to_string(f.players) + "/" + std::to_string(f.maxPlayers) + " players  -  click to join" : "Not hosting a world";
                    rows.push_back(std::move(e));
                }
            }
            std::lock_guard lock(tabMutex);
            auto& target = forRealms ? tabRealms : tabFriends;
            auto& message = forRealms ? tabRealmsMsg : tabFriendsMsg;
            target = std::move(rows);
            message = !ok ? error : (target.empty() ? (forRealms ? "You have no Realms yet" : "Your friends list is empty") : std::string());
            tabDirty = true;
            busy = false;
        }).detach();
    }

    void Perlin::RefreshFeatured(bool force) {
        if (!(accounts.Active() && accounts.Active()->microsoft)) {
            std::lock_guard lock(tabMutex);
            tabFeatured.clear();
            return;
        }
        if (!force && featuredAt != Clock::time_point{} && Clock::now() - featuredAt < std::chrono::minutes(10))
            return;
        if (featuredBusy.exchange(true))
            return;
        const std::string authPath = ActiveAuthPath();
        std::thread([this, authPath] {
            xbox::Account acct(authPath);
            std::string error;
            std::vector<featured::Server> list;
            const bool ok = acct.SignInSaved(error) && featured::List(acct, list, error);
            if (!ok)
                std::cout << "[featured] " << error << std::endl;
            std::vector<ui::ServerEntry> rows;
            for (const featured::Server& f : list) {
                ui::ServerEntry e;
                e.kind = ui::ServerEntry::Kind::Lan;
                e.name = f.name;
                e.address = f.host + ":" + std::to_string(f.port);
                e.right = f.ranked ? "Featured" : "Creator";
                e.pingable = true;
                e.motd = f.description;
                e.note = e.address + (f.ranked ? "" : "  -  Creator");
                const std::string key = "fs:" + e.address;
                if (!f.logoUrl.empty() && !ui::UiAssets::Instance().HasHead(key)) {
                    const http::Response pic = http::GetUrl(f.logoUrl, {}, 8);
                    Texture t;
                    if (pic.status == 200 && LoadTextureMemory(reinterpret_cast<const unsigned char*>(pic.body.data()), pic.body.size(), t))
                        ui::UiAssets::Instance().SetHead(key, std::move(t));
                }
                if (ui::UiAssets::Instance().HasHead(key))
                    e.icon = key;
                rows.push_back(std::move(e));
            }
            {
                std::lock_guard lock(tabMutex);
                if (ok) {
                    tabFeatured = std::move(rows);
                    featuredAt = Clock::now();
                    featuredPing = true;
                }
            }
            featuredBusy = false;
        }).detach();
    }

    void Perlin::AddRealm(const std::string& code) {
        if (!(accounts.Active() && accounts.Active()->microsoft)) {
            SetJoinMessage("Sign in with a Microsoft account to add a Realm");
            return;
        }
        if (realmAddBusy.exchange(true))
            return;
        SetJoinMessage("Adding the Realm...");
        const std::string authPath = ActiveAuthPath();
        std::thread([this, code, authPath] {
            xbox::Account acct(authPath);
            std::string error;
            realms::Realm added;
            if (acct.SignInSaved(error) && realms::AcceptInvite(acct, code, added, error)) {
                SetJoinMessage("Added the Realm \"" + added.name + "\"");
                realmAddedAt = Clock::now();
                realmAddedFlag = true;
            } else {
                SetJoinMessage(error.empty() ? "Could not add the Realm" : error);
            }
            realmAddBusy = false;
        }).detach();
    }

    void Perlin::JoinRealm(long long id) {
        if (!(accounts.Active() && accounts.Active()->microsoft)) {
            SetJoinMessage("Sign in with a Microsoft account to join a Realm");
            return;
        }
        if (realmJoinBusy.exchange(true))
            return;
        BeginLeaseJoin("Finding your Realm...");
        const std::string authPath = ActiveAuthPath();
        std::thread([this, id, authPath] {
            xbox::Account acct(authPath);
            std::string error;
            realms::Address address;
            if (acct.SignInSaved(error) && realms::Join(acct, id, address, error)) {
                if (address.protocol != "DEFAULT") {
                    SetJoinMessage("Connecting to the Realm...");
                    std::shared_ptr<friendworld::Lease> lease;
                    if (friendworld::JoinRealm(acct, address.host, address.protocol, kBedrockVersion, address.host, lease, error)) {
                        std::lock_guard lock(tabMutex);
                        friendLeaseReady = std::move(lease);
                    } else {
                        FailLeaseJoin(error.empty() ? "Could not reach the Realm" : error);
                    }
                } else {
                    std::lock_guard lock(tabMutex);
                    realmJoinAddress = address.host + ":" + std::to_string(address.port);
                    realmJoinReady = true;
                }
            } else {
                FailLeaseJoin(error.empty() ? "Could not join the Realm" : error);
            }
            realmJoinBusy = false;
        }).detach();
    }

    void Perlin::JoinFriend(const ui::ServerEntry& entry) {
        if (!entry.hosting) {
            SetJoinMessage(entry.name + (entry.offline ? " is offline" : " isn't hosting a world you can join"));
            return;
        }
        if (!(accounts.Active() && accounts.Active()->microsoft)) {
            SetJoinMessage("Sign in with a Microsoft account to join a friend");
            return;
        }
        if (friendJoinBusy.exchange(true))
            return;
        BeginLeaseJoin("Joining " + entry.name + "...");
        const std::string authPath = ActiveAuthPath();
        const std::string xuid = entry.xuid;
        const std::string name = entry.name;
        std::thread([this, authPath, xuid, name] {
            xbox::Account acct(authPath);
            std::string error;
            std::shared_ptr<friendworld::Lease> lease;
            if (acct.SignInSaved(error) &&
                friendworld::Join(acct, xuid, kBedrockVersion, lease, [this](const std::string& text) { SetJoinMessage(text); }, error)) {
                std::lock_guard lock(tabMutex);
                friendLeaseReady = std::move(lease);
            } else {
                std::cout << "[friends] join failed: " << error << std::endl;
                FailLeaseJoin(error.empty() ? "Could not join " + name : error);
            }
            friendJoinBusy = false;
        }).detach();
    }

    namespace {
        struct SharedSignaling : friendworld::Signaling {
            std::shared_ptr<friendworld::Signaling> inner;
            explicit SharedSignaling(std::shared_ptr<friendworld::Signaling> s) : inner(std::move(s)) {}
            bool Send(const std::string& to, const std::string& t, bool w, std::string& e) override { return inner->Send(to, t, w, e); }
            void SetHandler(std::function<void(const std::string&, const std::string&)> h) override { inner->SetHandler(std::move(h)); }
            bool Credentials(std::vector<friendworld::IceServerInfo>& s, std::string& e) override { return inner->Credentials(s, e); }
            bool Alive() const override { return inner->Alive(); }
            void Close() override { inner->SetHandler(nullptr); }
        };
    }

    void Perlin::JoinLanGame(const ui::ServerEntry& entry) {
        if (!(accounts.Active() && accounts.Active()->microsoft)) {
            SetJoinMessage("Sign in with a Microsoft account to join a LAN game");
            return;
        }
        std::shared_ptr<lan::Session> session;
        {
            std::lock_guard lock(lanSessionMutex);
            session = lanSession;
        }
        if (!session || !session->Alive()) {
            SetJoinMessage("Refresh the LAN list first");
            return;
        }
        if (friendJoinBusy.exchange(true))
            return;
        BeginLeaseJoin("Joining " + entry.name + "...");
        const std::string authPath = ActiveAuthPath();
        const uint64_t id = std::strtoull(entry.xuid.c_str() + 4, nullptr, 10);
        const std::string name = entry.name, version = entry.version;
        std::thread([this, authPath, id, name, version, session] {
            xbox::Account acct(authPath);
            std::string error;
            xbox::ServiceInfo info;
            if (acct.SignInSaved(error) && acct.ServiceSession(kBedrockVersion, info, error)) {
                auto lease = std::make_shared<friendworld::Lease>();
                lease->signaling = std::make_shared<SharedSignaling>(session);
                lease->hostNetworkId = std::to_string(id);
                lease->issuer = info.issuer;
                lease->hostName = name;
                lease->version = version;
                std::lock_guard lock(tabMutex);
                friendLeaseReady = std::move(lease);
            } else {
                std::cout << "[lan] join failed: " << error << std::endl;
                FailLeaseJoin(error.empty() ? "Could not join " + name : error);
            }
            friendJoinBusy = false;
        }).detach();
    }

    void Perlin::ReleaseFriendLease() {
        std::shared_ptr<friendworld::Lease> lease = std::move(friendLease);
        friendLease.reset();
        if (!lease)
            return;
        std::thread([lease = std::move(lease)]() mutable { lease.reset(); }).detach();
    }

    void Perlin::ToggleReplayRecording(bool start) {
        if (start) {
            if (!session || replayCtl.Recording())
                return;
            replay::ReplayController::LiveInfo info;
            info.server = session->ServerAddress();
            info.gameVersion = session->GameVersion();
            info.playerName = session->PlayerName();
            info.protocol = session->ServerProtocol();
            info.runtimeId = session->GetRuntimeId();
            const auto pose = session->GetPose();
            info.pose = {pose.x, pose.y, pose.z, pose.yaw, pose.pitch, pose.known};
            for (const auto& pf : session->ServerPackFiles())
                info.packs.push_back({pf.path, pf.fileName, pf.uuid, pf.key});
            std::string error;
            if (replayCtl.StartRecording(info, PERLIN_VERSION, error)) {
                chat.AddMessage("\xC2\xA7" "aReplay recording started", hudAnim.ticks);
                std::cout << "[replay] recording started" << std::endl;
            } else {
                chat.AddMessage("\xC2\xA7" "cCould not start the replay: " + error, hudAnim.ticks);
                std::cout << "[replay] start failed: " << error << std::endl;
            }
        } else {
            FinishReplayRecording("you stopped it");
        }
    }

    void Perlin::FinishReplayRecording(const char* why) {
        if (!replayCtl.Recording())
            return;
        const uint32_t ms = replayCtl.RecordingMs();
        std::string path, error;
        if (replayCtl.StopRecording(path, error)) {
            std::cout << "[replay] saved " << path << " (" << ms / 1000 << "s) because " << why << std::endl;
            chat.AddMessage("\xC2\xA7" "aReplay saved: " + std::filesystem::path(path).filename().string() + " (" + std::to_string(ms / 1000) + "s)", hudAnim.ticks);
        } else {
            std::cout << "[replay] save failed: " << error << std::endl;
            chat.AddMessage("\xC2\xA7" "cReplay could not be saved: " + error, hudAnim.ticks);
        }
    }

    void Perlin::LeaveToServerList(ui::Hud& hud) {
        std::cout << "[perlin] left " << options.server << ", back to the server list" << std::endl;
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
        disconnectShown = disconnectTextMode = disconnectLeftDown = false;
        disconnectSignature.clear();
        loadingShown = loadingTextMode = loadingLeftDown = false;
        loadingSignature.clear();
        loadingStartAt = {};
        loadingReadyArmed = false;
        pauseShown = invShown = deathShown = formShown = false;
        deathTextMode = false;
        pauseSignature.clear();
        deathSignature.clear();
        if (chat.IsOpen())
            chat.Close();
        levelUnloadPending = true;
        titleActive = true;
        titleReturnToServers = !inSingleplayer;
        inSingleplayer = false;
    }

    int Perlin::FrameGenMultiplier() {
        if (options.frameGen == "auto") {
            double target = platform ? platform->DisplayRefreshHz() : 0.0;
            if (target < 30.0)
                target = 60.0;
            if (options.maxFps > 0)
                target = std::min(target, static_cast<double>(options.maxFps));
            return fgPacer.AutoTotal(target, Clock::now());
        }
        return options.frameGen == "4x" ? 4 : options.frameGen == "3x" ? 3 : options.frameGen == "2x" ? 2 : 1;
    }

    void Perlin::DrawTitlePanorama(int fbW, int fbH) {
        const auto now = Clock::now();
        const uint64_t packStamp = (TexturePack::Instance().Generation() << 32) ^ ServerPacks::Instance().Generation();
        if (packStamp != panoramaPackStamp) {
            panoramaPackStamp = packStamp;
            for (uint32_t& id : panoramaTextures) {
                if (id)
                    renderer->UiDestroyTexture(id);
                id = 0;
            }
            panoramaReady = false;
            panoramaRetryAt = {};
        }
        if (!panoramaReady) {
            if (now < panoramaRetryAt)
                return;
            panoramaRetryAt = now + std::chrono::seconds(2);
            int loaded = 0;
            for (int i = 0; i < 6; i++) {
                if (panoramaTextures[i])
                    continue;
                Texture t;
                if (ui::UiAssets::Instance().Image("textures/ui/panorama_" + std::to_string(i), t) && t.width > 0)
                    panoramaTextures[i] = renderer->UiCreateTexture(t.width, t.height, t.pixels.data(), true);
            }
            for (uint32_t id : panoramaTextures)
                loaded += id != 0;
            panoramaReady = loaded == 6;
            if (!panoramaReady)
                return;
            panoramaClock = now;
        }
        const double seconds = std::chrono::duration<double>(now - panoramaClock).count();
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
            const uint32_t mesh = renderer->UiCreateFrameMesh(vertices.data(), static_cast<uint32_t>(vertices.size()), f.indices.data(),
                                                              static_cast<uint32_t>(f.indices.size()));
            if (!mesh)
                continue;
            UiDraw draw;
            draw.mesh = mesh;
            draw.texture = panoramaTextures[i];
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
            assetSkip = true;
        if (assetSkip || (!busy && !failed && !preview)) {
            if (assetShown) {
                assetShown = false;
                assetSignature.clear();
                hud.Overlay().ClearOverlay();
                titleShown = false;
            }
            return false;
        }
        const auto now = Clock::now();
        if (!assetShown) {
            assetShown = true;
            assetOpenedAt = now;
            assetSampleAt = now;
            assetLeftDown = true;
            assetSignature.clear();
            for (int i = 0; i < 2; i++) {
                assetLastBytes[i] = jobs[i]->bytes.done.load();
                assetSpeed[i] = 0.0;
            }
            input.SetTextMode(true);
        }
        const double dt = std::chrono::duration<double>(now - assetSampleAt).count();
        if (dt >= 0.25) {
            for (int i = 0; i < 2; i++) {
                const uint64_t b = jobs[i]->bytes.done.load();
                const double inst = b >= assetLastBytes[i] ? static_cast<double>(b - assetLastBytes[i]) / dt : 0.0;
                assetSpeed[i] = assetSpeed[i] * 0.6 + inst * 0.4;
                assetLastBytes[i] = b;
            }
            assetSampleAt = now;
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
                    std::snprintf(buf, sizeof(buf), "%.1f / %.1f MB  -  %.1f MB/s", mb(done), mb(total), mb(static_cast<uint64_t>(assetSpeed[i])));
                    row.status = buf;
                    row.progress = static_cast<float>(static_cast<double>(done) / static_cast<double>(total));
                } else if (j.filesTotal.load() > 0) {
                    std::snprintf(buf, sizeof(buf), "%llu / %llu files", static_cast<unsigned long long>(j.filesDone.load()),
                                  static_cast<unsigned long long>(j.filesTotal.load()));
                    row.status = buf;
                    row.progress = static_cast<float>(j.filesDone.load()) / static_cast<float>(j.filesTotal.load());
                } else {
                    row.status = "Downloading...";
                    indeterminate++;
                }
                break;
            case assets::Stage::Extracting:
                if (j.filesTotal.load() > 0) {
                    std::snprintf(buf, sizeof(buf), "Unpacking %llu / %llu files", static_cast<unsigned long long>(j.filesDone.load()),
                                  static_cast<unsigned long long>(j.filesTotal.load()));
                    row.status = buf;
                    row.progress = static_cast<float>(j.filesDone.load()) / static_cast<float>(j.filesTotal.load());
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

        const int scale = hud.OverlayScale();
        const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
        const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));
        const double time = std::chrono::duration<double>(now - assetOpenedAt).count();
        std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "|" + t.subtitle;
        for (const ui::DownloadRow& r : t.rows)
            signature += "|" + r.status + "|" + std::to_string(static_cast<int>(r.progress * 1000.0f));
        if (indeterminate)
            signature += "|" + std::to_string(static_cast<int>(time * 30.0));
        if (signature != assetSignature || !hud.Overlay().HasOverlay()) {
            assetSignature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildDownloadScreenRml(t, guiW, guiH, time), error))
                std::cout << "[ui] download screen: " << error << std::endl;
        }
        float mx = input.MouseX(), my = input.MouseY();
        if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))
            std::sscanf(m, "%f,%f", &mx, &my);
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (!down && assetLeftDown && now - assetOpenedAt > std::chrono::milliseconds(300)) {
            const std::string hover = hud.Overlay().OverlayHover();
            if (hover == ui::kDownloadRetryId) {
                std::cout << "[ui] download screen: retry" << std::endl;
                StartAssetDownloads();
            } else if (hover == ui::kDownloadSkipId) {
                assetSkip = true;
            }
        }
        assetLeftDown = down;
        return true;
    }

    bool Perlin::UpdateTitleScreen(ui::Hud& hud, int fbW, int fbH) {
        const auto now = Clock::now();
        if (titleActive && titleOptions) {
            if (titleShown) {
                hud.Overlay().ClearOverlay();
                titleShown = false;
                titleSignature.clear();
            }
            DrawTitlePanorama(fbW, fbH);
            return false;
        }
        if (!titleActive) {
            if (titleShown) {
                hud.Overlay().ClearOverlay();
                titleShown = false;
                titleSignature.clear();
                if (!chat.IsOpen())
                    input.SetTextMode(false);
            }
            titleLeftDown = false;
            return false;
        }
        if (UpdateAssetScreen(hud, fbW, fbH))
            return true;
        if (!titleShown) {
            titleShown = true;
            titleOpenedAt = now;
            titleLeftDown = true;
            titleSignature.clear();
            title.page = ui::TitlePage::Main;
            title.selected = -1;
            title.scroll = 0;
            LoadServers(title.servers, options.server);
            title.version = std::string("Perlin ") + PERLIN_VERSION;
            if (titleReturnToServers) {
                titleReturnToServers = false;
                title.page = ui::TitlePage::Servers;
                title.tab = ui::ServerTab::Saved;
                for (size_t i = 0; i < title.servers.size(); i++)
                    if (title.servers[i].address == options.server)
                        title.selected = static_cast<int>(i) + title.savedOffset;
            }
            if (titleReturnToReplays) {
                titleReturnToReplays = false;
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
                        title.servers = {{"Fozzy Minecraft Server", "23.109.60.29:25565"}, {"Minecraft Server", "play.example.net:19132"}};
                    title.page = page == "servers" ? ui::TitlePage::Servers : page == "edit" ? ui::TitlePage::Edit : ui::TitlePage::Direct;
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
                    titleOpenView = page == "mods" ? 4 : 5;
                    titleOptions = true;
                }
            }
            input.SetTextMode(true);
        }

        const int scale = hud.OverlayScale();
        const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
        const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));

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
                case ui::ServerEntry::Kind::Header: break;
                case ui::ServerEntry::Kind::Realm: JoinRealm(e.realmId); break;
                case ui::ServerEntry::Kind::Friend: JoinFriend(e); break;
                case ui::ServerEntry::Kind::Lan:
                    if (e.xuid.rfind("lan:", 0) == 0)
                        JoinLanGame(e);
                    else
                        JoinServer(e.address);
                    break;
                default: JoinServer(e.address); break;
            }
        };
        const auto back = [&] {
            switch (title.page) {
                case ui::TitlePage::Servers: title.page = ui::TitlePage::Main; break;
                case ui::TitlePage::Edit:
                case ui::TitlePage::Direct:
                case ui::TitlePage::AddRealm: title.page = ui::TitlePage::Servers; break;
                case ui::TitlePage::Accounts: title.page = ui::TitlePage::Main; break;
                case ui::TitlePage::Replays: title.page = ui::TitlePage::Main; break;
                case ui::TitlePage::ReplayRename:
                case ui::TitlePage::ReplayDelete: title.page = ui::TitlePage::Replays; break;
                case ui::TitlePage::AccountName: title.page = ui::TitlePage::Accounts; break;
                case ui::TitlePage::AccountSignIn:
                    CancelAccountSignIn();
                    title.page = ui::TitlePage::Accounts;
                    break;
                default: break;
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
                const ui::ServerEntry& s = title.servers[static_cast<size_t>(std::max(0, title.SavedIndex()))];
                title.name = s.name;
                title.address = s.address;
            }
        };

        for (const auto& t : inputManager.TakeTyped()) {
            if (!titleActive)
                break;
            const bool fields = title.page == ui::TitlePage::Edit || title.page == ui::TitlePage::Direct || title.page == ui::TitlePage::AddRealm;
            if (!t.character && t.key == Key::Escape) {
                back();
            } else if (title.page == ui::TitlePage::Replays || title.page == ui::TitlePage::ReplayRename || title.page == ui::TitlePage::ReplayDelete) {
                TitleReplayTyped(t);
            } else if (title.page == ui::TitlePage::AccountName) {
                ui::TextLimits limits{16};
                limits.accept = [](uint32_t cp) { return cp != ' '; };
                if (!t.character && t.key == Key::Enter)
                    addOffline();
                else
                    ui::TextEdit::Focused().Apply(t, title.accountName, limits);
            } else if (fields) {
                std::string& field = title.page == ui::TitlePage::Direct || title.page == ui::TitlePage::AddRealm || title.focus == 1 ? title.address : title.name;
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
        if (!titleActive)
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
            const int maxScroll = std::max(0, static_cast<int>(title.accounts.size()) - ui::TitleAccountRows(guiH));
            if (const float wheel = inputManager.Scroll(); wheel != 0.0f)
                title.accountScroll -= wheel > 0 ? 1 : -1;
            title.accountScroll = std::clamp(title.accountScroll, 0, maxScroll);
        }

        if (connectsInFlight.load() == 0)
            UnloadServerPacks();

        if (title.page == ui::TitlePage::Main || title.page == ui::TitlePage::Accounts || title.page == ui::TitlePage::AccountName ||
            title.page == ui::TitlePage::AccountSignIn)
            DrawTitlePanorama(fbW, fbH);

        {
            std::lock_guard lock(signInMutex);
            for (const auto& [id, gamertag] : pendingRenames)
                accounts.Rename(id, gamertag);
            pendingRenames.clear();
            if (signInState == 2) {
                accounts.AddMicrosoft(signInGamertag, signInAuthFile);
                signInAccount.reset();
                signInState = 0;
                ApplyActiveAccount();
                if (title.page == ui::TitlePage::AccountSignIn)
                    title.page = ui::TitlePage::Accounts;
            }
            title.signInUrl = signInUrl;
            title.signInCode = signInCode;
            title.signInStatus = signInStatus;
        }
        title.accounts.clear();
        for (const AccountEntry& e : accounts.Entries()) {
            const std::string head = SkinHeadKey(e.skinKnown ? e.skin : (e.id == accounts.ActiveId() ? options.skin : std::string()));
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
        if (title.page == ui::TitlePage::Accounts && titleLastPage != ui::TitlePage::Accounts &&
            Clock::now() - gamertagFetchAt > std::chrono::seconds(30) && !PERLIN_ENV("PERLIN_PREVIEW"))
            FetchMissingGamertags();

        if (title.page == ui::TitlePage::Servers && titleLastPage != ui::TitlePage::Servers && titleLastPage != ui::TitlePage::Edit &&
            titleLastPage != ui::TitlePage::Direct && titleLastPage != ui::TitlePage::AddRealm) {
            std::vector<std::string> addresses;
            for (const ui::ServerEntry& e : title.servers)
                addresses.push_back(e.address);
            PingServers(addresses);
            if (!PERLIN_ENV("PERLIN_PREVIEW"))
                RefreshFeatured(false);
        }
        titleLastPage = title.page;
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
                            head.name = std::string(kNames[i]) + " [" + std::to_string(sections[i].size()) + "]";
                            head.realmId = i;
                            title.friends.push_back(head);
                            if (title.friendsOpen[i])
                                title.friends.insert(title.friends.end(), sections[i].begin(), sections[i].end());
                        }
                }
                title.tabMessage = title.tab == ui::ServerTab::Lan ? tabLanMsg : title.tab == ui::ServerTab::Realms ? tabRealmsMsg : tabFriendsMsg;
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
                titleActive = false;
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
                    title.savedView.insert(title.savedView.end(), title.featured.begin(), title.featured.end());
            }
            const int offset = static_cast<int>(title.savedView.size());
            if (offset != title.savedOffset) {
                if (title.tab == ui::ServerTab::Saved && title.selected >= title.savedOffset)
                    title.selected += offset - title.savedOffset;
                title.savedOffset = offset;
            }
            title.savedView.insert(title.savedView.end(), title.servers.begin(), title.servers.end());
        }
        title.nativePanorama = panoramaReady;
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

        const int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        title.caret = (ms / 500) % 2 == 0;
        title.spinner = static_cast<int>(ms / 250);
        std::string signature = (title.oreUi ? "ore:" + title.dollSrc + "|" : std::string()) + std::to_string(guiW) + "x" + std::to_string(guiH) + "|" + std::to_string(static_cast<int>(title.page)) + "|" +
                                std::to_string(title.selected) + "|" + std::to_string(title.scroll) + "|" + std::to_string(title.focus) +
                                (title.nativePanorama ? "|np" : "|sp") + "|" + title.logoSrc;
        signature += "|" + title.accountLabel + "|" + title.accountUser + "|" + title.accountHead;
        signature += "|" + std::to_string(title.accountScroll);
        if (title.page == ui::TitlePage::Accounts)
            for (const ui::AccountRow& a : title.accounts)
                signature += "|" + a.id + a.name + (a.active ? "*" : "") + a.headKey;
        else if (title.page == ui::TitlePage::AccountName)
            signature += "|" + title.accountName + (title.caret ? "c" : "n");
        else if (title.page == ui::TitlePage::AccountSignIn)
            signature += "|" + title.signInUrl + "|" + title.signInCode + "|" + title.signInStatus;
        if (title.page == ui::TitlePage::Replays || title.page == ui::TitlePage::ReplayRename || title.page == ui::TitlePage::ReplayDelete) {
            signature += "|r" + std::to_string(title.replaySelected) + "|" + std::to_string(title.replayScroll) + "|" + std::to_string(title.replaySort) +
                         (title.replaySortDesc ? "d" : "a") + "|" + title.replayMessage + "|" + title.replayName + (title.caret ? "c" : "n") + "|" + std::to_string(title.replays.size());
            for (const ui::ReplayEntry& r : title.replays)
                signature += "|" + r.file + r.name + r.line1;
        }
        if (title.page == ui::TitlePage::Edit || title.page == ui::TitlePage::Direct || title.page == ui::TitlePage::AddRealm)
            signature += "|" + title.joinMessage + "|" + title.name + "|" + title.address + "|" + (title.caret ? "c" : "n") + (title.adding ? "a" : "e");
        else if (title.page == ui::TitlePage::Servers) {
            signature += "|t" + std::to_string(static_cast<int>(title.tab)) + "|" + title.tabMessage + "|" + title.joinMessage;
            signature += "|" + std::to_string(title.Shown().size()) + (title.friendsOpen[0] ? "H" : "h") + (title.friendsOpen[1] ? "O" : "o") + (title.friendsOpen[2] ? "F" : "f") + (title.friendsOpen[3] ? "S" : "s");
            for (const ui::ServerEntry& s : title.Shown())
                signature += "|" + s.name + "|" + s.address + "|" + std::to_string(static_cast<int>(s.status)) + s.motd + "|" + std::to_string(s.players) + "/" +
                             std::to_string(s.maxPlayers) + "|" + std::to_string(s.pingMs) + s.right + s.note + (s.offline ? "o" : "") +
                             (s.status == ui::ServerEntry::Status::Pinging ? std::to_string(title.spinner & 3) : std::string());
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
            screens.SetPaperDoll(SkinDollKey(options.skin, options.cape, options.skinParts.cape, testYaw, testPitch));
            signature = "forced:" + std::string(forcedScreen) + std::to_string(screens.Generation()) + forcedData.Signature();
        }
        if (signature != titleSignature || !hud.Overlay().HasOverlay()) {
            titleSignature = signature;
            std::string error, rml;
            if (forcedScreen) {
                if (!ui::JsonScreens::Instance().Build(forcedScreen, forcedData, guiW, guiH, 0.0f, rml) || !hud.Overlay().SetOverlay(rml, error))
                    std::cout << "[screens] forced " << forcedScreen << " failed: " << error << std::endl;
            } else if (!hud.Overlay().SetOverlay(ui::BuildTitleRml(title, guiW, guiH), error)) {
                std::cout << "[ui] title screen: " << error << std::endl;
            }
        }
        float mx = input.MouseX(), my = input.MouseY();
        if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))
            std::sscanf(m, "%f,%f", &mx, &my);
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (!down && titleLeftDown && now - titleOpenedAt > std::chrono::milliseconds(150)) {
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
                if (acc < static_cast<int>(title.accounts.size()) && accounts.SetActive(title.accounts[static_cast<size_t>(acc)].id))
                    ApplyActiveAccount();
            } else if (hover == ui::kTitleSingle) {
                JoinSingleplayer();
            } else if (hover == ui::kTitleMulti) {
                title.page = ui::TitlePage::Servers;
                title.tab = ui::ServerTab::Lan;
                title.selected = -1;
                title.scroll = 0;
                SetJoinMessage({});
                RefreshTab(title.tab);
            } else if (hover == ui::kTitleOptions) {
                titleOpenView = 1;
                titleOptions = true;
            } else if (hover == ui::kTitleMods) {
                titleOpenView = 4;
                titleOptions = true;
            } else if (hover == ui::kTitleWardrobe) {
                titleOpenView = 5;
                titleOptions = true;
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
                if (row < static_cast<int>(shown.size()) && shown[static_cast<size_t>(row)].kind == ui::ServerEntry::Kind::Header) {
                    title.friendsOpen[std::clamp<long long>(shown[static_cast<size_t>(row)].realmId, 0, 3)] ^= true;
                    title.selected = -1;
                } else {
                    if (row == titleClickRow && now - titleClickAt < std::chrono::milliseconds(400))
                        join(row);
                    title.selected = row;
                    titleClickRow = row;
                    titleClickAt = now;
                }
            }
        }
        titleLeftDown = down;
        return titleActive || titleShown;
    }

    bool Perlin::UpdateLoadingScreen(ui::Hud& hud, int fbW, int fbH) {
        if (leaseJoinFailed.exchange(false)) {
            hud.Overlay().ClearOverlay();
            loadingShown = loadingTextMode = loadingLeftDown = false;
            loadingSignature.clear();
            titleActive = true;
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
        if (titleActive || titleShown)
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
            t.warning = "Perlin is not yet stable, and may get you kicked/banned by server anticheats, due to movement and other inaccuracies";
        bool show = false;
        const bool transferring = session && session->Transferring();
        if (preview) {
            show = true;
            t.message = tr("progressScreen.title.downloading", "Downloading packs %1", {"3/25"});
            t.progress = 0.4f;
            t.detail = "40%  -  8.7 / 21.7 MB  -  262 KB/s";
        } else if (PERLIN_ENV("PERLIN_PREVIEW") && !session) {
            show = false;
        } else if (!session || (!sessionReady && session->GetState() != BedrockSession::State::Failed) || transferring) {
            show = true;
            if (transferring)
                loadingDone = false;
            const BedrockSession::State st = session ? session->GetState() : BedrockSession::State::Disconnected;
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
                t.message = onlineThisRun ? tr("connect.authorizing", "Logging in...") : tr("progressScreen.message.locating", "Locating server");
            } else if (st == BedrockSession::State::ResourcePacks) {
                if (progress.total > 0 && progress.done < progress.total) {
                    const int pct = progress.bytesTotal > 0 ? static_cast<int>(100.0 * static_cast<double>(progress.bytes) / static_cast<double>(progress.bytesTotal)) : 0;
                    t.message = tr("progressScreen.title.downloading", "Downloading packs %1", {std::to_string(progress.done + 1) + "/" + std::to_string(progress.total)});
                    t.progress = progress.bytesTotal > 0 ? static_cast<float>(progress.bytes) / static_cast<float>(progress.bytesTotal)
                                                          : static_cast<float>(progress.done) / static_cast<float>(progress.total);
                    static uint64_t rateBytes = 0;
                    static auto rateAt = now;
                    static double rate = 0.0;
                    if (progress.bytes < rateBytes) {
                        rateBytes = progress.bytes;
                        rateAt = now;
                        rate = 0.0;
                    }
                    if (const double dt = std::chrono::duration<double>(now - rateAt).count(); dt >= 2.0) {
                        rate = static_cast<double>(progress.bytes - rateBytes) / dt;
                        rateBytes = progress.bytes;
                        rateAt = now;
                    }
                    const auto mb = [](uint64_t b) {
                        char s[32];
                        std::snprintf(s, sizeof(s), "%.1f", static_cast<double>(b) / (1024.0 * 1024.0));
                        return std::string(s);
                    };
                    t.detail = std::to_string(pct) + "%  -  " + mb(progress.bytes) + " / " + mb(progress.bytesTotal) + " MB";
                    if (rate > 0.0)
                        t.detail += "  -  " + std::to_string(static_cast<int>(rate / 1024.0)) + " KB/s";
                } else {
                    t.message = tr("progressScreen.title.applyingPacks", "Loading resource packs");
                }
            } else {
                t.message = tr("connect.connecting", "Connecting to the server...");
            }
        } else if (sessionReady && !loadingDone) {
            show = true;
            if (!loadingStartAt.time_since_epoch().count())
                loadingStartAt = now;
            const int chunks = session->ChunksReceived();
            const int freshChunks = chunks - loadingChunkBase;
            const bool assets = levelRenderer && levelRenderer->Blocks().Complete();
            const bool enough = freshChunks >= 9 && assets;
            if (enough) {
                if (!loadingReadyArmed) {
                    loadingReadyArmed = true;
                    loadingReadyAt = now;
                }
            } else {
                loadingReadyArmed = false;
            }
            const bool timeout = now - loadingStartAt > std::chrono::seconds(5);
            if (loadingReadyArmed || timeout) {
                loadingDone = true;
                show = false;
            } else {
                t.message = tr("progressScreen.message.building", "Building terrain");
                const float fraction = std::min(1.0f, static_cast<float>(freshChunks) / 49.0f);
                t.progress = std::min(0.98f, fraction);
            }
        }
        if (!show) {
            if (loadingShown) {
                hud.Overlay().ClearOverlay();
                loadingShown = false;
                loadingSignature.clear();
                loadingStartAt = {};
                loadingReadyArmed = false;
            }
            if (loadingTextMode) {
                loadingTextMode = false;
                if (!chat.IsOpen())
                    input.SetTextMode(false);
            }
            loadingLeftDown = false;
            return false;
        }
        t.cancel = tr("gui.cancel", "Cancel");
        if (t.progress >= 0.0f && t.detail.empty())
            t.detail = std::to_string(static_cast<int>(std::clamp(t.progress, 0.0f, 1.0f) * 100.0f)) + "%";
        if (!input.TextMode())
            input.SetTextMode(true);
        loadingTextMode = true;
        if (!loadingShown) {
            loadingOpenedAt = now;
            loadingLeftDown = true;
            loadingShown = true;
            pauseShown = invShown = deathShown = false;
            pauseSignature.clear();
            deathSignature.clear();
        }
        if (now - loadingCheckAt > std::chrono::milliseconds(400) || loadingTemplate.empty()) {
            loadingCheckAt = now;
            uint64_t version = 0;
            std::string text = ui::LoadUiTemplate(paths::DataDir(), "loading_screen.rml", ui::DefaultLoadingScreenTemplate(), version);
            if (version != loadingTemplateVersion || loadingTemplate.empty()) {
                loadingTemplateVersion = version;
                loadingTemplate = std::move(text);
                loadingSignature.clear();
            }
        }
        const int scale = hud.OverlayScale();
        const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
        const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));
        const double time = std::chrono::duration<double>(now.time_since_epoch()).count();
        const std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "|" + t.title + "|" + t.message + "|" + t.detail + "|" + t.cancel;
        bool modHidden = false;
        {
            std::lock_guard lock(modHooks.mutex);
            modHidden = !modHooks.loadingHidden.empty();
        }
        // a mod hid the loading screen: connection carries on underneath, nothing drawn over it
        if (modHidden) {
            if (hud.Overlay().HasOverlay())
                hud.Overlay().ClearOverlay();
            loadingSignature.clear();
        } else if (signature != loadingSignature || !hud.Overlay().HasOverlay()) {
            loadingSignature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildLoadingScreenRml(loadingTemplate, t, guiW, guiH, time), error)) {
                std::cout << "[ui] loading_screen.rml: " << error << std::endl;
                loadingTemplate = ui::DefaultLoadingScreenTemplate();
                hud.Overlay().SetOverlay(ui::BuildLoadingScreenRml(loadingTemplate, t, guiW, guiH, time), error);
            }
        }
        float mx = input.MouseX(), my = input.MouseY();
        if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))
            std::sscanf(m, "%f,%f", &mx, &my);
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (!down && loadingLeftDown && !preview && now - loadingOpenedAt > std::chrono::milliseconds(300) &&
            hud.Overlay().OverlayHover() == ui::kLoadingCancelId) {
            std::cout << "[ui] loading screen: cancel" << std::endl;
            LeaveToServerList(hud);
            return true;
        }
        loadingLeftDown = down;
        return true;
    }

    bool Perlin::UpdateDisconnectScreen(ui::Hud& hud, int fbW, int fbH) {
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_DISCONNECT") != nullptr;
        const bool failed = preview || (session && session->GetState() == BedrockSession::State::Failed && !session->Transferring());
        const auto now = Clock::now();
        if (!failed) {
            if (disconnectShown) {
                hud.Overlay().ClearOverlay();
                disconnectShown = false;
                disconnectSignature.clear();
            }
            if (disconnectTextMode) {
                disconnectTextMode = false;
                if (!chat.IsOpen())
                    input.SetTextMode(false);
            }
            disconnectLeftDown = false;
            return false;
        }
        if (!disconnectShown) {
            disconnectShown = true;
            disconnectOpenedAt = now;
            pauseShown = invShown = deathShown = false;
            pauseSignature.clear();
            deathSignature.clear();
            std::cout << "[ui] disconnect screen shown" << std::endl;
        }
        if (!disconnectTextMode) {
            disconnectTextMode = true;
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
            info.message = PERLIN_ENV("PERLIN_PREVIEW_DISCONNECT")[0] == 'b'
                               ? "You are banned from this server.\nReason: Cheating (fly hacks)\nExpires in 29d 23h 59m"
                               : "Kicked by an operator";
            if (info.outdated)
                info.message.clear();
        } else {
            info = session->GetDisconnectInfo();
            lastError = session->LastError();
        }
        std::string message = ui::StripFormatting(info.message);
        if (!message.empty() && message.find(' ') == std::string::npos && message.find('.') != std::string::npos) {
            const std::string translated = i18n::Tr(message);
            if (translated != message)
                message = translated;
        }
        if (info.outdated != 0) {
            const bool clientOld = info.outdated == 1;
            t.title = i18n::Tr(clientOld ? "perlin.disconnect.outdated_client" : "perlin.disconnect.outdated_server");
            t.reason = i18n::Tr(clientOld ? "perlin.disconnect.outdated_client_reason" : "perlin.disconnect.outdated_server_reason");
            const std::string theirs = !info.serverVersion.empty() ? info.serverVersion
                                       : info.serverProtocol > 0  ? "protocol " + std::to_string(info.serverProtocol)
                                                                  : "?";
            t.reason += "\n\n" + i18n::Tr("perlin.disconnect.versions", {theirs, std::string(kBedrockVersion)});
            if (!message.empty())
                t.reason += "\n" + message;
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
                case 44: t.reason += "\n\n" + i18n::Tr("perlin.disconnect.hint.id_conflict"); break;
                case 8:
                case 35: t.reason += "\n\n" + i18n::Tr("perlin.disconnect.hint.version"); break;
                case 46: t.reason += "\n\n" + i18n::Tr("perlin.disconnect.hint.login"); break;
                default: break;
            }
            if (info.reason >= 0)
                t.code = i18n::Tr("perlin.disconnect.code", {std::to_string(info.reason)});
        } else {
            t.title = i18n::Tr("perlin.disconnect.failed");
            t.reason = lastError.empty() ? i18n::Tr("perlin.disconnect.no_reason") : lastError;
        }

        if (now - disconnectCheckAt > std::chrono::milliseconds(400) || disconnectTemplate.empty()) {
            disconnectCheckAt = now;
            uint64_t version = 0;
            std::string text = ui::LoadUiTemplate(paths::DataDir(), "disconnect_screen.rml", ui::DefaultDisconnectScreenTemplate(), version);
            if (version != disconnectTemplateVersion || disconnectTemplate.empty()) {
                disconnectTemplateVersion = version;
                disconnectTemplate = std::move(text);
                disconnectSignature.clear();
                std::cout << "[ui] disconnect screen: loaded " << paths::DataDir() << "/ui/disconnect_screen.rml" << std::endl;
            }
        }
        const int scale = hud.OverlayScale();
        const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
        const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));
        const std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "|" + t.title + "|" + t.reason + "|" + t.code;
        if (signature != disconnectSignature || !hud.Overlay().HasOverlay()) {
            disconnectSignature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildDisconnectScreenRml(disconnectTemplate, t, guiW, guiH), error)) {
                std::cout << "[ui] disconnect_screen.rml: " << error << std::endl;
                disconnectTemplate = ui::DefaultDisconnectScreenTemplate();
                hud.Overlay().SetOverlay(ui::BuildDisconnectScreenRml(disconnectTemplate, t, guiW, guiH), error);
            }
        }
        float mx = input.MouseX(), my = input.MouseY();
        if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))
            std::sscanf(m, "%f,%f", &mx, &my);
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (!down && disconnectLeftDown && now - disconnectOpenedAt > std::chrono::milliseconds(300) && !preview) {
            const std::string hover = hud.Overlay().OverlayHover();
            if (hover == ui::kDisconnectRejoinId) {
                std::cout << "[ui] disconnect screen: rejoin" << std::endl;
                hud.Overlay().ClearOverlay();
                disconnectShown = false;
                disconnectSignature.clear();
                if (sessionReady && session && session->RequestReconnect()) {
                } else {
                    sessionReady = false;
                    Connect();
                }
            } else if (hover == ui::kDisconnectQuitId) {
                LeaveToServerList(hud);
            }
        }
        disconnectLeftDown = down;
        return true;
    }

    void Perlin::UpdateChatClicks(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        if (!chat.IsOpen() || (!live && !PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))) {
            chatLeftDown = false;
            return;
        }
        const int scale = hud.OverlayScale();
        float mx = input.MouseX(), my = input.MouseY();
        if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_MOUSE"))
            std::sscanf(m, "%f,%f", &mx, &my);
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
        const bool down = input.IsMouseDown(0);
        if (down && !chatLeftDown) {
            const std::string link = hud.Overlay().HoverLink();
            if (link.rfind("link:url:", 0) == 0) {
                const std::string url = link.substr(9);
                if (url.rfind("https://", 0) == 0 && platform)
                    platform->OpenUrl(url);
                else if (url.rfind("http://", 0) == 0 && platform)
                    platform->OpenUrl("https://" + url.substr(7));
            } else if (link.size() > 6 && link[5] == '/' && live) {
                live->SendCommand(link.substr(6));
                chat.Close();
                input.SetTextMode(false);
            }
        }
        chatLeftDown = down;
    }

    void Perlin::FillHudState(ui::HudState& s, BedrockSession* live, bool preview) {
        // a screen over the world (server form, container, inventory) hides the hud, like #hud_visible in the game
        s.visible = options.showHud && (live || preview) && !(replayCtl.Playing() && replayUi.hideHud) && !formShown && !invShown;
        s.tick = preview ? static_cast<uint64_t>(std::chrono::duration<double>(Clock::now().time_since_epoch()).count() * 20.0)
                         : hudAnim.ticks;
        s.inWater = headUnderwater;
        chat.Fill(s, hudAnim.ticks, MsNow());
        s.sidebarVisible = false;
        s.sidebarTitle.clear();
        s.sidebar.clear();
        if (live) {
            const auto sb = live->Scoreboard();
            s.sidebarVisible = sb.visible;
            if (sb.visible) {
                s.sidebarTitle = sb.title;
                for (const auto& r : sb.rows)
                    s.sidebar.push_back({r.name, r.score});
            }
        } else if (preview && PERLIN_ENV("PERLIN_PREVIEW_SCOREBOARD")) {
            s.sidebarVisible = true;
            s.sidebarTitle = "\xC2\xA7" "e\xC2\xA7" "lPERLIN";
            s.sidebar = {{"Kills", 12}, {"Deaths", 3}, {"\xC2\xA7" "aCoins", 450}};
        }
        if (preview) {
            s.health = 20.0f;
            s.food = 20;
            s.xpProgress = 0.55f;
            s.xpLevel = 7;
            s.armor = 0;
            s.selectedSlot = previewSlot;
            static const struct { const char* name; int count; int maxDamage; int aux; } kItems[9] = {
                {"minecraft:diamond_sword", 1, 1561, 120}, {"minecraft:grass_block", 64, 0, 0},
                {"minecraft:oak_planks", 12, 0, 0}, {"minecraft:torch", 33, 0, 0},
                {"minecraft:apple", 5, 0, 0}, {"minecraft:cooked_beef", 2, 0, 0},
                {"minecraft:iron_pickaxe", 1, 250, 40}, {"minecraft:oak_log", 20, 0, 0}, {"minecraft:bucket", 1, 0, 0}};
            for (int i = 0; i < 9; i++) {
                s.hotbar[i] = {};
                s.hotbar[i].name = kItems[i].name;
                s.hotbar[i].count = kItems[i].count;
                s.hotbar[i].maxDamage = kItems[i].maxDamage;
                s.hotbar[i].aux = kItems[i].aux;
                s.hotbar[i].enchanted = PERLIN_ENV("PERLIN_PREVIEW_ENCHANT") != nullptr;
                if (s.hotbar[i].enchanted)
                    s.hotbar[i].enchants = {{9, 3}, {12, 2}, {17, 3}, {26, 1}};
                s.hotbar[i].nbtTags = 2;
            }
            s.burning = PERLIN_ENV("PERLIN_PREVIEW_FIRE") != nullptr;
            s.fireFrame = static_cast<int>(s.tick % 32);
            // PERLIN_PREVIEW_SCREENANIM=totem or an effect id (28 bad omen, 29 hero of the village): loops every 2.5 s
            if (const char* anim = PERLIN_ENV("PERLIN_PREVIEW_SCREENANIM")) {
                const double t = std::fmod(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(), 2.5) / 2.0;
                if (t < 1.0) {
                    s.screenAnimation = std::string(anim) == "totem" ? BedrockSession::kTotemScreenAnimation : std::atoi(anim);
                    s.screenAnimationT = static_cast<float>(t);
                }
            }
            if (const char* item = PERLIN_ENV("PERLIN_PREVIEW_ITEM")) {
                ui::HudItem& h = s.hotbar[std::clamp(previewSlot, 0, 8)];
                h.name = item;
                h.count = 1;
                h.maxDamage = h.aux = 0;
            }
            if (const char* list = PERLIN_ENV("PERLIN_PREVIEW_ITEMS")) {
                std::string all = list;
                size_t at = 0;
                for (int slot = 0; slot < 9 && at <= all.size(); slot++) {
                    size_t comma = all.find(',', at);
                    if (comma == std::string::npos) comma = all.size();
                    ui::HudItem& h = s.hotbar[slot];
                    h = {};
                    h.name = all.substr(at, comma - at);
                    h.count = 1;
                    if (const char* dye = PERLIN_ENV("PERLIN_PREVIEW_DYE")) {
                        h.hasColor = true;
                        h.color = static_cast<int32_t>(std::strtoul(dye, nullptr, 16));
                    }
                    at = comma + 1;
                }
            }
            if (const char* tab = PERLIN_ENV("PERLIN_PREVIEW_TAB")) {
                s.tabOpen = true;
                s.tabEntries.clear();
                Texture head;
                head.width = head.height = 8;
                head.pixels.assign(8 * 8 * 4, 255);
                for (size_t i = 0; i < 64; i++) { head.pixels[i * 4] = 170; head.pixels[i * 4 + 1] = 120; head.pixels[i * 4 + 2] = 90; }
                ui::UiAssets::Instance().SetHead("preview", head);
                for (int i = 0; i < std::atoi(tab); i++)
                    s.tabEntries.push_back({"Player" + std::to_string(i + 1), "preview"});
            }
            if (PERLIN_ENV("PERLIN_PREVIEW_EMPTYHAND"))
                s.hotbar[previewSlot] = {};
            if (const char* n = PERLIN_ENV("PERLIN_PREVIEW_ITEMNAME")) {
                s.itemName = n;
                s.itemNameAlpha = 1.0f;
            }
            s.displayHealth = s.health;
            s.effects.clear();
            if (PERLIN_ENV("PERLIN_PREVIEW_EFFECTS")) {
                s.effects.push_back({13, 0, 200, false});
                s.effects.push_back({1, 1, 3600, false});
                s.effects.push_back({10, 0, 1200, true});
                s.effects.push_back({19, 0, 600, false});
                s.effects.push_back({2, 2, 90, false});
            }
            s.locator.clear();
            s.locatorActive = false;
            if (const char* loc = PERLIN_ENV("PERLIN_PREVIEW_LOCATOR")) {
                s.locatorActive = true;
                const int count = std::max(1, std::atoi(loc));
                for (int i = 0; i < count; i++) {
                    ui::LocatorMarker m;
                    m.angle = count == 1 ? 0.0f : -55.0f + 110.0f * static_cast<float>(i) / static_cast<float>(count - 1);
                    m.distance = static_cast<float>(i) * 400.0f / static_cast<float>(std::max(1, count - 1));
                    m.pitch = i % 4 == 1 ? 1 : (i % 4 == 3 ? -1 : 0);
                    std::array<uint8_t, 16> id{};
                    id[0] = static_cast<uint8_t>(37 * (i + 3));
                    id[9] = static_cast<uint8_t>(91 * (i + 1));
                    m.color = ui::LocatorColorFor(id);
                    s.locator.push_back(m);
                }
            }
            return;
        }
        if (!live)
            return;

        const auto vitals = live->GetVitals();
        const auto inv = live->GetInventory();
        switch (live->GameType()) {
        case 1: s.mode = ui::GameMode::Creative; break;
        case 2: s.mode = ui::GameMode::Adventure; break;
        case 3: case 4: case 6: s.mode = ui::GameMode::Spectator; break;
        default: s.mode = ui::GameMode::Survival; break;
        }
        s.health = vitals.health;
        s.maxHealth = std::max(1.0f, vitals.maxHealth);
        s.absorption = vitals.absorption;
        s.food = static_cast<int>(std::lround(vitals.hunger));
        s.saturation = vitals.saturation;
        s.xpLevel = vitals.level;
        s.xpProgress = vitals.xpProgress;
        s.air = vitals.air;
        s.burning = vitals.onFire && s.mode != ui::GameMode::Spectator;
        s.fireFrame = static_cast<int>(s.tick % 32);
        if (auto next = live->TakeScreenAnimation(); next.effect >= 0)
            screenAnimation = next;
        if (screenAnimation.effect >= 0) {
            // vanilla plays the item activation for 40 ticks
            const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - screenAnimation.started).count() / 2.0f;
            if (t >= 1.0f) {
                screenAnimation = {};
            } else {
                s.screenAnimation = screenAnimation.effect;
                s.screenAnimationT = t;
            }
        }
        s.maxAir = std::max(1, vitals.maxAir);
        s.poison = vitals.poison;
        s.wither = vitals.wither;
        s.regeneration = vitals.regeneration;
        s.hungerEffect = vitals.hungerEffect;

        s.selectedSlot = std::clamp(inv.selected, 0, 8);
        for (int i = 0; i < 9; i++)
            s.hotbar[i] = ToHudItem(inv.hotbar[i]);
        s.offhand = ToHudItem(inv.offhand);
        int armor = 0;
        for (int i = 0; i < 4; i++) {
            s.armorItems[i] = ToHudItem(inv.armor[i]);
            armor += ui::ArmorPoints(inv.armor[i].name);
        }
        s.armor = std::min(20, armor);

        s.healthBlinkUntil = hudAnim.blinkUntil;
        s.displayHealth = static_cast<float>(hudAnim.displayHealth);

        s.effects.clear();
        for (const auto& e : live->GetEffects())
            s.effects.push_back({e.id, e.amplifier, e.ticks, e.ambient});
        s.locator.clear();
        s.locatorActive = false;
        {
            const std::vector<BedrockSession::LocatorWaypoint> waypoints = live->GetLocatorWaypoints();
            const double eyeX = camera.x, eyeY = camera.y, eyeZ = camera.z;
            const double lookElevation = static_cast<double>(camera.pitch);
            const double halfFov = static_cast<double>(camera.fov) * 0.5;
            for (const BedrockSession::LocatorWaypoint& wp : waypoints) {
                if (!wp.visible)
                    continue;
                double tx = wp.x, ty = wp.y, tz = wp.z;
                bool known = wp.hasPosition;
                if (wp.hasActor) {
                    entity::Entity target;
                    if (live->Entities().FindUnique(wp.actorId, target)) {
                        if (target.isSelf)
                            continue;
                        tx = target.pos.x;
                        ty = target.pos.y + 1.0;
                        tz = target.pos.z;
                        known = true;
                    }
                }
                if (!known)
                    continue;
                s.locatorActive = true;
                const double dx = tx - eyeX, dy = ty - eyeY, dz = tz - eyeZ;
                const double horizontal = std::sqrt(dx * dx + dz * dz);
                double angle = std::atan2(dz, dx) * 180.0 / 3.14159265358979323846 - static_cast<double>(camera.yaw);
                angle = std::fmod(angle + 540.0, 360.0) - 180.0;
                ui::LocatorMarker m;
                m.angle = static_cast<float>(angle);
                m.distance = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
                const double elevation = std::atan2(dy, std::max(horizontal, 1e-6)) * 180.0 / 3.14159265358979323846;
                const double rel = elevation - lookElevation;
                m.pitch = std::fabs(rel) <= halfFov ? 0 : (rel > 0 ? 1 : -1);
                m.color = wp.hasColor ? (static_cast<uint32_t>(wp.color) & 0xFFFFFFu) : ui::LocatorColorFor(wp.id);
                if (angle > -60.0 && angle <= 60.0)
                    s.locator.push_back(m);
            }
        }

        if (!pauseShown)
            pausePlayers = false;
        s.tabOpen = inputManager.IsDown(Action::PlayerList) && !inputManager.Blocked();
        s.tabEntries.clear();
        if (s.tabOpen) {
            auto players = live->PlayerList();
            std::stable_sort(players.begin(), players.end(), [](const BedrockSession::TabPlayer& a, const BedrockSession::TabPlayer& b) {
                std::string x = a.name, y = b.name;
                std::transform(x.begin(), x.end(), x.begin(), ::tolower);
                std::transform(y.begin(), y.end(), y.begin(), ::tolower);
                return x < y;
            });
            for (const BedrockSession::TabPlayer& p : players) {
                std::string key = "default";
                if (p.skin && p.skin->skinWidth >= 64 && p.skin->skinHeight >= 16) {
                    key = p.uuid + ":" + std::to_string(p.skin->serial);
                    if (!ui::UiAssets::Instance().HasHead(key)) {
                        const int sc = std::max(1, p.skin->skinWidth / 64);
                        Texture t;
                        t.width = t.height = 8;
                        t.pixels.assign(8 * 8 * 4, 255);
                        for (int y = 0; y < 8; y++)
                            for (int x = 0; x < 8; x++) {
                                const auto px = [&](int ox, int oy, uint8_t out[4]) {
                                    const size_t i = (static_cast<size_t>((oy + y) * sc + sc / 2) * p.skin->skinWidth +
                                                      static_cast<size_t>((ox + x) * sc + sc / 2)) * 4;
                                    for (int c = 0; c < 4; c++)
                                        out[c] = i + 3 < p.skin->skinRgba.size() ? p.skin->skinRgba[i + c] : 255;
                                };
                                uint8_t face[4], hat[4];
                                px(8, 8, face);
                                px(40, 8, hat);
                                uint8_t* o = &t.pixels[static_cast<size_t>(y * 8 + x) * 4];
                                const float a = hat[3] / 255.0f;
                                for (int c = 0; c < 3; c++)
                                    o[c] = static_cast<uint8_t>(face[c] * (1.0f - a) + hat[c] * a);
                                o[3] = 255;
                            }
                        ui::UiAssets::Instance().SetHead(key, std::move(t));
                    }
                } else if (!ui::UiAssets::Instance().HasHead(key)) {
                    Texture t;
                    t.width = t.height = 8;
                    t.pixels.assign(8 * 8 * 4, 255);
                    for (size_t i = 0; i < 64; i++) {
                        t.pixels[i * 4] = 170; t.pixels[i * 4 + 1] = 120; t.pixels[i * 4 + 2] = 90;
                    }
                    ui::UiAssets::Instance().SetHead(key, std::move(t));
                }
                s.tabEntries.push_back({p.name, key});
            }
        }

        const ui::HudItem& held = s.hotbar[s.selectedSlot];
        if (!held.Empty() && hudAnim.toolTimer > 0 && s.mode != ui::GameMode::Spectator) {
            const std::string shortName = held.name.rfind("minecraft:", 0) == 0 ? held.name.substr(10) : held.name;
            int rarity = 0;
            static const char* const kUncommon[] = {"experience_bottle", "nether_star", "totem_of_undying", "heart_of_the_sea", "dragon_breath", "enchanted_book"};
            static const char* const kRare[] = {"golden_apple", "beacon", "conduit"};
            static const char* const kEpic[] = {"enchanted_golden_apple", "dragon_egg", "command_block", "chain_command_block",
                                                "repeating_command_block", "structure_block", "jigsaw"};
            for (const char* n : kUncommon) if (shortName == n) rarity = 1;
            for (const char* n : kRare) if (shortName == n) rarity = 2;
            for (const char* n : kEpic) if (shortName == n) rarity = 3;
            if (shortName.rfind("music_disc_", 0) == 0)
                rarity = 2;
            if (held.enchanted && shortName != "enchanted_book" && shortName != "enchanted_golden_apple")
                rarity = rarity <= 1 ? 2 : 3;
            static const char* const kRarityCode[] = {"f", "e", "b", "d"};
            s.itemName = held.customName.empty() ? ui::UiAssets::Instance().ItemName(held.name, held.aux) : held.customName;
            s.itemName = std::string("\xC2\xA7") + kRarityCode[rarity] + (held.customName.empty() ? "" : "\xC2\xA7o") + s.itemName;
            s.itemNameAlpha = std::min(255.0f, static_cast<float>(hudAnim.toolTimer) * 256.0f / 10.0f) / 255.0f;
        } else {
            s.itemName.clear();
            s.itemNameAlpha = 0.0f;
        }

        const auto text = live->GetTitleText();
        if (hudAnim.overlayTime > 0 && !text.actionBar.empty()) {
            const float f = static_cast<float>(hudAnim.overlayTime) - hudPartial;
            s.actionBar = text.actionBar;
            s.actionBarAlpha = std::clamp(static_cast<int>(f * 255.0f / 20.0f), 0, 255) > 8
                                   ? std::min(255, static_cast<int>(f * 255.0f / 20.0f)) / 255.0f
                                   : 0.0f;
        } else {
            s.actionBar.clear();
            s.actionBarAlpha = 0.0f;
        }
        if (hudAnim.titleTime > 0 && !text.title.empty()) {
            const float f = static_cast<float>(hudAnim.titleTime) - hudPartial;
            int alpha = 255;
            if (hudAnim.titleTime > hudAnim.titleFadeOut + hudAnim.titleStay) {
                const float f1 = static_cast<float>(hudAnim.titleFadeIn + hudAnim.titleStay + hudAnim.titleFadeOut) - f;
                alpha = static_cast<int>(f1 * 255.0f / std::max(1, hudAnim.titleFadeIn));
            }
            if (hudAnim.titleTime <= hudAnim.titleFadeOut)
                alpha = static_cast<int>(f * 255.0f / std::max(1, hudAnim.titleFadeOut));
            alpha = std::clamp(alpha, 0, 255);
            s.title = text.title;
            s.subtitle = text.subtitle;
            s.titleAlpha = alpha > 8 ? static_cast<float>(alpha) / 255.0f : 0.0f;
        } else {
            s.title.clear();
            s.subtitle.clear();
            s.titleAlpha = 0.0f;
        }
    }

    void Perlin::TickHud() {
        HudAnim& a = hudAnim;
        a.ticks++;
        const int tick = static_cast<int>(a.ticks);
        ui::HudState& s = hudState;

        const int health = static_cast<int>(std::ceil(s.health));
        if (!a.haveHealth) {
            a.haveHealth = true;
            a.lastHealth = health;
            a.displayHealth = health;
            a.lastHealthTick = tick;
        }
        const bool invulnerable = tick - a.lastHurtTick < 20;
        if (health < a.lastHealth) {
            a.lastHurtTick = tick;
            a.lastHealthTick = tick;
            a.blinkUntil = tick + 20;
        } else if (health > a.lastHealth && invulnerable) {
            a.lastHealthTick = tick;
            a.blinkUntil = tick + 10;
        }
        if (tick - a.lastHealthTick > 20) {
            a.lastHealth = health;
            a.displayHealth = health;
            a.lastHealthTick = tick;
        }
        a.lastHealth = health;

        const ui::HudItem& held = s.hotbar[std::clamp(s.selectedSlot, 0, 8)];
        if (held.Empty()) {
            a.toolTimer = 0;
        } else if (!a.lastTool.Empty() && a.lastTool.name == held.name &&
                   (held.maxDamage > 0 || a.lastTool.aux == held.aux)) {
            if (a.toolTimer > 0)
                a.toolTimer--;
        } else {
            a.toolTimer = 40;
        }
        a.lastTool = held;

        if (a.overlayTime > 0)
            a.overlayTime--;
        if (a.titleTime > 0)
            a.titleTime--;
        if (session) {
            const auto text = session->GetTitleText();
            if (text.actionAt != a.actionAtSeen) {
                a.actionAtSeen = text.actionAt;
                if (!text.actionBar.empty())
                    a.overlayTime = 60;
            }
            if (text.titleAt != a.titleAtSeen) {
                a.titleAtSeen = text.titleAt;
                if (!text.title.empty()) {
                    a.titleFadeIn = std::max(0, text.fadeIn);
                    a.titleStay = std::max(0, text.stay);
                    a.titleFadeOut = std::max(0, text.fadeOut);
                    a.titleTime = a.titleFadeIn + a.titleStay + a.titleFadeOut;
                }
            }
            if (text.title.empty() && text.subtitle.empty())
                a.titleTime = 0;
        }
    }

    void Perlin::SpawnPreviewEntities() {
        const std::string list = PERLIN_ENV("PERLIN_PREVIEW_ENTITIES");
        std::vector<std::string> names;
        for (size_t start = 0; start <= list.size();) {
            const size_t comma = list.find(',', start);
            const std::string item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!item.empty())
                names.push_back(item);
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
        const float yawRad = glm::radians(camera.yaw);
        const glm::dvec3 forward(std::cos(yawRad), 0.0, std::sin(yawRad));
        const glm::dvec3 right(-forward.z, 0.0, forward.x);
        const double groundY = PERLIN_ENV("PERLIN_PREVIEW_ENTITY_Y") ? std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_Y")) : camera.y - 1.62;
        const double distance = PERLIN_ENV("PERLIN_PREVIEW_ENTITY_DIST") ? std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_DIST")) : 4.0;
        const double spacing = PERLIN_ENV("PERLIN_PREVIEW_ENTITY_SPACING") ? std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_SPACING")) : 1.8;
        for (size_t i = 0; i < names.size(); i++) {
            std::string type = names[i], variant;
            if (const size_t colon = type.find(':'); colon != std::string::npos) {
                variant = type.substr(colon + 1);
                type.resize(colon);
            }
            const glm::dvec3 p = glm::dvec3(camera.x, groundY, camera.z) + forward * distance +
                                 right * ((static_cast<double>(i) - (names.size() - 1) * 0.5) * spacing);
            const float yaw = camera.yaw - 90.0f + 180.0f + (PERLIN_ENV("PERLIN_PREVIEW_ENTITY_YAW") ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_YAW"))) : 0.0f);
            const uint64_t rid = 1000 + i;
            if (type == "item") {
                previewEntities.AddItem(rid, static_cast<int64_t>(rid), p, "minecraft:" + (variant.empty() ? std::string("diamond_sword") : variant),
                                        variant.find("stack") != std::string::npos ? 40 : 1, 0, 0, false);
            } else if (type == "player") {
                previewEntities.AddPlayer(rid, static_cast<int64_t>(rid), "preview" + std::to_string(i), "Steve",
                                          {p.x, p.y, p.z}, 0.0f, yaw, yaw, 0, "");
                auto skin = std::make_shared<entity::SkinData>();
                skin->serial = 100 + i;
                const bool slim = variant.find("alex") != std::string::npos;
                Texture t;
                if (ui::UiAssets::Instance().Image(slim ? "textures/entity/alex" : "textures/entity/steve", t)) {
                    skin->skinWidth = t.width;
                    skin->skinHeight = t.height;
                    skin->skinRgba.assign(t.pixels.begin(), t.pixels.end());
                }
                skin->slim = slim;
                if (variant.find("cape") != std::string::npos) {
                    skin->capeWidth = 64;
                    skin->capeHeight = 32;
                    skin->capeRgba.assign(64 * 32 * 4, 255);
                    for (int y = 0; y < 32; y++)
                        for (int x = 0; x < 64; x++) {
                            uint8_t* px = &skin->capeRgba[(static_cast<size_t>(y) * 64 + x) * 4];
                            const bool outer = x >= 1 && x < 11 && y >= 1 && y < 17;
                            const bool inner = x >= 12 && x < 22 && y >= 1 && y < 17;
                            px[0] = outer ? 30 : (inner ? 30 : 120);
                            px[1] = outer ? 200 : (inner ? 60 : 120);
                            px[2] = outer ? 40 : (inner ? 220 : 120);
                            px[3] = 255;
                        }
                }
                previewEntities.SetSkin("preview" + std::to_string(i), skin);
            } else {
                const uint64_t flags = variant.find("baby") != std::string::npos ? entity::flag::Baby : 0;
                // "namespace/name" previews a server pack's entity; anything else is a vanilla one.
                std::string id = "minecraft:" + type;
                if (const size_t slash = type.find('/'); slash != std::string::npos)
                    id = type.substr(0, slash) + ":" + type.substr(slash + 1);
                previewEntities.AddEntity(rid, static_cast<int64_t>(rid), id, p, PERLIN_ENV("PERLIN_PREVIEW_ENTITY_PITCH") ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_PITCH"))) : 0.0f, yaw, yaw, flags, 1.0f, 0, 0, 0);
            }
            if (type == "fishing_hook")
                previewEntities.SetOwner(rid, 1000);
            if (const char* held = PERLIN_ENV("PERLIN_PREVIEW_HELD"))
                previewEntities.SetHeld(rid, held);
            if (variant == "aiming") {
                previewEntities.SetHeld(rid, "minecraft:bow");
                previewEntities.SetTarget(rid, -7);
                previewEntities.SetFlagsExtended(rid, entity::flag::FacingTargetToRangeAttack);
            }
            if (const char* armor = PERLIN_ENV("PERLIN_PREVIEW_ARMOR")) {
                static const char* parts[4] = {"helmet", "chestplate", "leggings", "boots"};
                for (int i = 0; i < 4; i++)
                    previewEntities.SetArmor(rid, i, std::string("minecraft:") + armor + "_" + parts[i]);
            }
            if (const char* nt = PERLIN_ENV("PERLIN_PREVIEW_NAMETAG"))
                previewEntities.SetNameTag(rid, nt);
            if (PERLIN_ENV("PERLIN_PREVIEW_SNEAK"))
                previewEntities.SetFlags(rid, entity::flag::Sneaking);
            if (PERLIN_ENV("PERLIN_PREVIEW_SWIM"))
                previewEntities.SetFlags(rid, entity::flag::Swimming | entity::flag::Sprinting);
            if (PERLIN_ENV("PERLIN_PREVIEW_GLIDE")) {
                previewEntities.SetFlags(rid, entity::flag::Gliding);
                previewEntities.SetArmor(rid, 1, "minecraft:elytra");
            }
            if (PERLIN_ENV("PERLIN_PREVIEW_DEAD"))
                previewEntities.Die(rid);
            if (PERLIN_ENV("PERLIN_PREVIEW_ENTITY_WALK") || PERLIN_ENV("PERLIN_PREVIEW_ENTITY_HEAD"))
                previewWalk.push_back(rid);
        }
    }


    void Perlin::RenderThreadMain() {
        if (!renderer->Initialize(*platform, options)) {
            std::cout << "[render] renderer initialisation failed" << std::endl;
            platform->isRunning = false;
            return;
        }
        if (args.hidden) {
            renderer->SetForcedSize(1280, 720);
            renderer->SetExportTarget(true);
        }
        renderReady = true;
        StartSampleProfiler();
        levelRenderer = std::make_unique<LevelRenderer>(*renderer, options);
        handRenderer.Initialize(*renderer);
        entityRenderer.Initialize(*renderer);
        entityRenderer.SetItemRenderer(&handRenderer);

        camera.fov = options.fov;
        camera.nearPlane = 0.05f;
        camera.farPlane = std::max(static_cast<float>(options.renderDistance * 16 * 4),
                                   static_cast<float>(GraphicsSettings::From(options).cloudRange * 16));

        PERLIN_WEB_PERSIST Zoom zoom(options.zoom);
        PERLIN_WEB_PERSIST float scopeFov = 1.0f;
        PERLIN_WEB_PERSIST auto previous = Clock::now();
        PERLIN_WEB_PERSIST const auto startedAt = previous;
        PERLIN_WEB_PERSIST uint64_t frameCounter = 0;
        PERLIN_WEB_PERSIST DamageEffect damage;
        PERLIN_WEB_PERSIST uint32_t lastHurtCount = 0, lastHurtSerial = 0;
        PERLIN_WEB_PERSIST const BedrockSession* hurtSession = nullptr;
        PERLIN_WEB_PERSIST auto titleAt = previous;
        PERLIN_WEB_PERSIST auto animationAt = previous;
        PERLIN_WEB_PERSIST uint64_t animationStep = 0;

        PERLIN_WEB_PERSIST const bool preview = PERLIN_ENV("PERLIN_PREVIEW") != nullptr;
        PERLIN_WEB_PERSIST std::unique_ptr<Level> previewLevel;
        PERLIN_WEB_PERSIST double previewTicks = kNoonTick;
        PERLIN_WEB_PERSIST std::string previewShot;
        PERLIN_WEB_PERSIST double previewWait = 15.0;
        PERLIN_WEB_PERSIST int previewShotFrames = -1;
        PERLIN_WEB_PERSIST auto previewReadyAt = previous;
        PERLIN_WEB_PERSIST size_t previewLastMeshed = 0;
        if (preview) {
            if (const char* show = PERLIN_ENV("PERLIN_PREVIEW_HUD"))
                options.showHud = std::atoi(show) != 0;
            camera.x = 24.0f; camera.y = 74.0f; camera.z = -22.0f; camera.yaw = 100.0f; camera.pitch = -6.0f;
            if (const char* c = PERLIN_ENV("PERLIN_PREVIEW_CAM"))
                std::sscanf(c, "%f,%f,%f,%f,%f", &camera.x, &camera.y, &camera.z, &camera.yaw, &camera.pitch);
            if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_PERSPECTIVE"))
                camera.mode = static_cast<PerspectiveMode>(std::atoi(m) % 3);
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_TIME"))
                previewTicks = std::atof(t);
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_SHOT"))
                previewShot = t;
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_WAIT"))
                previewWait = std::atof(t);
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_SWING"))
                handRenderer.ForceSwingProgress(static_cast<float>(std::atof(t)));
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_SLOT"))
                previewSlot = std::clamp(std::atoi(t), 0, 8);
            if (PERLIN_ENV("PERLIN_PREVIEW_DEBUG"))
                debugOn = true;
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_CHAT")) {
                std::string all = t;
                size_t from = 0;
                while (from <= all.size()) {
                    size_t bar = all.find('|', from);
                    if (bar == std::string::npos)
                        bar = all.size();
                    chat.AddMessage(all.substr(from, bar - from), UINT64_MAX / 2);
                    from = bar + 1;
                }
            }
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_CHATOPEN")) {
                std::vector<ui::CommandInfo> fake;
                for (const char* n : {"time", "tp", "teleport", "tell", "title", "tag", "tellraw", "gamemode", "give"}) {
                    ui::CommandInfo c;
                    c.name = n;
                    c.overloads.push_back({ui::CommandParam{"target", {}, true, false}, ui::CommandParam{"mode", {"survival", "creative", "adventure"}, false, true}});
                    fake.push_back(std::move(c));
                }
                chat.SetCommands(std::move(fake));
                chat.Open(t, MsNow());
                chatOpenedAt = Clock::now() - std::chrono::seconds(5);
                input.SetTextMode(true);
            }
        }
        PERLIN_WEB_PERSIST int frames = 0;
        PERLIN_WEB_PERSIST bool appliedVsync = options.vsync;
        PERLIN_WEB_PERSIST auto handLightAt = previous;
        PERLIN_WEB_PERSIST int handSky = 15, handBlock = 0;

        PERLIN_WEB_PERSIST auto frameStep = [&]() {
            // previous-frame draws may still reference level textures until EndFrame
            if (levelUnloadPending) {
                levelUnloadPending = false;
                UnloadServerPacks(true);
            }
#ifdef __EMSCRIPTEN__
            platform->PumpFrame();
#endif
            auto frameStart = Clock::now();
            if ((frames & 15) == 0)
                i18n::Lang::Get().Configure(ui::UiAssets::Instance().VanillaRoot(), paths::DataDir(), options.language);
            // PERLIN_SHOT=<file.bmp>@<seconds>: capture our own framebuffer on any screen, then quit
            if (static const char* anyShot = PERLIN_ENV("PERLIN_SHOT"); anyShot) {
                static bool anyShotDone = false;
                static int anyShotFrames = 0;
                const std::string spec = anyShot;
                const size_t at = spec.rfind('@');
                const double when = at == std::string::npos ? 15.0 : std::atof(spec.c_str() + at + 1);
                if (!anyShotDone && std::chrono::duration<double>(frameStart - startedAt).count() > when) {
                    anyShotDone = true;
                    renderer->RequestScreenshot(spec.substr(0, at));
                } else if (anyShotDone && ++anyShotFrames > 8) {
                    platform->isRunning = false;
                }
            }
            float dt = std::chrono::duration<float>(frameStart - previous).count();
            previous = frameStart;
            fgActive = FrameGenMultiplier();
            if (dt <= 0.0f || dt > kMaxFrameDelta)
                dt = kDefaultFrameDelta;

            input.SetPadDeadzone(options.padDeadzone);
            PollControl();
            inputManager.Poll();
            static bool usingItem = false;
            bool earlyItemUse = false;
            bool blockedFoodUse = false;
            if (exportJob.active)
                ExportPreFrame(frameStart, dt);
            if (replayCtl.Playing())
                camera.mode = PerspectiveMode::FirstPerson;
            else if (inputManager.Pressed(Action::CyclePerspective))
                camera.CycleMode();
            zoom.SetConfig(options.zoom);
            PollHotReload();
            if (fontsDownloaded.exchange(false))
                ui::UiAssets::Instance().LoadFont(paths::DataDir());
            for (const std::string& dropped : platform->TakeDropped()) {
                namespace fsys = std::filesystem;
                std::error_code e;
                const fsys::path src(dropped);
                std::string ext = src.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                const bool onShaders = pauseShown && pauseView == 1 && optionsPage == 8;
                const bool onWardrobe = pauseShown && pauseView == 5 && ext == ".png";
                std::string lowerName = src.filename().string();
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                const bool dropIsCape = lowerName.find("cape") != std::string::npos;
                const char* sub = onWardrobe ? (dropIsCape ? "capes" : "skins") : ext == ".wasm" ? "mods" : onShaders ? "shaderpacks" : "texturepacks";
                if (ext == ".perlinreplay") {
                    ImportReplayFile(dropped);
                    continue;
                }
                if (!onWardrobe && ext != ".wasm" && ext != ".zip" && ext != ".mcpack" && !fsys::is_directory(src, e))
                    continue;
                const fsys::path destDir = fsys::path(paths::DataDir()) / sub;
                fsys::create_directories(destDir, e);
                fsys::copy(src, destDir / src.filename(), fsys::copy_options::overwrite_existing | fsys::copy_options::recursive, e);
                chat.AddMessage(e ? "Could not add " + src.filename().string() : "Added " + src.filename().string() + " to " + sub, hudAnim.ticks);
                packFoundAt = {};
                wardListedAt = {};
                optionsSignature.clear();
            }
            inputManager.SetServerLocks(sessionReady && session && session->GetState() == BedrockSession::State::InGame ? session->InputLocks() : 0u);
            const LookDelta look = zoom.Update(dt, inputManager.IsDown(Action::Zoom), inputManager.Scroll(),
                                               inputManager.Look());
            {
                auto& lookMods = mods::ModManager::Instance();
                bool lookTaken = false;
                if (lookMods.WantsEvent(abi::Event::Look)) {
                    abi::EventLook ev;
                    ev.dx = look.yaw;
                    ev.dy = look.pitch;
                    ev.dt = dt;
                    const glm::vec3 dir = camera.Direction();
                    ev.dirX = dir.x;
                    ev.dirY = dir.y;
                    ev.dirZ = dir.z;
                    const physics::State& ps = player.State();
                    ev.vx = ps.vx;
                    ev.vy = ps.vy;
                    ev.vz = ps.vz;
                    ev.keys = (inputManager.IsDown(Action::Forward) ? 1u : 0u) | (inputManager.IsDown(Action::Back) ? 2u : 0u) |
                              (inputManager.IsDown(Action::Left) ? 4u : 0u) | (inputManager.IsDown(Action::Right) ? 8u : 0u) |
                              (inputManager.IsDown(Action::Jump) ? 16u : 0u) | (inputManager.IsDown(Action::Sneak) ? 32u : 0u) |
                              (inputManager.IsDown(Action::Sprint) ? 64u : 0u);
                    ev.gliding = player.Gliding() && !ps.onGround && !ps.inWater;
                    ev.underwater = headUnderwater;
                    ev.thirdPerson = camera.mode != PerspectiveMode::FirstPerson;
                    ev.onGround = ps.onGround;
                    {
                        std::lock_guard lock(modAdv.mutex);
                        modAdv.lookTaken = false;
                    }
                    static float glideLostFor = 0.0f;
                    glideLostFor = ev.gliding ? 0.0f : glideLostFor + dt;
                    abi::Writer lw;
                    lw.write(ev);
                    lookMods.Dispatch(abi::Event::Look, lw, 20.0);
                    if (glideLostFor > 0.35f) {
                        std::lock_guard lock(modAdv.mutex);
                        modAdv.cameraUntil = {};
                        modAdv.poseUntil = {};
                        modAdv.lookTaken = false;
                    }
                    std::lock_guard lock(modAdv.mutex);
                    if (modAdv.lookTaken) {
                        lookTaken = true;
                        const glm::dvec3 d = modAdv.lookDir;
                        camera.yaw = static_cast<float>(glm::degrees(std::atan2(d.z, d.x)));
                        camera.pitch = std::clamp(static_cast<float>(glm::degrees(std::asin(std::clamp(d.y, -1.0, 1.0)))), -89.9f, 89.9f);
                        modAdv.lookTaken = false;
                    }
                }
                if (!lookTaken)
                    camera.Rotate(look.yaw, look.pitch);
            }
            {
                const float target = handRenderer.Scoping() && camera.mode == PerspectiveMode::FirstPerson ? 0.1f : 1.0f;
                scopeFov += (target - scopeFov) * (1.0f - std::pow(0.5f, dt * 20.0f));
            }
            if (sessionReady && session) {
                CameraEffects& fx = session->Camera();
                fx.Tick({glm::vec3(camera.x, camera.y, camera.z), camera.yaw - 90.0f, -camera.pitch});
                if (fx.PoseActive()) {
                    const CameraPose pose = fx.Pose();
                    camera.x = pose.position.x;
                    camera.y = pose.position.y;
                    camera.z = pose.position.z;
                    camera.yaw = pose.yaw + 90.0f;
                    camera.pitch = glm::clamp(-pose.pitch, -89.0f, 89.0f);
                }
            }
            if (sessionReady && session) {
                const uint32_t hurts = session->HurtCount();
                const auto source = session->LatestHurtSource();
                if (hurtSession != session.get()) {
                    hurtSession = session.get();
                    lastHurtCount = hurts;
                    lastHurtSerial = source.serial;
                }
                const auto bearing = [&] {
                    if (!source.known)
                        return static_cast<float>(std::rand() % 2) * 180.0f;
                    const float javaYaw = camera.yaw - 90.0f;
                    return static_cast<float>(std::atan2(source.dz, source.dx) * 57.29577951308232) - javaYaw;
                };
                if (hurts != lastHurtCount) {
                    lastHurtCount = hurts;
                    lastHurtSerial = source.serial;
                    damage.Hurt(bearing());
                } else if (source.serial != lastHurtSerial) {
                    lastHurtSerial = source.serial;
                    if (source.known)
                        damage.Redirect(bearing());
                }
            } else {
                damage = DamageEffect();
            }

            int width = 0, height = 0;
            FramebufferSizeEffective(width, height);
            if (width > 0 && height > 0)
                camera.aspect = static_cast<float>(width) / static_cast<float>(height);

            PumpReplay(dt);
            BedrockSession* live = sessionReady && session && !session->Transferring() ? session.get() : nullptr;
            static std::vector<entity::Entity> frameEntities;
            if (live) {
                const physics::State& viewerState = player.State();
                live->Entities().SetViewer(live->GetRuntimeId(), {viewerState.x, viewerState.y, viewerState.z});
                live->Entities().Snapshot(frameEntities);
            } else {
                previewEntities.Snapshot(frameEntities);
            }
            PumpMods(live, live ? &live->NetLevel() : previewLevel.get(), hud.get(), dt);
            if (live) {
                live->Update();
                live->PumpRequests(256);
                {
                    const int64_t rideUid = live->RidingUid();
                    std::vector<physics::Box> solid;
                    entity::Entity seatVehicle;
                    bool seated = false;
                    if (rideUid != 0 && live->Entities().FindUnique(rideUid, seatVehicle)) {
                        const glm::dvec3 seat = entityRenderer.SeatPosition(seatVehicle);
                        player.SetRidePosition(true, seat.x, seat.y, seat.z);
                        seated = true;
                    }
                    {
                        static bool wasSeated = false;
                        static entity::Entity lastSeat;
                        if (seated) {
                            wasSeated = true;
                            lastSeat = seatVehicle;
                        } else if (wasSeated) {
                            wasSeated = false;
                            if (lastSeat.type == "minecraft:cushion")
                                player.ModTeleport(lastSeat.pos.x, lastSeat.pos.y + 0.26, lastSeat.pos.z, false);
                        }
                    }
                    if (!seated)
                        player.SetRidePosition(false, 0, 0, 0);
                    for (const entity::Entity& e : frameEntities) {
                        if (e.type != "minecraft:cushion" || (seated && e.uniqueId == rideUid))
                            continue;
                        float w = 0.999f, h = 0.249f;
                        entityRenderer.CollisionBoxOf(e.type, w, h);
                        solid.push_back({e.pos.x - w / 2, e.pos.y, e.pos.z - w / 2, e.pos.x + w / 2, e.pos.y + h, e.pos.z + w / 2});
                    }
                    SetEntityCollisionBoxes(std::move(solid));
                }
                player.SetToggleModes(options.sprintMode == "toggle", options.sneakMode == "toggle");
                hudState.moveStatus.clear();
                {
                    const auto& held = hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)];
                    const auto& name = held.name;
                    const bool use = input.Captured() && inputManager.IsDown(Action::Use) && HasAmmo(hudState, live, name);
                    const bool drink = name == "minecraft:potion" || name == "minecraft:milk_bucket" ||
                                       name == "minecraft:honey_bottle" || name == "minecraft:ominous_bottle";
                    const bool always = drink || name == "minecraft:golden_apple" || name == "minecraft:enchanted_golden_apple" ||
                                        name == "minecraft:chorus_fruit" || name == "minecraft:suspicious_stew";
                    const bool eating = hudState.Survival() && HandRenderer::IsFood(name) && (always || hudState.food < 20);
                    blockedFoodUse = HandRenderer::IsFood(name) && !eating;
                    bool aimInteracts = false;
                    if (gAim.kind == AimHit::Kind::Block) {
                        bool loaded = false;
                        const uint16_t id = live->NetLevel().GetBlock(gAim.bx, gAim.by, gAim.bz, loaded);
                        aimInteracts = !loaded || (blockuse::IsInteractive(BlockPalette::Instance().Name(id)) && !player.Sneaking());
                    } else if (gAim.kind == AimHit::Kind::Entity) {
                        entity::Entity target;
                        aimInteracts = !live->Entities().Find(gAim.entity, target) || IsInteractable(target.type);
                    }
                    if (use && eating && !aimInteracts && inputManager.Pressed(Action::Use) && !DispatchModInteract(abi::InteractKind::UseItem, 0, 0, 0, 0, 0)) {
                        const float eye[3] = {static_cast<float>(camera.x), static_cast<float>(camera.y), static_cast<float>(camera.z)};
                        earlyItemUse = live->SendUseItemInAir(eye, true);
                        usingItem = earlyItemUse;
                    }
                    if (!use && usingItem) {
                        const float eye[3] = {static_cast<float>(camera.x), static_cast<float>(camera.y), static_cast<float>(camera.z)};
                        live->SendReleaseItem(eye);
                        usingItem = false;
                    }
                    const uint32_t useTicks = eating ? (name == "minecraft:dried_kelp" ? 16 :
                                                       name == "minecraft:honey_bottle" ? 40 : 32) : 0;
                    player.SetItemSlow(use && (eating || name == "minecraft:bow" || name == "minecraft:trident" ||
                                              name == "minecraft:spyglass"), useTicks, inputManager.Pressed(Action::Use));
                }
                if (!replayCtl.Playing())
                    player.Update(*live, inputManager, camera, levelRenderer->Blocks(), dt);
            }
            float movementFov = player.FovModifier();
            if (preview && !live) {
                if (const char* movement = PERLIN_ENV("PERLIN_PREVIEW_FOV_MOVEMENT")) {
                    float speed = 0.1f, walk = 0.1f;
                    int fly = 0, slowness = -1;
                    if (std::sscanf(movement, "%f,%f,%d,%d", &speed, &walk, &fly, &slowness) >= 2)
                        movementFov = MovementFovTarget(speed, walk, fly != 0, slowness);
                }
            }
            camera.fov = zoom.Fov(options.fov * (options.fovEffects ? movementFov : 1.0f)) * scopeFov;
            if (sessionReady && session)
                camera.fov = session->Camera().Fov(camera.fov);
            if (const char* g = PERLIN_ENV("PERLIN_GLASS"))
                options.glass = g;
            {
                if (options.renderer != runningRenderer && optionsDrag < 0 && renderer) {
                    runningRenderer = options.renderer;
                    options.Save();
                    if (!renderer->RequestRendererSwitch(options.renderer))
                        chat.AddMessage("Renderer cannot be changed right now", hudAnim.ticks);
                }
                if (renderer && !renderer->RendererSwitchPending()) {
                    // a switch that fell back to the old api must not leave the options claiming the new one
                    const std::string active = renderer->ActiveRendererId();
                    if (!active.empty() && options.renderer != "auto" && options.renderer != active && options.renderer == runningRenderer) {
                        chat.AddMessage("Could not switch renderer; staying on " + renderer->RendererName(), hudAnim.ticks);
                        options.renderer = runningRenderer = active;
                        options.Save();
                    }
                }
            }
            {
                static auto lastTrim = std::chrono::steady_clock::now();
                const auto nowTrim = std::chrono::steady_clock::now();
                if (nowTrim - lastTrim > std::chrono::seconds(45)) {
                    lastTrim = nowTrim;
                    std::thread([] { TrimMemory(false); }).detach();
                }
            }
            levelRenderer->SetBetterGrass(options.betterGrass ? 1 : 0);
            levelRenderer->SetGlassMode(options.glass == "connected" ? 1 : options.glass == "connectedClear" ? 2 : options.glass == "clear" ? 3 : 0);
            levelRenderer->ReloadAssetsIfNeeded();
            PollJavaPack(live);

            if (live)
                for (const std::string& line : live->TakeChat())
                    if (!FilterIncomingChat(line))
                        chat.AddMessage(line, hudAnim.ticks);
            MergePluginCommands(live);
            if (live) {
                if (chat.IsOpen() && Clock::now() - playersAt > std::chrono::seconds(1)) {
                    playersAt = Clock::now();
                    std::vector<std::string> names;
                    for (const auto& p : live->PlayerList())
                        names.push_back(p.name);
                    chat.SetPlayers(std::move(names));
                }
            }
            if ((live || preview) && !replayCtl.Playing()) {
                if (!chat.IsOpen()) {
                    if (inputManager.Pressed(Action::Chat) || inputManager.Pressed(Action::Command)) {
                        chat.Open(inputManager.Pressed(Action::Chat) ? "" : "/", MsNow());
                        chatOpenedAt = Clock::now();
                        input.SetTextMode(true);
                    }
                } else {
                    auto typed = inputManager.TakeTyped();
                    if (Clock::now() - chatOpenedAt < std::chrono::milliseconds(150))
                        typed.erase(std::remove_if(typed.begin(), typed.end(),
                                                   [](const InputManager::Typed& t) {
                                                       return t.character && (t.key == 't' || t.key == 'T' || t.key == '/');
                                                   }),
                                    typed.end());
                    const ui::ChatLog::Result r = chat.Type(typed, inputManager.Scroll());
                    if (r.send && InterceptOutgoing(live, r.text)) {
                    } else if (r.send) {
                        if (live && r.text[0] == '/')
                            live->SendCommand(r.text.substr(1));
                        else if (live)
                            live->SendChat(r.text);
                        else
                            chat.AddMessage(i18n::Tr("chat.type.text", {i18n::Tr("perlin.chat.preview_player"), r.text}), hudAnim.ticks);
                    }
                    if (r.close) {
                        chat.Close();
                        input.SetTextMode(false);
                    }
                }
            } else if (chat.IsOpen()) {
                chat.Close();
                input.SetTextMode(false);
            }

            if (inputManager.Pressed(Action::ToggleHud))
                options.showHud = !options.showHud;
            if ((live || preview) && input.Captured() && !replayCtl.Playing()) {
                int newSlot = -1;
                for (int i = 0; i < 9; i++)
                    if (inputManager.Pressed(static_cast<Action>(static_cast<int>(Action::Hotbar1) + i)))
                        newSlot = i;
                static float scrollAccum = 0.0f;
                if (!inputManager.IsDown(Action::Zoom)) {
                    scrollAccum += inputManager.Scroll();
                    while (scrollAccum >= 1.0f || scrollAccum <= -1.0f) {
                        const int step = scrollAccum > 0.0f ? 1 : -1;
                        scrollAccum -= static_cast<float>(step);
                        const int from = newSlot >= 0 ? newSlot : hudState.selectedSlot;
                        newSlot = ((from - step) % 9 + 9) % 9;
                    }
                }
                if (newSlot >= 0 && newSlot != hudState.selectedSlot) {
                    hudState.selectedSlot = newSlot;
                    if (live)
                        live->SelectHotbarSlot(newSlot);
                    else
                        previewSlot = newSlot;
                }
                const bool spectator = hudState.mode == ui::GameMode::Spectator;
                const bool creative = hudState.mode == ui::GameMode::Creative;
                const ui::HudItem& held = hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)];
                if (levelRenderer) {
                    int technical = 0;
                    if (creative && !held.Empty()) {
                        const std::string heldShort = held.name.rfind("minecraft:", 0) == 0 ? held.name.substr(10) : held.name;
                        if (heldShort == "barrier")
                            technical = BlockLibrary::kBarrier;
                        else if (heldShort.rfind("light_block", 0) == 0)
                            technical = BlockLibrary::kLightBlock;
                        else if (heldShort == "structure_void")
                            technical = BlockLibrary::kStructureVoid;
                    }
                    if (technical != levelRenderer->Blocks().TechnicalVisible()) {
                        levelRenderer->Blocks().SetTechnicalVisible(technical);
                        MesherInvalidateCaches();
                        if (live)
                            live->NetLevel().MarkAllDirty();
                    }
                }

                if (replayCtl.Playing() && live) {
                    struct ReplayMine { bool active = false; int x = 0, y = 0, z = 0, face = 0; float progress = 0.0f; int hits = 0; };
                    static ReplayMine rm;
                    std::vector<BedrockSession::ReplayClientEvent> events;
                    live->TakeReplayClientEvents(events);
                    if (replayCtl.CatchingUp()) {
                        events.clear();
                        rm = ReplayMine{};
                        gLocalCrack.active = false;
                    }
                    const auto heldName = [&]() {
                        const auto inv = live->GetInventory();
                        return inv.hotbar[std::clamp(inv.selected, 0, 8)].name;
                    };
                    const auto finishMine = [&](bool broke) {
                        if (broke && rm.active) {
                            bool loadedHere = false;
                            const uint16_t oldId = live->NetLevel().GetBlock(rm.x, rm.y, rm.z, loadedHere);
                            if (loadedHere && oldId != 0) {
                                const std::string nm = BlockPalette::Instance().Name(oldId);
                                if (!nm.empty())
                                    sounds.PlayBlockEvent(nm, "break", glm::dvec3(rm.x + 0.5, rm.y + 0.5, rm.z + 0.5));
                                particles.Destroy(rm.x, rm.y, rm.z, oldId);
                                lastLocalBreak = {rm.x, rm.y, rm.z, std::chrono::steady_clock::now()};
                            }
                        }
                        rm = ReplayMine{};
                        gLocalCrack.active = false;
                    };
                    for (const auto& ev : events) {
                        using Kind = BedrockSession::ReplayClientEvent::Kind;
                        if (ev.kind == Kind::Swing) {
                            handRenderer.Swing(options.hand);
                            gSelfSwing++;
                        } else if (ev.kind == Kind::Hotbar) {
                            live->ReplaySelectSlot(ev.slot);
                        } else {
                            switch (ev.action) {
                            case 0:
                                rm = ReplayMine{true, ev.x, ev.y, ev.z, ev.face, 0.0f, 0};
                                break;
                            case 18:
                            case 27: {
                                if (!rm.active || rm.x != ev.x || rm.y != ev.y || rm.z != ev.z)
                                    rm = ReplayMine{true, ev.x, ev.y, ev.z, ev.face, 0.0f, 0};
                                bool loaded = false;
                                const uint16_t id = live->NetLevel().GetBlock(rm.x, rm.y, rm.z, loaded);
                                const std::string nm = loaded ? BlockPalette::Instance().Name(id) : std::string();
                                float step = creative ? 1.0f : 0.0f;
                                if (!creative && !nm.empty() && nm != "minecraft:air") {
                                    destroy::Conditions c;
                                    c.heldItem = heldName();
                                    c.onGround = true;
                                    c.inWater = false;
                                    step = destroy::ProgressPerTick(destroy::Lookup(nm), c);
                                }
                                rm.progress += step;
                                if (!creative && loaded && id != 0)
                                    particles.Crack(rm.x, rm.y, rm.z, rm.face, id);
                                if (!creative && ++rm.hits % 4 == 0 && !nm.empty())
                                    sounds.PlayBlockEvent(nm, "hit", glm::dvec3(rm.x + 0.5, rm.y + 0.5, rm.z + 0.5));
                                gLocalCrack = {rm.progress > 0.0f && rm.progress < 1.0f, rm.x, rm.y, rm.z,
                                               std::clamp(static_cast<int>(rm.progress * 10.0f), 0, 9),
                                               std::clamp(rm.progress, 0.0f, 1.0f)};
                                break;
                            }
                            case 26:
                            case 2:
                                finishMine(true);
                                break;
                            case 1:
                                finishMine(false);
                                break;
                            default:
                                break;
                            }
                        }
                    }
                }

                gAim = AimHit{};
                if ((live || previewLevel) && !spectator && levelRenderer) {
                    const std::vector<entity::Entity>& aimEntities = frameEntities;
                    gAim = Raycast(live ? live->NetLevel() : *previewLevel, &levelRenderer->Blocks(), aimEntities,
                                   [&](const entity::Entity& e) {
                                       const auto hb = entityRenderer.HitBox(e);
                                       return glm::vec2(hb.first, hb.second);
                                   },
                                   {camera.x, camera.y, camera.z}, glm::dvec3(camera.Direction()),
                                   PickRange(creative, 1), EntityPickRange(creative));
                }
                if (PERLIN_ENV("PERLIN_AIM_DEBUG"))
                    std::cout << "[aim] kind " << static_cast<int>(gAim.kind) << " block " << gAim.bx << "," << gAim.by << "," << gAim.bz
                              << " face " << gAim.face << " dist " << gAim.distance << std::endl;
                const float eyeF[3] = {camera.x, camera.y, camera.z};
                const auto clickOf = [](const AimHit& aim, float out[3]) {
                    out[0] = static_cast<float>(aim.point.x - aim.bx);
                    out[1] = static_cast<float>(aim.point.y - aim.by);
                    out[2] = static_cast<float>(aim.point.z - aim.bz);
                };
                if (live && live->TakeBlockNamesLearned()) {
                    MesherInvalidateCaches();
                    live->NetLevel().MarkAllDirty();
                }
                static double breakClock = 0, useClock = 0, swingClock = 0;
                bool& breaking = gBreak.breaking;
                int& bkx = gBreak.x; int& bky = gBreak.y; int& bkz = gBreak.z; int& bkFace = gBreak.face;
                float& breakProgress = gBreak.progress;
                static int breakDelayTicks = 0;
                static uint64_t creativeBreakReadyAt = 0;
                static int hitTicks = 0;
                breakClock += dt;
                useClock += dt;
                swingClock += dt;

                auto stopBreaking = [&]() {
                    if (breaking && live) {
                        live->SendPlayerAuthInputBlockAction(1  , bkx, bky, bkz, bkFace);
                        live->ClearCrack(bkx, bky, bkz);
                    }
                    breaking = false;
                    breakProgress = 0.0f;
                    gLocalCrack.active = false;
                };

                if (spectator) {
                    gHeldBuild.Stop();
                    if (live) live->EndItemUseOn();
                    stopBreaking();
                } else {
                    const bool attackHeld = inputManager.IsDown(Action::Attack);
                    if (inputManager.Pressed(Action::Attack)) {
                        handRenderer.Attack(options.hand, hudState.Survival());
                        gSelfSwing++;
                        swingClock = 0;
                        if (live) {
                            live->SendSwingArm(gAim.kind == AimHit::Kind::Block ? "mine" : "attack");
                            if (gAim.kind == AimHit::Kind::None)
                                live->QueueInputFlag(InputFlag::MissedSwing);
                            entity::Entity attacked;
                            const bool mobBlocked = live->Adventure().noPvm && live->Entities().Find(gAim.entity, attacked) && !attacked.isPlayer;
                            if (gAim.kind == AimHit::Kind::Entity && !mobBlocked &&
                                !DispatchModInteract(abi::InteractKind::Attack, 0, 0, 0, 0, gAim.entity)) {
                                // click position: where the ray meets the target's hitbox, relative to its base coordinate, not world
                                const glm::dvec3 feet((gAim.boxLo.x + gAim.boxHi.x) * 0.5, gAim.boxLo.y, (gAim.boxLo.z + gAim.boxHi.z) * 0.5);
                                const glm::vec3 offset = EntityClickOffset({camera.x, camera.y, camera.z}, glm::dvec3(camera.Direction()), feet,
                                                                           (gAim.boxHi.x - gAim.boxLo.x) * 0.5, gAim.boxHi.y - gAim.boxLo.y,
                                                                           (gAim.boxLo + gAim.boxHi) * 0.5);
                                float click[3] = {offset.x, offset.y, offset.z};
                                live->SendAttackEntity(gAim.entity, eyeF, click);
                                // vanilla predicts the crit burst locally: falling, airborne, out of fluids, not flying
                                const physics::State& cst = player.State();
                                if (!cst.onGround && cst.vy < 0.0 && !cst.inWater && !cst.inLava && !player.Flying()) {
                                    const glm::dvec3 centre((gAim.boxLo.x + gAim.boxHi.x) * 0.5,
                                                            gAim.boxLo.y + (gAim.boxHi.y - gAim.boxLo.y) * 0.6,
                                                            (gAim.boxLo.z + gAim.boxHi.z) * 0.5);
                                    for (int ci = 0; ci < 12; ++ci)
                                        particles.Spawn(particle::Kind::Crit, centre);
                                }
                            }
                        }
                    }
                    const bool creativeSword = creative && held.name.ends_with("_sword");
                    if (live && attackHeld && !creativeSword && !live->Adventure().immutableWorld &&
                        (!creative || MsNow() >= creativeBreakReadyAt) && gAim.kind == AimHit::Kind::Block) {
                        const bool newTarget = !breaking || gAim.bx != bkx || gAim.by != bky || gAim.bz != bkz;
                        if (newTarget && breakDelayTicks > 0 && !breaking) {
                        } else if (newTarget && DispatchModInteract(abi::InteractKind::StartBreak, gAim.bx, gAim.by, gAim.bz, gAim.face, 0)) {
                        } else if (newTarget) {
                            stopBreaking();
                            static const int faceDx[6] = {0, 0, 0, 0, -1, 1};
                            static const int faceDy[6] = {-1, 1, 0, 0, 0, 0};
                            static const int faceDz[6] = {0, 0, -1, 1, 0, 0};
                            const int fx = gAim.bx + faceDx[gAim.face], fy = gAim.by + faceDy[gAim.face], fz = gAim.bz + faceDz[gAim.face];
                            bool fireLoaded = false;
                            const uint16_t fireId = live->NetLevel().GetBlock(fx, fy, fz, fireLoaded);
                            const std::string fireName = fireLoaded ? BlockPalette::Instance().Name(fireId) : std::string();
                            if (fireName == "minecraft:fire" || fireName == "minecraft:soul_fire") {
                                live->SendPlayerAuthInputBlockAction(0, gAim.bx, gAim.by, gAim.bz, gAim.face);
                                live->SendPlayerAuthInputBlockAction(1, gAim.bx, gAim.by, gAim.bz, gAim.face);
                                live->NetLevel().SetBlock(fx, fy, fz, 0);
                                sounds.PlayBlockEvent(fireName, "break", glm::dvec3(fx + 0.5, fy + 0.5, fz + 0.5));
                                VibrateBlockBroken();
                                breakDelayTicks = 5;
                                if (creative) creativeBreakReadyAt = MsNow() + 250;
                            } else {
                                bkx = gAim.bx; bky = gAim.by; bkz = gAim.bz; bkFace = gAim.face;
                                breaking = true;
                                breakProgress = 0.0f;
                                breakClock = creative ? 0.05 : 0.0;
                                hitTicks = 0;
                                live->SendPlayerAuthInputBlockAction(0  , bkx, bky, bkz, bkFace);
                            }
                        }
                        if (breaking) {
                            bool present = false;
                            const uint16_t cur = live->NetLevel().GetBlock(bkx, bky, bkz, present);
                            if (present && cur == 0)
                                stopBreaking();
                        }
                        while (breaking && breakClock >= 0.05) {
                            breakClock -= 0.05;
                            if (breakDelayTicks > 0) {
                                breakDelayTicks--;
                                continue;
                            }
                            { handRenderer.Swing(options.hand); gSelfSwing++; }
                            bool loaded = false;
                            const uint16_t id = live->NetLevel().GetBlock(bkx, bky, bkz, loaded);
                            const std::string blockName = loaded ? BlockPalette::Instance().Name(id) : std::string();
                            float step = 0.0f;
                            if (creative) {
                                step = 1.0f;
                            } else if (!blockName.empty() && blockName != "minecraft:air") {
                                destroy::Conditions c;
                                c.heldItem = held.Empty() ? std::string() : held.name;
                                c.onGround = player.State().onGround;
                                c.inWater = hudState.inWater;
                                step = destroy::ProgressPerTick(destroy::Lookup(blockName), c);
                            }
                            if (!live->ServerAuthoritativeBlockBreaking() && !creative)
                                live->SendPlayerAuthInputBlockAction(18  , bkx, bky, bkz, bkFace);
                            else
                                live->SendPlayerAuthInputBlockAction(27  , bkx, bky, bkz, bkFace);
                            breakProgress += step;
                            if (!creative) {
                                bool loadedHere = false;
                                const uint16_t hitId = live->NetLevel().GetBlock(bkx, bky, bkz, loadedHere);
                                if (loadedHere && hitId != 0)
                                    particles.Crack(bkx, bky, bkz, bkFace, hitId);
                            }
                            if (!creative && ++hitTicks % 4 == 0 && !blockName.empty())
                                sounds.PlayBlockEvent(blockName, "hit", glm::dvec3(bkx + 0.5, bky + 0.5, bkz + 0.5));
                            if (breakProgress >= 1.0f) {
                                live->SendPlayerAuthInputBlockAction(26  , bkx, bky, bkz, bkFace);
                                if (!live->ServerAuthoritativeBlockBreaking() && !creative)
                                    live->SendPlayerAuthInputBlockAction(2  , bkx, bky, bkz, bkFace);
                                if (!blockName.empty())
                                    sounds.PlayBlockEvent(blockName, "break", glm::dvec3(bkx + 0.5, bky + 0.5, bkz + 0.5));
                                VibrateBlockBroken();
                                {
                                    bool loadedHere = false;
                                    const uint16_t oldId = live->NetLevel().GetBlock(bkx, bky, bkz, loadedHere);
                                    if (loadedHere && oldId != 0)
                                        particles.Destroy(bkx, bky, bkz, oldId);
                                    lastLocalBreak = {bkx, bky, bkz, std::chrono::steady_clock::now()};
                                }
                                live->NetLevel().SetBlock(bkx, bky, bkz, 0);
                                std::vector<std::pair<int, uint16_t>> clearedAbove;
                                for (int up = 1; up <= 4; up++) {
                                    bool aboveLoaded = false;
                                    const uint16_t aboveId = live->NetLevel().GetBlock(bkx, bky + up, bkz, aboveLoaded);
                                    const std::string aboveName = aboveLoaded ? BlockPalette::Instance().Name(aboveId) : std::string();
                                    if (aboveName.empty() || !destroy::NeedsSupportBelow(aboveName))
                                        break;
                                    live->NetLevel().SetBlock(bkx, bky + up, bkz, 0);
                                    clearedAbove.emplace_back(bky + up, aboveId);
                                    if (up == 1 && !destroy::BreaksWholeStack(aboveName))
                                        break;
                                }
                                if (!clearedAbove.empty())
                                    live->NoteBrokenSupport(bkx, bky, bkz, std::move(clearedAbove));
                                breaking = false;
                                breakProgress = 0.0f;
                                gLocalCrack.active = false;
                                live->ClearCrack(bkx, bky, bkz);
                                { handRenderer.Swing(options.hand); gSelfSwing++; }
                                breakDelayTicks = 5;
                                if (creative) creativeBreakReadyAt = MsNow() + 250;
                            } else {
                                gLocalCrack = {breakProgress > 0.0f, bkx, bky, bkz, std::clamp(static_cast<int>(breakProgress * 10.0f), 0, 9), std::clamp(breakProgress, 0.0f, 1.0f)};
                            }
                        }
                        if (swingClock > 0.3) {
                            swingClock = 0;
                            live->SendSwingArm("mine");
                        }
                    } else if (breaking) {
                        stopBreaking();
                    } else {
                        gLocalCrack.active = false;
                        if (!attackHeld)
                            breakDelayTicks = 0;
                    }
                    if (breakDelayTicks > 0 && breakClock >= 0.05 && !breaking) {
                        breakClock -= 0.05;
                        breakDelayTicks--;
                    }

                    // attack/pick ray unchanged; with bedrock bridging, air beside your footing places against the block under you
                    // (reach-around), held use keeps building from the last placed block (heldbuild::HeldBuild); a real crosshair hit wins
                    AimHit useAim = gAim;
                    const bool bedrockBuild = options.bedrockBridging && !held.Empty() && held.blockRuntimeId != 0;
                    const glm::vec3 buildEye(camera.x, camera.y, camera.z);
                    const glm::vec3 buildVelocity = glm::vec3(static_cast<float>(player.State().vx), static_cast<float>(player.State().vy),
                                                              static_cast<float>(player.State().vz)) * 20.0f;
                    static const int kFaceNormal[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
                    const auto aimAt = [&](glm::ivec3 support, int face, glm::dvec3 point) {
                        useAim.kind = AimHit::Kind::Block;
                        useAim.bx = support.x;
                        useAim.by = support.y;
                        useAim.bz = support.z;
                        useAim.face = face;
                        useAim.point = point;
                    };
                    if (bedrockBuild && live && levelRenderer && gAim.kind == AimHit::Kind::None) {
                        Level& level = live->NetLevel();
                        const LevelCollision collision(level, &levelRenderer->Blocks());
                        auto& palette = BlockPalette::Instance();
                        const auto& shape = LevelCollision::ShapeOf(palette.InternRuntime(static_cast<int32_t>(held.blockRuntimeId)));
                        const std::vector<physics::Box> heldShape(shape.boxes.begin(), shape.boxes.end());
                        bridging::Request request;
                        request.eye = {camera.x, camera.y, camera.z};
                        request.direction = glm::dvec3(camera.Direction());
                        request.body = physics::BoundingBox(player.State(), player.Sneaking());
                        request.sneaking = player.Sneaking();
                        request.reach = PickRange(creative, 1);
                        request.placedShape = heldShape;
                        request.horizontalSlab = held.name.find("slab") != std::string::npos && heldShape.size() == 1 &&
                                                 std::abs(heldShape[0].y1 - heldShape[0].y0 - 0.5) < 1e-5;
                        const auto around = bridging::FindTarget(request, [&](glm::ivec3 position) {
                            bridging::Cell cell;
                            bool loaded = false;
                            const uint16_t id = level.GetBlock(position.x, position.y, position.z, loaded);
                            const std::string& name = palette.Name(id);
                            cell.loaded = loaded;
                            cell.replaceable = blockuse::IsReplaceableCell(loaded, id, name);
                            cell.interactive = blockuse::IsInteractive(name);
                            if (loaded && !cell.replaceable) {
                                collision.Selection(position.x, position.y, position.z, cell.collision);
                            }
                            return cell;
                        });
                        if (around)
                            aimAt(around->anchor, around->face, around->point);
                    }
                    if (bedrockBuild && inputManager.Pressed(Action::Use))
                        gHeldBuild.Start(MsNow(), player.TickCount());
                    // Held repeats follow the game's build rules (heldbuild::HeldBuild): direct crosshair hits only, the
                    // repeat delay, and the build line. Reach-around is for fresh presses.
                    bool heldRepeat = false;
                    if (bedrockBuild && inputManager.IsDown(Action::Use) && !inputManager.Pressed(Action::Use)) {
                        heldbuild::Hit hit;
                        hit.block = gAim.kind == AimHit::Kind::Block;
                        hit.target = {{gAim.bx, gAim.by, gAim.bz}, gAim.face};
                        hit.point = hit.block ? glm::vec3(gAim.point) : buildEye + glm::normalize(camera.Direction()) * static_cast<float>(PickRange(creative, 1));
                        const glm::vec3 posDelta = buildVelocity / 20.0f;
                        const auto chosen = gHeldBuild.Next(hit, buildEye, posDelta, player.Sneaking(), MsNow(), player.TickCount(), [&](glm::ivec3 p) {
                            if (!live)
                                return false;
                            bool loaded = false;
                            const uint16_t id = live->NetLevel().GetBlock(p.x, p.y, p.z, loaded);
                            return blockuse::IsReplaceableCell(loaded, id, BlockPalette::Instance().Name(id));
                        });
                        if (!chosen) {
                            useAim.kind = AimHit::Kind::None;
                        } else {
                            heldRepeat = true;
                            if (!hit.block || chosen->support != hit.target.support || chosen->face != hit.target.face)
                                aimAt(chosen->support, chosen->face,
                                      glm::dvec3(chosen->support) + glm::dvec3(0.5) +
                                          glm::dvec3(kFaceNormal[chosen->face][0], kFaceNormal[chosen->face][1], kFaceNormal[chosen->face][2]) * 0.5);
                        }
                    }
                    const bool ammoBlocked = !HasAmmo(hudState, live, held.name);
                    const bool useHeld = inputManager.IsDown(Action::Use) && !ammoBlocked;
                    const bool usePressed = inputManager.Pressed(Action::Use) && !ammoBlocked;
                    const bool holdsItem = !held.Empty();
                    const bool holdsBlock = holdsItem && held.blockRuntimeId != 0;
                    blockuse::Use use = blockuse::Use::Nothing;
                    int dest[3] = {0, 0, 0};
                    if (useAim.kind == AimHit::Kind::Block && (live || previewLevel)) {
                        Level& lv = live ? live->NetLevel() : *previewLevel;
                        const auto nameAt = [&](int x, int y, int z) {
                            bool loaded = false;
                            const uint16_t id = lv.GetBlock(x, y, z, loaded);
                            return loaded ? BlockPalette::Instance().Name(id) : std::string();
                        };
                        int clicked[3] = {useAim.bx, useAim.by, useAim.bz};
                        static const int kOff[6][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
                        const int f = std::clamp(useAim.face, 0, 5);
                        const blockuse::Result r = blockuse::Classify(
                            nameAt(clicked[0], clicked[1], clicked[2]),
                            nameAt(clicked[0] + kOff[f][0], clicked[1] + kOff[f][1], clicked[2] + kOff[f][2]), clicked, useAim.face,
                            holdsItem, holdsBlock, player.Sneaking());
                        use = r.use;
                        if (use == blockuse::Use::Place && live && live->Adventure().immutableWorld)
                            use = blockuse::Use::Nothing;
                        std::copy(r.dest, r.dest + 3, dest);
                    }
                    if (!useHeld || !holdsBlock)
                        gHeldBuild.Stop();
                    const glm::ivec3 destination{dest[0], dest[1], dest[2]};
                    const uint64_t useTime = MsNow();
                    static auto lastUseSent = Clock::time_point{};
                    static uint64_t lastBuildAt = 0;
                    static glm::ivec3 lastBuildCell{0, -1000, 0};
                    const bool tooSoon = Clock::now() - lastUseSent < std::chrono::milliseconds(60);
                    // held building repeats only when build rules allow it this tick; other held use keeps the 200 ms repeat
                    const bool repeatReady = bedrockBuild && use == blockuse::Use::Place ? heldRepeat : useClock >= 0.2 && !tooSoon;
                    // press and held repeat can fire on consecutive ticks for the same cell: drop the duplicate
                    const bool sameCell = destination == lastBuildCell && useTime - lastBuildAt < 200;
                    if (use == blockuse::Use::Place && holdsBlock && sameCell && useHeld && !usePressed)
                        use = blockuse::Use::Nothing;
                    // never place a block into the player's own body
                    if (use == blockuse::Use::Place && holdsBlock) {
                        const auto& placed = LevelCollision::ShapeOf(BlockPalette::Instance().InternRuntime(static_cast<int32_t>(held.blockRuntimeId)));
                        const std::vector<physics::Box> shape(placed.boxes.begin(), placed.boxes.end());
                        if (heldbuild::OverlapsBody(destination, shape, physics::BoundingBox(player.State(), player.Sneaking())))
                            use = blockuse::Use::Nothing;
                    }
                    // fresh clicks start item use immediately, food included
                    if (usePressed || (useHeld && repeatReady)) {
                        lastUseSent = Clock::now();
                        useClock = 0;
                        bool interactedEntity = false;
                        if (usePressed && live && useAim.kind == AimHit::Kind::Entity) {
                            entity::Entity target;
                            if (live->Entities().Find(useAim.entity, target) && IsInteractable(target.type) && !(live->InputLocks() & 128u) &&
                                !DispatchModInteract(abi::InteractKind::InteractEntity, 0, 0, 0, 0, useAim.entity)) {
                                const glm::vec3 offset = EntityClickOffset({camera.x, camera.y, camera.z}, glm::dvec3(camera.Direction()), target.pos,
                                                                           (useAim.boxHi.x - useAim.boxLo.x) * 0.5, useAim.boxHi.y - useAim.boxLo.y,
                                                                           (useAim.boxLo + useAim.boxHi) * 0.5);
                                float click[3] = {offset.x, offset.y, offset.z};
                                { handRenderer.Swing(options.hand); gSelfSwing++; }
                                live->SendSwingArm("interact");
                                live->SendInteractEntity(useAim.entity, eyeF, click);
                                interactedEntity = true;
                            }
                        }
                        const bool allowed = usePressed || (use == blockuse::Use::Place);
                        const bool consumableInAir = holdsItem && !holdsBlock && use == blockuse::Use::Nothing && HandRenderer::IsFood(held.name);
                        if (useAim.kind == AimHit::Kind::Block && allowed && !consumableInAir &&
                            !DispatchModInteract(abi::InteractKind::UseOnBlock, useAim.bx, useAim.by, useAim.bz, useAim.face, 0)) {
                            bool sent = !live;
                            if (live) {
                                float click[3];
                                clickOf(useAim, click);
                                sent = live->SendUseItemOnBlock(useAim.bx, useAim.by, useAim.bz, useAim.face, click, eyeF, static_cast<int>(use), dest);
                            }
                            if (use == blockuse::Use::Place && holdsBlock) {
                                lastBuildAt = useTime;
                                lastBuildCell = destination;
                                if (sent && bedrockBuild) {
                                    const auto& placedShape = LevelCollision::ShapeOf(BlockPalette::Instance().InternRuntime(static_cast<int32_t>(held.blockRuntimeId)));
                                    gHeldBuild.Built({{useAim.bx, useAim.by, useAim.bz}, useAim.face}, destination,
                                                     heldbuild::IsSnappable(held.name, std::vector<physics::Box>(placedShape.boxes.begin(), placedShape.boxes.end())),
                                                     false, useTime, !usePressed);
                                }
                            }
                            if (sent && use != blockuse::Use::Nothing && (usePressed || use == blockuse::Use::Place)) {
                                handRenderer.Swing(options.hand);
                                gSelfSwing++;
                                if (live) live->SendSwingArm(use == blockuse::Use::Place ? "build" : "interact");
                            }
                            if (sent && use == blockuse::Use::Place && holdsBlock) {
                                if (live && bedrockBuild) {
                                    // bridge placements checked against the actual body: the airborne padding below would block jump bridging
                                    float click[3];
                                    clickOf(useAim, click);
                                    live->PredictBridgePlace(dest[0], dest[1], dest[2], static_cast<int32_t>(held.blockRuntimeId), useAim.face, click);
                                } else if (live && !player.Flying()) {
                                    const physics::State& pst = player.State();
                                    const double pad = pst.onGround ? 0.0 : 0.6;
                                    const double lo = pst.y - pad + std::min(0.0, pst.vy * 2.0);
                                    const double hi = pst.y + 1.85 + pad + std::max(0.0, pst.vy * 2.0);
                                    const bool overlaps = pst.x + 0.35 + pad * 0.3 > dest[0] && pst.x - 0.35 - pad * 0.3 < dest[0] + 1 &&
                                                          pst.z + 0.35 + pad * 0.3 > dest[2] && pst.z - 0.35 - pad * 0.3 < dest[2] + 1 &&
                                                          hi > dest[1] && lo < dest[1] + 1;
                                    if (!overlaps)
                                        live->PredictPlace(dest[0], dest[1], dest[2], static_cast<int32_t>(held.blockRuntimeId));
                                } else if (live) {
                                    live->PredictPlace(dest[0], dest[1], dest[2], static_cast<int32_t>(held.blockRuntimeId));
                                }
                                sounds.PlayBlockEvent(held.name, "place", glm::dvec3(dest[0] + 0.5, dest[1] + 0.5, dest[2] + 0.5));
                            }
                        }
                        if (usePressed && !earlyItemUse && !blockedFoodUse && live && holdsItem && use == blockuse::Use::Nothing && !interactedEntity &&
                            !DispatchModInteract(abi::InteractKind::UseItem, 0, 0, 0, 0, 0)) {
                            CustomItemDef custom;
                            const bool heldUse = ServerBlocks::Instance().Item(held.name, custom)
                                                     ? custom.useTicks > 0
                                                     : blockuse::VanillaUseTicks(held.name, HandRenderer::IsFood(held.name)) > 0;
                            live->SendUseItemInAir(eyeF, heldUse);
                            usingItem = heldUse;
                        }
                    }
                    if (!useHeld && live)
                        live->EndItemUseOn();
                    if (usingItem && !useHeld) {
                        usingItem = false;
                        if (live)
                            live->SendReleaseItem(eyeF);
                    }

                    if (live && inputManager.Pressed(Action::PickBlock)) {
                        entity::Entity picked;
                        if (gAim.kind == AimHit::Kind::Block)
                            live->SendPickBlock(gAim.bx, gAim.by, gAim.bz);
                        else if (gAim.kind == AimHit::Kind::Entity && live->Entities().Find(gAim.entity, picked))
                            live->SendPickEntity(picked.runtimeId);
                    }

                    if (live && inputManager.Pressed(Action::Drop) && !held.Empty())
                        live->SendDropSelected(inputManager.IsDown(Action::Sprint) ? 0 : 1);
                }
            } else {
                gAim = AimHit{};
                gHeldBuild.Stop();
                if (live) live->EndItemUseOn();
                if (gBreak.breaking) {
                    if (live) {
                        live->SendPlayerAuthInputBlockAction(1, gBreak.x, gBreak.y, gBreak.z, gBreak.face);
                        live->ClearCrack(gBreak.x, gBreak.y, gBreak.z);
                    }
                    gBreak.breaking = false;
                    gBreak.progress = 0.0f;
                }
                gLocalCrack.active = false;
            }

            double worldTicks = preview ? previewTicks : kNoonTick;
            if (live)
                live->GetWorldTicks(worldTicks);
            if (replayCtl.Playing()) {
                float forcedTicks = 0;
                if (timeline.TimeOfDayAt(replayCtl.TimeMs(), forcedTicks))
                    worldTicks = forcedTicks;
            }
            if (preview && !previewLevel) {
                static auto lastNote = frameStart;
                if (frameStart - lastNote > std::chrono::seconds(3)) {
                    lastNote = frameStart;
                    std::cout << "[preview] waiting: ready=" << levelRenderer->Blocks().Ready()
                              << " complete=" << levelRenderer->Blocks().Complete() << std::endl;
                }
            }
            if (preview && !previewLevel && levelRenderer->Blocks().Ready()) {
                auto built = std::make_unique<Level>();
                if (BuildPreviewWorld(*built, 7)) {
                    previewLevel = std::move(built);
                    previewReadyAt = frameStart;
                    std::cout << "[preview] world built" << std::endl;
                }
            }

            if (preview && previewLevel && PERLIN_ENV("PERLIN_PREVIEW_EDIT")) {
                static bool edited = false;
                if (!edited && frameStart - previewReadyAt > std::chrono::seconds(4)) {
                    edited = true;
                    const std::string list = PERLIN_ENV("PERLIN_PREVIEW_EDIT");
                    size_t start = 0;
                    while (start < list.size()) {
                        size_t end = list.find(';', start);
                        if (end == std::string::npos)
                            end = list.size();
                        int dx = 0, dy = 0, dz = 0;
                        char name[128] = {0};
                        if (std::sscanf(list.substr(start, end - start).c_str(), "%d,%d,%d,%127s", &dx, &dy, &dz, name) == 4) {
                            BlockPalette& palette = BlockPalette::Instance();
                            std::string full = name;
                            std::vector<std::pair<std::string, BlockPalette::StateValue>> states;
                            if (const size_t lb = full.find('['); lb != std::string::npos) {
                                std::string body = full.substr(lb + 1, full.size() - lb - 2);
                                full = full.substr(0, lb);
                                size_t q = 0;
                                while (q < body.size()) {
                                    size_t c = body.find(',', q);
                                    if (c == std::string::npos) c = body.size();
                                    const std::string kv = body.substr(q, c - q);
                                    const size_t eq = kv.find('=');
                                    if (eq != std::string::npos) {
                                        BlockPalette::StateValue sv;
                                        const std::string val = kv.substr(eq + 1);
                                        if (val == "true" || val == "false") { sv.kind = BlockPalette::StateValue::Bool; sv.i = val == "true"; }
                                        else if (!val.empty() && (std::isdigit(static_cast<unsigned char>(val[0])) || val[0] == '-')) { sv.kind = BlockPalette::StateValue::Int; sv.i = std::atoi(val.c_str()); }
                                        else { sv.kind = BlockPalette::StateValue::String; sv.s = val; }
                                        states.emplace_back(kv.substr(0, eq), sv);
                                    }
                                    q = c + 1;
                                }
                                std::sort(states.begin(), states.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                            }
                            const uint16_t id = palette.Intern(BlockPalette::HashState(full, states));
                            previewLevel->SetBlock(static_cast<int>(std::floor(camera.x)) + dx, static_cast<int>(std::floor(camera.y)) + dy,
                                                   static_cast<int>(std::floor(camera.z)) + dz, id);
                        }
                        start = end + 1;
                    }
                    std::cout << "[preview] edits applied" << std::endl;
                }
            }

            camera.thirdPersonDistance = 5.0f;
            if (live && camera.mode != PerspectiveMode::FirstPerson) {
                const LevelCollision col(live->NetLevel(), &levelRenderer->Blocks());
                const glm::vec3 d = camera.mode == PerspectiveMode::ThirdPersonBack ? -camera.Direction() : camera.Direction();
                for (float t = 0.3f; t <= 5.0f; t += 0.15f) {
                    const glm::vec3 q = glm::vec3(camera.x, camera.y, camera.z) + d * t;
                    const physics::BlockShape cell = col.Query(static_cast<int>(std::floor(q.x)), static_cast<int>(std::floor(q.y)),
                                                               static_cast<int>(std::floor(q.z)));
                    if (!cell.Empty()) {
                        camera.thirdPersonDistance = std::max(0.3f, t - 0.3f);
                        break;
                    }
                }
            }
            bool underwater = false;
            bool eyeInLava = false;
            bool inPortalNow = false;
            std::string eyeBiome = "minecraft:plains";
            if (live) {
                const glm::vec3 e = camera.EyePosition();
                const int bx = static_cast<int>(std::floor(e.x)), by = static_cast<int>(std::floor(e.y)),
                          bz = static_cast<int>(std::floor(e.z));
                const LevelCollision col(live->NetLevel(), &levelRenderer->Blocks());
                const physics::BlockShape cell = col.Query(bx, by, bz);
                if (cell.fluid == 1) {
                    const float top = col.Query(bx, by + 1, bz).fluid == 1 ? 1.0f : cell.fluidLevel;
                    underwater = e.y < static_cast<float>(by) + top;
                    int k = 0;
                    while (k < 64 && col.Query(bx, by + k + 1, bz).fluid == 1)
                        k++;
                    const physics::BlockShape topCell = col.Query(bx, by + k, bz);
                    waterSurfaceAbove = static_cast<float>(by + k) + (k == 0 ? top : topCell.fluidLevel) - e.y;
                }
                if (cell.fluid == 2) {
                    const float top = col.Query(bx, by + 1, bz).fluid == 2 ? 1.0f : cell.fluidLevel;
                    eyeInLava = e.y < static_cast<float>(by) + top;
                }
                eyeBiome = levelRenderer->Tints().NameForId(live->NetLevel().GetBiome(bx, by, bz));
                {
                    const int fy = static_cast<int>(std::floor(camera.y - 1.62f));
                    const int fx = static_cast<int>(std::floor(e.x)), fz = static_cast<int>(std::floor(e.z));
                    inPortalNow = cell.portal || col.Query(fx, fy, fz).portal;
                }
            }
            static int waterVisionTime = 0;

            headUnderwater = underwater;
            headInLava = eyeInLava;
            {
                // Java-style nether portal overlay: ramps up over ~4 s inside a portal, fades fast outside.
                const auto nowPortal = Clock::now();
                static auto lastPortalAt = nowPortal;
                float dt = std::min(0.1f, std::chrono::duration<float>(nowPortal - lastPortalAt).count());
                lastPortalAt = nowPortal;
                if (inPortalNow && (live || previewLevel))
                    portalTime = std::min(1.0f, portalTime + dt * 0.25f);
                else
                    portalTime = std::max(0.0f, portalTime - dt * 1.0f);
            }
            if (live && session) {
                const int dim = live->DimensionId();
                if (lastDimId != -1 && dim != lastDimId) {
                    // Dimension hop (portal, end gateway): show the loading screen until the
                    // new dimension's terrain arrives instead of popping straight in.
                    loadingDone = false;
                    loadingStartAt = {};
                    loadingReadyArmed = false;
                    loadingChunkBase = session->ChunksReceived();
                }
                lastDimId = dim;
            }
            if (!underwater)
                waterSurfaceAbove = 0.0f;
            if ((live || previewLevel) && frameStart - handLightAt > std::chrono::milliseconds(200)) {
                handLightAt = frameStart;
                const glm::vec3 e = camera.EyePosition();
                const int bx = static_cast<int>(std::floor(e.x)), by = static_cast<int>(std::floor(e.y)),
                          bz = static_cast<int>(std::floor(e.z));
                Level& lightLevel = live ? live->NetLevel() : *previewLevel;
                const uint8_t packed = CachedLight(lightLevel, bx, by, bz, frameStart);
                handSky = ChunkLighting::Result::Sky(packed);
                handBlock = ChunkLighting::Result::Block(packed);
                dbgSky = handSky;
                dbgBlock = handBlock;
            }
            if (hud)
                hud->SetJavaStyle(true);
            FillHudState(hudState, live, preview);
            hudState.bossBars.clear();
            if (live)
                for (const auto& b : live->BossBars()) {
                    if (hudState.bossBars.size() == 8) break;
                    hudState.bossBars.push_back({b.title, b.progress, b.color, b.overlay});
                }
            hudState.sidebarVisible = false;
            hudState.sidebar.clear();
            if (live) {
                // sidebar is part of the json ui hud: server packs restyling scoreboards.json apply
                const auto sb = live->Scoreboard();
                // json ui sidebar when the server restyles it; else the java one
                hudState.sidebarVisible = sb.visible && !pauseShown && hud && hud->Overrides("scoreboard");
                hudState.sidebarTitle = sb.title;
                for (const auto& r : sb.rows)
                    hudState.sidebar.push_back({r.name, r.score});
            }
            ApplyModHud(hudState);
            {
                static bool f3Was = false, combo = false;
                static bool keyWas[256] = {};
                static std::chrono::steady_clock::time_point cHeldSince{};
                static bool cHeld = false;
                const bool f3 = input.IsKeyDown(Key::F3) && !input.TextMode();
                const auto edge = [&](int key) {
                    const bool down = f3 && input.IsKeyDown(key);
                    const bool fired = down && !keyWas[key & 255];
                    keyWas[key & 255] = down;
                    if (fired)
                        combo = true;
                    return fired;
                };
                const auto say = [&](const std::string& text) {
                    chat.AddMessage(i18n::Tr("perlin.debug.prefix") + " " + text, hudAnim.ticks);
                };
                const auto onOff = [&](bool on) { return i18n::Tr(on ? "options.on" : "options.off"); };
                const auto num = [](double v) {
                    char b[32];
                    std::snprintf(b, sizeof b, "%.2f", v);
                    return std::string(b);
                };
                const float feetY = camera.y - 1.62f;
                const std::string dimName = live ? (live->DimensionId() == 1 ? "minecraft:the_nether" : live->DimensionId() == 2 ? "minecraft:the_end" : "minecraft:overworld")
                                                 : "minecraft:overworld";
                (void)dimName;
                if (edge('A')) {
                    if (live)
                        live->ReloadChunks();
                    else if (levelRenderer)
                        levelRenderer->RemeshAll();
                    say(i18n::Tr("perlin.debug.reload_chunks"));
                }
                {
                    const bool cDown = f3 && input.IsKeyDown('C');
                    if (edge('C')) {
                        const std::string tp = "/tp @s " + num(camera.x) + " " + num(feetY) + " " + num(camera.z) + " " +
                                               num(camera.yaw - 90.0f) + " " + num(-camera.pitch);
                        platform->SetClipboard(tp);
                        say(i18n::Tr("perlin.debug.copied_location", {tp}));
                        cHeldSince = std::chrono::steady_clock::now();
                        cHeld = true;
                    }
                    if (!cDown)
                        cHeld = false;
                    else if (cHeld && std::chrono::steady_clock::now() - cHeldSince > std::chrono::seconds(10))
                        throw std::runtime_error(i18n::Tr("perlin.debug.manual_crash"));
                }
                if (edge('B')) {
                    showHitboxes = !showHitboxes;
                    say(i18n::Tr(showHitboxes ? "perlin.debug.hitboxes_on" : "perlin.debug.hitboxes_off"));
                }
                if (edge('D')) {
                    chat.Clear();
                    say(i18n::Tr("perlin.debug.chat_cleared"));
                }
                if (edge('G')) {
                    showChunkBorders = !showChunkBorders;
                    say(i18n::Tr(showChunkBorders ? "perlin.debug.chunk_borders_on" : "perlin.debug.chunk_borders_off"));
                }
                if (edge('H')) {
                    gAdvancedTooltips = !gAdvancedTooltips;
                    say(i18n::Tr(gAdvancedTooltips ? "perlin.debug.advanced_tooltips_on" : "perlin.debug.advanced_tooltips_off"));
                }
                if (edge('I')) {
                    if (gAim.kind == AimHit::Kind::Block && (live || previewLevel)) {
                        bool loaded = false;
                        Level& l = live ? live->NetLevel() : *previewLevel;
                        const std::string name = BlockPalette::Instance().Name(l.GetBlock(gAim.bx, gAim.by, gAim.bz, loaded));
                        const std::string cmd = "/setblock " + std::to_string(gAim.bx) + " " + std::to_string(gAim.by) + " " +
                                                std::to_string(gAim.bz) + " " + name;
                        platform->SetClipboard(cmd);
                        say(i18n::Tr("perlin.debug.copied_block", {cmd}));
                    } else if (gAim.kind == AimHit::Kind::Entity && live) {
                        entity::Entity target;
                        if (live->Entities().Find(gAim.entity, target)) {
                            const std::string cmd = "/summon " + target.type + " " + num(target.pos.x) + " " + num(target.pos.y) + " " + num(target.pos.z);
                            platform->SetClipboard(cmd);
                            say(i18n::Tr("perlin.debug.copied_entity", {cmd}));
                        }
                    } else {
                        say(i18n::Tr("perlin.debug.nothing_targeted"));
                    }
                }
                if (edge('N')) {
                    static int previousMode = static_cast<int>(ui::GameMode::Creative);
                    const ui::GameMode current = hudState.mode;
                    const auto modeName = [](int m) { return m == 0 ? "survival" : m == 1 ? "creative" : m == 2 ? "adventure" : "spectator"; };
                    if (live) {
                        if (current != ui::GameMode::Spectator) {
                            previousMode = static_cast<int>(current);
                            live->SendCommand("gamemode spectator");
                        } else {
                            live->SendCommand(std::string("gamemode ") + modeName(previousMode));
                        }
                    }
                    say(i18n::Tr("perlin.debug.gamemode_toggle"));
                }
                if (edge('P')) {
                    input.pauseOnLostFocus = !input.pauseOnLostFocus;
                    say(i18n::Tr(input.pauseOnLostFocus ? "perlin.debug.pause_on_lost_focus_on" : "perlin.debug.pause_on_lost_focus_off"));
                }
                if (edge('T')) {
                    if (levelRenderer)
                        levelRenderer->ReloadResources();
                    entityRenderer.ClearCaches();
                    ui::UiAssets::Instance().ClearItemCache();
                    i18n::Lang::Get().Invalidate();
                    sounds.Reload();
                    if (hud)
                        hud->Reload();
                    say(i18n::Tr("perlin.debug.reload_resources"));
                }
                if (edge(Key::F4)) {
                    if (switcherIndex < 0) {
                        const int cur = static_cast<int>(hudState.mode);
                        switcherIndex = cur == 1 ? 0 : cur == 0 ? 1 : cur == 2 ? 2 : 3;
                    }
                    switcherIndex = (switcherIndex + 1) % 4;
                }
                if (edge(Key::Escape)) {
                    softPaused = !softPaused;
                    inputManager.SetBlocked(softPaused);
                    say(i18n::Tr(softPaused ? "perlin.debug.paused" : "perlin.debug.unpaused"));
                }
                if (f3Was && !f3) {
                    if (switcherIndex >= 0) {
                        static const char* kModes[4] = {"creative", "survival", "adventure", "spectator"};
                        if (live)
                            live->SendCommand(std::string("gamemode ") + kModes[switcherIndex]);
                        switcherIndex = -1;
                    } else if (!combo) {
                        debugOn = !debugOn;
                    }
                    combo = false;
                }
                f3Was = f3;
                if (!f3)
                    std::fill(std::begin(keyWas), std::end(keyWas), false);
                if (switcherIndex >= 0) {
                    static const char* kKeys[4] = {"selectWorld.gameMode.creative", "selectWorld.gameMode.survival", "selectWorld.gameMode.adventure",
                                                   "selectWorld.gameMode.spectator"};
                    std::string line;
                    for (int i = 0; i < 4; i++) {
                        const std::string name = i18n::Tr(kKeys[i]);
                        line += (i == switcherIndex ? "\u00a7" "e[ " + name + " ]\u00a7" "r" : "\u00a7" "7" + name + "\u00a7" "r");
                        if (i < 3)
                            line += "   ";
                    }
                    hudState.actionBar = i18n::Tr("perlin.gamemode.switcher") + ": " + line;
                    hudState.actionBarAlpha = 1.0f;
                }
            }
            FillDebug(hudState, live, live ? &live->NetLevel() : previewLevel.get());
            UpdateAudio(live, std::chrono::duration<double>(frameStart - startedAt).count(), dt);

            tickDebt += dt * 20.0f;
            while (tickDebt >= 1.0) {
                tickDebt -= 1.0;
                blockLight.Tick();
                damage.Tick();
                TickMods(live);
                if (live || preview) {
                    TickHud();
                    HandRenderer::TickIn handIn;
                    handIn.yaw = camera.yaw - 90.0f;
                    handIn.pitch = -camera.pitch;
                    handIn.main = hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)];
                    handIn.off = hudState.offhand;
                    handIn.survival = hudState.Survival();
                    handIn.sneaking = player.Sneaking();
                    handIn.useHeld = ((input.Captured() && inputManager.IsDown(Action::Use)) || PERLIN_ENV("PERLIN_PREVIEW_USE") != nullptr) &&
                                     HasAmmo(hudState, live, handIn.main.name);
                    {
                        const std::string& mn = handIn.main.name;
                        const bool drink = mn == "minecraft:potion" || mn == "minecraft:milk_bucket" || mn == "minecraft:honey_bottle" ||
                                           mn == "minecraft:ominous_bottle";
                        const bool always = mn == "minecraft:golden_apple" || mn == "minecraft:enchanted_golden_apple" ||
                                            mn == "minecraft:chorus_fruit" || drink || mn == "minecraft:suspicious_stew";
                        handIn.eating = handIn.survival && handIn.useHeld && HandRenderer::IsFood(mn) &&
                                        (always || hudState.food < 20);
                    }
                    handRenderer.Tick(handIn, options.hand);
                    {
                        const int et = handRenderer.EatTicks();
                        if (et >= 7 && et % 4 == 3) {
                            const std::string& mn = handIn.main.name;
                            const bool drink = mn == "minecraft:potion" || mn == "minecraft:milk_bucket" ||
                                               mn == "minecraft:honey_bottle" || mn == "minecraft:ominous_bottle";
                            sounds.PlayEntityEvent("minecraft:player", drink ? "drink" : "eat",
                                                   glm::dvec3(camera.x, camera.y, camera.z));
                        }
                    }
                }
                waterVisionTime = underwater ? std::min(600, waterVisionTime + 1) : std::max(0, waterVisionTime - 10);
                if (live) {
                    previousRainLevel = rainLevel;
                    previousThunderLevel = thunderLevel;
                    rainLevel = StepWeather(rainLevel, live->RainTarget());
                    thunderLevel = StepWeather(thunderLevel, live->ThunderTarget());
                }
            }
            const float partial = static_cast<float>(tickDebt);
            hudPartial = partial;
            const float rain = glm::mix(previousRainLevel, rainLevel, partial);
            const float thunder = glm::mix(previousThunderLevel, thunderLevel, partial) * rain;

            EnvironmentInput input;
            input.ticks = worldTicks;
            input.rain = rain;
            input.thunder = thunder;
            if (live)
                input.dimension = static_cast<Dimension>(std::clamp(live->DimensionId(), 0, 2));
            const Environment env = SampleEnvironment(input);

            glm::mat4 bobMatrix(1.0f);
            if (live && options.hand.viewBobbing && camera.mode == PerspectiveMode::FirstPerson)
                bobMatrix = BobViewMatrix(player.Bob(), player.PartialTick(), options.hand.bobStrength);
            const glm::mat4 hurtMatrix = sessionReady ? damage.ViewMatrix(partial, options.damageTilt) : glm::mat4(1.0f);
            camera.viewEffect = hurtMatrix * (options.hand.bobCamera ? bobMatrix : glm::mat4(1.0f));
            if (sessionReady && session)
                camera.viewEffect = session->Camera().ShakeMatrix() * camera.viewEffect;
            const glm::mat4 handViewEffect = hurtMatrix * (options.hand.bobHand ? bobMatrix : glm::mat4(1.0f));
            {
                camera.farPlane = std::max(static_cast<float>(options.renderDistance * 16) * 1.6f,
                                           static_cast<float>(GraphicsSettings::From(options).cloudRange * 16));
                camera.nearPlane = std::clamp(0.05f + (camera.farPlane - 1500.0f) * 0.00004f, 0.05f, camera.farPlane > 8000.0f ? 0.4f : 0.25f);
            }
            if (replayCtl.Playing()) {
                if (replayUi.fovOverride > 0)
                    camera.fov = replayUi.fovOverride;
                if (replayUi.roll != 0.0f)
                    camera.viewEffect = glm::rotate(glm::mat4(1.0f), glm::radians(replayUi.roll), glm::vec3(0.0f, 0.0f, 1.0f)) * camera.viewEffect;
            }
            ApplyModCamera(camera.fov, camera.viewEffect);
            const glm::mat4 viewProjection = camera.ViewProjection();
            {
                std::lock_guard lock(modAdv.mutex);
                std::memcpy(modAdv.viewProjection.data(), &viewProjection[0][0], sizeof(float) * 16);
            }
            const Frustum frustum = Frustum::FromViewProjection(viewProjection);
            const glm::vec3 eye = camera.EyePosition();
            const GraphicsSettings graphics = GraphicsSettings::From(options);

            FogInput fogInput;
            fogInput.renderDistanceChunks = options.renderDistance;
            fogInput.cloudRangeChunks = graphics.cloudRange;
            fogInput.forward = camera.Direction();
            fogInput.cameraY = eye.y;
            // java scales the void darkening down to one block above the floor in flat worlds (clear colour scale 1 instead of 1/32)
            if (session && session->FlatWorld())
                fogInput.voidDarknessOnsetRange = 1.0f;
            fogInput.rain = rain;
            fogInput.thunder = thunder;
            fogInput.underwater = underwater;
            fogInput.inLava = eyeInLava;
            if (underwater) {
                const float t = static_cast<float>(waterVisionTime);
                fogInput.waterVision = waterVisionTime >= 600
                                           ? 1.0f
                                           : std::clamp(t / 100.0f, 0.0f, 1.0f) * 0.6f +
                                                 std::clamp((t - 100.0f) / 500.0f, 0.0f, 1.0f) * 0.39999998f;
                struct WaterFog { const char* biome; uint32_t rgb; };
                static const WaterFog kWaterFog[] = {
                    {"cherry_grove", 0x5db7ef}, {"dappled_forest", 0x375154}, {"deep_lukewarm_ocean", 0x041633},
                    {"lukewarm_ocean", 0x041633}, {"mangrove_swamp", 0x4d7a60}, {"pale_garden", 0x556980},
                    {"sulfur_caves", 0x17543c}, {"swamp", 0x232317}, {"warm_ocean", 0x041f33}};
                uint32_t rgb = 0x050533;
                std::string shortName = eyeBiome.rfind("minecraft:", 0) == 0 ? eyeBiome.substr(10) : eyeBiome;
                for (const WaterFog& w : kWaterFog)
                    if (shortName == w.biome)
                        rgb = w.rgb;
                fogInput.waterFogColor = {((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f};
                if (shortName == "swamp")
                    fogInput.waterFogEndScale = 0.85f;
            }
            FogState fog = ComputeFog(env, fogInput);
            if (sessionReady && session) {
                static auto fogCheck = std::chrono::steady_clock::now() - std::chrono::seconds(10);
                if (std::chrono::steady_clock::now() - fogCheck > std::chrono::seconds(2)) {
                    fogCheck = std::chrono::steady_clock::now();
                    fogDefs.Configure(ServerPacks::Instance().Directories());
                    static std::string hudPackKey;
                    std::string key;
                    for (const std::string& d : ServerPacks::Instance().Directories())
                        key += d + "|";
                    if (key != hudPackKey) {
                        hudPackKey = key;
                        ui::UiAssets::Instance().ClearItemCache();
                        if (hud)
                            hud->Reload();
                    }
                }
                const std::vector<std::string> fogStack = session->Camera().FogStack();
                if (!fogStack.empty()) {
                    const FogMedium m = fogDefs.Resolve(fogStack, fogInput.inLava ? 2 : fogInput.underwater ? 1 : 0);
                    const float distance = static_cast<float>(options.renderDistance * 16);
                    if (m.start && m.end) {
                        const float scale = m.relative ? distance : 1.0f;
                        fog.environmentalStart = *m.start * scale;
                        fog.environmentalEnd = std::max(*m.end * scale, fog.environmentalStart + 0.01f);
                        fog.skyEnd = std::min(fog.skyEnd, std::max(fog.environmentalEnd, 1.0f));
                    }
                    if (m.color)
                        fog.color = glm::vec4(*m.color, fog.color.w);
                } else if (live && static_cast<Dimension>(std::clamp(live->DimensionId(), 0, 2)) == Dimension::Nether) {
                    // no server fog pushed: fall back to per-biome nether haze
                    const std::string bn = eyeBiome.rfind("minecraft:", 0) == 0 ? eyeBiome.substr(10) : eyeBiome;
                    glm::vec3 tint(0.2f, 0.05f, 0.05f);
                    if (bn == "crimson_forest")
                        tint = glm::vec3(0.35f, 0.05f, 0.07f);
                    else if (bn == "warped_forest")
                        tint = glm::vec3(0.05f, 0.25f, 0.22f);
                    else if (bn == "basalt_deltas")
                        tint = glm::vec3(0.18f, 0.16f, 0.18f);
                    else if (bn == "soul_sand_valley")
                        tint = glm::vec3(0.15f, 0.12f, 0.14f);
                    fog.color = glm::vec4(tint, fog.color.w);
                }
            }

            ApplyModFog(fog);
            if (preview) {
                static int once = 0;
                if (once++ == 20)
                    std::cout << "[preview] fog env " << fog.environmentalStart << ".." << fog.environmentalEnd
                              << " rd " << fog.renderDistanceStart << ".." << fog.renderDistanceEnd << " sky "
                              << fog.skyEnd << " rdchunks " << options.renderDistance << " ticks " << worldTicks << " sun "
                              << env.sunAngle << std::endl;
            }
            std::vector<dynlight::Light> dyn;
            int dynMode = options.dynamicLights == "fancy" ? 2 : options.dynamicLights == "fast" ? 1 : 0;
            if (const char* dl = PERLIN_ENV("PERLIN_DYNLIGHTS"))
                dynMode = std::string(dl) == "fancy" ? 2 : std::string(dl) == "fast" ? 1 : 0;
            if (dynMode && (live || previewLevel)) {
                const std::vector<entity::Entity>& lightEntities = frameEntities;
                for (const entity::Entity& le : lightEntities)
                    if (!le.isSelf)
                        dynlight::Collect(le, dyn);
                if (hudState.mode != ui::GameMode::Spectator) {
                    const int own = std::max(dynlight::ItemLevel(hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)].name),
                                             dynlight::ItemLevel(hudState.offhand.name));
                    if (own > 0)
                        dyn.push_back({glm::dvec3(camera.x, camera.y - 0.25, camera.z), static_cast<float>(own)});
                }
                dynlight::Select(dyn, glm::dvec3(camera.x, camera.y, camera.z), 8);
            }
            FrameParams params{};
            params.motionBlur = options.frameGen != "off" ? 0.0f : options.motionBlur;
            params.bloom = options.bloom;
            params.shadowQuality = options.shadowQuality;
            params.shadowDistance = options.shadowDistance;
            params.shadowEntities = options.shadowEntities;
            params.cameraMedium = headUnderwater ? 1 : (headInLava ? 2 : 0);
            params.waterSurfaceAbove = waterSurfaceAbove;
            if (pauseShown && options.menuBlur > 0)
                params.menuBlur = static_cast<float>(options.menuBlur * std::max(1, hud->OverlayScale()));
            if (const char* mb = PERLIN_ENV("PERLIN_MOTION_BLUR"))
                params.motionBlur = std::clamp(static_cast<float>(std::atof(mb)), 0.0f, 1.0f);
            params.dynMode = dynMode;
            {
                const std::vector<ShaderSetting>& packSettingList = CachedPackSettings(options.shaderPack);
                for (const ShaderSetting& st : packSettingList) {
                    if (st.slot < 0 || st.slot >= 32)
                        continue;
                    const auto found = options.shaderSettings.find(st.id);
                    params.packSettings[st.slot] = static_cast<float>(found == options.shaderSettings.end() ? st.def
                                                                      : std::clamp(found->second, st.min, st.max));
                }
            }
            params.dynCount = static_cast<int>(dyn.size());
            for (size_t i = 0; i < dyn.size(); i++) {
                params.dynLights[i][0] = static_cast<float>(dyn[i].pos.x - camera.x);
                params.dynLights[i][1] = static_cast<float>(dyn[i].pos.y - camera.y);
                params.dynLights[i][2] = static_cast<float>(dyn[i].pos.z - camera.z);
                params.dynLights[i][3] = dyn[i].level;
            }
            std::copy_n(glm::value_ptr(viewProjection), 16, params.viewProjection);
            std::copy_n(glm::value_ptr(camera.SkyViewProjection()), 16, params.skyViewProjection);
            std::copy_n(glm::value_ptr(eye), 3, params.cameraPosition);
            std::copy_n(glm::value_ptr(fog.color), 4, params.fogColor);
            params.environmentalFogStart = fog.environmentalStart;
            params.environmentalFogEnd = fog.environmentalEnd;
            params.renderDistanceFogStart = fog.renderDistanceStart;
            params.renderDistanceFogEnd = fog.renderDistanceEnd;
            params.skyFogEnd = fog.skyEnd;
            params.cloudFogEnd = fog.cloudEnd;
            params.rgss = graphics.rgss;
            {
                glm::mat4 viewMatrix, projectionMatrix;
                camera.Matrices(viewMatrix, projectionMatrix);
                std::copy_n(glm::value_ptr(viewMatrix), 16, params.view);
                std::copy_n(glm::value_ptr(projectionMatrix), 16, params.projection);
                const glm::vec3 now(camera.x, camera.y, camera.z);
                static glm::vec3 lastCamera = now;
                params.teleported = glm::distance(now, lastCamera) > 6.0f;
                lastCamera = now;
            }
            if (options.upscaler != "off") {
                float scale = static_cast<float>(options.upscaleCustom) / 100.0f;
                if (options.upscaleTargetFps > 0) {
                    static float dynamic = 1.0f;
                    static double frameMs = 16.0;
                    static auto lastChange = std::chrono::steady_clock::now();
                    const double shownFrames = static_cast<double>(fgActive);
                    frameMs += (static_cast<double>(dt) * 1000.0 / shownFrames - frameMs) * 0.05;
                    const double targetMs = 1000.0 / static_cast<double>(options.upscaleTargetFps);
                    const auto nowDyn = std::chrono::steady_clock::now();
                    if (nowDyn - lastChange > std::chrono::milliseconds(1200)) {
                        if (frameMs > targetMs * 1.06 && dynamic > 0.34f) {
                            dynamic = std::max(0.33f, dynamic - 0.05f);
                            lastChange = nowDyn;
                        } else if (frameMs < targetMs * 0.85 && dynamic < 0.99f) {
                            dynamic = std::min(1.0f, dynamic + 0.05f);
                            lastChange = nowDyn;
                        }
                    }
                    scale = dynamic;
                }
                params.renderScale = scale;
                params.upscaler = options.upscaler == "temporal" ? 3 : options.upscaler == "fsr1" ? 2 : options.upscaler == "bicubic" ? 1 : 0;
                params.upscaleSharpness = static_cast<float>(options.upscaleSharpness) / 100.0f;
            }
            params.frameGen = fgActive;
            {
                const glm::mat4 handProjection = HandRenderer::Projection(70.0f, camera.aspect, camera.farPlane);
                std::copy_n(glm::value_ptr(handProjection), 16, params.handProjection);
                params.cameraNear = camera.nearPlane;
                params.cameraFar = camera.farPlane;
            }

            params.sky.enabled = input.dimension == Dimension::Overworld;
            std::copy_n(glm::value_ptr(env.skyColor), 3, params.sky.skyColor);
            params.sky.sunAngle = env.sunAngle;
            params.sky.moonAngle = env.moonAngle;
            params.sky.starAngle = env.starAngle;
            params.sky.rainBrightness = 1.0f - rain;
            params.sky.starBrightness = env.starBrightness;
            std::copy_n(glm::value_ptr(env.sunriseColor), 4, params.sky.sunrise);
            params.sky.moonPhase = env.moonPhase;
            // the sky turns dark below the horizon: sea level in a normal world, the world floor in a flat one
            params.sky.darkDisc = eye.y < (session && session->FlatWorld() ? kFlatHorizonHeight : kHorizonHeight);

            const glm::vec4 cloudColor = glm::round(glm::clamp(env.cloudColor, 0.0f, 1.0f) * 255.0f) / 255.0f;
            params.cloud.enabled = graphics.clouds && cloudColor.w > 0.0f;
            params.cloud.fancy = graphics.fancyClouds;
            params.cloud.rangeChunks = graphics.cloudRange;
            std::copy_n(glm::value_ptr(cloudColor), 4, params.cloud.color);
            params.cloud.height = env.cloudHeight;
            params.cloud.gameTicks = std::chrono::duration<double>(frameStart - startedAt).count() * 20.0;

            params.crosshair = false;
            {
                int cw = 0, ch = 0;
                renderer->FramebufferSize(cw, ch);
                params.guiScale = ui::JavaGuiScale(cw, ch, options.guiScale);
            }
            params.screenTint[0] = 1.0f;
            params.screenTint[1] = 0.0f;
            params.screenTint[2] = 0.0f;
            params.screenTint[3] = options.damageTint ? damage.TintAlpha() : 0.0f;
            params.portalOverlay = portalTime;
            if (sessionReady && session) {
                const glm::vec4 fade = session->Camera().Tint();
                if (fade.a > 0.0f) {
                    const float base = params.screenTint[3];
                    const float total = fade.a + base * (1.0f - fade.a);
                    for (int i = 0; i < 3; i++)
                        params.screenTint[i] = (fade[i] * fade.a + params.screenTint[i] * base * (1.0f - fade.a)) / std::max(total, 0.0001f);
                    params.screenTint[3] = total;
                }
            }

            {
                static float waterTile = -1, flowTile = -1, lavaTile = -1;
                static int tileScan = 0;
                if ((tileScan++ % 120) == 0 && levelRenderer->Blocks().Ready()) {
                    waterTile = static_cast<float>(levelRenderer->Blocks().TileContaining("water_still"));
                    flowTile = static_cast<float>(levelRenderer->Blocks().TileContaining("water_flow"));
                    lavaTile = static_cast<float>(levelRenderer->Blocks().TileContaining("lava_still"));
                    static std::vector<uint8_t> lastFoliage;
                    std::vector<uint8_t> foliage = levelRenderer->Blocks().FoliageFlags();
                    if (foliage != lastFoliage) {
                        renderer->UploadTileFlags(foliage);
                        lastFoliage = std::move(foliage);
                    }
                }
                params.waterTile = waterTile;
                params.waterFlowTile = flowTile;
                params.lavaTile = lavaTile;
                params.heldLight = static_cast<float>(options.heldLight);
                if (const char* h = PERLIN_ENV("PERLIN_HELDLIGHT"))
                    params.heldLight = static_cast<float>(std::atoi(h));
            }
            params.timeSeconds = std::chrono::duration<float>(frameStart - startedAt).count();
            params.frameIndex = static_cast<uint32_t>(frameCounter++);
            {
                int64_t day = -1;
                int tick = -1;
                if (sessionReady && session && session->GetWorldTime(day, tick)) {
                    params.worldDay = static_cast<int>(day);
                    params.worldTick = tick;
                } else {
                    // preview/offline: renderer clock stands in for session time
                    params.worldDay = static_cast<int>(worldTicks / 24000.0);
                    params.worldTick = static_cast<int>(std::fmod(worldTicks, 24000.0));
                }
            }

            LightmapParams lightmap;
            lightmap.blockFactor = blockLight.Value() + 1.4f;
            lightmap.skyFactor = env.skyLightFactor;
            lightmap.skyLightColor = env.skyLightColor;
            lightmap.ambientColor = env.ambientColor;
            lightmap.blockLightTint = env.blockLightTint;
            lightmap.nightVisionColor = env.nightVisionColor;
            lightmap.brightness = options.gamma;
            BuildLightmap(lightmap, params.lightmap);

            if (gProfile.enabled)
                gProfile.ms[0] += std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count();
            fgPacer.PreRenderMs(std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count());
            if (fgPacer.Pending() && !exportJob.active)
                frames += fgPacer.Pump(FrameGenPacer::Stage::BeforeRender, !options.vsync, Clock::now(),
                                       [&](int i, int n) { return renderer->PresentGenerated(i, n); });
            renderer->BeginFrame(params);
            if (live) {
                { ProfScope p(1); levelRenderer->Update(&live->NetLevel(), camera.x, camera.z, frustum); }
                { ProfScope p(2); levelRenderer->Render(frustum, camera.x, camera.z); }
            } else if (previewLevel) {
                { ProfScope p(1); levelRenderer->Update(previewLevel.get(), camera.x, camera.z, frustum); }
                { ProfScope p(2); levelRenderer->Render(frustum, camera.x, camera.z); }
                if (!previewShot.empty() && previewShotFrames < 0) {
                    const size_t meshed = levelRenderer->MeshedChunks();
                    if (meshed != previewLastMeshed) {
                        previewLastMeshed = meshed;
                        previewReadyAt = frameStart;
                    }
                    if (std::chrono::duration<double>(frameStart - previewReadyAt).count() > previewWait) {
                        renderer->RequestScreenshot(previewShot);
                        previewShotFrames = 0;
                    }
                } else if (previewShotFrames >= 0 && ++previewShotFrames > 6) {
                    std::cout << "[preview] screenshot saved, meshed chunks " << levelRenderer->MeshedChunks() << std::endl;
                    MeshProfilePrint();
                    if (const char* bench = PERLIN_ENV("PERLIN_MESHBENCH"))
                        levelRenderer->BenchmarkMeshing(*previewLevel, std::atoi(bench));
                    platform->isRunning = false;
                }
            }
            if (live) {
                static const char* shot = PERLIN_ENV("PERLIN_LIVE_SHOT");
                static const char* swingAt = PERLIN_ENV("PERLIN_LIVE_SWING");
                static bool swung = false, shotDone = false;
                static int shotFrames = 0;
                const double t = std::chrono::duration<double>(frameStart - startedAt).count();
                if (swingAt && !swung && t > std::atof(swingAt)) {
                    swung = true;
                    handRenderer.Attack(options.hand, true);
                    live->SendSwingArm("attack");
                }
                if (shot) {
                    const std::string spec = shot;
                    const size_t at = spec.rfind('@');
                    const double when = at == std::string::npos ? 20.0 : std::atof(spec.c_str() + at + 1);
                    if (!shotDone && t > when) {
                        shotDone = true;
                        renderer->RequestScreenshot(spec.substr(0, at));
                    } else if (shotDone && ++shotFrames > 8) {
                        platform->isRunning = false;
                    }
                }
            }
            std::optional<entity::Entity> fpSelf;
            if ((live || previewLevel) && ui::UiAssets::Instance().VanillaRoot().size()) {
                Level& entityLevel = live ? live->NetLevel() : *previewLevel;
                if (!live && !previewEntitiesSpawned && PERLIN_ENV("PERLIN_PREVIEW_ENTITIES")) {
                    previewEntitiesSpawned = true;
                    SpawnPreviewEntities();
                }
                std::vector<entity::Entity> entities = frameEntities;
                ApplyModSkins(entities);
                if (player.Gliding()) {
                    const glm::dvec3 here(camera.x, camera.y, camera.z);
                    entities.erase(std::remove_if(entities.begin(), entities.end(),
                                                  [&](const entity::Entity& e) {
                                                      return e.type == "minecraft:fireworks_rocket" && glm::distance(e.pos, here) < 6.0;
                                                  }),
                                   entities.end());
                }
                if (!live && PERLIN_ENV("PERLIN_PREVIEW_RIDE") && entities.size() >= 2) {
                    std::sort(entities.begin(), entities.end(), [](const entity::Entity& a, const entity::Entity& b) { return a.runtimeId < b.runtimeId; });
                    entities[1].vehicleUid = entities[0].uniqueId;
                }
                entityRenderer.ResolveSeats(entities);
                if (live)
                    for (entity::Entity& e : entities)
                        if (e.sleeping && e.isPlayer)
                            ApplyBedPose(live->NetLevel(), e);
                if (!live && !previewWalk.empty()) {
                    static std::unordered_map<uint64_t, glm::dvec3> base;
                    // timed from when the world settles: the screenshot fires at PERLIN_PREVIEW_WAIT on the same clock
                    const double t = std::chrono::duration<double>(frameStart - previewReadyAt).count();
                    static const bool walking = PERLIN_ENV("PERLIN_PREVIEW_ENTITY_WALK") != nullptr;
                    static const double walkTurn = PERLIN_ENV("PERLIN_PREVIEW_ENTITY_WALK_DIR") ? std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_WALK_DIR")) : 0.0;
                    // head turns by this much between 0.5 s and 1.5 s
                    static const float headTurn = PERLIN_ENV("PERLIN_PREVIEW_ENTITY_HEAD") ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_HEAD"))) : 0.0f;
                    for (entity::Entity& pe : entities)
                        if (std::find(previewWalk.begin(), previewWalk.end(), pe.runtimeId) != previewWalk.end()) {
                            const glm::dvec3 b = base.emplace(pe.runtimeId, pe.pos).first->second;
                            const double yawRad = glm::radians(static_cast<double>(pe.yaw) + walkTurn);
                            // always from the base: Move takes eye height for players, feeding pos back would sink it
                            pe.pos = b + glm::dvec3(-std::sin(yawRad), 0.0, std::cos(yawRad)) * (walking ? std::sin(t * 1.5) * 1.5 : 0.0);
                            pe.headYaw = pe.yaw + headTurn * static_cast<float>(std::clamp(t - 0.5, 0.0, 1.0));
                            previewEntities.Move(pe.runtimeId, pe.pos, pe.pitch, pe.yaw, pe.headYaw, true);
                        }
                }
                const bool firstPerson = camera.mode == PerspectiveMode::FirstPerson;
                if (live || !firstPerson || PERLIN_ENV("PERLIN_PREVIEW_SELF")) {
                    entity::Entity self;
                    self.runtimeId = 0xFFFFFFFFFFFF0001ull;
                    self.type = "minecraft:player";
                    self.isPlayer = self.isSelf = true;
                    if (live && live->Invisible())
                        self.flags |= entity::flag::Invisible;
                    self.name = live ? live->PlayerName() : std::string("Perlin");
                    self.pos = {camera.x, camera.y - (live ? static_cast<double>(player.EyeOffset()) : entity::kPlayerEye), camera.z};
                    self.yaw = self.headYaw = camera.yaw - 90.0f;
                    self.pitch = -camera.pitch;
                    if (replayCtl.Playing()) {
                        const replay::Pose rp = replayCtl.InterpolatedPose();
                        if (rp.known) {
                            self.pos = {rp.x, rp.y - entity::kPlayerEye, rp.z};
                            self.yaw = self.headYaw = rp.yaw;
                            self.pitch = rp.pitch;
                        }
                    }
                    self.moveSerial = 1;
                    self.swingSerial = gSelfSwing;
                    self.hurtSerial = live ? live->HurtCount() : 0;
                    self.mainHand = hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)].name;
                    for (int i = 0; i < 4; i++)
                        self.armor[i] = hudState.armorItems[i].name;
                    self.offHand = hudState.offhand.name;
                    {
                        const HandRenderer::RigState rig = handRenderer.Rig(0.0f);
                        if (rig.using_ || rig.blocking || handRenderer.EatTicks() > 0)
                            self.flags |= entity::flag::UsingItem;
                    }
                    if (player.Sneaking() && !player.Crawling())
                        self.flags |= entity::flag::Sneaking;
                    self.crawling = player.Crawling();
                    player.SetElytra(hudState.armorItems[1].name == "minecraft:elytra" && hudState.armorItems[1].aux < 431);
                    if (player.Gliding())
                        self.flags |= entity::flag::Gliding;
                    if (player.Swimming())
                        self.flags |= entity::flag::Swimming;
                    if (live && !replayCtl.Playing()) {
                        const LoadedSkin& ls = live->Skin();
                        const std::string key = ls.skinId + (ls.slimArms ? "/slim" : "/classic") + "/" + ls.capeId;
                        if (!selfSkinData || key != selfSkinKey) {
                            auto sd = std::make_shared<entity::SkinData>();
                            sd->serial = std::hash<std::string>{}(key) | 1ull;
                            sd->skinWidth = ls.skinWidth;
                            sd->skinHeight = ls.skinHeight;
                            sd->skinRgba = ls.skinRgba;
                            sd->capeWidth = ls.capeWidth;
                            sd->capeHeight = ls.capeHeight;
                            sd->capeRgba = ls.capeRgba;
                            sd->slim = ls.slimArms;
                            selfSkinData = sd;
                            selfSkinKey = key;
                        }
                        self.skin = selfSkinData;
                    }
                    if (replayCtl.Playing() && live) {
                        const std::string& rn = replayCtl.Meta().playerName;
                        for (const auto& tp : live->PlayerList())
                            if (tp.skin && !tp.skin->skinRgba.empty() && (tp.name == rn || tp.uid == static_cast<int64_t>(replayCtl.Meta().localRuntimeId))) {
                                self.skin = tp.skin;
                                if (tp.name == rn)
                                    break;
                            }
                    }
                    fpSelf = self;
                    if (live && live->RidingUid() != 0) {
                        self.vehicleUid = live->RidingUid();
                        self.flags |= entity::flag::Riding;
                    }
                    dollSelf = self;
                    if (live && live->Sleeping()) {
                        self.sleeping = true;
                        ApplyBedPose(live->NetLevel(), self);
                    }
                    entities.push_back(std::move(self));
                }
                if (replayCtl.Playing() && (!replayUi.showPlayers || !replayUi.showEntities || !replayUi.showNametags)) {
                    entities.erase(std::remove_if(entities.begin(), entities.end(), [&](const entity::Entity& en) {
                                       return en.isPlayer ? !replayUi.showPlayers : !replayUi.showEntities;
                                   }), entities.end());
                    if (!replayUi.showNametags)
                        for (entity::Entity& en : entities)
                            en.name.clear();
                }
                entity::EntityRenderer::FrameIn ein;
                ein.time = std::chrono::duration<double>(frameStart - startedAt).count();
                ein.dt = dt;
                {
                    const glm::vec3 eyePos = camera.EyePosition();
                    ein.camera = {eyePos.x, eyePos.y, eyePos.z};
                }
                ein.firstPerson = firstPerson && !(replayCtl.Playing() && replayCtl.cameraMode != replay::CameraMode::Follow);
                {
                    const bool packShadows = renderer->PackShadowsActive() && options.shadowEntities;
                    ein.blobShadows = options.shadows && !packShadows;
                    ein.selfShadow = packShadows;
                }
                ein.ownNametag = options.thirdPersonNametag;
                ein.nametagDepth = fgActive > 1;
                ein.ownParts = options.skinParts;
                ein.skins3d = options.skins3d;
                {
                    std::lock_guard lock(modAdv.mutex);
                    if (std::chrono::steady_clock::now() < modAdv.poseUntil)
                        ein.ownPose = glm::vec3(modAdv.pose.pitch, modAdv.pose.yaw, modAdv.pose.roll);
                    if (!modAdv.frameLines.empty() && !(replayCtl.Playing() && replayUi.hideHud))
                        ein.modLines = &modAdv.frameLines;
                }
                PrepareModRender(ein);
                ein.capePhysics = options.capePhysics == "shoulder" ? 2 : options.capePhysics == "off" ? 0 : 1;
                if (live) {
                    int fast = -1, slow = -1;
                    for (const auto& fx : live->GetEffects()) {
                        if (fx.id == 3 || fx.id == 26)
                            fast = std::max(fast, fx.amplifier);
                        else if (fx.id == 4)
                            slow = std::max(slow, fx.amplifier);
                    }
                    ein.ownSwingTicks = fast >= 0 ? 6.0f - static_cast<float>(1 + fast) : slow >= 0 ? 6.0f + static_cast<float>((1 + slow) * 2) : 6.0f;
                }
                ein.frustum = &frustum;
                ein.sectionReached = [this](int sx, int sy, int sz) { return renderer->SectionReached(sx, sy, sz); };
                ein.cameraYaw = camera.yaw - 90.0f;
                ein.cameraPitch = -camera.pitch;
                if (camera.mode == PerspectiveMode::ThirdPersonFront) {
                    ein.cameraYaw += 180.0f;
                    ein.cameraPitch = camera.pitch;
                }
                ein.worldTicks = worldTicks;
                {
                    int fw = 0, fh = 0;
                    FramebufferSizeEffective(fw, fh);
                    ein.pixelScale = fh > 0 ? 2.0f * std::tan(glm::radians(camera.fov) * 0.5f) / static_cast<float>(fh) : 0.0f;
                    ein.lineWidthPx = std::max(2.5f, static_cast<float>(fw) / 1920.0f * 2.5f);
                    ein.chunkBorders = showChunkBorders;
                    ein.hitboxes = showHitboxes;
                    ein.playerPos = glm::dvec3(camera.x, camera.y - 1.62, camera.z);
                }
                if (preview && previewLevel && PERLIN_ENV("PERLIN_PREVIEW_AIM")) {
                    gAim = Raycast(*previewLevel, &levelRenderer->Blocks(), {},
                                   [](const entity::Entity&) { return glm::vec2(0.6f, 1.8f); },
                                   {camera.x, camera.y, camera.z}, glm::dvec3(camera.Direction()), 5.7, 0.0);
                }
                if (gAim.kind == AimHit::Kind::Block && (options.showHud || (preview && PERLIN_ENV("PERLIN_PREVIEW_AIM")))) {
                    ein.outline = true;
                    ein.outlineLo = gAim.boxLo;
                    ein.outlineHi = gAim.boxHi;
                    ein.outlineShape = gAim.shape;
                }
                const auto crackAt = [&](int x, int y, int z, int stage) {
                    entity::EntityRenderer::FrameIn::Crack crack{x, y, z, stage, {}};
                    if (live && levelRenderer) {
                        std::vector<physics::Box> boxes;
                        if (LevelCollision(live->NetLevel(), &levelRenderer->Blocks()).Selection(x, y, z, boxes))
                            for (const physics::Box& b : boxes)
                                crack.boxes.push_back({static_cast<float>(b.x0), static_cast<float>(b.y0), static_cast<float>(b.z0),
                                                       static_cast<float>(b.x1), static_cast<float>(b.y1), static_cast<float>(b.z1)});
                    }
                    return crack;
                };
                if (live)
                    for (const auto& c : live->Cracks()) {
                        bool present = false;
                        const uint16_t cur = live->NetLevel().GetBlock(c.x, c.y, c.z, present);
                        if (present && cur == 0)
                            continue;
                        if (!(gLocalCrack.active && c.x == gLocalCrack.x && c.y == gLocalCrack.y && c.z == gLocalCrack.z))
                            ein.cracks.push_back(crackAt(c.x, c.y, c.z, c.stage));
                    }
                if (gLocalCrack.active)
                    ein.cracks.push_back(crackAt(gLocalCrack.x, gLocalCrack.y, gLocalCrack.z, gLocalCrack.stage));
                if (const char* cr = PERLIN_ENV("PERLIN_PREVIEW_CRACK"); cr && gAim.kind == AimHit::Kind::Block)
                    ein.cracks.push_back(crackAt(gAim.bx, gAim.by, gAim.bz, std::atoi(cr)));
                ein.groundY = [&](double x, double y, double z) -> double {
                    const int top = entityLevel.FindBlockBelow(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(z)),
                                                               static_cast<int>(std::floor(y)),
                                                               [](uint16_t id) {
                                                                   if (id == 0)
                                                                       return false;
                                                                   const physics::BlockShape& sh = LevelCollision::ShapeOf(id);
                                                                   return sh.boxes.size() == 1 && sh.boxes[0].x0 <= 0.001 && sh.boxes[0].y0 <= 0.001 &&
                                                                          sh.boxes[0].z0 <= 0.001 && sh.boxes[0].x1 >= 0.999 &&
                                                                          sh.boxes[0].y1 >= 0.999 && sh.boxes[0].z1 >= 0.999;
                                                               });
                    return top <= Chunk::MIN_Y - 1 ? -1000.0 : static_cast<double>(top) + 1.0;
                };
                ein.light = [&](const glm::dvec3& p) -> glm::vec3 {
                    const int bx = static_cast<int>(std::floor(p.x)), by = static_cast<int>(std::floor(p.y)),
                              bz = static_cast<int>(std::floor(p.z));
                    const int64_t key = (static_cast<int64_t>(bx) * 73856093) ^ (static_cast<int64_t>(by) * 19349663) ^ (static_cast<int64_t>(bz) * 83492791);
                    auto& entry = entityLight[key];
                    if (frameStart - entry.at > std::chrono::milliseconds(300)) {
                        entry.at = frameStart;
                        const uint8_t packed = CachedLight(entityLevel, bx, by, bz, frameStart);
                        int sky = std::clamp<int>(ChunkLighting::Result::Sky(packed), 0, 15);
                        int blk = std::clamp<int>(ChunkLighting::Result::Block(packed), 0, 15);
                        if (sky == 0 && blk == 0) {
                            static const int kN[6][3] = {{0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
                            for (const auto& n : kN) {
                                const uint8_t q = CachedLight(entityLevel, bx + n[0], by + n[1], bz + n[2], frameStart);
                                sky = std::max<int>(sky, std::clamp<int>(ChunkLighting::Result::Sky(q), 0, 15));
                                blk = std::max<int>(blk, std::clamp<int>(ChunkLighting::Result::Block(q), 0, 15));
                            }
                        }
                        const uint8_t* px = params.lightmap + (static_cast<size_t>(sky) * kLightmapSize + blk) * 4;
                        entry.colour = {px[0] / 255.0f, px[1] / 255.0f, px[2] / 255.0f};
                        entry.sky = sky;
                        entry.block = blk;
                    }
                    if (!dyn.empty()) {
                        const int extra = static_cast<int>(dynlight::LevelAt(dyn, p + glm::dvec3(0.0, 0.5, 0.0)));
                        if (extra > entry.block) {
                            const uint8_t* px = params.lightmap + (static_cast<size_t>(entry.sky) * kLightmapSize + extra) * 4;
                            return {px[0] / 255.0f, px[1] / 255.0f, px[2] / 255.0f};
                        }
                    }
                    return entry.colour;
                };
                if (entityLight.size() > 4096)
                    entityLight.clear();
                {
                    if (!particles.tileFor && levelRenderer) {
                        particles.tileFor = [this](uint16_t id, int bx, int by, int bz, particle::TileChoice& out) {
                            const BlockAppearance& app = levelRenderer->Blocks().Look(id);
                            const std::string pname = BlockPalette::Instance().Name(id);
                            const auto pHas = [&](const char* part) { return pname.find(part) != std::string::npos; };
                            const bool untinted = pHas("cherry_leaves") || pHas("azalea") || pHas("pale_oak_leaves") || pHas("leaf_litter");
                            const bool foliageLike = !untinted && (pHas("leaves") || pHas("vine"));
                            const bool grassLike = !untinted && (pHas("grass") || pHas("fern") || pHas("sugar_cane"));
                            const bool grassBlock = pHas("grass_block");
                            int tile = (app.overlayTile > 0 && !grassBlock) ? app.bottom : (app.side ? app.side : app.top);
                            if (grassBlock && app.top)
                                tile = app.top;
                            if (!tile)
                                tile = app.top ? app.top : app.bottom;
                            if (!tile)
                                return false;
                            out.tile = tile;
                            out.tint = (foliageLike || grassLike || (app.overlayTile <= 0 && (app.tintSide || app.tintTop)))
                                           ? glm::vec3(app.tintR, app.tintG, app.tintB)
                                           : glm::vec3(1.0f);
                            if ((foliageLike || grassLike) && out.tint == glm::vec3(0.0f))
                                out.tint = glm::vec3(0.45f, 0.75f, 0.3f);
                            const bool wantTint = foliageLike || grassLike || out.tint != glm::vec3(1.0f);
                            if (wantTint && particleLevel && levelRenderer->Tints().Ready()) {
                                const BiomeTints& tints = levelRenderer->Tints();
                                const std::string name = BlockPalette::Instance().Name(id);
                                const auto has = [&](const char* part) { return name.find(part) != std::string::npos; };
                                if (has("birch_leaves")) {
                                    const auto c = tints.MapAverage("birch");
                                    out.tint = {c[0], c[1], c[2]};
                                } else if (has("spruce_leaves")) {
                                    const auto c = tints.MapAverage("evergreen");
                                    out.tint = {c[0], c[1], c[2]};
                                } else if (has("leaves") || has("vine")) {
                                    glm::vec3 sum(0.0f);
                                    for (int dz = -2; dz <= 2; dz++)
                                        for (int dx = -2; dx <= 2; dx++) {
                                            const auto c = tints.Foliage(tints.NameForId(particleLevel->GetBiome(bx + dx, by, bz + dz)));
                                            sum += glm::vec3(c[0], c[1], c[2]);
                                        }
                                    out.tint = sum / 25.0f;
                                } else if (has("grass") || has("fern") || has("sugar_cane")) {
                                    glm::vec3 sum(0.0f);
                                    for (int dz = -2; dz <= 2; dz++)
                                        for (int dx = -2; dx <= 2; dx++) {
                                            const auto c = tints.Grass(tints.NameForId(particleLevel->GetBiome(bx + dx, by, bz + dz)));
                                            sum += glm::vec3(c[0], c[1], c[2]);
                                        }
                                    out.tint = sum / 25.0f;
                                }
                            }
                            return true;
                        };
                    }
                    static std::vector<int8_t> solidById;
                    particleLevel = &entityLevel;
                    particle::WorldAccess pw;
                    pw.block = [&](int x, int y, int z) -> uint16_t {
                        bool loaded = false;
                        const uint16_t id = entityLevel.GetBlock(x, y, z, loaded);
                        return loaded ? id : uint16_t(0);
                    };
                    pw.solid = [&](int x, int y, int z) {
                        bool loaded = false;
                        const uint16_t id = entityLevel.GetBlock(x, y, z, loaded);
                        if (!loaded || id == 0)
                            return false;
                        if (id >= solidById.size())
                            solidById.resize(static_cast<size_t>(id) + 256, -1);
                        int8_t& s = solidById[id];
                        if (s < 0) {
                            const physics::BlockShape& sh = LevelCollision::ShapeOf(id);
                            s = (sh.boxes.size() == 1 && sh.boxes[0].x0 <= 0.001 && sh.boxes[0].y0 <= 0.001 && sh.boxes[0].z0 <= 0.001 &&
                                 sh.boxes[0].x1 >= 0.999 && sh.boxes[0].y1 >= 0.999 && sh.boxes[0].z1 >= 0.999)
                                    ? 1
                                    : 0;
                        }
                        return s == 1;
                    };
                    if (live) {
                        static double itemAcc = 0.0;
                        itemAcc = std::min(itemAcc + static_cast<double>(dt), 0.15);
                        const auto itemSolid = [&](int x, int y, int z) {
                            bool loaded = false;
                            const uint16_t id = entityLevel.GetBlock(x, y, z, loaded);
                            return !loaded || (id != 0 && !LevelCollision::ShapeOf(id).Empty());
                        };
                        while (itemAcc >= 0.05) {
                            itemAcc -= 0.05;
                            live->Entities().StepItems(itemSolid);
                        }
                    }
                    pw.light = ein.light;
                    particle::DataWorld dataWorld;
                    dataWorld.solid = pw.solid;
                    dataWorld.light = pw.light;
                    if (live) {
                        for (const BedrockSession::ParticleEvent& pe : live->TakeParticles()) {
                            const glm::dvec3 at(pe.x, pe.y, pe.z);
                            if (pe.event == 0) {
                                if (player.Gliding() && pe.name.find("firework") != std::string::npos)
                                    continue;
                                if (!dataParticles.Spawn(pe.name, at, pe.molang))
                                    particles.SpawnNamed(pe.name, at);
                            } else if (pe.event == 2001 || pe.event == 2021 || pe.event == 16404) {
                                const int bx = static_cast<int>(std::floor(pe.x)), by = static_cast<int>(std::floor(pe.y)),
                                          bz = static_cast<int>(std::floor(pe.z));
                                const bool mine = lastLocalBreak.x == bx && lastLocalBreak.y == by && lastLocalBreak.z == bz &&
                                                  std::chrono::steady_clock::now() - lastLocalBreak.at < std::chrono::milliseconds(800);
                                if (!mine)
                                    particles.Destroy(bx, by, bz, BlockPalette::Instance().InternRuntime(pe.data));
                            } else {
                                particles.SpawnLevelEvent(pe.event, at, pe.data);
                            }
                        }
                    }
                    if (!live)
                        if (const char* pb = PERLIN_ENV("PERLIN_PREVIEW_BREAK")) {
                            static double burstClock = 0;
                            burstClock += dt;
                            int bx = 0, by = 0, bz = 0;
                            if (burstClock > 0.6 && std::sscanf(pb, "%d,%d,%d", &bx, &by, &bz) == 3) {
                                burstClock = 0;
                                bool loaded = false;
                                const uint16_t id = entityLevel.GetBlock(bx, by, bz, loaded);
                                if (loaded && id)
                                    particles.Destroy(bx, by, bz, id);
                            }
                        }
                    particleClock += std::min(dt, 0.25f);
                    ProfScope particleScope(8);
                    for (int steps = 0; particleClock >= 0.05 && steps < 5; steps++, particleClock -= 0.05)
                        particles.Tick(pw, glm::dvec3(camera.x, camera.y, camera.z));
                    if (particleClock > 0.2)
                        particleClock = 0;
                    {
                        static auto dataParticleCheck = std::chrono::steady_clock::now() - std::chrono::seconds(10);
                        if (std::chrono::steady_clock::now() - dataParticleCheck > std::chrono::seconds(2)) {
                            dataParticleCheck = std::chrono::steady_clock::now();
                            dataParticles.Configure(ServerPacks::Instance().Directories());
                        }
                        dataParticles.Update(std::min(dt, 0.1f), dataWorld);
                    }
                    particleQuads.clear();
                    particles.Collect(static_cast<float>(particleClock / 0.05), pw, particleQuads);
                    if (const int fireTile = levelRenderer->Blocks().TileContaining("fire_1"); fireTile >= 0) {
                        const int soulTile = levelRenderer->Blocks().TileContaining("soul_fire_1");
                        for (const entity::Entity& burner : entities) {
                            const bool selfBurning = burner.isSelf && hudState.burning;
                            if (!selfBurning && !(burner.flags & entity::flag::Onfire))
                                continue;
                            if ((burner.flags & entity::flag::FireImmune) || burner.dying || burner.isItem)
                                continue;
                            const glm::dvec3 at = burner.pos;
                            const float scale = burner.scale;
                            // blue flames when standing on soul soil/sand, matches the valley look
                            int tile = fireTile;
                            {
                                bool loaded = false;
                                const uint16_t below = live->NetLevel().GetBlock(
                                    static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y - 0.5)),
                                    static_cast<int>(std::floor(at.z)), loaded);
                                if (loaded && soulTile >= 0) {
                                    const std::string bn = BlockPalette::Instance().Name(below);
                                    if (bn == "soul_sand" || bn == "soul_soil")
                                        tile = soulTile;
                                }
                            }
                            for (int k = 0; k < 6; k++) {
                                const float a = static_cast<float>(k) * 1.0472f + 0.5f;
                                const float h = 0.15f + 0.3f * static_cast<float>(k % 3);
                                particle::Quad q;
                                q.centre = at + glm::dvec3(std::cos(a) * 0.3 * scale, (h + 0.35f * (k / 3)) * scale, std::sin(a) * 0.3 * scale);
                                q.half = 0.4f * scale;
                                q.terrainTile = tile;
                                particleQuads.push_back(q);
                            }
                        }
                    }
                    ein.particles = &particleQuads;
                    ein.dataParticles = &dataParticles;
                    ein.dataWorld = dataWorld;
                    ein.tilePixels = [this](int tile, std::vector<uint8_t>& rgba) { return levelRenderer->Blocks().TilePixels(tile, rgba); };
                }
                { ProfScope p(3); entityRenderer.Render(entities, ein); }
                {
                    WeatherRenderer::In wx;
                    wx.renderer = renderer.get();
                    wx.level = live ? &live->NetLevel() : previewLevel.get();
                    wx.tints = &levelRenderer->Tints();
                    wx.camera = glm::dvec3(camera.x, camera.y, camera.z);
                    wx.rain = rain;
                    wx.thunder = thunder;
                    wx.seconds = std::chrono::duration<double>(frameStart - startedAt).count();
                    wx.light = std::clamp(env.skyLightFactor, 0.0f, 1.0f);
                    wx.enabled = live != nullptr && input.dimension == Dimension::Overworld;
                    { ProfScope weatherScope(9); weather.Draw(wx); }
                    if (wx.enabled && rain > 0.1f) {
                        static double nextRainSound = 0.0;
                        if (wx.seconds >= nextRainSound) {
                            nextRainSound = wx.seconds + 3.2;
                            sounds.PlayNamed("ambient.weather.rain", wx.camera, std::min(1.0f, rain) * 0.4f, 1.0f, false);
                        }
                    }
                }
                {
                    static std::vector<SpecialBlock> specialBlocks;
                    static std::vector<entity::BlockEntityDraw> blockEntityDraws;
                    specialBlocks.clear();
                    blockEntityDraws.clear();
                    if (levelRenderer) {
                        levelRenderer->CollectSpecials(camera.x, camera.z, 64.0f, specialBlocks);
                        static const BlockEntityStore emptyStore;
                        entity::BuildBlockEntityDraws(specialBlocks, live ? live->BlockEntities() : emptyStore, blockEntityDraws);
                        entityRenderer.RenderBlockEntities(blockEntityDraws, ein);
                    }
                }
            }
            if ((live || preview) && (!replayCtl.Playing() || replayCtl.cameraMode == replay::CameraMode::Follow) && camera.mode == PerspectiveMode::FirstPerson && options.showHud &&
                hudState.mode != ui::GameMode::Spectator && !deathShown && ui::UiAssets::Instance().VanillaRoot().size()) {
                HandRenderer::FrameIn handFrame;
                handFrame.partial = partial;
                handFrame.yaw = camera.yaw - 90.0f;
                handFrame.pitch = -camera.pitch;
                {
                    const int sky = std::clamp(handSky, 0, 15);
                    int blk = std::clamp(handBlock, 0, 15);
                    if (!dyn.empty())
                        blk = std::max(blk, static_cast<int>(dynlight::LevelAt(dyn, glm::dvec3(camera.x, camera.y, camera.z))));
                    const uint8_t* px = params.lightmap + (static_cast<size_t>(sky) * kLightmapSize + blk) * 4;
                    handFrame.light = {px[0] / 255.0f, px[1] / 255.0f, px[2] / 255.0f};
                }
                handFrame.viewEffect = handViewEffect;
                handFrame.skins3d = options.skins3d;
                handFrame.hurt = damage.HurtFraction(partial);
                static LoadedSkin replayHandSkin;
                static uint64_t replayHandSerial = 0;
                const LoadedSkin* handSkin = session ? &session->Skin() : nullptr;
                if (session && replayCtl.Playing()) {
                    for (const auto& tp : session->PlayerList()) {
                        if (!tp.skin || tp.skin->skinRgba.empty() || tp.name != replayCtl.Meta().playerName)
                            continue;
                        if (replayHandSerial != tp.skin->serial) {
                            replayHandSerial = tp.skin->serial;
                            replayHandSkin = LoadedSkin{};
                            replayHandSkin.skinWidth = tp.skin->skinWidth;
                            replayHandSkin.skinHeight = tp.skin->skinHeight;
                            replayHandSkin.skinRgba = tp.skin->skinRgba;
                            replayHandSkin.slimArms = tp.skin->slim;
                            replayHandSkin.skinId = "replay-" + std::to_string(tp.skin->serial);
                        }
                        handSkin = &replayHandSkin;
                        break;
                    }
                }
                if (session && handSkin) {
                    handFrame.skin = handSkin;
                    handFrame.skinSerial = std::hash<std::string>{}(handSkin->skinId) * 2 + (handSkin->slimArms ? 1 : 0);
                    handFrame.slimArm = handSkin->slimArms;
                    handFrame.leftSleeve = options.skinParts.leftSleeve;
                    handFrame.rightSleeve = options.skinParts.rightSleeve;
                }
                { ProfScope p(4); handRenderer.Render(handFrame, options.hand); }
                {
                    int sw = 0, sh = 0;
                    renderer->FramebufferSize(sw, sh);
                    handRenderer.RenderScope(sw, sh, partial);
                }
                if (options.hand.bedrockRig && PERLIN_ENV("PERLIN_BEDROCK_RIG") && (fpSelf || previewEntitiesSpawned || true)) {
                    if (!fpSelf) {
                        fpSelf.emplace();
                        fpSelf->runtimeId = 0xFFFFFFFFFFFF0001ull;
                        fpSelf->type = "minecraft:player";
                        fpSelf->isPlayer = fpSelf->isSelf = true;
                        fpSelf->pos = {camera.x, camera.y - static_cast<double>(player.EyeOffset()), camera.z};
                        fpSelf->yaw = fpSelf->headYaw = camera.yaw - 90.0f;
                        fpSelf->pitch = -camera.pitch;
                        fpSelf->moveSerial = 1;
                    }
                    const HandRenderer::RigState rs = handRenderer.Rig(partial);
                    fpSelf->mainHand = rs.main;
                    fpSelf->offHand = options.hand.showOffhand ? rs.off : std::string();
                    if (options.hand.leftHanded)
                        std::swap(fpSelf->mainHand, fpSelf->offHand);
                    entity::EntityRenderer::FrameIn fin;
                    fin.time = std::chrono::duration<double>(frameStart - startedAt).count();
                    fin.dt = dt;
                    fin.camera = {camera.x, camera.y, camera.z};
                    fin.firstPerson = true;
                    fin.cameraYaw = camera.yaw - 90.0f;
                    fin.cameraPitch = -camera.pitch;
                    fin.worldTicks = worldTicks;
                    entity::EntityRenderer::FpState fps;
                    fps.attackTime = rs.attackTime;
                    fps.armHeight = rs.armHeight;
                    fps.offArmHeight = rs.offArmHeight;
                    fps.bob = options.hand.viewBobbing && options.hand.bobHand;
                    fps.using_ = rs.using_;
                    fps.blocking = rs.blocking;
                    fps.usedTicks = rs.usedTicks;
                    fps.viewEffect = handFrame.viewEffect;
                    fps.light = handFrame.light;
                    entityRenderer.RenderFirstPerson(*fpSelf, fin, fps);
                }
            }
            {
                static double hudRetry = 0;
                const double nowSeconds = std::chrono::duration<double>(frameStart - startedAt).count();
                const std::string vanillaRoot = options.bedrockSamplesDir + "/repo/resource_pack";
                if (!hud && nowSeconds >= hudRetry) {
                    hudRetry = nowSeconds + 2.0;
                    if (std::filesystem::exists(vanillaRoot + "/ui/hud_screen.json")) {
                        ui::UiAssets::Instance().Configure(vanillaRoot, &levelRenderer->Blocks());
                        auto candidate = std::make_unique<ui::Hud>();
                        if (candidate->Initialize(*renderer, vanillaRoot, paths::DataDir()))
                            hud = std::move(candidate);
                        else
                            candidate->Shutdown();
                    }
                }
                if (hud) {
                    int fbW = 0, fbH = 0;
                    renderer->FramebufferSize(fbW, fbH);
                    ProfScope hudScope(5);
                    hud->Update(hudState, fbW, fbH, options.guiScale);
                    if (touchEditing) {
                        if (!touchEditHidden) {
                            touchEditHidden = true;
                            hud->Overlay().ClearOverlay();
                        }
                    } else {
                        touchEditHidden = false;
                        UpdateModLayer(*hud);
                        UpdateServerLayer(*hud, live, fbW, fbH);
                    }
                    const bool titleUp = !touchEditing && UpdateTitleScreen(*hud, fbW, fbH);
                    const bool disconnectUp = !touchEditing && !titleUp && UpdateDisconnectScreen(*hud, fbW, fbH);
                    const bool loadingUp = !touchEditing && !titleUp && !disconnectUp && UpdateLoadingScreen(*hud, fbW, fbH);
                    if (!touchEditing && !titleUp && !disconnectUp && !loadingUp && replayCtl.Playing() && live && live->IsReplay()) {
                        if (exportJob.active)
                            ExportOverlay(*hud, fbW, fbH);
                        else
                            UpdateReplayScreen(*hud, live, fbW, fbH);
                    } else if (!touchEditing && !titleUp && !disconnectUp && !loadingUp) {
                        UpdateCreditsScreen(*hud, live, fbW, fbH);
                        if (!creditsShown) {
                            UpdateDeathScreen(*hud, live, hudState, fbW, fbH);
                            UpdateFormScreen(*hud, live, fbW, fbH);
                            UpdateInventoryScreen(*hud, live, hudState, fbW, fbH);
                            UpdatePauseScreen(*hud, live, fbW, fbH);
                            UpdateBedScreen(*hud, live, fbW, fbH);
                            UpdateChatClicks(*hud, live, fbW, fbH);
                        }
                    }
                    {
                        {
                            bool keyboard = chat.IsOpen();
                            if (titleUp)
                                keyboard = keyboard || title.page == ui::TitlePage::Edit || title.page == ui::TitlePage::Direct || title.page == ui::TitlePage::AddRealm ||
                                           title.page == ui::TitlePage::AccountName || title.page == ui::TitlePage::ReplayRename;
                            if (formShown && formModel.focus >= 0 && static_cast<size_t>(formModel.focus) < formModel.elements.size())
                                keyboard = keyboard || formModel.elements[static_cast<size_t>(formModel.focus)].kind == ui::FormElement::Kind::Input;
                            if (invShown && creativeTab == ui::kCreativeSearchTab)
                                keyboard = true;
                            this->input.SetKeyboard(keyboard);
                        }
                        const bool pressed = this->input.IsMouseDown(0);
                        const bool onButton = hud->Overlay().OverlayHoverIsButton();
                        if (pressed && !uiClickWasDown && onButton && !chat.IsOpen())
                        {
                            std::string clickName = "random.click";
                            float clickVolume = 1.0f, clickPitch = 1.0f;
                            hud->Overlay().OverlayHoverSound(clickName, clickVolume, clickPitch);
                            if (!clickName.empty())
                                sounds.PlayNamed(clickName, {camera.x, camera.y, camera.z}, clickVolume, clickPitch, false);
                        }
                        uiClickWasDown = pressed;
                    }
                    DrawModTint(fbW, fbH);
                    hud->Render();
                    DrawModGfx(fbW, fbH);
                    const bool creativeUi = hudState.mode == ui::GameMode::Creative || PERLIN_ENV("PERLIN_PREVIEW_CREATIVE");
                    if (invShown && dollSelf && !invWorkbench && !invHorse && !invChest && (!creativeUi || creativeDollVisible)) {
                        const int scale = hud->OverlayScale();
                        const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
                        const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));
                        const bool creativeDoll = creativeUi;
                        const float px = static_cast<float>(creativeDoll ? (guiW - ui::kCreativeW) / 2 : (guiW - ui::kInvPanelW) / 2 + (invBook ? 77 : 0)),
                                    py = static_cast<float>(creativeDoll ? (guiH - ui::kCreativeH) / 2 : (guiH - ui::kInvPanelH) / 2);
                        const float centerX = creativeDoll ? px + 89.0f : px + 51.0f, centerY = creativeDoll ? py + 27.5f : py + 43.0f;
                        const float f = std::atan((centerX - invMouseX) / 40.0f), g = std::atan((centerY - invMouseY) / 40.0f);
                        entity::Entity doll = *dollSelf;
                        doll.runtimeId = 0xFFFFFFFFFFFF0002ull;
                        doll.pos = {0.0, 0.0, 0.0};
                        doll.yaw = f * 20.0f * 1.0f;
                        doll.headYaw = f * 40.0f;
                        doll.pitch = -g * 20.0f;
                        doll.moveSerial = 1;
                        doll.isSelf = true;
                        doll.flags &= ~entity::flag::Sneaking;
                        entity::EntityRenderer::FrameIn din;
                        din.time = std::chrono::duration<double>(frameStart - startedAt).count();
                        din.dt = dt;
                        din.firstPerson = false;
                        din.camera = {0, 0, 0};
                        const float dollScale = creativeDoll ? 20.0f : 30.0f;
                        const int clip[4] = {static_cast<int>((creativeDoll ? px + 73 : px + 26) * scale), static_cast<int>((creativeDoll ? py + 6 : py + 8) * scale),
                                             (creativeDoll ? 32 : 49) * scale, (creativeDoll ? 43 : 70) * scale};
                        entityRenderer.RenderDoll(doll, din, (creativeDoll ? px + 89.0f : px + 50.5f) * scale,
                                                  (centerY + (1.8f / 2.0f + 0.0625f) * dollScale) * scale, dollScale * scale, clip);
                    }
                    if (invShown && invHorse && live) {
                        std::vector<entity::Entity> all;
                        live->Entities().Snapshot(all);
                        const int64_t mountRuntime = live->GetInventory().horseEntity;
                        const entity::Entity* found = nullptr;
                        for (const entity::Entity& e : all)
                            if (static_cast<int64_t>(e.runtimeId) == mountRuntime || (live->RidingUid() != 0 && e.uniqueId == live->RidingUid())) {
                                found = &e;
                                break;
                            }
                        if (found) {
                            const int scale = hud->OverlayScale();
                            const int guiW = static_cast<int>(std::ceil(static_cast<float>(fbW) / static_cast<float>(scale)));
                            const int guiH = static_cast<int>(std::ceil(static_cast<float>(fbH) / static_cast<float>(scale)));
                            const float px = static_cast<float>((guiW - ui::kInvPanelW) / 2), py = static_cast<float>((guiH - ui::kInvPanelH) / 2);
                            entity::Entity doll = *found;
                            doll.runtimeId = 0xFFFFFFFFFFFF0003ull;
                            doll.pos = {0.0, 0.0, 0.0};
                            doll.vehicleUid = 0;
                            doll.flags &= ~(entity::flag::Riding | entity::flag::Sneaking);
                            const float f = std::atan((px + 52.0f - invMouseX) / 40.0f), g = std::atan((py + 43.0f - invMouseY) / 40.0f);
                            doll.yaw = 0.0f + f * 20.0f;
                            doll.headYaw = f * 40.0f;
                            doll.pitch = -g * 20.0f;
                            doll.moveSerial = 1;
                            entity::EntityRenderer::FrameIn din;
                            din.time = std::chrono::duration<double>(frameStart - startedAt).count();
                            din.dt = dt;
                            din.firstPerson = false;
                            din.camera = {0, 0, 0};
                            const int clip[4] = {static_cast<int>((px + 26) * scale), static_cast<int>((py + 17) * scale), 52 * scale, 53 * scale};
                            entityRenderer.RenderDoll(doll, din, (px + 52.0f) * scale, (py + 65.0f) * scale, 22.0f * scale, clip);
                        }
                    }
                    float cx, cy, cw, ch;
                    if (options.showHud && !replayCtl.Playing() && !deathShown && !formShown && !invShown && !pauseShown && !disconnectShown && !loadingShown && !titleShown && !creditsShown && (camera.mode == PerspectiveMode::FirstPerson || options.thirdPersonCrosshair) &&
                        hudState.mode != ui::GameMode::Spectator &&
                        hud->Cursor(cx, cy, cw, ch))
                        renderer->DrawHudCursor(cx, cy, cw, ch);
                }
            }
            {
                TouchState touchState;
                touchState.flying = player.Flying();
                touchState.inWater = player.InWater() && !player.State().onGround;
                touchState.riding = live && live->RidingUid() != 0;
                touchState.editing = touchEditing;
                touchState.hudScale = hud ? hud->Scale() : 0;
                touchState.scale = options.touchScale;
                touchState.opacity = options.touchOpacity;
                touchState.sensitivity = options.touchSensitivity;
                touchState.layout = options.touchLayout;
                for (size_t a = 0; a < kActionCount; a++)
                    touchState.bindings[a] = options.GetKey(static_cast<Action>(a));
                platform->SetTouchState(touchState);
                std::map<std::string, TouchPlace> edited;
                if (platform->TakeTouchLayout(edited)) {
                    options.touchLayout = edited;
                    options.Save();
                    touchEditing = false;
                    optionsSignature.clear();
                }
                const bool previewTouch = preview && PERLIN_ENV("PERLIN_PREVIEW_TOUCH");
                if (previewTouch) {
                    static TouchControls previewControls;
                    previewControls.SetViewport(width, height);
                    previewControls.SetState(touchState);
                    previewControls.Draw(*renderer, true);
                } else if (touchEditing || (!deathShown && !pauseShown && !invShown && !disconnectShown && !loadingShown && !titleShown)) {
                    platform->DrawTouchOverlay(*renderer);
                }
            }
            if (inputManager.Pressed(Action::Screenshot))
                TakeScreenshot();
            if (exportJob.active)
                ExportPostRender();
            PublishControl();
            { ProfScope p(6); renderer->EndFrame(); }
            if (!exportJob.active) {
                const auto realEnd = Clock::now();
                const double work = std::chrono::duration<double, std::milli>(realEnd - frameStart).count() - fgPacer.TakeWaitedMs();
                fgPacer.RealFrame(realEnd, fgActive, work);
                if (fgActive > 1) {
                    const auto idleUntil = options.maxFps > 0 ? frameStart + std::chrono::microseconds(1000000LL * fgActive / options.maxFps) : realEnd;
                    frames += fgPacer.Pump(FrameGenPacer::Stage::AfterPresent, !options.vsync, idleUntil,
                                           [&](int i, int n) { return renderer->PresentGenerated(i, n); });
                }
            }
            if (gProfile.enabled) {
                const double total = std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count();
                gProfile.ms[7] += total;
                gProfile.last[7] = total;
                gProfile.last[0] = std::max(gProfile.last[0], 0.0);
                for (int i = 0; i < FrameProfile::kCount; i++) {
                    gProfile.worst[i] = std::max(gProfile.worst[i], gProfile.last[i]);
                    gProfile.last[i] = 0;
                }
                gProfile.frameTimes.push_back(total);
                if (++gProfile.frames >= 240) {
                    std::sort(gProfile.frameTimes.begin(), gProfile.frameTimes.end());
                    const size_t n = gProfile.frameTimes.size();
                    const double p99 = gProfile.frameTimes[std::min(n - 1, static_cast<size_t>(n * 0.99))];
                    std::cout << "[profile] avg ms:";
                    for (int i = 0; i < 10; i++)
                        if (*gProfile.names[i])
                            std::cout << ' ' << gProfile.names[i] << '=' << gProfile.ms[i] / gProfile.frames;
                    std::cout << " | worst ms:";
                    for (int i = 0; i < 10; i++)
                        if (*gProfile.names[i])
                            std::cout << ' ' << gProfile.names[i] << '=' << gProfile.worst[i];
                    std::cout << " | p99 frame=" << p99 << " (" << (p99 > 0 ? 1000.0 / p99 : 0.0) << " fps 1% low)" << std::endl;
                    for (double& m : gProfile.ms)
                        m = 0;
                    for (double& m : gProfile.worst)
                        m = 0;
                    gProfile.frameTimes.clear();
                    gProfile.frames = 0;
                }
            }

            if (frameStart - animationAt >= kAnimationStep) {
                animationAt = frameStart;
                levelRenderer->TickAnimation(++animationStep);
            }

            frames++;
            const auto now = Clock::now();
            if (now - titleAt >= kTitleInterval) {
                const double seconds = std::chrono::duration<double>(now - titleAt).count();
                fpsShown = static_cast<int>(frames / seconds + 0.5);
                UpdateTitle(fpsShown);
                titleAt = now;
                frames = 0;
            }

#ifndef __EMSCRIPTEN__
            {
                // fullscreen: the option is the intent; the window can also change on its own (os shortcut), so adopt that once settled
                static int appliedFullscreen = -1;
                static Clock::time_point fullscreenSettle{};
                if (inputManager.Pressed(Action::Fullscreen)) {
                    options.fullscreen = !options.fullscreen;
                    options.Save();
                }
                const bool windowFullscreen = platform->IsFullscreen();
                const auto fsNow = Clock::now();
                if (appliedFullscreen < 0 || static_cast<bool>(appliedFullscreen) != options.fullscreen) {
                    if (windowFullscreen != options.fullscreen)
                        platform->SetFullscreen(options.fullscreen);
                    appliedFullscreen = options.fullscreen;
                    fullscreenSettle = fsNow + std::chrono::milliseconds(1500);
                } else if (fsNow >= fullscreenSettle && windowFullscreen != options.fullscreen) {
                    options.fullscreen = windowFullscreen;
                    appliedFullscreen = windowFullscreen;
                    options.Save();
                }
            }
            const bool wantVsync = options.vsync && !exportJob.active;
            if (appliedVsync != wantVsync) {
                appliedVsync = wantVsync;
                renderer->SetVsync(appliedVsync);
            }
            {
                static bool background = false;
                static int backgroundCheck = 0;
                if (exportJob.active) {
                    background = false;
                } else if (++backgroundCheck >= 30) {
                    backgroundCheck = 0;
                    background = ProcessInBackground();
                }
                if (background) {
                    const auto target = frameStart + std::chrono::microseconds(1000000 / 60);
                    if (Clock::now() < target)
                        std::this_thread::sleep_until(target);
                }
            }
            const int fpsCap = ModFrameLimit(options.maxFps);
            if (fpsCap > 0 && !exportJob.active)
                FrameGenPacer::SleepUntil(frameStart + std::chrono::microseconds(1000000LL * std::max(1, fgActive) / fpsCap));
#endif
        };

        PERLIN_WEB_PERSIST const auto teardown = [&] {
            audioEngine.Close();
            StopSampleProfiler();
            entityRenderer.Shutdown();
            handRenderer.Shutdown();
            if (hud)
                hud->Shutdown();
            hud.reset();
            levelRenderer.reset();
            renderer->Shutdown();
        };

#ifdef __EMSCRIPTEN__
        RunWebMainLoop([&] { return renderRunning && platform->isRunning ? (frameStep(), true) : false; }, teardown);
#else
        while (renderRunning && platform->isRunning)
            frameStep();
        if (exportJob.active)
            ExportFinish(true, "");
        if (replayCtl.Playing()) {
            SaveTimeline();
            if (replayUi.dirty) {
                std::string saveError;
                replayCtl.SaveMarkers(saveError);
            }
        }
        teardown();
#endif
    }
}
