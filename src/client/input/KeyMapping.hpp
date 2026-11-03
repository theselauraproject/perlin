#pragma once
#include "client/input/Keys.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace perlin {
    enum class Action : uint8_t {
        Forward,
        Back,
        Left,
        Right,
        Jump,
        Sneak,
        Look,
        CyclePerspective,
        Screenshot,
        Sprint,
        Zoom,
        Attack,
        Use,
        Hotbar1,
        Hotbar2,
        Hotbar3,
        Hotbar4,
        Hotbar5,
        Hotbar6,
        Hotbar7,
        Hotbar8,
        Hotbar9,
        ToggleHud,
        Chat,
        Command,
        DebugScreen,
        PickBlock,
        Drop,
        PlayerList,
        Inventory,
        Pause,
        Fullscreen,
        SwapOffhand,
        Count
    };

    constexpr size_t kActionCount = static_cast<size_t>(Action::Count);

    struct Binding {
        enum class Device : uint8_t { None, Keyboard, Mouse, Gamepad };
        Device device = Device::None;
        int code = 0;

        static Binding KeyboardKey(int code) { return {Device::Keyboard, code}; }
        static Binding MouseButton(int code) { return {Device::Mouse, code}; }
        static Binding PadButton(int code) { return {Device::Gamepad, code}; }

        bool operator==(const Binding& o) const { return device == o.device && code == o.code; }
        std::string Serialize() const;
        static Binding Parse(const std::string& text);
    };

    using KeyBindings = std::array<Binding, kActionCount>;

    namespace Pad {
        constexpr int A = 0, B = 1, X = 2, Y = 3, Back = 4, Guide = 5, Start = 6, LeftStick = 7,
                      RightStick = 8, LeftShoulder = 9, RightShoulder = 10, DpadUp = 11, DpadDown = 12,
                      DpadLeft = 13, DpadRight = 14, LeftTrigger = 20, RightTrigger = 21, LeftStickUp = 30,
                      LeftStickDown = 31, LeftStickLeft = 32, LeftStickRight = 33, RightStickUp = 34,
                      RightStickDown = 35, RightStickLeft = 36, RightStickRight = 37;
        constexpr int kCount = 64;
        const char* Name(int code);
        int FromName(const std::string& name);
    }

    const char* ActionId(Action action);
    KeyBindings DefaultBindings();
    KeyBindings DefaultPadBindings();
}
