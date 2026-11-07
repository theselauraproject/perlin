#include "client/player/PerlinBlockSlowdown.hpp"

#include <array>
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
    int direct = 0, producer = 0, fall = 0, immunity = 0, failures = 0;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        char kind;
        if (!(row >> kind >> std::hex))
            return 1;
        const int count = kind == 'D' ? 18 : kind == 'P' ? 11 : kind == 'F' ? 3 : kind == 'I' ? 7 : 0;
        if (!count)
            return 1;
        std::array<uint32_t, 18> words{};
        for (int i = 0; i < count; ++i)
            if (!(row >> words[i]))
                return 1;
        std::string extra;
        if (row >> extra)
            return 1;
        const auto f = [&](int i) { return std::bit_cast<float>(words[i]); };
        bool equal = true;
        if (kind == 'D') {
            Float3 multiplier{f(0), f(1), f(2)}, movement{f(3), f(4), f(5)}, velocity{f(6), f(7), f(8)};
            ApplyBlockSlowdown(multiplier, movement, velocity);
            for (int axis = 0; axis < 3; ++axis) {
                equal &= std::bit_cast<uint32_t>((std::array<float, 3>{
                             multiplier.x, multiplier.y, multiplier.z}[axis])) == words[axis + 9];
                equal &=
                    std::bit_cast<uint32_t>(
                        (std::array<float, 3>{movement.x, movement.y, movement.z}[axis])) == words[axis + 12];
                equal &=
                    std::bit_cast<uint32_t>(
                        (std::array<float, 3>{velocity.x, velocity.y, velocity.z}[axis])) == words[axis + 15];
            }
            ++direct;
        } else if (kind == 'P') {
            if (words[0] > 2 || words[7] < 1 || words[7] > 2)
                return 1;
            std::optional<bool> first;
            if (words[0])
                first = words[0] == 2;
            Float3 multiplier{f(1), f(2), f(3)};
            AccumulateBlockSlowdown(multiplier, {f(4), f(5), f(6)}, first);
            equal = first && static_cast<uint32_t>(*first) + 1 == words[7];
            for (int axis = 0; axis < 3; ++axis)
                equal &= std::bit_cast<uint32_t>((std::array<float, 3>{
                             multiplier.x, multiplier.y, multiplier.z}[axis])) == words[axis + 8];
            ++producer;
        } else if (kind == 'I') {
            Float3 multiplier{f(1), f(2), f(3)};
            std::optional<bool> first;
            ApplyPlayerSlowdownImmunity(multiplier, first, words[0] & 1, words[0] & 4);
            for (int axis = 0; axis < 3; ++axis)
                equal &= std::bit_cast<uint32_t>((std::array<float, 3>{
                             multiplier.x, multiplier.y, multiplier.z}[axis])) == words[axis + 4];
            ++immunity;
        } else {
            if (words[0] > 255)
                return 1;
            float distance = f(1);
            AdjustSlowdownFallDistance(words[0] != 0, distance);
            equal = std::bit_cast<uint32_t>(distance) == words[2];
            ++fall;
        }
        if (!equal) {
            if (failures < 20)
                std::fprintf(stderr, "BDS slowdown %c case %d differs\n", kind,
                             kind == 'D'   ? direct - 1
                             : kind == 'P' ? producer - 1
                             : kind == 'I' ? immunity - 1
                                           : fall - 1);
            ++failures;
        }
    }
    if (!input.eof() || direct != 1024 || producer != 768 || fall != 256 || immunity != 256)
        return 1;
    std::printf("BDS slowdown: %d apply, %d accumulation, %d fall-distance, %d immunity cases, %d mismatches "
                "(bit-exact)\n",
                direct, producer, fall, immunity, failures);
    return failures ? 1 : 0;
}
