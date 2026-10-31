#pragma once

#include "client/player/PerlinCollision.hpp"

#include <cmath>
#include <limits>

namespace perlin::physics::bds {
    struct MovementFlags {
        bool horizontalCollision = false;
        bool verticalCollision = false;
        bool onGround = false;
        bool collision = false;
        bool collidableMobNear = false;
        // HorizontalCollisionFlagComponent lanes, kept stale under no-clip like the component
        bool horizontalCollisionX = false;
        bool horizontalCollisionZ = false;
    };

    struct FinalizedMove {
        Float3 position;
        Float3 velocity;
        MovementFlags flags;
    };

    inline FinalizedMove FinalizeMove(const CollisionBox& bounds, float verticalOffset,
                                      Float3 requested, Float3 resolved, Float3 velocity,
                                      MovementFlags flags, bool noClip, bool preserveHorizontalMotion) {
        const Float3 position{detail::Round(bounds[0] + bounds[3]) * 0.5f,
                              bounds[1] + verticalOffset,
                              detail::Round(bounds[2] + bounds[5]) * 0.5f};
        if (!noClip) {
            const float epsilon = std::numeric_limits<float>::epsilon();
            const float dx = std::fabs(requested.x - resolved.x);
            const float dy = std::fabs(requested.y - resolved.y);
            const float dz = std::fabs(requested.z - resolved.z);
            // FinalizeMoveSystem 0x145dd9980; a NaN dy counts as a vertical collision
            flags.horizontalCollisionX = dx > epsilon;
            flags.horizontalCollisionZ = dz > epsilon;
            flags.horizontalCollision = flags.horizontalCollisionX || flags.horizontalCollisionZ;
            flags.verticalCollision = !(dy <= epsilon);
            if (flags.verticalCollision && requested.y < 0)
                flags.onGround = true;
            else
                flags.onGround = flags.onGround && dy <= epsilon && requested.y == 0;
            flags.collision = flags.horizontalCollision || !(dy <= epsilon);
        }
        // ResetHorizontalVelocitySystem 0x147b8c020: per-axis flags past the epsilon, not exact inequality
        if (!preserveHorizontalMotion && flags.horizontalCollision) {
            if (flags.horizontalCollisionX)
                velocity.x = 0;
            if (flags.horizontalCollisionZ)
                velocity.z = 0;
        }
        flags.collidableMobNear = false;
        return {position, velocity, flags};
    }
}
