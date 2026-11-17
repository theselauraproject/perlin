#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace perlin::jshader {

    struct SamplerFlags {
        bool blur = false;
        bool clamp = false;
        bool operator==(const SamplerFlags&) const = default;
    };

    SamplerFlags ParseSamplerFlags(std::string_view mcmeta);
    std::string StripLeadingSlashes(std::string_view path);
    std::vector<uint8_t> GenerateNoise(int resolution);

}
