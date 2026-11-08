#pragma once
#include "client/player/PlayerPhysics.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace perlin::heldbuild {

    struct Target {
        glm::ivec3 support{};
        int face = 0;
    };

    struct Hit {
        bool block = false;
        Target target;
        glm::vec3 point{};

        bool assist = false;
    };

    class HeldBuild {
    public:

        void Start(uint64_t simulationTick, bool reachedBlock);

        void Stop();

        std::optional<Target> Next(const Hit& hit, glm::vec3 eye, glm::vec3 velocity, glm::vec3 moved, bool sneaking, int64_t nowNs,
                                   uint64_t simulationTick, const std::function<bool(glm::ivec3)>& canBuildOver);

        void Built(const Target& clicked, glm::ivec3 cell, bool snappable, bool interactive, int64_t nowNs, bool fromRepeat);

    private:
        int64_t DelayNs(glm::vec3 moved, bool sneaking) const;

        std::optional<uint64_t> lastAttemptTick;
        bool lastInteractive = false;
        bool lastSnappable = false;
        bool hasDirection = false;
        bool hasLastBuilt = false;
        glm::ivec3 lastBuilt{};
        glm::ivec3 nextBuild{};
        glm::ivec3 direction{};
        int continueFacing = 0;

        int64_t lastBuildNs = 0;
        int64_t nextBuildNs = 0;
        bool pendingLine = false;
        bool replacedSupport = false;
    };

    int FacingFromVec(glm::vec3 v);

    bool IsSnappable(const std::string& blockName, const std::vector<physics::Box>& shape);

    bool OverlapsBody(glm::ivec3 cell, const std::vector<physics::Box>& shape, const physics::Box& body);

    bool SegmentHitsCell(glm::vec3 a, glm::vec3 b, glm::ivec3 cell);
}
