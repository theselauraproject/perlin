#pragma once

#include "client/player/PerlinCollision.hpp"
#include "client/player/PerlinLanding.hpp"

#include <limits>
#include <span>

namespace perlin::physics::bds {
    struct SupportingShape {
        std::optional<size_t> index;
        BlockPosition position{};
        CollisionBox query;
    };

    namespace detail {
        inline float ShapeCenter(const CollisionBox& box, int axis) {
            return Round(Round(Round(box[axis + 3] - box[axis]) * 0.5f) + box[axis]);
        }

        inline float CenterDistanceSquared(const CollisionBox& box, float x, float y, float z) {
            const float dx = Round(ShapeCenter(box, 0) - x);
            const float dy = Round(ShapeCenter(box, 1) - y);
            const float dz = Round(ShapeCenter(box, 2) - z);
            return Round(Round(Round(dx * dx) + Round(dy * dy)) + Round(dz * dz));
        }
    }

    inline SupportingShape SelectSupportingShape(CollisionBox body, std::span<const CollisionBox> shapes) {
        body[1] = detail::Round(body[1] - 0.2f);
        body[4] = body[1];
        SupportingShape result{std::nullopt, {}, body};
        const float x = detail::ShapeCenter(body, 0);
        const float z = detail::ShapeCenter(body, 2);
        float bestDelta = std::numeric_limits<float>::max();
        std::optional<size_t> best;
        for (size_t i = 0; i < shapes.size(); ++i) {
            const float delta = detail::Round(body[1] - detail::ShapeCenter(shapes[i], 1));
            if (!(delta >= 0))
                continue;
            if (delta < bestDelta) {
                bestDelta = delta;
                best = i;
            } else if (best && delta == bestDelta &&
                       detail::CenterDistanceSquared(shapes[i], x, body[1], z) <
                           detail::CenterDistanceSquared(shapes[*best], x, body[1], z)) {
                best = i;
            }
        }
        if (best) {
            const auto& box = shapes[*best];
            if (box[0] < box[3] && box[4] > box[1] && box[5] > box[2]) {
                result.index = best;
                result.position = {FloorBlockCoordinate(box[0]), FloorBlockCoordinate(box[1]),
                                   FloorBlockCoordinate(box[2])};
            }
        }
        return result;
    }
}
