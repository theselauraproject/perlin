#pragma once

#include "client/player/PerlinFloat.hpp"
#include "client/player/PerlinLanding.hpp"

#include <span>

namespace perlin::physics::bds {
    inline Float3 HoneyContacts(Float3 position, Float3 velocity, float bodyWidth,
                                std::span<const BlockPosition> contacts, float& fallDistance) {
        for (const auto& block : contacts) {
            velocity.x = detail::SseMultiply(velocity.x, 0.4f);
            velocity.y = std::fmax(velocity.y, -0.12f);
            velocity.z = detail::SseMultiply(velocity.z, 0.4f);
            if (detail::SseAdd(static_cast<float>(block[1]), 0.9375f) >= position.y && velocity.y < 0) {
                const float radius = detail::SseAdd(detail::SseMultiply(bodyWidth, 0.5f), 0.43125f);
                const float dx =
                    detail::SseSubtract(detail::SseAdd(static_cast<float>(block[0]), 0.5f), position.x);
                const float dz =
                    detail::SseSubtract(detail::SseAdd(static_cast<float>(block[2]), 0.5f), position.z);
                if (std::fabs(dx) > radius || std::fabs(dz) > radius)
                    fallDistance = 0;
            }
        }
        return velocity;
    }
}
