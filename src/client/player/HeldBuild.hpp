#pragma once
#include "client/player/PlayerPhysics.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// held use, as ClientInstance::_tickBuildAction 0x1465A2950 and GameMode::continueBuildBlock 0x14376BB80 run it:
// the repeat waits 300 ms (sneaking, after an interaction, snappable block without a line), else
// min(900 / speed, 180) ms moving, 200 ms standing; a line of snappable blocks then only places its next cell
namespace perlin::heldbuild {
    // a block and the face to click (0 down, 1 up, 2 north, 3 south, 4 west, 5 east)
    struct Target {
        glm::ivec3 support{};
        int face = 0;
    };

    struct Hit {
        bool block = false; // crosshair on a block (direct hit)
        Target target;
        glm::vec3 point{}; // where the ray stopped: hit point, or eye + look * reach without a hit
    };

    class HeldBuild {
    public:
        // use pressed; `reachedBlock`: the press clicked a block (GameMode::startBuildBlock 0x14376B260)
        void Start(uint64_t simulationTick, bool reachedBlock);
        // use released (GameMode::stopBuildBlock 0x14376C540)
        void Stop();

        // times in steady-clock ns; `velocity` picks the face to extend toward (the game's posDelta),
        // `moved` is the last tick's displacement (PostTickPositionDeltaComponent) and sets the delay
        std::optional<Target> Next(const Hit& hit, glm::vec3 eye, glm::vec3 velocity, glm::vec3 moved, bool sneaking, int64_t nowNs,
                                   uint64_t simulationTick, const std::function<bool(glm::ivec3)>& canBuildOver);

        // placement went through: click on `clicked` produced a block at `cell`; `snappable` / `interactive`
        // describe the held and clicked blocks, as GameMode::useItemOn records them
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
        // survives presses: a press that builds nothing keeps the last build's time
        int64_t lastBuildNs = 0;
        int64_t nextBuildNs = 0;  // lastBuildNs once the attempt Next returned succeeds
        bool pendingLine = false;  // attempt in flight is the line's next cell
        bool replacedSupport = false;  // attempt in flight builds into a replaceable support
    };

    // face along the strictly dominant axis, y first, then x, else z (0x14376B8D0)
    int FacingFromVec(glm::vec3 v);

    // full cubes, stairs, slabs, fences, walls, panes, bars: blocks starting a build line
    bool IsSnappable(const std::string& blockName, const std::vector<physics::Box>& shape);

    // block with collision `shape` (block-local boxes) at `cell` overlaps `body`
    bool OverlapsBody(glm::ivec3 cell, const std::vector<physics::Box>& shape, const physics::Box& body);

    // segment a..b passes through the unit cell (touching counts)
    bool SegmentHitsCell(glm::vec3 a, glm::vec3 b, glm::ivec3 cell);
}
