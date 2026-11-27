#include "client/systems/input/InputManager.hpp"
#include "util/Utf8.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace perlin {
    bool InputManager::Matches(Action action, const Binding& binding) const {
        if (binding.device == Binding::Device::Gamepad)
            return options.GetPadKey(action) == binding;
        return options.GetKey(action) == binding;
    }

    void InputManager::Dispatch(const Binding& binding) {
        if (rebinding) {
            if (binding.device == Binding::Device::Keyboard && binding.code == Key::Escape) {
                rebinding.reset();
                return;
            }
            options.SetKey(*rebinding, binding);
            rebinding.reset();
            options.Save();
            return;
        }
        for (size_t i = 0; i < kActionCount; i++)
            if (Matches(static_cast<Action>(i), binding))
                pressed.set(i);
    }

    void InputManager::Poll() {
        pressed.reset();
        InputEvent ev;
        typed.clear();
        pluginEvents.clear();
        while (input.events.try_dequeue(ev)) {
            if (grabMouse || grabKeyboard) {
                PluginInput p;
                p.ctrl = input.IsKeyDown(Key::Control);
                p.shift = input.IsKeyDown(Key::Shift);
                p.alt = input.IsKeyDown(Key::Alt);
                if (ev.type == InputEvent::MouseMove && grabMouse) {
                    p.type = PluginInput::Type::Move;
                    p.x = ev.x;
                    p.y = ev.y;
                    if (!pluginEvents.empty() && pluginEvents.back().type == PluginInput::Type::Move)
                        pluginEvents.back() = p;
                    else
                        pluginEvents.push_back(p);
                    continue;
                }
                if ((ev.type == InputEvent::MouseDown || ev.type == InputEvent::MouseUp) && grabMouse) {
                    p.type = PluginInput::Type::Button;
                    p.code = ev.key;
                    p.down = ev.type == InputEvent::MouseDown;
                    p.x = ev.x;
                    p.y = ev.y;
                    pluginEvents.push_back(p);
                    continue;
                }
                if (grabKeyboard && (ev.type == InputEvent::KeyDown || ev.type == InputEvent::KeyUp ||
                                     ev.type == InputEvent::KeyRepeat)) {
                    p.type = PluginInput::Type::Key;
                    p.code = ev.key;
                    p.down = ev.type != InputEvent::KeyUp;
                    p.repeat = ev.type == InputEvent::KeyRepeat;
                    pluginEvents.push_back(p);
                    if (ev.type == InputEvent::KeyDown && ev.key == Key::Escape) {
                        const auto now = std::chrono::steady_clock::now();
                        escapes = {escapes[1], escapes[2], now};
                        if (escapes[0].time_since_epoch().count() != 0 &&
                            now - escapes[0] < std::chrono::milliseconds(1500))
                            failsafe = true;
                    }
                    continue;
                }
                if (grabKeyboard && ev.type == InputEvent::Char) {
                    p.type = PluginInput::Type::Text;
                    p.code = ev.key;
                    const int cp = ev.key;
                    if (cp > 0 && cp < 0x110000 && !(cp >= 0xD800 && cp < 0xE000)) {
                        AppendUtf8(p.text, static_cast<char32_t>(cp));
                        pluginEvents.push_back(p);
                    }
                    continue;
                }
            }
            if (input.TextMode()) {
                if (ev.type == InputEvent::Char)
                    typed.push_back({true, ev.key, false});
                else if (ev.type == InputEvent::KeyDown || ev.type == InputEvent::KeyRepeat)
                    typed.push_back(
                        {false, ev.key, input.IsKeyDown(Key::Control), input.IsKeyDown(Key::Shift)});
                if (ev.type == InputEvent::MouseDown && ev.key >= Mouse::Middle) {
                    if (ev.key != Mouse::Middle)
                        typed.push_back({false, 0, input.IsKeyDown(Key::Control), input.IsKeyDown(Key::Shift),
                                         ev.key});
                    if (options.GetKey(Action::MenuBack) == Binding::MouseButton(ev.key))
                        typed.push_back({false, Key::Escape, false, false, ev.key});
                } else if (ev.type == InputEvent::KeyDown && ev.key != Key::Escape &&
                           options.GetKey(Action::MenuBack) == Binding::KeyboardKey(ev.key)) {
                    typed.push_back({false, Key::Escape, false, false});
                }
                continue;
            }
            if (ev.type == InputEvent::KeyDown) {
                if (ev.key != Key::F3 && input.IsKeyDown(Key::F3))
                    continue;
                Dispatch(Binding::KeyboardKey(ev.key));
            } else if (ev.type == InputEvent::MouseDown)
                Dispatch(Binding::MouseButton(ev.key));
            else if (ev.type == InputEvent::PadDown)
                Dispatch(Binding::PadButton(ev.key));
        }

        const float mx = input.MouseX();
        const float my = input.MouseY();
        look = {};
        scroll = input.TakeWheel();
        if (grabMouse && scroll != 0.0f) {
            PluginInput p;
            p.type = PluginInput::Type::Wheel;
            p.y = scroll;
            pluginEvents.push_back(p);
            scroll = 0.0f;
        }
        float dx = 0, dy = 0;
        input.TakeMouseDelta(dx, dy);
        if (blocked) {
            look = {};
            pressed.reset();
        } else if (grabMouse) {
            look = {};
        } else if (input.Captured()) {
            const float sens = options.mouseSensitivity;
            look.yaw = dx * sens;
            look.pitch = dy * sens * (options.invertMouse ? 1.0f : -1.0f);
            const auto now = std::chrono::steady_clock::now();
            const float dt = lastPoll.time_since_epoch().count() == 0
                                 ? 0.0f
                                 : std::min(0.1f, std::chrono::duration<float>(now - lastPoll).count());
            lastPoll = now;
            const float sx = input.PadLookX(), sy = input.PadLookY();
            if (dt > 0.0f && (sx != 0.0f || sy != 0.0f)) {
                const float speed = 170.0f * options.padSensitivity;
                const auto curve = [](float v) { return v * std::fabs(v); };
                look.yaw += curve(sx) * speed * dt;
                look.pitch += curve(sy) * speed * dt * (options.padInvertY ? 1.0f : -1.0f);
            }
        } else if (haveMouse && IsDown(Action::Look)) {
            const float sens = options.mouseSensitivity;
            look.yaw = (mx - lastMouseX) * sens;
            look.pitch = (my - lastMouseY) * sens * (options.invertMouse ? 1.0f : -1.0f);
        }
        lastMouseX = mx;
        lastMouseY = my;
        haveMouse = true;
        if (!blocked && !input.TextMode())
            pressed |= controlPressed;
        controlPressed.reset();
    }

    bool InputManager::IsDown(Action action) const {
        if (modFreeze && Freezable(action))
            return false;
        return IsDownUnfrozen(action);
    }

    bool InputManager::IsDownUnfrozen(Action action) const {
        return IsDownImpl(action, true);
    }

    bool InputManager::ActionAllowed(Action action, bool applyServerLocks) const {
        if (blocked && action != Action::DebugScreen)
            return false;
        if (input.TextMode())
            return false;
        if (applyServerLocks && locks) {
            const bool all = (locks & 4u) != 0;
            switch (action) {
            case Action::Forward:
                if (all || (locks & 512u))
                    return false;
                break;
            case Action::Back:
                if (all || (locks & 1024u))
                    return false;
                break;
            case Action::Left:
                if (all || (locks & 2048u) || (locks & 16u))
                    return false;
                break;
            case Action::Right:
                if (all || (locks & 4096u) || (locks & 16u))
                    return false;
                break;
            case Action::Jump:
                if (all || (locks & 64u))
                    return false;
                break;
            case Action::Sneak:
                if (locks & 32u)
                    return false;
                break;
            default:
                break;
            }
        }
        return true;
    }

    namespace {
        bool MovementAction(Action action) {
            return action == Action::Forward || action == Action::Back || action == Action::Left ||
                   action == Action::Right;
        }

        bool AnalogBinding(const Binding& binding) {
            return binding.device == Binding::Device::Gamepad && binding.code >= Pad::LeftStickUp &&
                   binding.code <= Pad::RightStickRight;
        }
    }

    float InputManager::PadMovementValue(Action action) const {
        const Binding& binding = options.GetPadKey(action);
        if (!AnalogBinding(binding))
            return 0;
        switch (binding.code) {
        case Pad::LeftStickUp: return std::max(0.0f, -input.PadMoveY());
        case Pad::LeftStickDown: return std::max(0.0f, input.PadMoveY());
        case Pad::LeftStickLeft: return std::max(0.0f, -input.PadMoveX());
        case Pad::LeftStickRight: return std::max(0.0f, input.PadMoveX());
        case Pad::RightStickUp: return std::max(0.0f, -input.PadLookY());
        case Pad::RightStickDown: return std::max(0.0f, input.PadLookY());
        case Pad::RightStickLeft: return std::max(0.0f, -input.PadLookX());
        case Pad::RightStickRight: return std::max(0.0f, input.PadLookX());
        default: return 0;
        }
    }

    bool InputManager::IsDownImpl(Action action, bool applyServerLocks, bool includeAnalog) const {
        if (!ActionAllowed(action, applyServerLocks))
            return false;
        if (controlled)
            return controlTick < controlUntil && controlActions.test(static_cast<size_t>(action));
        if (holds[static_cast<size_t>(action)] > std::chrono::steady_clock::now())
            return true;
        const Binding& pb = options.GetPadKey(action);
        if (MovementAction(action) && AnalogBinding(pb)) {
            if (includeAnalog && Device() == InputDevice::Gamepad && PadMovementValue(action) > 0)
                return true;
        } else if (pb.device == Binding::Device::Gamepad && input.IsPadDown(pb.code)) {
            return true;
        }
        const Binding& b = options.GetKey(action);
        if ((grabMouse && b.device == Binding::Device::Mouse) ||
            (grabKeyboard && b.device == Binding::Device::Keyboard))
            return false;
        switch (b.device) {
        case Binding::Device::Keyboard:
            return input.IsKeyDown(b.code);
        case Binding::Device::Mouse:
            return input.IsMouseDown(b.code);
        default:
            return false;
        }
    }

    float InputManager::Axis(Action positive, Action negative) const {
        if (positive == Action::Forward && negative == Action::Back)
            return Movement(true).forward;
        if (positive == Action::Right && negative == Action::Left)
            return Movement(true).strafe;
        return (IsDown(positive) ? 1.0f : 0.0f) - (IsDown(negative) ? 1.0f : 0.0f);
    }

    float InputManager::AxisBeforeServerLocks(Action positive, Action negative) const {
        if (positive == Action::Forward && negative == Action::Back)
            return Movement(false).forward;
        if (positive == Action::Right && negative == Action::Left)
            return Movement(false).strafe;
        const auto down = [this](Action action) {
            return !(modFreeze && Freezable(action)) && IsDownImpl(action, false);
        };
        return (down(positive) ? 1.0f : 0.0f) - (down(negative) ? 1.0f : 0.0f);
    }

    MoveAxes InputManager::AnalogMovement(bool applyServerLocks) const {
        if (modFreeze || controlled)
            return {};
        MoveAxes result;
        if (Device() == InputDevice::Gamepad) {
            const auto value = [&](Action action) {
                return ActionAllowed(action, applyServerLocks) ? PadMovementValue(action) : 0.0f;
            };
            result = {value(Action::Right) - value(Action::Left),
                      value(Action::Forward) - value(Action::Back)};
        } else if (Device() == InputDevice::Touch) {
            result = {input.TouchMoveX(), -input.TouchMoveY()};
            const auto allowed = [&](Action action) {
                const Binding& binding = options.GetKey(action);
                return ActionAllowed(action, applyServerLocks) &&
                       !(grabKeyboard && binding.device == Binding::Device::Keyboard) &&
                       !(grabMouse && binding.device == Binding::Device::Mouse);
            };
            if (!allowed(result.strafe < 0 ? Action::Left : Action::Right))
                result.strafe = 0;
            if (!allowed(result.forward < 0 ? Action::Back : Action::Forward))
                result.forward = 0;
        }
        const float length = std::hypot(result.strafe, result.forward);
        if (!std::isfinite(length))
            return {};
        if (length > 1) {
            result.strafe /= length;
            result.forward /= length;
        }
        return result;
    }

    MoveAxes InputManager::Movement(bool applyServerLocks) const {
        const MoveAxes analog = AnalogMovement(applyServerLocks);
        if (analog.strafe != 0 || analog.forward != 0)
            return analog;
        const auto value = [&](Action action) {
            return !modFreeze && IsDownImpl(action, applyServerLocks, false) ? 1.0f : 0.0f;
        };
        return {value(Action::Right) - value(Action::Left), value(Action::Forward) - value(Action::Back)};
    }
}
