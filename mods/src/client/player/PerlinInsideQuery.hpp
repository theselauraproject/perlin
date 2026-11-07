#pragma once

#include "client/player/PerlinWorldCollision.hpp"

namespace perlin::physics::bds {
    namespace detail {
        inline int32_t IncrementInsideCoordinate(int32_t value) {
            return std::bit_cast<int32_t>(static_cast<uint32_t>(value) + 1);
        }

        inline int32_t FloorInsideCoordinate(float value) {
            int32_t truncated = std::numeric_limits<int32_t>::min();
            if (std::isfinite(value) && value >= -2147483648.0f && value < 2147483648.0f)
                truncated = static_cast<int32_t>(value);
            if (!(static_cast<float>(truncated) > value))
                return truncated;
            return std::bit_cast<int32_t>(static_cast<uint32_t>(truncated) - 1);
        }
    }

    template <class ReadChunk>
    bool InsideRegionReady(const CollisionBox& bounds, bool rejectField5352, ReadChunk&& read) {
        const int32_t minX = FloorBlockCoordinate(bounds[0]) >> 4;
        const int32_t minZ = FloorBlockCoordinate(bounds[2]) >> 4;
        const int32_t maxX = FloorBlockCoordinate(bounds[3]) >> 4;
        const int32_t maxZ = FloorBlockCoordinate(bounds[5]) >> 4;
        const uint32_t count = (static_cast<uint32_t>(maxX) - static_cast<uint32_t>(minX) + 1) *
                               (static_cast<uint32_t>(maxZ) - static_cast<uint32_t>(minZ) + 1);
        int32_t x = minX, z = minZ;
        for (uint32_t index = 0; index < count; ++index) {
            const auto chunk = read(x, z);
            if (!chunk.exists || (rejectField5352 && chunk.field5352))
                return false;
            x = detail::IncrementInsideCoordinate(x);
            if (x > maxX) {
                z = detail::IncrementInsideCoordinate(z);
                x = minX;
                if (z > maxZ)
                    z = minZ;
            }
        }
        return true;
    }

    template <class Ready, class Visit>
    bool CollectInsideCells(const CollisionBox& bounds, float expansion, bool callbackPresent, Ready&& ready,
                            Visit&& visit) {
        if (!callbackPresent)
            return false;
        std::array<int32_t, 6> cells;
        CollisionBox query;
        for (int axis = 0; axis < 3; ++axis) {
            cells[axis] = detail::FloorInsideCoordinate(
                detail::Round(detail::Round(bounds[axis] - expansion) + 0.001f));
            cells[axis + 3] = detail::FloorInsideCoordinate(
                detail::Round(detail::Round(expansion + bounds[axis + 3]) - 0.001f));
        }
        for (int axis = 0; axis < 6; ++axis)
            query[axis] = static_cast<float>(cells[axis]);
        if (!ready(query))
            return false;
        for (int32_t x = cells[0]; x <= cells[3]; x = detail::IncrementInsideCoordinate(x))
            for (int32_t y = cells[1]; y <= cells[4]; y = detail::IncrementInsideCoordinate(y))
                for (int32_t z = cells[2]; z <= cells[5]; z = detail::IncrementInsideCoordinate(z))
                    visit(x, y, z);
        return true;
    }
}
