#include "client/player/PerlinLanding.hpp"

#include <bit>
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
    int cases = 0, failures = 0;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        char kind;
        if (!(row >> kind >> std::hex) || kind != 'V')
            return 1;
        std::array<uint32_t, 22> words{};
        for (auto& word : words)
            if (!(row >> word))
                return 1;
        std::string extra;
        if (row >> extra || words[0] > 2 || words[1] > 31 || words[20] > 1 || words[21] > 1)
            return 1;
        const auto f = [&](int i) { return std::bit_cast<float>(words[i]); };
        const auto vector = [&](int i) { return Float3{f(i), f(i + 1), f(i + 2)}; };
        const auto flags = words[1];
        std::optional<Float3> rest;
        if (flags & 16)
            rest = vector(8);
        const auto result = VerticalCollision(vector(5), vector(2), static_cast<LandingSurface>(words[0]),
                                              flags & 1, flags & 2, flags & 4, flags & 8, rest);
        const auto impulse = rest.value_or(Float3{});
        const auto velocity = rest ? ApplyRestitution(result.velocity, *rest) : result.velocity;
        const std::array<float, 6> actual{impulse.x,  impulse.y,  impulse.z,
                                          velocity.x, velocity.y, velocity.z};
        for (int i = 0; i < 6; ++i) {
            const auto got = std::bit_cast<uint32_t>(actual[i]);
            if (got != words[11 + i]) {
                if (failures < 20)
                    std::fprintf(stderr, "Case %d field %d: %08x != BDS %08x\n", cases, i, got,
                                 words[11 + i]);
                ++failures;
            }
        }
        const auto pos = result.bouncePosition.value_or(BlockPosition{INT32_MAX, INT32_MAX, INT32_MAX});
        for (int i = 0; i < 3; ++i)
            if (std::bit_cast<uint32_t>(pos[i]) != words[17 + i])
                ++failures;
        if (bool(result.bouncePosition) != bool(words[20]))
            ++failures;
        if (bool(rest) != bool(words[21]))
            ++failures;
        ++cases;
    }
    if (!input.eof() || cases != 1856)
        return 1;
    std::printf("BDS landing conformance: %d cases, %d mismatching fields (bit-exact)\n", cases, failures);
    return failures ? 1 : 0;
}
