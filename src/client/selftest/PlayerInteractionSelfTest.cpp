#include "client/features/interaction/PlayerInteraction.hpp"
#include "client/selftest/Check.hpp"
#include "client/selftest/SelfTestGlobals.hpp"
#include "client/selftest/SelfTests.hpp"
#include "client/selftest/ItemFixtures.hpp"
#include "client/selftest/MovementProtocolFixtures.hpp"
#include "client/systems/player/AmmoRules.hpp"
#include "network/Stream.hpp"
#include "network/Transport.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>
#include <variant>

namespace perlin::interaction {
    struct InteractionTestAccess {
        static void Hotbar(Player& player, FrameContext& frame) {
            player.UpdateHotbar(frame);
        }
        static void BlockUse(Player& player, FrameContext& frame, const View& view,
                             const ui::HudItem& held) {
            player.view = view;
            player.mining.useClock += frame.dt;
            player.UpdateBlockUse(frame, held, false);
        }
        static void ReadyUseRepeat(Player& player) {
            player.mining.useClock = 0.2;
            player.lastUseSent = {};
        }
    };
}

namespace {
    using namespace perlin;

    proto::ItemV4 EnchantedUseItem(int16_t id, int16_t enchant, int16_t level) {
        proto::ItemV4 item;
        item.networkId = id;
        item.count = 1;
        proto::ItemExtraDataWithoutBlockingTick extra;
        extra.hasNbt = proto::ItemExtraDataWithoutBlockingTickHasNbt::True;
        extra.nbt.emplace();
        extra.nbt->version = 1;
        extra.nbt->nbt.bytes = {
            10, 0, 0, 9, 4, 0, 'e', 'n', 'c', 'h', 10, 1, 0, 0, 0,
            2, 2, 0, 'i', 'd', static_cast<uint8_t>(enchant), static_cast<uint8_t>(enchant >> 8),
            2, 3, 0, 'l', 'v', 'l', static_cast<uint8_t>(level), static_cast<uint8_t>(level >> 8), 0, 0};
        item.extra = std::move(extra);
        return item;
    }

    proto::ItemstatesEntry CustomUseItem(bool useModifiers, std::optional<float> movementModifier) {
        proto::ItemstatesEntry item;
        item.name = "test:timed_use";
        item.runtimeId = 90;
        item.componentBased = true;
        item.version = proto::ItemstatesEntryVersion::DataDriven;
        net::WriteStream stream(item.nbt.bytes);
        const auto byte = [&](uint8_t value) { stream.u8(value); };
        const auto text = [&](std::string value) {
            uint32_t length = static_cast<uint32_t>(value.size());
            stream.varint(length);
            stream.chars(value, value.size());
        };
        byte(10);
        text("");
        if (useModifiers) {
            byte(10);
            text("minecraft:use_modifiers");
            byte(5);
            text("use_duration");
            float duration = 2.0f;
            stream.le(duration);
            if (movementModifier) {
                byte(5);
                text("movement_modifier");
                stream.le(*movementModifier);
            }
            byte(0);
        } else {
            byte(3);
            text("use_duration");
            int32_t duration = 40;
            stream.zigzag(duration);
        }
        byte(0);
        return item;
    }

    proto::ItemV4 ChargedCrossbow() {
        auto item = EnchantedUseItem(10, 0, 0);
        auto& bytes = std::get<proto::ItemExtraDataWithoutBlockingTick>(item.extra).nbt->nbt.bytes;
        const std::string name = "minecraft:arrow";
        bytes = {10, 0, 0, 10, 11, 0, 'c', 'h', 'a', 'r', 'g', 'e', 'd', 'I', 't', 'e', 'm',
                 8, 4, 0, 'N', 'a', 'm', 'e', static_cast<uint8_t>(name.size()), 0};
        bytes.insert(bytes.end(), name.begin(), name.end());
        bytes.insert(bytes.end(), {1, 5, 0, 'C', 'o', 'u', 'n', 't', 1, 0, 0});
        return item;
    }

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

    struct UseRig : Rig {
        BedrockSession session;
        struct Air final : physics::World {
            physics::BlockShape Query(int, int, int) const override { return {}; }
        } air;
        int attempts = 0, releases = 0, starts = 0;
        std::optional<proto::PlayerAuthInputPacket> lastMovement;
        bool decoded = true;

