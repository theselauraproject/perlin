#pragma once
#include "client/javashaders/Transform.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace perlin::jshader {
    std::string PreprocessGlsl(const std::string& text, Stage stage, std::string& error);

    bool HlslToSpirv(const std::string& hlsl, Stage stage, std::vector<uint32_t>& spirv, std::string& log);
    bool CompileToSpirv(const std::string& glsl450, Stage stage, std::vector<uint32_t>& spirv,
                        std::string& log);

    struct EngineEnvironment {
        std::string vendor, renderer;

        std::set<std::string> features;
        int mipmapLevels = 4;
    };

    const std::set<std::string>& BaseFeatureFlags();
    MacroMap EngineMacros(int mcVersion, bool openglBackend, const EngineEnvironment& env = {});
}
