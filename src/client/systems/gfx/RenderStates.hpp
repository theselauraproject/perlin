#pragma once
#include <cstdint>

namespace perlin {
    enum class BlendFactor : uint8_t {
        Zero,
        One,
        SrcColor,
        InvSrcColor,
        SrcAlpha,
        InvSrcAlpha,
        DstColor,
        InvDstColor,
        DstAlpha,
        InvDstAlpha,
        SrcAlphaSaturate
    };

    enum class DepthFunc : uint8_t { LessEqual, Less, Equal, Always, Greater, GreaterEqual, NotEqual, Never };

    enum class StencilFunc : uint8_t {
        Always,
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        Never
    };

    enum class StencilOp : uint8_t { Keep, Replace, Zero, Increment, Decrement, Invert };

    enum class CullMode : uint8_t { Back, None, Front };
}
