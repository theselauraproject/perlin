#pragma once

#include "client/player/PlayerPhysics.hpp"

#include <cstdint>

namespace perlin::physics::vehicle {

    struct BoatPaddles {
        int32_t rowEnd[2] = {0, 0};
        int32_t rowStart[2] = {0, 0};
        float rowTime[2] = {0, 0};
        float force[2] = {0, 0};
        int64_t tick = 0;
    };

    struct Buoyancy {
        float base = 1.0f;
        float bigWaveProbability = 0.03f;
        float bigWaveSpeed = 10.0f;

        uint8_t movementType = 1;
        bool applyGravity = true;

        bool lava = false;
        bool canAutoStepFromLiquid = false;
    };

    inline Buoyancy HorseBuoyancy() {
        Buoyancy b;
        b.movementType = 2;
        b.applyGravity = false;
        b.lava = true;
        b.canAutoStepFromLiquid = true;
        return b;
    }

    struct Horse {

        State state;

        float pitch = 0, yaw = 0, previousPitch = 0, previousYaw = 0;

        float jumpPendingScale = 0;

        float jumpRidingScale = 0;
        int32_t jumpCounter = 0;
        bool jumpHeldBefore = false;

        bool standing = false;
        double waveTimer = 0;
        bool atSurface = false, submerged = false;
    };

    struct HorseInput {

        float forward = 0, strafe = 0;

        float riderPitch = 0, riderYaw = 0;
        bool jumpHeld = false;

        float movement = 0.1f;
        std::optional<float> jumpStrength;
        float underwaterMovement = 0.02f;
        float lavaMovement = 0.02f;

        bool canPowerJump = false;
        bool hasGravity = true;
        bool canClimb = false, wallClimbing = false;

        bool localController = true;
        float maxAutoStep = 0.5625f;
    };

    void TickHorse(Horse& horse, const HorseInput& input, const World& world);

    struct Boat {

        float x = 0, y = 0, z = 0;
        float vx = 0, vy = 0, vz = 0;

        float pitch = 0, yaw = 0;
        float width = 1.4f, height = 0.455f;
        bool onGround = false;
        bool horizontalCollision = false, verticalCollision = false;

        bool leashed = false;

        bool uncontrolled = false;
        int32_t submergedTicks = 0;
        float angularVelocity = 0;
        float friction = 1.0f;
        BoatPaddles paddles;

        double waveTimer = 0;
        bool atSurface = false, submerged = false;
        bds::RecoveryState recovery;

        Box Bounds() const;
    };

    struct BoatInput {

        float moveX = 0, moveZ = 0;
        bool controlled = false;
    };

    void PaddleIntent(float moveX, float moveZ, float& left, float& right);

    void TickBoat(Boat& boat, const BoatInput& input, const Buoyancy& buoyancy, const World& world);
}
