#include "bedsim/simulator.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace bedsim;

namespace perlin_bds_reference {
    void sprint(float yaw, float& x, float& z) {
        std::cout << "Q " << std::hex << std::bit_cast<uint32_t>(yaw) << ' '
                  << std::bit_cast<uint32_t>(x) << ' ' << std::bit_cast<uint32_t>(z)
                  << std::dec << std::endl;
        uint32_t nextX, nextZ;
        if (!(std::cin >> std::hex >> nextX >> nextZ))
            throw std::runtime_error("Missing native sprint-impulse reply");
        x = std::bit_cast<float>(nextX);
        z = std::bit_cast<float>(nextZ);
    }
}

namespace {
    struct Facts {
        float friction = 0.6f, horizontal = 1, vertical = 1, fluidHeight = 0;
        int flags = 0, surface = 0;
    };
    struct Region { BlockPos min, max; Facts facts; };
    struct TraceWorld {
        Facts base;
        std::vector<AABB> boxes;
        std::vector<Region> regions;
        int jumpBoost = 0, levitation = 0, slowFalling = 0;

        Facts facts(BlockPos position) const {
            auto result = base;
            for (const auto& region : regions)
                if (position.x >= region.min.x && position.x < region.max.x &&
                    position.y >= region.min.y && position.y < region.max.y &&
                    position.z >= region.min.z && position.z < region.max.z)
                    result = region.facts;
            return result;
        }
        BlockMovement movement(BlockPos position) const {
            const auto value = facts(position);
            BlockMovement result;
            result.groundFriction = value.friction;
            result.climbable = value.flags & 1;
            result.cobweb = value.flags & 8;
            result.accelerationFriction = value.surface == 4 ? 1.225f : 1;
            result.bounce = value.surface == 1 ? Bounce::Slime : value.surface == 2 ? Bounce::Bed : Bounce::None;
            return result;
        }
        void blockCollisions(BlockPos position, std::vector<AABB>& out) const {
            const auto cell = AABB::unitAt(position);
            for (const auto& box : boxes)
                if (box.intersects(cell, 0)) out.push_back(box.translate(-Vec3(position)));
        }
        void collisions(const AABB& area, std::vector<AABB>& out) const {
            for (const auto& box : boxes)
                if (box.intersects(area, 0)) out.push_back(box);
        }
        bool isLoaded(const AABB&) const { return true; }
        std::optional<int> effectAmplifier(Effect effect) const {
            if (effect == Effect::JumpBoost && jumpBoost) return jumpBoost - 1;
            if (effect == Effect::Levitation && levitation) return levitation - 1;
            if (effect == Effect::SlowFalling && slowFalling) return 0;
            return {};
        }
    };
    template<class T> void ReadVector(std::istream& input, T& value) { input >> value.x >> value.y >> value.z; }
    void ReadFacts(std::istream& input, Facts& value) {
        input >> value.friction >> value.horizontal >> value.vertical >> value.fluidHeight >> value.flags >> value.surface;
        if (value.flags & ~1 || value.horizontal != 1 || value.vertical != 1 || value.fluidHeight != 0)
            throw std::runtime_error("Unsupported legacy scenario facts");
    }
}

