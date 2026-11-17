#pragma once
#include <cmath>

namespace perlin::math {
    inline float WrapDegrees(float angle) noexcept {
        const float m = std::fmod(angle + 180.0f, 360.0f);
        return (m < 0.0f ? m + 360.0f : m) - 180.0f;
    }
}
