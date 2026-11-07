#include "client/player/PerlinSneak.hpp"
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
        if (!(row >> kind >> std::hex) || kind != 'S')
            return 1;
        std::array<uint32_t, 19> w{};
        for (auto& v : w)
            if (!(row >> v))
                return 1;
        if (w[0] > 255 || w[1] > 2 || w[2] > 2 || w[3] > 1 || w[4] > 1 || w[18] > 32)
            return 1;
        std::vector<CollisionBox> shapes(w[18]);
        for (auto& box : shapes)
            for (float& v : box) {
                uint32_t bits;
                if (!(row >> bits))
                    return 1;
                v = std::bit_cast<float>(bits);
            }
        std::array<uint32_t, 7> out{};
        for (auto& v : out)
            if (!(row >> v))
                return 1;
        std::string extra;
        if (row >> extra)
            return 1;
        auto f = [&](int i) { return std::bit_cast<float>(w[i]); };
        CollisionBox body{f(6), f(7), f(8), f(9), f(10), f(11)};
        SneakMovement result{{f(12), f(13), f(14)}, {f(15), f(16), f(17)}};
        std::optional<bool> control;
        if (w[1] == 1)
            control = w[3] != 0;
        bool requestFlag = w[4] != 0;
        if (ShouldClipSneak(static_cast<uint8_t>(w[0]), control, w[2] == 1, requestFlag))
            result = ClipSneakMovement(body, f(5), result.movement, result.velocity, shapes);
        bool equal = bool(out[0]) == requestFlag;
        for (int i = 0; i < 3; ++i) {
            equal &= std::bit_cast<uint32_t>(result.movement[i]) == out[i + 1];
            equal &= std::bit_cast<uint32_t>(result.velocity[i]) == out[i + 4];
        }
        if (!equal) {
            if (failures < 20)
                std::fprintf(stderr, "Sneak case %d differs\n", cases);
            ++failures;
        }
        ++cases;
    }
    if (!input.eof() || cases != 1024)
        return 1;
    std::printf("BDS sneak: %d cases, %d mismatches (bit-exact)\n", cases, failures);
    return failures ? 1 : 0;
}
