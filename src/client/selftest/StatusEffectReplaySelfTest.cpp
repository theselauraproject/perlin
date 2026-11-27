#include "client/selftest/Check.hpp"
#include "client/selftest/MovementProtocolFixtures.hpp"
#include "network/Stream.hpp"

#include <algorithm>
#include <optional>

namespace {
    using namespace perlin;

    struct Rig {
        BedrockSession session;
        LocalPlayer player;

        explicit Rig(int protocol) {
            session.BeginReplayMode("effect-replay", 7, protocol);
            MovementProtocolTestAccess::Initialize(session);
            MovementProtocolTestAccess::SetAuthTick(session, 108);
            physics::State state;
            state.y = 64;
            MovementProtocolTestAccess::Place(player, state);
            session.SetPose(0, 64, 0, 0, 0);
        }

        void Frames(int id, int amplifier, int duration) {
            for (int frame = 0; frame < 8; ++frame) {
                const int remaining = duration == -1 ? -1 : std::max(0, duration - frame);
                std::vector<BedrockSession::EffectState> effects;
                physics::Attributes attributes;
                if (remaining) {
                    effects.push_back({id, amplifier, remaining, false, true});
                    if (id == 24)
                        attributes.levitation = amplifier + 1;
                    else if (id == 8)
                        attributes.jumpBoost = amplifier + 1;
                    else if (id == 27)
                        attributes.slowFalling = true;
                }
                // These frames isolate effect-history propagation from collision-world replay.
                // Live physics and outgoing packets are exercised by MovementEffectsSelfTest.
                MovementProtocolTestAccess::EffectFrame(player, 101 + frame, effects, attributes);
            }
        }

        void Update(int id, int amplifier, int duration, uint64_t tick,
                    proto::MobEffectPacketEventId event = proto::MobEffectPacketEventId::Update) {
            proto::MobEffectPacket packet;
            packet.runtimeEntityId = 7;
            packet.eventId = event;
            packet.effectId = id;
            packet.amplifier = amplifier;
            packet.duration = duration;
            packet.tick = tick;
            session.InjectIncoming(packet.kId, net::Encode(packet));
            MovementProtocolTestAccess::Effects(player, session);
        }

        std::optional<BedrockSession::EffectState> Effect(size_t index, int id) const {
            const auto& effects = MovementProtocolTestAccess::FrameEffects(player, index);
            const auto found = std::find_if(effects.begin(), effects.end(),
                                             [id](const auto& effect) { return effect.id == id; });
            return found == effects.end() ? std::nullopt : std::optional{*found};
        }
    };
}

