#include "client/player/PerlinInsideQuery.hpp"
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
    std::string line;
    int traversal = 0, readiness = 0, failures = 0;
    const auto position = [](std::istringstream& row, BlockPosition& out) {
        for (auto& value : out) {
            uint32_t word;
            if (!(row >> word))
                return false;
            value = std::bit_cast<int32_t>(word);
        }
        return true;
    };
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        char kind;
        if (!(row >> kind >> std::hex))
            return 1;
        bool equal = true;
        if (kind == 'T') {
            std::array<uint32_t, 11> words{};
            for (auto& word : words)
                if (!(row >> word))
                    return 1;
            if (words[0] > 1 || words[1] > 1 || words[9] > 1 || words[10] > 1)
                return 1;
            std::vector<std::array<uint32_t, 6>> expectedRegions(words[10]), regions;
            for (auto& region : expectedRegions)
                for (auto& word : region)
                    if (!(row >> word))
                        return 1;
            uint32_t count;
            if (!(row >> count) || count > 4096)
                return 1;
            std::vector<BlockPosition> expectedCells(count), cells;
            for (auto& cell : expectedCells)
                if (!position(row, cell))
                    return 1;
            uint32_t visits;
            if (!(row >> visits) || visits != count)
                return 1;
            for (uint32_t i = 0; i < visits; ++i) {
                uint64_t block;
                BlockPosition cell;
                if (!(row >> block) || !position(row, cell))
                    return 1;
                if (block != 0x10000000ULL + 0x5000 + ((i + 1) % 8) * 8 || cell != expectedCells[i])
                    return 1;
            }
            const auto f = [&](int i) { return std::bit_cast<float>(words[i]); };
            const bool result = CollectInsideCells(
                {f(3), f(4), f(5), f(6), f(7), f(8)}, f(2), words[0],
                [&](const CollisionBox& box) {
                    std::array<uint32_t, 6> bits;
                    for (int axis = 0; axis < 6; ++axis)
                        bits[axis] = std::bit_cast<uint32_t>(box[axis]);
                    regions.push_back(bits);
                    return words[1] != 0;
                },
                [&](int32_t x, int32_t y, int32_t z) { cells.push_back({x, y, z}); });
            equal = result == bool(words[9]) && regions == expectedRegions && cells == expectedCells;
            ++traversal;
        } else if (kind == 'R') {
            std::array<uint32_t, 10> words{};
            for (auto& word : words)
                if (!(row >> word))
                    return 1;
            if (words[0] > 4 || words[1] > 1 || words[8] > 1 || words[9] > 4096)
                return 1;
            std::vector<std::array<int32_t, 2>> expected(words[9]), visited;
            for (auto& cell : expected)
                for (auto& value : cell) {
                    uint32_t word;
                    if (!(row >> word))
                        return 1;
                    value = std::bit_cast<int32_t>(word);
                }
            const auto f = [&](int i) { return std::bit_cast<float>(words[i]); };
            const bool result =
                InsideRegionReady({f(2), f(3), f(4), f(5), f(6), f(7)}, words[1], [&](int32_t x, int32_t z) {
                    visited.push_back({x, z});
                    ChunkCollisionState chunk;
                    chunk.exists = words[0] != 1 && (words[0] != 2 || !((x + z) & 1)) &&
                                   (words[0] != 4 || visited.size() <= 2);
                    chunk.field5352 = words[0] == 3;
                    return chunk;
                });
            equal = result == bool(words[8]) && expected == visited;
            ++readiness;
        } else
            return 1;
        std::string extra;
        if (row >> extra)
            return 1;
        if (!equal) {
            if (failures < 20)
                std::fprintf(stderr, "Inside query %c case %d differs\n", kind,
                             kind == 'T' ? traversal - 1 : readiness - 1);
            ++failures;
        }
    }
    if (!input.eof() || traversal != 804 || readiness != 512)
        return 1;
    std::printf("BDS inside query: %d traversals, %d readiness cases, %d mismatches (bit-exact)\n", traversal,
                readiness, failures);
    return failures ? 1 : 0;
}
