#pragma once
#include "client/systems/javashaders/Include.hpp"
#include "client/systems/javashaders/RhiBackend.hpp"
#include "client/systems/javashaders/Transform.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace perlin::gpu::program_cache {
    inline constexpr int kCacheVersion = 6;

    struct Program {
        std::vector<jshader::UniformDecl> uniforms;
        std::vector<int> drawBuffers;
        std::vector<jshader::RhiSampler> samplers;
        std::vector<std::string> images;
        int uniformRegs = 0;
        std::vector<uint8_t> vertex, fragment;
    };

    std::filesystem::path PathFor(const jshader::ExpandedSource& vertex, const jshader::ExpandedSource& fragment,
                                  const jshader::TransformOptions& options, int shaderKind);
    std::optional<Program> Load(const std::filesystem::path& file);
    void Store(const std::filesystem::path& file, const Program& program);
}
