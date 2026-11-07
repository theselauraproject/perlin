#include "client/input/Input.hpp"

namespace perlin {
    void Input::OnKeyDown(int key) {
        keys[key & 0xFF] = true;
        events.enqueue({InputEvent::KeyDown, key, 0, 0});
    }

    void Input::OnKeyUp(int key) {
        keys[key & 0xFF] = false;
        events.enqueue({InputEvent::KeyUp, key, 0, 0});
    }

    void Input::OnKeyRepeat(int key) {
        events.enqueue({InputEvent::KeyRepeat, key, 0, 0});
    }

    void Input::OnChar(int codepoint) {
        events.enqueue({InputEvent::Char, codepoint, 0, 0});
    }

    void Input::OnMouseMove(float x, float y) {
        mouseX = x;
        mouseY = y;
        events.enqueue({InputEvent::MouseMove, 0, x, y});
    }

    void Input::OnMouseDown(int button) {
        buttons[button & 7] = true;
        events.enqueue({InputEvent::MouseDown, button, mouseX, mouseY});
    }

    void Input::OnMouseUp(int button) {
        buttons[button & 7] = false;
        events.enqueue({InputEvent::MouseUp, button, mouseX, mouseY});
    }

    void Input::OnPadDown(int code) {
        if (code < 0 || code >= 64)
            return;
        pad[code] = true;
        events.enqueue({InputEvent::PadDown, code, 0, 0});
    }

    void Input::OnPadUp(int code) {
        if (code < 0 || code >= 64)
            return;
        pad[code] = false;
        events.enqueue({InputEvent::PadUp, code, 0, 0});
    }

    void Input::ReleaseAll() {
        for (auto& p : pad)
            p = false;
        for (auto& k : keys)
            k = false;
        for (auto& b : buttons)
            b = false;
    }

    bool Input::IsKeyDown(int key) const {
        return keys[key & 0xFF];
    }
    bool Input::IsMouseDown(int button) const {
        return buttons[button & 7];
    }
}
