#pragma once
// the pause screen and the menus it opens, shared by PerlinPause/Options/PackMenu/Wardrobe.cpp
#include "client/PerlinInternal.hpp"

namespace perlin {
    // one frame of the pause screen: where it draws and what the pointer did
    struct PauseFrame {
        ui::Hud& hud;
        BedrockSession* live;
        int fbW, fbH;
        std::chrono::steady_clock::time_point now;
        bool preview;
        int scale, guiW, guiH;
        float mx, my;
        bool down, clickReady;
    };
}
