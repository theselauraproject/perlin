#include "client/player/HeldBuild.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace perlin::heldbuild {
    namespace {
        const glm::ivec3 kFaceNormal[6] = {{0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};

        bool Contains(const std::string& s, const char* part) { return s.contains(part); }
    }

    int FacingFromVec(glm::vec3 v) {
        const float ax = std::fabs(v.x), ay = std::fabs(v.y), az = std::fabs(v.z);
        // ties fall through to z
        if (ay > ax && ay > az)
            return v.y > 0.0f ? 1 : 0;
        if (ax > ay && ax > az)
            return v.x > 0.0f ? 5 : 4;
        return v.z > 0.0f ? 3 : 2;
    }

    bool IsSnappable(const std::string& name, const std::vector<physics::Box>& shape) {
        if (Contains(name, "stairs") || Contains(name, "slab") || Contains(name, "fence") || Contains(name, "_wall") ||
            Contains(name, "glass_pane") || Contains(name, "iron_bars"))
            return true;
        return shape.size() == 1 && shape[0].x0 <= 1e-6 && shape[0].y0 <= 1e-6 && shape[0].z0 <= 1e-6 && shape[0].x1 >= 1.0 - 1e-6 &&
               shape[0].y1 >= 1.0 - 1e-6 && shape[0].z1 >= 1.0 - 1e-6;
    }

    // AABB::intersectSegment 0x142D345C0: separating axes on half extents, 0.0001f slack on the cross axes only
    bool SegmentHitsCell(glm::vec3 a, glm::vec3 b, glm::ivec3 cell) {
        const glm::vec3 lo(cell), hi = lo + 1.0f;
        const glm::vec3 d = (b - a) * 0.5f;
        const glm::vec3 c = (a + d) - (hi + lo) * 0.5f;
        const glm::vec3 e = (hi - lo) * 0.5f;
        const glm::vec3 ad = glm::abs(d);
        if (std::fabs(c.x) > ad.x + e.x || std::fabs(c.y) > ad.y + e.y || std::fabs(c.z) > ad.z + e.z)
            return false;
        constexpr float kSlack = 0.0001f;
        if (std::fabs(d.y * c.z - c.y * d.z) > (ad.y * e.z + e.y * ad.z) + kSlack)
            return false;
        if (std::fabs(d.z * c.x - d.x * c.z) > (ad.z * e.x + ad.x * e.z) + kSlack)
            return false;
        return std::fabs(d.x * c.y - d.y * c.x) <= (ad.x * e.y + ad.y * e.x) + kSlack;
    }

    bool OverlapsBody(glm::ivec3 cell, const std::vector<physics::Box>& shape, const physics::Box& body) {
        // hair of tolerance: standing exactly on or against the new block is allowed
        constexpr double kEpsilon = 1e-4;
        const physics::Box inner{body.x0 + kEpsilon, body.y0 + kEpsilon, body.z0 + kEpsilon,
                                 body.x1 - kEpsilon, body.y1 - kEpsilon, body.z1 - kEpsilon};
        for (const physics::Box& b : shape)
            if (b.Offset(cell.x, cell.y, cell.z).Intersects(inner))
                return true;
        return false;
    }

    void HeldBuild::Start(uint64_t simulationTick, bool reachedBlock) {
        // the press tick runs the start path, not a repeat
        lastAttemptTick = simulationTick;
        pendingLine = replacedSupport = false;
        if (!reachedBlock)
            return;
        lastInteractive = lastSnappable = false;
        hasDirection = hasLastBuilt = false;
    }

    void HeldBuild::Stop() {
        hasDirection = hasLastBuilt = false;
        pendingLine = replacedSupport = false;
    }

    namespace {
        // blocks per second squared, scaled before squaring (0x14376BDD7)
        float SpeedSquared(glm::vec3 moved) {
            const float x = 20.0f * moved.x, y = moved.y * 20.0f, z = moved.z * 20.0f;
            const float xy = y * y + x * x;
            return z * z + xy;
        }
    }

    int64_t HeldBuild::DelayNs(glm::vec3 moved, bool sneaking) const {
        const float speedSq = SpeedSquared(moved);
        const bool lineless = lastSnappable && !hasDirection;
        float delay = 300.0f;
        if (!sneaking && !lastInteractive && !lineless)
            delay = speedSq > 0.0f ? std::min(900.0f / std::sqrt(speedSq), 180.0f) : 200.0f;
        // mouse input: no 100 ms floor (touch and gamepad); 20 tps: the 20 / ticksPerSecond scale is 1
        return static_cast<int64_t>(static_cast<int>(delay)) * 1000000;
    }

    std::optional<Target> HeldBuild::Next(const Hit& hit, glm::vec3 eye, glm::vec3 velocity, glm::vec3 moved, bool sneaking, int64_t nowNs,
                                          uint64_t simulationTick, const std::function<bool(glm::ivec3)>& canBuildOver) {
        pendingLine = replacedSupport = false;
        if (lastAttemptTick == simulationTick)
            return std::nullopt;
        lastAttemptTick = simulationTick;
        // which block to click: 0x14376B8D0
        Target click;
        if (hasDirection && lastSnappable) {
            if (!SegmentHitsCell(eye, hit.point, nextBuild))
                return std::nullopt;
            click = {nextBuild - direction, continueFacing};
        } else if (hit.block) {
            click = hit.target;
            const float xy = velocity.x * velocity.x + velocity.y * velocity.y;
            if (lastSnappable && !hasDirection && hasLastBuilt && velocity.z * velocity.z + xy > 0.0099999998f) {
                // sneaking keeps the crosshair block but still takes the movement face
                const int face = FacingFromVec(velocity);
                click = {sneaking ? hit.target.support : lastBuilt, face};
            }
        } else {
            return std::nullopt;
        }

        // GameMode::continueBuildBlock 0x14376BB80: a replaceable support is built into, clicked on face 1
        const bool replaces = canBuildOver(click.support);
        if (replaces)
            click.face = 1;
        const glm::ivec3 cell = replaces ? click.support : click.support + kFaceNormal[std::clamp(click.face, 0, 5)];
        const int64_t deadline = lastBuildNs + DelayNs(moved, sneaking);
        if (deadline >= nowNs)
            return std::nullopt;
        // moving: a late repeat banks at most 180 ms of lost time
        nextBuildNs = SpeedSquared(moved) > 0.0f ? std::max(deadline, nowNs - 180000000) : nowNs;
        replacedSupport = replaces;
        if (sneaking || !lastSnappable)
            return click;
        // the attempt goes through either way; only an adjacent cell starts the line
        if (!hasDirection && hasLastBuilt) {
            const glm::ivec3 d = cell - lastBuilt;
            if (std::abs(d.x) + std::abs(d.y) + std::abs(d.z) == 1) {
                hasDirection = true;
                nextBuild = cell;
                direction = d;
                continueFacing = FacingFromVec(glm::vec3(d));
            }
        }
        pendingLine = hasDirection && cell == nextBuild;
        return click;
    }

    void HeldBuild::Built(const Target& clicked, glm::ivec3 cell, bool snappable, bool interactive, int64_t nowNs, bool fromRepeat) {
        if (fromRepeat && pendingLine)
            nextBuild += direction;
        lastInteractive = interactive;
        lastSnappable = !interactive && snappable;
        // the game re-reads the support after the build: now solid, so the last build lands one above it
        lastBuilt = fromRepeat && replacedSupport ? clicked.support + glm::ivec3(0, 1, 0) : cell;
        hasLastBuilt = true;
        lastBuildNs = fromRepeat ? nextBuildNs : nowNs;
        pendingLine = replacedSupport = false;
    }
}
