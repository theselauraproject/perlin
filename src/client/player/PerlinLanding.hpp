#pragma once

#include "client/player/PerlinPhysicsNumerics.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace perlin::physics::bds {
    enum class LandingSurface { Ordinary, Bed, Slime };
    using BlockPosition = std::array<int32_t, 3>;

    inline int32_t FloorBlockCoordinate(float value) {
        constexpr int32_t invalid = std::numeric_limits<int32_t>::min();
        if (!std::isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
            return invalid;
        const int32_t truncated = static_cast<int32_t>(value);
        if (truncated == invalid)
            return invalid;
        return truncated - (static_cast<float>(truncated) != value && std::signbit(value) ? 1 : 0);
    }

    struct LandingResult {
        Float3 velocity;
        std::optional<BlockPosition> bouncePosition;
    };

    inline LandingResult VerticalCollision(Float3 velocity, Float3 position, LandingSurface surface,
                                            bool sneaking, bool minecart, bool hasBounceComponent,
                                            bool isPlayer, std::optional<Float3>& restitution) {
        LandingResult result{velocity, std::nullopt};
        // ComputeBlockRestitutionSystem 0x1484bc1e0: falls slower than this don't bounce, so a body resting on
        // slime (gravity alone gives -0.0784 per tick) lands like on any block
        constexpr float minimumBounceSpeed = std::bit_cast<float>(0x3da3d71au);
        const bool bounces = velocity.y < 0 && !(minimumBounceSpeed > std::fabs(velocity.y));
        if (surface != LandingSurface::Ordinary && !sneaking && bounces) {
            if (!restitution)
                restitution = Float3{};
            const float coefficient = surface == LandingSurface::Bed ? 0.75f : 1.0f;
            // keeps the larger of a pending upward impulse and this bounce
            const float bounce = -detail::Round(velocity.y * coefficient);
            restitution->y = bounce > restitution->y ? bounce : restitution->y;
            if (minecart)
                restitution->y = detail::Round(restitution->y * 0.95f);
            if (hasBounceComponent && isPlayer)
                result.bouncePosition = BlockPosition{FloorBlockCoordinate(position.x),
                                                       FloorBlockCoordinate(position.y),
                                                       FloorBlockCoordinate(position.z)};
        }
        // ResetVerticalVelocitySystem 0x147b8d6a0 runs after the restitution is computed: every vertical
        // collision, up or down and on any block, ends with vy = 0 before the impulse is added
        result.velocity.y = 0.0f;
        return result;
    }

    inline Float3 ApplyRestitution(Float3 velocity, Float3 restitution) {
        return {detail::Round(restitution.x + velocity.x), detail::Round(restitution.y + velocity.y),
                detail::Round(restitution.z + velocity.z)};
    }
}
