#include "world/movement/PlayerPhysics.hpp"
#include <cstdio>
#include <cstring>
#include <random>
using namespace perlin;
using namespace perlin::physics;

namespace {
    uint64_t H = 1469598103934665603ull;
    template <class T> void Mix(const T& v) {
        unsigned char b[sizeof(T)];
        std::memcpy(b, &v, sizeof(T));
        for (auto c : b) {
            H ^= c;
            H *= 1099511628211ull;
        }
    }
    void MixD(double d) {
        Mix(d);
    }

    class FuzzWorld : public World {
    public:
        uint64_t seed;
        int ground;
        BlockShape Query(int x, int y, int z) const override {
            uint64_t h = seed ^ (uint64_t(uint32_t(x)) * 0x9E3779B97F4A7C15ull) ^
                         (uint64_t(uint32_t(y)) * 0xC2B2AE3D27D4EB4Full) ^
                         (uint64_t(uint32_t(z)) * 0x165667B19E3779F9ull);
            h ^= h >> 29;
            h *= 0xBF58476D1CE4E5B9ull;
            h ^= h >> 32;
            BlockShape b;
            const int kind = y < ground ? 1 : int(h % 23);
            switch (kind) {
            case 1:
            case 2:
            case 3:
                b.boxes.push_back({0, 0, 0, 1, 1, 1});
                b.isAir = false;
                b.friction = (h >> 8) % 5 == 0 ? 0.98f : 0.6f;
                b.surface = (h >> 12) % 6;
                break;
            case 4:
                b.fluid = 1;
                b.fluidLevel = 1.0f;
                b.fluidDepth = 0;
                b.isAir = false;
                break;
            case 5:
                b.fluid = 2;
                b.fluidLevel = 1.0f;
                b.fluidDepth = 0;
                b.isAir = false;
                break;
            case 6:
                b.climbable = true;
                b.isAir = false;
                break;
            case 7:
                b.web = true;
                b.isAir = false;
                break;
            case 8:
                b.powderSnow = true;
                b.isAir = false;
                break;
            case 9:
                b.honey = true;
                b.speedFactor = 0.4f;
                b.isAir = false;
                b.boxes.push_back({0.0625, 0, 0.0625, 0.9375, 0.9375, 0.9375});
                break;
            case 10:
                b.boxes.push_back({0, 0, 0, 1, 0.5, 1});
                b.isAir = false;
                break;
            case 11:
                b.boxes.push_back({0, 0, 0, 1, 0.0625, 1});
                b.isAir = false;
                b.surface = 1;
                break;
            default:
                break;
            }
            return b;
        }
    };
}

