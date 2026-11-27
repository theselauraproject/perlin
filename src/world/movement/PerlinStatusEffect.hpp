#pragma once

#include <cstdint>

namespace perlin::physics::bds {
    struct StatusEffectValue {
        int32_t amplifier = 0;
        int32_t duration = 0;
        bool ambient = false;
        bool particles = true;

        bool operator==(const StatusEffectValue&) const = default;
    };

    // Actor::addEffect and MobEffectsReplay merge an existing effect. A weaker
    // update is not saved to become active after the stronger effect expires.
    constexpr StatusEffectValue MergeStatusEffect(StatusEffectValue current,
                                                   const StatusEffectValue& incoming) {
        const bool replace = incoming.amplifier > current.amplifier ||
                             (incoming.amplifier == current.amplifier && current.duration != -1 &&
                              (incoming.duration == -1 || incoming.duration > current.duration));
        if (replace) {
            current.amplifier = incoming.amplifier;
            current.duration = incoming.duration;
            current.ambient = incoming.ambient;
        } else if (!incoming.ambient) {
            current.ambient = false;
        }
        current.particles = incoming.particles;
        return current;
    }

    constexpr bool ReplaysStatusEffectAt(uint64_t appliedTick, uint64_t recordTick) {
        return appliedTick != 0 && recordTick > appliedTick;
    }

    inline int32_t StatusEffectTicksAt(int32_t duration, uint64_t appliedTick, uint64_t movementTick) {
        if (duration == -1)
            return -1;
        if (duration <= 0)
            return 0;
        const uint64_t elapsed = movementTick > appliedTick ? movementTick - appliedTick : 0;
        if (elapsed >= static_cast<uint64_t>(duration))
            return 0;
        return duration - static_cast<int32_t>(elapsed);
    }
}
