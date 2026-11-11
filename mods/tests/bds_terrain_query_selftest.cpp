#include "client/player/PerlinWorldCollision.hpp"

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
        uint32_t low, high, mode;
        if (!(row >> kind >> std::hex >> low >> high >> mode) || kind != 'T' || mode > 2)
            return 1;
        CollisionBox bounds;
        for (auto& value : bounds) {
            uint32_t word;
            if (!(row >> word))
                return 1;
            value = std::bit_cast<float>(word);
        }
        uint32_t count;
        if (!(row >> count) || count > 1024)
            return 1;
        std::vector<std::array<uint32_t, 2>> expectedColumns(count), columns;
        for (auto& pos : expectedColumns)
            if (!(row >> pos[0] >> pos[1]))
                return 1;
        if (!(row >> count) || count > 8192)
            return 1;
        std::vector<std::array<uint32_t, 3>> expectedCells(count), cells;
        for (auto& pos : expectedCells)
            if (!(row >> pos[0] >> pos[1] >> pos[2]))
                return 1;
        if (!(row >> count) || count > 1)
            return 1;
        std::vector<std::array<uint32_t, 6>> expectedBoundaries(count), boundaries;
        for (auto& box : expectedBoundaries)
            for (auto& word : box)
                if (!(row >> word))
                    return 1;
        std::string extra;
        if (row >> extra)
            return 1;
        const auto minimum = std::bit_cast<int32_t>(low), maximum = std::bit_cast<int32_t>(high);
        if (minimum < INT16_MIN || minimum > INT16_MAX || maximum < INT16_MIN || maximum > INT16_MAX)
            return 1;
        CollectTerrainCells(
            bounds, {static_cast<int16_t>(minimum), static_cast<int16_t>(maximum)},
            [&](int32_t x, int32_t z) {
                columns.push_back({std::bit_cast<uint32_t>(x), std::bit_cast<uint32_t>(z)});
                return mode == 0 || (mode == 2 && !(((x >> 4) + (z >> 4)) & 1));
            },
            [&](int32_t x, int32_t y, int32_t z) {
                cells.push_back(
                    {std::bit_cast<uint32_t>(x), std::bit_cast<uint32_t>(y), std::bit_cast<uint32_t>(z)});
            },
            [&](const CollisionBox& box) {
                std::array<uint32_t, 6> words{};
                for (int i = 0; i < 6; ++i)
                    words[i] = std::bit_cast<uint32_t>(box[i]);
                boundaries.push_back(words);
            });
        if (columns != expectedColumns || cells != expectedCells || boundaries != expectedBoundaries) {
            if (failures < 20)
                std::fprintf(stderr, "Terrain-query case %d differs from BDS\n", cases);
            ++failures;
        }
        ++cases;
    }
    if (!input.eof() || cases != 382)
        return 1;
    std::printf("BDS terrain query: %d cases, %d mismatches (bit-exact)\n", cases, failures);
    return failures ? 1 : 0;
}
