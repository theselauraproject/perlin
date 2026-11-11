#include "client/player/PerlinJumpRequests.hpp"

#include <array>
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
    EntityMembership View() const { return {pages}; }
};

int main(int argc, char** argv) {
    if (argc != 2)
        return 1;
    std::ifstream input(argv[1]);
    if (!input)
        return 1;
    std::string line;
    int cases = 0, failures = 0;
    constexpr std::array routes{GroundJumpRequest::LocalPlayer, GroundJumpRequest::Other,
                                GroundJumpRequest::Squid, GroundJumpRequest::LavaSlime};
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        char kind;
        std::array<uint32_t, 8> words;
        if (!(row >> kind >> std::hex) || kind != 'F')
            return 1;
        for (auto& word : words)
            if (!(row >> word))
                return 1;
        std::string extra;
        if (row >> extra || words[5] >= routes.size() || words[6] > 1)
            return 1;
        for (int index = 1; index <= 4; ++index)
            if (words[index] > 5)
                return 1;
        Pool local(words[0], words[1]), lava(words[0], words[2]), squid(words[0], words[3]),
            destination(words[0], words[4]);
        std::vector<GroundJumpRequest> queries;
        std::vector<std::pair<GroundJumpRequest, uint32_t>> creates;
        ClassifyGroundJump(
            words[0], local.View(), lava.View(), squid.View(),
            [&](GroundJumpRequest request) {
                queries.push_back(request);
                return destination.View();
            },
            [&](GroundJumpRequest request, uint32_t entity) { creates.emplace_back(request, entity); });
        const bool equal =
            queries == std::vector{routes[words[5]]} && creates.size() == words[6] &&
            (creates.empty() ? words[7] == 0 : creates.front() == std::pair{routes[words[5]], words[7]});
        if (!equal && failures++ < 10)
            std::fprintf(stderr, "BDS jump request case %d differs\n", cases);
        ++cases;
    }
    if (!input.eof() || cases != 1296)
        return 1;
    std::printf("BDS jump requests: %d cases, %d mismatches\n", cases, failures);
    return failures ? 1 : 0;
}
