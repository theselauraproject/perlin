#include "client/player/Bridging.hpp"
#include "client/player/HeldBuild.hpp"
#include "client/player/BlockUse.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>

using namespace perlin;
using namespace perlin::bridging;

static int checks = 0, failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

constexpr physics::Box kCube{0, 0, 0, 1, 1, 1};
constexpr physics::Box kSlab{0, 0, 0, 1, 0.5, 1};

struct World {
    std::map<std::array<int, 3>, Cell> cells;
    Cell Read(glm::ivec3 p) const {
        const auto found = cells.find({p.x, p.y, p.z});
        return found == cells.end() ? Cell{true, true, false, {}} : found->second;
    }
    void Solid(glm::ivec3 p, physics::Box shape = kCube) { cells[{p.x, p.y, p.z}] = {true, false, false, {shape}}; }
    std::optional<Target> Find(const Request& request) const {
        return FindTarget(request, [&](glm::ivec3 p) { return Read(p); });
    }
};

static Request Looking(glm::dvec3 feet, glm::dvec3 at, bool sneak = false) {
    Request request;
    request.eye = feet + glm::dvec3(0, sneak ? 1.27 : 1.62, 0);
    request.direction = at - request.eye;
    request.body = {feet.x - 0.3, feet.y, feet.z - 0.3, feet.x + 0.3, feet.y + (sneak ? 1.5 : 1.8), feet.z + 0.3};
    request.sneaking = sneak;
    return request;
}

