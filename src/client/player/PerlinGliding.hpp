#pragma once

#include "client/player/PerlinRotation.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace perlin::physics::bds {
    // GlideMoveSystem 0x146d1a440: client sine table, divisions by the look length (no reciprocal)
    inline Float3 GlideVelocity(Float3 velocity, Rotation previous, Rotation current, bool slowFalling,
                                bool boost, float& fallDistance) {
        const auto mul = [](float a, float b) { return detail::Round(a * b); };
        const auto add = [](float a, float b) { return detail::Round(a + b); };
        const auto sub = [](float a, float b) { return detail::Round(a - b); };
        const auto div = [](float a, float b) { return detail::Round(a / b); };
        const auto sine = [](float coordinate) { return CurrentSineEntry(SineIndex(coordinate)); };
        if (velocity.y > -0.5f)
            fallDistance = 1.0f;
        const float yaw = InterpolateAngle(previous.yaw, current.yaw, 1.0f);
        const float pitch = InterpolateAngle(previous.pitch, current.pitch, 1.0f);
        // yaw goes through radians and -pi before the table scale: not the same index as yaw * -182.04443
        const float yawCoordinate =
            mul(add(mul(yaw, -0.017453292f), std::bit_cast<float>(0xc0490fdbu)), 10430.378f);
        const float pitchCoordinate = mul(mul(pitch, -0.017453292f), 10430.378f);
        const float horizontal = -sine(add(pitchCoordinate, 16384.0f));
        const Float3 look{mul(horizontal, sine(yawCoordinate)), sine(pitchCoordinate),
                          mul(horizontal, sine(add(yawCoordinate, 16384.0f)))};
        const float radians = mul(current.pitch, 0.017453292f);
        const float horizontalLookSquared = add(mul(look.x, look.x), mul(look.z, look.z));
        const float horizontalLook = std::sqrt(horizontalLookSquared);
        const float horizontalSpeed = std::sqrt(add(mul(velocity.x, velocity.x), mul(velocity.z, velocity.z)));
        const float trigCoordinate = mul(radians, 10430.378f);
        const float cosine = sine(add(trigCoordinate, 16384.0f));
        const float lookLength =
            std::sqrt(add(add(mul(look.y, look.y), mul(look.x, look.x)), mul(look.z, look.z)));
        const float lift = mul(mul(std::min(div(lookLength, 0.4f), 1.0f), cosine), cosine);
        const float gravity = slowFalling ? -0.01f : -0.08f;
        velocity.y = sub(velocity.y, mul(add(mul(lift, 0.75f), -1.0f), gravity));
        if (horizontalLookSquared > 0 && velocity.y < 0) {
            const float descent = mul(mul(velocity.y, -0.1f), lift);
            velocity.y = add(velocity.y, descent);
            velocity.x = add(velocity.x, div(mul(descent, look.x), horizontalLook));
            velocity.z = add(velocity.z, div(mul(descent, look.z), horizontalLook));
        }
        if (radians < 0) {
            const float climb = mul(mul(sine(trigCoordinate), horizontalSpeed), -0.04f);
            velocity.y = add(velocity.y, mul(climb, 3.2f));
            velocity.x = sub(velocity.x, div(mul(climb, look.x), horizontalLook));
            velocity.z = sub(velocity.z, div(mul(climb, look.z), horizontalLook));
        }
        if (horizontalLookSquared > 0) {
            velocity.x = add(velocity.x, mul(sub(mul(horizontalSpeed, div(look.x, horizontalLook)), velocity.x), 0.1f));
            velocity.z = add(velocity.z, mul(sub(mul(horizontalSpeed, div(look.z, horizontalLook)), velocity.z), 0.1f));
        }
        if (boost) {
            const auto boosted = [&](float speed, float direction) {
                return add(speed, add(mul(sub(mul(direction, 1.5f), speed), 0.5f), mul(direction, 0.1f)));
            };
            velocity.x = boosted(velocity.x, look.x);
            velocity.y = boosted(velocity.y, look.y);
            velocity.z = boosted(velocity.z, look.z);
        }
        return {mul(velocity.x, 0.99f), mul(velocity.y, 0.98f), mul(velocity.z, 0.99f)};
    }
}
