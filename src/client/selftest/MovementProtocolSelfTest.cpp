#include "client/selftest/Check.hpp"
#include "client/selftest/MovementProtocolFixtures.hpp"
#include "network/Stream.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace {
    using namespace perlin;

    template <class P> void Inject(BedrockSession& session, const P& packet) {
        session.InjectIncoming(P::kId, net::Encode(packet));
    }

    bool Has(const proto::PlayerAuthInputPacket& packet, proto::InputData flag) {
        return std::ranges::find(packet.inputData, flag) != packet.inputData.end();
    }

    struct Rig {
        BedrockSession session;
        movement::MoveInput move;
        std::vector<proto::PlayerAuthInputPacket> attempts;
        std::vector<uint8_t> predictionSyncBody;
        int predictionSyncs = 0;
        bool decodeOk = true;
        std::function<void()> afterAttempt;

        explicit Rig(int protocol = kBedrockProtocol) {
            session.BeginReplayMode("movement", 7, protocol);
            MovementProtocolTestAccess::Initialize(session);
            session.SetPose(0, 64, 0, 0, 0);
            move.position[1] = 64;
            session.SetSentTap([this](uint32_t id, const std::vector<uint8_t>& bytes) {
                if (id == proto::ClientMovementPredictionSyncPacket::kId) {
                    ++predictionSyncs;
                    predictionSyncBody = bytes;
                }
                if (id != proto::PlayerAuthInputPacket::kId)
                    return;
                proto::PlayerAuthInputPacket packet;
                net::ReadStream stream(bytes.data(), bytes.size());
                decodeOk &= proto::Read(stream, packet) && stream.remaining() == 0;
                attempts.push_back(std::move(packet));
                if (auto callback = std::exchange(afterAttempt, {}))
                    callback();
            });
        }

        ~Rig() { session.SetSentTap({}); }

        void Teleport(float x) {
            proto::MovePlayerPacket packet;
            packet.runtimeId = 7;
            packet.position = {x, 65.62001f, 0};
            packet.mode = proto::MovePlayerPacketMode::Teleport;
            packet.teleport.emplace();
            Inject(session, packet);
        }

        void BlockAction(proto::Action action, int x) {
            session.SendPlayerAuthInputBlockAction(static_cast<uint32_t>(action), x, 64, 0, 1);
        }

        void MovementAttributes() {
            proto::UpdateAttributesPacket attributes;
            attributes.runtimeEntityId = 7;
            for (const char* name : {"minecraft:movement", "minecraft:underwater_movement",
                                     "minecraft:lava_movement", "minecraft:horse.jump_strength",
                                     "minecraft:health", "minecraft:player.hunger"}) {
                proto::PlayerAttributesEntry entry;
                entry.name = name;
                entry.max = entry.defaultMax = 100;
                entry.current = entry.default_ = 1;
                attributes.attributes.push_back(std::move(entry));
            }
            Inject(session, attributes);
        }
    };
}

