#pragma once

#include "client/player/PerlinCollision.hpp"

namespace perlin::physics::bds {
    inline bool ShouldClipSneak(uint8_t actorFlags, std::optional<bool> controlSneaking, bool grounded,
                                bool& requestFlag85) {
        requestFlag85 = grounded && (controlSneaking ? *controlSneaking : bool(actorFlags & 2));
        return requestFlag85 && !(actorFlags & 0x40);
    }

    struct SneakMovement {
        CollisionVector movement;
        CollisionVector velocity;
    };

    inline SneakMovement ClipSneakMovement(CollisionBox body, float stepHeight, CollisionVector movement,
                                           CollisionVector velocity, std::span<const CollisionBox> shapes) {
        const auto original = body;
        for (const int axis : {0, 2}) {
            body[axis] = detail::Round(body[axis] + 0.025f);
            body[axis + 3] = detail::Round(body[axis + 3] - 0.025f);
        }
        for (int axis = 0; axis < 3; ++axis)
            if (body[axis] > body[axis + 3]) {
                const float center = detail::Round(detail::Round(original[axis] + original[axis + 3]) * 0.5f);
                body[axis] = body[axis + 3] = center;
            }
        const float drop = detail::Round(stepHeight * 1.01f);
        const auto supported = [&](float x, float z) {
            const float x0 = detail::Round(x + body[0]), x1 = detail::Round(x + body[3]);
            const float y0 = detail::Round(body[1] - drop), y1 = detail::Round(body[4] - drop);
            const float z0 = detail::Round(z + body[2]), z1 = detail::Round(z + body[5]);
            for (const auto& box : shapes)
                if (x1 > box[0] && x0 < box[3] && y1 > box[1] && y0 < box[4] && z1 > box[2] && z0 < box[5])
                    return true;
            return false;
        };
        const auto reduce = [](float value) {
            if (value >= 0.0f) {
                const float next = detail::Round(value - 0.05f);
                return next <= value ? std::fmax(next, 0.0f) : value;
            }
            const float next = detail::Round(value + 0.05f);
            return next <= 0.0f ? std::fmax(next, value) : 0.0f;
        };
        while (movement[0] != 0.0f && !supported(movement[0], 0.0f))
            movement[0] = reduce(movement[0]);
        while (movement[2] != 0.0f && !supported(0.0f, movement[2]))
            movement[2] = reduce(movement[2]);
        while ((movement[0] != 0.0f || movement[2] != 0.0f) && !supported(movement[0], movement[2])) {
            movement[0] = reduce(movement[0]);
            movement[2] = reduce(movement[2]);
        }
        if (std::fabs(movement[0]) <= std::numeric_limits<float>::epsilon())
            velocity[0] = 0.0f;
        if (std::fabs(movement[2]) <= std::numeric_limits<float>::epsilon())
            velocity[2] = 0.0f;
        return {movement, velocity};
    }
}
