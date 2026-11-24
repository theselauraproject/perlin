#pragma once
#include "client/features/debug/DebugOverlay.hpp"
#include "client/ui/HudAnimation.hpp"
#include "client/systems/renderer/FrameLoop.hpp"
#include "client/features/interaction/PlayerInteraction.hpp"
#include "client/features/worldview/EntityPass.hpp"
#include "client/features/worldview/FrameParamsBuilder.hpp"
#include "client/features/worldview/GameTick.hpp"
#include "client/features/worldview/LevelPass.hpp"
#include "client/features/worldview/ViewEnvironment.hpp"
#include "client/systems/renderer/LightBandCache.hpp"
#include "client/platform/Application.hpp"
#include "client/platform/LaunchArgs.hpp"
#include "client/application/AppServices.hpp"
#include "client/services/discord/DiscordRpc.hpp"
#include "client/systems/camera/Camera.hpp"
#include "client/application/ControlBridge.hpp"
#include "client/systems/renderer/FrameGenPacer.hpp"
#include "client/systems/config/Options.hpp"
#include "client/screen/OverlayScreen.hpp"
#include "client/screen/ScreenHost.hpp"
#include "client/systems/resources/ServerPacks.hpp"
#include "client/screen/ScreenManager.hpp"
#include "client/screen/screens/ScreenSupport.hpp"
#include "client/systems/audio/Capture.hpp"
#include "client/systems/audio/MovementSounds.hpp"
#include "client/systems/audio/SoundSystem.hpp"
#include "client/features/audio/AudioDirector.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "client/systems/entity/EntityRenderer.hpp"
#include "client/systems/hand/HandRenderer.hpp"
#include "client/systems/input/Input.hpp"
#include "client/systems/input/InputManager.hpp"
#include "client/systems/mods/ModManager.hpp"
#include "client/features/mods/ModHost.hpp"
#include "client/application/ModWindow.hpp"
#include "client/systems/renderer/gpu/ModGpuHub.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/particle/DataParticles.hpp"
#include "client/systems/particle/ParticleSystem.hpp"
#include "client/systems/player/HeldBuild.hpp"
#include "client/systems/player/Interaction.hpp"
#include "client/systems/player/LocalPlayer.hpp"
#include "client/systems/renderer/Fog.hpp"
#include "client/systems/renderer/FogDefs.hpp"
#include "client/systems/renderer/LevelRenderer.hpp"
#include "client/systems/gfx/Lightmap.hpp"
#include "client/systems/gfx/RenderContext.hpp"
#include "client/systems/renderer/WeatherRenderer.hpp"
#include "client/features/packs/PackManager.hpp"
#include "client/features/replay/Studio.hpp"
#include "client/features/servers/ServerBrowser.hpp"
#include "client/application/ReplayHostAdapter.hpp"
#include "client/screen/screens/ReplayLibrary.hpp"
#include "client/systems/replay/ReplayController.hpp"
#include "client/systems/replay/Timeline.hpp"
#include "client/systems/diagnostics/BenchmarkDriver.hpp"
#include "client/systems/resources/PackInfo.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/screen/screens/BedScreen.hpp"
#include "client/screen/screens/CreditsScreen.hpp"
#include "client/screen/screens/DeathScreen.hpp"
#include "client/screen/screens/DisconnectScreen.hpp"
#include "client/screen/screens/PauseScreen.hpp"
#include "client/services/update/Updater.hpp"
#include "client/screen/screens/AssetDownloadScreen.hpp"
#include "client/screen/screens/ChestScreen.hpp"
#include "client/screen/screens/FormScreen.hpp"
#include "client/ui/Hud.hpp"
#include "client/screen/screens/InventoryScreen.hpp"
#include "client/screen/screens/LoadingScreen.hpp"
#include "client/systems/multiplayer/PackDownloader.hpp"
#include "client/ui/PanoramaBackdrop.hpp"
#include "client/screen/screens/HudScreen.hpp"
#include "client/screen/screens/TitleScreen.hpp"
#include "client/screen/screens/AssetDownloadController.hpp"
#include "client/screen/screens/DisconnectController.hpp"
#include "client/screen/screens/LoadingController.hpp"
#include "client/screen/screens/TitleController.hpp"
#include "client/application/ConnectionPortsImpl.hpp"
#include "client/application/TitlePortsImpl.hpp"
#include "network/FriendWorld.hpp"
#include "network/LanDiscovery.hpp"
#include "util/ModSocket.hpp"
#include "util/TaskGroup.hpp"
#include "util/ThreadPool.hpp"
#ifdef PERLIN_SINGLEPLAYER
#include "client/features/singleplayer/EmbeddedServer.hpp"
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class AppPlatform;

