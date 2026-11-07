#include "client/player/PerlinVerticalMotion.hpp"

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
    int gravityCases = 0, pipelineCases = 0, failures = 0;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        char kind;
        row >> kind >> std::hex;
        const int fields = kind == 'G' ? 7 : kind == 'V' ? 9 : 0;
        if (!fields)
            return 1;
        std::array<uint32_t, 9> words{};
        for (int i = 0; i < fields; ++i)
            if (!(row >> words[i]))
                return 1;
        std::string extra;
        if (row >> extra)
            return 1;
        const auto f = [&](int i) { return std::bit_cast<float>(words[i]); };
        const auto check = [&](uint32_t actual, int expected) {
            if (actual != words[expected]) {
                if (failures < 20)
                    std::fprintf(stderr, "Case %d (%c), field %d: %08x != BDS %08x\n",
                                 gravityCases + pipelineCases, kind, expected, actual, words[expected]);
                ++failures;
            }
        };
        if (kind == 'G') {
            const GravityFlags flags{words[0] != 0, (words[1] & 1) != 0, (words[1] & 2) != 0,
                                     (words[1] & 4) != 0};
            const auto gravity = GroundGravity(f(3), flags, words[2] != 0);
            check(gravity.has_value(), 4);
            check(gravity ? std::bit_cast<uint32_t>(gravity->acceleration) : 0, 5);
            check(gravity && gravity->resetFallDistance, 6);
            ++gravityCases;
        } else {
            const bool descent = (words[2] & 1) != 0;
            const GravityFlags flags{words[1] != 0, descent, false, descent};
            float fallDistance = 11.25f;
            float velocity;
            switch (words[0]) {
            case 0:
                velocity = PostGroundVertical(f(6), fallDistance, flags, words[3], words[4] != 0,
                                              (words[2] & 2) != 0, (words[2] & 4) != 0);
                break;
            case 1:
                velocity = PostWaterVertical(f(6), fallDistance, words[3], words[5] != 0);
                break;
            case 2:
                velocity = FlyingVerticalDrag(f(6), fallDistance);
                break;
            default:
                return 1;
            }
            check(std::bit_cast<uint32_t>(velocity), 7);
            check(std::bit_cast<uint32_t>(fallDistance), 8);
            ++pipelineCases;
        }
    }
    if (!input.eof() || gravityCases != 288 || pipelineCases != 3456)
        return 1;
    std::printf("BDS vertical conformance: %d cases, %d mismatching fields (bit-exact)\n",
                gravityCases + pipelineCases, failures);
    return failures ? 1 : 0;
}
