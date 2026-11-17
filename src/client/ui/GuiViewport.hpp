#pragma once
#include "client/ui/Hud.hpp"

#include <cmath>

namespace perlin::ui {
    struct GuiViewport {
        int scale = 1;
        int w = 0, h = 0;
        float ToGui(float px) const { return px / static_cast<float>(scale); }
    };

    inline GuiViewport OverlayViewport(const Hud& hud, int fbW, int fbH) {
        GuiViewport v;
        v.scale = hud.OverlayScale();
        v.w = static_cast<int>(std::floor(static_cast<float>(fbW) / static_cast<float>(v.scale)));
        v.h = static_cast<int>(std::floor(static_cast<float>(fbH) / static_cast<float>(v.scale)));
        return v;
    }
}
