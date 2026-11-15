#pragma once
#include "client/systems/gfx/Tessellator.hpp"
#include "client/systems/renderer/gpu/PackVertex.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace perlin::gpu {

    uint16_t ToHalf(float value);

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
