#include "client/player/PerlinRelativeMovement.hpp"

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
    int sineCases = 0, cosineCases = 0, movementCases = 0, failures = 0;
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
            const int count = fixture == 1 ? (kind == 'S' ? 3 : kind == 'C' ? 5 : 0) : (kind == 'M' ? 12 : 0);
            if (!count)
                return 1;
            std::array<uint32_t, 12> words{};
            for (int i = 0; i < count; ++i)
                if (!(row >> words[i]))
                    return 1;
            std::string extra;
            if (row >> extra)
                return 1;
            const auto check = [&](float actual, int expected) {
                const auto got = std::bit_cast<uint32_t>(actual);
                if (got != words[expected]) {
                    if (failures < 30)
                        std::fprintf(stderr, "%c input %08x, field %d: %08x != BDS %08x\n", kind,
                                     words[kind == 'M' ? 1 : 0], expected, got, words[expected]);
                    ++failures;
                }
            };
            const auto f = [&](int index) { return std::bit_cast<float>(words[index]); };
            const float angle = f(0);
            if (kind == 'S') {
                const float actual = Sine(angle);
                check(actual, 1);
                check(actual, 2);
                ++sineCases;
            } else if (kind == 'C') {
                const auto actual = SinCos(angle);
                check(actual.sine, 1);
                check(actual.cosine, 2);
                check(actual.sine, 3);
                check(actual.cosine, 4);
                ++cosineCases;
            } else {
                const auto actual = RelativeMovement({f(6), f(7), f(8)}, f(1), {f(2), f(3), f(4)}, f(5));
                check(actual.x, 9);
                check(actual.y, 10);
                check(actual.z, 11);
                ++movementCases;
            }
        }
        if (!input.eof())
            return 1;
    }
    if (sineCases != 3624 || cosineCases != 3624 || movementCases != 3584)
        return 1;
    std::printf("BDS trig/relative movement: %d cases, %d mismatching fields (bit-exact)\n",
                sineCases + cosineCases + movementCases, failures);
    return failures ? 1 : 0;
}
