#include "client/player/PerlinCollisionCache.hpp"

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
        if (!(row >> kind >> std::hex) || kind != 'Q')
            return 1;
        std::array<uint32_t, 28> words{};
        for (auto& word : words)
            if (!(row >> word))
                return 1;
        if (words[16] > 1 || words[26] > 1 || words[27] > 32)
            return 1;
        std::vector<std::array<uint32_t, 6>> expected(words[27]), visited;
        for (auto& box : expected)
            for (auto& word : box)
                if (!(row >> word))
                    return 1;
        std::string extra;
        if (row >> extra)
            return 1;
        const auto f = [&](int i) { return std::bit_cast<float>(words[i]); };
        const auto search = SearchMovement({f(1), f(2), f(3), f(4), f(5), f(6)}, {f(7), f(8), f(9)}, f(0));
        CollisionCache cache;
        cache.region = {f(10), f(11), f(12), f(13), f(14), f(15)};
        cache.preserveHorizontalMotion = words[16] != 0;
        cache.shapes = {{0, 0, 0, 1, 1, 1}, {2, 2, 2, 3, 3, 3}};
        cache.blocks = {{0x123456789ULL, {}}, {0x987654321ULL, {}}};
        const auto oldShapes = cache.shapes;
        RefreshCollisionCache(cache, search.region, [&](const CollisionBox& box) {
            std::array<uint32_t, 6> bits{};
            for (int axis = 0; axis < 6; ++axis)
                bits[axis] = std::bit_cast<uint32_t>(box[axis]);
            visited.push_back(bits);
            return std::vector<CollisionRecord>{};
        });
        bool equal = visited == expected && cache.preserveHorizontalMotion == bool(words[26]) &&
                     cache.shapes == oldShapes && cache.blocks.size() == 2 &&
                     cache.blocks[0].block == 0x123456789ULL && cache.blocks[1].block == 0x987654321ULL;
        for (int axis = 0; axis < 3; ++axis)
            equal &= std::bit_cast<uint32_t>(search.movement[axis]) == words[17 + axis];
        for (int axis = 0; axis < 6; ++axis)
            equal &= std::bit_cast<uint32_t>(cache.region[axis]) == words[20 + axis];
        if (!equal) {
            if (failures < 20)
                std::fprintf(stderr, "Cache refresh case %d differs from BDS\n", cases);
            ++failures;
        }
        ++cases;
    }
    if (!input.eof() || cases != 512)
        return 1;
    std::printf("BDS cache refresh: %d cases, %d mismatches (bit-exact)\n", cases, failures);
    return failures ? 1 : 0;
}
