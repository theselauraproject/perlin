#include "client/features/replay/Studio.hpp"
#include "client/selftest/Check.hpp"
#include "client/selftest/SelfTestGlobals.hpp"
#include "client/selftest/SelfTests.hpp"
#include "client/selftest/TestEnvironment.hpp"
#include "client/systems/replay/ReplayFormat.hpp"
#include "util/SimClock.hpp"

#include <chrono>
#include <cstring>
#include <thread>

namespace {
    using namespace perlin;

    class FakeHost final : public replay::Host {
    public:
        int width = 1280, height = 720;
        bool running = true, loadingDone = true, pauseOpen = false;
        std::vector<std::string> events;
        std::string failure;
        bool cursorCaptured = false;

        void FramebufferSize(int& w, int& h) override {
            w = width;
            h = height;
        }
        void SetCursorCaptured(bool captured) override { cursorCaptured = captured; }
        void OpenUrl(const std::string& url) override { events.push_back("OpenUrl:" + url); }
        bool Running() const override { return running; }
        void SetStatusHint(const std::string& text) override { events.push_back("Status:" + text); }
        bool LoadingDone() const override { return loadingDone; }
        void ResetLoading() override { events.push_back("ResetLoading"); }
        bool PauseOpen() const override { return pauseOpen; }
        void UpdatePause(ui::Hud&, BedrockSession*, int, int) override {}
        void OpenPauseOptions(std::chrono::steady_clock::time_point) override {}
        void ClosePause() override { events.push_back("ClosePause"); }
        void DeactivateTitle() override { events.push_back("DeactivateTitle"); }
        void ReportOpenFailure(const std::string& message) override {
            failure = message;
            events.push_back("OpenFailure");
        }
        void ReturnToReplayList(ui::Hud&) override { events.push_back("ReturnToReplayList"); }
        int Count(const std::string& e) const { return static_cast<int>(std::count(events.begin(), events.end(), e)); }
    };

    struct Rig {
        Options options;
        Input input;
        InputManager inputManager{input, options};
        Camera camera;
        LocalPlayer player;
        ui::ChatLog chat;
        uint64_t ticks = 0;
        entity::EntityRenderer entityRenderer;
        audio::AudioEngine engine;
        audio::SoundSystem sounds{engine};
        std::unique_ptr<LevelRenderer> levelRenderer;
        std::unique_ptr<RenderContext> renderer;
        std::unique_ptr<ui::Hud> hud;
        const Level* particleLevel = reinterpret_cast<const Level*>(1);
        std::unique_ptr<BedrockSession> session = std::make_unique<BedrockSession>();
        std::vector<std::unique_ptr<BedrockSession>> retired;
        std::atomic<bool> sessionReady{true};
        std::atomic<uint64_t> connectGeneration{0};
        replay::ReplayController controller;
        FakeHost host;
        replay::Studio studio{replay::Studio::Deps{
            options, selftest::G().uiAssets, selftest::G().serverPacks, input, inputManager, camera, player, chat,
            ticks, entityRenderer, sounds, levelRenderer, renderer, hud, particleLevel, session, retired,
            sessionReady, connectGeneration, controller, host}};
    };

