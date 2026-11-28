#include "client/selftest/Check.hpp"
#include "client/selftest/MovementProtocolFixtures.hpp"
#include "client/systems/player/BlockCollision.hpp"
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
            for (const auto& update : session.TakeAttributeUpdates())
                session.ApplyAttributeUpdate(update);
        }
    };
}

namespace perlin::selftest {
    int MovementProtocol() {
        Checker t("movement_protocol_test");
        for (int protocol : {944, 2193, kBedrockProtocol}) {
            Rig rig(protocol);
            rig.session.NetLevel().EnsureChunk(0, 0);
            MovementProtocolTestAccess::SettleColumn(rig.session, 0, 0);
            LevelCollision world(rig.session.NetLevel(), nullptr);
            Input input;
            Options options;
            InputManager controls(input, options);
            input.OnKeyDown(options.GetKey(Action::Forward).code);
            input.OnMouseDown(Mouse::Right);
            controls.Poll();
            LocalPlayer player;
            Camera camera;
            camera.yaw = 90;
            physics::State state;
            state.x = state.z = 8;
            state.y = 64;
            MovementProtocolTestAccess::Place(player, state);
            const std::array<float, 5> modifiers{0.5f, 0.2f, 1.0f, 0.0f, -0.5f};
            for (const float modifier : modifiers) {
                player.SetItemSlow(true, 0, false, modifier);
                const size_t before = rig.attempts.size();
                MovementProtocolTestAccess::Tick(player, rig.session, controls, camera, world);
                t.Check(rig.decodeOk && rig.attempts.size() == before + 1,
                        "custom item-use movement produces a fully decoded auth-input packet");
                t.Check(rig.attempts.size() > before &&
                            rig.attempts.back().moveVector.z == modifier * modifier &&
                            rig.attempts.back().moveVector.x == 0,
                        "item-use movement squares the configured modifier once without clamping it");
            }
            bool snapshot = player.PredictionHistorySize() == modifiers.size();
            if (snapshot)
                for (size_t i = 0; i < modifiers.size(); ++i)
                    snapshot &= MovementProtocolTestAccess::FrameInput(player, i).forward ==
                                modifiers[i] * modifiers[i];
            t.Check(snapshot, "prediction records retain each tick's already attenuated movement input");
            const auto predicted = player.State();
            player.SetItemSlow(true, 0, false, 0.8f);
            MovementProtocolTestAccess::Replay(player, rig.session, 0);
            t.Check(player.State().x == predicted.x && player.State().y == predicted.y &&
                        player.State().z == predicted.z && player.State().vx == predicted.vx &&
                        player.State().vy == predicted.vy && player.State().vz == predicted.vz,
                    "replaying earlier movement uses its saved modifiers after the active item changes");
        }
        for (int protocol : {944, 2193, kBedrockProtocol}) {
            struct Air final : physics::World {
                physics::BlockShape Query(int, int, int) const override { return {}; }
            } world;
            Rig rig(protocol);
            MovementProtocolTestAccess::SettleColumn(rig.session, 0, 0);
            Input input;
            Options options;
            InputManager controls(input, options);
            LocalPlayer player;
            Camera actorView;
            actorView.yaw = 90;
            actorView.pitch = 23;
            const auto step = [&] {
                physics::State state;
                state.x = state.z = 0.5;
                state.y = 64;
                MovementProtocolTestAccess::Place(player, state);
                const size_t before = rig.attempts.size();
                MovementProtocolTestAccess::Tick(player, rig.session, controls, actorView, world);
                t.Check(rig.decodeOk && rig.attempts.size() == before + 1,
                        "camera control simulation emits one fully decoded movement packet");
            };
            const auto orientation = [&] {
                if (rig.attempts.empty())
                    return false;
                const auto& packet = rig.attempts.back();
                return packet.yaw == 0 && packet.pitch == -23 && player.State().yaw == 0;
            };
            player.SetCameraControl(0, 0);
            player.SetCameraView(std::array<float, 3>{0.8f, 0.6f, 0});
            step();
            t.Check(orientation() && player.State().vx == 0 && player.State().vz == 0,
                    "locked player-relative control does not turn an idle actor toward a detached camera");
            t.Check(!rig.attempts.empty() && rig.attempts.back().cameraOrientation.x == 0.8f &&
                        rig.attempts.back().cameraOrientation.y == 0.6f &&
                        rig.attempts.back().cameraOrientation.z == 0 &&
                        rig.attempts.back().interactRotation.x == -23 &&
                        rig.attempts.back().interactRotation.z == 0,
                    "auth input carries independent camera direction without replacing actor or interaction rotation");
            input.OnKeyDown(options.GetKey(Action::Forward).code);
            step();
            t.Check(orientation() && player.State().vz > 0 && std::fabs(player.State().vx) < 0.00001,
                    "locked player-relative forward motion follows actor yaw rather than detached camera yaw");
            input.OnKeyUp(options.GetKey(Action::Forward).code);
            input.OnKeyDown(options.GetKey(Action::Right).code);
            player.SetCameraControl(0, -90);
            step();
            t.Check(orientation() && player.State().vx < 0 && std::fabs(player.State().vz) < 0.00001,
                    "locked player-relative strafe preserves actor orientation while the detached camera turns");
            player.SetCameraControl(-1, 180);
            step();
            t.Check(orientation() && player.State().vx < 0,
                    "clearing camera controls resumes the same player-relative movement and look");
            player.SetCameraView(std::nullopt);
            step();
            t.Check(orientation() && !rig.attempts.empty() &&
                        std::fabs(rig.attempts.back().cameraOrientation.x) < 0.00001f &&
                        std::fabs(rig.attempts.back().cameraOrientation.y - std::sin(glm::radians(23.0f))) <
                            0.00001f &&
                        std::fabs(rig.attempts.back().cameraOrientation.z - std::cos(glm::radians(23.0f))) <
                            0.00001f,
                    "clearing a camera view restores the existing actor-derived direction fallback");
        }
        for (int protocol : {944, 2193, kBedrockProtocol}) {
            struct Air final : physics::World {
                physics::BlockShape Query(int, int, int) const override { return {}; }
            } world;
            Rig rig(protocol);
            MovementProtocolTestAccess::SettleColumn(rig.session, 0, 0);
            const auto near = [](float a, float b) { return std::fabs(a - b) < 0.00002f; };
            struct Sample {
                proto::PlayerAuthInputPacket packet;
                physics::State state;
            };
            const auto simulate = [&](int scheme, float forward, float strafe, bool itemSlow = false,
                                      bool sneak = false) {
                Input input;
                Options options;
                InputManager controls(input, options);
                input.NoteDevice(InputDevice::Gamepad);
                input.SetPadMove(strafe, -forward);
                if (itemSlow)
                    input.OnMouseDown(Mouse::Right);
                if (sneak)
                    input.OnKeyDown(options.GetKey(Action::Sneak).code);
                controls.Poll();
                LocalPlayer player;
                player.SetCameraControl(scheme, 0);
                player.SetItemSlow(itemSlow);
                Camera actorView;
                actorView.yaw = 90;
                actorView.pitch = 23;
                physics::State state;
                state.x = state.z = 0.5;
                state.y = 64;
                MovementProtocolTestAccess::Place(player, state);
                const size_t before = rig.attempts.size();
                MovementProtocolTestAccess::Tick(player, rig.session, controls, actorView, world);
                t.Check(rig.decodeOk && rig.attempts.size() == before + 1,
                        "native camera movement transform emits one decodable auth-input packet");
                return Sample{rig.attempts.size() > before ? rig.attempts.back()
                                                          : proto::PlayerAuthInputPacket{},
                              player.State()};
            };
            const auto relative = simulate(1, 0.25f, 0);
            t.Check(near(relative.packet.yaw, -90) && relative.packet.pitch == 0 &&
                        near(relative.packet.moveVector.z, 0.25f) && relative.packet.moveVector.x == 0 &&
                        relative.state.vx > 0 && std::fabs(relative.state.vz) < 0.00001,
                    "camera-relative movement faces its travel direction without losing analog magnitude");
            const auto independent = simulate(2, 1, 0);
            t.Check(independent.packet.yaw == 0 && independent.packet.pitch == 0 &&
                        near(independent.packet.moveVector.x, 1) &&
                        std::fabs(independent.packet.moveVector.z) < 0.00001 &&
                        independent.state.vx > 0 && std::fabs(independent.state.vz) < 0.00001,
                    "camera-relative strafe rotates movement into actor space and preserves actor yaw");
            const auto diagonal = simulate(2, 0.6f, 0.8f);
            t.Check(diagonal.packet.yaw == 0 && near(diagonal.packet.moveVector.x, 0.6f) &&
                        near(diagonal.packet.moveVector.z, 0.8f),
                    "camera-relative strafe preserves both components and length of diagonal input");
            const auto right = simulate(3, 0, 1);
            t.Check(near(right.packet.yaw, 10) && right.packet.pitch == 0 &&
                        right.packet.moveVector.x == 0 && right.packet.moveVector.z == 0 &&
                        right.state.vx == 0 && right.state.vz == 0 &&
                        Has(right.packet, proto::InputData::Right),
                    "player-relative right input turns ten degrees without lateral travel or erasing raw intent");
            const auto left = simulate(3, 0, -1);
            t.Check(near(left.packet.yaw, -10) && left.packet.moveVector.x == 0 &&
                        left.packet.moveVector.z == 0,
                    "player-relative left input turns in the opposite direction without strafing");
            const auto forward = simulate(3, 1, 0);
            t.Check(forward.packet.yaw == 0 && forward.packet.pitch == 0 &&
                        near(forward.packet.moveVector.z, 1) && forward.state.vz > 0,
                    "player-relative full forward input follows actor orientation and clears pitch");
            const auto back = simulate(3, -1, 0);
            t.Check(back.packet.yaw == 0 && near(back.packet.moveVector.z, -1) && back.state.vz < 0,
                    "player-relative full backward input keeps actor yaw");
            const auto turn = simulate(3, 0.75f, 0.25f);
            t.Check(near(turn.packet.yaw, 5.78125f) && near(turn.packet.moveVector.z, 0.75f) &&
                        near(turn.packet.moveVector.x, -0.25f),
                    "player-relative partial turn uses the recovered cubic curve and retains shallow travel");
            const auto slowed = simulate(3, 0.75f, 0.25f, true);
            t.Check(near(slowed.packet.yaw, 5.78125f) &&
                        near(slowed.packet.moveVector.z, 0.091875f) &&
                        near(slowed.packet.moveVector.x, -0.030625f),
                    "item-use slowdown follows camera turning rather than changing the turn threshold");
            const auto sneak = simulate(3, 1, 0, false, true);
            t.Check(near(sneak.packet.yaw, -9.73f) && sneak.packet.moveVector.x == 0 &&
                        sneak.packet.moveVector.z == 0,
                    "native player-relative transform sees the already sneak-scaled movement vector");
            const auto deadzone = simulate(3, 0, 0.1f);
            t.Check(deadzone.packet.yaw == 0 && deadzone.packet.pitch == -23 &&
                        deadzone.packet.moveVector.x == 0 && deadzone.packet.moveVector.z == 0,
                    "player-relative movement at the native deadzone neither travels nor resets pitch");
            const auto above = simulate(3, 0, 0.1001f);
            t.Check(near(above.packet.yaw, 10) && above.packet.pitch == 0 &&
                        above.packet.moveVector.x == 0 && above.packet.moveVector.z == 0,
                    "player-relative movement above the deadzone applies turning and pitch reset");

            Input input;
            Options options;
            InputManager controls(input, options);
            LocalPlayer player;
            Camera actorView;
            actorView.yaw = 90;
            actorView.pitch = 23;
            physics::State state;
            state.x = state.z = 0.5;
            state.y = 64;
            for (int scheme : {2, 4}) {
                player.SetCameraControl(scheme, 0);
                MovementProtocolTestAccess::Place(player, state);
                MovementProtocolTestAccess::Tick(player, rig.session, controls, actorView, world);
                t.Check(!rig.attempts.empty() && rig.attempts.back().pitch == 0 &&
                            rig.attempts.back().yaw == 0,
                        "entering an absolute-aim strafe control resets pitch even without movement");
                player.RequestCameraControlPitchReset();
                actorView.pitch = 47;
                MovementProtocolTestAccess::Place(player, state);
                MovementProtocolTestAccess::Tick(player, rig.session, controls, actorView, world);
                t.Check(!rig.attempts.empty() && rig.attempts.back().pitch == 0 &&
                            rig.attempts.back().yaw == 0,
                        "a repeated strafe control packet can request another idle pitch reset");
            }
        }
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
            malformed = rig.move;
            malformed.cameraOrientation =
                std::array<float, 3>{0, std::numeric_limits<float>::quiet_NaN(), 1};
            t.Check(!rig.session.SendMoveInput(malformed) && rig.session.LastAuthTick() == 0 &&
                        rig.attempts.empty(),
                    "invalid camera direction leaves the movement frame and pending actions intact");

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
