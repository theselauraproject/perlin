#pragma once
#include <cmath>
#include <cstdint>

namespace perlin {
    class RenderContext;
}

namespace perlin::ui {
    uint32_t CreateGlintTexture(RenderContext& renderer, int* width = nullptr);

    struct GlintTransform {
        float matrix[4];
        float offset[2];
    };

    inline GlintTransform MakeGlintTransform(uint64_t millis, float scale = 0.5f) {
        const float a = -static_cast<float>(millis % 1750) / 1750.0f;
        const float b = static_cast<float>(millis % 3000) / 3000.0f;
        return {{a, b, scale, scale}, {0.0f, 0.0f}};
    }
}
