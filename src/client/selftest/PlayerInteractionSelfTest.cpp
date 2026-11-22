#include "client/features/interaction/PlayerInteraction.hpp"
#include "client/selftest/Check.hpp"
#include "client/selftest/SelfTestGlobals.hpp"
#include "client/selftest/SelfTests.hpp"
#include "network/Stream.hpp"
#include "network/Transport.hpp"

#include <variant>

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
            r.hudState.food = 20;
            f = FrameContext{};
            f.live = &session;
            r.interactionPlayer.UpdateEatingAndDrinking(f, session);
            t.Check(f.blockedFoodUse, "food cannot be eaten while the hunger bar is full");

            r.hudState.food = 12;
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
        {
            Rig r;
            BedrockSession session;
            session.BeginReplayMode("eater", 1, kBedrockProtocol);
            std::vector<proto::InventoryTransactionPacket> sent;
            session.SetSentTap([&](uint32_t id, const std::vector<uint8_t>& body) {
                if (id != proto::InventoryTransactionPacket::kId)
                    return;
                net::ReadStream in(body.data(), body.size());
                proto::InventoryTransactionPacket p;
                if (proto::Read(in, p))
                    sent.push_back(std::move(p));
            });
            const auto use = [&](size_t i) {
                return i < sent.size() ? std::get_if<proto::TransactionUseItem>(&sent[i].transaction.transactionData)
                                       : nullptr;
            };
            const auto release = [&](size_t i) {
                return i < sent.size()
                           ? std::get_if<proto::TransactionTransactionDataCase2>(&sent[i].transaction.transactionData)
                           : nullptr;
            };
            r.input.SetCaptured(true);
            r.hudState.mode = ui::GameMode::Survival;
            r.hudState.food = 12;
            r.hudState.hotbar[0].name = "minecraft:apple";
            r.hudState.hotbar[0].count = 3;

            r.Press(Action::Use);
            FrameContext f;
            f.live = &session;
            r.interactionPlayer.UpdateEatingAndDrinking(f, session);
            t.Check(f.earlyItemUse && r.interactionPlayer.usingItem, "pressing use with food starts eating");
            t.Equal(sent.size(), size_t{1}, "starting to eat sends one transaction");
            const auto* start = use(0);
            t.Check(start && start->actionType == proto::TransactionUseItemActionType::ClickAir,
                    "eating starts with a click in the air");
            t.Check(start && start->triggerType == proto::TransactionUseItemTriggerType::Unknown,
                    "the click in the air leaves the trigger type unset");
            t.Check(start && start->clientPrediction == proto::TransactionUseItemClientPrediction::Failure,
                    "the click in the air leaves the client prediction unset");
            t.Check(start && start->face == 255 && start->hand == proto::TransactionUseItemHand::MainHand,
                    "the click in the air has no face and uses the main hand");

            r.Idle();
            f = FrameContext{};
            f.live = &session;
            r.interactionPlayer.UpdateEatingAndDrinking(f, session);
            t.Check(!r.interactionPlayer.usingItem, "letting go of use stops eating");
            t.Equal(sent.size(), size_t{2}, "letting go early sends one release");
            const auto* early = release(1);
            t.Check(early && early->actionType == proto::TransactionTransactionDataCase2ActionType::Release,
                    "letting go early releases the item instead of consuming it");

            sent.clear();
            const float eye[3] = {1, 2, 3};
            session.SetUseOffhand(true);
            session.SendUseItemInAir(eye, true);
            session.SendReleaseItem(eye);
            t.Check(use(0) && use(0)->hotbarSlot == 0 && use(0)->hand == proto::TransactionUseItemHand::OffHand,
                    "an off-hand click in the air reports hotbar slot 0");
            t.Check(release(1) && release(1)->hotbarSlot == 0 && release(1)->hand == 1 &&
                        release(1)->actionType == proto::TransactionTransactionDataCase2ActionType::Release,
                    "an off-hand release reports hotbar slot 0 and never consumes");
            t.Check(use(0) && use(0)->playerPos.x == 1 && use(0)->playerPos.y == 2 && use(0)->playerPos.z == 3,
                    "before any movement input the given position is used");
            session.SetSentTap({});
        }
        {
            Rig r;
            BedrockSession session;
            session.BeginReplayMode("dropper", 1, kBedrockProtocol);
            std::vector<std::string> swings;
            bool dropSent = false;
            session.SetSentTap([&](uint32_t id, const std::vector<uint8_t>& body) {
                dropSent = dropSent || id == proto::ItemStackRequestPacket::kId ||
                           id == proto::InventoryTransactionPacket::kId;
                if (id != proto::AnimatePacket::kId)
                    return;
                net::ReadStream in(body.data(), body.size());
                proto::AnimatePacket p;
                if (proto::Read(in, p) && p.actionId == proto::AnimatePacketActionId::SwingArm)
                    swings.push_back(p.swingSource.value_or(std::string()));
            });
            const uint32_t before = r.interactionPlayer.selfSwing;
            r.interactionPlayer.DropSelected(session, false);
            t.Check(!dropSent && swings.empty(), "dropping from an empty slot neither drops nor swings");
            t.Equal(r.interactionPlayer.selfSwing, before, "dropping from an empty slot keeps the arm still");

            proto::InventorySlotPacket slot;
            slot.windowId = proto::WindowIDVarint::Inventory;
            slot.slot = 0;
            slot.item.networkId = 5;
            slot.item.count = 3;
            session.InjectIncoming(proto::InventorySlotPacket::kId, net::Encode(slot));
            r.interactionPlayer.DropSelected(session, false);
            t.Check(dropSent, "dropping a held item sends the drop");
            t.Equal(r.interactionPlayer.selfSwing, before + 1, "dropping swings the local arm");
            t.Check(swings.size() == 1 && swings[0] == "dropitem", "dropping tells the server the arm swung");
            session.SetSentTap({});
        }
        return t.Finish();
    }
}
