#pragma once

#include <cmath>
#include <limits>

namespace perlin::physics::bds {
    namespace detail {
        inline float Round(float value) {
            volatile float rounded = value;
            return rounded;
        }
    }

    struct Float3 {
        float x, y, z;
    };

    struct TravelIntentResult {
        Float3 movement;
        float turn;
        Float3 copiedMovement;
    };

    inline TravelIntentResult AttenuateTravelIntent(Float3 movement, float turn) {
        movement.x *= 0.98f;
        movement.y *= 0.98f;
        movement.z *= 0.98f;
        turn *= 0.9f;
        return {movement, turn, movement};
    }

    inline float DampComponent(float velocity, float retention) {
        return velocity * (std::fabs(velocity) > std::numeric_limits<float>::epsilon() ? retention : 0.0f);
    }

    inline Float3 ApplyFriction(Float3 velocity, float coefficient, float multiplier, bool dampVertical,
                                bool vex) {
        const float retention = coefficient * multiplier;
        if (vex) {
            velocity.x = DampComponent(velocity.x, 0.91f);
            velocity.y = DampComponent(velocity.y, 0.91f);
            velocity.z = DampComponent(velocity.z, 0.91f);
        } else {
            velocity.x = DampComponent(velocity.x, retention);
            velocity.z = DampComponent(velocity.z, retention);
            if (dampVertical)
                velocity.y = DampComponent(velocity.y, retention);
        }
        return velocity;
    }

    inline int TickJumpDelay(int delay) {
        return delay > 0 ? delay - 1 : delay;
    }

    inline float GroundAcceleration(float speed, float friction) {
        const float drag = friction * 0.91f;
        const float ratio = 0.54600006f / (drag == 0.0f ? 0.54600006f : drag);
        return ((ratio * ratio) * ratio) * speed;
    }
}
