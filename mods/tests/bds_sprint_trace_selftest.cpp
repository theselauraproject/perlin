#include "client/player/PerlinSineTable.hpp"

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
    std::string line;
    int cases = 0, failures = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        char kind;
        std::array<uint32_t, 7> words;
        if (!(row >> kind >> std::hex) || kind != 'J')
            return 1;
        for (auto& word : words)
            if (!(row >> word))
                return 1;
        std::string extra;
        if (row >> extra)
            return 1;
        const float yaw = std::bit_cast<float>(words[0]);
        const auto velocity =
            ApplySprintJump({std::bit_cast<float>(words[1]), 0, std::bit_cast<float>(words[2])}, yaw);
        if (JumpSinIndex(yaw) != words[3] || JumpCosIndex(yaw) != words[4] ||
            std::bit_cast<uint32_t>(velocity.x) != words[5] ||
            std::bit_cast<uint32_t>(velocity.z) != words[6]) {
            std::fprintf(stderr, "BDS trace sprint impulse %d differs\n", cases);
            ++failures;
        }
        ++cases;
    }
    if (!input.eof() || cases != 7)
        return 1;
    std::printf("BDS trace sprint impulses: %d cases, %d mismatches (bit-exact)\n", cases, failures);
    return failures ? 1 : 0;
}
