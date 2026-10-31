#pragma once
#include "client/Accounts.hpp"
#include "client/Camera.hpp"
#include "client/ClientControl.hpp"
#include "client/FrameGenPacer.hpp"
#include "client/Options.hpp"
#include "client/audio/SoundSystem.hpp"
#include "client/audio/Subtitles.hpp"
#include "client/entity/EntityRenderer.hpp"
#include "client/hand/HandRenderer.hpp"
#include "client/input/Input.hpp"
#include "client/input/InputManager.hpp"
#include "client/mods/ModManager.hpp"
#include "client/multiplayer/BedrockSession.hpp"
#include "client/particle/DataParticles.hpp"
#include "client/particle/ParticleSystem.hpp"
#include "client/player/HeldBuild.hpp"
#include "client/player/Interaction.hpp"
#include "client/player/LocalPlayer.hpp"
#include "client/renderer/Fog.hpp"
#include "client/renderer/FogDefs.hpp"
#include "client/renderer/LevelRenderer.hpp"
#include "client/renderer/Lightmap.hpp"
#include "client/renderer/RenderContext.hpp"
#include "client/renderer/WeatherRenderer.hpp"
#include "client/replay/ExportConfig.hpp"
#include "client/replay/Ffmpeg.hpp"
#include "client/replay/FfmpegDownload.hpp"
#include "client/replay/ReplayController.hpp"
#include "client/replay/Timeline.hpp"
#include "client/resources/PackInfo.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/ui/ChestScreen.hpp"
#include "client/ui/FormScreen.hpp"
#include "client/ui/Hud.hpp"
#include "client/ui/InventoryScreen.hpp"
#include "client/ui/TitleScreen.hpp"
#include "client/ui/UiTemplate.hpp"
#include "network/FriendWorld.hpp"
#include "network/LanDiscovery.hpp"
#include "util/ThreadPool.hpp"
#ifdef PERLIN_SINGLEPLAYER
#include "EmbeddedServer.hpp"
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
    struct LocalBreakState {
        bool active = false;
        int x = 0, y = 0, z = 0;
        float progress = 0.0f;
    };

    struct LaunchArgs {
        std::string server;
        std::string playerName;
        std::string skin;
        std::string cape;
        bool slimArms = false;
        bool login = false;
        bool offline = false;
        std::string transport;
        bool logout = false;
        bool secure = false;
        bool acceptEula = false;
        std::string stun;
        bool hidden = false;
        int controlPort = 0;
    };

    struct FrameContext;
    struct PauseFrame;
    struct OptionsModel;

    class Perlin {
    public:
        explicit Perlin(LaunchArgs args);
        ~Perlin();

        bool Initialize(AppPlatform* platform);
        void Shutdown();

        Input& GetInput() { return input; }

    private:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        // the parts every rml overlay screen keeps between frames
        struct OverlayState {
            bool shown = false;
            bool leftDown = false;
            std::string signature;
            TimePoint openedAt{};
        };

        // lifecycle (Perlin.cpp)
        bool AcceptEula();
        void Connect();
        void StartAssetDownloads();
        SkinConfig MakeSkinConfig(const std::string& skinPath, const std::string& capePath) const;
        void SetStatusHint(const std::string& text);
        void TakeScreenshot();
        void UpdateTitle(int fps);
        void VibrateBlockBroken();
        LocalBreakState LocalBreak() const;

        // frame loop (PerlinFrame.cpp, PerlinFrameWorld.cpp, PerlinFrameRender.cpp)
        void RenderThreadMain();
        void InitRenderThread();
        void FrameStep();
        void TearDownRenderThread();
        void BeginFrame(FrameContext& f);
        void ImportDroppedFiles();
        void UpdateLook(FrameContext& f);
        void UpdateCameraEffects();
        void TrackDamage();
        void UpdateSession(FrameContext& f);
        void UpdateRiding(BedrockSession& live, const std::vector<entity::Entity>& entities);
        void UpdateEatingAndDrinking(FrameContext& f, BedrockSession& live);
        void ReleaseHeldItem(BedrockSession* live, const ui::HudItem& held, const float eye[3]);
        void UpdateViewSettings(FrameContext& f);
        void ApplyRendererSwitch();
        void UpdateChat(FrameContext& f);
        void UpdateGameInput(FrameContext& f);
        void UpdateHotbar(FrameContext& f);
        void UpdateTechnicalBlocks(const ui::HudItem& held, bool creative, BedrockSession* live);
        void UpdateAim(FrameContext& f, bool spectator, bool creative);
        void UpdateBlockBreaking(FrameContext& f, const ui::HudItem& held, bool creative);
        void UpdateBlockUse(FrameContext& f, const ui::HudItem& held, bool creative);
        void StopBreaking(BedrockSession* live);
        void SwingHand();
        void UpdatePreviewWorld(FrameContext& f);
        void UpdateThirdPersonDistance(BedrockSession* live);
        void SampleEyeEnvironment(FrameContext& f);
        void FillFrameHud(FrameContext& f);
        void HandleDebugKeys(FrameContext& f);
        void TickGame(FrameContext& f);
        void PrepareView(FrameContext& f);
        void BuildFrameParams(FrameContext& f);
        FogState ComputeFrameFog(FrameContext& f);
        void CollectDynamicLights(FrameContext& f);
        void RenderLevel(FrameContext& f);
        void HandleLiveScreenshot(FrameContext& f);
        void RenderEntities(FrameContext& f);
        void TickParticles(FrameContext& f, Level& level, entity::EntityRenderer::FrameIn& ein,
                           const std::vector<entity::Entity>& entities);
        void RenderWeather(FrameContext& f);
        void RenderHand(FrameContext& f);
        void UpdateHudAndScreens(FrameContext& f);
        void UpdateTouch(FrameContext& f);
        void EndFrame(FrameContext& f);
        void PaceFrame(FrameContext& f);

        // rml screens (PerlinScreens.cpp)
        void UpdateServerLayer(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);
        void UpdateFormScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);
        void UpdateBedScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);
        void UpdateCreditsScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);
        void UpdateDeathScreen(ui::Hud& hud, BedrockSession* live, const ui::HudState& state, int fbW,
                               int fbH);
        void UpdateChatClicks(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);
        void CloseOverlay(ui::Hud& hud, OverlayState& screen);
        void CenterCursorForGui() {
            if (options.centerCursor)
                input.RequestCursorCenter();
        }

        // inventory (PerlinInventory.cpp, PerlinCreative.cpp)
        void UpdateInventoryScreen(ui::Hud& hud, BedrockSession* live, const ui::HudState& state, int fbW,
                                   int fbH);
        void UpdateCreativeScreen(ui::Hud& hud, BedrockSession* live, const ui::HudState& state, int fbW,
                                  int fbH);
        void OpenInventory(TimePoint now);

        // pause and options menus (PerlinPause.cpp, PerlinOptions.cpp, PerlinPackMenu.cpp, PerlinWardrobe.cpp)
        void UpdatePauseScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);
        bool OpenPauseScreen(ui::Hud& hud, BedrockSession* live, TimePoint now, bool preview);
        void UpdateKeybindPage(PauseFrame& f);
        void UpdateWardrobe(PauseFrame& f);
        void UpdatePackMenu(PauseFrame& f);
        void UpdateOptionsMenu(PauseFrame& f);
        void BuildWorldOptions(OptionsModel& m, PauseFrame& f);
        void BuildModsOptions(OptionsModel& m, PauseFrame& f);
        void BuildMainOptions(OptionsModel& m, PauseFrame& f);
        void BuildVideoOptions(OptionsModel& m, PauseFrame& f);
        void BuildResourcePacksOptions(OptionsModel& m, PauseFrame& f);
        void BuildShaderPacksOptions(OptionsModel& m, PauseFrame& f);
        void BuildSoundOptions(OptionsModel& m, PauseFrame& f);
        void BuildControlsOptions(OptionsModel& m, PauseFrame& f);
        void BuildModSettingsOptions(OptionsModel& m, PauseFrame& f);
        void BuildJavaShaderSettingsOptions(OptionsModel& m, PauseFrame& f);
        void BuildShaderSettingsOptions(OptionsModel& m, PauseFrame& f);
        void BuildSkinOptions(OptionsModel& m, PauseFrame& f);
        void BuildHandOptions(OptionsModel& m, PauseFrame& f);
        void BuildKeyBindsOptions(OptionsModel& m, PauseFrame& f);
        void UpdatePauseMenu(PauseFrame& f);
        const Binding& BindingOf(Action a) const {
            return optionsUi.bindDevice == 1 ? options.GetPadKey(a) : options.GetKey(a);
        }
        void SetBindingOf(Action a, const Binding& b) {
            if (optionsUi.bindDevice == 1)
                options.SetPadKey(a, b);
            else
                options.SetKey(a, b);
        }

        // title, loading and disconnect screens (PerlinTitle.cpp)
        bool UpdateTitleScreen(ui::Hud& hud, int fbW, int fbH);
        bool UpdateAssetScreen(ui::Hud& hud, int fbW, int fbH);
        bool UpdateLoadingScreen(ui::Hud& hud, int fbW, int fbH);
        bool UpdateDisconnectScreen(ui::Hud& hud, int fbW, int fbH);
        void DrawTitlePanorama(int fbW, int fbH);
        void LeaveToServerList(ui::Hud& hud);
        int FrameGenMultiplier();

        // accounts and server browser (PerlinServers.cpp)
        void ApplyActiveAccount();
        std::string ActiveAuthPath() const;
        void PrewarmSignIn();
        void StartAccountSignIn();
        void CancelAccountSignIn();
        void FetchMissingGamertags();
        void PingServers(const std::vector<std::string>& addresses);
        void RefreshTab(ui::ServerTab tab);
        void RefreshFeatured(bool force);
        void JoinServer(const std::string& address);
        void JoinSingleplayer();
        void JoinRealm(long long id);
        void JoinFriend(const ui::ServerEntry& entry);
        void JoinLanGame(const ui::ServerEntry& entry);
        void BeginLeaseJoin(const std::string& text);
        void FailLeaseJoin(const std::string& text);
        void ReleaseFriendLease();
        void AddRealm(const std::string& code);
        void SetJoinMessage(const std::string& text);

        // resource and shader packs (PerlinPacks.cpp)
        void ApplyTexturePacks();
        void UnloadServerPacks(bool leavingLevel = false);
        void CommitTexturePacks();
        void ApplyShaderPack();
        void PollJavaPack(BedrockSession* live);
        void PollHotReload();

        // hud state (PerlinHud.cpp)
        void FillHudState(ui::HudState& state, BedrockSession* live, bool preview);
        void FillDebug(ui::HudState& state, BedrockSession* live, Level* level);
        void TickHud();
        void UpdateAudio(BedrockSession* live, double now, float dt);
        void SpawnPreviewEntities();

        // remote control api (PerlinControl.cpp)
        void PollControl();
        void PublishControl();
        void TraceControlTick(uint64_t tick, const physics::State& state, const physics::Input& input);

        // mods (PerlinMods*.cpp)
        void SetupMods();
        void SetupModPlayerApi();
        void SetupModWorldApi();
        void SetupModClientApi();
        void SetupModInteractionApi();
        void SetupModActions();
        void SetupModAdvanced();
        void SetupModHooks();
        void ToggleMod(const std::string& name);
        void PumpMods(BedrockSession* live, Level* level, ui::Hud* hudPtr, float dt);
        void TickMods(BedrockSession* live);
        bool InterceptOutgoing(BedrockSession* live, const std::string& text);
        bool FilterIncomingChat(const std::string& line);
        void UpdateModLayer(ui::Hud& hud);
        void ApplyModHud(ui::HudState& s);
        void NoteModSound(const std::string& name, double x, double y, double z, float volume, float pitch);
        void MergePluginCommands(BedrockSession* live);
        void StartModHttp(const std::string& plugin, uint32_t id, std::string method, std::string url,
                          int timeoutMs, std::vector<std::pair<std::string, std::string>> headers,
                          std::vector<uint8_t> body, std::string savePath = {});
        abi::Screen CurrentScreen() const;
        void ApplyModCamera(float& fov, glm::mat4& viewEffect);
        void DropModHooks(const std::string& plugin);
        bool ModHudHidden(abi::HudElement element, float& dx, float& dy);
        void ApplyModDebugLines(std::vector<std::string>& left, std::vector<std::string>& right);
        void ApplyModFog(FogState& fog);
        void DrawModTint(int fbW, int fbH);
        int ModFrameLimit(int fps) const;
        void PumpModHooks(BedrockSession* live);
        void ApplyModSkins(std::vector<entity::Entity>& entities);
        void PrepareModRender(entity::EntityRenderer::FrameIn& in);
        // true: a mod cancelled the action
        bool DispatchModInteract(abi::InteractKind kind, int x, int y, int z, int face, uint64_t entity);
        bool DispatchModSlotClick(int slot, bool right, bool shift);
        bool DispatchModToast(const std::string& title, const std::string& body);
        std::vector<std::string> ModTooltipLines(const ui::HudItem& item);
        void PumpModInput(mods::ModManager& mm);
        void DrawModGfx(int fbW, int fbH);

        // replay recording, playback and the timeline editor (PerlinReplay.cpp, PerlinEditor.cpp)
        void ToggleReplayRecording(bool start);
        void FinishReplayRecording(const char* why);
        void StartReplayPlayback(const std::string& path);
        void MakeReplaySession();
        void MountReplayPacks();
        void ExitReplayPlayback(ui::Hud& hud);
        void PumpReplay(float dt);
        void UpdateReplayCursor();
        bool UpdateReplayScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);
        void ReplayStatus(const std::string& text);
        void RefreshReplayList();
        bool TitleReplayTyped(const InputManager::Typed& t);
        bool TitleReplayClick(const std::string& hover);
        bool ImportReplayFile(const std::string& source);
        bool UpdateKeyframePanel(ui::Hud& hud, int fbW, int fbH);
        bool UpdateEditor(ui::Hud& hud, int fbW, int fbH);
        void LoadTimeline();
        void SaveTimeline();

        // video export (PerlinExport.cpp, PerlinExportUi.cpp)
        struct ExportRequest {
            replay::ExportConfig cfg;
            bool screenshot = false;
        };
        bool UpdateExportMenu(ui::Hud& hud, int fbW, int fbH);
        void ExportOpenMenu();
        bool ExportStart(const ExportRequest& req);
        void ExportPreFrame(TimePoint& frameStart, float& dt);
        void ExportPostRender();
        void ExportFinish(bool cancelled, const std::string& error);
        void ExportWriterLoop();
        void ExportStartNext();
        void ExportOverlay(ui::Hud& hud, int fbW, int fbH);
        void FramebufferSizeEffective(int& w, int& h);
        void LoadExportConfig();
        void SaveExportConfig();

        LaunchArgs args;
        AppPlatform* platform = nullptr;
        Options options;
        std::string runningRenderer;
        Input input;
        InputManager inputManager{input, options};
        std::shared_ptr<ClientControl> control;
        ControlSnapshot controlState;

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
        double particleClock = 0;
        std::vector<particle::Quad> particleQuads;
        FogDefs fogDefs;
        audio::AudioEngine audioEngine;
        audio::SoundSystem sounds{audioEngine};
        audio::Subtitles subtitles;
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
        bool headUnderwater = false;
        bool headInLava = false;
        float waterSurfaceAbove = 0.0f;
        int lastDimId = -1;
        float portalTime = 0.0f;

        // block targeting, breaking and placing; render thread only
        struct LocalCrack {
            bool active = false;
            int x = 0, y = 0, z = 0, stage = 0;
            float progress = 0.0f;
        };
        struct Mining {
            bool breaking = false;
            int x = 0, y = 0, z = 0, face = 1;
            float progress = 0.0f;
            int delayTicks = 0;
            int hitTicks = 0;
            uint64_t creativeReadyAt = 0;
            double breakClock = 0, useClock = 0, swingClock = 0;
        };
        struct RecentBreak {
            int x = 0, y = 0, z = 0;
            TimePoint at{};
        };
        AimHit aimHit;
        uint32_t selfSwing = 0;
        LocalCrack localCrack;
        Mining mining;
        heldbuild::HeldBuild heldBuild;
        RecentBreak lastLocalBreak;
        bool usingItem = false;

        // persistent state of the frame loop
        struct FrameLoop;
        std::unique_ptr<FrameLoop> loop;

        struct DebugOverlay {
            bool on = false;
            bool chunkBorders = false;
            bool hitboxes = false;
            bool softPaused = false;
            bool advancedTooltips = false;
            int switcherIndex = -1;
            int fps = 0;
            uint64_t rateTx = 0, rateRx = 0;
            int txRate = 0, rxRate = 0;
            TimePoint rateAt{};
            int heightSurface = 0;
            TimePoint heightAt{};
            int sky = 15, block = 0;
        } debug;

        struct HudAnim {
            uint64_t ticks = 0;
            int toolTimer = 0;
            ui::HudItem lastTool;
            TimePoint titleAtSeen{}, actionAtSeen{};
            int overlayTime = 0, titleTime = 0, titleFadeIn = 10, titleStay = 70, titleFadeOut = 20;
            bool haveHealth = false;
            int lastHealth = 0;
            int lastHealthTick = 0, lastHurtTick = -1000;
            int blinkUntil = 0, displayHealth = 0;
        } hudAnim;
        float hudPartial = 0.0f;
        int previewSlot = 0;
        bool uiClickWasDown = false;
        ui::ChatLog chat;
        TimePoint chatOpenedAt{};
        TimePoint playersAt{};
        bool chatLeftDown = false;

        entity::EntityTracker previewEntities;
        bool previewEntitiesSpawned = false;
        std::vector<uint64_t> previewWalk;
        std::shared_ptr<const entity::SkinData> selfSkinData;
        std::string selfSkinKey;
        std::optional<entity::Entity> dollSelf;
        struct EntityLightEntry {
            TimePoint at{};
            glm::vec3 colour{1.0f};
            int sky = 0, block = 0;
        };
        std::unordered_map<int64_t, EntityLightEntry> entityLight;

        struct ServerLayer {
            bool shown = false;
            std::string signature;
        } serverLayer;
        struct Toast {
            std::string text, title, message;
            TimePoint at{};
        } toast;

        struct BedScreen : OverlayState {
        } bed;
        BedrockSession::ScreenAnimation screenAnimation;

        struct CreditsScreen : OverlayState {
            double scroll = 0;
            TimePoint last{};
        } credits;

        struct DeathScreen : OverlayState {
            bool respawnSent = false, textMode = false, hoverLogged = false;
            TimePoint sentAt{};
            ui::TemplateSlot tmpl;
        } death;

        struct FormScreen : OverlayState {
            ui::FormModel model;
            ui::FormUi ui;
            // the json ui could not draw this form: stay on the built-in layout
            bool engine = false, engineFailed = false;
            float scroll = 0.0f, contentH = 0.0f, viewH = 0.0f;
            int dragSlider = -1;
            TimePoint lastWarm{}, lastAnim{};
            bool previewOpened = false;
        } form;

        struct InventoryScreen : OverlayState {
            bool rightDown = false, middleDown = false, previewOpened = false, bookInit = false;
            float mouseX = 0, mouseY = 0;
            int hoverAddr = -1, picked = -1, hover = -1;
            std::string hoverId;
            bool workbench = false, book = false, horse = false, chest = false;
            TimePoint mountAsk{};
            int dragButton = 0;
            std::vector<int> dragSlots;
            int bookPage = 0;
            std::vector<BedrockSession::RecipeEntry> recipeAll;
            TimePoint recipeAt{};
            ui::TemplateSlot tmpl;
            float panel[4] = {};
            bool jsonPanel = false, closeClicked = false;
            std::string chestScreen, chestProblem;
            ui::ChestUi chestUi;
        } inventory;

        struct CreativeScreen {
            int tab = 0;
            bool dollVisible = false;
            float scroll = 0.0f;
            bool dragging = false;
            std::string search, signature;
            ui::TemplateSlot tmpl;
        } creativeScreen;

        struct PauseScreen : OverlayState {
            bool previewOpened = false, players = false;
            int view = 0;
            ui::TemplateSlot tmpl;
        } pause;

        struct OptionsMenu {
            int drag = -1, page = 0, sub = 0;
            int bindCapture = -1, controlsTab = 0, bindDevice = 0;
            bool bindWasCapturing = false;
            TimePoint bindStartedAt{};
            bool touchEditing = false, touchEditHidden = false;
            float kbScroll = 0.0f, scroll = 0.0f;
            int scrollKey = -1;
            int hoverRow = -1;
            TimePoint hoverSince{};
            std::map<std::string, std::pair<int, double>> worldOverride;
            std::string signature;
        } optionsUi;

        struct PackMetaCacheEntry {
            std::string stamp;
            PackMeta meta;
        };
        struct PackMenu {
            float scrollActive = 0.0f, scrollAvailable = 0.0f, shaderScroll = 0.0f;
            float modScroll = 0.0f, modDetailScroll = 0.0f;
            std::string modSelected, modSearch;
            std::string shaderPending, metaStampDir;
            bool shaderPendingEnabled = false, shaderPendingInit = false;
            std::map<std::string, PackMetaCacheEntry> metaCache;
            std::map<std::string, std::string> modIconStamp;
            std::vector<std::string> foundCache;
            TimePoint foundAt{};
            std::vector<std::string> pending;
            bool pendingInit = false;
            std::vector<std::string> modRowTips;
        } packUi;
        std::atomic<bool> fontsDownloaded{false};

        struct Wardrobe {
            int line = 0;
            bool skinOpen = false, capeOpen = false, drag = false;
            float yaw = -25.0f, pitch = 0.0f, dragX = 0.0f, dragY = 0.0f;
            int previewSerial = 0;
            std::string previewKey, previewSource;
            std::vector<std::string> skins, capes;
            TimePoint listedAt{};
            std::unordered_map<std::string, std::string> stamp;
            std::unordered_map<std::string, bool> slimGuess;
            std::string skin, cape;
            bool init = false;
            std::map<std::string, bool> slim;
            LoadedSkin previewLoaded;
        } wardrobe;

        struct TitleScreenState : OverlayState {
            bool active = false;
            bool options = false;
            bool returnToServers = false, returnToReplays = false;
            int openView = 1;
            int clickRow = -1;
            TimePoint clickAt{};
            ui::TitlePage lastPage = ui::TitlePage::Main;
        } titleScreen;
        ui::TitleModel title;
        struct Panorama {
            uint32_t textures[6] = {};
            bool ready = false;
            uint64_t packStamp = 0;
            TimePoint clock{}, retryAt{};
        } panorama;
        TimePoint logoRetryAt{};
        bool logoBedrock = false;

        struct AssetScreen : OverlayState {
            bool skip = false;
            TimePoint sampleAt{};
            uint64_t lastBytes[2] = {0, 0};
            double speed[2] = {0.0, 0.0};
        } asset;

        struct LoadingScreen : OverlayState {
            bool textMode = false, done = false, readyArmed = false;
            TimePoint readyAt{}, startAt{};
            int chunkBase = 0;
            ui::TemplateSlot tmpl;
        } loading;

        struct DisconnectScreen : OverlayState {
            bool textMode = false;
            ui::TemplateSlot tmpl;
        } disconnect;

        // texture/shader pack state
        std::string javaPackPending;
        int javaPackDimension = 0;
        bool levelUnloadPending = false;
        struct HotStamps {
            std::string tex, shader;
        };
        std::future<HotStamps> hotScan;
        TimePoint hotPollAt{};
        std::string hotTexStamp, hotShaderStamp, shaderBuildReport;

        // accounts; the sign-in runs on a worker
        AccountList accounts;
        struct SignIn {
            std::mutex mutex;
            std::shared_ptr<xbox::Account> account;
            std::string authFile, url, code, status, gamertag;
            std::vector<std::pair<std::string, std::string>> pendingRenames;
            TimePoint gamertagFetchAt{};
            std::atomic<int> state{0};
        } signIn;

        // server browser tabs; filled by workers under tabMutex
        std::mutex pingMutex;
        std::map<std::string, ui::ServerEntry> pingResults;
        std::mutex tabMutex;
        std::vector<ui::ServerEntry> tabLan, tabRealms, tabFriends, tabFeatured;
        std::string tabLanMsg, tabRealmsMsg, tabFriendsMsg, tabJoinMsg, realmJoinAddress;
        bool tabDirty = false, realmJoinReady = false;
        std::atomic<bool> lanBusy{false}, realmsBusy{false}, friendsBusy{false}, realmJoinBusy{false};
        std::atomic<bool> featuredBusy{false}, featuredPing{false};
        TimePoint featuredAt{};
        std::atomic<bool> realmAddBusy{false}, realmAddedFlag{false};
        TimePoint realmAddedAt{};
        std::atomic<bool> leaseJoining{false}, leaseJoinFailed{false};
        std::shared_ptr<lan::Session> lanSession;
        std::mutex lanSessionMutex;
        std::shared_ptr<friendworld::Lease> friendLease;
        std::shared_ptr<friendworld::Lease> friendLeaseReady;
        std::atomic<bool> friendJoinBusy{false};

        // mods
        int modOpenView = -1, modOpenPage = 0;
        std::string modSettingsFor;
        BedrockSession* modLive = nullptr;
        Level* modLevel = nullptr;
        ui::Hud* modHud = nullptr;
        float modDt = 0.0f;
        int modGuiW = 0, modGuiH = 0;
        std::mutex modMutex;
        struct ModAdvanced {
            std::mutex mutex;
            abi::CameraControlState camera;
            TimePoint cameraUntil{};
            abi::ModelPose pose;
            bool lookTaken = false;
            glm::dvec3 lookDir{0.0, 0.0, -1.0};
            TimePoint poseUntil{};
            struct DrawSet {
                TimePoint until{};
                std::vector<entity::EntityRenderer::FrameIn::ModLine> lines;
            };
            std::map<std::string, DrawSet> lines;
            std::map<std::string, std::pair<TimePoint, std::vector<uint32_t>>> drops;
            std::map<std::string, std::pair<TimePoint, std::vector<uint32_t>>> outDrops;
            std::array<float, 16> viewProjection{};
            std::vector<entity::EntityRenderer::FrameIn::ModLine> frameLines;
        } modAdv;
        // keyed by plugin, dropped on unload
        struct ModHooks {
            using TimePoint = std::chrono::steady_clock::time_point;
            std::mutex mutex;
            std::map<std::string, std::map<abi::HudElement, abi::HudElementState>> hud;
            std::map<std::string, abi::DebugLinesData> debug;
            std::pair<TimePoint, abi::FogOverride> fog{};
            std::pair<TimePoint, abi::ScreenTintData> tint{};
            struct EntityRule {
                TimePoint until{};
                abi::EntityOverride rule;
            };
            std::map<std::string, std::map<std::string, EntityRule>> entities; // plugin -> "id:N" / "type:X"
            struct Mesh {
                TimePoint until{};
                abi::WorldMeshData data;
            };
            std::map<std::string, Mesh> meshes; // "plugin/id"
            std::map<std::string, abi::FrameLimitData> frame;
            struct Button {
                std::string plugin;
                abi::ScreenButtonDef def;
            };
            std::vector<Button> buttons;
            std::string customOwner, customTitle;
            bool customOpen = false;
            std::vector<std::string> tooltipLines;
            std::set<std::string> loadingHidden;
            std::map<std::string, abi::SkinOverrideData> skins;
            uint64_t skinSerial = 0;
            std::map<std::string, abi::AmbientSoundData> ambient; // "plugin/id"
            std::vector<std::pair<std::string, abi::EventServerPing>> pings;
            std::atomic<uint32_t> nextPing{1};
            std::vector<std::pair<std::string, std::string>> packs; // plugin, absolute path
            std::vector<abi::ParticleRequest> particles;
            bool windowFocused = true, windowMinimized = false;
        } modHooks;
        std::vector<entity::EntityRenderer::FrameIn::ModMesh> modFrameMeshes;
        struct ScreenButtonView {
            abi::ScreenButtonDef def;
            bool hover = false;
        };
        std::vector<ScreenButtonView> modScreenButtons;
        bool modLayerOnScreen = false;
        std::string modLayerButtons;
        std::unordered_map<uint64_t, entity::EntityRenderer::FrameIn::ModStyle> modFrameStyles;
        std::unordered_map<std::string, entity::EntityRenderer::FrameIn::ModStyle> modFrameTypeStyles;
        std::map<std::string, std::shared_ptr<const entity::SkinData>> modSkinCache;
        uint64_t modSkinCacheSerial = 0;
        std::vector<std::pair<std::string, abi::EventHttp>> modHttp;
        std::atomic<int> modHttpActive{0};
        std::vector<abi::EventSound> modSounds;
        std::map<std::string, bool> modKeyDown;
        std::atomic<bool> modWantPackets{false};
        bool modWasConnected = false, modWasDead = false;
        bool modGrabMouseWas = false, modGrabKeyboardWas = false;
        std::atomic<int> modFbW{0}, modFbH{0};
        std::mutex modNativeMutex;
        abi::NativeHandles modNative;
        bool modNativeReady = false;
        struct ModGfxTexture {
            uint32_t gpu = 0;
            uint64_t version = 0;
        };
        std::unordered_map<std::string, ModGfxTexture> modGfxTextures;
        int modLastDim = 0, modLastSlot = -1;
        uint32_t modLastHurt = 0, modTitleSerial = 0;
        abi::Screen modLastScreen = abi::Screen::None;
        std::unordered_set<uint64_t> modEntityIds;
        bool modEntitySeeded = false;
        std::string modToastText, modTitleText, modSubtitleText;
        TimePoint modToastUntil{}, modTitleStart{};
        int modTitleFadeIn = 500, modTitleStay = 3500, modTitleFadeOut = 1000;
        uint64_t modLayerVersion = 0;
        int modLayerW = 0, modLayerH = 0;
        bool modLayerShown = false;
        std::string modCommandSig;
        uint32_t modCommandsVersion = 0;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> modPackets;

        // replay
        replay::ReplayController replayCtl;
        struct ReplayUi {
            bool menuOpen = true, grab = false, hideHud = true, camInit = false, leftDown = false,
                 scrubbing = false, dirty = false;
            glm::vec3 flyVel{0.0f};
            bool showPlayers = true, showEntities = true, showNametags = true, editorOverUi = false;
            float editorViewX0 = 0, editorViewY0 = 0, editorViewX1 = 1e9f, editorViewY1 = 1e9f;
            double lastScrubSeekMs = -1e9;
            TimePoint lastScrubAt{};
            double scrubMs = -1, flySpeed = 16.0, inMs = -1, outMs = -1;
            int speedIdx = 3;
            std::string signature, status;
            TimePoint openedAt{}, statusUntil{};
            bool keyPrev[256] = {};
            float roll = 0, fovOverride = -1;
        } replayUi;
        replay::Timeline timeline;
        bool timelineDirty = false;
        struct KeyframeUi {
            bool open = false;
            int tab = 0, scroll = 0;
            int sel[3] = {-1, -1, -1};
            int interp = 0;
            float fov = 70, roll = 0, speed = 1.0f, timeTicks = 6000;
            std::string signature;
            bool leftDown = false;
            TimePoint openedAt{};
        } kfUi;
        struct EditorUi {
            bool open = false, leftDown = false;
            double viewStart = 0, viewSpan = 0;
            int selTrack = -1, selIndex = -1, sideScroll = 0;
            int drag = 0, dragTrack = -1;
            double dragOffset = 0;
            double lastSeekMs = -1e9;
            TimePoint lastSeekAt{}, openedAt{};
            std::string signature;
            bool keyPrev[256] = {};
        } editorUi;
        bool replayRecoveryChecked = false;
        double replayPrevMs = 0;
        double replayWorldFromMs = 0;
        int replayClickRow = -1;
        TimePoint replayClickAt{};

        // export
        struct ExportUi {
            bool open = false, cfgLoaded = false, catQueried = false, leftDown = false;
            int tab = 0, scroll = 0;
            std::string focus, signature, status;
            std::map<std::string, std::string> text;
            replay::ExportConfig cfg;
            replay::ffmpeg::Catalog cat;
            std::unique_ptr<replay::ffmpeg::Downloader> downloader;
            std::vector<ExportRequest> queue;
            TimePoint openedAt{}, caretAt{};
            bool caret = true;
            bool downloadHandled = false;
        } exportUi;
        struct ExportJob {
            bool offscreen = false;
            bool active = false, screenshot = false, useFfmpeg = true, imageSeq = false, finishing = false,
                 advanceNow = false;
            replay::ExportConfig cfg;
            replay::ffmpeg::Settings fs;
            std::unique_ptr<replay::ffmpeg::Encoder> encoder;
            std::string outPath, name, error;
            int renderW = 0, renderH = 0, outW = 0, outH = 0;
            double startMs = 0, endMs = 0, targetMs = 0, stepMs = 0;
            long long frame = 0, total = 0;
            enum class Phase {
                Seeking,
                Settle,
                Capture,
                Waiting,
                Captured,
                Advance,
                Draining
            } phase = Phase::Seeking;
            int settleIters = 0, quietIters = 0, waitIters = 0;
            long long virtualUs = 0;
            TimePoint virtualBase{}, realStart{}, phaseStart{}, lastOverlay{};
            struct CapturedFrame {
                std::vector<uint8_t> data;
            };
            std::mutex capMutex;
            std::condition_variable capCv;
            std::deque<CapturedFrame> ready;
            std::thread writer;
            bool stopWriter = false;
            bool sizeMismatch = false;
            std::string writerError;
            std::atomic<long long> written{0};
            long long maxInFlight = 6;
            std::string overlayRml;
            TimePoint lastTitle{}, lastProgress{};
            long long lastWritten = 0;
            bool savedHideHud = true, savedPaused = false, savedMenu = false;
            double savedSpeed = 1, savedTimeMs = 0;
            replay::CameraMode savedMode = replay::CameraMode::Free;
            ~ExportJob() {
                {
                    std::lock_guard lk(capMutex);
                    stopWriter = true;
                    ready.clear();
                }
                capCv.notify_all();
                if (writer.joinable())
                    writer.join();
            }
        } exportJob;

        // sessions
        std::unique_ptr<BedrockSession> session;
        std::vector<std::unique_ptr<BedrockSession>> retiredSessions;
        // singleplayer: the world server runs in this process; its transport goes to the next Connect
#ifdef PERLIN_SINGLEPLAYER
        std::unique_ptr<EmbeddedServer> singleplayerServer;
#endif
        std::unique_ptr<Transport> pendingSingleplayer;
        // the current session is the embedded server: leaving it goes to the title screen, not the server list
        bool inSingleplayer = false;
        std::shared_ptr<xbox::Account> account;
        std::mutex statusMutex;
        std::string statusHint;
        bool onlineThisRun = false;
        std::atomic<bool> sessionReady{false};
        std::atomic<uint64_t> connectGeneration{0};
        std::atomic<int> connectsInFlight{0};
    };
}
