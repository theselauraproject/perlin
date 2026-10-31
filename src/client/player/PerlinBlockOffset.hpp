#pragma once

#include "client/player/PerlinFloat.hpp"
#include "client/player/PerlinLanding.hpp"

#include <bit>
#include <cmath>

namespace perlin::physics::bds {
    struct PositionRandomState {
        uint64_t first;
        uint64_t second;

        float NextUnit() {
            const uint64_t value = std::rotl(first + second, 17) + first;
            const uint64_t mixed = second ^ first;
            first = std::rotl(first, 49) ^ mixed ^ (mixed << 21);
            second = std::rotl(mixed, 28);
            return static_cast<float>(value >> 40) * 0x1p-24f;
        }
    };

    inline PositionRandomState SeedRandomFromHash(uint64_t hash) {
        const auto extend = [](int32_t value) { return static_cast<uint64_t>(static_cast<int64_t>(value)); };
        const uint64_t product = hash * (42317861ULL * hash + 11);
        const uint64_t seed = extend(std::bit_cast<int32_t>(static_cast<uint32_t>(product >> 16))) ^
                              0x6A09E667F3BCC909ULL;
        const auto mix = [](uint64_t value) {
            value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
            value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
            return value ^ (value >> 31);
        };
        PositionRandomState state{mix(seed), mix(seed + 0x9E3779B97F4A7C15ULL)};
        if ((state.first | state.second) == 0)
            state = {0x9E3779B97F4A7C15ULL, 0x6A09E667F3BCC909ULL};
        return state;
    }

    inline PositionRandomState SeedPositionRandom(BlockPosition position) {
        const auto extend = [](int32_t value) { return static_cast<uint64_t>(static_cast<int64_t>(value)); };
        const uint32_t xProduct = std::bit_cast<uint32_t>(position[0]) * 3129871u;
        return SeedRandomFromHash(extend(std::bit_cast<int32_t>(xProduct)) ^
                                  (extend(position[2]) * 116129781ULL) ^ extend(position[1]));
    }

    // block offset generator 0x145c633b0: x multiplied in 64 bits (movsxd + imul), no y term
    inline PositionRandomState SeedBlockOffsetRandom(BlockPosition position) {
        const auto extend = [](int32_t value) { return static_cast<uint64_t>(static_cast<int64_t>(value)); };
        return SeedRandomFromHash((extend(position[0]) * 3129871ULL) ^ (extend(position[2]) * 116129781ULL));
    }

    struct BlockOffsetAxis {
        float minimum = 0;
        float maximum = 0;
        uint32_t steps = 0;
    };

    struct BlockRandomOffset {
        std::array<BlockOffsetAxis, 3> axes;
    };

    inline BlockRandomOffset BlockOffsetFromDescription(const std::array<BlockOffsetAxis, 3>& description) {
        BlockRandomOffset component;
        for (size_t i = 0; i < 3; ++i)
            component.axes[i] = {detail::SseMultiply(description[i].minimum, 0.0625f),
                                 detail::SseMultiply(description[i].maximum, 0.0625f), description[i].steps};
        return component;
    }

    inline float SampleBlockOffset(BlockOffsetAxis range, float unit) {
        if (!(range.minimum < range.maximum))
            return range.minimum;
        if (range.steps == 1)
            return detail::SseMultiply(detail::SseAdd(range.minimum, range.maximum), 0.5f);
        const float difference = detail::SseSubtract(range.maximum, range.minimum);
        if (range.steps == 0)
            return detail::SseAdd(detail::SseMultiply(difference, unit), range.minimum);
        const float index = std::floor(detail::SseMultiply(static_cast<float>(range.steps), unit));
        const float step = detail::SseDivide(difference, static_cast<float>(range.steps - 1));
        return detail::SseAdd(detail::SseMultiply(index, step), range.minimum);
    }

    inline Float3 GenerateBlockOffset(const BlockRandomOffset& component, BlockPosition position) {
        auto random = SeedBlockOffsetRandom(position);
        const float x = SampleBlockOffset(component.axes[0], random.NextUnit());
        const float y = SampleBlockOffset(component.axes[1], random.NextUnit());
        const float z = SampleBlockOffset(component.axes[2], random.NextUnit());
        return {x, y, z};
    }
}
