#pragma once
#include "perlin/rpc.hpp"

namespace perlin::input {
    enum class Button : int32_t { Left = 0, Right = 1, Middle = 2 };

    enum class Code : int32_t {
        Backspace = 0x08,
        Tab = 0x09,
        Enter = 0x0D,
        Shift = 0x10,
        Control = 0x11,
        Alt = 0x12,
        Escape = 0x1B,
        Space = 0x20,
        PageUp = 0x21,
        PageDown = 0x22,
        End = 0x23,
        Home = 0x24,
        Left = 0x25,
        Up = 0x26,
        Right = 0x27,
        Down = 0x28,
        Delete = 0x2E,
        A = 'A',
        C = 'C',
        V = 'V',
        X = 'X',
        Y = 'Y',
        Z = 'Z',
    };

    inline Result<void> grab(bool mouse, bool keyboard) {
        abi::Writer a;
        a.boolean(mouse).boolean(keyboard);
        return detail::call_void(Op::InputGrab, a);
    }
    inline Result<void> release() {
        return grab(false, false);
    }

    class Grab {
    public:
        explicit Grab(bool mouse = true, bool keyboard = true) { ok = grab(mouse, keyboard).has_value(); }
        Grab(const Grab&) = delete;
        Grab& operator=(const Grab&) = delete;
        ~Grab() {
            if (ok)
                (void)release();
        }
        explicit operator bool() const { return ok; }

    private:
        bool ok = false;
    };
}
