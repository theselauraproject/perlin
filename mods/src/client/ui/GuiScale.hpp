#pragma once

#include <algorithm>

namespace perlin::ui {
    inline int JavaGuiScale(int framebufferW, int framebufferH, int option) {
        int scale = 1;
        while ((option <= 0 || scale != option) && framebufferW / (scale + 1) >= 320 &&
               framebufferH / (scale + 1) >= 240)
            scale++;
        return std::max(1, scale);
    }
}
