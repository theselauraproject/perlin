#include "client/selftest/Check.hpp"
#include "client/selftest/MovementProtocolFixtures.hpp"
#include "client/systems/input/InputManager.hpp"
#include "network/Stream.hpp"

#include <cmath>
#include <limits>

namespace perlin::selftest {
    int InputReporting() {
        Checker t("input_reporting_test");
        const auto near = [](float a, float b) { return std::fabs(a - b) < 0.00001f; };
        {
            Input input;
            Options options;
            InputManager manager(input, options);
            input.OnMouseDown(Mouse::Right);
            manager.Poll();
            t.Check(manager.Pressed(Action::Use), "the first button transition reports use pressed");
            input.OnMouseDown(Mouse::Right);
            manager.Poll();
            t.Check(!manager.Pressed(Action::Use) && manager.IsDown(Action::Use),
                    "a repeated mouse-down notification preserves held state without another press");
            input.OnMouseUp(Mouse::Right);
            manager.Poll();
            input.OnMouseDown(Mouse::Right);
            manager.Poll();
            t.Check(manager.Pressed(Action::Use), "a real release and new press creates a new input edge");
            input.OnMouseUp(Mouse::Right);
            manager.Poll();
            t.Equal(manager.Device(), InputDevice::Mouse, "default input remains mouse and keyboard");
            input.SetPadConnected(true);
            t.Equal(manager.Device(), InputDevice::Mouse, "connecting an idle controller does not select it");
            input.NoteDevice(InputDevice::Gamepad);
            input.SetPadMove(0.2f, -0.35f);
            input.OnPadDown(Pad::LeftStickUp);
            t.Check(near(manager.Axis(Action::Forward, Action::Back), 0.35f) &&
                        near(manager.Axis(Action::Right, Action::Left), 0.2f),
                    "stick movement preserves magnitude instead of becoming virtual-button speed");
            t.Check(near(manager.AnalogMovement().forward, 0.35f) &&
                        near(manager.AxisBeforeServerLocks(Action::Forward, Action::Back), 0.35f),
                    "effective analog and raw movement use the same underlying stick sample");
            manager.SetServerLocks(512u);
            t.Check(manager.Axis(Action::Forward, Action::Back) == 0 &&
                        manager.AnalogMovement().forward == 0 &&
                        near(manager.AxisBeforeServerLocks(Action::Forward, Action::Back), 0.35f),
                    "a forward permission lock filters effective analog but preserves raw analog");
            manager.SetServerLocks(4u);
            t.Check(manager.AnalogMovement().forward == 0 && manager.AnalogMovement().strafe == 0 &&
                        near(manager.AxisBeforeServerLocks(Action::Right, Action::Left), 0.2f),
                    "the all-movement lock does not rewrite raw stick input");
            manager.SetServerLocks(0);
            manager.SetModFreeze(true);
            t.Check(manager.AnalogMovement().forward == 0 &&
                        manager.AxisBeforeServerLocks(Action::Forward, Action::Back) == 0,
                    "mod freeze applies to effective and raw movement");
            manager.SetModFreeze(false);
            manager.SetBlocked(true);
            t.Check(manager.AnalogMovement().forward == 0 &&
                        manager.AxisBeforeServerLocks(Action::Forward, Action::Back) == 0,
                    "blocking gameplay prevents stick movement");
            manager.SetBlocked(false);
            input.SetTextMode(true);
            t.Check(manager.AnalogMovement().forward == 0, "text entry prevents stick movement");
            input.SetTextMode(false);
            input.OnKeyDown(options.GetKey(Action::Forward).code);
            input.OnMouseMove(50, 50);
            t.Equal(manager.Device(), InputDevice::Gamepad,
                    "synthetic key and cursor events do not misreport the physical controller");
            input.NoteDevice(InputDevice::Mouse);
            t.Check(manager.Axis(Action::Forward, Action::Back) == 1 &&
                        manager.AnalogMovement().forward == 0,
                    "actual keyboard activity selects digital movement");
            input.NoteDevice(InputDevice::Touch);
            input.SetTouchMove(-0.25f, -0.5f);
            input.SetTouchInteract(true);
            t.Check(near(manager.Axis(Action::Right, Action::Left), -0.25f) &&
                        near(manager.Axis(Action::Forward, Action::Back), 0.5f),
                    "touch stick magnitude reaches movement without synthetic-key quantization");
            t.Equal(manager.Interaction(), InputInteraction::Touch, "touch aiming reports touch interaction");
            input.SetTouchInteract(false);
            t.Equal(manager.Interaction(), InputInteraction::Crosshair,
                    "touch crosshair controls retain their selected interaction model");
            manager.SetPluginGrab(false, true);
            t.Check(manager.AnalogMovement().forward == 0 &&
                        manager.AxisBeforeServerLocks(Action::Forward, Action::Back) == 0,
                    "touch analog movement respects a plugin keyboard grab");
            manager.SetPluginGrab(false, false);
            input.ReleaseAll();
            t.Check(manager.AnalogMovement().forward == 0 && manager.AnalogMovement().strafe == 0,
                    "focus release clears all analog movement");
            input.NoteDevice(InputDevice::Gamepad);
            input.SetPadMove(1, -1);
            const auto diagonal = manager.AnalogMovement();
            t.Check(near(std::hypot(diagonal.strafe, diagonal.forward), 1),
                    "diagonal stick input is bounded by the unit circle");
            options.SetPadKey(Action::Forward, Binding::PadButton(Pad::LeftStickRight));
            options.SetPadKey(Action::Right, Binding{});
            input.SetPadMove(0.4f, 0);
            t.Check(near(manager.Axis(Action::Forward, Action::Back), 0.4f) &&
                        manager.Axis(Action::Right, Action::Left) == 0,
                    "analog movement follows the configured direction binding");
        }
        {
            struct Air final : physics::World {
                physics::BlockShape Query(int, int, int) const override { return {}; }
            } world;
            const auto simulate = [&](float stick, uint32_t locks) {
                Input input;
                Options options;
                InputManager manager(input, options);
                input.NoteDevice(InputDevice::Gamepad);
                input.SetPadMove(0, -stick);
                manager.SetServerLocks(locks);
                BedrockSession session;
                session.BeginReplayMode("input", 7, kBedrockProtocol);
                MovementProtocolTestAccess::Initialize(session);
                session.SetPose(0, 64, 0, 0, 0);
                // The supplied physics world is ready, but the session also waits for its column.
                // Replay initialization alone leaves that guard closed and would skip every step.
                MovementProtocolTestAccess::SettleColumn(session, 0, 0);
                t.Check(session.ColumnSettled(0, 0), "the simulation fixture has a settled player column");
                proto::PlayerAuthInputPacket sent;
                bool captured = false;
                session.SetSentTap([&](uint32_t id, const std::vector<uint8_t>& bytes) {
                    if (id != proto::PlayerAuthInputPacket::kId)
                        return;
                    net::ReadStream stream(bytes.data(), bytes.size());
                    captured = proto::Read(stream, sent) && stream.remaining() == 0;
                });
                LocalPlayer player;
                player.ReplayPlace(0, 64, 0);
                Camera camera;
                MovementProtocolTestAccess::Tick(player, session, manager, camera, world);
                session.SetSentTap({});
                t.Check(captured, "a simulation tick emits a decodable auth-input packet");
                t.Equal(sent.inputMode, proto::PlayerAuthInputPacketInputMode::GamePad,
                        "the packet reports the active physical gamepad");
                t.Equal(sent.interactionModel, proto::PlayerAuthInputPacketInteractionModel::Crosshair,
                        "gamepad aiming reports crosshair interaction");
                t.Check(near(sent.rawMoveVector.z, stick), "the packet preserves raw stick magnitude");
                t.Check(near(sent.analogueMoveVector.z, locks ? 0.0f : stick) &&
                            near(sent.moveVector.z, locks ? 0.0f : stick),
                        "packet effective movement and analog fields agree with permission filtering");
                const auto& state = player.State();
                t.Check(near(sent.delta.x, static_cast<float>(state.vx)) &&
                            near(sent.delta.z, static_cast<float>(state.vz)),
                        "the packet velocity is the resulting local simulation velocity");
                return std::hypot(state.vx, state.vz);
            };
            const double full = simulate(1, 0);
            const double quarter = simulate(0.25f, 0);
            const double locked = simulate(0.25f, 512u);
            t.Check(full > 0 && quarter > 0 && std::fabs(quarter / full - 0.25) < 0.0001,
                    "quarter stick input produces quarter movement acceleration");
            t.Check(locked == 0, "permission-locked analog input does not move local physics");
        }
        return t.Finish();
    }
}
