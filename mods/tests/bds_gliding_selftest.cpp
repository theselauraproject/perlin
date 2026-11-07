#include "client/player/PerlinGliding.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace perlin::physics::bds;

int main(int argc, char** argv) {
    if (argc != 3)
        return 1;
    int rotations = 0, glides = 0, indices = 0, failures = 0;
    for (int fixture = 1; fixture < argc; ++fixture) {
        std::ifstream input(argv[fixture]);
        if (!input)
            return 1;
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream row(line);
            char kind;
            row >> kind >> std::hex;
            if (fixture == 1 ? kind != 'R' && kind != 'J' : kind != 'G')
                return 1;
            const int count = kind == 'R' ? 8 : kind == 'J' ? 5 : 14;
            std::array<uint32_t, 14> words{};
            for (int i = 0; i < count; ++i)
                if (!(row >> words[i]))
                    return 1;
            std::string extra;
            if (row >> extra)
                return 1;
            const auto f = [&](int index) { return std::bit_cast<float>(words[index]); };
            const auto check = [&](float actual, int expected) {
                const auto got = std::bit_cast<uint32_t>(actual);
                if (got != words[expected]) {
                    if (failures < 20)
                        std::fprintf(stderr, "Case %d (%c), field %d: %08x != BDS %08x\n", rotations + glides,
                                     kind, expected, got, words[expected]);
                    ++failures;
                }
            };
            if (kind == 'R') {
                const auto result = DirectionFromRotation({f(0), f(1)}, {f(2), f(3)}, f(4));
                check(result.x, 5);
                check(result.y, 6);
                check(result.z, 7);
                ++rotations;
            } else if (kind == 'J') {
                if (JumpSinIndex(f(0)) != words[1] || JumpCosIndex(f(0)) != words[2])
                    ++failures;
                const auto velocity = ApplySprintJump({0, 0, 0}, f(0));
                check(velocity.x, 3);
                check(velocity.z, 4);
                ++indices;
            } else {
                float fallDistance = f(9);
                const auto result = GlideVelocity({f(6), f(7), f(8)}, {f(4), f(5)}, {f(2), f(3)},
                                                  words[0] != 0, words[1] == 3, fallDistance);
                check(result.x, 10);
                check(result.y, 11);
                check(result.z, 12);
                check(fallDistance, 13);
                ++glides;
            }
        }
        if (!input.eof())
            return 1;
    }
    if (rotations != 836 || glides != 704 || indices != 10)
        return 1;
    std::printf("BDS direction/gliding: %d cases, %d mismatching fields (bit-exact)\n",
                rotations + glides + indices, failures);
    return failures ? 1 : 0;
}
