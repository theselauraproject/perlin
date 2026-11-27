#pragma once
#include <algorithm>
#include <cmath>

namespace perlin::screen::creative_scroll {
inline float Wheel(float current, float delta, int totalRows, int visibleRows = 5) {
    const int maxRow = std::max(0, totalRows - visibleRows);
    if (maxRow == 0) return 0.0f;
    if (!std::isfinite(delta)) return std::clamp(current, 0.0f, 1.0f);
    // One wheel notch moves one complete row. Preserve fractional wheel deltas.
    return std::clamp(current - delta / static_cast<float>(maxRow), 0.0f, 1.0f);
}
inline float Track(float cursorY, float trackTop, float travel) {
    return travel > 0.0f ? std::clamp((cursorY - trackTop) / travel, 0.0f, 1.0f) : 0.0f;
}
inline int FirstRow(float scroll, int totalRows, int visibleRows = 5) {
    return static_cast<int>(std::lround(std::clamp(scroll, 0.0f, 1.0f) *
                                        static_cast<float>(std::max(0, totalRows - visibleRows))));
}
}
