#include "client/input/KeyMapping.hpp"

namespace perlin {
    std::string Binding::Serialize() const {
        switch (device) {
        case Device::Keyboard:
            return KeyName(code);
        case Device::Mouse:
            return MouseName(code);
        case Device::Gamepad:
            return std::string("pad.") + Pad::Name(code);
        default:
            return {};
        }
    }

    namespace Pad {
        namespace {
            struct Entry {
                int code;
                const char* name;
            };
            constexpr Entry kEntries[] = {{A, "a"},
                                          {B, "b"},
                                          {X, "x"},
                                          {Y, "y"},
                                          {Back, "back"},
                                          {Guide, "guide"},
                                          {Start, "start"},
                                          {LeftStick, "left_stick_click"},
                                          {RightStick, "right_stick_click"},
                                          {LeftShoulder, "left_bumper"},
                                          {RightShoulder, "right_bumper"},
                                          {DpadUp, "dpad_up"},
                                          {DpadDown, "dpad_down"},
                                          {DpadLeft, "dpad_left"},
                                          {DpadRight, "dpad_right"},
                                          {LeftTrigger, "left_trigger"},
                                          {RightTrigger, "right_trigger"},
                                          {LeftStickUp, "left_stick_up"},
                                          {LeftStickDown, "left_stick_down"},
                                          {LeftStickLeft, "left_stick_left"},
                                          {LeftStickRight, "left_stick_right"},
                                          {RightStickUp, "right_stick_up"},
                                          {RightStickDown, "right_stick_down"},
                                          {RightStickLeft, "right_stick_left"},
                                          {RightStickRight, "right_stick_right"}};
        }
        const char* Name(int code) {
            for (const Entry& e : kEntries)
                if (e.code == code)
                    return e.name;
            return "unknown";
        }
        int FromName(const std::string& name) {
            for (const Entry& e : kEntries)
                if (name == e.name)
                    return e.code;
            return -1;
        }
    }

    Binding Binding::Parse(const std::string& text) {
        if (text.starts_with("pad.")) {
            const int code = Pad::FromName(text.substr(4));
            return code >= 0 ? PadButton(code) : Binding{};
        }
        if (int key = KeyFromName(text))
            return KeyboardKey(key);
        if (int button = MouseFromName(text); button >= 0)
            return MouseButton(button);
        return {};
    }

    const char* ActionId(Action action) {
        switch (action) {
        case Action::Forward:
            return "key.forward";
        case Action::Back:
            return "key.back";
        case Action::Left:
            return "key.left";
        case Action::Right:
            return "key.right";
        case Action::Jump:
            return "key.jump";
        case Action::Sneak:
            return "key.sneak";
        case Action::Look:
            return "key.look";
        case Action::CyclePerspective:
            return "key.togglePerspective";
        case Action::Screenshot:
            return "key.screenshot";
        case Action::Sprint:
            return "key.sprint";
        case Action::Zoom:
            return "key.zoom";
        case Action::Attack:
            return "key.attack";
        case Action::Use:
            return "key.use";
        case Action::Hotbar1:
            return "key.hotbar.1";
        case Action::Hotbar2:
            return "key.hotbar.2";
        case Action::Hotbar3:
            return "key.hotbar.3";
        case Action::Hotbar4:
            return "key.hotbar.4";
        case Action::Hotbar5:
            return "key.hotbar.5";
        case Action::Hotbar6:
            return "key.hotbar.6";
        case Action::Hotbar7:
            return "key.hotbar.7";
        case Action::Hotbar8:
            return "key.hotbar.8";
        case Action::Hotbar9:
            return "key.hotbar.9";
        case Action::ToggleHud:
            return "key.toggleHud";
        case Action::Chat:
            return "key.chat";
        case Action::Command:
            return "key.command";
        case Action::DebugScreen:
            return "key.debugScreen";
        case Action::PickBlock:
            return "key.pickItem";
        case Action::Drop:
            return "key.drop";
        case Action::Inventory:
            return "key.inventory";
        case Action::Pause:
            return "key.pause";
        case Action::PlayerList:
            return "key.playerlist";
        case Action::Fullscreen:
            return "key.fullscreen";
        case Action::SwapOffhand:
            return "key.swapOffhand";
        default:
            return "";
        }
    }

    KeyBindings DefaultBindings() {
        KeyBindings b{};
        auto set = [&](Action a, Binding v) { b[static_cast<size_t>(a)] = v; };
        set(Action::Forward, Binding::KeyboardKey('W'));
        set(Action::Back, Binding::KeyboardKey('S'));
        set(Action::Left, Binding::KeyboardKey('A'));
        set(Action::Right, Binding::KeyboardKey('D'));
        set(Action::Jump, Binding::KeyboardKey(Key::Space));
        set(Action::Sneak, Binding::KeyboardKey(Key::Shift));
        set(Action::Look, Binding::MouseButton(Mouse::Left));
        set(Action::CyclePerspective, Binding::KeyboardKey(Key::F1 + 4));
        set(Action::Screenshot, Binding::KeyboardKey(Key::F12));
        set(Action::Sprint, Binding::KeyboardKey(Key::Control));
        set(Action::Zoom, Binding::KeyboardKey('C'));
        set(Action::Attack, Binding::MouseButton(Mouse::Left));
        set(Action::Use, Binding::MouseButton(Mouse::Right));
        for (int i = 0; i < 9; i++)
            set(static_cast<Action>(static_cast<int>(Action::Hotbar1) + i), Binding::KeyboardKey('1' + i));
        set(Action::ToggleHud, Binding::KeyboardKey(Key::F1));
        set(Action::Chat, Binding::KeyboardKey('T'));
        set(Action::Command, Binding::KeyboardKey(Key::Slash));
        set(Action::DebugScreen, Binding::KeyboardKey(Key::F1 + 2));
        set(Action::PickBlock, Binding::MouseButton(Mouse::Middle));
        set(Action::Drop, Binding::KeyboardKey('Q'));
        set(Action::Inventory, Binding::KeyboardKey('E'));
        set(Action::Pause, Binding::KeyboardKey(Key::Escape));
        set(Action::PlayerList, Binding::KeyboardKey(Key::Tab));
        set(Action::Fullscreen, Binding::KeyboardKey(Key::F1 + 10));
        set(Action::SwapOffhand, Binding::KeyboardKey('F'));
        return b;
    }

    KeyBindings DefaultPadBindings() {
        KeyBindings b{};
        auto set = [&](Action a, int code) { b[static_cast<size_t>(a)] = Binding::PadButton(code); };
        set(Action::Forward, Pad::LeftStickUp);
        set(Action::Back, Pad::LeftStickDown);
        set(Action::Left, Pad::LeftStickLeft);
        set(Action::Right, Pad::LeftStickRight);
        set(Action::Jump, Pad::A);
        set(Action::Sneak, Pad::B);
        set(Action::Sprint, Pad::LeftStick);
        set(Action::Attack, Pad::RightTrigger);
        set(Action::Use, Pad::LeftTrigger);
        set(Action::Inventory, Pad::Y);
        set(Action::Drop, Pad::X);
        set(Action::Chat, Pad::DpadRight);
        set(Action::Command, Pad::DpadLeft);
        set(Action::CyclePerspective, Pad::DpadUp);
        set(Action::PlayerList, Pad::Back);
        set(Action::Pause, Pad::Start);
        set(Action::PickBlock, Pad::RightStick);
        set(Action::SwapOffhand, Pad::DpadDown);
        return b;
    }
}
