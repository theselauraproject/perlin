#pragma once
#include "client/input/KeyMapping.hpp"

#include <map>
#include <string>

namespace perlin {
    struct TouchPlace {
        float x = -1.0f;
        float y = -1.0f;
        float scale = 1.0f;
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
        float sensitivity = 1.0f;
        std::map<std::string, TouchPlace> layout;
        KeyBindings bindings = DefaultBindings();
    };
}
