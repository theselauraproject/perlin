#pragma once

#include "client/player/PerlinPhysicsNumerics.hpp"

#include <algorithm>
#include <optional>

namespace perlin::physics::bds {
    inline float WaterAcceleration(std::optional<float> movement, std::optional<float> underwater,
                                   float fallback, float swimSpeedMultiplier, int waterWalkSpeedEnchant,
                                   bool dolphin, bool grounded, bool player) {
        const float speed = player ? movement.value_or(0.0f) : fallback;
        float acceleration = dolphin ? speed : underwater.value_or(0.0f);
        float level = std::clamp(static_cast<float>(waterWalkSpeedEnchant), 0.0f, 3.0f);
        if (swimSpeedMultiplier <= 1.0f) {
            if (level > 0.0f) {
                if (!grounded)
                    level *= 0.5f;
                const float difference = detail::Round(speed - acceleration);
                const float product = detail::Round(difference * level);
                acceleration += detail::Round(product / 3.0f);
            }
        } else {
            const float ratio = detail::Round(level / 3.0f);
            const float scaled = detail::Round(ratio * 0.3f);
            const float base = detail::Round(scaled + 0.7f);
            const float multiplier = detail::Round(base * swimSpeedMultiplier);
            acceleration *= multiplier;
        }
        return acceleration;
    }

    inline Float3 WaterDrag(Float3 velocity, bool grounded, bool sprinting, float swimSpeedMultiplier,
                            int waterWalkSpeedEnchant, std::optional<float> customDrag = std::nullopt) {
        float retention = sprinting ? 0.9f : customDrag.value_or(0.8f);
        float level = std::clamp(static_cast<float>(waterWalkSpeedEnchant), 0.0f, 3.0f);
        if (!grounded)
            level *= 0.5f;
        if (swimSpeedMultiplier <= 1.0f) {
            const float ratio = detail::Round(level / 3.0f);
            const float difference = detail::Round(0.54600006f - retention);
            retention = detail::Round(ratio * difference) + retention;
        }
        return {retention * velocity.x, velocity.y * 0.8f, retention * velocity.z};
    }
}
