#pragma once

#include "client/player/PerlinSineTable.hpp"

#include <cmath>

namespace perlin::physics::bds {
    struct Rotation {
        float pitch = 0;
        float yaw = 0;
    };

    inline float InterpolateAngle(float previous, float current, float alpha) {
        float delta = std::fmod(detail::Round(detail::Round(current - previous) + 180.0f), 360.0f);
        if (delta < 0)
            delta += 360.0f;
        const float movement = detail::Round(detail::Round(delta - 180.0f) * alpha);
        return movement + previous;
    }

    inline Float3 DirectionFromRotation(Rotation previous, Rotation current, float alpha = 1.0f) {
        const float yaw = InterpolateAngle(previous.yaw, current.yaw, alpha);
        const float pitch = InterpolateAngle(previous.pitch, current.pitch, alpha);
        const float yawIndex = detail::Round(detail::Round(yaw * -182.04443f) - 32768.0f);
        const float pitchIndex = detail::Round(pitch * -182.04443f);
        const auto& sine = SineTable();
        const float horizontal = -sine[SineIndex(detail::Round(pitchIndex + 16384.0f))];
        return {horizontal * sine[SineIndex(yawIndex)], sine[SineIndex(pitchIndex)],
                horizontal * sine[SineIndex(detail::Round(yawIndex + 16384.0f))]};
    }
}
