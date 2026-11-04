#include "client/player/PerlinSupport.hpp"

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
        size_t count;
        if (!(row >> kind >> std::hex >> count) || kind != 'S' || count > 16)
            return 1;
        auto readBox = [&](CollisionBox& box) {
            for (float& value : box) {
                uint32_t word;
                if (!(row >> word))
                    return false;
                value = std::bit_cast<float>(word);
            }
            return true;
        };
        CollisionBox body;
        if (!readBox(body))
            return 1;
        std::vector<CollisionBox> boxes(count);
        std::vector<uint32_t> blocks(count);
        for (size_t i = 0; i < count; ++i)
            if (!readBox(boxes[i]) || !(row >> blocks[i]) || blocks[i] > count)
                return 1;
        std::array<uint32_t, 10> expected{};
        for (auto& word : expected)
            if (!(row >> word))
                return 1;
        std::string extra;
        if (row >> extra)
            return 1;
        const auto result = SelectSupportingShape(body, boxes);
        std::array<uint32_t, 10> actual{};
        for (int i = 0; i < 3; ++i)
            actual[i] = std::bit_cast<uint32_t>(result.position[i]);
        actual[3] = result.index ? blocks[*result.index] : 0;
        for (int i = 0; i < 6; ++i)
            actual[i + 4] = std::bit_cast<uint32_t>(result.query[i]);
        for (int i = 0; i < 10; ++i)
            if (actual[i] != expected[i]) {
                if (failures < 20)
                    std::fprintf(stderr, "Case %d field %d: %08x != BDS %08x\n", cases, i, actual[i],
                                 expected[i]);
                ++failures;
            }
        ++cases;
    }
    if (!input.eof() || cases != 1280)
        return 1;
    std::printf("BDS supporting-shape conformance: %d cases, %d mismatching fields (bit-exact)\n", cases,
                failures);
    return failures ? 1 : 0;
}
