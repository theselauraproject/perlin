#include "client/player/PerlinMovementEffects.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace perlin::physics::bds;

int main(int argc, char** argv) {
    if (argc != 2)
        return 1;
    std::ifstream input(argv[1]);
    if (!input)
        return 1;
    int ticks = 0, modifiers = 0, applications = 0, failures = 0;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        char kind;
        row >> kind >> std::hex;
        const int count = kind == 'T' ? 16 : kind == 'S' ? 5 : kind == 'A' ? 14 : 0;
        if (!count)
            return 1;
        std::array<uint32_t, 16> words{};
        for (int i = 0; i < count; ++i)
            if (!(row >> words[i]))
                return 1;
        std::string extra;
        if (row >> extra)
            return 1;
        const auto effect = [&](int offset) {
            return MovementEffect{uint64_t(words[offset]) | (uint64_t(words[offset + 1]) << 32),
                                  std::bit_cast<int32_t>(words[offset + 2]),
                                  std::bit_cast<int32_t>(words[offset + 3])};
        };
        const auto check = [&](uint32_t actual, int expected) {
            if (actual != words[expected]) {
                if (failures < 20)
                    std::fprintf(stderr, "Case %d (%c), field %d: %08x != BDS %08x\n",
                                 ticks + modifiers + applications, kind, expected, actual, words[expected]);
                ++failures;
            }
        };
        const auto checkEffects = [&](const MovementEffects& effects, int offset) {
            for (const auto& actual : effects) {
                check(static_cast<uint32_t>(actual.tick), offset++);
                check(static_cast<uint32_t>(actual.tick >> 32), offset++);
                check(std::bit_cast<uint32_t>(actual.remaining), offset++);
                check(std::bit_cast<uint32_t>(actual.type), offset++);
            }
        };
        if (kind == 'T') {
            MovementEffects effects{effect(0), effect(4)};
            TickMovementEffects(effects);
            checkEffects(effects, 8);
            ++ticks;
        } else if (kind == 'S') {
            MovementEffects effects;
            if (words[1] > 1)
                effects[1] = {0, std::bit_cast<int32_t>(words[3]), std::bit_cast<int32_t>(words[2])};
            check(std::bit_cast<uint32_t>(SwimSpeedMultiplier(words[0] != 0, effects)), 4);
            ++modifiers;
        } else {
            if (words[0] > 1)
                return 1;
            MovementEffects effects;
            effects[words[0]] = {0xABCDEF01DEADBEEF, std::bit_cast<int32_t>(words[1]),
                                 std::bit_cast<int32_t>(words[2])};
            const uint64_t tick = uint64_t(words[4]) | (uint64_t(words[5]) << 32);
            ApplyMovementEffect(effects,
                                {tick, std::bit_cast<int32_t>(words[3]), static_cast<int32_t>(words[0])});
            checkEffects(effects, 6);
            ++applications;
        }
    }
    if (!input.eof() || ticks != 1296 || modifiers != 288 || applications != 648)
        return 1;
    std::printf("BDS movement effects: %d cases, %d mismatching fields (exact)\n",
                ticks + modifiers + applications, failures);
    return failures ? 1 : 0;
}
