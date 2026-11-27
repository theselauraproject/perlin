#include "client/selftest/Check.hpp"
#include "client/selftest/MovementProtocolFixtures.hpp"
#include "network/Stream.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <optional>

namespace {
    using namespace perlin;

    uint32_t Bits(float value) { return std::bit_cast<uint32_t>(value); }

    bool Has(const proto::PlayerAuthInputPacket& packet, proto::InputData flag) {
        return std::ranges::find(packet.inputData, flag) != packet.inputData.end();
    }

    struct TestWorld final : physics::World {
        bool floor = false;
        physics::BlockShape Query(int, int y, int) const override {
            physics::BlockShape block;
            if (floor && y < 64) {
                block.isAir = false;
                block.boxes.push_back({0, 0, 0, 1, 1, 1});
            }
            return block;
        }
    };

    struct Rig {
        BedrockSession session;
        Input input;
        Options options;
        InputManager controls{input, options};
        LocalPlayer player;
        Camera camera;
        TestWorld world;
        std::vector<proto::PlayerAuthInputPacket> sent;
        bool decoded = true;

        explicit Rig(int protocol) {
            session.BeginReplayMode("movement-effects", 7, protocol);
            MovementProtocolTestAccess::Initialize(session);
            MovementProtocolTestAccess::SettleColumn(session, 0, 0);
            Place(0);
            session.SetSentTap([this](uint32_t id, const std::vector<uint8_t>& body) {
                if (id != proto::PlayerAuthInputPacket::kId)
                    return;
                proto::PlayerAuthInputPacket packet;
                net::ReadStream stream(body.data(), body.size());
                decoded &= proto::Read(stream, packet) && stream.remaining() == 0;
                sent.push_back(std::move(packet));
            });
        }

        ~Rig() { session.SetSentTap({}); }

        void Place(float vertical, bool grounded = false) {
            physics::State state;
            state.x = state.z = 0.5;
            state.y = 64;
            state.vy = vertical;
            state.onGround = grounded;
            state.fallDistance = 11.25f;
            MovementProtocolTestAccess::Place(player, state);
            session.SetPose(0.5f, 64, 0.5f, 0, 0);
        }

        void Effect(int id, int amplifier, int duration,
                    proto::MobEffectPacketEventId event = proto::MobEffectPacketEventId::Add,
                    uint64_t actor = 7, uint64_t stamp = 0) {
            proto::MobEffectPacket packet;
            packet.runtimeEntityId = actor;
            packet.eventId = event;
            packet.effectId = id;
            packet.amplifier = amplifier;
            packet.duration = duration;
            packet.tick = stamp;
            packet.particles = true;
            // Tick zero is the native immediate path. Stamped rewind semantics
            // have separate tests and must not be inferred from this fixture.
            const net::context::ScopedSettings context({session.ServerProtocol(), -1});
            session.InjectIncoming(packet.kId, net::Encode(packet));
        }

        std::optional<BedrockSession::EffectState> Effect(int id) const {
            for (const auto& effect : session.GetEffects())
                if (effect.id == id)
                    return effect;
            return std::nullopt;
        }

        const proto::PlayerAuthInputPacket& Step(selftest::Checker& t) {
            const size_t count = sent.size();
            MovementProtocolTestAccess::Effects(player, session);
            MovementProtocolTestAccess::Tick(player, session, controls, camera, world);
            t.Check(sent.size() == count + 1 && decoded,
                    "a real local-player tick emits one fully decoded movement packet");
            static const proto::PlayerAuthInputPacket empty;
            if (sent.size() == count)
                return empty;
            const auto& packet = sent.back();
            const auto& state = player.State();
            t.Check(packet.tick == session.LastAuthTick() &&
                        Bits(packet.delta.y) == Bits(static_cast<float>(state.vy)) &&
                        Bits(packet.position.y) ==
                            Bits(static_cast<float>(state.y) + kPlayerEyeHeight),
                    "wire tick, vertical velocity and eye position reflect the completed simulation");
            return packet;
        }
    };
}

