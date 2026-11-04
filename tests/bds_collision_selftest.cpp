#include "client/player/PerlinCollision.hpp"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace perlin::physics::bds;

int main(int argc, char** argv) {
    if (argc != 4)
        return 1;
    int contactCases = 0, moveCases = 0, recoveryCases = 0, updateCases = 0, stepCases = 0, broadCases = 0,
        filterCases = 0, failures = 0;
    for (int file = 1; file < argc; ++file) {
        std::ifstream input(argv[file]);
        if (!input)
            return 1;
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream stream(line);
            char kind;
            stream >> kind >> std::hex;
            std::vector<uint32_t> words;
            uint32_t word;
            while (stream >> word)
                words.push_back(word);
            if (!stream.eof())
                return 1;
            const auto f = [&](size_t index) { return std::bit_cast<float>(words.at(index)); };
            const auto box = [&](size_t index) -> CollisionBox {
                return {f(index), f(index + 1), f(index + 2), f(index + 3), f(index + 4), f(index + 5)};
            };
            const auto vector = [&](size_t index) -> CollisionVector {
                return {f(index), f(index + 1), f(index + 2)};
            };
            const auto check = [&](float actual, size_t expected) {
                if (std::bit_cast<uint32_t>(actual) != words.at(expected)) {
                    if (failures < 20)
                        std::fprintf(stderr, "%c case %d/%d step %d field %zu: %08x != BDS %08x\n", kind,
                                     contactCases, moveCases, stepCases, expected,
                                     std::bit_cast<uint32_t>(actual), words[expected]);
                    ++failures;
                }
            };
            if (kind == 'C') {
                if (words.size() != 23)
                    return 1;
                const auto result = Collide(box(0), box(6), vector(12));
                check(result.penetration, 15);
                for (size_t axis = 0; axis < 3; ++axis) {
                    check(result.clipped[axis], 16 + axis);
                    check(result.recovered[axis], 19 + axis);
                }
                if (static_cast<uint32_t>(result.axis) != words[22])
                    ++failures;
                ++contactCases;
            } else if (kind == 'M') {
                if (words.size() < 21 || words[0] > 3 || words[1] > 6)
                    return 1;
                const size_t end = 11 + words[0] * 3 + words[1] * 6;
                if (words.size() != end + 10)
                    return 1;
                std::vector<CollisionVector> moves;
                std::vector<CollisionBox> colliders;
                size_t cursor = 11;
                for (uint32_t i = 0; i < words[0]; ++i, cursor += 3)
                    moves.push_back(vector(cursor));
                for (uint32_t i = 0; i < words[1]; ++i, cursor += 6)
                    colliders.push_back(box(cursor));
                const auto result = ResolveMoves(box(2), moves, colliders, vector(8));
                for (size_t axis = 0; axis < 3; ++axis)
                    check(result.displacement[axis], end + axis);
                check(result.penetration, end + 3);
                for (size_t axis = 0; axis < 6; ++axis)
                    check(result.box[axis], end + 4 + axis);
                ++moveCases;
            } else if (kind == 'R') {
                if (words.size() != 12)
                    return 1;
                RecoveryState state;
                state.flags = words[0];
                state.hasHistory = words[1] != 0;
                state.minimum = vector(2);
                if (words[5] != 0)
                    state.customMinimum = vector(6);
                const auto result = RecoveryLimits(state);
                for (size_t axis = 0; axis < 3; ++axis)
                    check(result[axis], 9 + axis);
                ++recoveryCases;
            } else if (kind == 'U') {
                if (words.size() != 5)
                    return 1;
                RecoveryState state;
                state.flags = words[0];
                state.customMinimum = CollisionVector{1, 2, 3};
                UpdateRecovery(state, words[1] != 0, words[2] != 0);
                if (state.flags != words[3] || state.customMinimum.has_value() != (words[4] != 0))
                    ++failures;
                ++updateCases;
            } else if (kind == 'T') {
                if (words.size() != 12)
                    return 1;
                bool oneShot = words[9] != 0;
                const bool selected =
                    ShouldAutoStep(f(0), vector(1), vector(4), words[7] != 0, words[8] != 0, oneShot);
                if (selected != (words[10] != 0) || oneShot != (words[11] != 0))
                    ++failures;
                ++filterCases;
            } else if (kind == 'B') {
                if (words.size() != 19)
                    return 1;
                const auto result = SearchMovement(box(1), vector(7), f(0));
                for (size_t axis = 0; axis < 3; ++axis)
                    check(result.movement[axis], 10 + axis);
                for (size_t index = 0; index < 6; ++index)
                    check(result.region[index], 13 + index);
                ++broadCases;
            } else if (kind == 'S') {
                if (words.size() < 23 || words[1] > 8 || words.size() != 23 + words[1] * 6)
                    return 1;
                const auto start = box(2);
                const auto velocity = vector(8);
                const auto limits = vector(11);
                std::vector<CollisionBox> shapes;
                for (size_t index = 0; index < words[1]; ++index)
                    shapes.push_back(box(14 + index * 6));
                const std::array<CollisionVector, 3> moves{
                    {{0, velocity[1], 0}, {velocity[0], 0, 0}, {0, 0, velocity[2]}}};
                auto result = ResolveMoves(start, moves, shapes, limits);
                TryAutoStep(start, velocity, f(0), shapes, limits, result);
                const size_t end = 14 + shapes.size() * 6;
                for (size_t axis = 0; axis < 3; ++axis)
                    check(result.displacement[axis], end + axis);
                for (size_t index = 0; index < 6; ++index)
                    check(result.box[index], end + 3 + index);
                ++stepCases;
            } else {
                return 1;
            }
        }
        if (!input.eof())
            return 1;
    }
    std::printf(
        "BDS collision conformance: contacts=%d moves=%d recovery=%d updates=%d steps=%d broad=%d filters=%d "
        "mismatching "
        "fields=%d\n",
        contactCases, moveCases, recoveryCases, updateCases, stepCases, broadCases, filterCases, failures);
    return contactCases != 2404 || moveCases != 512 || recoveryCases != 384 || updateCases != 128 ||
                   stepCases != 512 || broadCases != 512 || filterCases != 256 || failures
               ? 1
               : 0;
}
