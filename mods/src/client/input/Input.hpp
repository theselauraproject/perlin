#pragma once
#include "client/input/Keys.hpp"

#include <atomic>
#include <concurrentqueue.h>

namespace perlin {
    struct InputEvent {
        enum Type { KeyDown, KeyUp, MouseMove, MouseDown, MouseUp, KeyRepeat, Char, PadDown, PadUp };
        Type type;
        int key;
        float x, y;
    };

    inline void AtomicAdd(std::atomic<float>& target, float value) {
        float expected = target.load(std::memory_order_relaxed);
        while (!target.compare_exchange_weak(expected, expected + value, std::memory_order_relaxed)) {}
    }

    class Input {
    public:
        void OnKeyDown(int key);
        void OnKeyUp(int key);
        void OnMouseMove(float x, float y);
        void OnMouseDown(int button);
        void OnMouseUp(int button);
        void OnMouseDelta(float dx, float dy) {
            AtomicAdd(lookX, dx);
            AtomicAdd(lookY, dy);
        }
        void TakeMouseDelta(float& dx, float& dy) {
            dx = lookX.exchange(0.0f, std::memory_order_relaxed);
            dy = lookY.exchange(0.0f, std::memory_order_relaxed);
        }
        void OnPadDown(int code);
        void OnPadUp(int code);
        bool IsPadDown(int code) const { return code >= 0 && code < 64 && pad[code]; }
        void SetPadLook(float x, float y) {
            padLookX = x;
            padLookY = y;
        }
        float PadLookX() const { return padLookX; }
        float PadLookY() const { return padLookY; }
        void SetPadCapture(bool on) { padCapture = on; }
        bool PadCapture() const { return padCapture; }
        void SetPadDeadzone(float v) { padDeadzone = v; }
        float PadDeadzone() const { return padDeadzone; }
        bool PadConnected() const { return padConnected; }
        void SetPadConnected(bool on) { padConnected = on; }
        void OnMouseWheel(float dy) { AtomicAdd(wheel, dy); }
        float TakeWheel() { return wheel.exchange(0.0f, std::memory_order_relaxed); }
        void SetCaptured(bool c) { captured = c; }
        bool Captured() const { return captured; }
        bool pauseOnLostFocus = true;
        void OnKeyRepeat(int key);
        void OnChar(int codepoint);
        void SetTextMode(bool on) { textMode = on; }
        void SetFreeCursor(bool on) { freeCursor = on; }
        // a gui just opened: the platform moves the freed pointer to the window centre once, after it releases the grab
        void RequestCursorCenter() { centerRequested = true; }
        bool TakeCursorCenter() { return centerRequested.exchange(false); }
        bool FreeCursor() const { return freeCursor; }
        bool TextMode() const { return textMode; }
        void SetKeyboard(bool on) { keyboard = on; }
        bool Keyboard() const { return keyboard; }
        void ReleaseAll();

        bool IsKeyDown(int key) const;
        bool IsMouseDown(int button) const;
        float MouseX() const { return mouseX.load(std::memory_order_relaxed); }
        float MouseY() const { return mouseY.load(std::memory_order_relaxed); }

        moodycamel::ConcurrentQueue<InputEvent> events;

    private:
        std::atomic<bool> keys[256]{};
        std::atomic<bool> buttons[8]{};
        std::atomic<bool> pad[64]{};
        std::atomic<float> padLookX{0}, padLookY{0};
        std::atomic<bool> padConnected{false};
        std::atomic<bool> padCapture{false};
        std::atomic<float> padDeadzone{0.15f};
        std::atomic<float> mouseX{0}, mouseY{0};
        std::atomic<float> lookX{0}, lookY{0};
        std::atomic<float> wheel{0};
        std::atomic<bool> captured{false};
        std::atomic<bool> freeCursor{false};
        std::atomic<bool> centerRequested{false};
        std::atomic<bool> textMode{false};
        std::atomic<bool> keyboard{false};
    };
}
