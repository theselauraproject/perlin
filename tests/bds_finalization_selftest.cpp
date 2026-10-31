#include "client/player/PerlinMoveFinalization.hpp"

#include <array>
#include <bit>
#include <cstdint>
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
        if (!(row >> kind >> std::hex) || kind != 'F')
            return 1;
        std::array<uint32_t, 26> words{};
        for (auto& word : words)
            if (!(row >> word))
                return 1;
        std::string extra;
        if (row >> extra || words[0] > 2 || words[1] > 1 || words[2] > 31 || words[25] > 31)
            return 1;
        const auto f = [&](int i) { return std::bit_cast<float>(words[i]); };
        const auto vector = [&](int i) { return Float3{f(i), f(i + 1), f(i + 2)}; };
        const auto old = words[2];
        const auto result = FinalizeMove({f(4), f(5), f(6), f(7), f(8), f(9)}, f(3),
                                         vector(10), vector(13), vector(16),
                                         // stale horizontal tag: both lanes set, a corner hit
                                         {bool(old & 1), bool(old & 2), bool(old & 4),
                                          bool(old & 8), bool(old & 16), bool(old & 1), bool(old & 1)},
                                         words[0] == 2, words[1] != 0);
        const std::array<float, 6> actual{result.position.x, result.position.y, result.position.z,
                                          result.velocity.x, result.velocity.y, result.velocity.z};
        for (int field = 0; field < 6; ++field) {
            const auto got = std::bit_cast<uint32_t>(actual[field]);
            if (got != words[19 + field]) {
                if (failures < 20)
                    std::fprintf(stderr, "Case %d, field %d: %08x != BDS %08x\n",
                                 cases, field, got, words[19 + field]);
                ++failures;
            }
        }
        const auto& flags = result.flags;
        const uint32_t got = flags.horizontalCollision | (flags.verticalCollision << 1) |
                             (flags.onGround << 2) | (flags.collision << 3) | (flags.collidableMobNear << 4);
        if (got != words[25]) {
            if (failures < 20)
                std::fprintf(stderr, "Case %d, flags: %02x != BDS %02x\n", cases, got, words[25]);
            ++failures;
        }
        ++cases;
    }
    if (!input.eof() || cases != 1792)
        return 1;
    std::printf("BDS move finalization: %d cases, %d mismatching fields (bit-exact)\n", cases, failures);
    return failures ? 1 : 0;
}
