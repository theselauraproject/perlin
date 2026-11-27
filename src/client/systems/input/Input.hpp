#pragma once
#include "client/systems/bindings/Keys.hpp"
#include "client/systems/input/InputDevice.hpp"

#include <atomic>
#include <cstdint>
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
        void NoteDevice(InputDevice device) { activeDevice.store(device, std::memory_order_relaxed); }
        InputDevice ActiveDevice() const { return activeDevice.load(std::memory_order_relaxed); }
        void SetPadMove(float x, float y) {
            padMoveX = x;
            padMoveY = y;
        }
        float PadMoveX() const { return padMoveX; }
        float PadMoveY() const { return padMoveY; }
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

        void SetTouchAim(float nx, float ny, int64_t untilMs) {
            touchAimX.store(nx, std::memory_order_relaxed);
            touchAimY.store(ny, std::memory_order_relaxed);
            touchAimUntil.store(untilMs, std::memory_order_relaxed);
        }
        void EndTouchAim(int64_t untilMs) { touchAimUntil.store(untilMs, std::memory_order_relaxed); }
        bool TouchAim(int64_t nowMs, float& nx, float& ny) const {
            if (nowMs >= touchAimUntil.load(std::memory_order_relaxed))
                return false;
            nx = touchAimX.load(std::memory_order_relaxed);
            ny = touchAimY.load(std::memory_order_relaxed);
            return true;
        }
        void SetTouchHold(bool on) { touchHold.store(on, std::memory_order_relaxed); }
        bool TouchHold() const { return touchHold.load(std::memory_order_relaxed); }
        void AddTouchTap() { touchTaps.fetch_add(1, std::memory_order_relaxed); }
        int TakeTouchTaps() { return touchTaps.exchange(0, std::memory_order_relaxed); }
        void SetTouchInteract(bool on) { touchInteract.store(on, std::memory_order_relaxed); }
        bool TouchInteract() const { return touchInteract.load(std::memory_order_relaxed); }
        void SetTouchMove(float x, float y) {
            touchMoveX = x;
            touchMoveY = y;
        }
        float TouchMoveX() const { return touchMoveX; }
        float TouchMoveY() const { return touchMoveY; }

        moodycamel::ConcurrentQueue<InputEvent> events;

    private:
        std::atomic<bool> keys[256]{};
        std::atomic<bool> buttons[8]{};
        std::atomic<bool> pad[64]{};
        std::atomic<InputDevice> activeDevice{InputDevice::Mouse};
        std::atomic<float> padMoveX{0}, padMoveY{0};
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
        std::atomic<float> touchAimX{0.5f}, touchAimY{0.5f};
        std::atomic<int64_t> touchAimUntil{0};
        std::atomic<bool> touchHold{false};
        std::atomic<int> touchTaps{0};
        std::atomic<bool> touchInteract{false};
        std::atomic<float> touchMoveX{0}, touchMoveY{0};
    };
}
