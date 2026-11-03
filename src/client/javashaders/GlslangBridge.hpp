#pragma once
#include "client/javashaders/Transform.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace perlin::jshader {
    std::string PreprocessGlsl(const std::string& text, Stage stage, std::string& error);
    // developer testing: the hlsl the direct3d backends get, compiled for vulkan (registers become bindings and
    // spaces sets; TEXCOORDn stage variables keep location n)
    bool HlslToSpirv(const std::string& hlsl, Stage stage, std::vector<uint32_t>& spirv, std::string& log);
    bool CompileToSpirv(const std::string& glsl450, Stage stage, std::vector<uint32_t>& spirv,
                        std::string& log);
    // what the running renderer tells packs about itself (iris StandardMacros)
    struct EngineEnvironment {
        std::string vendor, renderer; // gpu strings, for MC_GL_VENDOR_* / MC_GL_RENDERER_*
        // iris feature flags the renderer implements (CUSTOM_IMAGES, COMPUTE_SHADERS, SSBO, ...); empty = the
        // flags every backend supports
        std::set<std::string> features;
        int mipmapLevels = 4;
    };
    // the iris feature flags every backend implements
    const std::set<std::string>& BaseFeatureFlags();
    MacroMap EngineMacros(int mcVersion, bool openglBackend, const EngineEnvironment& env = {});
}
