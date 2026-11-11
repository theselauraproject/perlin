#include "client/player/PerlinHoney.hpp"

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
        std::array<uint32_t, 9> words;
        if (!(row >> kind >> std::hex) || kind != 'H')
            return 1;
        for (auto& word : words)
            if (!(row >> word))
                return 1;
        if (words[8] > 4)
            return 1;
        std::vector<BlockPosition> contacts(words[8]);
        for (auto& position : contacts)
            for (auto& value : position) {
                uint32_t word;
                if (!(row >> word))
                    return 1;
                value = std::bit_cast<int32_t>(word);
            }
        std::array<uint32_t, 4> expected;
        for (auto& word : expected)
            if (!(row >> word))
                return 1;
        std::string extra;
        if (row >> extra)
            return 1;
        const auto f = [&](int i) { return std::bit_cast<float>(words[i]); };
        float fall = f(7);
        const auto result = HoneyContacts({f(0), f(1), f(2)}, {f(3), f(4), f(5)}, f(6), contacts, fall);
        const std::array actual{std::bit_cast<uint32_t>(result.x), std::bit_cast<uint32_t>(result.y),
                                std::bit_cast<uint32_t>(result.z), std::bit_cast<uint32_t>(fall)};
        if (actual != expected && failures++ < 10)
            std::fprintf(stderr, "BDS honey case %d differs\n", cases);
        ++cases;
    }
    if (!input.eof() || cases != 640)
        return 1;
    std::printf("BDS honey contacts: %d cases, %d mismatches (bit-exact)\n", cases, failures);
    return failures ? 1 : 0;
}
