#include "client/player/PerlinFluidState.hpp"
#include "client/player/PerlinEntityMembership.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace perlin::physics::bds;

struct Pool {
    std::vector<uint32_t> values;
    std::vector<std::span<const uint32_t>> pages;

    Pool(uint32_t entity, uint32_t membership) {
        const auto page = (entity & 0x3FFFF) >> 11;
        pages.resize(page + (membership != 3));
        if (membership == 3 || membership == 4)
            return;
        values.resize(2048, 0xFFFFFFFF);
        uint32_t value = (entity & 0xFFFC0000) | 17;
        if (membership == 0)
            value = 0xFFFFFFFF;
        if (membership == 2)
            value ^= 0x40000;
        if (membership == 5)
            value = (entity & 0xFFFC0000) | 0x3FFFF;
        values[entity & 0x7FF] = value;
        pages[page] = values;
    }
    bool Contains(uint32_t entity) const { return EntityMembership{pages}.Contains(entity); }
};

int main(int argc, char** argv) {
    if (argc != 2)
        return 1;
    std::ifstream input(argv[1]);
    if (!input)
        return 1;
    std::string line;
    int waterCases = 0, lavaCases = 0, chainedCases = 0, materialCases = 0, boundsCases = 0, failures = 0;
    std::array<uint32_t, 4> previousChain{};
    uint32_t chainEntity = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        char kind;
        if (!(row >> kind >> std::hex))
            return 1;
        if (kind == 'W' || kind == 'C') {
            std::array<uint32_t, 18> words;
            for (auto& word : words)
                if (!(row >> word))
                    return 1;
            for (int i = 2; i <= 4; ++i)
                if (words[i] > 5)
                    return 1;
            if (words[5] > 1 || words[6] > 1 || words[8] > 1)
                return 1;
            if (kind == 'C') {
                uint32_t chain, tick;
                if (!(row >> chain >> tick) || chain != uint32_t(chainedCases / 16) ||
                    tick != uint32_t(chainedCases % 16))
                    return 1;
                const std::array current{words[4], words[7], words[8], words[9]};
                if (tick && (current != previousChain || words[0] != chainEntity))
                    return 1;
                chainEntity = words[0];
                ++chainedCases;
            }
            Pool falling(words[0], words[2]), passenger(words[0], words[3]), water(words[0], words[4]);
            bool wasInWater = water.Contains(words[0]);
            const bool previous = wasInWater;
            float fall = std::bit_cast<float>(words[7]);
            std::optional<uint32_t> splash;
            if (words[8])
                splash = words[9];
            uint32_t boatQueries = 0;
            UpdateWaterState(
                words[1], wasInWater, fall, splash,
                [&] {
                    if (!passenger.Contains(words[0]) || !words[5])
                        return false;
                    ++boatQueries;
                    return words[6] != 0;
                },
                [&] { return falling.Contains(words[0]); });
            const std::array<uint32_t, 8> actual{uint32_t(wasInWater),
                                                 std::bit_cast<uint32_t>(fall),
                                                 uint32_t(splash.has_value()),
                                                 splash.value_or(words[9]),
                                                 uint32_t(!previous && wasInWater),
                                                 uint32_t(previous && !wasInWater),
                                                 boatQueries,
                                                 uint32_t(!previous && wasInWater)};
            if (!std::equal(actual.begin(), actual.end(), words.begin() + 10) && failures++ < 10)
                std::fprintf(stderr, "BDS water-state case %d differs\n", waterCases);
            previousChain = {actual[0], actual[1], actual[2], actual[3]};
            if (kind == 'W')
                ++waterCases;
        } else if (kind == 'L') {
            std::array<uint32_t, 6> words;
            for (auto& word : words)
                if (!(row >> word))
                    return 1;
            if (words[2] > 5)
                return 1;
            const bool previous = Pool(words[0], words[2]).Contains(words[0]);
            const bool lava = WasInLava(words[1]);
            const std::array<uint32_t, 3> actual{uint32_t(lava), uint32_t(!previous && lava),
                                                 uint32_t(previous && !lava)};
            if (!std::equal(actual.begin(), actual.end(), words.begin() + 3) && failures++ < 10)
                std::fprintf(stderr, "BDS lava-state case %d differs\n", lavaCases);
            ++lavaCases;
        } else if (kind == 'K') {
            std::array<uint32_t, 4> words;
            for (auto& word : words)
                if (!(row >> word))
                    return 1;
            if (words[0] > 1 || words[1] > 1)
                return 1;
            if (SelectFluidMaterial(words[0], words[1]) != words[2] || words[0] != words[3])
                ++failures;
            ++materialCases;
        } else if (kind == 'B') {
            std::array<uint32_t, 25> words;
            for (auto& word : words)
                if (!(row >> word))
                    return 1;
            if (words[6] > 1)
                return 1;
            CollisionBox box;
            for (int i = 0; i < 6; ++i)
                box[i] = std::bit_cast<float>(words[i]);
            const auto bounds = MakeLiquidQueryBounds(box, words[6]);
            std::array<uint32_t, 18> actual;
            int index = 0;
            for (const auto& output : {bounds.lava, bounds.water, bounds.readiness})
                for (auto value : output)
                    actual[index++] = std::bit_cast<uint32_t>(value);
            if (!std::equal(actual.begin(), actual.end(), words.begin() + 7) && failures++ < 10)
                std::fprintf(stderr, "BDS liquid bounds case %d differs\n", boundsCases);
            ++boundsCases;
        } else
            return 1;
        std::string extra;
        if (row >> extra)
            return 1;
    }
    if (!input.eof() || waterCases != 2048 || lavaCases != 128 || chainedCases != 512 ||
        materialCases != 32 || boundsCases != 1024)
        return 1;
    std::printf("BDS fluid state: %d water, %d lava, %d chained callbacks, %d material selections, %d "
                "bounds, %d mismatches (exact)\n",
                waterCases, lavaCases, chainedCases, materialCases, boundsCases, failures);
    return failures ? 1 : 0;
}
