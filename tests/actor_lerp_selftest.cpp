#include "client/entity/EntityTracker.hpp"

#include <cmath>
#include <cstdio>

using namespace perlin::entity;

static int checks = 0, failures = 0;
#define CHECK(condition)                                                                 \
    do {                                                                                 \
        ++checks;                                                                        \
        if (!(condition)) {                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);            \
            ++failures;                                                                  \
        }                                                                                \
    } while (0)

static bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }

static Entity Get(const EntityTracker& t, uint64_t rid) {
    Entity e;
    t.Find(rid, e);
    return e;
}

int main() {
    using Kind = EntityTracker::MoveKind;
    {
        EntityTracker t;
        t.AddEntity(1, 1, "minecraft:zombie", {0, 0, 0}, 0, 0, 0, 0, 1, 0, 0, 0);
        t.TickInterpolation();
        // 3 steps: 1/3, then 1/2 of the rest, then the target
        t.Move(1, {3, 0, 0}, 0, 90, 90, true, Kind::Lerp);
        CHECK(Near(Get(t, 1).pos.x, 0));
        t.TickInterpolation();
        CHECK(Near(Get(t, 1).pos.x, 1));
        CHECK(Near(Get(t, 1).prevPos.x, 0));
        t.TickInterpolation();
        CHECK(Near(Get(t, 1).pos.x, 2));
        t.TickInterpolation();
        CHECK(Near(Get(t, 1).pos.x, 3));
        CHECK(std::fabs(Get(t, 1).yaw - 90.0f) < 1e-4f);
        t.TickInterpolation();
        CHECK(Near(Get(t, 1).pos.x, 3));
        CHECK(Near(Get(t, 1).prevPos.x, 3));
    }
    {
        // a new target replaces an ordinary interpolation
        EntityTracker t;
        t.AddEntity(1, 1, "minecraft:zombie", {0, 0, 0}, 0, 0, 0, 0, 1, 0, 0, 0);
        t.TickInterpolation();
        t.Move(1, {3, 0, 0}, 0, 0, 0, true, Kind::Lerp);
        t.TickInterpolation();
        t.Move(1, {7, 0, 0}, 0, 0, 0, true, Kind::Lerp);
        t.TickInterpolation();
        CHECK(Near(Get(t, 1).pos.x, 3));
    }
    {
        // ticks above 3 set the step count; delta fills missing lanes from the last received pose
        EntityTracker t;
        t.AddEntity(1, 1, "minecraft:zombie", {0, 5, 0}, 0, 0, 0, 0, 1, 0, 0, 0);
        t.TickInterpolation();
        const double x = 4;
        t.MoveDelta(1, &x, nullptr, nullptr, nullptr, nullptr, nullptr, true, false, 4);
        t.TickInterpolation();
        CHECK(Near(Get(t, 1).pos.x, 1));
        CHECK(Near(Get(t, 1).pos.y, 5));
    }
    {
        // forceCompletion: the next target waits, shrinking by a step each tick, then plays out
        EntityTracker t;
        t.AddEntity(1, 1, "minecraft:zombie", {0, 0, 0}, 0, 0, 0, 0, 1, 0, 0, 0);
        t.TickInterpolation();
        const double a = 3, b = 9;
        t.MoveDelta(1, &a, nullptr, nullptr, nullptr, nullptr, nullptr, true, false, 0, true);
        t.MoveDelta(1, &b, nullptr, nullptr, nullptr, nullptr, nullptr, true, false, 0, false);
        t.TickInterpolation();
        CHECK(Near(Get(t, 1).pos.x, 1));
        t.TickInterpolation();
        t.TickInterpolation();
        CHECK(Near(Get(t, 1).pos.x, 3));
        // queued 3 -> 2 -> 1 while the forced one ran, then becomes active with 1 step left
        t.TickInterpolation();
        CHECK(Near(Get(t, 1).pos.x, 9));
    }
    {
        // a teleport of 0.5 block or more lands at once, a shorter one still interpolates
        EntityTracker t;
        t.AddEntity(1, 1, "minecraft:zombie", {0, 0, 0}, 0, 0, 0, 0, 1, 0, 0, 0);
        t.TickInterpolation();
        t.Move(1, {10, 0, 0}, 0, 0, 0, true, Kind::Teleport);
        CHECK(Near(Get(t, 1).pos.x, 10));
        t.Move(1, {10.3, 0, 0}, 0, 0, 0, true, Kind::Teleport);
        CHECK(Near(Get(t, 1).pos.x, 10));
        t.TickInterpolation();
        CHECK(Near(Get(t, 1).pos.x, 10.1));
    }
    {
        // before the first tick there is nothing to interpolate from
        EntityTracker t;
        t.AddEntity(1, 1, "minecraft:zombie", {0, 0, 0}, 0, 0, 0, 0, 1, 0, 0, 0);
        t.Move(1, {2, 0, 0}, 0, 0, 0, true, Kind::Lerp);
        CHECK(Near(Get(t, 1).pos.x, 2));
    }
    std::printf("ActorLerp: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
