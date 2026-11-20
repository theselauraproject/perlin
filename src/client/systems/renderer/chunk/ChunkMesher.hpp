#pragma once
#include "world/level/Level.hpp"
#include "world/level/lighting/LightRegistry.hpp"

#include <memory>
#include <vector>

namespace perlin {
    class Tessellator;
    class BlockLibrary;
    class BiomeTints;

    int BlockLightOpacity(BlockLibrary& library, uint16_t id);
    bool BlockFaceOcclusion(uint16_t id, FaceOcclusion& out);

    void MeshProfilePrint();
    void MesherInvalidateCaches();
    void MesherSetCullInnerLeaves(bool on);
    struct BlockRemapTable {
        std::vector<uint16_t> map;
        bool fullbright = false;
    };
    void MesherSetBlockRemap(std::shared_ptr<const BlockRemapTable> table);
    uint64_t MesherBlockRemapEpoch();
    struct MeshReuse {
        std::shared_ptr<const Tessellator> mesh;
        uint64_t generation = 0;
        uint64_t key = 0;
        int cellBase = 0, cellRows = 0, lightBase = 0, lightRows = 0, faceLo = 0;
        std::vector<uint16_t> cells, liquid;
        std::vector<uint8_t> biome, light;
        int rebuilt = 0, copied = 0;
    };

    void BuildChunkMesh(Level& level, const ChunkPos& pos, Tessellator& tess, BlockLibrary* library,
                        BiomeTints* tints, MeshReuse* reuse = nullptr);
}