namespace perlin {
    struct FrameContext;
    class Perlin final : public Application {
    public:
        explicit Perlin(LaunchArgs args);
        ~Perlin() override;

        bool Initialize(AppPlatform* platform) override;
        void Shutdown() override;

        Input& GetInput() override { return input; }
        void OnBackground() override;

    private:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        bool AcceptEula();
        void BindClipboard();
        void NotifyResourcesReloaded();
        void PlatformFramebufferSize(int& w, int& h);
        void PlatformSetClipboard(const std::string& text);
        void LoadConfiguration();
        void StartMods();
        void LoadAccounts();
        bool StartControl();
        void StartInitialScreen();
        void StopRenderThread();
        void StopServices();
        void Connect();
        void ResetLoadingForConnect();
        bool ResolveConnectTarget(std::string& host, uint16_t& port);
        void ConfigureSession();
        void LoadSessionSkin();
        void AttachSessionAccount();
        void StartConnectWorker(const std::string& host, uint16_t port, uint64_t gen);
        void StartAssetDownloads();
        void SetStatusHint(const std::string& text);
        void CaptureCursor(bool captured);
        void OpenUrl(const std::string& url);
        bool PickFile(const char* kind);
        void TakeScreenshot();
        void UpdateTitle(int fps);
        void VibrateBlockBroken();
        LocalBreakState LocalBreak() const;

        void RenderThreadMain();
        void InitRenderThread();
        void RecordFrameStats();
        void FrameStep();
        void TearDownRenderThread();
        void BeginFrame(FrameContext& f);
        void ImportDroppedFiles();
        void UpdateLook(FrameContext& f);
        void UpdateCameraEffects();
        void RestorePlayerCamera();
        void TrackDamage();
        void UpdateSession(FrameContext& f);
        void UpdateRiding(BedrockSession& live, const std::vector<entity::Entity>& entities);
        void UpdateViewSettings(FrameContext& f);
        void ApplyRendererSwitch();
        void UpdateChat(FrameContext& f);
        void UpdateHudAndScreens(FrameContext& f);
        void UpdateTouch(FrameContext& f);
        void EndFrame(FrameContext& f);
        void PaceFrame(FrameContext& f);

        void RegisterScreens();
        void UpdateServerLayer(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);
        void UpdateChatClicks(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);


        const Binding& BindingOf(Action a) const {
            return pause.optionsUi.bindDevice == 1 ? options.GetPadKey(a) : options.GetKey(a);
        }
        void SetBindingOf(Action a, const Binding& b) {
            if (pause.optionsUi.bindDevice == 1)
                options.SetPadKey(a, b);
            else
                options.SetKey(a, b);
        }

        bool WorldCovered() const;
        void LeaveToServerList(ui::Hud& hud);
        int FrameGenMultiplier();

        void ApplyActiveAccount();
        void PrewarmSignIn();
        services::AccountService& Accounts() { return *appServices.accounts; }
        void BuildDiscordService();
        void UpdateDiscord();
        void JoinSingleplayer(const std::string& worldDir = {}, uint64_t seed = 0, int gameMode = -1,
                              bool persistent = true);
        void StartBenchmark(bench::Settings settings);
        void UpdateBenchmark(FrameContext& f);
        void FinishBenchmark();



        ControlBridge::Context ControlContext() {
            return {input, inputManager, player, camera, platform, session.get(), sessionReady.load(), levelRenderer.get(), renderer.get()};
        }

        LaunchArgs args;
        AppPlatform* platform = nullptr;
        Options options;
        std::string runningRenderer, notifiedRenderer;
        Input input;
        InputManager inputManager{input, options};
        ControlBridge controlBridge;

        std::unique_ptr<RenderContext> renderer;
        std::unique_ptr<LevelRenderer> levelRenderer;
        std::unique_ptr<ui::Hud> hud;
        ui::HudState hudState;
        HandRenderer handRenderer;
        entity::EntityRenderer entityRenderer;
        WeatherRenderer weather;
        particle::ParticleSystem particles;
        particle::DataParticles dataParticles;
        const Level* particleLevel = nullptr;
        FogDefs fogDefs;
        audio::AudioEngine audioEngine;
        audio::SoundSystem sounds{audioEngine};
        std::unique_ptr<ThreadPool> workers;
        std::thread renderThread;
        std::atomic<bool> renderRunning{false};
        std::atomic<bool> renderReady{false};
        FrameGenPacer fgPacer;
        int fgActive = 1;

