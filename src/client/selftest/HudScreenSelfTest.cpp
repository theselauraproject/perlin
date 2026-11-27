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
            r.chat.AddMessage("hello", 0);
            r.hudAnim.ticks = 1000;
            r.Fill();
            t.Check(r.hudState.chatLines.empty(), "old chat lines fade out of the hud");
            r.screens.Open(r.pause);
            r.pause.view = 1;
            r.pause.optionsUi.page = 12;
            r.Fill();
            t.Check(r.hudState.chatLines.size() == 1 && r.hudState.chatLines[0].alpha == 1.0f,
                    "the chat settings page shows faded chat lines at full opacity");
            t.Check(!r.hudState.chatOpen, "the chat settings preview does not open the chat");
            r.pause.optionsUi.page = 0;
            r.Fill();
            t.Check(r.hudState.chatLines.empty(), "other option pages leave faded chat hidden");
            r.pause.optionsUi.page = 12;
            r.screens.Close(r.pause);
            r.Fill();
            t.Check(r.hudState.chatLines.empty(), "closing the pause screen ends the chat preview");
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
            t.Equal(r.hudAnim.blinkUntil, 22, "regaining health during the blink leaves it running");

            for (int i = 0; i < 30; i++)
                r.hudScreen.TickHud();
            t.Equal(r.hudAnim.displayHealth, 16, "displayed health catches up once the blink window passes");

            r.hudState.health = 18.0f;
            r.hudScreen.TickHud();
            t.Check(r.hudAnim.blinkUntil < static_cast<int>(r.hudAnim.ticks), "healing never blinks the hearts");
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
        {
            Rig r;
            r.session->BeginReplayMode("ui", 7, kBedrockProtocol);
            proto::GuiDataPickItemPacket pick;
            pick.itemName = "Server item name";
            pick.itemEffects = "Server item effects";
            pick.hotbarSlot = 8;
            r.session->InjectIncoming(pick.kId, net::Encode(pick));
            r.Fill();
            t.Equal(r.hudState.itemName, std::string("Server item name\nServer item effects"),
                    "pick-item text reaches the item text HUD even with an empty hand");
            t.Equal(r.hudState.selectedSlot, 0, "presentation does not select the flash slot");
            const uint32_t serial = r.hudState.itemNameSerial;
            r.Fill();
            t.Equal(r.hudState.itemNameSerial, serial, "render frames do not replay a consumed popup");
            for (int i = 0; i < 40; ++i)
                r.hudScreen.TickHud();
            r.Fill();
            t.Check(r.hudState.itemName.empty(), "popup presentation expires independently of inventory");
            r.session->InjectIncoming(pick.kId, net::Encode(pick));
            r.Fill();
            r.session->ResetServerGui();
            r.Fill();
            t.Check(r.hudState.itemName.empty(), "reset does not leave a popup from the previous server");
        }
        {
            Rig r;
            r.session->BeginReplayMode("ui", 7, kBedrockProtocol);
            proto::SetPlayerInventoryOptionsPacket options;
            options.leftTab = static_cast<decltype(options.leftTab)>(5);
            options.filtering = true;
            options.layout = static_cast<decltype(options.layout)>(2);
            options.craftingLayout = static_cast<decltype(options.craftingLayout)>(1);
            r.session->InjectIncoming(options.kId, net::Encode(options));
            r.inventory.ApplyServerOptions(*r.session);
            t.Check(r.inventory.creative.tab == ui::kCreativeSearchTab && r.inventory.book &&
                        r.inventory.recipeFilter,
                    "inventory options select the server tab, book layout and craftable filter");
            r.inventory.book = false;
            r.inventory.ApplyServerOptions(*r.session);
            t.Check(!r.inventory.book, "unchanged server preferences do not overwrite subsequent UI input");
            r.inventory.workbench = true;
            r.inventory.ApplyServerOptions(*r.session);
            t.Check(!r.inventory.book, "crafting-table layout is independent of inventory layout");
            options.craftingLayout = static_cast<decltype(options.craftingLayout)>(3);
            options.filtering = false;
            r.session->InjectIncoming(options.kId, net::Encode(options));
            r.inventory.ApplyServerOptions(*r.session);
            t.Check(r.inventory.book && !r.inventory.recipeFilter, "a new server snapshot replaces UI preferences");
        }
        {
            Rig r;
            r.session->BeginReplayMode("locator", 7, kBedrockProtocol);
            r.camera.x = r.camera.y = r.camera.z = 0;
            r.camera.yaw = 0;
            proto::PlayerLocationPacket location;
            location.entityUniqueId = 100;
            location.type = proto::PlayerLocationPacketType::Coordinates;
            location.position = proto::Vec3F{10, 0, 0};
            r.session->InjectIncoming(location.kId, net::Encode(location));
            r.Fill();
            t.Check(r.hudState.locatorActive && r.hudState.locator.size() == 1 &&
                        r.hudState.locator.front().distance == 10,
                    "an unloaded player's received location reaches the HUD locator");
            location.type = proto::PlayerLocationPacketType::TypeHide;
            location.position.reset();
            r.session->InjectIncoming(location.kId, net::Encode(location));
            r.Fill();
            t.Check(!r.hudState.locatorActive && r.hudState.locator.empty(),
                    "a hidden location removes its HUD marker");
        }
        return t.Finish();
    }
}
