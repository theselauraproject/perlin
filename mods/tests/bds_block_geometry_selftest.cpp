#include "client/player/PerlinBlockGeometry.hpp"

#include <bit>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
        uint32_t mode, count, hasQuery;
        if (!(row >> kind >> std::hex >> mode >> count >> hasQuery) || kind != 'G' || mode > 2 ||
            count > 32 || hasQuery > 1)
            return 1;
        std::array<uint32_t, 18> words{};
        for (auto& word : words)
            if (!(row >> word))
                return 1;
        const auto f = [&](int i) { return std::bit_cast<float>(words[i]); };
        const Float3 offset{f(0), f(1), f(2)};
        const BlockPosition position{std::bit_cast<int32_t>(words[3]), std::bit_cast<int32_t>(words[4]),
                                     std::bit_cast<int32_t>(words[5])};
        const CollisionBox fallback{f(6), f(7), f(8), f(9), f(10), f(11)};
        const CollisionBox query{f(12), f(13), f(14), f(15), f(16), f(17)};
        auto readBox = [&](CollisionBox& box) {
            for (float& value : box) {
                uint32_t word;
                if (!(row >> word))
                    return false;
                value = std::bit_cast<float>(word);
            }
            return true;
        };
        std::vector<CollisionBox> shapes(count), actual;
        for (auto& box : shapes)
            if (!readBox(box))
                return 1;
        uint32_t expectedReturn;
        if (!(row >> expectedReturn >> count) || expectedReturn > 1 || count > 32)
            return 1;
        std::vector<CollisionBox> expected(count);
        for (auto& box : expected)
            if (!readBox(box))
                return 1;
        std::string extra;
        if (row >> extra)
            return 1;
        const auto emit = [&](const CollisionBox& box) { actual.push_back(box); };
        const auto filter = hasQuery ? &query : nullptr;
        const bool returned =
            mode ? CollectBlockComponentShapes(mode == 2, offset, position, shapes, filter, emit)
                 : CollectFallbackBlockShape(fallback, filter, emit);
        bool equal = actual.size() == expected.size() && returned == bool(expectedReturn);
        for (size_t i = 0; i < actual.size() && i < expected.size(); ++i)
            for (int axis = 0; axis < 6; ++axis)
                equal &=
                    std::bit_cast<uint32_t>(actual[i][axis]) == std::bit_cast<uint32_t>(expected[i][axis]);
        if (!equal) {
            if (failures < 20)
                std::fprintf(stderr, "Block geometry case %d differs from BDS\n", cases);
            ++failures;
        }
        ++cases;
    }
    if (!input.eof() || cases != 310)
        return 1;
    std::printf("BDS block geometry: %d cases, %d mismatches (bit-exact)\n", cases, failures);
    return failures ? 1 : 0;
}