int main() {
    {
        constexpr int64_t ms = 1000000;
        heldbuild::HeldBuild build;
        heldbuild::Hit hit{true, {{0, 0, 0}, 5}, {1, 0.5f, 0.5f}};
        const auto solid = [](glm::ivec3) { return false; };
        const glm::vec3 eye{0.5f, 2, 0.5f}, movement{0.2f, 0, 0};
        build.Start(20, true);
        build.Built(hit.target, {1, 0, 0}, true, false, 1000 * ms, false);
        // snappable without a line: 300 ms even while moving; extends from the last block on the movement face
        CHECK(!build.Next(hit, eye, movement, movement, false, 1300 * ms, 26, solid));
        const auto repeat = build.Next(hit, eye, movement, movement, false, 1300 * ms + 1, 27, solid);
        CHECK(repeat && repeat->support == glm::ivec3(1, 0, 0) && repeat->face == 5);
        if (repeat) build.Built(*repeat, {2, 0, 0}, true, false, 1300 * ms + 1, true);
        // one attempt per tick
        CHECK(!build.Next(hit, eye, movement, movement, false, 1500 * ms, 27, solid));
        // line: 4 blocks/s gives min(900 / 4, 180) ms, and only while the look ray crosses the next cell
        hit.point = {3.5f, 0.5f, 0.5f};
        CHECK(!build.Next(hit, eye, movement, movement, false, 1480 * ms, 28, solid));
        const auto line = build.Next(hit, eye, movement, movement, false, 1481 * ms, 29, solid);
        CHECK(line && line->support == glm::ivec3(2, 0, 0) && line->face == 5);
        if (line) build.Built(*line, {3, 0, 0}, true, false, 1481 * ms, true);
        hit.point = {1, 0.5f, 0.5f};
        CHECK(!build.Next(hit, eye, movement, movement, false, 2000 * ms, 30, solid));
        build.Stop();
        // a press on nothing keeps the last build time: the first repeat on a block goes at once
        build.Start(40, false);
        hit.target = {{10, 0, 0}, 1};
        hit.point = {10.5f, 1, 0.5f};
        CHECK(!build.Next(hit, eye, {}, {}, false, 2000 * ms, 40, solid));
        const auto retry = build.Next(hit, eye, {}, {}, false, 2000 * ms, 41, solid);
        CHECK(retry && retry->support == hit.target.support && retry->face == 1);
        if (retry) build.Built(*retry, {10, 1, 0}, true, false, 2000 * ms, true);
        // snappable, not next to the last block: placed anyway, no line
        hit.target = {{20, 0, 0}, 1};
        CHECK(!build.Next(hit, eye, {}, {}, false, 2300 * ms, 42, solid));
        const auto apart = build.Next(hit, eye, {}, {}, false, 2300 * ms + 1, 43, solid);
        CHECK(apart && apart->support == glm::ivec3(20, 0, 0) && apart->face == 1);
        if (apart) build.Built(*apart, {20, 1, 0}, false, false, 2300 * ms + 1, true);
        // replaceable support, standing, not snappable: 200 ms, built into and clicked on top
        const auto grass = [](glm::ivec3 p) { return p == glm::ivec3(30, 0, 0); };
        hit.target = {{30, 0, 0}, 4};
        CHECK(!build.Next(hit, eye, {}, {}, false, 2500 * ms + 1, 44, grass));
        const auto over = build.Next(hit, eye, {}, {}, false, 2500 * ms + 2, 45, grass);
        CHECK(over && over->support == glm::ivec3(30, 0, 0) && over->face == 1);
        if (over) build.Built(*over, {30, 0, 0}, true, false, 2500 * ms + 2, true);
        // sneaking: 300 ms, crosshair block with the movement face
        hit.target = {{40, 0, 0}, 1};
        CHECK(!build.Next(hit, eye, {0, 0, 0.2f}, {}, true, 2800 * ms + 2, 46, solid));
        const auto sneak = build.Next(hit, eye, {0, 0, 0.2f}, {}, true, 2800 * ms + 3, 47, solid);
        CHECK(sneak && sneak->support == glm::ivec3(40, 0, 0) && sneak->face == 3);
        // ties go to z
        CHECK(heldbuild::FacingFromVec({0.2f, 0, 0.2f}) == 3);
        CHECK(heldbuild::FacingFromVec({-0.2f, 0.2f, 0}) == 2);
        CHECK(heldbuild::FacingFromVec({0, -0.3f, 0.2f}) == 0);
        CHECK(heldbuild::SegmentHitsCell({0.5f, 2, 0.5f}, {3.5f, 0.5f, 0.5f}, {3, 0, 0}));
        CHECK(!heldbuild::SegmentHitsCell({0.5f, 2, 0.5f}, {1, 0.5f, 0.5f}, {3, 0, 0}));
    }

    CHECK(blockuse::IsReplaceableCell(true, 0, ""));
    CHECK(blockuse::IsReplaceableCell(true, 1, "minecraft:air"));
    CHECK(blockuse::IsReplaceableCell(true, 2, "minecraft:water"));
    CHECK(blockuse::IsReplaceableCell(true, 3, "minecraft:short_grass"));
    CHECK(!blockuse::IsReplaceableCell(false, 0, "minecraft:air"));
    CHECK(!blockuse::IsReplaceableCell(false, 2, "minecraft:water"));
    CHECK(!blockuse::IsReplaceableCell(true, 4, ""));
    CHECK(!blockuse::IsReplaceableCell(true, 5, "minecraft:stone"));
    CHECK(blockuse::IsUpperSlabPlacement(0, 0));
    CHECK(!blockuse::IsUpperSlabPlacement(1, 1));
    for (int face = 2; face <= 5; ++face) {
        CHECK(!blockuse::IsUpperSlabPlacement(face, 0.499f));
        CHECK(!blockuse::IsUpperSlabPlacement(face, 0.5f));
        CHECK(blockuse::IsUpperSlabPlacement(face, 0.501f));
    }
    CHECK(blockuse::IsUpsideDownStairPlacement(0, 0, false));
    CHECK(!blockuse::IsUpsideDownStairPlacement(1, 0.9f, false));
    CHECK(blockuse::IsUpsideDownStairPlacement(1, 0.1f, true));
    CHECK(!blockuse::IsUpsideDownStairPlacement(2, 0.5f, false));
    CHECK(blockuse::IsUpsideDownStairPlacement(2, 0.501f, false));
    CHECK(blockuse::IsUpsideDownStairPlacement(5, 0.2f, true));
    CHECK(blockuse::StairDirection(0) == 2);
    CHECK(blockuse::StairDirection(44.9f) == 2);
    CHECK(blockuse::StairDirection(45) == 1);
    CHECK(blockuse::StairDirection(90) == 1);
    CHECK(blockuse::StairDirection(180) == 3);
    CHECK(blockuse::StairDirection(270) == 0);
    CHECK(blockuse::StairDirection(-45) == 2);
    CHECK(blockuse::StairDirection(-90) == 0);
    CHECK(blockuse::StairDirection(-180) == 3);
    for (int face = 0; face < 6; ++face)
        CHECK(blockuse::PillarAxis(face) == (face < 2 ? 0 : face < 4 ? 2 : 1));
    const std::array<glm::ivec3, 4> offsets{glm::ivec3(0, 0, -1), {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
    for (int direction = 0; direction < 4; ++direction)
        for (int negative = 0; negative < 2; ++negative)
            for (bool crouch : {false, true}) {
                const glm::ivec3 anchor = negative ? glm::ivec3(-17, -2, -17) : glm::ivec3(0, 0, 0);
                const glm::dvec3 feet = glm::dvec3(anchor) + glm::dvec3(0.5, 1, 0.5);
                const glm::dvec3 aim = feet + glm::dvec3(offsets[direction]) * 0.75;
                World world;
                world.Solid(anchor);
                const auto target = world.Find(Looking(feet, aim, crouch));
                CHECK(target.has_value());
                if (!target) continue;
                CHECK(target->anchor == anchor);
                CHECK(target->destination == anchor + offsets[direction]);
                CHECK(target->face == direction + 2);
                const auto click = target->point - glm::dvec3(anchor);
                CHECK(click.x >= 0 && click.x <= 1 && click.y >= 0 && click.y <= 1 && click.z >= 0 && click.z <= 1);
                CHECK((direction == 0 && click.z == 0) || (direction == 1 && click.z == 1) ||
                      (direction == 2 && click.x == 0) || (direction == 3 && click.x == 1));
                int clicked[3]{anchor.x, anchor.y, anchor.z};
                const auto use = blockuse::Classify("minecraft:stone", "minecraft:air", clicked, target->face, true, true, crouch);
                CHECK(use.use == blockuse::Use::Place);
                CHECK(glm::ivec3(use.dest[0], use.dest[1], use.dest[2]) == target->destination);
            }

    World world;
    world.Solid({0, 0, 0});
    Request request = Looking({0.5, 1, 0.5}, {1.25, 1, 0.5});
    const auto baseline = world.Find(request);
    CHECK(baseline && baseline->anchor.y == 0);

    for (double height : {1.0, 1.05, 1.25, 2.25, 2.5}) {
        const auto target = world.Find(Looking({1.1, height, 0.5}, {1.4, 1, 0.5}));
        CHECK(target && target->anchor == glm::ivec3(0) && target->destination == glm::ivec3(1, 0, 0));
    }
    CHECK(!world.Find(Looking({1.31, 1, 0.5}, {1.5, 1, 0.5})));
    CHECK(!world.Find(Looking({0.5, 2.51, 0.5}, {1.25, 1, 0.5})));
    CHECK(!world.Find(Looking({0.5, 0.9, 0.5}, {1.25, 1, 0.5})));

    CHECK(!world.Find(Looking({0.5, 1, 0.5}, {1.2, 1, 1.2})));
    world.Solid({1, 0, 0});
    const auto diagonal = world.Find(Looking({0.9, 1, 0.9}, {1.2, 1, 1.2}));
    CHECK(diagonal && diagonal->anchor == glm::ivec3(1, 0, 0) && diagonal->destination == glm::ivec3(1, 0, 1));

    world = {};
    world.Solid({0, 0, 0}, kSlab);
    const auto slab = world.Find(Looking({0.5, 0.5, 0.5}, {1.25, 0.5, 0.5}));
    CHECK(slab && slab->anchor.y == 0 && std::abs(slab->point.y - 0.499) < 1e-6);
    request = Looking({1.1, 0.5, 0.5}, {1.4, 0.5, 0.5});
    CHECK(!world.Find(request));
    const std::array slabShape{kSlab};
    request.placedShape = slabShape;
    request.horizontalSlab = true;
    CHECK(world.Find(request).has_value());

    world.Solid({0, 0, 0}, kCube);
    request = Looking({0.5, 1, 0.5}, {1.25, 1, 0.5});
    request.placedShape = slabShape;
    request.horizontalSlab = true;
    const auto upperSlab = world.Find(request);
    CHECK(upperSlab && upperSlab->placedShape.size() == 1 && upperSlab->placedShape[0].y0 == 0.5 && upperSlab->placedShape[0].y1 == 1);
    world.Solid({0, 0, 0}, {0, 0, 0, 1, 0.75, 1});
    request = Looking({1.1, 0.75, 0.5}, {1.4, 0.75, 0.5});
    request.placedShape = slabShape;
    request.horizontalSlab = true;
    CHECK(!world.Find(request));

    world = {};
    world.Solid({0, 0, 0}, {0.375, 0, 0.375, 0.625, 1.5, 0.625});
    const auto fence = world.Find(Looking({0.5, 1.5, 0.5}, {1.2, 1.5, 0.5}));
    CHECK(fence && fence->point.y <= 1 && fence->point.x == 0.625);
    CHECK(!world.Find(Looking({1.0, 1.5, 0.5}, {1.4, 1.5, 0.5})));

    world = {};
    world.Solid({0, 0, 0});
    world.cells[{1, 0, 0}] = {false, true, false, {}};
    request = Looking({0.5, 1, 0.5}, {1.25, 1, 0.5});
    CHECK(!world.Find(request));
    world.Solid({1, 0, 0});
    CHECK(!world.Find(request));
    world.cells.erase({1, 0, 0});
    world.cells[{0, 0, 0}].loaded = false;
    CHECK(!world.Find(request));
    world.cells[{0, 0, 0}].loaded = true;
    world.cells[{0, 0, 0}].replaceable = true;
    CHECK(!world.Find(request));
    world.cells[{0, 0, 0}].replaceable = false;
    world.cells[{0, 0, 0}].interactive = true;
    CHECK(!world.Find(request));
    request.sneaking = true;
    CHECK(world.Find(request).has_value());
    world.cells[{0, 0, 0}].interactive = false;

    request = Looking({0.5, 1, 0.5}, {1.25, 1, 0.5});
    request.reach = 1;
    CHECK(!world.Find(request));
    request.reach = 5.7;
    request.direction = {1, 0, 0};
    CHECK(!world.Find(request));
    request.direction = {0, 1, 0};
    CHECK(!world.Find(request));
    request.direction = {0, 0, 0};
    CHECK(!world.Find(request));
    request.direction = {std::numeric_limits<double>::quiet_NaN(), -1, 0};
    CHECK(!world.Find(request));
    request.direction = {1, -1, 0};
    request.body.x0 = std::numeric_limits<double>::infinity();
    CHECK(!world.Find(request));

    PlacementRepeat repeat;
    CHECK(repeat.Ready(1000, {1, 0, 0}, 42, false));
    repeat.Record(1000, {1, 0, 0}, 42);
    CHECK(!repeat.Ready(1199, {1, 0, 0}, 42, false));
    CHECK(repeat.Ready(1200, {1, 0, 0}, 42, false));
    CHECK(!repeat.Ready(1049, {2, 0, 0}, 42, false));
    CHECK(!repeat.Ready(1050, {2, 0, 0}, 42, false));
    CHECK(!repeat.Ready(1050, {1, 0, 0}, 43, false));
    CHECK(repeat.Ready(1200, {2, 0, 0}, 42, false));
    CHECK(repeat.Ready(1001, {1, 0, 0}, 42, true));
    CHECK(repeat.Ready(999, {1, 0, 0}, 42, false));
    repeat.Reset();
    CHECK(repeat.Ready(1001, {1, 0, 0}, 42, false));

    for (int material : {0, 1, 2}) for (bool jumping : {false, true}) {
        World route;
        const physics::Box initial = material == 1 ? kSlab : material == 2 ? physics::Box{0, 0.5, 0, 1, 1, 1} : kCube;
        route.Solid({0, 0, 0}, initial);
        PlacementRepeat held;
        int placed = 0;
        for (int tick = 0; tick < 160; ++tick) {
            const double x = 0.5 + tick * 0.27;
            const double y = initial.y1 + (jumping ? 1.25 * std::sin((tick % 12) * 3.141592653589793 / 12) : 0);
            auto move = Looking({x, y, 0.5}, {x + 0.85, initial.y1, 0.5});
            move.direction = {0.85, -1.62, 0};
            if (material) {
                move.placedShape = slabShape;
                move.horizontalSlab = true;
            }
            CHECK(!route.Read({static_cast<int>(std::floor(x)), 0, 0}).replaceable);
            const auto target = route.Find(move);
            if (!target || !held.Ready(tick * 50, target->destination, 42, tick == 0)) continue;
            CHECK(target->destination == target->anchor + glm::ivec3(1, 0, 0));
            CHECK(target->destination.y == 0);
            CHECK(!OverlapsPlacement(move.body, target->destination, target->placedShape));
            CHECK(target->placedShape.size() == 1 && target->placedShape[0].y1 == initial.y1);
            route.Solid(target->destination, target->placedShape[0]);
            held.Record(tick * 50, target->destination, 42);
            ++placed;
        }
        CHECK(placed >= 40);
        CHECK(!route.Read({43, 0, 0}).replaceable);
    }

    {
        // standing in a cell, or mid-jump inside a 1x1 hole: a block there would land in the body
        physics::State standing;
        standing.x = 0.5;
        standing.y = 0.0;
        standing.z = 0.5;
        CHECK(heldbuild::OverlapsBody({0, 0, 0}, {kCube}, physics::BoundingBox(standing, false)));
        CHECK(heldbuild::OverlapsBody({0, 1, 0}, {kCube}, physics::BoundingBox(standing, false)));
        physics::State jumping = standing;
        jumping.y = 0.42;
        CHECK(heldbuild::OverlapsBody({0, 0, 0}, {kCube}, physics::BoundingBox(jumping, false)));
        // high enough to clear the cell: pillaring under the feet stays allowed
        jumping.y = 1.0;
        CHECK(!heldbuild::OverlapsBody({0, 0, 0}, {kCube}, physics::BoundingBox(jumping, false)));
        // the block under the feet and the cell beside the body are free
        CHECK(!heldbuild::OverlapsBody({0, -1, 0}, {kCube}, physics::BoundingBox(standing, false)));
        CHECK(!heldbuild::OverlapsBody({1, 0, 0}, {kCube}, physics::BoundingBox(standing, false)));
        // a slab in the feet cell still hits the body
        CHECK(heldbuild::OverlapsBody({0, 0, 0}, {kSlab}, physics::BoundingBox(standing, false)));
    }
    std::printf("Bridging: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
