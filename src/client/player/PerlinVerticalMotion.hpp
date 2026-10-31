#pragma once

#include "client/player/PerlinPhysicsNumerics.hpp"

#include <bit>
#include <cmath>
#include <optional>

namespace perlin::physics::bds {
    struct GravityFlags {
        bool hasGravity = true;
        bool inScaffolding = false;
        bool overScaffolding = false;
        bool descendThroughBlock = false;
    };

    struct Gravity {
        float acceleration;
        bool resetFallDistance = false;
    };

    inline std::optional<Gravity> GroundGravity(float velocity, const GravityFlags& flags, bool slowFalling) {
        if (!flags.hasGravity ||
            ((flags.inScaffolding || flags.overScaffolding) && flags.descendThroughBlock))
            return std::nullopt;
        if (velocity < 0 && slowFalling)
            return Gravity{-0.01f, true};
        return Gravity{-0.08f, false};
    }

    inline std::optional<Gravity> PlayerWaterGravity(bool swimming) {
        if (swimming)
            return std::nullopt;
        return Gravity{-0.005f, false};
    }

    // BounceGravityCorrectionComponent: MoveRequest +52 (requested y) and +64 (resolved y) of a bounce tick
    struct BounceGravityCorrection {
        float requested;
        float resolved;
    };

    // RequestGravityCorrectionSystem 0x1484cd7b0: only while a bounce impulse is pending and the body moved in y
    inline std::optional<BounceGravityCorrection> RequestGravityCorrection(float restitutionY, float requested,
                                                                           float resolved) {
        const float epsilon = std::bit_cast<float>(0x34000000u);
        if (!(std::fabs(restitutionY) > epsilon) || !(std::fabs(resolved) > epsilon))
            return std::nullopt;
        return BounceGravityCorrection{requested, resolved};
    }

    inline float ApplyGravity(float velocity, float& fallDistance, Gravity gravity,
                              std::optional<BounceGravityCorrection> bounce = std::nullopt) {
        if (gravity.resetFallDistance)
            fallDistance = 0;
        const float epsilon = std::bit_cast<float>(0x34000000u);
        const float g = std::fabs(gravity.acceleration);
        // ApplyGravityWithBounceCorrection 0x143368b50: gravity only for the part of the tick after the impact
        // (|v|t + g t^2/2 = |resolved|); it removes ApplyGravityComponent, so the plain gravity is skipped
        if (bounce && std::fabs(bounce->resolved) > epsilon && g > epsilon) {
            const float v = bounce->requested;
            const float sign = 0.0f < v ? 1.0f : (v < 0.0f ? -1.0f : 0.0f);
            const float root = detail::Round(std::sqrt(
                detail::Round(detail::Round((g + g) * std::fabs(bounce->resolved)) + detail::Round(v * v))));
            const float impact = std::fabs(detail::Round(detail::Round(std::fabs(v) - root) / gravity.acceleration));
            return detail::Round(detail::Round(detail::Round(1.0f - impact) * sign) * g) + velocity;
        }
        return gravity.acceleration + velocity;
    }

    inline float LevitateVelocity(float velocity, int level) {
        return detail::Round(velocity * 0.8f) + detail::Round(static_cast<float>(level) * 0.01f);
    }

    // MobMovementDrag::Ground 0x14334fb00: the air drag modifier scales the lost fraction, then clamps it to [0, 1]
    inline float GroundVerticalDrag(float velocity, bool hasGravity, std::optional<float> airDrag = std::nullopt) {
        if (!hasGravity)
            return velocity;
        const float lost = detail::Round(detail::Round(1.0f - 0.98f) * airDrag.value_or(1.0f));
        const float retention = 1.0f < lost ? 0.0f : detail::Round(1.0f - (0.0f > lost ? 0.0f : lost));
        return retention * velocity;
    }

    inline float FlyingControl(float velocity, float speed, bool hover, bool creative,
                               bool up, bool down, bool upSlow = false, bool downSlow = false) {
        if (up && down)
            return 0.0f;
        if (hover && creative && !up && !down && !upSlow && !downSlow)
            velocity *= 0.375f;
        float acceleration = up ? 0.15f : 0.0f;
        if (upSlow) acceleration += 0.05f;
        if (down) acceleration -= 0.22f;
        if (downSlow) acceleration -= 0.15f;
        return detail::Round(acceleration * speed) + velocity;
    }

    inline float FlyingDragRetention(float amount) {
        const float upper = amount <= 1.0f ? amount : 1.0f;
        return amount >= 0.0f ? 1.0f - upper : 1.0f;
    }

    inline float FlyingHorizontalRetention(float coefficient, std::optional<float> override,
                                           std::optional<float> airDrag) {
        if (override)
            return (coefficient * *override) * 0.91f;
        const float modifier = airDrag.value_or(1.0f);
        const auto retention = [](float amount) {
            return amount < 0.0f ? 1.0f : 1.0f - (amount > 1.0f ? 1.0f : amount);
        };
        return retention((1.0f - coefficient) * modifier) * retention(0.089999974f * modifier);
    }

    inline float FlyingVerticalDrag(float velocity, float& fallDistance,
                                    std::optional<float> airDrag = std::nullopt) {
        fallDistance = 0;
        return velocity * FlyingDragRetention(airDrag ? *airDrag * 0.39999998f : 0.39999998f);
    }

    inline float PostGroundVertical(float velocity, float& fallDistance, const GravityFlags& flags,
                                    int levitationLevel, bool slowFalling, bool autoClimb,
                                    bool discardFriction = false, std::optional<float> airDrag = std::nullopt,
                                    std::optional<BounceGravityCorrection> bounce = std::nullopt) {
        if (levitationLevel > 0)
            velocity = LevitateVelocity(velocity, levitationLevel);
        else if (!autoClimb)
            if (auto gravity = GroundGravity(velocity, flags, slowFalling))
                velocity = ApplyGravity(velocity, fallDistance, *gravity, bounce);
        if (!autoClimb && !discardFriction)
            velocity = GroundVerticalDrag(velocity, flags.hasGravity, airDrag);
        return velocity;
    }

    inline float PostWaterVertical(float velocity, float& fallDistance, int levitationLevel, bool swimming,
                                   std::optional<BounceGravityCorrection> bounce = std::nullopt) {
        if (levitationLevel > 0)
            return LevitateVelocity(velocity, levitationLevel);
        if (auto gravity = PlayerWaterGravity(swimming))
            velocity = ApplyGravity(velocity, fallDistance, *gravity, bounce);
        return velocity;
    }

    inline float BubbleColumnVelocity(float velocity, bool dragDown, float& fallDistance, bool surface = false) {
        fallDistance = 0;
        const float next = detail::Round(velocity + (dragDown ? -0.03f : surface ? 0.1f : 0.06f));
        if (dragDown)
            return next < (surface ? -0.9f : -0.3f) ? (surface ? -0.9f : -0.3f) : next;
        return next > (surface ? 1.8f : 0.7f) ? (surface ? 1.8f : 0.7f) : next;
    }
}
