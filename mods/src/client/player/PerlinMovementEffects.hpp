#pragma once

#include <array>
#include <bit>
#include <cstdint>

namespace perlin::physics::bds {
    enum class MovementEffectType : int32_t { GlideBoost = 0, DolphinBoost = 1 };

    struct MovementEffect {
        uint64_t tick = 0;
        int32_t remaining = 0;
        int32_t type = -1;
        // server event replayed in its frame (0x143A18B90): sets the entry outright, no shorter-duration check
        bool server = false;
    };

    using MovementEffects = std::array<MovementEffect, 2>;

    inline void ApplyMovementEffect(MovementEffects& effects, MovementEffect incoming) {
        if (incoming.type < 0 || incoming.type >= static_cast<int32_t>(effects.size()))
            return;
        auto& current = effects[incoming.type];
        if (!incoming.server && incoming.remaining != current.remaining &&
            (current.remaining == -1 || (incoming.remaining != -1 && incoming.remaining < current.remaining)))
            return;
        current = incoming;
    }

    inline void TickMovementEffects(MovementEffects& effects) {
        for (auto& effect : effects) {
            if (effect.type == -1 || effect.remaining == -1)
                continue;
            if (effect.remaining != 0)
                effect.remaining = std::bit_cast<int32_t>(std::bit_cast<uint32_t>(effect.remaining) - 1u);
            if (effect.remaining <= 0)
                effect = {};
        }
    }

    inline float SwimSpeedMultiplier(bool swimming, const MovementEffects& effects) {
        return swimming && effects[static_cast<int32_t>(MovementEffectType::DolphinBoost)].type != -1 ? 2.0f
                                                                                                      : 1.0f;
    }
}
