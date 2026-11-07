#pragma once

#include "client/player/PlayerPhysics.hpp"

#include <cstdint>

namespace perlin::physics::vehicle {
    // BoatPaddleComponent: one 20-byte lane per paddle (left, right) then the tick counter at +40
    struct BoatPaddles {
        int32_t rowEnd[2] = {0, 0};
        int32_t rowStart[2] = {0, 0};
        float rowTime[2] = {0, 0};
        float force[2] = {0, 0};
        int64_t tick = 0;
    };

    // minecraft:buoyant (BuoyancyComponent): the values are entity-definition data, not code
    struct Buoyancy {
        float base = 1.0f;
        float bigWaveProbability = 0.03f;
        float bigWaveSpeed = 10.0f;
        // 0 none, 1 waves, 2 bobbing
        uint8_t movementType = 1;
        bool applyGravity = true;
        // liquid_blocks also holds lava / flowing_lava
        bool lava = false;
        bool canAutoStepFromLiquid = false;
    };

    // horse family definition values (minecraft:buoyant of horse.json): bobbing, no gravity, water and lava
    inline Buoyancy HorseBuoyancy() {
        Buoyancy b;
        b.movementType = 2;
        b.applyGravity = false;
        b.lava = true;
        b.canAutoStepFromLiquid = true;
        return b;
    }

    struct Horse {
        // mob movement state; mobSize carries the horse box
        State state;
        // ActorRotation (pitch, yaw) and the previous-rotation lanes kept in step by the wrap
        float pitch = 0, yaw = 0, previousPitch = 0, previousYaw = 0;
        // JumpPendingScaleComponent of the horse
        float jumpPendingScale = 0;
        // rider side: JumpRidingScaleComponent, VanillaClientGameplayComponent +4 (counter) and +21 (jump held
        // last tick)
        float jumpRidingScale = 0;
        int32_t jumpCounter = 0;
        bool jumpHeldBefore = false;
        // actor flag 39 STANDING, set when a power jump is released
        bool standing = false;
        double waveTimer = 0;
        bool atSurface = false, submerged = false;
    };

    struct HorseInput {
        // rider move vector as Input::forward / Input::strafe (strafe right-positive)
        float forward = 0, strafe = 0;
        // rider ActorRotation (pitch, yaw)
        float riderPitch = 0, riderYaw = 0;
        bool jumpHeld = false;
        // horse attributes: minecraft:movement, minecraft:horse.jump_strength, underwater / lava movement
        float movement = 0.1f;
        std::optional<float> jumpStrength;
        float underwaterMovement = 0.02f;
        float lavaMovement = 0.02f;
        // actor flags of the horse: 45 CAN_POWER_JUMP, gravity, CAN_CLIMB / WALLCLIMBING
        bool canPowerJump = false;
        bool hasGravity = true;
        bool canClimb = false, wallClimbing = false;
        // VehicleInputIntent+2: the controlling passenger is the local player (eased turning)
        bool localController = true;
        float maxAutoStep = 0.5625f;
    };

    void TickHorse(Horse& horse, const HorseInput& input, const World& world);

    struct Boat {
        // StateVector: feet centre and posDelta
        float x = 0, y = 0, z = 0;
        float vx = 0, vy = 0, vz = 0;
        // ActorRotation
        float pitch = 0, yaw = 0;
        float width = 1.4f, height = 0.455f;
        bool onGround = false;
        bool horizontalCollision = false, verticalCollision = false;
        // actor flag 30 LEASHED of the boat
        bool leashed = false;
        // BoatMovementComponent (+0 byte, +4, +8, +12)
        bool uncontrolled = false;
        int32_t submergedTicks = 0;
        float angularVelocity = 0;
        float friction = 1.0f;
        BoatPaddles paddles;
        // BuoyancyComponent +24 (double) and the float request of this tick
        double waveTimer = 0;
        bool atSurface = false, submerged = false;
        bds::RecoveryState recovery;

        Box Bounds() const;
    };

    struct BoatInput {
        // MoveInputComponent +36 / +40: x is left-positive, z forward
        float moveX = 0, moveZ = 0;
        bool controlled = false;
    };

    // PassengerBoatPaddleInputSystem 0x14b372ea0: analog (non-touch) paddle intent from the move vector
    void PaddleIntent(float moveX, float moveZ, float& left, float& right);

    void TickBoat(Boat& boat, const BoatInput& input, const Buoyancy& buoyancy, const World& world);
}
