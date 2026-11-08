#pragma once
#include "client/javashaders/Transform.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace perlin::jshader {

    enum class RhiShaderKind { SpirV, Hlsl, Dxbc, Msl };

    using DxbcCompiler = std::function<bool(const std::string& hlsl, Stage stage, std::vector<uint8_t>& dxbc,
                                            std::string& error)>;

    struct RhiSampler {
        std::string name;
        int slot = 0;
        bool shadow = false;
    };

    struct RhiProgram {
        bool ok = false;
        std::string error;
        RhiShaderKind kind = RhiShaderKind::SpirV;
        std::vector<uint8_t> vertex, fragment;
        std::vector<RhiSampler> samplers;

        std::vector<std::string> images;
        int uniformRegs = 0;
        std::string vertexSource, fragmentSource;
    };

    RhiProgram BuildRhiProgram(const ProgramResult& program, RhiShaderKind kind, const DxbcCompiler& dxbc);

    struct RhiCompute {
        bool ok = false;
        std::string error;
        RhiShaderKind kind = RhiShaderKind::SpirV;
        std::vector<uint8_t> code;
        std::string source;
        std::vector<RhiSampler> samplers;
        std::vector<std::string> images;
        int uniformRegs = 0;
    };

    RhiCompute BuildRhiCompute(const ComputeResult& program, RhiShaderKind kind, const DxbcCompiler& dxbc);

    DxbcCompiler SystemDxbcCompiler();

    std::string MergeHlslSamplers(const std::string& hlsl,
                                  const std::function<std::string(const std::string& texture, bool comparison)>& keyOf);
}