        Camera camera;
        LocalPlayer player;
        BlockLightFlicker blockLight;
        double tickDebt = 0.0;
        float rainLevel = 0.0f, thunderLevel = 0.0f;
        float previousRainLevel = 0.0f, previousThunderLevel = 0.0f;

        bool cameraDetached = false;
        bool cameraModeForced = false, cameraOverridden = false;
        bool modDetachForced = false;
        PerspectiveMode modDetachSavedMode = PerspectiveMode::FirstPerson;
        std::chrono::steady_clock::time_point modDetachClock;
        void UpdateServerCameraEffects();
        float cameraPlayerView[5] = {0, 0, 0, 0, 0};
        PerspectiveMode cameraSavedMode = PerspectiveMode::FirstPerson;

        std::unique_ptr<FrameLoop> loop;

        ui::HudAnimation hudAnim;
        float hudPartial = 0.0f;
        int previewSlot = 0;
        bool uiClickWasDown = false;
        audio::AudioDirector audioDirector{audio::AudioDirector::Deps{
            options, sounds, audioEngine, camera, player, hudState, BlockPalette::Instance(),
            [this](const BedrockSession::SoundRequest& r) {
                modHost.NoteModSound(r.name, r.x, r.y, r.z, r.volume, r.pitch);
            },
            [] { return ui::UiAssets::Instance().VanillaRoot(); },
            [] { return ServerPacks::Instance().Directories(); },
            [] { return std::rand(); }}};
        interaction::Player interaction{interaction::Player::Deps{
            options, BlockPalette::Instance(), ServerBlocks::Instance(), input, inputManager, camera, player, hudState,
            entityRenderer, handRenderer, sounds, audioDirector, particles, levelRenderer, weather, rainLevel,
            previousRainLevel, previewSlot,
            [this] { return replayCtl.Playing(); },
            [this](abi::InteractKind kind, int x, int y, int z, int face, uint64_t entity) {
                return modHost.DispatchModInteract(kind, x, y, z, face, entity);
            },
            [this] { VibrateBlockBroken(); }}};
        ui::ChatLog chat;
        debug::Overlay debugOverlay{debug::Overlay::Deps{
            options, BlockPalette::Instance(), ui::UiAssets::Instance(), input, inputManager, camera, chat, hudState, entityRenderer, sounds, levelRenderer, renderer, hud,
            interaction.aimHit, hudAnim.ticks,
            [this](int& w, int& h) { PlatformFramebufferSize(w, h); },
            [this](const std::string& text) { PlatformSetClipboard(text); },
            [this](std::vector<std::string>& l, std::vector<std::string>& r) { modHost.ApplyModDebugLines(l, r); }}};
        TimePoint chatOpenedAt;
        TimePoint playersAt;
        bool chatLeftDown = false;

        entity::EntityTracker previewEntities;
        std::optional<entity::Entity> dollSelf;

        screen::Manager screens;
        screen::Host screenHost{screens, input, inputManager, options, [this](bool captured) { CaptureCursor(captured); }, [] { return ServerPacks::Instance().Directories(); }, chat, hudState, [this](int slot, bool right, bool shift) { return modHost.DispatchModSlotClick(slot, right, shift); }, debugOverlay.state.advancedTooltips};
        screen::CallbackScreen& replayScreen = screens.Emplace<screen::CallbackScreen>(screen::Id::Replay, screen::Traits{false});
        screen::CallbackScreen& chatScreen = screens.Emplace<screen::CallbackScreen>(screen::Id::Chat, screen::Traits{false});

        struct ServerLayer {
            bool shown = false;
            std::string signature;
        } serverLayer;
        struct Toast {
            std::string text, title, message;
            TimePoint at;
        } toast;

