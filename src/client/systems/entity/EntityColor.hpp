#pragma once
#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>

namespace perlin::entity {
    inline constexpr float kDeg = 3.14159265358979323846f / 180.0f;

    inline uint32_t PackColor(const glm::vec3& c, float alpha = 1.0f) {
        auto q = [](float x) { return static_cast<uint32_t>(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return (q(alpha) << 24) | (q(c.b) << 16) | (q(c.g) << 8) | q(c.r);
    }

    inline uint32_t DyeColorRgb(int dye) {
        static constexpr uint32_t kDye[16] = {0xF9FFFE, 0xF9801D, 0xC74EBD, 0x3AB3DA, 0xFED83D, 0x80C71F,
                                              0xF38BAA, 0x474F52, 0x9D9D97, 0x169C9C, 0x8932B8, 0x3C44AA,
                                              0x835432, 0x5E7C16, 0xB02E26, 0x1D1D21};
        return (dye >= 0 && dye < 16) ? kDye[dye] : kDye[0];
    }
}
