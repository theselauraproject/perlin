#include "client/player/PerlinBlockOffset.hpp"

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
    int seeds = 0, offsets = 0, descriptions = 0, failures = 0;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        char kind;
        if (!(row >> kind >> std::hex) || (kind != 'R' && kind != 'O' && kind != 'D'))
            return 1;
        if (kind == 'D') {
            uint32_t enabled;
            if (!(row >> enabled) || enabled > 1)
                return 1;
            std::array<BlockOffsetAxis, 3> description;
            for (auto& axis : description) {
                uint32_t low, high;
                if (!(row >> low >> high >> axis.steps))
                    return 1;
                axis.minimum = std::bit_cast<float>(low);
                axis.maximum = std::bit_cast<float>(high);
            }
            if (enabled) {
                const auto component = BlockOffsetFromDescription(description);
                for (const auto& axis : component.axes) {
                    uint32_t low, high, steps;
                    if (!(row >> low >> high >> steps))
                        return 1;
                    if (std::bit_cast<uint32_t>(axis.minimum) != low ||
                        std::bit_cast<uint32_t>(axis.maximum) != high || axis.steps != steps)
                        ++failures;
                }
            }
            std::string extra;
            if (row >> extra)
                return 1;
            ++descriptions;
            continue;
        }
        BlockPosition position;
        for (auto& value : position) {
            uint32_t word;
            if (!(row >> word))
                return 1;
            value = std::bit_cast<int32_t>(word);
        }
        if (kind == 'R') {
            std::array<uint32_t, 4> expected{};
            for (auto& word : expected)
                if (!(row >> word))
                    return 1;
            const auto state = SeedPositionRandom(position);
            const std::array<uint32_t, 4> actual{
                static_cast<uint32_t>(state.first), static_cast<uint32_t>(state.first >> 32),
                static_cast<uint32_t>(state.second), static_cast<uint32_t>(state.second >> 32)};
            if (actual != expected) {
                if (failures < 20)
                    std::fprintf(stderr, "Position seed %d differs from BDS\n", seeds);
                ++failures;
            }
            ++seeds;
        } else {
            BlockRandomOffset component;
            for (auto& axis : component.axes) {
                uint32_t low, high;
                if (!(row >> low >> high >> axis.steps))
                    return 1;
                axis.minimum = std::bit_cast<float>(low);
                axis.maximum = std::bit_cast<float>(high);
            }
            std::array<uint32_t, 3> expected{};
            for (auto& word : expected)
                if (!(row >> word))
                    return 1;
            const auto offset = GenerateBlockOffset(component, position);
            const std::array<uint32_t, 3> actual{std::bit_cast<uint32_t>(offset.x),
                                                 std::bit_cast<uint32_t>(offset.y),
                                                 std::bit_cast<uint32_t>(offset.z)};
            if (actual != expected) {
                if (failures < 20)
                    std::fprintf(stderr, "Block offset %d differs from BDS\n", offsets);
                ++failures;
            }
            ++offsets;
        }
        std::string extra;
        if (row >> extra)
            return 1;
    }
    if (!input.eof() || seeds != 137 || offsets != 1233 || descriptions != 18)
        return 1;
    std::printf("BDS block offsets: %d seeds, %d offsets, %d descriptions, %d mismatches (bit-exact)\n",
                seeds, offsets, descriptions, failures);
    return failures ? 1 : 0;
}
