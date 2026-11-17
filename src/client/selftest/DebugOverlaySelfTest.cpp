#include "client/features/debug/DebugOverlay.hpp"
#include "client/selftest/Check.hpp"
#include "client/selftest/SelfTestGlobals.hpp"
#include "client/selftest/SelfTests.hpp"
#include "client/systems/bindings/Keys.hpp"
#include "client/systems/i18n/Lang.hpp"
#include "util/Paths.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <algorithm>

namespace {
    using namespace perlin;

    struct Rig {
        Options options;
        Input input;
        InputManager inputManager{input, options};
        Camera camera;
        ui::ChatLog chat;
        ui::HudState hudState;
        entity::EntityRenderer entityRenderer;
        audio::AudioEngine engine;
        audio::SoundSystem sounds{engine};
        std::unique_ptr<LevelRenderer> levelRenderer;
        std::unique_ptr<RenderContext> renderer;
        std::unique_ptr<ui::Hud> hud;
        AimHit aim;
        uint64_t ticks = 0;
        std::vector<std::string> clipboard;
        int modHookCalls = 0;
        debug::Overlay overlay{debug::Overlay::Deps{
            options, selftest::G().blockPalette, selftest::G().uiAssets, input, inputManager, camera, chat,
            hudState, entityRenderer, sounds, levelRenderer, renderer, hud, aim, ticks,
            [](int& w, int& h) {
                w = 1280;
                h = 720;
            },
            [this](const std::string& text) { clipboard.push_back(text); },
            [this](std::vector<std::string>& left, std::vector<std::string>&) {
                modHookCalls++;
                left.push_back("mod line");
            }}};

        void Frame() {
            FrameContext f;
            overlay.HandleKeys(f, nullptr);
        }
        void Hold(int key) {
            input.OnKeyDown(key);
            Frame();
        }
        void Release(int key) {
            input.OnKeyUp(key);
            Frame();
        }
        void Combo(int key) {
            Hold(Key::F3);
            Hold(key);
            Release(key);
            Release(Key::F3);
        }
        bool ChatMentions(const std::string& needle) {
            const auto lines = chat.RecentLines(20);
            return std::ranges::any_of(lines, [&](const std::string& l) { return l.find(needle) != std::string::npos; });
        }
    };

    bool AnyLineHas(const std::vector<std::string>& lines, const std::string& needle) {
        return std::ranges::any_of(lines, [&](const std::string& l) { return l.find(needle) != std::string::npos; });
    }
}

