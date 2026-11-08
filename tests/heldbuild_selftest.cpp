#include "client/player/HeldBuild.hpp"
#include "client/player/BlockUse.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>

using namespace perlin;

static int checks = 0, failures = 0;
#define CHECK(condition)                                                                                     \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(condition)) {                                                                                  \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);                                \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (0)

constexpr physics::Box kCube{0, 0, 0, 1, 1, 1};
constexpr physics::Box kSlab{0, 0, 0, 1, 0.5, 1};

int main() {
    {
        constexpr int64_t ms = 1000000;
        heldbuild::HeldBuild build;
        heldbuild::Hit hit{true, {{0, 0, 0}, 5}, {1, 0.5f, 0.5f}};
        const auto solid = [](glm::ivec3) { return false; };
        const glm::vec3 eye{0.5f, 2, 0.5f}, movement{0.2f, 0, 0};
        build.Start(20, true);
        build.Built(hit.target, {1, 0, 0}, true, false, 1000 * ms, false);

        CHECK(!build.Next(hit, eye, movement, movement, false, 1300 * ms, 26, solid));
        const auto repeat = build.Next(hit, eye, movement, movement, false, 1300 * ms + 1, 27, solid);
        CHECK(repeat && repeat->support == glm::ivec3(1, 0, 0) && repeat->face == 5);
        if (repeat) build.Built(*repeat, {2, 0, 0}, true, false, 1300 * ms + 1, true);

        CHECK(!build.Next(hit, eye, movement, movement, false, 1500 * ms, 27, solid));

        hit.point = {3.5f, 0.5f, 0.5f};
        CHECK(!build.Next(hit, eye, movement, movement, false, 1480 * ms, 28, solid));
        const auto line = build.Next(hit, eye, movement, movement, false, 1481 * ms, 29, solid);
        CHECK(line && line->support == glm::ivec3(2, 0, 0) && line->face == 5);
        if (line) build.Built(*line, {3, 0, 0}, true, false, 1481 * ms, true);
        hit.point = {1, 0.5f, 0.5f};
        CHECK(!build.Next(hit, eye, movement, movement, false, 2000 * ms, 30, solid));
        build.Stop();

        build.Start(40, false);
        hit.target = {{10, 0, 0}, 1};
        hit.point = {10.5f, 1, 0.5f};
        CHECK(!build.Next(hit, eye, {}, {}, false, 2000 * ms, 40, solid));
        const auto retry = build.Next(hit, eye, {}, {}, false, 2000 * ms, 41, solid);
        CHECK(retry && retry->support == hit.target.support && retry->face == 1);
        if (retry) build.Built(*retry, {10, 1, 0}, true, false, 2000 * ms, true);

        hit.target = {{20, 0, 0}, 1};
        CHECK(!build.Next(hit, eye, {}, {}, false, 2300 * ms, 42, solid));
        const auto apart = build.Next(hit, eye, {}, {}, false, 2300 * ms + 1, 43, solid);
        CHECK(apart && apart->support == glm::ivec3(20, 0, 0) && apart->face == 1);
        if (apart) build.Built(*apart, {20, 1, 0}, false, false, 2300 * ms + 1, true);

        const auto grass = [](glm::ivec3 p) { return p == glm::ivec3(30, 0, 0); };
        hit.target = {{30, 0, 0}, 4};
        CHECK(!build.Next(hit, eye, {}, {}, false, 2500 * ms + 1, 44, grass));
        const auto over = build.Next(hit, eye, {}, {}, false, 2500 * ms + 2, 45, grass);
        CHECK(over && over->support == glm::ivec3(30, 0, 0) && over->face == 1);
        if (over) build.Built(*over, {30, 0, 0}, true, false, 2500 * ms + 2, true);

        hit.target = {{40, 0, 0}, 1};
        CHECK(!build.Next(hit, eye, {0, 0, 0.2f}, {}, true, 2800 * ms + 2, 46, solid));
        const auto sneak = build.Next(hit, eye, {0, 0, 0.2f}, {}, true, 2800 * ms + 3, 47, solid);
        CHECK(sneak && sneak->support == glm::ivec3(40, 0, 0) && sneak->face == 3);

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
    {

        physics::State standing;
        standing.x = 0.5;
        standing.y = 0.0;
        standing.z = 0.5;
        CHECK(heldbuild::OverlapsBody({0, 0, 0}, {kCube}, physics::BoundingBox(standing, false)));
        CHECK(heldbuild::OverlapsBody({0, 1, 0}, {kCube}, physics::BoundingBox(standing, false)));
        physics::State jumping = standing;
        jumping.y = 0.42;
        CHECK(heldbuild::OverlapsBody({0, 0, 0}, {kCube}, physics::BoundingBox(jumping, false)));

        jumping.y = 1.0;
        CHECK(!heldbuild::OverlapsBody({0, 0, 0}, {kCube}, physics::BoundingBox(jumping, false)));

        CHECK(!heldbuild::OverlapsBody({0, -1, 0}, {kCube}, physics::BoundingBox(standing, false)));
        CHECK(!heldbuild::OverlapsBody({1, 0, 0}, {kCube}, physics::BoundingBox(standing, false)));

        CHECK(heldbuild::OverlapsBody({0, 0, 0}, {kSlab}, physics::BoundingBox(standing, false)));
    }
    std::printf("HeldBuild: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
