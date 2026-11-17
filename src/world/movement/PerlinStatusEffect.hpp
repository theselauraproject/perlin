#pragma once

#include <cstdint>

namespace perlin::physics::bds {
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