        ShaderPackCache shaderPackCache;
        packs::PackManager packManager{packs::PackManager::Deps{
            options, ui::UiAssets::Instance(), TexturePack::Instance(), ServerPacks::Instance(), ServerBlocks::Instance(),
            ui::JsonScreens::Instance(), ui::McFont::Instance(), ShaderPack::Instance(), BlockPalette::Instance(),
            LightRegistry::Instance(), renderer, levelRenderer, hud, entityRenderer, handRenderer, hudState, audioEngine, sounds,
            chat, hudAnim.ticks, shaderPackCache,
            [this] { interaction.StopHeldBuild(); },
            [this] {
                std::vector<std::string> stack;
                std::lock_guard lock(modHost.modHooks.mutex);
                for (const auto& pack : modHost.modHooks.packs)
                    stack.push_back(pack.second);
                return stack;
            },
            [this] { NotifyResourcesReloaded(); },
            [this] {
                form.ui.Reset();
                inventory.chestUi.Reset();
                form.model = {};
                form.engine = form.engineFailed = false;
                form.signature.clear();
            }}};
        screen::BedScreen& bed = screens.Emplace<screen::BedScreen>(screen::Id::Bed, screen::Traits{false}, screenHost);
        BedrockSession::ScreenAnimation screenAnimation;

        screen::CreditsScreen& credits = screens.Emplace<screen::CreditsScreen>(screen::Id::Credits, screen::Traits{true}, screenHost);

        screen::DeathScreen& death = screens.Emplace<screen::DeathScreen>(screen::Id::Death, screen::Traits{true}, screenHost);

        screen::FormScreen& form = screens.Emplace<screen::FormScreen>(screen::Id::Form, screen::Traits{true}, screenHost);

        screen::InventoryScreen& inventory = screens.Emplace<screen::InventoryScreen>(screen::Id::Inventory, screen::Traits{true}, screenHost);


        std::atomic<bool> fontsDownloaded{false};

        screen::TitleScreen& titleScreen = screens.Emplace<screen::TitleScreen>(screen::Id::Title, screen::Traits{true});
        ui::TitleModel title;
        ui::PanoramaBackdrop panorama{options, renderer, ui::UiAssets::Instance(), TexturePack::Instance(), ServerPacks::Instance()};

        screen::AssetDownloadScreen& asset = screens.Emplace<screen::AssetDownloadScreen>(screen::Id::Asset, screen::Traits{false});

        screen::LoadingScreen& loading = screens.Emplace<screen::LoadingScreen>(screen::Id::Loading, screen::Traits{true});

        screen::DisconnectScreen& disconnect = screens.Emplace<screen::DisconnectScreen>(screen::Id::Disconnect, screen::Traits{true});


        AppServices appServices;


