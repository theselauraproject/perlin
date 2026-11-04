#include "client/player/PerlinFluidState.hpp"

#include <array>
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
    std::string line;
    int cases = 0, failures = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        char kind;
        std::array<uint32_t, 11> words;
        if (!(row >> kind >> std::hex) || kind != 'Q')
            return 1;
        for (auto& word : words)
            if (!(row >> word))
                return 1;
        if (words[7] > 1 || words[9] > 1 || words[10] > 256)
            return 1;
        std::vector<BlockPosition> expectedQueries(words[10]);
        for (auto& position : expectedQueries)
            for (auto& coordinate : position) {
                uint32_t word;
                if (!(row >> word))
                    return 1;
                coordinate = std::bit_cast<int32_t>(word);
            }
        uint32_t recordCount;
        if (!(row >> recordCount) || recordCount > 256)
            return 1;
        std::vector<std::array<uint32_t, 4>> expectedRecords(recordCount);
        for (auto& record : expectedRecords)
            for (auto& word : record)
                if (!(row >> word))
                    return 1;
        std::string extra;
        if (row >> extra)
            return 1;
        CollisionBox bounds;
        for (int i = 0; i < 6; ++i)
            bounds[i] = std::bit_cast<float>(words[i]);
        std::vector<BlockPosition> queries;
        std::vector<std::array<uint32_t, 4>> records;
        const bool found = CollectLiquidCells(
            bounds, words[6], words[7],
            [&](BlockPosition position) {
                queries.push_back(position);
                constexpr std::array<uint32_t, 4> materials{0, 5, 6, 25};
                const auto x = static_cast<uint32_t>(position[0]), y = static_cast<uint32_t>(position[1]),
                           z = static_cast<uint32_t>(position[2]);
                return materials[(x * 3 + y * 5 + z * 7 + words[8]) & 3];
            },
            [&](BlockPosition position, uint8_t faces) {
                records.push_back({static_cast<uint32_t>(position[0]), static_cast<uint32_t>(position[1]),
                                   static_cast<uint32_t>(position[2]), faces});
            });
        if ((found != bool(words[9]) || queries != expectedQueries || records != expectedRecords) &&
            failures++ < 10)
            std::fprintf(stderr, "BDS liquid collector case %d differs\n", cases);
        ++cases;
    }
    if (!input.eof() || cases != 512)
        return 1;
    std::printf("BDS liquid collector: %d cases, %d mismatches (exact outputs and query order)\n", cases,
                failures);
    return failures ? 1 : 0;
}
