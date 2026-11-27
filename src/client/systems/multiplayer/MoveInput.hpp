#pragma once

#include "client/systems/input/InputDevice.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace perlin::movement {
    inline constexpr float kPositionLimit = 33554432.0f;

    inline bool ValidPlayerPosition(float x, float y, float z) {
        for (float value : {x, y, z})
            if (!std::isfinite(value) || std::fabs(value) >= kPositionLimit)
                return false;
        return true;
    }

    struct BlockActionData {
        uint32_t action = 0;
        int x = 0, y = 0, z = 0, face = 0;
        uint32_t group = 0;
    };

    struct MoveInput {
        std::vector<uint32_t> flags;
        float moveX = 0, moveZ = 0;
        float yaw = 0, pitch = 0;
        float eyeHeight = 1.62001f;
        float rawX = 0, rawZ = 0;
        float analogX = 0, analogZ = 0;
        InputDevice device = InputDevice::Mouse;
        InputInteraction interaction = InputInteraction::Crosshair;
        float position[3] = {0, 0, 0};
        float delta[3] = {0, 0, 0};
        bool hasDelta = false;
        bool hasBlockAction = false;
        BlockActionData blockAction;
        std::optional<std::array<float, 2>> vehicleRotation;
        std::optional<int64_t> predictedVehicle;
    };

    inline bool ValidMoveInput(const MoveInput& input) {
        for (float value : {input.position[0], input.position[1], input.position[2], input.yaw,
                            input.pitch, input.eyeHeight, input.moveX, input.moveZ, input.rawX,
                            input.rawZ, input.analogX, input.analogZ, input.delta[0], input.delta[1],
                            input.delta[2]})
            if (!std::isfinite(value))
                return false;
        if (!ValidPlayerPosition(input.position[0], input.position[1], input.position[2]) ||
            !ValidPlayerPosition(input.position[0], input.position[1] + input.eyeHeight, input.position[2]) ||
            std::fabs(input.yaw) > kPositionLimit)
            return false;
        for (float value : input.delta)
            if (std::fabs(value) > 500.0f)
                return false;
        if (input.predictedVehicle.has_value() != input.vehicleRotation.has_value())
            return false;
        if (input.device != InputDevice::Mouse && input.device != InputDevice::Touch &&
            input.device != InputDevice::Gamepad)
            return false;
        if (input.interaction != InputInteraction::Touch && input.interaction != InputInteraction::Crosshair &&
            input.interaction != InputInteraction::Classic)
            return false;
        if (input.vehicleRotation)
            for (float value : *input.vehicleRotation)
                if (!std::isfinite(value))
                    return false;
        return true;
    }
}