        UseRig() {
            session.BeginReplayMode("item-use-lifecycle", 7, 2193);
            MovementProtocolTestAccess::Initialize(session);
            MovementProtocolTestAccess::SettleColumn(session, 0, 0);
            session.SetPose(0.5f, 64, 0.5f, 0, 0);
            player.ReplayPlace(0.5, 64, 0.5);
            proto::ItemRegistryPacket registry;
            int16_t id = 10;
            for (const char* name : {"minecraft:crossbow", "minecraft:arrow", "minecraft:trident",
                                     "minecraft:apple", "minecraft:stone", "minecraft:ender_pearl",
                                     "minecraft:wind_charge"}) {
                proto::ItemstatesEntry item;
                item.name = name;
                item.runtimeId = id++;
                registry.itemstates.push_back(std::move(item));
            }
            session.InjectIncoming(registry.kId, net::Encode(registry));
            proto::InventorySlotPacket ammo;
            ammo.windowId = proto::WindowIDVarint::Inventory;
            ammo.slot = 9;
            ammo.item.networkId = 11;
            ammo.item.count = 4;
            session.InjectIncoming(ammo.kId, net::Encode(ammo));
            hudState.mode = ui::GameMode::Survival;
            hudState.food = 10;
            input.SetCaptured(true);
            session.SetSentTap([this](uint32_t id, const std::vector<uint8_t>& body) {
                net::ReadStream stream(body.data(), body.size());
                if (id == proto::InventoryTransactionPacket::kId) {
                    proto::InventoryTransactionPacket packet;
                    decoded &= proto::Read(stream, packet) && stream.remaining() == 0;
                    if (const auto* use = std::get_if<proto::TransactionUseItem>(&packet.transaction.transactionData))
                        attempts += use->actionType == proto::TransactionUseItemActionType::ClickAir;
                    releases += packet.transaction.transactionType == proto::TransactionTransactionType::ItemRelease;
                } else if (id == proto::PlayerAuthInputPacket::kId) {
                    proto::PlayerAuthInputPacket packet;
                    decoded &= proto::Read(stream, packet) && stream.remaining() == 0;
                    starts += std::ranges::find(packet.inputData, proto::InputData::StartUsingItem) !=
                              packet.inputData.end();
                    lastMovement = packet;
                }
            });
        }

        ~UseRig() { session.SetSentTap({}); }

        void Held(const proto::ItemV4& item, const std::string& name) {
            proto::InventorySlotPacket slot;
            slot.windowId = proto::WindowIDVarint::Inventory;
            slot.slot = static_cast<uint32_t>(hudState.selectedSlot);
            slot.item = item;
            session.InjectIncoming(slot.kId, net::Encode(slot));
            auto& held = hudState.hotbar[hudState.selectedSlot];
            held.name = name;
            held.count = item.count;
        }

