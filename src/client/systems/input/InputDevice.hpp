#pragma once

#include <cstdint>

namespace perlin {
    enum class InputDevice : uint8_t { Mouse = 1, Touch = 2, Gamepad = 3 };
    enum class InputInteraction : uint8_t { Touch = 0, Crosshair = 1, Classic = 2 };
}