namespace perlin::selftest {
    int MovementProtocol() {
        Checker t("movement_protocol_test");
        for (int protocol : {944, 2193, kBedrockProtocol}) {
            Rig rig(protocol);
            rig.Teleport(4);
            rig.session.QueueInputFlag(InputFlag::StartUsingItem);
            rig.BlockAction(proto::Action::StartBreak, 1);
            rig.session.EndBlockActionTick();
            // AbortBreak carries coordinates in all three formats; the older
            // StopBreak action has no position payload.
            rig.BlockAction(proto::Action::AbortBreak, 2);

            auto malformed = rig.move;
            if (protocol == 944) {
                // This older codec requires a transaction when its flag is set.
                // Protocols 2193/2225 encode the transaction as an independent
                // presence optional, so this is not their encoding-failure case.
                malformed.flags.push_back(static_cast<uint32_t>(proto::InputData::ItemInteract));
                t.Check(!rig.session.SendMoveInput(malformed) && rig.session.LastAuthTick() == 0 &&
                            rig.attempts.empty(),
                        "944 absent flagged transaction fails before transport or frame-tick mutation");
            }
            malformed = rig.move;
            malformed.rawX = std::numeric_limits<float>::quiet_NaN();
            t.Check(!rig.session.SendMoveInput(malformed) && rig.session.LastAuthTick() == 0 &&
                        rig.attempts.empty(),
                    "invalid raw input leaves the movement frame and pending actions intact");

            MovementProtocolTestAccess::ReplayTransport(rig.session, false);
            t.Check(!rig.session.SendMoveInput(rig.move) && rig.session.LastAuthTick() == 1,
                    "an encoded frame advances simulation time even if transport fails");
            MovementProtocolTestAccess::ReplayTransport(rig.session, true);
            t.Check(rig.session.SendMoveInput(rig.move) && rig.attempts.size() == 2 && rig.decodeOk,
                    "the next movement frame can retry pending protocol state");
            if (rig.attempts.size() >= 2) {
                const auto& retry = rig.attempts.back();
                t.Check(retry.tick == 2 && Has(retry, proto::InputData::HandledTeleport) &&
                            Has(retry, proto::InputData::StartUsingItem),
                        "failed transmission does not lose the teleport acknowledgment or queued input flag");
                t.Check(retry.blockAction && retry.blockAction->size() == 1 &&
                            retry.blockAction->front().action == proto::Action::StartBreak &&
                            retry.blockAction->front().position.x == 1,
                        "the first pending action group survives failed encoding and transmission");
            }
            t.Check(rig.session.SendMoveInput(rig.move), "the following action group can be sent");
            if (!rig.attempts.empty()) {
                const auto& next = rig.attempts.back();
                t.Check(next.blockAction && next.blockAction->size() == 1 &&
                            next.blockAction->front().action == proto::Action::AbortBreak &&
                            next.blockAction->front().position.x == 2 &&
                            !Has(next, proto::InputData::HandledTeleport) &&
                            !Has(next, proto::InputData::StartUsingItem),
                        "accepted state is consumed once and block-action groups retain their order");
            }
        }
        {
            Rig rig;
            rig.Teleport(1);
            rig.session.QueueInputFlag(InputFlag::StartUsingItem);
            rig.BlockAction(proto::Action::StartBreak, 1);
            rig.afterAttempt = [&] {
                rig.Teleport(2);
                rig.session.QueueInputFlag(InputFlag::MissedSwing);
                rig.session.EndBlockActionTick();
                rig.BlockAction(proto::Action::StopBreak, 2);
            };
            t.Check(rig.session.SendMoveInput(rig.move) && rig.session.SendMoveInput(rig.move),
                    "new state may be queued while an earlier movement packet is transmitted");
            if (rig.attempts.size() == 2) {
                const auto& next = rig.attempts.back();
                t.Check(Has(next, proto::InputData::HandledTeleport) &&
                            Has(next, proto::InputData::MissedSwing) &&
                            !Has(next, proto::InputData::StartUsingItem) && next.blockAction &&
                            next.blockAction->size() == 1 && next.blockAction->front().position.x == 2,
                        "committing the old snapshot preserves newly queued flags, actions, and teleport");
            } else {
                t.Check(false, "both accepted movement packets were captured");
            }
        }
        {
            Rig rig;
            rig.session.ArmPredictionSync();
            bool sent = true;
            for (int i = 0; i < 200; ++i)
                sent &= rig.session.SendMoveInput(rig.move);
            t.Check(sent && rig.predictionSyncs == 0,
                    "a due prediction sync waits for its required player attributes");
            rig.MovementAttributes();
            t.Check(rig.session.SendMoveInput(rig.move) && rig.predictionSyncs == 1,
                    "a previously due sync retries once the required attributes arrive");
            t.Check(rig.session.SendMoveInput(rig.move) && rig.predictionSyncs == 1,
                    "an accepted prediction sync does not repeat on every movement frame");
        }
        {
            Rig rig;
            rig.Teleport(4);
            rig.session.QueueInputFlag(InputFlag::MissedSwing);
            rig.BlockAction(proto::Action::StartBreak, 1);
            MovementProtocolTestAccess::Reset(rig.session);
            t.Check(!rig.session.Initialized() && rig.session.LastAuthTick() == 0,
                    "a connection reset clears movement initialization and frame state");
            MovementProtocolTestAccess::ReplayTransport(rig.session, false);
            t.Check(!MovementProtocolTestAccess::SendInitialized(rig.session) && !rig.session.Initialized(),
                    "failed initialization transmission cannot enable movement packets");
            MovementProtocolTestAccess::ReplayTransport(rig.session, true);
            t.Check(MovementProtocolTestAccess::SendInitialized(rig.session) && rig.session.Initialized(),
                    "accepted initialization enables movement packets");
            t.Check(rig.session.SendMoveInput(rig.move) && rig.attempts.size() == 1 &&
                        !rig.attempts.back().blockAction &&
                        !Has(rig.attempts.back(), proto::InputData::HandledTeleport) &&
                        !Has(rig.attempts.back(), proto::InputData::MissedSwing),
                    "a new connection does not inherit the old movement queues");
        }
        {
            Rig rig;
            rig.afterAttempt = [&] {
                MovementProtocolTestAccess::Reset(rig.session);
                rig.session.QueueInputFlag(InputFlag::MissedSwing);
            };
            t.Check(!rig.session.SendMoveInput(rig.move) && rig.session.LastAuthTick() == 0 &&
                        !MovementProtocolTestAccess::HasSentPosition(rig.session),
                    "an in-flight send cannot restore movement state after a connection reset");
            MovementProtocolTestAccess::Initialize(rig.session);
            t.Check(rig.session.SendMoveInput(rig.move) &&
                        Has(rig.attempts.back(), proto::InputData::MissedSwing),
                    "committing an old send cannot erase the new connection's input flags");
        }
        {
            BedrockSession session;
            session.BeginReplayMode("start-position", 7, kBedrockProtocol);
            proto::StartGamePacket start;
            start.runtimeEntityId = 7;
            start.playerPosition = {150000, 20000, -150000};
            Inject(session, start);
            float position[3]{};
            session.GetStartPos(position);
            t.Check(session.GetPose().known && session.GetPose().x == 150000 &&
                        session.GetPose().y == 20000 && session.GetPose().z == -150000 &&
                        session.HaveStartPos() && position[0] == 150000 && position[1] == 20000,
                    "StartGame accepts finite distant coordinates inside Perlin's simulation envelope");
        }
        for (float invalidY : {std::numeric_limits<float>::quiet_NaN(), movement::kPositionLimit,
                               -movement::kPositionLimit}) {
            BedrockSession session;
            session.BeginReplayMode("invalid-start-position", 7, kBedrockProtocol);
            proto::StartGamePacket start;
            start.runtimeEntityId = 7;
            start.playerPosition = {1, invalidY, 3};
            Inject(session, start);
            t.Check(!session.GetPose().known && !session.HaveStartPos(),
                    "StartGame validates all axes before storing its initial or fallback position");
        }
        {
            Rig rig;
            proto::RespawnPacket respawn;
            respawn.state = 1;
            respawn.position = {150000, 20000, -150000};
            Inject(rig.session, respawn);
            BedrockSession::ServerMotion motion;
            t.Check(rig.session.GetPose().x == 150000 &&
                        rig.session.GetPose().y == 20000 - kPlayerEyeHeight &&
                        rig.session.TakeServerMotion(motion) && motion.teleport,
                    "Respawn converts distant eye coordinates into a queued feet-position teleport");
            respawn.position.x = movement::kPositionLimit;
            Inject(rig.session, respawn);
            t.Check(rig.session.GetPose().x == 150000 && !rig.session.TakeServerMotion(motion),
                    "Respawn rejects coordinates outside Perlin's simulation envelope");
        }
        {
            Rig rig;
            proto::ChangeDimensionPacket dimension;
            dimension.dimension = 1;
            dimension.position = {-150000, 20000, 150000};
            Inject(rig.session, dimension);
            BedrockSession::ServerMotion motion;
            t.Check(rig.session.GetPose().x == -150000 && rig.session.GetPose().y == 20000 &&
                        rig.session.DimensionId() == 1 && rig.session.TakeServerMotion(motion) &&
                        motion.teleport,
                    "ChangeDimension preserves its position convention at distant coordinates");
            dimension.position.z = -movement::kPositionLimit;
            Inject(rig.session, dimension);
            t.Check(rig.session.GetPose().z == 150000 && !rig.session.TakeServerMotion(motion),
                    "ChangeDimension cannot inject a position beyond Perlin's simulation envelope");
        }
        {
            Rig rig;
            proto::CorrectPlayerMovePredictionPacket correction;
            correction.predictionType = proto::CorrectPlayerMovePredictionPacketPredictionType::Player;
            correction.position = {150000, 20000, -150000};
            correction.delta = {0.125f, -0.25f, 0.5f};
            BedrockSession::ServerMotion motion;
            Inject(rig.session, correction);
            t.Check(rig.session.GetPose().x == 0 && rig.session.CorrectionCount() == 0 &&
                        !rig.session.TakeServerMotion(motion),
                    "tick zero is ineligible for player prediction correction");
            correction.tick = 1;
            Inject(rig.session, correction);
            t.Check(rig.session.GetPose().x == 150000 && rig.session.CorrectionCount() == 1 &&
                        rig.session.TakeServerMotion(motion) && motion.correction &&
                        motion.pos[1] == 20000 - kPlayerEyeHeight && motion.velocity[2] == 0.5f,
                    "nonzero distant player corrections retain the existing replay/fallback path");
            correction.position.x = movement::kPositionLimit;
            Inject(rig.session, correction);
            t.Check(rig.session.CorrectionCount() == 1 && !rig.session.TakeServerMotion(motion),
                    "player corrections respect Perlin's finite simulation envelope");
            correction.predictionType = proto::CorrectPlayerMovePredictionPacketPredictionType::Vehicle;
            correction.position.x = -150000;
            correction.tick = 0;
            Inject(rig.session, correction);
            BedrockSession::VehicleCorrection vehicle;
            t.Check(!rig.session.TakeVehicleCorrection(vehicle),
                    "tick zero is also ineligible for vehicle prediction correction");
            correction.tick = 2;
            Inject(rig.session, correction);
            t.Check(rig.session.TakeVehicleCorrection(vehicle) && vehicle.pos[0] == -150000 &&
                        vehicle.pos[1] == 20000,
                    "vehicle correction keeps its own position convention at distant coordinates");
        }
        {
            Rig rig(2193);
            proto::StartGamePacket start;
            start.runtimeEntityId = 7;
            start.entityId = 70;
            start.playerPosition = {0, 64, 0};
            Inject(rig.session, start);
            const std::vector<uint8_t> sleeping{7, 1, 92, 7, 0, 0x80, 0x40, 0, 0, 1};
            rig.session.InjectIncoming(proto::SetEntityDataPacket::kId, sleeping);
            t.Check(rig.session.Sleeping() &&
                        rig.session.OwnActorFlags()[1] == (uint64_t{1} << 12),
                    "2193 literal sleeping metadata reaches the own-player movement state without shifting");
            const std::vector<uint8_t> laying{7, 1, 92, 7, 0, 2, 0, 0, 1};
            rig.session.InjectIncoming(proto::SetEntityDataPacket::kId, laying);
            t.Check(!rig.session.Sleeping() && rig.session.OwnActorFlags()[1] == 1,
                    "2193 laying-down metadata retains bit zero and does not imply sleeping");
            const std::vector<uint8_t> sneezing{7, 1, 92, 7, 0, 4, 0, 0, 1};
            rig.session.InjectIncoming(proto::SetEntityDataPacket::kId, sneezing);
            t.Check(rig.session.OwnActorFlags()[1] == 2,
                    "2193 sneezing metadata retains bit one");
            const std::vector<uint8_t> escape{7, 1, 92, 7, 0, 0x80, 0x80, 0x80, 0x80,
                                               0x80, 0x80, 0x10, 0, 0, 1};
            rig.session.InjectIncoming(proto::SetEntityDataPacket::kId, escape);
            t.Check(rig.session.PushTowardsClosestSpace(),
                    "2193 closest-space escape metadata reaches the movement consumer");
            rig.session.Entities().AddEntity(8, 80, "minecraft:pig", {0, 64, 0}, 0, 0, 0, 0, 1, 0, 0, 0);
            auto remoteFlags = sleeping;
            remoteFlags[0] = 8;
            rig.session.InjectIncoming(proto::SetEntityDataPacket::kId, remoteFlags);
            entity::Entity remote;
            t.Check(rig.session.Entities().Find(8, remote) &&
                        remote.flagsExtended == (uint64_t{1} << 12),
                    "2193 extended metadata reaches remote actors without shifting");
            rig.MovementAttributes();
            t.Check(rig.session.SendPredictionSync() && rig.predictionSyncs == 1,
                    "the live session emits prediction sync for a 2193 connection");
            const auto& bytes = rig.predictionSyncBody;
            t.Check(bytes.size() >= 3 && bytes[bytes.size() - 3] == 0x8c &&
                        bytes[bytes.size() - 2] == 1 && bytes.back() == 0,
                    "prediction sync sends unique ID70 as literal ZigZag bytes, not runtime ID7 or zero");
            proto::ClientMovementPredictionSyncPacket sync;
            net::ReadStream stream(bytes.data(), bytes.size());
            t.Check(proto::Read(stream, sync) && stream.remaining() == 0 &&
                        sync.entityUniqueId == 70 && sync.dataFlags[1] == (uint64_t{1} << 45),
                    "2193 prediction sync preserves the selected actor identity and corrected flag word");
            net::context::SetProtocol(kBedrockProtocol);
        }
        return t.Finish();
    }
}
