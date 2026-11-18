#pragma once
#include <cstdint>

namespace perlin {
    struct SkinVertex {
        float x, y, z;
        float u, v;
        float nx, ny, nz;
        uint32_t bone;
    };
    static_assert(sizeof(SkinVertex) == 36);

    struct SkinDraw {
        const float* bones = nullptr;
        uint32_t boneCount = 0;
        float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        float light[3] = {1, 1, 1};
        float tint[4] = {1, 1, 1, 1};
        float uvScale[2] = {1, 1};
        float uvOffset[2] = {0, 0};
        bool shade = true;
        bool flat = false;
        float bounds[4] = {0, 0, 0, -1};
    };
}
