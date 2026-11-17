#pragma once
#include <string>

#include <glm/glm.hpp>

namespace perlin::itemdisplay {

    struct Transform {
        glm::vec3 rotation{0.0f}, translation{0.0f}, scale{1.0f};
        bool present = false;
    };

    struct Entry {
        enum class Kind { Flat, Block, Special } kind = Kind::Flat;
        std::string special;
        std::string model;
        Transform firstRight, firstLeft, thirdRight, thirdLeft, ground, head, fixed;
        bool found = false;
    };

    const Entry& Lookup(const std::string& name, bool ground);

    glm::mat4 Apply(const Transform& t, bool leftHand);
}
