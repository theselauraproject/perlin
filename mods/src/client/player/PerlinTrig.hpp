#pragma once

#include "client/player/PerlinPhysicsNumerics.hpp"
#include "client/player/PerlinTrigTables.hpp"

#include <bit>
#include <cstdint>

namespace perlin::physics::bds {
    namespace detail {
        inline float SineSmallPositive(float angle) {
            const auto constant = [](uint32_t bits) { return std::bit_cast<float>(bits); };
            const float roundedTurns = Round(Round(angle * constant(0x3ea2f983)) + 12582912.0f);
            const float turns = Round(roundedTurns - 12582912.0f);
            float reduced = Round(angle - Round(3.140625f * turns));
            reduced = Round(reduced - Round(constant(0x3a7da000) * turns));
            reduced = Round(reduced - Round(constant(0x34222000) * turns));
            reduced = Round(reduced - Round(constant(0x2cb4611a) * turns));
            const float square = Round(reduced * reduced);
            const float signedReduced = std::bit_cast<float>(std::bit_cast<uint32_t>(reduced) ^
                                                             (std::bit_cast<uint32_t>(roundedTurns) << 31));
            float polynomial = Round(Round(constant(0x362edef8) * square) + constant(0xb94fb7ff));
            polynomial = Round(Round(polynomial * square) + constant(0x3c088766));
            polynomial = Round(Round(polynomial * square) + constant(0xbe2aaaa6));
            return Round(signedReduced + Round(Round(square * polynomial) * signedReduced));
        }

        inline float SineLarge(float angle) {
            const uint32_t raw = std::bit_cast<uint32_t>(angle);
            const uint32_t exponent = (raw >> 23) & 255u;
            const uint32_t reductionIndex = (exponent - 140u) * 3u;
            const uint32_t first = SineReductionWords[reductionIndex];
            const uint32_t middle = SineReductionWords[reductionIndex + 1];
            const uint32_t last = SineReductionWords[reductionIndex + 2];
            const uint32_t mantissa = (raw & 0x7fffffu) + 0x800000u;
            const uint32_t low = mantissa & 65535u, high = mantissa >> 16;
            const uint32_t firstLow = first & 65535u, firstHigh = first >> 16;
            const uint32_t middleLow = middle & 65535u, middleHigh = middle >> 16;
            const uint32_t lastLow = last & 65535u, lastHigh = last >> 16;
            const uint32_t lowFirst = low * firstLow;
            const uint32_t lowMiddle = low * middleLow;
            const uint32_t lowMiddleHigh = low * middleHigh;
            const uint32_t tail =
                ((high * lastLow) >> 16) + (lowMiddle & 65535u) + high * lastHigh + ((lastHigh * low) >> 16);
            const uint32_t center = (lowMiddleHigh & 65535u) + middleLow * high + (lowMiddle >> 16);
            const uint32_t carriedCenter = (tail >> 16) + center;
            const uint32_t head =
                (carriedCenter >> 16) + (lowFirst & 65535u) + middleHigh * high + (lowMiddleHigh >> 16);
            const uint32_t fractionHigh =
                (((head >> 16) + ((low * firstHigh) & 65535u) + high * firstLow + (lowFirst >> 16)) << 16) +
                (head & 65535u);
            const uint32_t fractionLow = (carriedCenter << 16) + (tail & 65535u);
            const uint32_t sign = raw & 0x80000000u;
            const auto f = [](uint32_t bits) { return std::bit_cast<float>(bits); };
            const auto add = [](float a, float b) { return Round(a + b); };
            const auto sub = [](float a, float b) { return Round(a - b); };
            const auto mul = [](float a, float b) { return Round(a * b); };
            const float whole = f((fractionHigh >> 9) | (0x3f800000u ^ sign));
            const float grid = add(whole, f(0x47400000));
            const float lowScale = f(0x28800000u ^ sign);
            const float middleScale = f(0x34000000u ^ sign);
            const float middlePart = sub(
                f(((fractionHigh & 511u) << 14) | (fractionLow >> 18) | std::bit_cast<uint32_t>(middleScale)),
                middleScale);
            const float lowPart =
                sub(f(((fractionLow & 0x3ffffu) << 5) | std::bit_cast<uint32_t>(lowScale)), lowScale);
            const float residual = sub(whole, sub(grid, f(0x47400000)));
            const float combined = add(residual, middlePart);
            const float correction = add(lowPart, add(middlePart, sub(residual, combined)));
            const float upper = f(std::bit_cast<uint32_t>(combined) & 0xfffff000u);
            const float upperProduct = mul(f(0x40c91000), upper);
            const float lower = sub(combined, upper);
            const float lowerProduct = add(add(mul(f(0x40c91000), lower), mul(upper, f(0xb795777a))),
                                           add(mul(f(0x40c90fdb), correction), mul(f(0xb795777a), lower)));
            const float reduced = add(lowerProduct, upperProduct);
            const float reducedError = add(lowerProduct, sub(upperProduct, reduced));
            const uint32_t index = (std::bit_cast<uint32_t>(grid) & 255u) * 4;
            const float cosineHigh = f(SineReconstructionWords[index]);
            const float sineHigh = f(SineReconstructionWords[index + 1]);
            const float sineLow = f(SineReconstructionWords[index + 2]);
            const float cosineLow = f(SineReconstructionWords[index + 3]);
            const float lowProduct = mul(reduced, cosineLow);
            const float square = mul(reduced, reduced);
            const float highProduct = mul(reduced, cosineHigh);
            const float partial = add(sineHigh, lowProduct);
            const float total = add(partial, highProduct);
            const float derivative = sub(add(cosineHigh, cosineLow), mul(reduced, sineHigh));
            const float evenPolynomial =
                mul(sineHigh, mul(square, add(mul(f(0x3d2aaa7c), square), f(0xbf000000))));
            const float oddPolynomial =
                mul(mul(mul(add(mul(f(0x3c08885c), square), f(0xbe2aaaab)), square), reduced), derivative);
            const float roundoff =
                add(add(lowProduct, sub(sineHigh, partial)), add(highProduct, sub(partial, total)));
            return add(total, add(add(add(sineLow, mul(reducedError, derivative)), evenPolynomial),
                                  add(roundoff, oddPolynomial)));
        }
    }

    inline float Sine(float angle) {
        const uint32_t raw = std::bit_cast<uint32_t>(angle);
        const uint32_t absolute = raw & 0x7fffffffu;
        if (absolute >= 0x7f800000u)
            return std::bit_cast<float>(absolute == 0x7f800000u ? 0xffc00000u : raw | 0x00400000u);
        if (absolute > 0x461c4000u)
            return detail::SineLarge(angle);
        const float value = detail::SineSmallPositive(std::bit_cast<float>(absolute));
        return std::bit_cast<float>(std::bit_cast<uint32_t>(value) ^ (raw & 0x80000000u));
    }

    struct SineCosine {
        float sine, cosine;
    };

    inline SineCosine SinCos(float angle) {
        const uint32_t absoluteBits = std::bit_cast<uint32_t>(angle) & 0x7fffffffu;
        if (absoluteBits > 0x7f800000u)
            return {angle, std::bit_cast<float>(absoluteBits | 0x00400000u)};
        const float absolute = std::bit_cast<float>(absoluteBits);
        if (absolute < 0.0001220703125f) {
            const float square = detail::Round(absolute * absolute);
            return {angle, 1.0f - detail::Round(square * 0.5f)};
        }
        return {Sine(angle), Sine(detail::Round(absolute + std::bit_cast<float>(0x3fc90fdbu)))};
    }
}