int main(int argc, char** argv) {
    if (argc != 3 || (std::string(argv[2]) != "validate" && std::string(argv[2]) != "replay")) return 2;
    const bool validate = std::string(argv[2]) == "validate";
    std::ifstream file(argv[1]);
    if (!file) return 2;
    TraceWorld world;
    Simulator simulator(world, predictionOptions());
    Body body;
    std::map<std::string, int> frames;
    std::string line, scenario;
    int step = 0, checked = 0, failed = 0;
    while (std::getline(file, line)) {
        std::istringstream input(line);
        std::string tag;
        if (!(input >> tag) || tag.starts_with('#')) continue;
        if (tag == "S") {
            input >> scenario;
            if (scenario != "gen:terrain1_1" && scenario != "gen:floors1_0" &&
                scenario != "gen:ladder1_0" && scenario != "gen:effects1_0") return 2;
            if (frames.contains(scenario)) return 2;
            frames[scenario] = 0;
            step = 0;
            body = {};
            ReadVector(input, body.position);
            ReadVector(input, body.velocity);
            int grounded, count, x, y, z;
            input >> grounded >> body.jumpDelay >> count >> x >> y >> z;
            if (count != 50) return 2;
            body.onGround = grounded;
            body.collideX = x; body.collideY = y; body.collideZ = z;
            body.gravity = 0.08f;
            body.jumpHeight = 0.42f;
            std::cout << "S " << scenario << '\n';
        } else if (tag == "W") {
            world.base = {};
            world.boxes.clear(); world.regions.clear();
            ReadFacts(input, world.base);
            int boxes, regions, unloaded;
            input >> boxes >> regions >> unloaded;
            if (unloaded) return 2;
        } else if (tag == "B") {
            AABB box;
            ReadVector(input, box.min); ReadVector(input, box.max);
            world.boxes.push_back(box);
        } else if (tag == "R") {
            Region region;
            ReadVector(input, region.min); ReadVector(input, region.max); ReadFacts(input, region.facts);
            world.regions.push_back(region);
        } else if (tag == "A") {
            input >> world.jumpBoost >> world.levitation >> world.slowFalling;
        } else if (tag == "I") {
            float forward, strafe, yaw;
            int jump, jumpPressed, sprint, sneak;
            input >> forward >> strafe >> yaw >> jump >> jumpPressed >> sprint >> sneak;
            const float length = std::sqrt(forward * forward + strafe * strafe);
            if (length > 1) { forward /= length; strafe /= length; }
            if (sneak) { forward *= 0.3f; strafe *= 0.3f; }
            body.impulse = {strafe * 0.98f, forward * 0.98f};
            body.rotation.z = yaw;
            body.sprinting = sprint; body.sneaking = sneak;
            body.jumping = body.pressingJump = body.effectiveJumping = jump;
            if (!jump) body.jumpDelay = 0;
            body.movementSpeed = 0.1f * (sprint ? 1.3f : 1.0f);
            body.airSpeed = sprint ? 0.026f : 0.02f;
            body.jumpHeight = 0.42f + world.jumpBoost * 0.1f;
            body.slowFalling = world.slowFalling;
            if (simulator.simulateState(body).outcome != Outcome::Normal) return 2;
            tickState(body, true);
        } else if (tag == "E") {
            Vec3 position, velocity;
            ReadVector(input, position); ReadVector(input, velocity);
            int grounded, x, y, z;
            input >> grounded >> x >> y >> z;
            const float actual[]{body.position.x, body.position.y, body.position.z,
                body.velocity.x, body.velocity.y, body.velocity.z};
            const float expected[]{position.x, position.y, position.z, velocity.x, velocity.y, velocity.z};
            for (int axis = 0; axis < 6; ++axis) {
                ++checked;
                failed += !std::isfinite(actual[axis]) || std::abs(actual[axis] - expected[axis]) > 1e-5f;
            }
            checked += 3;
            failed += body.onGround != bool(grounded);
            failed += body.collideY != bool(y);
            failed += (body.collideX || body.collideZ) != bool(x || z);
            std::cout << "E " << scenario << ' ' << step << ' ' << std::setprecision(9)
                      << body.position.x << ' ' << body.position.y << ' ' << body.position.z << ' '
                      << body.velocity.x << ' ' << body.velocity.y << ' ' << body.velocity.z << ' '
                      << body.onGround << ' ' << body.collideX << ' ' << body.collideY << ' ' << body.collideZ << '\n';
            ++step; ++frames[scenario];
        } else return 2;
        if (input.fail()) return 2;
    }
    if (!file.eof() || checked != 1800 || frames.size() != 4) return 2;
    for (const auto& [name, count] : frames) if (count != 50) return 2;
    std::cerr << "Independent BedSim compatibility replay: " << checked << " checks, " << failed << " differences\n";
    return validate && failed ? 1 : 0;
}
