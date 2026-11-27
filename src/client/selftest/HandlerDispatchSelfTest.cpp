#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/multiplayer/handlers/HandlerSupport.hpp"
#include "network/Stream.hpp"
#include "client/selftest/SelfTests.hpp"
#include "client/selftest/ItemFixtures.hpp"

#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
    int failures = 0;

    void Check(bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            failures++;
        }
    }

    template <class P> void Inject(perlin::BedrockSession& s, const P& packet) {
        s.InjectIncoming(P::kId, perlin::net::Encode(packet));
    }

    struct InventoryRig {
        perlin::BedrockSession session;
        std::vector<perlin::proto::ItemStackRequest> requests;

        InventoryRig() {
            using namespace perlin;
            session.BeginReplayMode("inventory", 7, kBedrockProtocol);
            session.SetSentTap([this](uint32_t id, const std::vector<uint8_t>& bytes) {
                if (id != proto::ItemStackRequestPacket::kId)
                    return;
                proto::ItemStackRequestPacket packet;
                net::ReadStream stream(bytes.data(), bytes.size());
                Check(proto::Read(stream, packet) && stream.remaining() == 0, "inventory request encodes fully");
                for (const auto& request : packet.requests)
                    requests.push_back(request);
            });
            proto::ItemRegistryPacket registry;
            proto::ItemstatesEntry item;
            item.name = "minecraft:arrow";
            item.runtimeId = 5;
            registry.itemstates.push_back(item);
            item.name = "minecraft:saddle";
            item.runtimeId = 6;
            registry.itemstates.push_back(item);
            Inject(session, registry);
        }

        ~InventoryRig() { session.SetSentTap({}); }

        void Open(perlin::proto::WindowType type) {
            perlin::proto::ContainerOpenPacket packet;
            packet.windowId = static_cast<perlin::proto::WindowID>(1);
            packet.windowType = type;
            Inject(session, packet);
        }

        void Store(int window, perlin::proto::ContainerSlotType type, int slot, int count, int stackId) {
            using namespace perlin::proto;
            InventorySlotPacket packet;
            packet.windowId = static_cast<WindowIDVarint>(window);
            packet.container = FullContainerName{};
            packet.container->containerId = type;
            packet.slot = slot;
            packet.item.networkId = type == ContainerSlotType::HorseEquip && slot == 0 ? 6 : 5;
            packet.item.count = static_cast<uint16_t>(count);
            packet.item.hasStackId = true;
            packet.item.stackId = stackId;
            Inject(session, packet);
        }

        void Respond(perlin::proto::ContainerSlotType type, int slot, int count, int stackId) {
            using namespace perlin::proto;
            ItemStackResponsePacket packet;
            ItemStackResponsesEntry response;
            response.status = ItemStackResponsesEntryStatus::Ok;
            response.containers.emplace();
            ItemStackResponsesEntryContainersEntry container;
            container.slotType.containerId = type;
            ItemStackResponsesEntryContainersEntrySlotsEntry entry;
            entry.slot = static_cast<uint8_t>(slot);
            entry.hotbarSlot = static_cast<uint8_t>(slot);
            entry.count = static_cast<uint8_t>(count);
            if (count > 0)
                entry.itemStackId = stackId;
            container.slots.push_back(entry);
            response.containers->push_back(container);
            packet.responses.push_back(response);
            Inject(session, packet);
        }
    };
}

