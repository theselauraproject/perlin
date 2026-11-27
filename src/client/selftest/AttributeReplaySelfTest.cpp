#include "client/selftest/Check.hpp"
#include "client/selftest/MovementProtocolFixtures.hpp"
#include "client/systems/player/BlockCollision.hpp"
#include "network/Stream.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace {
    using namespace perlin;

    proto::PlayerAttributesEntry Attribute(std::string name, float value,
                                           std::optional<float> base = std::nullopt) {
        proto::PlayerAttributesEntry entry;
        entry.name = std::move(name);
        entry.min = entry.defaultMin = 0;
        entry.max = entry.defaultMax = 10;
        entry.current = value;
        entry.default_ = base.value_or(value);
        return entry;
    }

    bool SameState(const physics::State& a, const physics::State& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z && a.vx == b.vx && a.vy == b.vy && a.vz == b.vz;
    }

    struct Rig {
        BedrockSession session;
        LocalPlayer player;
        Input input;
        Options options;
        InputManager controls{input, options};
        Camera camera;
        LevelCollision world{session.NetLevel(), nullptr};
        std::vector<proto::PlayerAuthInputPacket> sent;
        bool decoded = true;

        explicit Rig(int protocol) {
            session.BeginReplayMode("attribute-replay", 7, protocol);
            MovementProtocolTestAccess::Initialize(session);
            session.NetLevel().EnsureChunk(0, 0);
            MovementProtocolTestAccess::SettleColumn(session, 0, 0);
            physics::State state;
            state.x = state.z = 8;
            state.y = 64;
            MovementProtocolTestAccess::Place(player, state);
            session.SetPose(8, 64, 8, 0, 0);
            camera.yaw = 90;
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

        void Update(std::vector<proto::PlayerAttributesEntry> entries, uint64_t tick,
                    uint64_t actor = 7, bool drain = true) {
            proto::UpdateAttributesPacket packet;
            packet.runtimeEntityId = actor;
            packet.tick = tick;
            packet.attributes = std::move(entries);
            const net::context::ScopedSettings context({session.ServerProtocol(), -1});
            session.InjectIncoming(packet.kId, net::Encode(packet));
            if (drain)
                MovementProtocolTestAccess::Attributes(player, session);
        }

        void Seed(float value = .1f, std::optional<float> base = std::nullopt) {
            Update({Attribute("minecraft:movement", value, base),
                    Attribute("minecraft:underwater_movement", .03f),
                    Attribute("minecraft:lava_movement", .04f),
                    Attribute("minecraft:air_drag_modifier", .91f)}, 0);
        }

        void Frames(const std::array<bool, 8>& sprint = {}, const std::array<float, 8>& freeze = {}) {
            for (size_t i = 0; i < 8; ++i) {
                physics::Input in;
                in.forward = 1;
                in.sprint = sprint[i];
                MovementProtocolTestAccess::AttributeFrame(player, session, 101 + i, in, freeze[i]);
            }
            MovementProtocolTestAccess::SetAuthTick(session, 108);
        }

        const physics::Attributes& Frame(size_t i) const {
            return MovementProtocolTestAccess::FrameAttributes(player, i);
        }

        void Tick() { MovementProtocolTestAccess::Tick(player, session, controls, camera, world); }
    };
}

