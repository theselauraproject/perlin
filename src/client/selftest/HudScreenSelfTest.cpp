#include "client/screen/screens/HudScreen.hpp"
#include "client/selftest/Check.hpp"
#include "client/selftest/SelfTestGlobals.hpp"
#include "client/selftest/SelfTests.hpp"
#include "client/ui/ChatLog.hpp"

#include <cstdlib>

namespace {
    using namespace perlin;

    struct Rig {
        Options options;
        Input input;
        InputManager inputManager{input, options};
        screen::Manager screens;
        ui::ChatLog chat;
        ui::HudState hudState;
        bool advancedTooltips = false;
        screen::Host host{screens, input, inputManager, options, [](bool) {}, [] { return std::vector<std::string>{}; },
                          chat, hudState, [](int, bool, bool) { return false; }, advancedTooltips};
        screen::FormScreen& form = screens.Emplace<screen::FormScreen>(screen::Id::Form, screen::Traits{true}, host);
        screen::InventoryScreen& inventory =
            screens.Emplace<screen::InventoryScreen>(screen::Id::Inventory, screen::Traits{true}, host);
        replay::ReplayController replayCtl;
        screen::TitleScreen titleScreen{screen::Id::Title, screen::Traits{true}};
        std::unique_ptr<RenderContext> renderer;
        std::string runningRenderer;
        std::string javaPackPending;
        mods::UiRequest modUi;
        ShaderPackCache packCache;
        screen::PauseHost pauseHost{
            options, renderer, runningRenderer, javaPackPending, replayCtl, titleScreen, selftest::G().modManager,
            modUi, [](const std::string&) {}, [] { return false; }, [](const bench::Settings&) {}, [](bool) {},
            [](bool) {}, [](ui::Hud&) {}, [] {}, [] {}, [] {}, [] { return false; }, [] { return false; },
            [](const std::string&) {}, [](const char*) { return false; }, packCache,
            [] { return std::string(); }, [](const std::string&, const std::string&, const std::string&) {}};
        screen::PauseScreen& pause =
            screens.Emplace<screen::PauseScreen>(screen::Id::Pause, screen::Traits{true}, host, pauseHost);
        ui::UiAssets& uiAssets = selftest::G().uiAssets;
        bool underwater = false;
        ui::HudAnimation hudAnim;
        std::unique_ptr<ui::Hud> hud;
        Camera camera;
        std::unique_ptr<FrameLoop> loop = std::make_unique<FrameLoop>(options);
        float hudPartial = 0.0f;
        int previewSlot = 3;
        std::unique_ptr<BedrockSession> session = std::make_unique<BedrockSession>();
        int modSlotCalls = 0, modHudCalls = 0;
        bool covered = false, replayHides = false;
        std::unique_ptr<LevelRenderer> levelRenderer;
        entity::EntityRenderer entityRenderer;
        std::optional<entity::Entity> dollSelf;
        screen::HudScreen hudScreen{screen::HudScreen::Deps{
            uiAssets, underwater, hudAnim, options, inputManager, hud, hudState, camera, loop, hudPartial,
            previewSlot, chat, form, inventory, pause, session, [this](ui::HudState&) { modSlotCalls++; },
            [this](ui::HudState&) { modHudCalls++; }, [this] { return covered; }, [this] { return replayHides; },
            renderer, levelRenderer, entityRenderer, dollSelf, screens, replayCtl, [](int, int) {},
            [](int, int, bool) {}}};

        void Fill(bool live = true) {
            FrameContext f;
            f.live = live ? session.get() : nullptr;
            hudScreen.FillFrameHud(f);
        }
    };
}

namespace {
    int HudScreenPreview(perlin::selftest::Checker& t) {
        using namespace perlin;
        {
            Rig r;
            r.previewSlot = 5;
            r.options.showHud = true;
            r.Fill(false);
            t.Check(r.hudState.visible, "preview mode shows the hud without a session");
            t.Equal(r.hudState.selectedSlot, 5, "preview mode follows the preview slot");
            t.Equal(r.hudState.xpLevel, 7, "preview mode shows sample vitals");
            t.Check(r.hudState.health == 20.0f && r.hudState.food == 20, "preview vitals are full");
        }
        return t.Finish();
    }
}

