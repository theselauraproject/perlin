#pragma once
#include "client/screen/ScreenManager.hpp"

#include <functional>
#include <string>
#include <vector>

namespace perlin {
    class Input;
    class InputManager;
    struct Options;
    namespace ui {
        class ChatLog;
        struct HudState;
    }
}

namespace perlin::screen {
    struct Host {
        Manager& screens;
        Input& input;
        InputManager& inputManager;
        const Options& options;
        std::function<void(bool)> captureCursor;
        std::function<std::vector<std::string>()> serverPackDirectories;
        const ui::ChatLog& chat;
        const ui::HudState& hudState;
        std::function<bool(int slot, bool right, bool shift)> dispatchSlotClick;
        bool& advancedTooltips;
    };
}
