#pragma once
#include <algorithm>

namespace perlin::ui {
    struct Scrollbar {
        float x = 0, top = 0, w = 6, h = 0, thumbY = 0, thumbH = 0, maxScroll = 0;

        bool Contains(float gx, float gy) const {
            return maxScroll > 0.0f && gx >= x && gx < x + w && gy >= top && gy < top + h;
        }
        bool OnThumb(float gy) const { return gy >= thumbY && gy < thumbY + thumbH; }
        // Where in the thumb a press holds it: pressing the track grabs the thumb's middle, so the
        // thumb jumps to the pointer and keeps following it.
        float Grab(float gy) const { return OnThumb(gy) ? gy - thumbY : thumbH / 2.0f; }
        float ScrollFor(float gy, float grab) const {
            const float travel = h - thumbH;
            if (travel <= 0.0f || maxScroll <= 0.0f)
                return 0.0f;
            return std::clamp((gy - grab - top) / travel, 0.0f, 1.0f) * maxScroll;
        }
    };

    inline Scrollbar MakeScrollbar(float x, float top, float h, float contentH, float scroll,
                                   float maxScroll) {
        Scrollbar b;
        b.x = x;
        b.top = top;
        b.h = h;
        b.maxScroll = std::max(0.0f, maxScroll);
        b.thumbH = contentH > 0.0f ? std::min(h, std::max(16.0f, h * h / contentH)) : h;
        const float at = b.maxScroll > 0.0f ? std::clamp(scroll, 0.0f, b.maxScroll) / b.maxScroll : 0.0f;
        b.thumbY = top + (h - b.thumbH) * at;
        return b;
    }
}
