#pragma once
#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

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

    // Undyed leather armour (worn and as an item) is this brown.
    inline constexpr uint32_t kLeatherColor = 0xA06540;

    // Bakes a colour-mask texture (leather armour and its item icons): opaque pixels are the dyed part and
    // are multiplied by `rgb` (0xRRGGBB); barely visible ones (alpha 1 to 127) are the undyed overlay and
    // are made opaque unchanged.
    inline void BakeColorMask(std::vector<uint8_t>& rgba, uint32_t rgb) {
        const uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
        for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
            uint8_t* p = &rgba[i];
            if (p[3] == 0)
                continue;
            if (p[3] >= 128) {
                p[0] = static_cast<uint8_t>(p[0] * r / 255);
                p[1] = static_cast<uint8_t>(p[1] * g / 255);
                p[2] = static_cast<uint8_t>(p[2] * b / 255);
            }
            p[3] = 255;
        }
    }
}