namespace perlin::selftest {
    int AttributeReplay() {
        Checker t("attribute_replay_test");
        for (int protocol : {944, 2193, kBedrockProtocol}) {
            const net::context::ScopedSettings context({protocol, -1});
            {
                Rig rig(protocol);
                rig.Seed();
                rig.Frames();
                rig.Update({Attribute("minecraft:movement", .2f),
                            Attribute("minecraft:underwater_movement", .07f),
                            Attribute("minecraft:lava_movement", .09f),
                            Attribute("minecraft:air_drag_modifier", .8f)}, 103);
                for (size_t i = 0; i < 8; ++i) {
                    t.Check(rig.Frame(i).speed == (i < 3 ? .1f : .2f) &&
                                rig.Frame(i).underwaterSpeed == (i < 3 ? .03f : .07f) &&
                                rig.Frame(i).lavaSpeed == (i < 3 ? .04f : .09f) &&
                                rig.Frame(i).airDrag == (i < 3 ? .91f : .8f),
                            "stamped movement and fluid attributes begin at the following saved frame");
                }
                t.Equal(MovementProtocolTestAccess::AttributeEvents(rig.player, 3), size_t{1},
                        "a correction retains its full attribute event at the first affected frame");
                rig.Update({Attribute("minecraft:movement", .3f)}, 106);
                rig.Update({Attribute("minecraft:movement", .4f)}, 102);
                for (size_t i = 0; i < 8; ++i) {
                    const float expected = i < 2 ? .1f : i == 2 ? .4f : i < 6 ? .2f : .3f;
                    t.Equal(rig.Frame(i).speed, expected,
                            "replaying an earlier attribute update reapplies later retained updates");
                }
                t.Check(rig.Frame(7).underwaterSpeed == .07f && rig.Frame(7).lavaSpeed == .09f &&
                            rig.Frame(7).airDrag == .8f,
                        "a partial speed correction preserves later fluid and drag attributes");
            }
            {
                Rig rig(protocol);
                rig.Seed(.3f, .2f);
                constexpr std::array<bool, 8> sprint{false, true, true, false, false, true, false, false};
                constexpr std::array<float, 8> freeze{0, .2f, .4f, .6f, .8f, 1, .5f, 0};
                rig.Frames(sprint, freeze);
                rig.Update({Attribute("minecraft:movement", .6f, .4f)}, 101);
                t.Equal(rig.Frame(0).speed, .3f, "the stamped frame retains its original raw current value");
                for (size_t i = 1; i < 8; ++i) {
                    const float expected = (.4f - .05f * freeze[i]) * (sprint[i] ? 1.3f : 1.0f);
                    t.Check(std::fabs(rig.Frame(i).speed - expected) < 0.0000001f,
                            "attribute replay evaluates each saved sprint and freeze state in sequence");
                }
                t.Equal(rig.Frame(7).speed, .4f,
                        "removing a predicted sprint modifier retains the recomputed base, not packet current");
                t.Equal(rig.session.CaptureMovementAttribute().value, .6f,
                        "historical modifier evaluation does not overwrite the separately updated live model");
            }
            {
                Rig rig(protocol);
                rig.Seed();
                rig.Frames();
                rig.Update({Attribute("minecraft:movement", .4f)}, 106);
                auto acknowledgment = Attribute("minecraft:movement", .1f, .8f);
                proto::PlayerAttributesEntryModifiersEntry modifier;
                modifier.id = "4807483d-ec4e-4c8f-8c98-cb6e335f48b6";
                modifier.name = "Server modifier snapshot";
                modifier.amount = .15f;
                modifier.operation = 0;
                modifier.operand = 2;
                modifier.serializable = true;
                acknowledgment.modifiers.push_back(modifier);
                rig.Update({acknowledgment}, 101);
                t.Check(rig.session.CaptureMovementAttribute().value == .4f &&
                            rig.session.CaptureMovementAttribute().base[2] == .4f &&
                            rig.session.CaptureMovementAttribute().modifiers.empty(),
                        "an equal historical current value preserves newer live values and modifiers");
                t.Check(rig.Frame(1).speed == .1f && rig.Frame(7).speed == .4f &&
                            MovementProtocolTestAccess::AttributeEvents(rig.player, 1) == 0,
                        "a matching acknowledgment does not create a history correction");
                rig.Update({acknowledgment, Attribute("minecraft:air_drag_modifier", .7f)}, 101);
                const auto& saved = MovementProtocolTestAccess::FrameMovementAttribute(rig.player, 1);
                t.Check(rig.session.CaptureMovementAttribute().value == .4f &&
                            rig.session.CaptureMovementAttribute().base[2] == .4f &&
                            rig.session.AirDragAttribute() == .7f,
                        "mixed acknowledgments update only differing live attributes");
                t.Check(saved.base[2] == .8f && saved.modifiers.size() == 1 &&
                            saved.modifiers[0].id == modifier.id && saved.modifiers[0].amount == .15f,
                        "a required rewind retains even the matching entry's complete historical modifiers");
                t.Check(rig.Frame(7).speed == .4f && rig.Frame(7).airDrag == .7f,
                        "mixed attribute replay preserves a later separate movement update");
            }
            {
                Rig rig(protocol);
                rig.Seed();
                rig.Frames();
                rig.Update({Attribute("minecraft:movement", .2f)}, 103, 7, false);
                rig.Update({Attribute("minecraft:movement", .1f, .7f)}, 101, 7, false);
                MovementProtocolTestAccess::Attributes(rig.player, rig.session);
                t.Check(rig.session.CaptureMovementAttribute().value == .1f &&
                            rig.session.CaptureMovementAttribute().base[2] == .7f,
                        "a pending attribute rewind suppresses the equal-value shortcut for later arrivals");
                t.Check(MovementProtocolTestAccess::AttributeEvents(rig.player, 1) == 1 &&
                            rig.Frame(1).speed == .1f && rig.Frame(3).speed == .2f,
                        "same-drain attribute corrections preserve packet arrival and frame boundary order");
            }
            {
                Rig rig(protocol);
                rig.Seed();
                constexpr std::array<float, 8> freeze{.5f, .5f, .5f, .5f, .5f, .5f, .5f, .5f};
                rig.Frames({}, freeze);
                const float used = rig.Frame(7).speed;
                t.Check(used != rig.session.CaptureMovementAttribute().value,
                        "latest-frame acknowledgment fixture separates evaluated and raw movement values");
                rig.Update({Attribute("minecraft:movement", used, .8f)}, 108);
                t.Check(rig.session.CaptureMovementAttribute().value == .1f &&
                            rig.session.CaptureMovementAttribute().base[2] == .1f &&
                            MovementProtocolTestAccess::PendingAttributes(rig.player) == 0,
                        "a latest-stamp acknowledgment compares the evaluated live value including freeze");
                rig.Update({Attribute("minecraft:movement", .2f)}, 108);
                t.Check(rig.Frame(7).speed == used && rig.session.CaptureMovementAttribute().value == .2f &&
                            MovementProtocolTestAccess::PendingAttributes(rig.player) == 1,
                        "a changed latest-stamp value updates live state and queues the following frame");
                rig.Tick();
                t.Check(rig.player.PredictionHistorySize() == 9 && rig.Frame(8).speed == .2f &&
                            MovementProtocolTestAccess::AttributeEvents(rig.player, 8) == 1,
                        "the next actual input records a changed latest-stamp attribute once");
            }
            {
                Rig rig(protocol);
                rig.Seed();
                rig.Frames();
                rig.Update({Attribute("minecraft:movement", .9f)}, 104, 8);
                t.Check(rig.Frame(7).speed == .1f && rig.session.CaptureMovementAttribute().value == .1f,
                        "another actor's attributes do not alter local movement or replay history");
                rig.Update({Attribute("minecraft:movement", .5f)}, 0);
                t.Check(rig.Frame(7).speed == .1f && rig.session.CaptureMovementAttribute().value == .5f &&
                            MovementProtocolTestAccess::PendingAttributes(rig.player) == 1,
                        "an unstamped update applies live and records a future boundary without rewinding");
                MovementProtocolTestAccess::ReplayTransport(rig.session, false);
                rig.Tick();
                t.Check(MovementProtocolTestAccess::PendingAttributes(rig.player) == 1 &&
                            rig.player.PredictionHistorySize() == 8,
                        "failed transmission retains an unrecorded attribute event");
                MovementProtocolTestAccess::ReplayTransport(rig.session, true);
                rig.Tick();
                t.Check(MovementProtocolTestAccess::PendingAttributes(rig.player) == 0 &&
                            rig.player.PredictionHistorySize() == 9 &&
                            MovementProtocolTestAccess::AttributeEvents(rig.player, 8) == 1,
                        "the next accepted frame records the pending attribute boundary exactly once");
                rig.Update({Attribute("minecraft:movement", .2f)}, 104);
                t.Check(rig.Frame(4).speed == .2f && rig.Frame(7).speed == .2f && rig.Frame(8).speed == .5f,
                        "earlier replay preserves an unstamped update across a failed-send gap");
            }
            {
                Rig rig(protocol);
                rig.Seed();
                rig.Frames();
                rig.Update({Attribute("minecraft:movement", .2f)}, 100);
                rig.Update({Attribute("minecraft:movement", .3f)}, 200);
                t.Check(rig.Frame(0).speed == .1f && rig.Frame(7).speed == .1f &&
                            rig.session.CaptureMovementAttribute().value == .3f &&
                            MovementProtocolTestAccess::PendingAttributes(rig.player) == 2,
                        "out-of-window stamps update the live state without inventing retained frames");
            }
            {
                Rig rig(protocol);
                rig.Frames();
                rig.Update({Attribute("minecraft:movement", .1f, .7f)}, 101);
                t.Check(rig.session.CaptureMovementAttribute().known &&
                            rig.session.CaptureMovementAttribute().base[2] == .7f &&
                            MovementProtocolTestAccess::AttributeEvents(rig.player, 1) == 1,
                        "a substituted default value is not mistaken for a present matching attribute");
            }
            {
                Rig rig(protocol);
                rig.Seed();
                rig.input.OnKeyDown(rig.options.GetKey(Action::Forward).code);
                for (int i = 0; i < 4; ++i)
                    rig.Tick();
                t.Check(rig.player.PredictionHistorySize() == 4 && rig.sent.size() == 4 && rig.decoded,
                        "attribute integration starts from four fully decoded, actually simulated input ticks");
                if (rig.player.PredictionHistorySize() != 4)
                    continue;
                physics::State expected = MovementProtocolTestAccess::FrameBefore(rig.player, 1);
                for (size_t i = 1; i < 4; ++i) {
                    auto attributes = rig.Frame(i);
                    attributes.speed = .2f;
                    attributes.airDrag = .5f;
                    physics::Step(expected, MovementProtocolTestAccess::FrameInput(rig.player, i),
                                  attributes, rig.world);
                }
                const auto old = rig.player.State();
                rig.Update({Attribute("minecraft:movement", .2f),
                            Attribute("minecraft:air_drag_modifier", .5f)}, 1);
                t.Check(SameState(rig.player.State(), expected) && !SameState(old, expected),
                        "a real stamped packet replays the trajectory with corrected movement and drag values");
                t.Equal(rig.sent.size(), size_t{4}, "history replay does not duplicate old input packets");
                rig.Tick();
                t.Check(rig.sent.size() == 5 && rig.decoded &&
                            rig.sent.back().delta.x == static_cast<float>(rig.player.State().vx) &&
                            rig.sent.back().delta.y == static_cast<float>(rig.player.State().vy) &&
                            rig.sent.back().delta.z == static_cast<float>(rig.player.State().vz),
                        "the next outgoing input uses the corrected trajectory and post-step velocity");
            }
        }
        return t.Finish();
    }
}
