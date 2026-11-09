#pragma once
#include "client/resources/TextureLoader.hpp"
#include "util/Resources.hpp"

#include <algorithm>
#include <string>
#include <vector>

// the Perlin logo shown beside Perlin users in nametags and the tab list
namespace perlin::ui {
    inline const Texture& PerlinBadgeSource() {
        static const Texture source = [] {
            Texture t;
            std::string png;
            if (!resources::Read("perlin_badge.png", png) ||
                !LoadTextureMemory(reinterpret_cast<const unsigned char*>(png.data()), png.size(), t))
                t = {};
            return t;
        }();
        return source;
    }

    // box filter with fractional coverage, alpha-weighted: no mipmaps on ui textures, so each size is
    // prefiltered here; plain sampling of the 256px logo at 16px turns the lines to noise
    inline Texture PerlinBadge(int size) {
        const Texture& src = PerlinBadgeSource();
        if (src.width <= 0 || size <= 0 || src.pixels.size() < static_cast<size_t>(src.width) * src.height * 4)
            return {};
        if (size >= src.width && src.width == src.height)
            return src;
        Texture out;
        out.width = out.height = size;
        out.pixels.assign(static_cast<size_t>(size) * size * 4, 0);
        const double sx = static_cast<double>(src.width) / size, sy = static_cast<double>(src.height) / size;
        for (int oy = 0; oy < size; oy++)
            for (int ox = 0; ox < size; ox++) {
                const double x0 = ox * sx, x1 = x0 + sx, y0 = oy * sy, y1 = y0 + sy;
                double acc[4] = {}, area = 0.0;
                for (int y = static_cast<int>(y0); y < std::min(src.height, static_cast<int>(y1 + 0.999)); y++) {
                    const double wy = std::min<double>(y + 1, y1) - std::max<double>(y, y0);
                    if (wy <= 0.0)
                        continue;
                    for (int x = static_cast<int>(x0); x < std::min(src.width, static_cast<int>(x1 + 0.999));
                         x++) {
                        const double w = wy * (std::min<double>(x + 1, x1) - std::max<double>(x, x0));
                        if (w <= 0.0)
                            continue;
                        const unsigned char* p = &src.pixels[(static_cast<size_t>(y) * src.width + x) * 4];
                        const double a = p[3] / 255.0 * w;
                        acc[0] += p[0] * a;
                        acc[1] += p[1] * a;
                        acc[2] += p[2] * a;
                        acc[3] += a;
                        area += w;
                    }
                }
                unsigned char* o = &out.pixels[(static_cast<size_t>(oy) * size + ox) * 4];
                if (acc[3] > 0.0)
                    for (int c = 0; c < 3; c++)
                        o[c] = static_cast<unsigned char>(std::clamp(acc[c] / acc[3] + 0.5, 0.0, 255.0));
                o[3] = static_cast<unsigned char>(std::clamp(acc[3] / std::max(area, 1e-9) * 255.0 + 0.5, 0.0, 255.0));
            }
        return out;
    }
}
