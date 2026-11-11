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
        uint32_t mode;
        if (!(row >> kind >> std::hex >> mode) || kind != 'U' || mode > 6)
            return 1;
        CollisionBox bounds;
        for (auto& value : bounds) {
            uint32_t word;
            if (!(row >> word))
                return 1;
            value = std::bit_cast<float>(word);
        }
        uint32_t count;
        if (!(row >> count) || count > 512)
            return 1;
        std::vector<std::array<uint32_t, 2>> expectedVisits(count), visited;
        for (auto& pair : expectedVisits)
            if (!(row >> pair[0] >> pair[1]))
                return 1;
        if (!(row >> count) || count > 512)
            return 1;
        std::vector<std::array<uint32_t, 7>> expectedBarriers(count), barriers;
        for (auto& box : expectedBarriers)
            for (auto& word : box)
                if (!(row >> word))
                    return 1;
        std::string extra;
        if (row >> extra)
            return 1;
        CollectUnavailableChunkBarriers(
            bounds,
            [&](int32_t x, int32_t z) {
                visited.push_back({std::bit_cast<uint32_t>(x), std::bit_cast<uint32_t>(z)});
                uint32_t state =
                    mode == 6 ? (static_cast<uint32_t>(x) + 2 * static_cast<uint32_t>(z)) & 7 : mode;
                if (state == 2)
                    state = (static_cast<uint32_t>(x) + static_cast<uint32_t>(z)) & 1 ? 1 : 0;
                return ChunkCollisionState{state != 1, state == 3 ? 0x8000000080000000ULL : 0,
                                           static_cast<uint8_t>(state == 4),
                                           static_cast<uint8_t>(state == 5)};
            },
            [&](const CollisionBox& box) {
                std::array<uint32_t, 7> words{};
                for (int i = 0; i < 6; ++i)
                    words[i] = std::bit_cast<uint32_t>(box[i]);
                words[6] = 1;
                barriers.push_back(words);
            });
        if (visited != expectedVisits || barriers != expectedBarriers) {
            if (failures < 20)
                std::fprintf(stderr, "Unavailable-chunk case %d differs from BDS\n", cases);
            ++failures;
        }
        ++cases;
    }
    if (!input.eof() || cases != 564)
        return 1;
    std::printf("BDS unavailable-chunk conformance: %d cases, %d mismatches (bit-exact)\n", cases, failures);
    return failures ? 1 : 0;
}
