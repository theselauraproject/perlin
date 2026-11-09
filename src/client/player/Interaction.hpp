#pragma once
#include "client/entity/EntityTracker.hpp"
#include "client/player/BlockCollision.hpp"
#include "client/player/Targeting.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <glm/glm.hpp>
#include <vector>

namespace perlin {
    struct AimHit {
        enum class Kind { None, Block, Entity } kind = Kind::None;
        int bx = 0, by = 0, bz = 0;
        int face = 1;
        glm::dvec3 point{0.0};
        double distance = 0;
        uint64_t entity = 0;
        glm::dvec3 boxLo{0.0}, boxHi{0.0};
        std::vector<std::pair<glm::dvec3, glm::dvec3>> shape;

        bool buildAssist = false;
    };

    glm::vec3 EntityClickPoint(const glm::dvec3& eye, const glm::dvec3& direction, const glm::dvec3& feet,
                               double halfWidth, double height, const glm::dvec3& fallback);

    // which fluids stop the ray: an empty bucket takes water and lava sources, bottles, lily pads and boats any water
    enum class FluidAim : uint8_t { None, Sources, Water };
    FluidAim FluidAimFor(const std::string& heldItem);

    AimHit Raycast(const Level& level, const BlockLibrary* blocks,
                   const std::vector<entity::Entity>& entities,
                   const std::function<glm::vec2(const entity::Entity&)>& bounds, const glm::dvec3& eye,
                   const glm::dvec3& direction, double reach, double entityReach = -1.0,
                   FluidAim fluids = FluidAim::None);

}
