#pragma once
#include "client/systems/javashaders/PipelineConfig.hpp"

#include <array>
#include <string_view>

namespace perlin::jshader {

    struct ShaderUsage {
        std::array<bool, kMaxColorTex> colorTex{};
        std::array<bool, kMaxColorTex> colorImage{};
        std::array<bool, 2> shadowColor{};
        bool shadow = false;
        bool depthTex1 = false;
        bool depthTex2 = false;

        void Merge(const ShaderUsage& other);
    };

    ShaderUsage ScanShaderText(std::string_view text);

}
