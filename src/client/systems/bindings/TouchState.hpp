#pragma once
#include "client/systems/bindings/KeyMapping.hpp"

#include <map>
#include <string>

namespace perlin {
    struct TouchPlace {
        float x = -1.0f;
        float y = -1.0f;
        float scale = 1.0f;
        bool hidden = false;
        bool operator==(const TouchPlace&) const = default;
    };

    struct TouchState {
        bool flying = false;
        bool inWater = false;
        bool riding = false;
        bool editing = false;
        int hudScale = 0;
        float scale = 1.0f;
        float opacity = 0.85f;
        bool dpad = true;
        int interaction = 0;
        float circleSize = 1.0f;
        float holdDelay = 0.3f;
        float dpadGap = 0.12f;
        bool debugButtons = true;
        bool leftHanded = false;
        bool invertY = false;
        bool circle = true;
        float sensitivity = 1.0f;
        std::map<std::string, TouchPlace> layout;
        KeyBindings bindings = DefaultBindings();
    };
}
