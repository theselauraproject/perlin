#include "client/input/InputManager.hpp"

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
                        if (cp < 0x80) {
                            p.text += static_cast<char>(cp);
                        } else if (cp < 0x800) {
                            p.text += static_cast<char>(0xC0 | (cp >> 6));
                            p.text += static_cast<char>(0x80 | (cp & 0x3F));
                        } else if (cp < 0x10000) {
                            p.text += static_cast<char>(0xE0 | (cp >> 12));
                            p.text += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            p.text += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            p.text += static_cast<char>(0xF0 | (cp >> 18));
                            p.text += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                            p.text += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            p.text += static_cast<char>(0x80 | (cp & 0x3F));
                        }
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
        if (blocked && action != Action::DebugScreen)
            return false;
        if (input.TextMode())
            return false;
        if (locks) {
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
        if (controlled)
            return controlTick < controlUntil && controlActions.test(static_cast<size_t>(action));
        if (holds[static_cast<size_t>(action)] > std::chrono::steady_clock::now())
            return true;
        const Binding& pb = options.GetPadKey(action);
        if (pb.device == Binding::Device::Gamepad && input.IsPadDown(pb.code))
            return true;
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
        return (IsDown(positive) ? 1.0f : 0.0f) - (IsDown(negative) ? 1.0f : 0.0f);
    }
}