namespace perlin::selftest {
    int HudScreen() {
        const bool preview = std::getenv("PERLIN_PREVIEW") != nullptr;
        Checker t(preview ? "hud_screen_preview_test" : "hud_screen_test");
        if (preview)
            return HudScreenPreview(t);

        {
            Rig r;
            r.options.showHud = true;
            r.Fill();
            t.Check(r.hudState.visible, "a connected, uncovered world shows the hud");
            t.Equal(r.modSlotCalls, 1, "mod slot overlays are applied once per fill");
            t.Equal(r.modHudCalls, 1, "mod hud hook runs once per fill");
            r.Fill();
            t.Equal(r.modHudCalls, 2, "mod hud hook runs on every frame");

            r.options.showHud = false;
            r.Fill();
            t.Check(!r.hudState.visible, "the hud option hides the hud");
            r.options.showHud = true;
            r.covered = true;
            r.Fill();
            t.Check(!r.hudState.visible, "a covering title or loading screen hides the hud");
            r.covered = false;
            r.replayHides = true;
            r.Fill();
            t.Check(!r.hudState.visible, "replay playback can hide the hud");
            r.replayHides = false;
            r.screens.Open(r.form);
            r.Fill();
            t.Check(!r.hudState.visible, "an open form hides the hud");
            r.screens.Close(r.form);
            r.screens.Open(r.inventory);
            r.Fill();
            t.Check(!r.hudState.visible, "an open inventory hides the hud");
            r.screens.Close(r.inventory);
            r.Fill();
            t.Check(r.hudState.visible, "closing the screens restores the hud");
            r.Fill(false);
            t.Check(!r.hudState.visible, "no session and no preview hides the hud");
        }
        {
            Rig r;
            r.underwater = true;
            r.Fill();
            t.Check(r.hudState.inWater, "head-underwater state reaches the hud");
            r.underwater = false;
            r.Fill();
            t.Check(!r.hudState.inWater, "leaving the water clears the flag");
        }
        {
            Rig r;
            r.hudState.bossBars.push_back({"stale", 1.0f, 0, 0});
            r.hudState.sidebarVisible = true;
            r.hudState.sidebar.push_back({"stale", 1});
            r.Fill();
            t.Check(r.hudState.bossBars.empty(), "boss bars are rebuilt from the session each frame");
            t.Check(!r.hudState.sidebarVisible && r.hudState.sidebar.empty(), "stale sidebar rows are cleared");
        }
        {
            Rig r;
            r.hudState.health = 20.0f;
            r.hudScreen.TickHud();
            t.Equal(r.hudAnim.ticks, uint64_t{1}, "ticking advances the hud clock");
            t.Check(r.hudAnim.haveHealth && r.hudAnim.displayHealth == 20, "first tick records the health");

            r.hudState.health = 14.0f;
            r.hudScreen.TickHud();
            t.Equal(r.hudAnim.lastHurtTick, 2, "losing health records the hurt tick");
            t.Check(r.hudAnim.blinkUntil == 22, "losing health starts a 20 tick blink");
            t.Equal(r.hudAnim.displayHealth, 20, "displayed health lags behind during the blink");

            r.hudState.health = 16.0f;
            r.hudScreen.TickHud();
            t.Equal(r.hudAnim.blinkUntil, 13, "regaining health while invulnerable restarts a shorter blink");

            for (int i = 0; i < 30; i++)
                r.hudScreen.TickHud();
            t.Equal(r.hudAnim.displayHealth, 16, "displayed health catches up once the blink window passes");
        }
        {
            Rig r;
            r.hudState.hotbar[0].name = "minecraft:diamond_sword";
            r.hudState.hotbar[0].count = 1;
            r.hudState.hotbar[0].maxDamage = 1561;
            r.hudState.selectedSlot = 0;
            r.hudScreen.TickHud();
            t.Equal(r.hudAnim.toolTimer, 40, "a newly held item shows its name for 40 ticks");
            r.hudScreen.TickHud();
            r.hudScreen.TickHud();
            t.Equal(r.hudAnim.toolTimer, 38, "the name timer counts down while the same item stays held");
            r.hudState.hotbar[0].name = "minecraft:stone";
            r.hudScreen.TickHud();
            t.Equal(r.hudAnim.toolTimer, 40, "switching items restarts the name timer");
            r.hudState.hotbar[0] = {};
            r.hudScreen.TickHud();
            t.Equal(r.hudAnim.toolTimer, 0, "an empty hand hides the name");
        }
        return t.Finish();
    }
}
