#pragma once
#include <algorithm>
namespace perlin::touchlayout {
    struct ToolBox {
        float x, y, w, h;
        const char* label;
    };

    inline void ToolBoxes(int width, int height, ToolBox out[4]) {
        const float u = static_cast<float>(std::min(width, height));
        const float w = 0.15f * u, h = 0.075f * u, gap = 0.02f * u;
        const float total = 4 * w + 3 * gap;
        const float x0 = static_cast<float>(width) * 0.5f - total * 0.5f;
        const float y = static_cast<float>(height) * 0.5f - h * 0.5f;
        const char* labels[4] = {"-", "+", "Reset", "Done"};
        for (int i = 0; i < 4; i++)
            out[i] = {x0 + static_cast<float>(i) * (w + gap), y, w, h, labels[i]};
    }

}
