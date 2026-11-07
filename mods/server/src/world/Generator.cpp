#include "world/Generator.hpp"

#include "core/Log.hpp"
#include "world/BlockCatalog.hpp"
#include "world/Blocks.hpp"

#include <algorithm>

namespace perlin::server {

    FlatCpuBackend::FlatCpuBackend(const ServerConfig& config) {
        profile.assign(static_cast<size_t>(kHeight), BlockRegistry::kAir);
        size_t y = 0;
        for (const FlatLayer& layer : config.flatLayers) {
            const uint16_t id = BlockCatalog::Instance().DefaultBlock(layer.block);
            for (int i = 0; i < layer.count && y < profile.size(); i++)
                profile[y++] = id;
        }
    }

    void FlatCpuBackend::Generate(std::span<const ChunkPos> positions, std::span<ChunkColumn> out) {
        std::array<uint16_t, kCells> cells;
        for (size_t n = 0; n < positions.size() && n < out.size(); n++) {
            ChunkColumn& column = out[n];
            for (int sec = 0; sec < kSections; sec++) {
                // a flat world has the same block in every column of a layer, so a section is uniform unless a layer boundary crosses it
                const uint16_t first = profile[static_cast<size_t>(sec * 16)];
                bool uniform = true;
                for (int y = 1; y < 16 && uniform; y++)
                    uniform = profile[static_cast<size_t>(sec * 16 + y)] == first;
                if (uniform) {
                    column.FillSection(sec, first);
                    continue;
                }
                for (int x = 0; x < 16; x++)
                    for (int z = 0; z < 16; z++)
                        for (int y = 0; y < 16; y++)
                            cells[static_cast<size_t>(CellIndex(x, y, z))] =
                                profile[static_cast<size_t>(sec * 16 + y)];
                column.SetSection(sec, cells.data());
            }
        }
    }

    int FlatSurfaceY(const ServerConfig& config) {
        int total = 0;
        for (const FlatLayer& l : config.flatLayers)
            total += l.count;
        return kMinY + total;
    }
}
