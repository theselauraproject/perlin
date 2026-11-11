#include "client/player/PerlinPhysicsNumerics.hpp"
#include "client/player/PerlinSineTable.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace perlin::physics::bds;

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "Usage: bds_numeric_selftest <BDS fixture> <BDS sine table>\n");
        return 1;
    }
    std::ifstream input(argv[1]);
    if (!input) {
        std::fprintf(stderr, "Cannot read BDS fixture: %s\n", argv[1]);
        return 1;
    }
    int cases = 0, failures = 0, intentCases = 0, frictionCases = 0, delayCases = 0, jumpCases = 0,
        accelerationCases = 0;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        char kind;
        row >> kind >> std::hex;
        std::array<uint32_t, 11> words{};
        const int fields = kind == 'I'                    ? 11
                           : kind == 'F'                  ? 10
                           : kind == 'D'                  ? 2
                           : (kind == 'J' || kind == 'A') ? 7
                                                          : 0;
        if (!fields) {
            std::fprintf(stderr, "Unknown fixture record: %s\n", line.c_str());
            return 1;
        }
        for (int i = 0; i < fields; ++i) {
            if (!(row >> words[i])) {
                std::fprintf(stderr, "Malformed fixture record: %s\n", line.c_str());
                return 1;
            }
        }
        std::string extra;
        if (row >> extra) {
            std::fprintf(stderr, "Unexpected trailing fixture field\n");
            return 1;
        }
        const auto f = [&](int index) { return std::bit_cast<float>(words[index]); };
        const auto check = [&](float actual, int expected) {
            const uint32_t got = std::bit_cast<uint32_t>(actual);
            if (got != words[expected]) {
                if (failures < 20)
                    std::fprintf(stderr, "Case %d (%c) field %d: %08x != BDS %08x\n", cases, kind, expected,
                                 got, words[expected]);
                ++failures;
            }
        };
        if (kind == 'I') {
            const auto result = AttenuateTravelIntent({f(0), f(1), f(2)}, f(3));
            check(result.movement.x, 4);
            check(result.movement.y, 5);
            check(result.movement.z, 6);
            check(result.turn, 7);
            check(result.copiedMovement.x, 8);
            check(result.copiedMovement.y, 9);
            check(result.copiedMovement.z, 10);
            ++intentCases;
        } else if (kind == 'F') {
            const auto result = ApplyFriction({f(4), f(5), f(6)}, f(2), f(3), words[1] != 0, words[0] != 0);
            check(result.x, 7);
            check(result.y, 8);
            check(result.z, 9);
            ++frictionCases;
        } else if (kind == 'A') {
            float effectiveFriction = words[0] == 0 || words[1] == 1 ? f(3) : f(4);
            if (words[1] == 1 && words[2] == 0)
                effectiveFriction *= 1.225f;
            if (words[1] == 2)
                effectiveFriction = 0.0f;
            check(GroundAcceleration(f(5), effectiveFriction), 6);
            ++accelerationCases;
        } else if (kind == 'J') {
            const auto result = ApplySprintJump({f(1), 0.0f, f(2)}, f(0));
            if (JumpSinIndex(f(0)) != words[3] || JumpCosIndex(f(0)) != words[4])
                ++failures;
            check(result.x, 5);
            check(result.z, 6);
            ++jumpCases;
        } else {
            static_assert(sizeof(int) == sizeof(uint32_t));
            const auto actual = static_cast<uint32_t>(TickJumpDelay(std::bit_cast<int>(words[0])));
            if (actual != words[1])
                ++failures;
            ++delayCases;
        }
        ++cases;
    }
    if (!input.eof() || intentCases != 109 || frictionCases != 3052 || delayCases != 8 || jumpCases != 277 ||
        accelerationCases != 336) {
        std::fprintf(stderr, "Incomplete fixture: intent=%d friction=%d delay=%d jump=%d acceleration=%d\n",
                     intentCases, frictionCases, delayCases, jumpCases, accelerationCases);
        return 1;
    }
    const int numericCases = cases;
    std::ifstream sine(argv[2], std::ios::binary);
    for (const float actual : SineTable()) {
        std::array<unsigned char, 4> bytes{};
        if (!sine.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) {
            std::fprintf(stderr, "Missing or truncated BDS sine table\n");
            return 1;
        }
        const uint32_t expected = uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
                                  (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
        if (std::bit_cast<uint32_t>(actual) != expected) {
            if (failures < 20)
                std::fprintf(stderr, "Sine entry %d: %08x != BDS %08x\n", cases - numericCases,
                             std::bit_cast<uint32_t>(actual), expected);
            ++failures;
        }
        ++cases;
    }
    char extra;
    if (sine.get(extra) || !sine.eof()) {
        std::fprintf(stderr, "Invalid BDS sine table length\n");
        return 1;
    }
    std::printf("BDS numeric conformance: %d cases, %d mismatching fields (bit-exact)\n", cases, failures);
    return failures ? 1 : 0;
}
