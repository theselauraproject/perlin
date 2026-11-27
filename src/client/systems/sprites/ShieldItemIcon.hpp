#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace perlin::ui {
    // Produces a 32x32 inventory preview from the Bedrock shield entity atlas.
    // Keeping this in a pure function makes the preview independent of the GPU
    // backend and avoids re-interpreting the entire atlas as a single sprite.
    inline std::vector<uint8_t> BuildShieldItemIcon(const std::vector<uint8_t>& texture,
                                                     int textureWidth, int textureHeight) {
        constexpr int side = 32;
        std::vector<uint8_t> icon(side * side * 4, 0);
        if (textureWidth < 64 || textureHeight < 64 ||
            texture.size() < static_cast<size_t>(textureWidth) * textureHeight * 4)
            return icon;
        const auto sample = [&](float u, float v, float shade, int x, int y) {
            const int sx = std::clamp(static_cast<int>(u * textureWidth / 64.0f), 0, textureWidth - 1);
            const int sy = std::clamp(static_cast<int>(v * textureHeight / 64.0f), 0, textureHeight - 1);
            const size_t src = (static_cast<size_t>(sy) * textureWidth + sx) * 4;
            const size_t dst = (static_cast<size_t>(y) * side + x) * 4;
            if (texture[src + 3] == 0)
                return;
            for (int c = 0; c < 3; ++c)
                icon[dst + c] = static_cast<uint8_t>(std::clamp(texture[src + c] * shade, 0.0f, 255.0f));
            icon[dst + 3] = texture[src + 3];
        };
        // Render the thin dark rear edge before the textured front face.
        for (int y = 3; y < 29; ++y) {
            const float v = (y - 3.0f) / 26.0f;
            const int edgeX = 11 + static_cast<int>(v * 2.0f);
            for (int x = edgeX; x < edgeX + 4; ++x)
                sample(1.0f + (x - edgeX) * 3.0f, 1.0f + v * 22.0f, 0.70f, x, y);
        }
        // Tapered plate: the source UVs are from the shield plate, not the atlas bounds.
        for (int y = 2; y < 29; ++y) {
            const float v = (y - 2.0f) / 27.0f;
            const int left = 13 + static_cast<int>(v * 2.0f);
            const int width = y >= 23 ? std::max(2, 7 - (y - 23)) : 7;
            for (int x = left; x < left + width; ++x) {
                const float u = (x - left + 0.5f) / static_cast<float>(width);
                sample(1.0f + u * 12.0f, 1.0f + v * 22.0f, 0.94f, x, y);
            }
        }
        return icon;
    }
}
