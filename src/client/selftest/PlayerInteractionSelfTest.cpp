#include "client/features/interaction/PlayerInteraction.hpp"
#include "client/selftest/Check.hpp"
#include "client/selftest/SelfTestGlobals.hpp"
#include "client/selftest/SelfTests.hpp"

namespace {
    using namespace perlin;

    struct Rig {
        Options options;
        Input input;
        InputManager inputManager{input, options};
        Camera camera;
        LocalPlayer player;
        ui::HudState hudState;
        entity::EntityRenderer entityRenderer;
        HandRenderer handRenderer;
        audio::AudioEngine engine;
        audio::SoundSystem sounds{engine};
        particle::ParticleSystem particles;
        WeatherRenderer weather;
        std::unique_ptr<LevelRenderer> levelRenderer;
        float rain = 0.0f, previousRain = 0.0f;
        int previewSlot = 0;
        bool replaying = false;
        std::vector<abi::InteractKind> modInteractions;
        bool modConsumes = false;
        int vibrations = 0;
        audio::AudioDirector director{audio::AudioDirector::Deps{
            options, sounds, engine, camera, player, hudState, selftest::G().blockPalette,
            [](const BedrockSession::SoundRequest&) {}, [] { return std::string(); },
            [] { return std::vector<std::string>{}; }, [] { return 0; }}};
        interaction::Player interactionPlayer{interaction::Player::Deps{
            options, selftest::G().blockPalette, selftest::G().serverBlocks, input, inputManager, camera, player,
            hudState, entityRenderer, handRenderer, sounds, director, particles, levelRenderer, weather, rain,
            previousRain, previewSlot, [this] { return replaying; },
            [this](abi::InteractKind kind, int, int, int, int, uint64_t) {
                modInteractions.push_back(kind);
                return modConsumes;
            },
            [this] { vibrations++; }}};
        std::unique_ptr<Level> previewLevel;
        std::vector<entity::Entity> entities;
        uint64_t tick = 0;

        interaction::View View(bool preview) {
            interaction::View v;
            v.previewLevel = &previewLevel;
            v.entities = &entities;
            v.preview = preview;
            return v;
        }

        void Press(Action action) {
            std::bitset<kActionCount> bits;
            bits.set(static_cast<size_t>(action));
            tick += 2;
            inputManager.SetControlTick(tick);
            inputManager.SetControlActions(bits, tick + 1);
            inputManager.Poll();
        }

        void Idle() {
            tick += 2;
            inputManager.SetControlTick(tick);
            inputManager.SetControlActions({}, tick);
            inputManager.Poll();
        }

        void Update(BedrockSession* live, bool preview = false) {
            FrameContext f;
            f.live = live;
            interactionPlayer.UpdateGameInput(f, View(preview));
        }
    };
}

namespace perlin::selftest {
    int PlayerInteraction() {
        Checker t("player_interaction_test");

        {
            Rig r;
            const bool shown = r.options.showHud;
            r.Press(Action::ToggleHud);
            r.Update(nullptr);
            t.Check(r.options.showHud != shown, "the hud toggle works even while input is not captured");
        }
        {
            Rig r;
            r.input.SetCaptured(false);
            r.Press(Action::Hotbar1);
            r.interactionPlayer.aimHit.kind = AimHit::Kind::Block;
            r.Update(nullptr, true);
            t.Check(r.interactionPlayer.aimHit.kind == AimHit::Kind::None, "uncaptured input clears the aim");
            t.Equal(r.hudState.selectedSlot, 0, "uncaptured input ignores hotbar keys");
            t.Check(!r.interactionPlayer.localCrack.active, "uncaptured input stops local cracking");
        }
        {
            Rig r;
            r.input.SetCaptured(true);
            r.replaying = true;
            r.interactionPlayer.aimHit.kind = AimHit::Kind::Block;
            r.Press(Action::Hotbar1);
            r.Update(nullptr, true);
            t.Check(r.interactionPlayer.aimHit.kind == AimHit::Kind::None, "replay playback suppresses gameplay input");
        }
        {
            Rig r;
            r.input.SetCaptured(true);
            r.Update(nullptr, false);
            t.Equal(r.hudState.selectedSlot, 0, "no session and no preview leaves the slot alone");
        }
        {
            Rig r;
            r.input.SetCaptured(true);
            r.Press(Action::Hotbar1);
            r.Press(static_cast<Action>(static_cast<int>(Action::Hotbar1) + 4));
            r.Update(nullptr, true);
            t.Equal(r.hudState.selectedSlot, 4, "pressing hotbar 5 selects slot 4");
            t.Equal(r.previewSlot, 4, "preview mode records the selected slot");
            r.Idle();
            r.Update(nullptr, true);
            t.Equal(r.hudState.selectedSlot, 4, "selection persists without input");
        }
        {
            Rig r;
            r.input.SetCaptured(true);
            r.hudState.selectedSlot = 0;
            r.input.OnMouseWheel(1.0f);
            r.Idle();
            r.Update(nullptr, true);
            t.Equal(r.hudState.selectedSlot, 8, "scrolling up from the first slot wraps to the last");
            r.input.OnMouseWheel(-1.0f);
            r.Idle();
            r.Update(nullptr, true);
            t.Equal(r.hudState.selectedSlot, 0, "scrolling down moves to the next slot and wraps");
            r.input.OnMouseWheel(-3.0f);
            r.Idle();
            r.Update(nullptr, true);
            t.Equal(r.hudState.selectedSlot, 3, "several wheel notches in one frame advance several slots");
        }
        {
            Rig r;
            const uint32_t before = r.interactionPlayer.selfSwing;
            r.interactionPlayer.SwingHand();
            t.Equal(r.interactionPlayer.selfSwing, before + 1, "swinging increments the swing counter");
        }
        {
            Rig r;
            r.interactionPlayer.StopBreaking(nullptr);
            t.Check(!r.interactionPlayer.localCrack.active, "stopping with nothing mined is harmless");
            r.interactionPlayer.localCrack.active = true;
            r.interactionPlayer.StopBreaking(nullptr);
            t.Check(!r.interactionPlayer.localCrack.active, "stopping breaking clears the local crack");
        }
        {
            Rig r;
            BedrockSession session;
            r.input.SetCaptured(true);
            r.hudState.mode = ui::GameMode::Creative;
            r.hudState.hotbar[0].name = "minecraft:apple";
            r.hudState.hotbar[0].count = 3;
            FrameContext f;
            f.live = &session;
            r.interactionPlayer.UpdateEatingAndDrinking(f, session);
            t.Check(f.blockedFoodUse, "food cannot be eaten in creative mode");

            r.hudState.mode = ui::GameMode::Survival;
            f = FrameContext{};
            f.live = &session;
            r.interactionPlayer.UpdateEatingAndDrinking(f, session);
            t.Check(!f.blockedFoodUse, "food can be eaten in survival mode");
            t.Check(!f.earlyItemUse && !r.interactionPlayer.usingItem, "nothing is used while the use key is up");
            t.Check(r.modInteractions.empty(), "mods are not asked about an idle use key");

            r.hudState.hotbar[0].name = "minecraft:stone";
            f = FrameContext{};
            f.live = &session;
            r.interactionPlayer.UpdateEatingAndDrinking(f, session);
            t.Check(!f.blockedFoodUse, "non-food is never reported as blocked food");
        }
        return t.Finish();
    }
}