    std::string MakeReplay(const std::string& dir, const std::string& name, int steps) {
        std::filesystem::create_directories(dir + "/work");
        replay::Metadata meta;
        meta.name = name;
        meta.server = "127.0.0.1:19132";
        meta.startPose = {1, 2, 3, 4, 5, true};
        replay::Recorder rec;
        std::string error;
        if (!rec.Start(dir + "/work", meta, {}, error))
            return {};
        for (int i = 0; i < steps; i++) {
            rec.Push(replay::Dir::ToClient, 9, std::vector<uint8_t>(3, static_cast<uint8_t>(i)));
            std::vector<uint8_t> input(32, 0);
            const float f[5] = {10, 20, static_cast<float>(30 + i), 40, 50};
            std::memcpy(input.data(), f, sizeof f);
            rec.Push(replay::Dir::ToServer, 144, input);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        const std::string file = dir + "/" + name + ".perlinreplay";
        if (!rec.Stop(file, {}, error))
            return {};
        return file;
    }
}

namespace perlin::selftest {
    int Studio() {
        Checker t("studio_test");
        TempDataDir data;
        const std::string dir = data.Path().string();

        {
            Rig r;
            r.studio.StartPlayback(dir + "/missing.perlinreplay");
            t.Equal(r.host.Count("OpenFailure"), 1, "a missing replay is reported to the host");
            t.Check(r.host.failure.starts_with("Could not open the replay"), "failure message names the problem");
            t.Check(!r.controller.Playing(), "a failed open leaves playback off");
            t.Check(r.session != nullptr && r.retired.empty(), "a failed open keeps the current session");
            t.Equal(r.connectGeneration.load(), uint64_t{0}, "a failed open does not invalidate pending connections");
            t.Check(r.host.Count("DeactivateTitle") == 0, "a failed open keeps the title screen state");
        }
        {
            Rig r;
            t.Check(!r.studio.Exporting() && !r.studio.MenuOpen(), "idle studio has no export or menu");
            t.Check(r.studio.ShowsPlayers() && r.studio.ShowsEntities() && r.studio.ShowsNametags(),
                    "all entity categories are visible by default");
            t.Check(!r.studio.FiltersEntities(), "no entity filtering by default");
            t.Check(!r.studio.HidesHud(), "hud is not hidden outside playback");
            float ticks = 0;
            t.Check(!r.studio.TimeOfDayAt(0.0, ticks), "no time-of-day override without a timeline");
            r.studio.Pump(0.05f);
            t.Check(r.session != nullptr, "pumping without playback leaves the session alone");
            int w = 0, h = 0;
            r.studio.FramebufferSizeEffective(w, h);
            t.Check(w == 1280 && h == 720, "framebuffer size comes from the host when not exporting");
        }
        {
            Rig r;
            r.studio.ToggleRecording(false);
            t.Check(!r.controller.Recording(), "stopping a recording that never started is harmless");
            r.studio.FinishRecording("test");
            t.Check(!r.controller.Recording(), "finishing without a recording is harmless");
        }
        const std::string file = MakeReplay(dir, "studio", 12);
        t.Check(!file.empty(), "test replay was written");
        if (!file.empty()) {
            Rig r;
            BedrockSession* original = r.session.get();
            r.studio.StartPlayback(file);
            t.Check(r.controller.Playing(), "a valid replay opens");
            t.Equal(r.host.failure, std::string(), "a valid replay reports no failure");
            t.Equal(r.connectGeneration.load(), uint64_t{1}, "opening a replay invalidates pending connections");
            t.Check(r.sessionReady.load() && r.session.get() != original, "opening a replay installs a fresh ready session");
            t.Check(r.particleLevel == nullptr, "opening a replay drops the particle level");
            t.Check(r.session != nullptr && r.session.get() != original && r.session->IsReplay(),
                    "playback installs a replay session");
            t.Equal(r.retired.size(), size_t{1}, "the previous session is retired");
            t.Equal(r.host.Count("DeactivateTitle"), 1, "title screen is deactivated");
            t.Equal(r.host.Count("ResetLoading"), 1, "loading state is reset for the new world");
            t.Check(r.controller.cameraMode == replay::CameraMode::Free, "playback starts in free camera");
            t.Check(r.controller.TimeMs() == 0.0, "playback starts at the beginning");

            r.host.loadingDone = false;
            r.studio.Pump(0.5f);
            t.Check(r.controller.TimeMs() == 0.0, "playback waits while the world is still loading");
            r.host.loadingDone = true;
            r.studio.Pump(0.1f);
            t.Check(r.controller.TimeMs() > 0.0, "playback advances once loading is done");
            const double first = r.controller.TimeMs();
            r.studio.Pump(0.1f);
            t.Check(r.controller.TimeMs() > first, "playback keeps advancing");
            for (int i = 0; i < 40; i++)
                r.studio.Pump(0.1f);
            t.Check(r.controller.AtEnd(), "playback stops at the end of the recording");

            r.controller.Seek(0);
            r.studio.Pump(0.0f);
            t.Equal(r.retired.size(), size_t{2}, "seeking restarts the session and retires the old one");
            t.Check(r.session && r.session->IsReplay(), "the restarted session is still a replay session");
            t.Check(r.controller.TimeMs() <= 100.0, "seeking moves the playhead to the start");

            r.studio.FinishRecording("test");
            SimClock::ExitVirtual();
        }
        if (!file.empty()) {
            replay::Timeline timeline;
            timeline.SetTimeOfDay({0.0, 6000.0f, replay::Interp::Linear});
            timeline.SetTimeOfDay({1000.0, 18000.0f, replay::Interp::Linear});
            timeline.SetSpeed({0.0, 2.0f, replay::Interp::Linear});
            {
                replay::ReplayController writer;
                std::string error;
                t.Check(writer.Open(file, error), "replay can be reopened to attach a timeline");
                t.Check(writer.SaveArchiveFile("editor.json", timeline.ToJson(), error), "timeline saved into the replay");
                writer.Close();
            }
            Rig r;
            r.studio.StartPlayback(file);
            float noon = 0;
            t.Check(r.studio.TimeOfDayAt(500.0, noon), "a saved timeline is loaded with the replay");
            t.Check(noon > 11000.0f && noon < 13000.0f, "time of day interpolates between keyframes");
            float before = 0;
            t.Check(r.studio.TimeOfDayAt(0.0, before) && before == 6000.0f, "first keyframe value at its time");
            SimClock::ExitVirtual();
        }
        return t.Finish();
    }
}
