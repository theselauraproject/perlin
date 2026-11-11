#pragma once
#include "world/level/Level.hpp"
#include "world/level/lighting/LightRegistry.hpp"

namespace perlin {
    class Tessellator;
    class BlockLibrary;
    class BiomeTints;

    int BlockLightOpacity(BlockLibrary& library, uint16_t id);
    bool BlockFaceOcclusion(uint16_t id, FaceOcclusion& out);

    void MeshProfilePrint();
    void MesherInvalidateCaches();
    void BuildChunkMesh(Level& level, const ChunkPos& pos, Tessellator& tess, BlockLibrary* library,
                        BiomeTints* tints);
}