namespace perlin::selftest {
    int DebugOverlay() {
        Checker t("debug_overlay_test");
        i18n::Lang::Get().Configure(std::string(), paths::DataDir(), "en_US");

        {
            Rig r;
            t.Check(!r.overlay.state.on, "overlay starts hidden");
            r.Hold(Key::F3);
            t.Check(!r.overlay.state.on, "holding F3 alone does not toggle yet");
            r.Release(Key::F3);
            t.Check(r.overlay.state.on, "releasing F3 shows the overlay");
            r.Hold(Key::F3);
            r.Release(Key::F3);
            t.Check(!r.overlay.state.on, "a second tap hides the overlay");
        }
        {
            Rig r;
            r.Hold('B');
            r.Release('B');
            t.Check(!r.overlay.state.hitboxes, "combo keys without F3 do nothing");

            r.Combo('B');
            t.Check(r.overlay.state.hitboxes, "F3+B enables hitboxes");
            t.Check(!r.overlay.state.on, "a combo does not toggle the overlay on release");
            t.Check(r.ChatMentions(""), "toggle reports to chat");
            r.Combo('B');
            t.Check(!r.overlay.state.hitboxes, "F3+B again disables hitboxes");

            r.Combo('G');
            t.Check(r.overlay.state.chunkBorders, "F3+G enables chunk borders");
            r.Combo('G');
            t.Check(!r.overlay.state.chunkBorders, "F3+G again disables chunk borders");
            r.Combo('H');
            t.Check(r.overlay.state.advancedTooltips, "F3+H enables advanced tooltips");
            const bool pauseBefore = r.input.pauseOnLostFocus;
            r.Combo('P');
            t.Check(r.input.pauseOnLostFocus != pauseBefore, "F3+P toggles pause on lost focus");
            t.Check(!r.overlay.state.on, "none of the combos toggled the overlay");
        }
        {
            Rig r;
            r.camera.x = 12.5f;
            r.camera.y = 71.62f;
            r.camera.z = -3.25f;
            r.camera.yaw = 0.0f;
            r.camera.pitch = 10.0f;
            r.Combo('C');
            t.Equal(r.clipboard.size(), size_t{1}, "F3+C copies once");
            if (!r.clipboard.empty())
                t.Equal(r.clipboard[0], std::string("/tp @s 12.50 70.00 -3.25 -90.00 -10.00"),
                        "F3+C copies the feet position and rotation as a teleport command");
        }
        {
            Rig r;
            r.chat.AddMessage("old", 0);
            r.Combo('D');
            t.Check(!r.ChatMentions("old"), "F3+D clears earlier chat");
        }
        {
            Rig r;
            r.Combo('I');
            t.Check(r.clipboard.empty(), "F3+I with nothing targeted copies nothing");
        }
        {
            Rig r;
            t.Check(!r.overlay.state.softPaused && !r.inputManager.Blocked(), "not paused initially");
            r.Combo(Key::Escape);
            t.Check(r.overlay.state.softPaused && r.inputManager.Blocked(), "F3+Esc soft-pauses and blocks input");
            r.Combo(Key::Escape);
            t.Check(!r.overlay.state.softPaused && !r.inputManager.Blocked(), "F3+Esc again resumes");
        }
        {
            Rig r;
            r.hudState.mode = ui::GameMode::Survival;
            r.Hold(Key::F3);
            t.Equal(r.overlay.state.switcherIndex, -1, "no switcher before F4");
            r.Hold(Key::F4);
            r.Release(Key::F4);
            t.Check(r.overlay.state.switcherIndex >= 0, "F3+F4 opens the game mode switcher");
            const int first = r.overlay.state.switcherIndex;
            r.Hold(Key::F4);
            r.Release(Key::F4);
            t.Equal(r.overlay.state.switcherIndex, (first + 1) % 4, "F4 cycles the selected mode");
            r.Release(Key::F3);
            t.Equal(r.overlay.state.switcherIndex, -1, "releasing F3 closes the switcher");
            t.Check(!r.overlay.state.on, "using the switcher does not toggle the overlay");
        }
        {
            Rig r;
            ui::HudState state;
            state.visible = true;
            r.overlay.state.on = false;
            r.overlay.Fill(state, nullptr, nullptr);
            t.Check(!state.debug && state.debugLeft.empty() && state.debugRight.empty(), "hidden overlay produces nothing");
            t.Equal(r.modHookCalls, 0, "hidden overlay skips the mod hook");

            r.overlay.state.on = true;
            state.visible = false;
            r.overlay.Fill(state, nullptr, nullptr);
            t.Check(!state.debug, "overlay stays off while the hud is hidden");

            state.visible = true;
            r.camera.x = 100.0f;
            r.camera.z = 200.0f;
            r.overlay.state.fps = 144;
            r.overlay.Fill(state, nullptr, nullptr);
            t.Check(state.debug, "visible hud with the overlay on is in debug mode");
            t.Check(!state.debugLeft.empty() && !state.debugRight.empty(), "both columns are filled");
            t.Check(AnyLineHas(state.debugLeft, "Perlin"), "left column starts with the version");
            t.Check(AnyLineHas(state.debugLeft, "144"), "fps is reported");
            t.Check(AnyLineHas(state.debugLeft, "100.000"), "x coordinate is reported");
            t.Check(AnyLineHas(state.debugLeft, "200.000"), "z coordinate is reported");
            t.Check(AnyLineHas(state.debugRight, "1280"), "display size reaches the right column");
            t.Check(state.debugLeft.back() == "mod line", "mod hook can append lines");
            t.Equal(r.modHookCalls, 1, "mod hook runs once per fill");

            r.overlay.state.customSky = "sky_pack";
            r.overlay.Fill(state, nullptr, nullptr);
            t.Equal(r.modHookCalls, 2, "mod hook runs on every fill");
        }
        {
            Rig r;
            ui::HudState state;
            state.visible = true;
            r.overlay.state.on = true;
            Level level;
            level.EnsureChunk(0, 0);
            level.MarkComplete(0, 0);
            r.camera.x = 8.5f;
            r.camera.z = 8.5f;
            r.camera.y = 70.0f + 1.62f;
            r.overlay.Fill(state, nullptr, &level);
            t.Check(AnyLineHas(state.debugLeft, "1"), "level fill reports loaded chunks");
            t.Check(!AnyLineHas(state.debugLeft, "Outside"), "camera over a loaded chunk is not reported as outside");

            Rig far;
            ui::HudState farState;
            farState.visible = true;
            far.overlay.state.on = true;
            far.camera.x = 4000.0f;
            far.camera.z = 4000.0f;
            far.overlay.Fill(farState, nullptr, &level);
            t.Check(!farState.debugLeft.empty(), "camera over unloaded terrain still reports");
        }
        return t.Finish();
    }
}
