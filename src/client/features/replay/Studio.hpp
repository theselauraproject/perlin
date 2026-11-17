#pragma once
#include "client/systems/sprites/UiAssets.hpp"
#include "client/systems/resources/ServerPacks.hpp"
#include "client/features/replay/ReplayHost.hpp"
#include "client/systems/camera/Camera.hpp"
#include "client/systems/config/Options.hpp"
#include "client/systems/entity/EntityRenderer.hpp"
#include "client/systems/audio/SoundSystem.hpp"
#include "client/systems/gfx/RenderContext.hpp"
#include "client/systems/input/Input.hpp"
#include "client/systems/input/InputManager.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/player/LocalPlayer.hpp"
#include "client/systems/renderer/LevelRenderer.hpp"
#include "client/systems/replay/ExportConfig.hpp"
#include "client/systems/replay/ExportForm.hpp"
#include "client/systems/replay/Ffmpeg.hpp"
#include "client/systems/replay/FfmpegDownload.hpp"
#include "client/systems/replay/ReplayController.hpp"
#include "client/systems/replay/Timeline.hpp"
#include "client/systems/replay/TimelineEditor.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/ui/Hud.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace perlin::replay {
    class Studio {
    public:
        struct Deps {
            Options& options;
            ui::UiAssets& uiAssets;
            ServerPacks& serverPacks;
            Input& input;
            InputManager& inputManager;
            Camera& camera;
            LocalPlayer& player;
            ui::ChatLog& chat;
            const uint64_t& hudTicks;
            entity::EntityRenderer& entityRenderer;
            audio::SoundSystem& sounds;
            std::unique_ptr<LevelRenderer>& levelRenderer;
            std::unique_ptr<RenderContext>& renderer;
            std::unique_ptr<ui::Hud>& hud;
            const Level*& particleLevel;
            std::unique_ptr<BedrockSession>& session;
            std::vector<std::unique_ptr<BedrockSession>>& retiredSessions;
            std::atomic<bool>& sessionReady;
            std::atomic<uint64_t>& connectGeneration;
            ReplayController& controller;
            Host& host;
        };

        explicit Studio(Deps deps);
        ~Studio() = default;
        Studio(const Studio&) = delete;
        Studio& operator=(const Studio&) = delete;

        void ToggleRecording(bool start);
        void FinishRecording(const char* why);
        void StartPlayback(const std::string& path);
        void Pump(float dt);
        bool UpdateScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);

        bool Exporting() const { return exportJob.active; }
        bool MenuOpen() const { return exportUi.open || kfUi.open; }
        bool HidesHud() const { return replayCtl.Playing() && replayUi.hideHud; }
        bool ShowsPlayers() const { return replayUi.showPlayers; }
        bool ShowsEntities() const { return replayUi.showEntities; }
        bool ShowsNametags() const { return replayUi.showNametags; }
        bool FiltersEntities() const { return !replayUi.showPlayers || !replayUi.showEntities || !replayUi.showNametags; }
        float FovOverride() const { return replayUi.fovOverride; }
        float Roll() const { return replayUi.roll; }
        bool TimeOfDayAt(double timeMs, float& ticks) const { return timeline.TimeOfDayAt(timeMs, ticks); }
        void Finalize();

        void ExportPreFrame(std::chrono::steady_clock::time_point& frameStart, float& dt);
        void ExportPostRender();
        void ExportOverlay(ui::Hud& hud, int fbW, int fbH);
        void FramebufferSizeEffective(int& w, int& h);

    private:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        void MakeSession();
        void MountPacks();
        void ExitPlayback(ui::Hud& hud);
        void UpdateCursor();
        void SetStatus(const std::string& text);
        bool UpdateKeyframePanel(ui::Hud& hud, int fbW, int fbH);
        bool UpdateEditor(ui::Hud& hud, int fbW, int fbH);
        void ExportCloseMenu(ui::Hud& hud, TimePoint now);
        void ExportPollDownload();
        bool ExportTyped(ui::Hud& hud, TimePoint now);
        void ExportOpenFolder();
        bool ExportClick(ui::Hud& hud, const std::string& hover, const ExportDerived& derived, TimePoint now);
        struct EditorFrame;
        bool EditorKeyEdge(int code);
        void EditorSeek(const EditorFrame& f, double ms, bool force);
        void EditorTogglePlay();
        void EditorGoToView(const CamKey& key);
        void AdoptCameraKey(const CamKey& key);
        void EditorClose(const EditorFrame& f);
        void EditorOpenOptions(const EditorFrame& f);
        void EditorAddCameraKey();
        void EditorAddMarker(double tMs, bool announce);
        bool EditorShortcuts(const EditorFrame& f);
        void EditorFillModel(EditorFrame& f);
        void EditorSyncPointer(EditorFrame& f);
        void EditorWheel(const EditorFrame& f);
        bool EditorPress(const EditorFrame& f, const std::string& id);
        bool EditorMenuHit(const EditorFrame& f, const std::string& id);
        bool EditorTransportHit(const EditorFrame& f, const std::string& id);
        bool EditorTimelineHit(const EditorFrame& f, const std::string& id);
        void EditorSideAction(const EditorFrame& f, const std::string& id);
        void EditorDrag(const EditorFrame& f);
        void LoadTimeline();
        void SaveTimeline();

    struct ExportRequest {
        replay::ExportConfig cfg;
        bool screenshot = false;
    };

        bool UpdateExportMenu(ui::Hud& hud, int fbW, int fbH);
        void ExportOpenMenu();
        bool ExportStart(const ExportRequest& req);
        void ExportFinish(bool cancelled, const std::string& error);
        void ExportWriterLoop();
        void ExportStartNext();
        void LoadExportConfig();
        void SaveExportConfig();

        Options& options;
        ui::UiAssets& uiAssets;
        ServerPacks& serverPacks;
        Input& input;
        InputManager& inputManager;
        Camera& camera;
        LocalPlayer& player;
        ui::ChatLog& chat;
        const uint64_t& hudTicks;
        entity::EntityRenderer& entityRenderer;
        audio::SoundSystem& sounds;
        std::unique_ptr<LevelRenderer>& levelRenderer;
        std::unique_ptr<RenderContext>& renderer;
        std::unique_ptr<ui::Hud>& hud;
        const Level*& particleLevel;
        std::unique_ptr<BedrockSession>& session;
        std::vector<std::unique_ptr<BedrockSession>>& retiredSessions;
        std::atomic<bool>& sessionReady;
        std::atomic<uint64_t>& connectGeneration;
        ReplayController& replayCtl;
        Host& host;

        struct ReplayUi {
            bool menuOpen = true, grab = false, hideHud = true, camInit = false, leftDown = false,
                 scrubbing = false, dirty = false;
            glm::vec3 flyVel{0.0f};
            bool showPlayers = true, showEntities = true, showNametags = true, editorOverUi = false;
            float editorViewX0 = 0, editorViewY0 = 0, editorViewX1 = 1e9f, editorViewY1 = 1e9f;
            double lastScrubSeekMs = -1e9;
            TimePoint lastScrubAt;
            double scrubMs = -1, flySpeed = 16.0, inMs = -1, outMs = -1;
            int speedIdx = 3;
            std::string signature, status;
            TimePoint openedAt, statusUntil;
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
            TimePoint openedAt;
        } kfUi;
        struct EditorUi {
            bool open = false, leftDown = false;
            replay::TimelineViewport view;
            replay::KeySelection sel;
            int sideScroll = 0;
            int drag = 0, dragTrack = -1;
            double dragOffset = 0;
            double lastSeekMs = -1e9;
            TimePoint lastSeekAt, openedAt;
            std::string signature;
            bool keyPrev[256] = {};
        } editorUi;
        double replayPrevMs = 0;
        double replayWorldFromMs = 0;

        struct ExportUi {
            bool open = false, cfgLoaded = false, catQueried = false, leftDown = false;
            int tab = 0, scroll = 0;
            std::string focus, signature, status;
            std::map<std::string, std::string> text;
            replay::ExportConfig cfg;
            replay::ffmpeg::Catalog cat;
            std::unique_ptr<replay::ffmpeg::Downloader> downloader;
            std::vector<ExportRequest> queue;
            TimePoint openedAt, caretAt;
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
            TimePoint virtualBase, realStart, phaseStart, lastOverlay;
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
            ui::SceneDoc overlay;
            bool hasOverlay = false;
            TimePoint lastTitle, lastProgress;
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
    };
}
