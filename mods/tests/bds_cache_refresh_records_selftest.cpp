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
    int cases = 0, failures = 0, constructors = 0;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        char kind;
        if (!(row >> kind >> std::hex))
            return 1;
        if (kind == 'I') {
            const CollisionCache cache;
            for (int axis = 0; axis < 6; ++axis) {
                uint32_t expected;
                if (!(row >> expected))
                    return 1;
                if (std::bit_cast<uint32_t>(cache.region[axis]) != expected)
                    ++failures;
            }
            std::string extra;
            if (row >> extra)
                return 1;
            ++constructors;
            continue;
        }
        if (kind != 'R')
            return 1;
        std::array<uint32_t, 19> words{};
        std::array<uint32_t, 11> result{};
        for (auto& word : words)
            if (!(row >> word))
                return 1;
        for (auto& word : result)
            if (!(row >> word))
                return 1;
        if (words[17] > 2 || words[18] > 1 || result[9] > 1 || result[10] > 32)
            return 1;
        std::vector<std::array<uint32_t, 6>> expectedQueries(result[10]), visited;
        for (auto& query : expectedQueries)
            for (auto& word : query)
                if (!(row >> word))
                    return 1;
        uint32_t outputCount;
        if (!(row >> outputCount) || outputCount > 128)
            return 1;
        std::vector<std::array<uint32_t, 6>> expectedBoxes(outputCount);
        std::vector<CachedBlockMetadata> expectedBlocks(outputCount);
        for (size_t i = 0; i < outputCount; ++i) {
            for (auto& word : expectedBoxes[i])
                if (!(row >> word))
                    return 1;
            std::array<uint32_t, 3> pos;
            if (!(row >> expectedBlocks[i].block >> pos[0] >> pos[1] >> pos[2]))
                return 1;
            expectedBlocks[i].position = {std::bit_cast<int32_t>(pos[0]), std::bit_cast<int32_t>(pos[1]),
                                          std::bit_cast<int32_t>(pos[2])};
        }
        std::string extra;
        if (row >> extra)
            return 1;
        const auto f = [&](int i) { return std::bit_cast<float>(words[i]); };
        const auto search = SearchMovement({f(2), f(3), f(4), f(5), f(6), f(7)}, {f(8), f(9), f(10)}, f(1));
        CollisionCache cache;
        cache.region = {f(11), f(12), f(13), f(14), f(15), f(16)};
        cache.preserveHorizontalMotion = words[18] != 0;
        for (uint32_t i = 0; i < words[17]; ++i) {
            const float low = static_cast<float>(i), high = static_cast<float>(i + 1);
            cache.shapes.push_back({low, low, low, high, high, high});
            cache.blocks.push_back({0x98700000000ULL + i, {-1, static_cast<int32_t>(i), 100}});
        }
        const auto pattern = words[0];
        RefreshCollisionCache(cache, search.region, [&](const CollisionBox& box) {
            const uint32_t ordinal = static_cast<uint32_t>(visited.size());
            std::array<uint32_t, 6> query{};
            for (int axis = 0; axis < 6; ++axis)
                query[axis] = std::bit_cast<uint32_t>(box[axis]);
            visited.push_back(query);
            std::vector<CollisionRecord> records;
            for (uint32_t i = 0; i < (pattern / 8 + pattern + ordinal) % 4; ++i) {
                const uint64_t block =
                    (pattern + i) % 3 ? 0x12300000000ULL + ((ordinal + 1) << 8) + i + 1 : 0;
                records.push_back({box,
                                   {block,
                                    {static_cast<int32_t>(ordinal) - 3, static_cast<int32_t>(i) * 5 - 7,
                                     static_cast<int32_t>(pattern) - 128}},
                                   (pattern + ordinal + i) % 3 == 0});
            }
            return records;
        });
        bool equal = visited == expectedQueries && cache.preserveHorizontalMotion == bool(result[9]) &&
                     cache.shapes.size() == outputCount && cache.blocks.size() == outputCount;
        for (int axis = 0; axis < 3; ++axis)
            equal &= std::bit_cast<uint32_t>(search.movement[axis]) == result[axis];
        for (int axis = 0; axis < 6; ++axis)
            equal &= std::bit_cast<uint32_t>(cache.region[axis]) == result[axis + 3];
        if (cache.shapes.size() == outputCount && cache.blocks.size() == outputCount) {
            for (size_t i = 0; i < outputCount; ++i) {
                for (int axis = 0; axis < 6; ++axis)
                    equal &= std::bit_cast<uint32_t>(cache.shapes[i][axis]) == expectedBoxes[i][axis];
                const auto& actual = cache.blocks[i];
                const auto& expected = expectedBlocks[i];
                equal &= actual.block == expected.block && actual.position == expected.position;
            }
        }
        if (!equal) {
            if (failures == 0) {
                for (size_t q = 0; q < std::min(visited.size(), expectedQueries.size()); ++q)
                    for (int axis = 0; axis < 6; ++axis)
                        if (visited[q][axis] != expectedQueries[q][axis])
                            std::fprintf(stderr, "query %zu lane %d: %08x/%08x\n", q, axis, visited[q][axis],
                                         expectedQueries[q][axis]);
                for (int axis = 0; axis < 6; ++axis)
                    if (std::bit_cast<uint32_t>(cache.region[axis]) != result[axis + 3])
                        std::fprintf(stderr, "region lane %d: %08x/%08x\n", axis,
                                     std::bit_cast<uint32_t>(cache.region[axis]), result[axis + 3]);
            }
            if (failures < 20)
                std::fprintf(stderr,
                             "Nonempty cache refresh case %d differs from BDS (queries %zu/%zu, shapes "
                             "%zu/%u, flag %d/%u)\n",
                             cases, visited.size(), expectedQueries.size(), cache.shapes.size(), outputCount,
                             cache.preserveHorizontalMotion, result[9]);
            ++failures;
        }
        ++cases;
    }
    if (!input.eof() || cases != 256 || constructors != 1)
        return 1;
    std::printf("BDS nonempty cache refresh: %d cases and constructor, %d mismatches (bit-exact)\n", cases,
                failures);
    return failures ? 1 : 0;
}
