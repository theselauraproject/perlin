#pragma once
#include "client/screen/OverlayScreen.hpp"
#include <chrono>
#include <string>
#include <vector>

namespace perlin::screen {
    struct AssetDownloadScreen : OverlayScreen {
        using OverlayScreen::OverlayScreen;
        bool skip = false;
        std::chrono::steady_clock::time_point sampleAt;
        uint64_t lastBytes[2] = {0, 0};
        double speed[2] = {0.0, 0.0};
    };
}