        void Frame(bool blockUse = true) {
            inputManager.Poll();
            FrameContext frame;
            frame.live = &session;
            frame.dt = 0.05f;
            interactionPlayer.UpdateEatingAndDrinking(frame, session);
            MovementProtocolTestAccess::Tick(player, session, inputManager, camera, air);
            if (blockUse)
                interaction::InteractionTestAccess::BlockUse(
                    interactionPlayer, frame, View(false), hudState.hotbar[hudState.selectedSlot]);
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
            session.BeginReplayMode("cooldown-eater", 1, kBedrockProtocol);
            proto::ItemRegistryPacket registry;
            registry.itemstates.push_back(CooldownItem("minecraft:apple", 5, "test:food"));
            session.InjectIncoming(registry.kId, net::Encode(registry));
            proto::InventorySlotPacket slot;
            slot.windowId = proto::WindowIDVarint::Inventory;
            slot.slot = 0;
            slot.item.networkId = 5;
            slot.item.count = 3;
            session.InjectIncoming(slot.kId, net::Encode(slot));
            proto::ClientStartItemCooldownPacket cooldown{"test:food", 2};
            session.InjectIncoming(cooldown.kId, net::Encode(cooldown));
            int sent = 0;
            session.SetSentTap([&](uint32_t id, const std::vector<uint8_t>&) {
                if (id == proto::InventoryTransactionPacket::kId)
                    ++sent;
            });
            r.input.SetCaptured(true);
            r.hudState.mode = ui::GameMode::Survival;
            r.hudState.food = 12;
            r.hudState.hotbar[0].name = "minecraft:apple";
            r.hudState.hotbar[0].count = 3;
            r.Press(Action::Use);
            FrameContext f;
            f.live = &session;
            r.interactionPlayer.UpdateEatingAndDrinking(f, session);
            t.Check(f.earlyItemUse && !r.interactionPlayer.usingItem,
                    "a received food cooldown sends the use attempt without starting local eating");
            r.interactionPlayer.UpdateGameInput(f, r.View(false));
            t.Equal(sent, 1, "a cooldown-blocked early use is not sent a second time in block-use input");
            r.Idle();
            f = FrameContext{};
            f.live = &session;
            r.interactionPlayer.UpdateEatingAndDrinking(f, session);
            t.Equal(sent, 1, "releasing a cooldown-blocked use does not release an unstarted item action");
            session.TickItemCooldowns();
            session.TickItemCooldowns();
            r.Press(Action::Use);
            f = FrameContext{};
            f.live = &session;
            r.interactionPlayer.UpdateEatingAndDrinking(f, session);
            t.Check(f.earlyItemUse && r.interactionPlayer.usingItem,
                    "local eating starts again after the server cooldown expires");
            t.Equal(sent, 2, "the next use after cooldown sends one new attempt");
            session.SetSentTap({});
        }
        {
            Rig r;
            BedrockSession session;
            session.BeginReplayMode("held-food", 7, 2193);
            MovementProtocolTestAccess::Initialize(session);
            MovementProtocolTestAccess::SettleColumn(session, 0, 0);
            session.SetPose(0.5f, 64, 0.5f, 0, 0);
            r.player.ReplayPlace(0.5, 64, 0.5);
            proto::ItemRegistryPacket registry;
            proto::ItemstatesEntry definition;
            definition.name = "minecraft:golden_apple";
            definition.runtimeId = 45;
            registry.itemstates.push_back(definition);
            session.InjectIncoming(registry.kId, net::Encode(registry));
            proto::InventorySlotPacket slot;
            slot.windowId = proto::WindowIDVarint::Inventory;
            slot.slot = 0;
            slot.item.networkId = 45;
            slot.item.count = 3;
            session.InjectIncoming(slot.kId, net::Encode(slot));
            r.hudState.hotbar[0].name = "minecraft:golden_apple";
            r.hudState.hotbar[0].count = 3;
            r.hudState.mode = ui::GameMode::Survival;
            r.hudState.food = 20;
            r.input.SetCaptured(true);
            r.input.OnMouseDown(Mouse::Right);
            struct Air final : physics::World {
                physics::BlockShape Query(int, int, int) const override { return {}; }
            } air;
            int airUses = 0, releases = 0, legacyConsumes = 0;
            session.SetSentTap([&](uint32_t id, const std::vector<uint8_t>& body) {
                if (id != proto::InventoryTransactionPacket::kId)
                    return;
                net::ReadStream stream(body.data(), body.size());
                proto::InventoryTransactionPacket packet;
                if (!proto::Read(stream, packet))
                    return;
                if (const auto* use = std::get_if<proto::TransactionUseItem>(
                        &packet.transaction.transactionData))
                    airUses += use->actionType == proto::TransactionUseItemActionType::ClickAir;
                if (const auto* release = std::get_if<proto::TransactionTransactionDataCase2>(
                        &packet.transaction.transactionData)) {
                    releases += release->actionType == proto::TransactionTransactionDataCase2ActionType::Release;
                    legacyConsumes += release->actionType == proto::TransactionTransactionDataCase2ActionType::Consume;
                }
            });
            const auto frame = [&] {
                r.inputManager.Poll();
                FrameContext context;
                context.live = &session;
                context.dt = 0.05f;
                r.interactionPlayer.UpdateEatingAndDrinking(context, session);
                MovementProtocolTestAccess::Tick(r.player, session, r.inputManager, r.camera, air);
                interaction::InteractionTestAccess::BlockUse(
                    r.interactionPlayer, context, r.View(false), r.hudState.hotbar[0]);
            };
            for (int tick = 0; tick < 32; ++tick)
                frame();
            t.Check(airUses == 1 && releases == 0 && r.interactionPlayer.usingItem,
                    "held golden apple at full hunger starts once without repeated air-use attempts during eating");
            frame();
            t.Check(airUses == 2 || !r.interactionPlayer.usingItem,
                    "a golden apple finishes after exactly 32 ticks of use, not 33");
            for (int tick = 0; tick < 8; ++tick)
                frame();
            t.Check(airUses == 2 && r.interactionPlayer.usingItem,
                    "held food can restart through ordinary air use after the first timer completes");
            t.Equal(legacyConsumes, 0,
                    "client food continuation does not invent the server-only legacy Consume transaction");
            r.input.OnMouseUp(Mouse::Right);
            frame();
            t.Check(releases == 1 && !r.interactionPlayer.usingItem,
                    "releasing during the restarted use sends one cancellation and stops local use");
            frame();
            t.Check(airUses == 2 && releases == 1,
                    "an idle frame cannot restart eating or duplicate its release");
            session.SetSentTap({});
        }
        {
            for (const auto& [hasComponent, configured, expected] :
                 {std::tuple{false, std::optional<float>{}, 0.35f},
                  std::tuple{true, std::optional<float>{}, 1.0f},
                  std::tuple{true, std::optional<float>{0.5f}, 0.5f},
                  std::tuple{true, std::optional<float>{0.0f}, 0.0f}}) {
                UseRig r;
                proto::ItemRegistryPacket registry;
                registry.itemstates.push_back(CustomUseItem(hasComponent, configured));
                r.session.InjectIncoming(registry.kId, net::Encode(registry));
                CustomItemDef definition;
                t.Check(selftest::G().serverBlocks.Item("test:timed_use", definition) &&
                            definition.useTicks == 40 && definition.useMovementModifier == expected,
                        "received custom use metadata preserves explicit and component-dependent default modifiers");
                r.Held(EnchantedUseItem(90, 0, 0), "test:timed_use");
                r.input.OnKeyDown(r.options.GetKey(Action::Forward).code);
                r.input.OnMouseDown(Mouse::Right);
                r.Frame();
                r.Frame(false);
                t.Check(r.decoded && r.attempts == 1 && r.starts == 1 && r.interactionPlayer.usingItem &&
                            MovementProtocolTestAccess::ItemSlow(r.player),
                        "a received custom timed-use item starts the real held-use movement path");
                t.Check(r.lastMovement &&
                            std::fabs(r.lastMovement->moveVector.z - expected * expected) < 0.00001f,
                        "custom movement modifier reaches the actual auth-input vector and is squared once");
                r.input.OnMouseUp(Mouse::Right);
                r.Frame(false);
                t.Check(r.releases == 1 && !MovementProtocolTestAccess::ItemSlow(r.player) &&
                            r.lastMovement && std::fabs(r.lastMovement->moveVector.z - 1.0f) < 0.00001f,
                        "ending custom held use restores the ordinary movement vector");
            }
        }
        {
            UseRig r;
            r.Held(EnchantedUseItem(12, 30, 1), "minecraft:trident");
            r.input.OnMouseDown(Mouse::Right);
            r.Frame();
            r.Frame(false);
            t.Check(r.decoded && r.attempts == 1 && r.starts == 0 && !r.interactionPlayer.usingItem,
                    "dry Riptide sends the air-use attempt without starting held use or a movement start flag");
            r.input.OnMouseUp(Mouse::Right);
            r.Frame(false);
            t.Equal(r.releases, 0, "a rejected dry Riptide use has no active charge to release");
        }
        {
            UseRig r;
            r.Held(EnchantedUseItem(10, 35, 3), "minecraft:crossbow");
            r.input.OnMouseDown(Mouse::Right);
            r.Frame();
            for (int tick = 0; tick < 5; ++tick)
                r.Frame(false);
            t.Check(r.interactionPlayer.usingItem && r.starts == 1 && r.attempts == 1,
                    "Quick Charge III starts one crossbow use and remains active before its duration elapses");
            r.Held(EnchantedUseItem(10, 35, 5), "minecraft:crossbow");
            r.Frame(false);
            t.Check(r.interactionPlayer.usingItem,
                    "an enchantment update does not replace the duration captured for an already active use");
            for (int tick = 0; tick < 10; ++tick)
                r.Frame(false);
            t.Check(r.decoded && !r.interactionPlayer.usingItem && r.releases == 0 && r.attempts == 1,
                    "crossbow timer completion stops local use without inventing a release or second transaction");
            r.input.OnMouseUp(Mouse::Right);
            r.Frame(false);
            t.Equal(r.releases, 0, "releasing after timed completion cannot duplicate completion as a release");
            r.input.OnMouseDown(Mouse::Right);
            r.Frame();
            r.Frame(false);
            r.Frame(false);
            r.Frame(false);
            t.Check(!r.interactionPlayer.usingItem && r.attempts == 2 && r.starts == 2,
                    "the next use observes updated Quick Charge V and completes its zero-duration use");
            r.input.OnMouseUp(Mouse::Right);
            r.Frame(false);
            r.Held(EnchantedUseItem(10, 35, 6), "minecraft:crossbow");
            r.input.OnMouseDown(Mouse::Right);
            r.Frame();
            for (int tick = 0; tick < 15; ++tick)
                r.Frame(false);
            t.Check(r.interactionPlayer.usingItem && r.starts == 3,
                    "a negative native Quick Charge duration is retained instead of wrapping or expiring");
            r.input.OnMouseUp(Mouse::Right);
            r.Frame(false);
            t.Check(!r.interactionPlayer.usingItem && r.releases == 1,
                    "manual release still cancels a retained negative-duration use exactly once");
        }
        {
            for (const auto& [id, name, duration] :
                 {std::tuple{int16_t{15}, "minecraft:ender_pearl", 20},
                  std::tuple{int16_t{16}, "minecraft:wind_charge", 10}}) {
                UseRig r;
                auto stack = EnchantedUseItem(id, 0, 0);
                stack.count = 3;
                r.Held(stack, name);
                r.input.OnMouseDown(Mouse::Right);
                r.Frame();
                t.Check(r.attempts == 1 && !r.interactionPlayer.usingItem && r.starts == 0 &&
                            r.session.GetInventory().hotbar[0].count == 2 &&
                            r.session.ItemCooldownTicks(name) == duration,
                        "a thrown item predicts consumption/cooldown without entering timed use");
                for (int tick = 1; tick < duration; ++tick) {
                    interaction::InteractionTestAccess::ReadyUseRepeat(r.interactionPlayer);
                    r.Frame();
                }
                t.Check(r.session.GetInventory().hotbar[0].count == 2 &&
                            r.session.ItemCooldownTicks(name) == 1 && r.starts == 0,
                        "retained held-throw attempts cannot consume another item while cooldown remains active");
                interaction::InteractionTestAccess::ReadyUseRepeat(r.interactionPlayer);
                r.Frame();
                t.Check(r.session.GetInventory().hotbar[0].count == 1 &&
                            r.session.ItemCooldownTicks(name) == duration,
                        "continued held use throws again after the locally predicted cooldown expires");
                for (int tick = 0; tick < duration; ++tick) {
                    interaction::InteractionTestAccess::ReadyUseRepeat(r.interactionPlayer);
                    r.Frame();
                }
                t.Check(r.session.GetInventory().hotbar[0].Empty() && r.session.ItemCooldownTicks(name) == 0,
                        "last-item throwing empties the stack and does not create a post-use cooldown");
                const int attempts = r.attempts;
                for (int tick = 0; tick < 5; ++tick) {
                    interaction::InteractionTestAccess::ReadyUseRepeat(r.interactionPlayer);
                    r.Frame();
                }
                t.Check(r.attempts == attempts && r.releases == 0,
                        "an empty thrown stack clears the retained intention without creating a release transaction");
            }
        }
        {
            for (bool eventFirst : {false, true}) {
                UseRig r;
                r.Held(EnchantedUseItem(10, 0, 0), "minecraft:crossbow");
                r.input.OnMouseDown(Mouse::Right);
                r.Frame();
                r.Frame(false);
                t.Check(r.attempts == 1 && r.starts == 1 && r.interactionPlayer.usingItem,
                        "an uncharged crossbow starts one active loading use");
                proto::EntityEventPacket complete;
                complete.runtimeEntityId = 7;
                complete.eventId = proto::EntityEventPacketEventId::ChargedItem;
                const auto finished = [&] {
                    r.session.InjectIncoming(complete.kId, net::Encode(complete));
                };
                if (eventFirst) {
                    finished();
                    r.Frame(false);
                    r.Held(ChargedCrossbow(), "minecraft:crossbow");
                } else {
                    r.Held(ChargedCrossbow(), "minecraft:crossbow");
                    r.Frame(false);
                    finished();
                }
                r.Frame(false);
                t.Check(r.interactionPlayer.usingItem && r.releases == 0,
                        "charge-finish cancels retained input without fabricating a release or cancelling the active timer");
                for (int tick = 0; tick < 35; ++tick) {
                    interaction::InteractionTestAccess::ReadyUseRepeat(r.interactionPlayer);
                    r.Frame();
                }
                t.Check(r.decoded && r.attempts == 1 && r.starts == 1 && r.releases == 0 &&
                            !r.interactionPlayer.usingItem && !r.session.TakeChargedItemUseReset(),
                        "charged-item event and NBT in either order prevent same-press crossbow auto-fire after completion");
                r.input.OnMouseUp(Mouse::Right);
                r.Frame();
                r.input.OnMouseDown(Mouse::Right);
                r.Frame();
                t.Check(r.attempts == 2 && r.starts == 1 && !r.interactionPlayer.usingItem,
                        "a fresh press fires the loaded crossbow without starting another charge");
                for (int tick = 0; tick < 8; ++tick) {
                    interaction::InteractionTestAccess::ReadyUseRepeat(r.interactionPlayer);
                    r.Frame();
                }
                t.Check(r.attempts == 2 && r.releases == 0,
                        "the completed charge latch cannot restart loaded firing after physical release and a fresh press");
            }
        }
        {
            UseRig r;
            auto apple = EnchantedUseItem(13, 0, 0);
            apple.count = 3;
            r.Held(apple, "minecraft:apple");
            r.input.OnMouseDown(Mouse::Right);
            r.Frame();
            apple.count = 2;
            apple.metadata = 7;
            r.Held(apple, "minecraft:apple");
            r.hudState.hotbar[0].aux = 7;
            r.hudState.food = 20;
            r.Frame(false);
            t.Check(r.interactionPlayer.usingItem && r.attempts == 1 && r.releases == 0 &&
                        MovementProtocolTestAccess::ItemSlow(r.player),
                    "nonempty count, aux and hunger changes preserve active food use and its movement slowdown");
            r.hudState.hotbar[1] = r.hudState.hotbar[0];
            r.Press(static_cast<Action>(static_cast<int>(Action::Hotbar1) + 1));
            FrameContext hotbarFrame;
            hotbarFrame.live = &r.session;
            interaction::InteractionTestAccess::Hotbar(r.interactionPlayer, hotbarFrame);
            t.Check(!r.interactionPlayer.usingItem && r.releases == 0 && r.hudState.selectedSlot == 1 &&
                        !MovementProtocolTestAccess::ItemSlow(r.player),
                    "changing slots cancels the old use locally without releasing a weapon or sending a consume");
            r.Frame(false);
            t.Check(r.attempts == 1 && r.releases == 0,
                    "a held key cannot reuse an old use timer after selection changes");
        }
        {
            UseRig r;
            r.Held(EnchantedUseItem(12, 30, 0), "minecraft:trident");
            r.input.OnMouseDown(Mouse::Right);
            MovementProtocolTestAccess::ReplayTransport(r.session, false);
            r.Frame();
            t.Check(!r.interactionPlayer.usingItem, "a failed air-use send does not start local held use");
            MovementProtocolTestAccess::ReplayTransport(r.session, true);
            r.Frame(false);
            t.Check(r.decoded && r.starts == 0 && r.attempts == 1,
                    "a failed air-use send cannot leak StartUsingItem into a later movement packet");
        }
        {
            // The backend can repeat a button-down notification while that
            // button is already held. It must not become a second press or a
            // second placement on the next render frame.
            Rig r;
            BedrockSession session;
            session.BeginReplayMode("single-placement", 1, kBedrockProtocol);
            auto& palette = G().blockPalette;
            const bool sequential = palette.Sequential();
            palette.SetSequential(false);
            const int32_t stoneHash = BlockPalette::HashState("minecraft:stone", {});
            palette.LearnName(stoneHash, "minecraft:stone");
            const uint16_t stone = palette.Intern(stoneHash);
            session.NetLevel().EnsureChunk(0, 0);
            for (int x : {2, 4, 6})
                session.NetLevel().SetBlock(x, 64, 2, stone);
            proto::ItemRegistryPacket registry;
            proto::ItemstatesEntry definition;
            definition.name = "minecraft:stone";
            definition.runtimeId = 55;
            registry.itemstates.push_back(definition);
            session.InjectIncoming(registry.kId, net::Encode(registry));
            proto::InventorySlotPacket slot;
            slot.windowId = proto::WindowIDVarint::Inventory;
            slot.slot = 0;
            slot.item.networkId = 55;
            slot.item.count = 64;
            slot.item.blockRuntimeId = static_cast<uint32_t>(stoneHash);
            session.InjectIncoming(slot.kId, net::Encode(slot));
            r.hudState.hotbar[0].name = "minecraft:stone";
            r.hudState.hotbar[0].count = 64;
            r.hudState.hotbar[0].blockRuntimeId = static_cast<uint32_t>(stoneHash);
            std::vector<proto::TransactionUseItem> placements;
            session.SetSentTap([&](uint32_t id, const std::vector<uint8_t>& body) {
                if (id != proto::InventoryTransactionPacket::kId)
                    return;
                net::ReadStream stream(body.data(), body.size());
                proto::InventoryTransactionPacket packet;
                if (proto::Read(stream, packet))
                    if (const auto* use = std::get_if<proto::TransactionUseItem>(
                            &packet.transaction.transactionData))
                        placements.push_back(*use);
            });
            const auto frame = [&](int x) {
                r.inputManager.Poll();
                r.interactionPlayer.aimHit.kind = AimHit::Kind::Block;
                r.interactionPlayer.aimHit.bx = x;
                r.interactionPlayer.aimHit.by = 64;
                r.interactionPlayer.aimHit.bz = 2;
                r.interactionPlayer.aimHit.face = 1;
                r.interactionPlayer.aimHit.point = {x + 0.5, 65, 2.5};
                FrameContext context;
                context.live = &session;
                interaction::InteractionTestAccess::BlockUse(
                    r.interactionPlayer, context, r.View(false), r.hudState.hotbar[0]);
            };
            r.input.SetCaptured(true);
            r.input.OnMouseDown(Mouse::Right);
            frame(2);
            t.Check(placements.size() == 1 &&
                        placements[0].triggerType == proto::TransactionUseItemTriggerType::PlayerInput &&
                        session.GetInventory().hotbar[0].count == 63,
                    "one right-button press emits one placement and predicts one item decrement");
            r.input.OnMouseDown(Mouse::Right);
            frame(4);
            t.Check(placements.size() == 1 && session.GetInventory().hotbar[0].count == 63,
                    "duplicate down notification while held cannot place a second block");
            r.input.OnMouseUp(Mouse::Right);
            frame(4);
            t.Equal(placements.size(), size_t{1}, "releasing the button never places a block");
            r.input.OnMouseDown(Mouse::Right);
            frame(6);
            t.Check(placements.size() == 2 && session.GetInventory().hotbar[0].count == 62,
                    "a genuine second press remains responsive without a blanket click cooldown");
            r.input.OnMouseUp(Mouse::Right);
            frame(6);
            const int32_t chestHash = BlockPalette::HashState("minecraft:chest", {});
            palette.LearnName(chestHash, "minecraft:chest");
            session.NetLevel().SetBlock(8, 64, 2, palette.Intern(chestHash));
            definition.name = "minecraft:bow";
            definition.runtimeId = 56;
            registry.itemstates.push_back(definition);
            session.InjectIncoming(registry.kId, net::Encode(registry));
            slot.item.networkId = 56;
            slot.item.count = 1;
            slot.item.blockRuntimeId = 0;
            session.InjectIncoming(slot.kId, net::Encode(slot));
            r.hudState.hotbar[0].name = "minecraft:bow";
            r.hudState.hotbar[0].count = 1;
            r.hudState.hotbar[0].blockRuntimeId = 0;
            r.hudState.mode = ui::GameMode::Survival;
            t.Check(!detail::HasAmmo(r.hudState, &session, "minecraft:bow"),
                    "the interaction fixture holds a bow with no ammunition");
            r.input.OnMouseDown(Mouse::Right);
            frame(8);
            t.Check(placements.size() == 3 && placements.back().blockPosition.x == 8 &&
                        placements.back().clientPrediction == proto::TransactionUseItemClientPrediction::Success,
                    "an empty bow can still interact with a chest before its own item-use checks");
            session.SetSentTap({});
            palette.SetSequential(sequential);
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
