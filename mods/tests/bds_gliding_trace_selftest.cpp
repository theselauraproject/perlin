#include "client/player/PlayerPhysics.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace perlin::physics;

struct EmptyWorld : World {
    BlockShape Query(int, int, int) const override { return {}; }
};

int main(int argc, char** argv) {
    if (argc != 2)
        return 1;
    std::ifstream input(argv[1]);
    if (!input)
        return 1;
    EmptyWorld world;
    State state;
    float referenceGlideFall = 0, expectedIntegratedFall = 0;
    int scenarios = 0, ticks = 0, failures = 0;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        char kind;
        row >> kind >> std::hex;
        const int fields = kind == 'B' ? 7 : kind == 'C' ? 15 : 0;
        if (!fields)
            return 1;
        std::array<uint32_t, 15> words{};
        for (int i = 0; i < fields; ++i)
            if (!(row >> words[i]))
                return 1;
        std::string extra;
        if (row >> extra)
            return 1;
        const auto f = [&](int index) { return std::bit_cast<float>(words[index]); };
        if (kind == 'B') {
            if (words[0] != static_cast<uint32_t>(scenarios) || ticks != scenarios * 96)
                return 1;
            state = {};
            state.y = 1000;
            state.vx = f(1);
            state.vy = f(2);
            state.vz = f(3);
            state.fallDistance = f(4);
            referenceGlideFall = expectedIntegratedFall = f(4);
            state.previousRotation = {f(5), f(6)};
            ++scenarios;
            continue;
        }
        if (!scenarios)
            return 1;
        const auto check = [&](uint32_t actual, int expected) {
            if (actual != words[expected]) {
                if (failures < 20)
                    std::fprintf(stderr, "Tick %d, field %d: %08x != BDS %08x\n", ticks, expected, actual,
                                 words[expected]);
                ++failures;
            }
        };
        Input controls;
        controls.gliding = true;
        controls.pitch = f(1);
        state.yaw = f(2);
        if (words[3])
            controls.movementEffects.push_back(
                {uint64_t(words[5]) | (uint64_t(words[6]) << 32), std::bit_cast<int32_t>(words[4]), 0});
        Attributes attributes;
        attributes.slowFalling = words[0] != 0;
        if (static_cast<float>(state.vy) > -0.5f)
            referenceGlideFall = expectedIntegratedFall = 1.0f;
        check(std::bit_cast<uint32_t>(referenceGlideFall), 10);
        expectedIntegratedFall = f(8) < 0 ? expectedIntegratedFall - f(8) : 0.0f;
        Step(state, controls, attributes, world);
        check(std::bit_cast<uint32_t>(static_cast<float>(state.vx)), 7);
        check(std::bit_cast<uint32_t>(static_cast<float>(state.vy)), 8);
        check(std::bit_cast<uint32_t>(static_cast<float>(state.vz)), 9);
        if (std::bit_cast<uint32_t>(state.fallDistance) != std::bit_cast<uint32_t>(expectedIntegratedFall)) {
            if (failures < 20)
                std::fprintf(stderr, "Tick %d: integrated fall-distance mismatch\n", ticks);
            ++failures;
        }
        const auto& effect = state.movementEffects[0];
        check(static_cast<uint32_t>(effect.tick), 11);
        check(static_cast<uint32_t>(effect.tick >> 32), 12);
        check(std::bit_cast<uint32_t>(effect.remaining), 13);
        check(std::bit_cast<uint32_t>(effect.type), 14);
        ++ticks;
    }
    if (!input.eof() || scenarios != 3 || ticks != 288)
        return 1;
    std::printf("BDS chained glide integration: %d ticks, %d mismatching fields (exact)\n", ticks, failures);
    return failures ? 1 : 0;
}
