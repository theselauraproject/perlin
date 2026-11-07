#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace perlin::ui {
    struct ServerLayerData {
        struct Bar {
            std::string title;
            float progress = 1.0f;
            int color = 0;
            int overlay = 0;
        };
        std::vector<Bar> bars;
        bool scoreboard = false;
        std::string scoreboardTitle;
        std::vector<std::pair<std::string, int>> rows;
        std::string toastTitle, toastMessage;
        bool toast = false;
        bool Empty() const { return bars.empty() && !scoreboard && !toast; }
    };

    std::string BuildServerLayerRml(const ServerLayerData& data, int guiW, int guiH);
    std::string ServerLayerSignature(const ServerLayerData& data, int guiW, int guiH);
}
