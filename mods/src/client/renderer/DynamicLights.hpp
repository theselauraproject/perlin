#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace perlin {
    namespace entity {
        struct Entity;
    }

    namespace dynlight {
        int ItemLevel(const std::string& name);
        int EntityLevel(const entity::Entity& e);

        struct Light {
            glm::dvec3 pos;
            float level = 0;
        };
        void Collect(const entity::Entity& e, std::vector<Light>& out);
        void Select(std::vector<Light>& lights, const glm::dvec3& camera, size_t max);
        float LevelAt(const std::vector<Light>& lights, const glm::dvec3& p);
    }
}
