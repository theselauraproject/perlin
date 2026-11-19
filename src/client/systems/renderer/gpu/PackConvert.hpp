#pragma once
#include "client/systems/gfx/Tessellator.hpp"
#include "client/systems/renderer/gpu/PackVertex.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace perlin::gpu {

    inline uint16_t ToHalf(float f) {
        uint32_t x;
        std::memcpy(&x, &f, 4);
        const uint32_t sign = (x >> 16) & 0x8000u;
        int32_t exp = static_cast<int32_t>((x >> 23) & 0xFF) - 127 + 15;
        uint32_t mant = x & 0x7FFFFFu;
        if (exp <= 0) {
            if (exp < -10)
                return static_cast<uint16_t>(sign);
            mant |= 0x800000u;
            const uint32_t shift = static_cast<uint32_t>(14 - exp);
            return static_cast<uint16_t>(sign | ((mant + (1u << (shift - 1))) >> shift));
        }
        if (exp >= 31)
            return static_cast<uint16_t>(sign | 0x7BFFu);
        return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | ((mant + 0x1000u) >> 13));
    }

    struct PackConvertContext {
        int atlasTilePx = 16;
        int gridCols = 1;
        int gridRows = 1;
        bool separateAo = false;
        bool noSkyLight = false;
        bool dropBlockIds = false;
        std::array<int, 4> fluidTiles{-1, -1, -1, -1};
        std::array<int, 2> fluidIds{-1, -1};
        std::function<int(uint16_t)> packBlockId;
        std::function<bool(uint16_t)> isFluid;
        std::function<int(uint16_t)> blockEmission;
        std::function<void(uint16_t block, int packId, uint32_t rgba)> traceTranslucent;
    };

    void ConvertQuads(const std::vector<Vertex>& verts, const std::vector<QuadInfo>& quads,
                      const PackConvertContext& context, std::vector<uint8_t>& out);

}
