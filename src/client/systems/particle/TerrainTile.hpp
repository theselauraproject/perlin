#pragma once
#include "client/systems/blocks/BlockLibrary.hpp"

namespace perlin::particle {

    struct TerrainTile {
        int tile = 0;
        bool tintable = true;
    };

    // face uses the protocol's block-face order (0 down, 1 up, 2 north, 3 south, 4 west, 5 east); any other
    // value picks the block's general particle texture. Layered blocks such as grass show dirt below and on
    // their sides, which never takes the biome tint.
    inline TerrainTile TerrainTileFor(const BlockAppearance& app, int face, bool grassBlock) {
        const bool overlay = app.overlayTile > 0;
        if (face >= 0 && face <= 5) {
            const bool layered = grassBlock || overlay;
            TerrainTile out;
            if (face == 1) {
                out.tile = app.top;
            } else if (face == 0) {
                out.tile = app.bottom;
                out.tintable = !layered || app.tintBottom;
            } else if (overlay) {
                out.tile = app.bottom;
                out.tintable = false;
            } else {
                static constexpr int kSideSlot[4] = {3, 2, 1, 0};
                const int slot = app.sideTile[kSideSlot[face - 2]];
                out.tile = slot ? slot : app.side;
                out.tintable = !layered || app.tintSide;
            }
            if (out.tile)
                return out;
        }
        int tile = (overlay && !grassBlock) ? app.bottom : (app.side ? app.side : app.top);
        if (grassBlock && app.top)
            tile = app.top;
        if (!tile)
            tile = app.top ? app.top : app.bottom;
        return {tile, true};
    }

}