namespace perlin::selftest {
    int StatusEffectReplay() {
        Checker t("status_effect_replay_test");
        const int previousProtocol = net::context::ProtocolSlot().load(std::memory_order_relaxed);
        for (const int protocol : {2193, 2225}) {
            const net::context::ScopedSettings context({protocol, -1});
            {
                Rig rig(protocol);
                rig.Frames(24, 3, 3);
                rig.Update(24, 1, 40, 100);
                for (size_t frame = 0; frame < 8; ++frame) {
                    const auto effect = rig.Effect(frame, 24);
                    t.Check(frame < 3 ? effect && effect->amplifier == 3 &&
                                            effect->ticks == 3 - static_cast<int>(frame)
                                      : !effect,
                            "an ignored weaker effect stays absent after the stronger effect expires");
                    t.Equal(MovementProtocolTestAccess::FrameAttributes(rig.player, frame).levitation,
                            frame < 3 ? 4 : 0, "replay physics uses the effective merged levitation level");
                }
            }
            {
                Rig rig(protocol);
                rig.Frames(8, 1, 6);
                rig.Update(8, 1, 2, 100);
                t.Check(rig.Effect(4, 8) && rig.Effect(4, 8)->ticks == 2 && !rig.Effect(6, 8),
                        "same-amplifier shortening cannot truncate a historical effect");
                rig.Update(8, 4, 2, 100);
                for (size_t frame = 0; frame < 8; ++frame) {
                    t.Equal(MovementProtocolTestAccess::FrameAttributes(rig.player, frame).jumpBoost,
                            frame < 2 ? 5 : 0,
                            "a stronger short effect replaces the old effect without restoring it later");
                }
            }
            {
                Rig rig(protocol);
                rig.Frames(27, 0, -1);
                rig.Update(27, 0, 1, 100);
                t.Check(rig.Effect(7, 27) && rig.Effect(7, 27)->ticks == -1,
                        "finite same-amplifier updates preserve historical infinite duration");
                rig.Update(27, 0, 0, 104, proto::MobEffectPacketEventId::Remove);
                t.Check(MovementProtocolTestAccess::FrameAttributes(rig.player, 3).slowFalling &&
                            !MovementProtocolTestAccess::FrameAttributes(rig.player, 4).slowFalling,
                        "a stamped removal first affects the frame after its stamp");
                rig.Update(27, 1, 20, 100);
                t.Check(rig.Effect(3, 27) && rig.Effect(3, 27)->amplifier == 1 && !rig.Effect(4, 27) &&
                            !rig.Effect(7, 27),
                        "replaying an earlier update preserves a later recorded removal");
            }
            {
                Rig rig(protocol);
                rig.Frames(24, 0, 0);
                rig.Update(24, 2, 2, 106, proto::MobEffectPacketEventId::Add);
                rig.Update(24, 1, 20, 100);
                for (size_t frame = 0; frame < 8; ++frame) {
                    t.Equal(MovementProtocolTestAccess::FrameAttributes(rig.player, frame).levitation,
                            frame < 6 ? 2 : 3,
                            "an earlier stamped correction reapplies later recorded stronger updates");
                }
                t.Equal(MovementProtocolTestAccess::EffectEvents(rig.player, 0), size_t{1},
                        "the earlier update is recorded at its first affected frame");
                t.Equal(MovementProtocolTestAccess::EffectEvents(rig.player, 6), size_t{1},
                        "the later update retains its separate frame boundary");
            }
            {
                Rig rig(protocol);
                rig.Frames(24, 0, 0);
                rig.Update(24, 2, 4, 98);
                t.Check(rig.Effect(0, 24) && rig.Effect(0, 24)->ticks == 2 &&
                            rig.Effect(1, 24) && rig.Effect(1, 24)->ticks == 1 && !rig.Effect(2, 24),
                        "an update older than retained history is aged to its first replayed frame");
            }
            {
                Rig rig(protocol);
                rig.Frames(27, 0, 0);
                rig.Update(27, 0, 2, 200);
                t.Check(!rig.Effect(0, 27) && !rig.Effect(7, 27) &&
                            MovementProtocolTestAccess::PendingEffects(rig.player) == 1,
                        "a future stamp preserves the historical frames and records the live boundary");
                rig.Update(27, 0, 2, 0);
                t.Check(!rig.Effect(0, 27) && !rig.Effect(7, 27) &&
                            MovementProtocolTestAccess::PendingEffects(rig.player) == 2,
                        "an unstamped event applies live without rewriting older history");
            }
            {
                Rig rig(protocol);
                rig.Frames(27, 0, 0);
                rig.Update(27, 0, 4, 0);
                MovementProtocolTestAccess::SettleColumn(rig.session, 0, 0);
                struct Air final : physics::World {
                    physics::BlockShape Query(int, int, int) const override { return {}; }
                } world;
                Input input;
                Options options;
                InputManager controls(input, options);
                Camera camera;
                MovementProtocolTestAccess::ReplayTransport(rig.session, false);
                MovementProtocolTestAccess::Tick(rig.player, rig.session, controls, camera, world);
                t.Check(rig.session.LastAuthTick() == 109 &&
                            MovementProtocolTestAccess::PendingEffects(rig.player) == 1,
                        "a failed send retains an unrecorded effect event while simulation time advances");
                MovementProtocolTestAccess::ReplayTransport(rig.session, true);
                MovementProtocolTestAccess::Tick(rig.player, rig.session, controls, camera, world);
                t.Check(MovementProtocolTestAccess::PendingEffects(rig.player) == 0 &&
                            MovementProtocolTestAccess::EffectEvents(rig.player, 8) == 1,
                        "the next accepted frame records the retained effect event once");
                const auto& event = MovementProtocolTestAccess::EffectEvent(rig.player, 8, 0);
                t.Check(event.tick == 109 && event.duration == 3,
                        "an unstamped event ages from reception through a failed-send gap");
            }
        }
        net::context::SetProtocol(previousProtocol);
        return t.Finish();
    }
}