        replay::ReplayController replayCtl;
        screen::PauseHost pauseHost{
            options, renderer, runningRenderer, packManager.JavaPackPending(), replayCtl, titleScreen, mods::ModManager::Instance(),
            modUi,
            [this](const std::string& name) { modHost.ToggleMod(name); },
#ifdef PERLIN_SINGLEPLAYER
            [this] { return benchmark != nullptr; },
            [this](const bench::Settings& settings) { StartBenchmark(settings); },
            [this](bool noAsk) { versionOverride.dontAsk = noAsk; },
#else
            [] { return false; },
            [](const bench::Settings&) {},
            [](bool) {},
#endif
            [this](bool start) { replayStudio.ToggleRecording(start); },
            [this](ui::Hud& hud) { LeaveToServerList(hud); },
            [this] {
                if (!pause.packUi.pendingInit)
                    return;
                pause.packUi.pendingInit = false;
                packManager.CommitTexturePacks(pause.packUi.pending);
            },
            [this] { packManager.ApplyTexturePacks(); },
            [this] { packManager.ApplyShaderPack(); },
            [] { return discord::Supported(); },
            [] { return update::Supported(); },
            [this](const std::string& url) { OpenUrl(url); },
            [this](const char* kind) { return PickFile(kind); },
            shaderPackCache,
            [this] { return Accounts().ActiveId(); },
            [this](const std::string& id, const std::string& skin, const std::string& cape) {
                Accounts().SetSkin(id, skin, cape);
            }};
        screen::PauseScreen& pause = screens.Emplace<screen::PauseScreen>(screen::Id::Pause, screen::Traits{true}, screenHost, pauseHost);
        std::unique_ptr<BedrockSession> session;
        std::vector<std::unique_ptr<BedrockSession>> retiredSessions;
#ifdef PERLIN_SINGLEPLAYER
        std::unique_ptr<EmbeddedServer> singleplayerServer;
#endif
        std::unique_ptr<bench::Driver> benchmark;
        bool benchmarkCliStarted = false;
        screen::ConnectionPorts::VersionOverride versionOverride;
        std::unique_ptr<Transport> pendingSingleplayer;
        bool inSingleplayer = false;
        services::AccountService::ConnectLogin connectLogin;
        std::mutex statusMutex;
        std::string statusHint;
        bool onlineThisRun = false;
        std::atomic<bool> sessionReady{false};
        std::atomic<uint64_t> connectGeneration{0};
        std::atomic<int> connectsInFlight{0};
        ModWindow modWindow{platform};
        mods::UiRequest modUi;
        ModHost modHost{ModHost::Deps{
            mods::ModManager::Instance(), modWindow, modUi, gpu::ModGpuHub::Instance(),
            camera, options, hudState, levelRenderer, handRenderer, input, inputManager, previewEntities, player, chat,
            entityRenderer, sounds, audioEngine, particles, renderer, session, tasks, rainLevel, thunderLevel,
            [this] {
                return ModHost::ScreenFlags{death.IsOpen(), inventory.IsOpen(), inventory.workbench, pause.IsOpen(),
                                            disconnect.IsOpen(), replayStudio.MenuOpen(), pause.view};
            },
            [this] { return ModHost::FrameStats{debugOverlay.state.fps, debugOverlay.state.on}; },
            [this] { return hudAnim.ticks; },
            [this] { return replayStudio.HidesHud(); },
            [this] { return replayCtl.Playing(); },
            [this] {
                pause.packUi.foundAt = {};
                packManager.MarkTexturesStale();
            },
            [this] { TakeScreenshot(); },
            [this] { packManager.ApplyTexturePacks(); },
            [this] { packManager.ApplyShaderPack(); },
            [this](const std::string& address) { serverBrowser.JoinServer(address); },
            [this] { return LocalBreak(); }}};
        struct FrameLocals {
            bool anyShotDone = false;
            int anyShotFrames = 0;
            float glideLostFor = 0.0f;
            bool wasSeated = false;
            entity::Entity lastSeat;
            Clock::time_point lastTrim = Clock::now();
            int appliedFullscreen = -1;
            Clock::time_point fullscreenSettle{};
            Clock::time_point minimizedSince{};
            bool trimmed = false;
        } frameLocals;
        TaskGroup tasks;
        ReplayHostAdapter replayHost{ReplayHostAdapter::Deps{
            platform, screens, pause, loading, titleScreen, title,
            [this](const std::string& text) { SetStatusHint(text); },
            [this](ui::Hud& h) { LeaveToServerList(h); }}};
        replay::Studio replayStudio{replay::Studio::Deps{
            options, ui::UiAssets::Instance(), ServerPacks::Instance(), input, inputManager, camera, player, chat, hudAnim.ticks, entityRenderer, sounds, levelRenderer,
            renderer, hud, particleLevel, session, retiredSessions, sessionReady, connectGeneration, replayCtl,
            replayHost}};
        servers::ServerBrowser serverBrowser{servers::ServerBrowser::Deps{
            options, ui::UiAssets::Instance(), workers, tasks,
            [this] { return Accounts().ActiveIsMicrosoft(); },
            [this] { return Accounts().ActiveAuthPath(); },
            [this](const std::string& text) { SetStatusHint(text); },
            [this] { titleScreen.active = false; },
            [this] { Connect(); }}};
        screen::ReplayLibrary replayLibrary{screen::ReplayLibrary::Deps{
            title, [this] { titleScreen.signature.clear(); },
            [this](const std::string& url) { OpenUrl(url); },
            [this](const std::string& file) { replayStudio.StartPlayback(file); },
            [this](const std::string& text) { chat.AddMessage(text, hudAnim.ticks); }}};
        TitlePortsImpl titlePorts{TitlePortsImpl::Deps{
            serverBrowser, appServices, packManager, connectsInFlight, ServerPacks::Instance(),
            [this] { ApplyActiveAccount(); },
            [this] { JoinSingleplayer(); },
            [this](ui::Hud& hud, int fbW, int fbH) { return assetController.Update(hud, fbW, fbH); },
            [this] { platform->isRunning = false; }}};
        screen::AssetDownloadController assetController{screen::AssetDownloadController::Deps{
            asset, titleScreen, screens, input, options, connectionPorts}};
        screen::LoadingController loadingController{screen::LoadingController::Deps{
            loading, titleScreen, screens, input, options, chat, panorama, connectionPorts}};
        screen::DisconnectController disconnectController{screen::DisconnectController::Deps{
            disconnect, screens, input, options, chat, panorama, connectionPorts}};
        screen::TitleController titleController{screen::TitleController::Deps{
            titleScreen, title, options, input, inputManager, panorama, screens, chat, replayLibrary,
            ui::UiAssets::Instance(), ui::JsonScreens::Instance(), titlePorts}};
        ConnectionPortsImpl connectionPorts{ConnectionPortsImpl::Deps{
            session, levelRenderer, sessionReady, statusMutex, statusHint, onlineThisRun, inSingleplayer, replayCtl,
            serverBrowser, versionOverride,
            [this] {
                std::lock_guard lock(modHost.modHooks.mutex);
                return !modHost.modHooks.loadingHidden.empty();
            },
            [this] { Connect(); },
            [this](ui::Hud& hud) { LeaveToServerList(hud); },
            [this] { StartAssetDownloads(); },
            [this] {
                screens.CloseAll({&pause, &inventory, &death});
                pause.signature.clear();
                death.signature.clear();
            },
            [this] {
                screen::ConnectionPorts::SingleplayerSetup out;
#ifdef PERLIN_SINGLEPLAYER
                if (singleplayerServer) {
                    const EmbeddedServer::SetupStatus s = singleplayerServer->Status();
                    out.starting = s.starting;
                    out.text = s.text;
                    out.progress = s.progress;
                }
#endif
                return out;
            }}};
        LightBandCache lightCache;
        worldview::ViewEnvironment view{worldview::ViewEnvironment::Deps{
            modHost, BlockPalette::Instance(), lightCache, replayStudio, levelRenderer, camera, loop, debugOverlay, replayCtl, session,
            [this] {
                loading.done = false;
                loading.startAt = {};
                loading.readyArmed = false;
                loading.chunkBase = session->ChunksReceived();
            }}};
        worldview::GameTick gameTick{worldview::GameTick::Deps{
            modHost, input, inputManager, hudState, handRenderer, sounds, camera, player, blockLight, tickDebt, rainLevel,
            previousRainLevel, thunderLevel, previousThunderLevel, loop, [this] { hudScreen.TickHud(); }}};
        screen::HudScreen hudScreen{screen::HudScreen::Deps{
            ui::UiAssets::Instance(),
            view.headUnderwater,
            hudAnim,
            options,
            inputManager,
            hud,
            hudState,
            camera,
            loop,
            hudPartial,
            previewSlot,
            chat,
            form,
            inventory,
            pause,
            session,
            [this](ui::HudState& s) { modHost.ApplyModSlotOverlays(s); },
            [this](ui::HudState& s) { modHost.ApplyModHud(s); },
            [this] { return WorldCovered(); },
            [this] { return replayStudio.HidesHud(); },
            renderer, levelRenderer, entityRenderer, dollSelf, screens, replayCtl,
            [this](int w, int h) { modHost.DrawModTint(w, h); },
            [this](int w, int h, bool under) { modHost.DrawModGfx(w, h, under); }}};
        worldview::EntityPass entityPass{worldview::EntityPass::Deps{
            BlockPalette::Instance(),
            ui::UiAssets::Instance(),
            ServerPacks::Instance(),
            debugOverlay,
            interaction,
            lightCache,
            modHost,
            replayStudio,
            options,
            renderer,
            levelRenderer,
            hudState,
            handRenderer,
            entityRenderer,
            weather,
            particles,
            dataParticles,
            particleLevel,
            sounds,
            fgActive,
            camera,
            player,
            cameraDetached,
            loop,
            previewEntities,
            dollSelf,
            replayCtl}};
        worldview::LevelPass levelPass{worldview::LevelPass::Deps{
            ui::UiAssets::Instance(),
            modHost,
            replayStudio,
            options,
            renderer,
            levelRenderer,
            hudState,
            handRenderer,
            entityRenderer,
            fgPacer,
            camera,
            player,
            cameraDetached,
            loop,
            entityPass.previewEntitiesSpawned,
            replayCtl,
            session,
            [this] { return death.IsOpen(); },
            [this] { platform->isRunning = false; }}};
        worldview::FrameParamsBuilder frameParams{worldview::FrameParamsBuilder::Deps{
            ui::UiAssets::Instance(), ServerPacks::Instance(), debugOverlay, modHost, replayStudio, view, options, renderer,
            levelRenderer, hud, hudState, fogDefs, fgActive, camera, player, blockLight, tickDebt, rainLevel,
            previousRainLevel, thunderLevel, previousThunderLevel, loop, hudPartial, shaderPackCache, replayCtl, session,
            sessionReady, [this] { return pause.IsOpen(); }}};
    };
}
