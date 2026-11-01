#include "client/entity/JavaAnimation.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <array>
#include <fstream>
#include <sstream>
using namespace perlin;
using namespace perlin::entity::java;
int failures = 0;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            std::cerr << __LINE__ << ": " #x "\n";                                                           \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (0)
bool Near(float a, float b, float epsilon = 0.0001f) {
    return std::abs(a - b) < epsilon;
}
struct Trace {
    float body, speed, phase;
    int ticks;
};
Trace Run(int fps) {
    BodyRotation body;
    body.Reset(0);
    WalkAnimation walk;
    double remainder = 0, previousTick = 0;
    int ticks = 0;
    for (int frame = 1; frame <= fps * 6; ++frame) {
        const double dt = 1.0 / fps, end = double(frame) / fps;
        remainder += dt;
        while (remainder + 1e-10 >= 0.05) {
            remainder = std::max(0.0, remainder - 0.05);
            const double t = end - remainder;
            const auto position = [](double time) {
                return std::min(time, 2.0) * 2.0 - std::max(time - 4.0, 0.0) * 2.0;
            };
            const double dx = position(t) - position(previousTick);
            body.PlayerTick(dx, 0, 179.0f + float(t) * 2, t > 3 && t < 3.5);
            walk.Tick(float(std::abs(dx)), false);
            previousTick = t;
            ++ticks;
        }
    }
    const float p = float(remainder / 0.05);
    return {body.Sample(p), walk.Speed(p), walk.Position(p), ticks};
}
void CheckJavaReference(const char* file) {
    std::ifstream input(file);
    CHECK(input.good());
    std::string line;
    int cases = 0, mismatches = 0;
    float largestError = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        PlayerState s;
        int id, right, left;
        row >> id >> s.headYaw >> s.pitch >> s.walk >> s.speed >> s.age >> s.attack >> s.swim >>
            s.speedScale >> s.useTicks >> s.chargeTicks >> s.crouching >> s.riding >> s.gliding >>
            s.usingItem >> s.useLeft >> s.mainLeft >> s.attackLeft >> right >> left;
        s.swinging = true;
        s.right = static_cast<ArmPose>(right);
        s.left = static_cast<ArmPose>(left);
        const auto p = AnimatePlayer(s);
        const std::array<const Part*, 6> parts = {&p.head,    &p.body,     &p.rightArm,
                                                  &p.leftArm, &p.rightLeg, &p.leftLeg};
        bool match = true;
        for (int bone = 0; bone < 6; ++bone)
            for (int axis = 0; axis < 6; ++axis) {
                float expected = 0;
                row >> expected;
                CHECK(!row.fail());
                const float actual = axis < 3 ? parts[bone]->rotation[axis] : parts[bone]->offset[axis - 3];
                const float error = std::abs(actual - expected);
                largestError = std::max(largestError, error);
                if (error > .00003f) {
                    if (mismatches < 12)
                        std::cerr << "Java reference " << id << " bone " << bone << " axis " << axis << ": "
                                  << actual << " expected " << expected << "\n";
                    ++mismatches;
                    match = false;
                }
            }
        if (match)
            ++cases;
    }
    CHECK(cases == 400);
    CHECK(mismatches == 0);
    std::cout << "Java JAR reference: " << cases << "/400 poses, " << mismatches
              << " mismatched components, maximum error " << largestError << "\n";
}
void CheckJavaRootReference(const char* file) {
    std::ifstream input(file);
    CHECK(input.good());
    std::string line;
    int cases = 0, mismatches = 0;
    float largestError = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        int id;
        float scale;
        RootState s;
        row >> id >> s.bodyYaw >> s.pitch >> s.age >> s.deathTicks >> s.swim >> s.glideTicks >> s.flightYaw >>
            scale >> s.height >> s.gliding >> s.spin >> s.sleeping >> s.inWater >> s.visuallySwimming >>
            s.flyingYaw >> s.upsideDown >> s.shaking >> s.bedDirection;
        auto matrix = PlayerRootTransform(s, scale) * glm::scale(glm::mat4(1), glm::vec3(scale / 16));
        bool match = true;
        for (int col = 0; col < 4; ++col)
            for (int axis = 0; axis < 4; ++axis) {
                float expected = 0;
                row >> expected;
                CHECK(!row.fail());
                const float error = std::abs(matrix[col][axis] - expected);
                largestError = std::max(largestError, error);
                if (error > .00003f) {
                    if (mismatches < 12)
                        std::cerr << "Java root " << id << " [" << col << "][" << axis
                                  << "]: " << matrix[col][axis] << " expected " << expected << "\n";
                    ++mismatches;
                    match = false;
                }
            }
        if (match)
            ++cases;
    }
    CHECK(cases == 120);
    CHECK(mismatches == 0);
    std::cout << "Java JAR roots: " << cases << "/120 matrices, maximum error " << largestError << "\n";
}
static glm::mat4 JavaRotation(glm::vec3 r) {
    return glm::rotate(glm::mat4(1), -r.z, glm::vec3(0, 0, 1)) *
           glm::rotate(glm::mat4(1), r.y, glm::vec3(0, 1, 0)) *
           glm::rotate(glm::mat4(1), -r.x, glm::vec3(1, 0, 0));
}

