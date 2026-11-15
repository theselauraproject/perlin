#pragma once

namespace perlin::physics::bds {
    inline void ResetClimbingFallDistance(float& distance, bool wallClimbing, bool canClimb,
                                          bool climbableBlock, bool powderSnow, bool canStandOnSnow) {
        if (distance == 0.0f)
            return;
        if (wallClimbing || (canClimb && (climbableBlock || (powderSnow && canStandOnSnow))))
            distance = 0.0f;
    }

    inline float FallDistanceAfterMove(float distance, float resolvedY, bool onGround, bool wasInWater,
                                       bool disabled = false, bool fallingBlock = false) {
        if (disabled)
            return 0.0f;
        if (wasInWater && !fallingBlock)
            distance = 0.0f;
        return !onGround && resolvedY < 0.0f ? distance - resolvedY : 0.0f;
    }
}
