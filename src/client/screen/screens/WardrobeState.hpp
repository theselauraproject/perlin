#pragma once
#include "client/systems/resources/SkinLoader.hpp"

#include <chrono>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::screen {
    struct WardrobeState {
        int line = 0;
        bool skinOpen = false, capeOpen = false, drag = false;
        float yaw = -25.0f, pitch = 0.0f, dragX = 0.0f, dragY = 0.0f;
        int previewSerial = 0;
        std::string previewKey, previewSource;
        std::vector<std::string> skins, capes;
        std::chrono::steady_clock::time_point listedAt;
        std::unordered_map<std::string, std::string> stamp;
        std::unordered_map<std::string, bool> slimGuess;
        std::string skin, cape;
        bool init = false;
        std::map<std::string, bool> slim;
        LoadedSkin previewLoaded;
    };
}
