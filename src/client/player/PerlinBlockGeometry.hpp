#pragma once

#include "client/player/PerlinSupport.hpp"

#include <bit>

namespace perlin::physics::bds {
    inline Float3 BlockCollisionOrigin(Float3 offset, BlockPosition position) {
        const auto axis = [](float value, int32_t coordinate) {
            const uint32_t sum = std::bit_cast<uint32_t>(FloorBlockCoordinate(value)) +
                                 std::bit_cast<uint32_t>(coordinate);
            return static_cast<float>(std::bit_cast<int32_t>(sum));
        };
        return {axis(offset.x, position[0]), axis(offset.y, position[1]), axis(offset.z, position[2])};
    }

    inline CollisionBox TranslateBlockCollisionShape(const CollisionBox& local, Float3 origin) {
        const CollisionVector low{detail::Round(origin.x + local[0]), detail::Round(origin.y + local[1]),
                                  detail::Round(origin.z + local[2])};
        const CollisionVector high{detail::Round(origin.x + local[3]), detail::Round(origin.y + local[4]),
                                   detail::Round(origin.z + local[5])};
        // collision component collector 0x149b69ce0 reorders each axis (cmpltps/maxps): inverted shapes still collide
        CollisionBox box;
        for (int axis = 0; axis < 3; ++axis) {
            box[axis] = low[axis] < high[axis] ? low[axis] : high[axis];
            box[axis + 3] = low[axis] > high[axis] ? low[axis] : high[axis];
        }
        return box;
    }

    inline bool AcceptBlockCollisionShape(const CollisionBox& box, const CollisionBox* query) {
        return box[0] < box[3] && box[1] < box[4] && box[2] < box[5] &&
               (!query || (box[3] > (*query)[0] && box[0] < (*query)[3] &&
                           box[4] > (*query)[1] && box[1] < (*query)[4] &&
                           box[5] > (*query)[2] && box[2] < (*query)[5]));
    }

    template<class Emit>
    bool CollectBlockComponentShapes(bool enabled, Float3 offset, BlockPosition position,
                                     std::span<const CollisionBox> shapes, const CollisionBox* query,
                                     Emit&& emit) {
        const auto origin = BlockCollisionOrigin(offset, position);
        if (!enabled)
            return false;
        for (const auto& local : shapes) {
            const auto box = TranslateBlockCollisionShape(local, origin);
            if (AcceptBlockCollisionShape(box, query))
                emit(box);
        }
        return true;
    }

    template<class Emit>
    bool CollectFallbackBlockShape(const CollisionBox& world, const CollisionBox* query, Emit&& emit) {
        if (world[0] >= world[3] || world[1] >= world[4] || world[2] >= world[5])
            return false;
        if (AcceptBlockCollisionShape(world, query))
            emit(world);
        return true;
    }
}