int main(int argc, char** argv) {
    for (int a = 1; a < argc; ++a) {
        const unsigned seed = unsigned(std::atoi(argv[a]));
        std::mt19937_64 r(seed * 7919u + 13);
        auto rb = [&](int n) { return int(r() % n) == 0; };
        auto rf = [&](float lo, float hi) { return lo + float(r() % 100001) / 100000.0f * (hi - lo); };
        FuzzWorld w;
        w.seed = r();
        w.ground = int(r() % 6) + 60;
        State s;
        s.x = rf(-5, 5);
        s.y = w.ground + rf(0, 3);
        s.z = rf(-5, 5);
        s.yaw = rf(-180, 180);
        Attributes at;
        at.speed = rf(0.05f, 0.2f);
        at.flySpeed = rf(0.02f, 0.2f);
        if (rb(4))
            at.airDrag = rf(0.9f, 1.0f);
        if (rb(5))
            at.jumpHeight = rf(0.2f, 1.5f);
        at.jumpBoost = rb(6) ? int(r() % 4) : 0;
        at.levitation = rb(8) ? int(r() % 3) : 0;
        at.slowFalling = rb(6);
        at.soulSpeed = rb(5);
        at.dolphin = rb(5);
        at.waterWalkSpeedEnchant = rb(6) ? int(r() % 4) : 0;
        if (rb(5))
            at.airSpeed = rf(0.01f, 0.05f);
        if (rb(5))
            at.waterDrag = rf(0.5f, 1.0f);
        at.currentMovementMath = !rb(8);
        const int mode = int(r() % 5);
        for (int t = 0; t < 200; ++t) {
            Input in;
            if (!rb(4)) {
                in.forward = rf(-1, 1);
                in.strafe = rf(-1, 1);
            }
            in.jump = rb(4);
            in.sneak = rb(7);
            in.sprint = rb(4);
            if (mode == 1 || (mode == 2 && t % 40 < 20)) {
                in.flying = true;
                in.flyToggled = rb(6);
                in.flyUp = rb(3);
                in.flyDown = rb(4);
                in.flyUpSlow = rb(8);
                in.flyDownSlow = rb(8);
                in.creativeFlight = rb(2);
            }
            if (mode == 3 && rb(2))
                in.flyToggled = true;
            in.noClip = rb(15) && mode != 0;
            in.swimming = rb(6);
            in.headInWater = rb(3);
            in.lookY = rf(-1, 1);
            in.gliding = rb(8);
            in.glideLift = rb(12);
            in.pitch = rf(-90, 90);
            in.crawl = rb(10);
            in.forceCrouch = rb(12);
            in.overDescendableBlock = rb(10);
            in.inAscendableBlock = rb(10);
            in.canStandOnPowderSnow = rb(3);
            in.canClimb = !rb(6);
            in.wallClimbing = rb(10);
            in.pushTowardsClosestSpace = rb(5);
            in.actorMovementFlags = uint8_t(r());
            in.gravity.hasGravity = !rb(10);
            in.gravity.inScaffolding = rb(10);
            in.gravity.overScaffolding = rb(10);
            in.gravity.descendThroughBlock = rb(10);
            if (rb(10))
                in.mobIntent = bds::Float3{rf(-1, 1), 0.0f, rf(-1, 1)};
            if (rb(20)) {
                s.hasTeleported = true;
            }
            s.yaw += rf(-20, 20);
            Step(s, in, at, w);
            s.hasTeleported = false;
            MixD(s.x);
            MixD(s.y);
            MixD(s.z);
            MixD(s.vx);
            MixD(s.vy);
            MixD(s.vz);
            Mix(s.yaw);
            Mix(s.onGround);
            Mix(s.horizontalCollision);
            Mix(s.horizontalCollisionX);
            Mix(s.horizontalCollisionZ);
            Mix(s.verticalCollision);
            Mix(s.collision);
            Mix(s.collidableMobNear);
            Mix(s.inWater);
            Mix(s.inLava);
            Mix(s.waterSplashRequest.has_value());
            Mix(s.waterSplashRequest.value_or(0));
            Mix(s.onClimbable);
            Mix(s.crawling);
            Mix(s.swimming);
            Mix(s.gliding);
            Mix(s.spinAttack);
            Mix(s.spinTicks);
            Mix(s.collisionHeight);
            Mix(s.swimAmount);
            Mix(s.jumpDelay);
            Mix(s.stuckX);
            Mix(s.stuckY);
            Mix(s.stuckZ);
            Mix(s.requestX);
            Mix(s.requestZ);
            Mix(s.requestFromX);
            Mix(s.requestFromZ);
            Mix(s.fallDistance);
            for (auto& e : s.movementEffects) {
                Mix(e.tick);
                Mix(e.remaining);
                Mix(e.type);
                Mix(e.server);
            }
            Mix(s.previousRotation.pitch);
            Mix(s.previousRotation.yaw);
            Mix(s.previousJumpPosition);
            Mix(s.recovery.flags);
            Mix(s.recovery.hasHistory);
            Mix(s.canAlwaysAutoStep);
            Mix(s.hasBox);
            Mix(s.box);
            if (s.y < 0) {
                s.y = w.ground + 2;
                s.vy = 0;
            }
        }
        std::printf("%u %016llx\n", seed, (unsigned long long)H);
    }
}
