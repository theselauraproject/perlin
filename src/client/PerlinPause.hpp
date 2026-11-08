#pragma once

#include "client/PerlinInternal.hpp"

namespace perlin {

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
