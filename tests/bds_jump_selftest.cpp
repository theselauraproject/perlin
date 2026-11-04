#include "client/player/PerlinJump.hpp"
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
        if (!(row >> kind >> std::hex) || kind != 'J')
            return 1;
        std::array<uint64_t, 31> words{};
        for (auto& word : words)
            if (!(row >> word))
                return 1;
        if (words[0] > 1 || words[1] > 255 || words[2] > 1 || words[3] > 2 || words[5] > 3 || words[7] > 1 ||
            words[18] > 2 || words[26] > 1 || words[30] > 2)
            return 1;
        std::vector<BlockPosition> expected(words[30]), queries;
        for (auto& position : expected)
            for (auto& value : position) {
                uint32_t word;
                if (!(row >> word))
                    return 1;
                value = std::bit_cast<int32_t>(word);
            }
        std::string extra;
        if (row >> extra)
            return 1;
        auto f = [&](int i) { return std::bit_cast<float>(static_cast<uint32_t>(words[i])); };
        JumpParameters parameters;
        parameters.alternate = words[0] != 0;
        parameters.sprinting = words[1] & 8;
        parameters.forceLowHeight = words[2] != 0;
        if (words[3] == 2)
            parameters.customHeight = f(4);
        if (words[5] == 1)
            parameters.effectAmplifier = std::bit_cast<int32_t>(static_cast<uint32_t>(words[6]));
        if (words[7])
            parameters.existingTrigger = TriggerJumpRequest{true, {-11, -12, -13}};
        parameters.bodyMinimumY = f(10);
        parameters.position = {f(11), f(12), f(13)};
        parameters.velocity = {f(14), f(15), f(16)};
        parameters.yaw = f(17);
        parameters.previousJumpPosition = {-9, -8, -7};
        if (words[18] == 1)
            parameters.syncedJumpLevel = std::bit_cast<int32_t>(static_cast<uint32_t>(words[19]));
        const auto result = GroundJump(parameters, [&](BlockPosition position) {
            queries.push_back(position);
            return words[queries.size() == 1 ? 8 : 9];
        });
        const std::array<float, 6> actual{result.previousJumpPosition.x,
                                          result.previousJumpPosition.y,
                                          result.previousJumpPosition.z,
                                          result.velocity.x,
                                          result.velocity.y,
                                          result.velocity.z};
        bool equal = expected == queries && result.trigger.reducedSurface == bool(words[26]);
        for (int axis = 0; axis < 6; ++axis)
            equal &= std::bit_cast<uint32_t>(actual[axis]) == words[axis + 20];
        for (int axis = 0; axis < 3; ++axis)
            equal &= static_cast<uint32_t>(result.trigger.position[axis]) == words[axis + 27];
        if (!equal) {
            if (failures < 20)
                std::fprintf(stderr, "BDS jump case %d differs\n", cases);
            ++failures;
        }
        ++cases;
    }
    if (!input.eof() || cases != 1536)
        return 1;
    std::printf("BDS complete jumps: %d cases, %d mismatches (bit-exact)\n", cases, failures);
    return failures ? 1 : 0;
}