namespace perlin::selftest {
int HandlerDispatch() {
    {
        perlin::proto::MetadataDictionaryEntry e;
        e.value.emplace<1>(7);
        double d = 0;
        Check(perlin::handlers::MetaNumber(e, d) && d == 7.0, "MetaNumber reads integer metadata");
        Check(perlin::handlers::SaneWorldPos(1, 2, 3), "sane position accepted");
        Check(!perlin::handlers::SaneWorldPos(1e9f, 0, 0), "huge position rejected");
        Check(!perlin::handlers::SaneWorldPos(0, std::nanf(""), 0), "nan rejected");
        perlin::proto::MovePlayerPacket move;
        move.position = {200000, 20000, -200000};
        Check(perlin::handlers::SanePlayerMove(move),
              "MovePlayer accepts positions beyond the former local limits");
        move.position.x = 33554432.0f;
        Check(!perlin::handlers::SanePlayerMove(move), "MovePlayer position limit is exclusive");
        move.position.x = 0;
        move.headYaw = 33554432.0f;
        Check(perlin::handlers::SanePlayerMove(move), "MovePlayer head yaw limit is inclusive");
        move.headYaw = std::nextafter(move.headYaw, std::numeric_limits<float>::infinity());
        Check(!perlin::handlers::SanePlayerMove(move), "MovePlayer head yaw over the limit is rejected");
        Check(perlin::handlers::EqualsIgnoreCase("AbC", "aBc"), "case-insensitive equal");
        Check(!perlin::handlers::EqualsIgnoreCase("abc", "abd"), "different strings unequal");
        Check(!perlin::handlers::EqualsIgnoreCase("ab", "abc"), "different lengths unequal");
        perlin::proto::Uuid u{};
        u.bytes[0] = 0xab;
        u.bytes[15] = 0x01;
        const std::string key = perlin::handlers::UuidKey(u);
        Check(key.size() == 32 && key.substr(0, 2) == "ab" && key.substr(30) == "01", "uuid key format");
    }
    {
        perlin::BedrockSession session;
        Check(session.World().difficulty == 2, "difficulty default");
        perlin::proto::SetDifficultyPacket diff;
        diff.difficulty = 3;
        Inject(session, diff);
        Check(session.World().difficulty == 3, "level handler: set_difficulty applied");

        perlin::proto::SetPlayerGameTypePacket gt;
        gt.gamemode = static_cast<decltype(gt.gamemode)>(1);
        Inject(session, gt);
        Check(session.GameType() == 1, "player-rule handler: set_player_game_type applied");

        perlin::proto::SetTimePacket t;
        t.time = 6000;
        Inject(session, t);
        int64_t day = 0;
        int ticks = 0;
        Check(session.GetWorldTime(day, ticks) && day == 0 && ticks >= 6000 && ticks < 6100,
              "world-state handler: set_time applied");

        session.InjectIncoming(perlin::proto::SetDifficultyPacket::kId, {});
        Check(session.World().difficulty == 3, "truncated packet leaves state unchanged");
        session.InjectIncoming(0x7fffffff, {1, 2, 3});
        Check(session.World().difficulty == 3, "unknown packet id ignored");

        perlin::proto::SetDifficultyPacket again;
        again.difficulty = 0;
        Inject(session, again);
        Check(session.World().difficulty == 0, "later packet supersedes earlier state");
    }
    {
        perlin::BedrockSession session;
        session.Entities().AddItem(7, 70, {1, 2, 3}, "minecraft:stone", 1, 0, 0, false);
        perlin::proto::TakeItemEntityPacket take;
        take.runtimeEntityId = 7;
        take.target = 99;
        Inject(session, take);
        const auto sounds = session.TakeSounds();
        Check(sounds.size() == 1 && sounds[0].name == "pop" &&
                  sounds[0].kind == perlin::BedrockSession::SoundRequest::Kind::Named,
              "pickup by another collector pops");
        Check(sounds.size() == 1 && sounds[0].x == 1.0f && sounds[0].y == 2.0f && sounds[0].z == 3.0f,
              "pickup pop plays at the item");
        perlin::proto::RemoveEntityPacket remove;
        remove.entityIdSelf = 70;
        Inject(session, remove);
        perlin::entity::Entity item;
        Check(session.Entities().Find(7, item), "a collected item outlives its removal while it flies");
    }
    {
        perlin::BedrockSession session;
        session.BeginReplayMode("local", 7, perlin::kBedrockProtocol);
        session.SetPose(1, 64, 3, 20, -5);
        perlin::proto::MovePlayerPacket head;
        head.runtimeId = 7;
        head.mode = perlin::proto::MovePlayerPacketMode::Rotation;
        head.position = {200000, 20000, -200000};
        head.yaw = 120;
        head.pitch = 10;
        head.headYaw = 90;
        head.onGround = true;
        Inject(session, head);
        auto pose = session.GetPose();
        Check(pose.x == 1 && pose.y == 64 && pose.z == 3 && pose.yaw == 20,
              "OnlyHeadRot does not replace local position or body yaw");
        Check(pose.pitch == 10 && pose.headYaw == 90 && pose.onGround,
              "OnlyHeadRot applies local head rotation and ground state");
        perlin::BedrockSession::ServerMotion motion;
        Check(session.TakeServerMotion(motion) && motion.hasRotation && motion.yaw == 20 &&
                  motion.pitch == 10 && !motion.poseChanged && !motion.teleport && !motion.hasVelocity,
              "OnlyHeadRot updates local pitch without changing body yaw, position, or velocity");
        Check(!session.TakeServerMotion(motion), "OnlyHeadRot queues one rotation update");

        head.pitch = std::numeric_limits<float>::quiet_NaN();
        Inject(session, head);
        Check(session.GetPose().pitch == 10 && !session.TakeServerMotion(motion),
              "invalid local rotation leaves pose and motion queue unchanged");

        head.pitch = 15;
        head.mode = perlin::proto::MovePlayerPacketMode::Teleport;
        Inject(session, head);
        Check(session.GetPose().x == 1 && !session.TakeServerMotion(motion),
              "teleport mode without teleport data leaves the player unchanged");
        head.teleport = perlin::proto::MovePlayerPacketTeleport{};
        head.tick = 42;
        Inject(session, head);
        pose = session.GetPose();
        Check(pose.x == 200000 && pose.z == -200000 && pose.yaw == 120 && pose.pitch == 15,
              "teleport applies valid distant position and full rotation");
        Check(session.TakeServerMotion(motion) && motion.poseChanged && motion.teleport &&
                  motion.filterTravel && motion.hasVelocity && motion.tick == 42,
              "teleport still queues its existing simulation reset");
        head.position.x = movement::kPositionLimit;
        Inject(session, head);
        Check(session.GetPose().x == 200000 && !session.TakeServerMotion(motion),
              "out-of-range teleport cannot change pose or queue motion");
        session.SetPose(-200000, 20000, 200000, 30, 5);
        Check(session.GetPose().x == -200000 && session.GetPose().y == 20000,
              "local pose synchronization accepts valid distant coordinates");
        session.SetPose(movement::kPositionLimit, 64, 0, 30, 5);
        Check(session.GetPose().x == -200000, "out-of-range local pose is rejected");
    }
    {
        perlin::BedrockSession session;
        session.BeginReplayMode("local", 7, perlin::kBedrockProtocol);
        perlin::proto::MovePlayerPacket head;
        head.runtimeId = 7;
        head.mode = perlin::proto::MovePlayerPacketMode::Rotation;
        Inject(session, head);
        Check(!session.GetPose().known, "head rotation cannot manufacture an initial spawn position");
        session.Entities().AddPlayer(8, 80, "remote", "Remote", {5, 64, -7}, 0, 25, 25, 0, "");
        head.runtimeId = 8;
        head.position = {999, 500, 999};
        head.pitch = 15;
        head.yaw = 150;
        head.headYaw = 65;
        Inject(session, head);
        perlin::entity::Entity remote;
        Check(session.Entities().Find(8, remote) && remote.pos.x == 5 && remote.pos.y == 64 &&
                  remote.pos.z == -7 && remote.yaw == 25,
              "OnlyHeadRot does not move a remote player or replace its body yaw");
        Check(remote.pitch == 15 && remote.headYaw == 65,
              "OnlyHeadRot updates remote pitch and head yaw");
    }
    {
        perlin::BedrockSession session;
        session.BeginReplayMode("local", 7, perlin::kBedrockProtocol);
        session.SetPose(1, 64, 3, 0, 0);
        perlin::proto::CorrectPlayerMovePredictionPacket correction;
        correction.predictionType = perlin::proto::CorrectPlayerMovePredictionPacketPredictionType::Player;
        correction.position = {4, 65.62001f, 6};
        correction.delta = {0.1f, std::numeric_limits<float>::quiet_NaN(), 0};
        correction.tick = 12;
        Inject(session, correction);
        perlin::BedrockSession::ServerMotion motion;
        Check(session.GetPose().x == 1 && session.CorrectionCount() == 0 &&
                  !session.TakeServerMotion(motion),
              "non-finite correction velocity cannot poison local prediction state");
        correction.delta.y = -0.0784f;
        Inject(session, correction);
        Check(session.GetPose().x == 4 && session.CorrectionCount() == 1 &&
                  session.TakeServerMotion(motion) && motion.correction && motion.tick == 12 &&
                  motion.velocity[1] == -0.0784f,
              "finite player correction still updates pose and queues velocity replay");

        correction.predictionType = perlin::proto::CorrectPlayerMovePredictionPacketPredictionType::Vehicle;
        correction.rotation = {0, 90};
        correction.angularVelocity = std::numeric_limits<float>::infinity();
        Inject(session, correction);
        perlin::BedrockSession::VehicleCorrection vehicle;
        Check(!session.TakeVehicleCorrection(vehicle), "non-finite vehicle angular velocity is rejected");
        correction.angularVelocity = 0.25f;
        Inject(session, correction);
        Check(session.TakeVehicleCorrection(vehicle) && vehicle.tick == 12 &&
                  vehicle.rotation[1] == 90 && vehicle.angularVelocity == 0.25f,
              "finite vehicle correction still reaches vehicle prediction");

        perlin::proto::SetEntityMotionPacket knockback;
        knockback.runtimeEntityId = 7;
        knockback.velocity = {std::numeric_limits<float>::infinity(), 0.4f, 0};
        knockback.tick = 13;
        Inject(session, knockback);
        Check(!session.TakeServerMotion(motion), "non-finite local entity motion is rejected");
        knockback.velocity.x = 0.2f;
        Inject(session, knockback);
        Check(session.TakeServerMotion(motion) && motion.stampedVelocity && motion.tick == 13 &&
                  motion.velocity[0] == 0.2f && motion.velocity[1] == 0.4f,
              "finite knockback retains its tick and velocity");
    }
    {
        using C = proto::ContainerSlotType;
        InventoryRig rig;
        rig.Store(119, C::Offhand, 0, 3, 91);
        rig.Respond(C::Armor, 4, 0, 0);
        rig.Respond(C::Hotbar, BedrockSession::kInvOffhand, 0, 0);
        Check(rig.session.GetInventory().offhand.count == 3,
              "out-of-range armor and hotbar responses cannot clear the offhand slot");
    }
    for (const auto type : {proto::WindowType::Furnace, proto::WindowType::BlastFurnace,
                            proto::WindowType::Smoker}) {
        using C = proto::ContainerSlotType;
        InventoryRig rig;
        rig.Open(type);
        for (int slot = 0; slot < 3; ++slot)
            rig.Store(1, C::Container, slot, 4, 10 + slot);
        const C ingredient = type == proto::WindowType::BlastFurnace ? C::BlastFurnaceIngredient
                             : type == proto::WindowType::Smoker    ? C::SmokerIngredient
                                                                   : C::FurnaceIngredient;
        rig.Respond(ingredient, 0, 1, 100);
        rig.Respond(C::FurnaceFuel, 1, 2, 101);
        rig.Respond(C::FurnaceOutput, 2, 3, 102);
        const auto view = rig.session.GetInventory();
        Check(view.chest[0].count == 1 && view.chest[1].count == 2 && view.chest[2].count == 3,
              "furnace ingredient, fuel and output responses update their own slots");
        rig.session.InvClick(BedrockSession::kInvChest0, false);
        Check(rig.requests.size() == 1 && rig.requests[0].actions.size() == 1 &&
                  rig.requests[0].actions[0].source.slotType.containerId == ingredient &&
                  rig.requests[0].actions[0].source.stackId == 100,
              "the next furnace action uses the server-confirmed stack ID");
    }
    {
        using C = proto::ContainerSlotType;
        InventoryRig rig;
        rig.Open(proto::WindowType::Horse);
        rig.Store(1, C::HorseEquip, 0, 1, 31);
        rig.Store(1, C::Container, 0, 4, 32);
        rig.Respond(C::HorseEquip, 0, 1, 71);
        rig.Respond(C::Container, 0, 3, 72);
        const auto view = rig.session.GetInventory();
        Check(view.horse[0].count == 1 && view.horse[2].count == 3,
              "horse equipment and horse chest responses reach separate local slots");
        rig.session.InvClick(BedrockSession::kInvHorse0, false);
        Check(rig.requests.size() == 1 && rig.requests[0].actions.size() == 1 &&
                  rig.requests[0].actions[0].source.stackId == 71,
              "the next horse action uses the server-confirmed stack ID");
    }
    {
        using C = proto::ContainerSlotType;
        InventoryRig rig;
        rig.Open(proto::WindowType::Horse);
        rig.Store(1, C::HorseEquip, 0, 1, 51);
        rig.session.InvClick(BedrockSession::kInvHorse0, false);
        Check(rig.requests.size() == 1 && rig.session.GetInventory().horse[0].Empty(),
              "taking horse equipment creates an optimistic request");
        if (!rig.requests.empty()) {
            proto::ItemStackResponsePacket reject;
            proto::ItemStackResponsesEntry response;
            response.status = proto::ItemStackResponsesEntryStatus::Error;
            response.requestId = rig.requests[0].requestId;
            reject.responses.push_back(response);
            Inject(rig.session, reject);
            Check(rig.session.GetInventory().horse[0].count == 1 && rig.session.GetInventory().cursor.Empty(),
                  "a rejected horse request restores the visible inventory");
            rig.session.InvClick(BedrockSession::kInvHorse0, false);
            Check(rig.requests.size() == 2 && rig.requests[1].actions.size() == 1 &&
                      rig.requests[1].actions[0].source.stackId == 51,
                  "a rejected horse request restores the raw item and stack ID for the next request");
        }
    }
    {
        using C = proto::ContainerSlotType;
        InventoryRig rig;
        rig.Store(0, C::HotbarAndInventory, 0, 1, 81);
        rig.session.previewOffhand = true;
        rig.session.SwapOffhand();
        Check(rig.session.GetInventory().offhand.count == 1 && rig.requests.size() == 1 &&
                  rig.requests[0].actions.size() == 1 &&
                  rig.requests[0].actions[0].typeId == proto::ItemStackRequestActionsEntryTypeId::Swap &&
                  rig.requests[0].actions[0].destination.slotType.containerId == C::Offhand,
              "offhand swap uses an inventory transaction even when preview offhand is enabled");
    }
    {
        BedrockSession session;
        session.BeginReplayMode("cooldown", 7, kBedrockProtocol);
        std::vector<proto::TransactionUseItem> attempts;
        int cooldownEchoes = 0;
        session.SetSentTap([&](uint32_t id, const std::vector<uint8_t>& bytes) {
            if (id == proto::ClientStartItemCooldownPacket::kId)
                ++cooldownEchoes;
            if (id != proto::InventoryTransactionPacket::kId)
                return;
            proto::InventoryTransactionPacket packet;
            net::ReadStream stream(bytes.data(), bytes.size());
            Check(proto::Read(stream, packet) && stream.remaining() == 0, "cooldown transaction encodes fully");
            if (const auto* use = std::get_if<proto::TransactionUseItem>(&packet.transaction.transactionData))
                attempts.push_back(*use);
        });
        proto::ItemRegistryPacket registry;
        proto::ItemstatesEntry horn;
        horn.name = "minecraft:goat_horn";
        horn.runtimeId = 21;
        horn.nbt = CooldownItem("unused", 0, "test:incorrect_override").nbt;
        registry.itemstates.push_back(horn);
        registry.itemstates.push_back(CooldownItem("test:first", 22, "test:shared"));
        registry.itemstates.push_back(CooldownItem("test:second", 23, "test:shared"));
        registry.itemstates.push_back(CooldownItem("test:attack", 24, "test:shared", "attack"));
        registry.itemstates.push_back(CooldownItem("minecraft:apple", 25, "food"));
        Inject(session, registry);

        Inject(session, proto::ClientStartItemCooldownPacket{"goat_horn", 3});
        Check(session.ItemCooldownTicks("minecraft:goat_horn") == 3 &&
                  session.ItemUseOnCooldown("minecraft:goat_horn"),
              "a native item getter's cooldown category takes precedence over component metadata");
        Inject(session, proto::ClientStartItemCooldownPacket{"minecraft:goat_horn", 1});
        Check(session.ItemCooldownTicks("minecraft:goat_horn") == 1,
              "qualified and unqualified categories replace the same cooldown");
        session.TickItemCooldowns();
        Check(!session.ItemUseOnCooldown("minecraft:goat_horn"), "cooldown expires after its simulation tick");
        Inject(session, proto::ClientStartItemCooldownPacket{"food", 2});
        Check(session.ItemUseOnCooldown("minecraft:apple"),
              "component cooldown metadata is read for vanilla item registry entries too");
        Inject(session, proto::ClientStartItemCooldownPacket{"test:shared", 2});
        Check(session.ItemUseOnCooldown("test:first") && session.ItemUseOnCooldown("test:second") &&
                  !session.ItemUseOnCooldown("test:attack") && session.ItemCooldownTicks("test:attack") == 2,
              "shared categories affect all members while use and attack actions stay distinct");
        session.InjectIncoming(proto::ClientStartItemCooldownPacket::kId, {0x04, 't'});
        Check(session.ItemCooldownTicks("test:first") == 2, "a truncated cooldown cannot reset valid state");
        Check(cooldownEchoes == 0, "received cooldowns are not echoed back to the server");

        proto::InventorySlotPacket held;
        held.windowId = proto::WindowIDVarint::Inventory;
        held.slot = 0;
        held.item.networkId = 22;
        held.item.count = 2;
        held.item.hasStackId = true;
        held.item.stackId = 9;
        Inject(session, held);
        Check(session.HeldItemUseOnCooldown(), "the selected main-hand item resolves its cooldown by network ID");
        proto::InventorySlotPacket offhand = held;
        offhand.windowId = proto::WindowIDVarint::Offhand;
        offhand.item.networkId = 21;
        Inject(session, offhand);
        session.SetUseOffhand(true);
        Check(!session.HeldItemUseOnCooldown(), "the off-hand query does not reuse the main-hand cooldown");
        session.SetUseOffhand(false);
        const float eye[3] = {0, 2, 0};
        Check(session.SendUseItemInAir(eye, true), "an air-use attempt is sent while the item is on cooldown");
        Check(attempts.size() == 1 &&
                  attempts.back().actionType == proto::TransactionUseItemActionType::ClickAir &&
                  attempts.back().clientCooldownState == proto::TransactionUseItemClientCooldownState::Off &&
                  attempts.back().clientPrediction == proto::TransactionUseItemClientPrediction::Failure,
              "ordinary air attempts preserve the matched client's cooldown and prediction wire states");
        Check(session.GetInventory().hotbar[0].count == 2, "a blocked air use does not consume inventory");
        held.item.blockRuntimeId = 1;
        Inject(session, held);
        session.NetLevel().EnsureChunk(0, 0);
        const int destination[3] = {0, 0, 0};
        const float click[3] = {0.5f, 0.5f, 0.5f};
        Check(!session.SendUseItemOnBlock(0, 0, 0, 1, click, eye, 2, destination),
              "a cooling-down held block does not produce a successful placement prediction");
        Check(attempts.size() == 2 &&
                  attempts.back().actionType == proto::TransactionUseItemActionType::ClickBlock &&
                  attempts.back().clientCooldownState == proto::TransactionUseItemClientCooldownState::Off &&
                  attempts.back().clientPrediction == proto::TransactionUseItemClientPrediction::Failure &&
                  session.GetInventory().hotbar[0].count == 2,
              "blocked placement still sends the failure transaction without consuming its item");
        Check(session.SendUseItemOnBlock(0, 0, 0, 1, click, eye, 1, destination) &&
                  attempts.size() == 3 &&
                  attempts.back().clientPrediction == proto::TransactionUseItemClientPrediction::Success,
              "a held-item cooldown does not block the block's own interaction");
        session.TickItemCooldowns();
        Check(session.ItemCooldownTicks("test:first") == 1, "one tick leaves one cooldown tick");
        session.TickItemCooldowns();
        Check(!session.ItemUseOnCooldown("test:first") && !session.ItemUseOnCooldown("test:second"),
              "all members of a category become usable when the cooldown expires");
        Check(session.SendUseItemOnBlock(0, 0, 0, 1, click, eye, 2, destination) &&
                  attempts.size() == 4 &&
                  attempts.back().clientPrediction == proto::TransactionUseItemClientPrediction::Success &&
                  session.GetInventory().hotbar[0].count == 1,
              "placement resumes and consumes one item after the server cooldown expires");
        Inject(session, proto::ClientStartItemCooldownPacket{"test:shared", 5});
        proto::ItemRegistryPacket replacement;
        replacement.itemstates.push_back(CooldownItem("test:first", 22, "test:changed"));
        Inject(session, replacement);
        Check(!session.ItemUseOnCooldown("test:first") && !session.ItemUseOnCooldown("test:second"),
              "registry replacement discards old item-to-category definitions");
        Inject(session, registry);
        Check(session.ItemCooldownTicks("test:first") == 5,
              "registry replacement preserves the player's active category durations");
        Inject(session, proto::ItemRegistryPacket{});
        Check(session.ItemCooldownTicks("test:first") == 5,
              "an ignored empty registry does not discard cooldown definitions");
        session.SetSentTap({});
    }
    if (failures == 0)
        std::puts("handler_dispatch_test: ok");
    return failures == 0 ? 0 : 1;
}
}
