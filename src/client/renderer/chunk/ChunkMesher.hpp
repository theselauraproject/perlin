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
    // what a column was last meshed from, so a rebuild can copy every section whose inputs did not change.
    // A section's faces read blocks up to 2 rows past it and light up to 1 row past it; light is solved for the
    // whole column each time, so a change that relights distant rows (a block over a shaft) is still caught
    struct MeshReuse {
        std::shared_ptr<const Tessellator> mesh; // set by the caller once the build it describes is done
        uint64_t generation = 0;                 // the caller's: bump it to drop every reuse
        uint64_t key = 0;
        int cellBase = 0, cellRows = 0, lightBase = 0, lightRows = 0, faceLo = 0;
        std::vector<uint16_t> cells, liquid;
        std::vector<uint8_t> biome, light;
        int rebuilt = 0, copied = 0; // sections of the last build
    };

    // reuse: in, the previous build of this column (may be empty); out, the snapshot of this build
    void BuildChunkMesh(Level& level, const ChunkPos& pos, Tessellator& tess, BlockLibrary* library,
                        BiomeTints* tints, MeshReuse* reuse = nullptr);
}
