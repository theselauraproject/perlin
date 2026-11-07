#include "client/player/PerlinJumpRequests.hpp"

#include <array>
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
        std::array<uint32_t, 6> words;
        if (!(row >> kind >> std::hex) || kind != 'P')
            return 1;
        for (auto& word : words)
            if (!(row >> word))
                return 1;
        if (words[0] > 255 || words[1] > 1 || words[2] > 1 || words[3] > 1 || words[4] > 1 || words[5] > 2)
            return 1;
        std::vector<uint32_t> expected(words[5]), queries;
        for (auto& query : expected)
            if (!(row >> query) || query > 1)
                return 1;
        std::string extra;
        if (row >> extra)
            return 1;
        const bool allowed = GroundJumpAllowed(
            words[0], words[1],
            [&] {
                queries.push_back(0);
                return words[2] != 0;
            },
            [&] {
                queries.push_back(1);
                return words[3] != 0;
            });
        if ((allowed != bool(words[4]) || queries != expected) && failures++ < 10)
            std::fprintf(stderr, "BDS jump permission case %d differs\n", cases);
        ++cases;
    }
    if (!input.eof() || cases != 2048)
        return 1;
    std::printf("BDS jump permission: %d cases, %d mismatches\n", cases, failures);
    return failures ? 1 : 0;
}
