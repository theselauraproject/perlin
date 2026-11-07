#include "client/player/PerlinWaterDrag.hpp"

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
    int cases = 0, failures = 0, dragCases = 0, accelerationCases = 0;
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
            if (kind != (fixture == 1 ? 'W' : 'A'))
                return 1;
            std::array<uint32_t, 12> words{};
            const int fields = kind == 'W' ? 12 : 10;
            for (int i = 0; i < fields; ++i)
                if (!(row >> words[i]))
                    return 1;
            std::string extra;
            if (row >> extra)
                return 1;
            const auto f = [&](int i) { return std::bit_cast<float>(words[i]); };
            if (kind == 'A') {
                const auto result = WaterAcceleration(
                    words[0] & 1 ? std::optional<float>(f(6)) : std::nullopt,
                    words[0] & 2 ? std::optional<float>(f(7)) : std::nullopt, f(8), f(4),
                    std::bit_cast<int32_t>(words[5]), words[2] != 0, words[3] != 0, words[1] == 2);
                const auto got = std::bit_cast<uint32_t>(result);
                if (got != words[9]) {
                    if (failures < 20)
                        std::fprintf(stderr, "Acceleration case %d: %08x != BDS %08x\n", accelerationCases,
                                     got, words[9]);
                    ++failures;
                }
                ++accelerationCases;
            } else {
                const auto result = WaterDrag({f(6), f(7), f(8)}, words[0] != 0, words[1] != 0, f(4),
                                              std::bit_cast<int32_t>(words[5]),
                                              words[2] ? std::optional<float>(f(3)) : std::nullopt);
                const std::array<float, 3> actual{result.x, result.y, result.z};
                for (int axis = 0; axis < 3; ++axis) {
                    const auto got = std::bit_cast<uint32_t>(actual[axis]);
                    if (got != words[axis + 9]) {
                        if (failures < 20)
                            std::fprintf(stderr, "Case %d, axis %d: %08x != BDS %08x\n", cases, axis, got,
                                         words[axis + 9]);
                        ++failures;
                    }
                }
                ++dragCases;
            }
            ++cases;
        }
        if (!input.eof())
            return 1;
    }
    if (dragCases != 4320 || accelerationCases != 6912)
        return 1;
    std::printf("BDS water conformance: %d cases, %d mismatching fields (bit-exact)\n", cases, failures);
    return failures ? 1 : 0;
}
