#pragma once

#include <algorithm>
#include <cmath>
#include <algorithm>
namespace perlin::touchlayout {
    struct ToolBox {
        float x, y, w, h;
        const char* label;
    };

    constexpr int kToolCount = 5;

    inline void ToolBoxes(int width, int height, ToolBox out[kToolCount], bool hiddenSelected = false) {
        const float u = static_cast<float>(std::min(width, height));
        const float k = std::max(1.0f, std::round(0.075f * u / 20.0f));
        const float h = 20.0f * k, gap = 4.0f * k;
        const float widths[kToolCount] = {20.0f * k, 20.0f * k, 48.0f * k, 48.0f * k, 48.0f * k};
        float total = (kToolCount - 1) * gap;
        for (float w : widths)
            total += w;
        float x = std::round(static_cast<float>(width) * 0.5f - total * 0.5f);
        const float y = std::round(static_cast<float>(height) * 0.5f - h * 0.5f);
        const char* labels[kToolCount] = {"-", "+", hiddenSelected ? "Show" : "Hide", "Reset", "Done"};
        for (int i = 0; i < kToolCount; i++) {
            out[i] = {x, y, widths[i], h, labels[i]};
            x += widths[i] + gap;
        }
    }

}