namespace perlin::selftest {
    int MovementEffects() {
        Checker t("movement_effects_test");
        for (int protocol : {944, 2193, kBedrockProtocol}) {
            // These literal float bits are derived from the inspected native
            // gravity/drag operations, independently of the Perlin helpers.
            // They prove this bounded packet-to-physics path, not whole-client
            // equivalence for any of these protocol profiles.
            for (const auto& [vertical, expected] :
                 std::array<std::pair<float, uint32_t>, 3>{{{-0.25f, 0xbe827525u},
                                                           {0.0f, 0xbda0902eu},
                                                           {0.25f, 0x3e2a9931u}}}) {
                Rig rig(protocol);
                rig.Effect(27, 0, 100);
                rig.Place(vertical);
                const auto& packet = rig.Step(t);
                t.Equal(Bits(packet.delta.y), expected,
                        "slow falling changes descent gravity only for strictly negative velocity");
                if (vertical < 0)
                    t.Check(rig.player.State().fallDistance == 0,
                            "slow falling resets simulated fall distance during descent");
            }
            {
                Rig rig(protocol);
                rig.Effect(27, 0, -1);
                rig.Place(-0.25f);
                constexpr std::array<uint32_t, 6> descent{
                    0xbe827525u, 0xbe84ddb5u, 0xbe8739f0u, 0xbe898a15u, 0xbe8bce63u, 0xbe8e0716u};
                for (uint32_t expected : descent)
                    t.Equal(Bits(rig.Step(t).delta.y), expected,
                            "an infinite slow-falling packet remains active over successive movement frames");
                rig.Effect(27, 0, 0, proto::MobEffectPacketEventId::Remove);
                t.Check(!rig.Effect(27), "a removal packet clears the local effect");
                rig.Place(0);
                t.Equal(Bits(rig.Step(t).delta.y), 0xbda0902eu,
                        "the first frame after removal restores normal gravity");
            }
            {
                Rig rig(protocol);
                rig.Effect(27, 0, 100, proto::MobEffectPacketEventId::Add, 8);
                t.Check(!rig.Effect(27), "another actor's effect cannot alter local physics");
                for (uint8_t event : {uint8_t{0}, uint8_t{4}, uint8_t{255}})
                    rig.Effect(27, 0, 100, static_cast<proto::MobEffectPacketEventId>(event));
                t.Check(!rig.Effect(27), "unknown effect events cannot install slow falling");
            }
            {
                Rig rig(protocol);
                rig.Effect(27, 0, -1);
                rig.Effect(24, 0, 40);
                rig.Place(-0.25f);
                t.Equal(Bits(rig.Step(t).delta.y), 0xbe3eab37u,
                        "levitation takes precedence over slow-falling ground gravity");
                rig.Effect(24, 0, 0, proto::MobEffectPacketEventId::Remove);
                rig.Place(-0.25f);
                t.Equal(Bits(rig.Step(t).delta.y), 0xbe827525u,
                        "removing levitation reveals the still-active slow-falling effect");
            }
            {
                Rig rig(protocol);
                rig.player.SetElytra(true);
                rig.input.OnKeyDown(rig.options.GetKey(Action::Jump).code);
                rig.Effect(24, 0, 100);
                rig.Place(0);
                const auto& start = rig.Step(t);
                t.Check(rig.player.Gliding() && Has(start, proto::InputData::StartGliding),
                        "an airborne elytra input begins gliding with levitation still active");
                t.Equal(Bits(start.delta.y), 0xbb86bdf6u,
                        "a live levitation packet modifies the resolved glide velocity after glide drag");
                rig.Effect(27, 0, -1);
                rig.Place(0);
                t.Equal(Bits(rig.Step(t).delta.y), 0x3c06f049u,
                        "slow-falling glide acceleration and post-move levitation both affect the wire velocity");
                rig.Effect(24, 0, 0, proto::MobEffectPacketEventId::Remove);
                rig.Place(0);
                t.Equal(Bits(rig.Step(t).delta.y), 0xbb1081c3u,
                        "removing levitation preserves gliding and its remaining slow-falling acceleration");
            }
            {
                Rig rig(protocol);
                rig.Effect(24, 2, 40);
                rig.Effect(24, 1, 200, proto::MobEffectPacketEventId::Update);
                const auto strong = rig.Effect(24);
                t.Check(strong && strong->amplifier == 2 && strong->ticks == 40,
                        "a weaker update cannot replace the stronger active magnitude or duration");
                rig.Place(-0.25f);
                t.Equal(Bits(rig.Step(t).delta.y), 0xbe2a9931u,
                        "the retained stronger effect controls the next outgoing velocity");
                rig.Effect(24, 2, 5, proto::MobEffectPacketEventId::Update);
                const auto shorter = rig.Effect(24);
                t.Check(shorter && shorter->ticks == 39,
                        "an equal amplifier with shorter duration preserves elapsed remaining time");
                rig.Effect(24, 2, 60, proto::MobEffectPacketEventId::Update);
                const auto longer = rig.Effect(24);
                t.Check(longer && longer->ticks == 60,
                        "an equal amplifier may extend the active duration");
                rig.Effect(24, 3, 2, proto::MobEffectPacketEventId::Update);
                const auto stronger = rig.Effect(24);
                t.Check(stronger && stronger->amplifier == 3 && stronger->ticks == 2,
                        "a stronger update replaces duration even when the new duration is shorter");
                rig.Place(-0.25f);
                t.Equal(Bits(rig.Step(t).delta.y), 0xbe20902eu,
                        "a stronger effect changes the actual packet velocity");
            }
            {
                Rig rig(protocol);
                rig.Effect(27, 0, -1);
                rig.Effect(27, 0, 1, proto::MobEffectPacketEventId::Update);
                t.Check(rig.Effect(27) && rig.Effect(27)->ticks == -1,
                        "an equal-amplifier finite update cannot shorten an infinite effect");
                rig.Effect(27, 0, 0, static_cast<proto::MobEffectPacketEventId>(0));
                t.Check(rig.Effect(27) && rig.Effect(27)->ticks == -1,
                        "an unknown event cannot remove or replace an existing effect");
            }
            {
                Rig rig(protocol);
                rig.Effect(27, 0, 3, proto::MobEffectPacketEventId::Add, 7, 1000);
                t.Check(rig.Effect(27) && rig.Effect(27)->ticks == 3,
                        "a future packet timestamp applies the full live duration on receipt");
                rig.Place(-0.25f);
                rig.Step(t);
                t.Check(rig.Effect(27) && rig.Effect(27)->ticks == 2,
                        "a future timestamp cannot postpone the live effect countdown");
                rig.Step(t);
                rig.Step(t);
                t.Check(!rig.Effect(27), "finite future-stamped effects expire after their actual duration");
            }
            {
                Rig rig(protocol);
                rig.world.floor = true;
                rig.Place(0, true);
                rig.Effect(27, 0, -1);
                rig.Effect(8, 1, 100);
                rig.input.OnKeyDown(rig.options.GetKey(Action::Jump).code);
                const auto& packet = rig.Step(t);
                t.Equal(Bits(packet.delta.y), 0x3f0779a7u,
                        "jump boost and slow falling preserve the native ascending jump impulse and drag");
                t.Check(Has(packet, proto::InputData::StartJumping) &&
                            Has(packet, proto::InputData::JumpCurrentRaw),
                        "the boosted ground jump reports both the start action and held raw input");
                const auto& airborne = rig.Step(t);
                t.Check(!Has(airborne, proto::InputData::StartJumping) &&
                            Has(airborne, proto::InputData::JumpCurrentRaw),
                        "holding jump in the air cannot repeat the ground-jump action");
            }
        }
        return t.Finish();
    }
}
