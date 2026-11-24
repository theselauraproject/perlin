#include "client/systems/model/EntityTracker.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

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

        t.TickInterpolation();
        CHECK(Near(Get(t, 1).pos.x, 9));
    }
    {

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

        EntityTracker t;
        t.AddEntity(1, 1, "minecraft:zombie", {0, 0, 0}, 0, 0, 0, 0, 1, 0, 0, 0);
        t.Move(1, {2, 0, 0}, 0, 0, 0, true, Kind::Lerp);
        CHECK(Near(Get(t, 1).pos.x, 2));
    }
    {
        EntityTracker t;
        t.AddEntity(2, 2, "minecraft:arrow", {0, 10, 0}, 0, 0, 0, 0, 1, 0, 0, 0);
        t.TickInterpolation();
        t.Move(2, {1, 10, 0}, 0, 0, 0, true, Kind::Lerp);
        t.TickInterpolation();
        CHECK(Near(Get(t, 2).pos.x, 1));
        t.Move(2, {2, 9.95, 0}, 0, 0, 0, true, Kind::Lerp);
        t.TickInterpolation();
        CHECK(Near(Get(t, 2).pos.x, 2));
        CHECK(Near(Get(t, 2).prevPos.x, 1));
        t.TickInterpolation();
        CHECK(Near(Get(t, 2).pos.x, 3));
        CHECK(Near(Get(t, 2).pos.y, 9.9));
        t.TickInterpolation();
        CHECK(Near(Get(t, 2).pos.x, 3.99));
        CHECK(std::fabs(Get(t, 2).pos.y - 9.8005) < 1e-9);
        const double held = Get(t, 2).pos.x;
        t.TickInterpolation();
        CHECK(Near(Get(t, 2).pos.x, held));
    }
    {
        EntityTracker t;
        t.AddItem(3, 3, {0, 5, 0}, "minecraft:stone", 1, 0, 0, false);
        t.SetMotion(3, {0, 0, 0});
        t.TickInterpolation();
        t.Move(3, {0.5, 5, 0}, 0, 0, 0, false, Kind::Lerp);
        CHECK(Near(Get(t, 3).pos.x, 0));
        t.TickInterpolation();
        CHECK(Near(Get(t, 3).pos.x, 0.5));
        CHECK(Near(Get(t, 3).prevPos.x, 0));
    }
    {
        EntityTracker t;
        t.AddEntity(1, 1, "minecraft:zombie", {4, 0, 0}, 0, 0, 0, 0, 1, 0, 0, 0);
        t.AddItem(5, 50, {0, 0, 0}, "minecraft:stone", 1, 0, 0, false);
        t.Pickup(5, 1);
        t.Remove(50);
        Entity flying;
        CHECK(t.Find(5, flying));
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        std::vector<Entity> seen;
        t.Snapshot(seen);
        for (const Entity& e : seen)
            if (e.runtimeId == 5) {
                CHECK(e.pos.x > 0.0 && e.pos.x < 4.0);
                CHECK(e.scale > 0.0f && e.scale < 1.0f);
            }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        t.Snapshot(seen);
        CHECK(!t.Find(5, flying));
        CHECK(t.Find(1, flying));
    }
    {
        EntityTracker t;
        t.AddItem(6, 60, {0, 0, 0}, "minecraft:stone", 1, 0, 0, false);
        t.Pickup(6, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        std::vector<Entity> seen;
        t.Snapshot(seen);
        t.AddItem(6, 61, {2, 0, 0}, "minecraft:dirt", 1, 0, 0, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        t.Snapshot(seen);
        Entity reused;
        CHECK(t.Find(6, reused) && reused.scale == 1.0f && Near(reused.pos.x, 2));
    }
    // A falling block between the server's corrections (sent every 5 ticks) keeps falling on the
    // client along the same path as the server's tick, so it moves every tick.
    {
        EntityTracker t;
        t.SetItemCollider([](double, double y0, double, double, double, double) { return y0 < 0.0; });
        t.AddEntity(9, 9, "minecraft:falling_block", {0.5, 20, 0.5}, 0, 0, 0, 0, 1, 0, 0, 0);
        double y = 20, vy = 0, last = Get(t, 9).pos.y;
        bool movedEveryTick = true, onPath = true;
        for (int tick = 1; tick <= 40; tick++) {
            vy = (vy - 0.04) * 0.98;
            y = std::max(0.0, y + vy);
            t.TickInterpolation();
            if (tick % 5 == 0)
                t.Move(9, {0.5, y, 0.5}, 0, 0, 0, false, Kind::Lerp);
            const double now = Get(t, 9).pos.y;
            if (y > 0.0 && !(now < last))
                movedEveryTick = false;
            if (std::fabs(now - y) > 1e-3 && std::getenv("LERP_DEBUG")) std::printf("tick %d server %.6f client %.6f\n", tick, y, now);
            if (std::fabs(now - y) > 1e-3)
                onPath = false;
            last = now;
        }
        CHECK(movedEveryTick);
        CHECK(onPath);
        CHECK(Get(t, 9).pos.y >= 0.0);
        // A late correction is eased in, not jumped to.
        t.Move(9, {0.5, Get(t, 9).pos.y + 0.5, 0.5}, 0, 0, 0, false, Kind::Lerp);
        const double before = Get(t, 9).pos.y;
        t.TickInterpolation();
        CHECK(Get(t, 9).pos.y - before < 0.4);
    }
    std::printf("ActorLerp: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
