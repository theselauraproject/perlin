#include "client/systems/multiplayer/MoveInput.hpp"

#include <cstdio>
#include <limits>

namespace {
    int failures = 0;

    void Check(bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++failures;
        }
    }
}

int main() {
    using perlin::movement::MoveInput;
    using perlin::movement::ValidMoveInput;
    MoveInput walking;
    walking.position[0] = 12.5f;
    walking.position[1] = 64.0f;
    walking.position[2] = -9.0f;
    walking.moveX = walking.rawX = 1.0f;
    walking.delta[0] = 0.1f;
    walking.delta[1] = -0.0784f;
    walking.hasDelta = true;
    Check(ValidMoveInput(walking), "ordinary walking input is valid");

    for (int axis = 0; axis < 3; ++axis) {
        MoveInput input = walking;
        input.delta[axis] = 500.0f;
        Check(ValidMoveInput(input), "positive vanilla velocity boundary is valid");
        input.delta[axis] = -500.0f;
        Check(ValidMoveInput(input), "negative vanilla velocity boundary is valid");
        input.delta[axis] = 500.01f;
        Check(!ValidMoveInput(input), "velocity above vanilla limit is rejected");
        input.delta[axis] = -500.01f;
        Check(!ValidMoveInput(input), "velocity below vanilla limit is rejected");
    }

    for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity()}) {
        for (int axis = 0; axis < 3; ++axis) {
            MoveInput input = walking;
            input.delta[axis] = invalid;
            Check(!ValidMoveInput(input), "non-finite velocity cannot enter PlayerAuthInput");
            input = walking;
            input.position[axis] = invalid;
            Check(!ValidMoveInput(input), "non-finite position cannot enter PlayerAuthInput");
        }
        for (float MoveInput::*field : {&MoveInput::yaw, &MoveInput::pitch, &MoveInput::eyeHeight,
                                       &MoveInput::moveX, &MoveInput::moveZ, &MoveInput::rawX,
                                       &MoveInput::rawZ, &MoveInput::analogX, &MoveInput::analogZ}) {
            MoveInput input = walking;
            input.*field = invalid;
            Check(!ValidMoveInput(input), "non-finite movement scalar is rejected");
        }
        for (int axis = 0; axis < 2; ++axis) {
            MoveInput input = walking;
            input.predictedVehicle = 7;
            input.vehicleRotation = std::array<float, 2>{0, 90};
            (*input.vehicleRotation)[axis] = invalid;
            Check(!ValidMoveInput(input), "non-finite predicted vehicle rotation is rejected");
        }
    }

    MoveInput vehicle = walking;
    vehicle.predictedVehicle = 42;
    Check(!ValidMoveInput(vehicle), "vehicle prediction requires rotation before dereferencing it");
    vehicle.vehicleRotation = std::array<float, 2>{5, -90};
    Check(ValidMoveInput(vehicle), "complete vehicle prediction is valid");
    vehicle.predictedVehicle.reset();
    Check(!ValidMoveInput(vehicle), "rotation requires a matching predicted vehicle");
    MoveInput device = walking;
    device.device = static_cast<perlin::InputDevice>(0);
    Check(!ValidMoveInput(device), "an unsupported physical input device is rejected");
    device = walking;
    device.interaction = static_cast<perlin::InputInteraction>(3);
    Check(!ValidMoveInput(device), "an unsupported interaction model is rejected");

    MoveInput distant = walking;
    distant.position[0] = 200000.0f;
    distant.position[1] = 20000.0f;
    distant.position[2] = -200000.0f;
    Check(ValidMoveInput(distant), "valid positions beyond the former local limits can be sent");
    constexpr float limit = perlin::movement::kPositionLimit;
    for (int axis = 0; axis < 3; ++axis)
        for (float sign : {-1.0f, 1.0f}) {
            MoveInput input = walking;
            input.eyeHeight = 0;
            input.position[axis] = sign * std::nextafter(limit, 0.0f);
            Check(ValidMoveInput(input), "the nearest float inside the vanilla position limit is valid");
            input.position[axis] = sign * limit;
            Check(!ValidMoveInput(input), "the vanilla position limit is exclusive on every axis");
        }
    distant = walking;
    distant.position[1] = std::nextafter(limit, 0.0f);
    Check(!ValidMoveInput(distant), "the serialized eye position must also be inside the position limit");
    distant = walking;
    distant.yaw = limit;
    Check(ValidMoveInput(distant), "the serialized head yaw limit is inclusive");
    distant.yaw = std::nextafter(limit, std::numeric_limits<float>::infinity());
    Check(!ValidMoveInput(distant), "head yaw beyond the vanilla limit cannot be sent");

    if (failures == 0)
        std::puts("move_input_selftest: ok");
    return failures == 0 ? 0 : 1;
}
