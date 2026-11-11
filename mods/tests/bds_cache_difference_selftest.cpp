#include "client/player/PerlinCacheDifference.hpp"

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
        if (!(row >> kind >> std::hex) || kind != 'E')
            return 1;
        auto readBox = [&](CollisionBox& box) {
            for (float& value : box) {
                uint32_t word;
                if (!(row >> word))
                    return false;
                value = std::bit_cast<float>(word);
            }
            return true;
        };
        CollisionBox query, cached;
        uint32_t count;
        if (!readBox(query) || !readBox(cached) || !(row >> count) || count > 32)
            return 1;
        std::vector<CollisionBox> expected(count);
        for (auto& box : expected)
            if (!readBox(box))
                return 1;
        std::string extra;
        if (row >> extra)
            return 1;
        const auto actual = CacheRegionDifference(query, cached);
        bool equal = actual.size() == expected.size();
        for (size_t i = 0; i < actual.size() && i < expected.size(); ++i)
            for (int axis = 0; axis < 6; ++axis)
                equal &=
                    std::bit_cast<uint32_t>(actual[i][axis]) == std::bit_cast<uint32_t>(expected[i][axis]);
        if (!equal) {
            if (failures < 20)
                std::fprintf(stderr, "Cache difference case %d differs from BDS (%zu/%zu boxes)\n", cases,
                             actual.size(), expected.size());
            ++failures;
        }
        ++cases;
    }
    if (!input.eof() || cases != 569)
        return 1;
    std::printf("BDS cache difference: %d cases, %d mismatches (bit-exact)\n", cases, failures);
    return failures ? 1 : 0;
}
