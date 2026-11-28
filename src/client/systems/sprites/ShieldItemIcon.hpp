#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace perlin::ui {
    // Software preview of the shield's textured plate, rather than a crop of
    // the atlas. The shape uses the Java item GUI display rotation (15,-25,-5)
    // as its reference. The supplied atlas is still Perlin's Bedrock resource.
    inline std::vector<uint8_t> BuildShieldItemIcon(const std::vector<uint8_t>& texture,
                                                    int textureWidth, int textureHeight) {
        constexpr int side = 32;
        std::vector<uint8_t> icon(side * side * 4, 0);
        if (textureWidth < 64 || textureHeight < 64 ||
            texture.size() < static_cast<size_t>(textureWidth) * textureHeight * 4)
            return icon;
        struct Point { float x, y; };
        // Shield front is 12x22 model pixels; projecting the GUI rotation makes
        // the right-hand edge visible instead of crushing the item into a line.
        const auto project = [](float x, float y, float z) -> Point {
            constexpr float ry = -0.436332313f, rx = 0.261799388f, rz = -0.087266463f;
            const float xx = x * std::cos(ry) + z * std::sin(ry);
            const float zz = -x * std::sin(ry) + z * std::cos(ry);
            const float yy = y * std::cos(rx) - zz * std::sin(rx);
            const float xxx = xx * std::cos(rz) - yy * std::sin(rz);
            const float yyy = xx * std::sin(rz) + yy * std::cos(rz);
            return {16.5f + xxx * 0.94f, 16.0f - yyy * 1.04f};
        };
        const auto sample = [&](float u, float v, float shade, int x, int y) {
            if (x < 0 || x >= side || y < 0 || y >= side) return;
            const int sx = std::clamp(static_cast<int>(u * textureWidth / 64.0f), 0, textureWidth - 1);
            const int sy = std::clamp(static_cast<int>(v * textureHeight / 64.0f), 0, textureHeight - 1);
            const size_t src = (static_cast<size_t>(sy) * textureWidth + sx) * 4;
            const size_t dst = (static_cast<size_t>(y) * side + x) * 4;
            if (texture[src + 3] == 0) return;
            for (int c = 0; c < 3; ++c)
                icon[dst + c] = static_cast<uint8_t>(std::clamp(texture[src + c] * shade, 0.0f, 255.0f));
            icon[dst + 3] = texture[src + 3];
        };
        const auto face = [&](Point a, Point b, Point c, Point d, float u0, float v0,
                              float du, float dv, float shade) {
            const float minX = std::min({a.x,b.x,c.x,d.x}), maxX = std::max({a.x,b.x,c.x,d.x});
            const float minY = std::min({a.y,b.y,c.y,d.y}), maxY = std::max({a.y,b.y,c.y,d.y});
            for (int y = std::max(0, static_cast<int>(std::floor(minY))); y < std::min(side, static_cast<int>(std::ceil(maxY))); ++y)
                for (int x = std::max(0, static_cast<int>(std::floor(minX))); x < std::min(side, static_cast<int>(std::ceil(maxX))); ++x) {
                    // Invert the affine rectangle transformation (the two
                    // projected edges correspond to the texture U/V axes).
                    const float dx = x + 0.5f - a.x, dy = y + 0.5f - a.y;
                    const float ux = b.x - a.x, uy = b.y - a.y;
                    const float vx = d.x - a.x, vy = d.y - a.y;
                    const float det = ux * vy - uy * vx;
                    if (std::abs(det) < 1e-6f) continue;
                    const float u = (dx * vy - dy * vx) / det;
                    const float v = (ux * dy - uy * dx) / det;
                    if (u >= 0 && u <= 1 && v >= 0 && v <= 1)
                        sample(u0 + u * du, v0 + v * dv, shade, x, y);
                }
        };
        // Side before front, using the same 12x22x1 plate proportions as the
        // first-person shield model. The short handle is mostly hidden in GUI.
        face(project(-6,11,-2), project(-6,11,0), project(-6,-11,0), project(-6,-11,-2),
             0, 1, 2, 22, 0.60f);
        face(project(6,11,0), project(6,11,-2), project(6,-11,-2), project(6,-11,0),
             13, 1, 2, 22, 0.70f);
        face(project(-6,11,0), project(6,11,0), project(6,-11,0), project(-6,-11,0),
             14, 1, 12, 22, 0.95f);
        return icon;
    }
}