// one render-frame stream through PlayerBodyClock; pos and head are functions of time
template <class Pos, class Head>
float RunClock(int fps, double seconds, Pos pos, Head head, float& maxOffset, bool swinging = false) {
    PlayerBodyClock clock;
    float body = 0;
    maxOffset = 0;
    for (int frame = 0; frame <= int(seconds * fps); ++frame) {
        const double t = double(frame) / fps;
        const auto [x, z] = pos(t);
        body = clock.Frame(x, z, head(t), frame == 0 ? 0.0 : 1.0 / fps, swinging, false, 0);
        if (frame > 0)
            maxOffset = std::max(maxOffset, std::abs(Wrap(head(t) - body)));
    }
    return body;
}
int main(int argc, char** argv) {
    if (argc > 1)
        CheckJavaReference(argv[1]);
    if (argc > 2)
        CheckJavaRootReference(argv[2]);
    CHECK(Near(Turn(179, -179, 0.5f), 180));
    BodyRotation b;
    b.Reset(0);
    b.PlayerTick(0, 0, 40, false);
    CHECK(Near(b.current, 0));
    b.PlayerTick(0, 0, 100, false);
    CHECK(Near(b.current, 50));
    b.Reset(0);
    b.PlayerTick(0.1, 0, 0, false);
    CHECK(Near(b.current, -27));
    b.Reset(0);
    b.PlayerTick(0, -0.1, 0, false);
    CHECK(Near(b.current, 0));
    b.Reset(179);
    b.PlayerTick(0, 0, -179, true);
    CHECK(Near(b.current, 179.6f));
    b.Reset(0);
    for (int i = 0; i < 10; ++i)
        b.MobTick(0, 0, 0, 40);
    CHECK(Near(b.current, 0));
    for (int i = 0; i < 11; ++i)
        b.MobTick(0, 0, 0, 40);
    CHECK(Near(b.current, 40));
    b.Reset(0);
    b.MobTick(.1, 0, 90, 100);
    CHECK(Near(b.current, 90));
    WalkAnimation w;
    w.Tick(.1f, false);
    CHECK(Near(w.speed, .16f));
    CHECK(Near(w.Position(.5f), .08f));
    w.Tick(.1f, true);
    CHECK(Near(w.Position(1), 0));
    CHECK(Near(w.Speed(1), 0));
    const auto reference = Run(20);
    for (int fps : {10, 15, 30, 60, 144, 240}) {
        auto t = Run(fps);
        CHECK(t.ticks == 120);
        CHECK(Near(t.body, reference.body, .001f));
        CHECK(Near(t.speed, reference.speed, .001f));
        CHECK(Near(t.phase, reference.phase, .001f));
    }
    {
        // standing, head turns 90: body stays put until the 50 degree limit, then is dragged. The limit holds at
        // ticks; between them the head leads by up to one tick of its own turn (4.5 degrees here), as in Java
        float off;
        const auto still = [](double) { return std::pair{0.0, 0.0}; };
        const float body = RunClock(60, 2, still, [](double t) { return float(std::min(t, 1.0) * 90); }, off);
        CHECK(Near(body, 40, .01f));
        CHECK(off <= 54.51f);
        CHECK(off > 50.0f);
        RunClock(60, 2, still, [](double t) { return float(std::min(t, 1.0) * 40); }, off);
        CHECK(Near(off, 40, .01f));
        // walking backward (+z is yaw 0, so -z is behind): body keeps facing the head
        const float back = RunClock(
            60, 3, [](double t) { return std::pair{0.0, -t * 4.3}; }, [](double) { return 0.0f; }, off);
        CHECK(std::abs(Wrap(back)) < .01f);
        // strafing left (+x is yaw -90): body turns toward the movement, capped 50 from the head
        const float strafe = RunClock(
            60, 3, [](double t) { return std::pair{t * 4.3, 0.0}; }, [](double) { return 0.0f; }, off);
        CHECK(Near(strafe, -50, .01f));
        CHECK(off <= 50.01f);
        // diagonal forward-left: body follows the movement direction, under the cap
        const float diag = RunClock(
            60, 3, [](double t) { return std::pair{t * 3.0, t * 3.0}; }, [](double) { return 0.0f; }, off);
        CHECK(Near(diag, -45, .05f));
        // swinging snaps the body back to the head while moving sideways
        const float swing = RunClock(
            60, 3, [](double t) { return std::pair{t * 4.3, 0.0}; }, [](double) { return 0.0f; }, off, true);
        CHECK(std::abs(Wrap(swing)) < .01f);
        // head crossing +-180: no full spin, offset bounded (one tick of a 20 degree/s turn past the limit)
        RunClock(144, 3, still, [](double t) { return float(170 + t * 20); }, off);
        CHECK(off <= 51.01f);
        // same trajectory, every frame rate: same body
        const auto walk = [](double t) {
            return std::pair{std::sin(t) * 3.0, std::min(t, 2.0) * 2.0 - std::max(t - 3.0, 0.0) * 3.0};
        };
        const auto look = [](double t) { return float(t * 37.0); };
        const float ref = RunClock(20, 6, walk, look, off);
        for (int fps : {10, 15, 30, 60, 144, 240})
            CHECK(Near(Wrap(RunClock(fps, 6, walk, look, off) - ref), 0, 1.0f));
        // teleport: reset to the head, no turn toward the jump
        PlayerBodyClock c;
        c.Frame(0, 0, 0, 0, false, false, 0);
        for (int i = 0; i < 10; ++i)
            c.Frame(0, 0, 0, 0.05, false, false, 0);
        CHECK(Near(c.Frame(100, 0, 30, 0.05, false, false, 0), 30));
        // riding: the mount faces 90, the rider looks at 0: the head is held to 85 from the mount's body and the
        // body leans a fifth of that toward the head (LivingEntityRenderer.solveBodyRot): 90 - 5 - 17 = 68
        c.Reset(0, 0, 0);
        float ride = 0;
        for (int i = 0; i < 40; ++i)
            ride = c.Frame(0, 0, 0, 0.05, false, true, 90);
        CHECK(Near(ride, 68, .1f));
        // looking 30 degrees off the mount: the body is the mount's
        for (int i = 0; i < 40; ++i)
            ride = c.Frame(0, 0, 60, 0.05, false, true, 90);
        CHECK(Near(ride, 90, .1f));
    }
    PoseBlend blend;
    blend.Tick(true);
    CHECK(Near(blend.Sample(.5f), .045f));
    for (int i = 0; i < 20; ++i)
        blend.Tick(true);
    CHECK(Near(blend.current, 1));
    for (int i = 0; i < 20; ++i)
        blend.Tick(false);
    CHECK(Near(blend.current, 0));
    CHECK(Near(AdvanceAttack(0, .1f, .3f, true), .0001f));
    CHECK(Near(AdvanceAttack(.1f, .03f, .3f, false), .2f));
    CHECK(Near(AdvanceAttack(.9f, .1f, .3f, false), 0));
    PlayerState state;
    state.age = 0;
    auto p = AnimatePlayer(state);
    CHECK(Near(p.rightArm.rotation.z, .1f));
    CHECK(Near(p.leftArm.rotation.z, -.1f));
    state.speed = 1;
    p = AnimatePlayer(state);
    CHECK(Near(p.rightLeg.rotation.x, 1.4f));
    CHECK(Near(p.leftLeg.rotation.x, -1.4f));
    state.riding = true;
    p = AnimatePlayer(state);
    CHECK(Near(p.rightLeg.rotation.x, -1.4137167f));
    state.riding = false;
    state.crouching = true;
    state.attack = .3f;
    p = AnimatePlayer(state);
    CHECK(Near(p.head.offset.y, -4.2f));
    CHECK(Near(p.body.rotation.x, .5f));
    CHECK(std::abs(p.body.rotation.y) > .01f);
    attachable::GeometryLibrary lib;
    lib.Load(R"({"minecraft:geometry":[{"description":{"identifier":"geometry.test"},"bones":[
      {"name":"root"},{"name":"waist","parent":"root"},
      {"name":"body","parent":"waist","pivot":[0,24,0]},
      {"name":"head","parent":"body","pivot":[0,24,0]},
      {"name":"rightarm","parent":"body","pivot":[-5,22,0]},
      {"name":"rightsleeve","parent":"rightarm","pivot":[-5,22,0]},
      {"name":"leftarm","parent":"body","pivot":[5,22,0]},
      {"name":"rightleg","parent":"root","pivot":[-1.9,12,0]},
      {"name":"leftleg","parent":"root","pivot":[1.9,12,0]}]}]})");
    const auto* baseGeometry = lib.Find("geometry.test");
    CHECK(baseGeometry);
    for (bool armor : {false, true}) {
        attachable::Geometry geometry = *baseGeometry;
        if (armor) {
            int torso = 0;
            for (size_t i = 0; i < geometry.bones.size(); ++i)
                if (geometry.bones[i].name == "body")
                    torso = int(i);
            for (auto& bone : geometry.bones)
                if (bone.name == "rightleg" || bone.name == "leftleg")
                    bone.parent = torso;
        } else {
            for (auto& bone : geometry.bones)
                if (bone.name == "rightarm" || bone.name == "leftarm")
                    bone.pivot.y -= .5f;
        }
        const auto* g = &geometry;
        for (float swim : {0.0f, .3f, 1.0f})
            for (float phase : {0.0f, 13.999f, 14.001f, 21.999f, 22.001f, 25.999f}) {
                state.swim = swim;
                state.walk = phase;
                state.headYaw = 50;
                state.pitch = 35;
                p = AnimatePlayer(state);
                attachable::Pose pose;
                pose["root"].rotation = {20, 30, 40};
                pose["rightsleeve"].rotation = {60, 0, 0};
                CHECK(ApplyPlayerPose(p, g, pose));
                std::array<std::pair<const char*, const Part*>, 6> parts = {{{"head", &p.head},
                                                                             {"body", &p.body},
                                                                             {"rightarm", &p.rightArm},
                                                                             {"leftarm", &p.leftArm},
                                                                             {"rightleg", &p.rightLeg},
                                                                             {"leftleg", &p.leftLeg}}};
                for (auto [name, part] : parts) {
                    glm::mat4 actual;
                    glm::vec3 pivot;
                    CHECK(attachable::BoneWorld(*g, pose, name, actual, pivot));
                    const glm::mat4 expected = glm::translate(glm::mat4(1), part->offset + pivot) *
                                               JavaRotation(part->rotation) *
                                               glm::translate(glm::mat4(1), -pivot);
                    for (int i = 0; i < 4; ++i)
                        for (int j = 0; j < 4; ++j)
                            CHECK(Near(actual[i][j], expected[i][j], .003f));
                }
                glm::mat4 arm, sleeve;
                glm::vec3 pivot;
                attachable::BoneWorld(*g, pose, "rightarm", arm, pivot);
                attachable::BoneWorld(*g, pose, "rightsleeve", sleeve, pivot);
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j)
                        CHECK(Near(arm[i][j], sleeve[i][j]));
            }
    }
    p = {};
    p.head.rotation = {0.3f, 1.5707963268f, -0.2f};
    attachable::Pose singularPose;
    CHECK(ApplyPlayerPose(p, baseGeometry, singularPose));
    glm::mat4 actual;
    glm::vec3 pivot;
    attachable::BoneWorld(*baseGeometry, singularPose, "head", actual, pivot);
    auto expected = glm::translate(glm::mat4(1), pivot) * JavaRotation(p.head.rotation) *
                    glm::translate(glm::mat4(1), -pivot);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            CHECK(Near(actual[i][j], expected[i][j], .003f));
    state = {};
    state.usingItem = true;
    state.right = ArmPose::Bow;
    p = AnimatePlayer(state);
    CHECK(Near(p.rightArm.rotation.x, -1.5707964f));
    CHECK(Near(p.leftArm.rotation.x, -1.5707964f));
    state.right = ArmPose::Spyglass;
    state.pitch = 90;
    p = AnimatePlayer(state);
    CHECK(Near(p.rightArm.rotation.z, 0));
    attachable::Pose empty;
    CHECK(!ApplyPlayerPose(p, nullptr, empty));
    std::cout << (failures ? "FAIL " : "PASS ")
              << "entity animation (20 Hz timing, body/head rotation, player poses and bone hierarchy)\n";
    return failures ? 1 : 0;
}
