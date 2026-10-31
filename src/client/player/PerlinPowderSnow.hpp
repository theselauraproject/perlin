#pragma once

#include <cmath>

namespace perlin::physics::bds {
    inline float PowderSnowSupportHeight(float feetY, float blockTop, bool fallThrough,
                                         float fallDistance, bool canStand) {
        if (feetY < blockTop - 0.00000011920929f || fallThrough)
            return 0;
        if (!(fallDistance <= 2.5f))
            return 0.9f;
        return canStand ? 1.0f : 0.0f;
    }

    // FreezingSystem 0x145E14260: +1/140 (0x3BEA0EA1) up to 1, else -2/140 (0xBC6A0EA1) down to 0
    inline float FreezeStep(float strength, bool rising) {
        return rising ? std::fmin(strength + 0.0071428571827709675f, 1.0f)
                      : std::fmax(strength + -0.014285714365541935f, 0.0f);
    }

    // cells floor(min + 0.001) .. floor(max - 0.001) of the actor box, inclusive
    template <class IsPowderSnow>
    bool TouchesPowderSnow(float x0, float y0, float z0, float x1, float y1, float z1, IsPowderSnow isPowderSnow) {
        const float lx = std::floor(x0 + 0.001f), ly = std::floor(y0 + 0.001f), lz = std::floor(z0 + 0.001f);
        const float hx = std::floor(x1 + -0.001f), hy = std::floor(y1 + -0.001f), hz = std::floor(z1 + -0.001f);
        const int minX = static_cast<int>(std::fmin(lx, hx)), maxX = static_cast<int>(std::fmax(lx, hx));
        const int minY = static_cast<int>(std::fmin(ly, hy)), maxY = static_cast<int>(std::fmax(ly, hy));
        const int minZ = static_cast<int>(std::fmin(lz, hz)), maxZ = static_cast<int>(std::fmax(lz, hz));
        for (int x = minX; x <= maxX; ++x)
            for (int z = minZ; z <= maxZ; ++z)
                for (int y = minY; y <= maxY; ++y)
                    if (isPowderSnow(x, y, z))
                        return true;
        return false;
    }
}
