#pragma once

#include "client/player/PerlinSupport.hpp"

#include <algorithm>
#include <bit>

namespace perlin::physics::bds {
    struct TerrainHeightLimits {
        int16_t minimum = std::numeric_limits<int16_t>::min();
        int16_t maximum = std::numeric_limits<int16_t>::max();
    };

    template <class ReadColumn, class VisitCell, class AddBoundary>
    void CollectTerrainCells(const CollisionBox& bounds, TerrainHeightLimits limits, ReadColumn&& read,
                             VisitCell&& visit, AddBoundary&& boundary) {
        if (static_cast<float>(limits.minimum) > bounds[4]) {
            auto floor = bounds;
            floor[1] = -std::numeric_limits<float>::max();
            const auto height =
                std::bit_cast<int16_t>(static_cast<uint16_t>(static_cast<int>(limits.minimum) - 40));
            floor[4] = static_cast<float>(height);
            boundary(floor);
        }
        const int32_t minY =
            std::max<int32_t>(limits.minimum, FloorBlockCoordinate(detail::Round(bounds[1] - 1.0f)));
        const int32_t maxY =
            std::min<int32_t>(limits.maximum, FloorBlockCoordinate(detail::Round(bounds[4] + 1.0f)));
        if (minY >= maxY)
            return;
        const int32_t minX = FloorBlockCoordinate(detail::Round(bounds[0] - 1.0f));
        const int32_t maxX = FloorBlockCoordinate(detail::Round(bounds[3] + 1.0f));
        const int32_t minZ = FloorBlockCoordinate(detail::Round(bounds[2] - 1.0f));
        const int32_t maxZ = FloorBlockCoordinate(detail::Round(bounds[5] + 1.0f));
        for (int32_t x = minX; x <= maxX; ++x)
            for (int32_t z = minZ; z <= maxZ; ++z)
                if (read(x, z))
                    for (int32_t y = minY; y <= maxY; ++y)
                        visit(x, y, z);
    }

    struct ChunkCollisionState {
        bool exists = true;
        uint64_t position = 0;
        uint8_t field130 = 0;
        uint8_t field5352 = 0;
    };

    inline bool IsUnavailableChunk(const ChunkCollisionState& state) {
        return !state.exists || state.position == 0x8000000080000000ULL || state.field130 != 0 ||
               state.field5352 != 0;
    }

    template <class ReadChunk, class AddBarrier>
    void CollectUnavailableChunkBarriers(const CollisionBox& bounds, ReadChunk&& read, AddBarrier&& add) {
        const int32_t minX = (FloorBlockCoordinate(bounds[0]) - 8) >> 4;
        const int32_t minZ = (FloorBlockCoordinate(bounds[2]) - 8) >> 4;
        const int32_t maxX = (FloorBlockCoordinate(bounds[3]) + 8) >> 4;
        const int32_t maxZ = (FloorBlockCoordinate(bounds[5]) + 8) >> 4;
        for (int32_t z = minZ; z <= maxZ; ++z)
            for (int32_t x = minX; x <= maxX; ++x)
                if (IsUnavailableChunk(read(x, z))) {
                    const CollisionBox box{
                        static_cast<float>(16 * x),      -100000.0f, static_cast<float>(16 * z),
                        static_cast<float>(16 * x + 16), 100000.0f,  static_cast<float>(16 * z + 16)};
                    add(box);
                }
    }
}
