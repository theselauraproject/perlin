#include "client/javashaders/GlslangBridge.hpp"

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <cctype>
#include <mutex>

namespace perlin::jshader {
    namespace {
        std::once_flag gInit;
        void Init() {
            std::call_once(gInit, [] { glslang::InitializeProcess(); });
        }

        EShLanguage Lang(Stage s) {
            switch (s) {
            case Stage::Vertex:
                return EShLangVertex;
            case Stage::Geometry:
                return EShLangGeometry;
            case Stage::Compute:
                return EShLangCompute;
            default:
                return EShLangFragment;
            }
        }

        class NoIncluder : public glslang::TShader::Includer {
        public:
            IncludeResult* includeSystem(const char*, const char*, size_t) override { return nullptr; }
            IncludeResult* includeLocal(const char*, const char*, size_t) override { return nullptr; }
            void releaseInclude(IncludeResult*) override {}
        };
    }

    std::string PreprocessGlsl(const std::string& text, Stage stage, std::string& error) {
        Init();
        glslang::TShader shader(Lang(stage));
        const char* src = text.c_str();
        shader.setStrings(&src, 1);
        shader.setEnvInput(glslang::EShSourceGlsl, Lang(stage), glslang::EShClientVulkan, 450);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
        NoIncluder includer;
        std::string out;
        if (!shader.preprocess(GetDefaultResources(), 450, ENoProfile, false, false, EShMsgDefault, &out,
                               includer)) {
            error = shader.getInfoLog();
            return {};
        }
        error = shader.getInfoLog();
        return out;
    }

    bool CompileToSpirv(const std::string& glsl, Stage stage, std::vector<uint32_t>& spirv,
                        std::string& log) {
        Init();
        glslang::TShader shader(Lang(stage));
        const char* src = glsl.c_str();
        shader.setStrings(&src, 1);
        shader.setEnvInput(glslang::EShSourceGlsl, Lang(stage), glslang::EShClientVulkan, 450);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
        const EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
        if (!shader.parse(GetDefaultResources(), 450, false, messages)) {
            log = shader.getInfoLog();
            return false;
        }
        glslang::TProgram program;
        program.addShader(&shader);
        if (!program.link(messages)) {
            log = std::string(shader.getInfoLog()) + program.getInfoLog();
            return false;
        }
        spv::SpvBuildLogger logger;
        glslang::SpvOptions options;
        options.disableOptimizer = true;
        glslang::GlslangToSpv(*program.getIntermediate(Lang(stage)), spirv, &logger, &options);
        log = logger.getAllMessages();
        return !spirv.empty();
    }

    const std::set<std::string>& BaseFeatureFlags() {
        static const std::set<std::string> flags = {
            "SEPARATE_HARDWARE_SAMPLERS", "HIGHER_SHADOWCOLOR",  "ENTITY_TRANSLUCENT", "REVERSED_CULLING",
            "BLOCK_EMISSION_ATTRIBUTE",   "CAN_DISABLE_WEATHER", "FADE_VARIABLE",      "PER_BUFFER_BLENDING"};
        return flags;
    }

    MacroMap EngineMacros(int mcVersion, bool opengl, const EngineEnvironment& env) {
        MacroMap m;
        m["MC_VERSION"] = std::to_string(mcVersion);
        m["MC_GL_VERSION"] = "460";
        m["MC_GLSL_VERSION"] = "460";
        // packs look like they run on iris 1.11.7, which perlin follows (uniforms, features, transforms)
        m["IS_IRIS"] = "";
        m["IRIS_VERSION"] = "11107";
        m["IRIS_TAG_SUPPORT"] = "2";
        m["IRIS_HAS_TRANSLUCENCY_SORTING"] = "";
        m["IRIS_REQUIRES_SEPARATE_ENTITY_DRAWS"] = "";
        m["MAX_COLOR_BUFFERS"] = "16";
        m["MC_MIPMAP_LEVEL"] = std::to_string(env.mipmapLevels);
        const char* dhBlocks[] = {"UNKNOWN", "LEAVES",    "STONE", "WOOD",       "METAL",      "DIRT",
                                  "LAVA",    "DEEPSLATE", "SNOW",  "SAND",       "TERRACOTTA", "NETHER_STONE",
                                  "WATER",   "GRASS",     "AIR",   "ILLUMINATED"};
        for (int i = 0; i < 16; i++)
            m[std::string("DH_BLOCK_") + dhBlocks[i]] = std::to_string(i);
#if defined(__APPLE__)
        m["MC_OS_MAC"] = "";
#elif defined(_WIN32)
        m["MC_OS_WINDOWS"] = "";
#elif defined(__linux__) || defined(__ANDROID__)
        m["MC_OS_LINUX"] = "";
#else
        m["MC_OS_UNKNOWN"] = "";
#endif
        std::string vendor = env.vendor, renderer = env.renderer;
        for (char& c : vendor)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : renderer)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto starts = [](const std::string& s, const char* p) { return s.rfind(p, 0) == 0; };
        const auto has = [](const std::string& s, const char* p) { return s.find(p) != std::string::npos; };
        // rhi vendor strings are not opengl's: match on the names inside them as well
        if (starts(vendor, "ati") || starts(vendor, "amd") || has(vendor, "amd") || has(vendor, "radeon"))
            m[starts(vendor, "ati") ? "MC_GL_VENDOR_ATI" : "MC_GL_VENDOR_AMD"] = "";
        else if (has(vendor, "intel"))
            m["MC_GL_VENDOR_INTEL"] = "";
        else if (has(vendor, "nvidia"))
            m["MC_GL_VENDOR_NVIDIA"] = "";
        else
            m["MC_GL_VENDOR_OTHER"] = "";
        const std::string& r = renderer.empty() ? vendor : renderer;
        if (has(r, "radeon") || starts(r, "amd") || starts(r, "ati"))
            m["MC_GL_RENDERER_RADEON"] = "";
        else if (has(r, "intel"))
            m["MC_GL_RENDERER_INTEL"] = "";
        else if (has(r, "quadro"))
            m["MC_GL_RENDERER_QUADRO"] = "";
        else if (has(r, "geforce") || has(r, "nvidia") || has(r, "rtx") || has(r, "gtx"))
            m["MC_GL_RENDERER_GEFORCE"] = "";
        else if (has(r, "mesa") || has(r, "llvmpipe"))
            m["MC_GL_RENDERER_MESA"] = "";
        else
            m["MC_GL_RENDERER_OTHER"] = "";
        // every flag the renderer supports, as iris gives shaders.properties; the instance narrows the set the
        // shaders see to the pack's iris.features.optional list
        for (const std::string& f : env.features.empty() ? BaseFeatureFlags() : env.features)
            m["IRIS_FEATURE_" + f] = "";
        m["MC_NORMAL_MAP"] = "";
        m["MC_SPECULAR_MAP"] = "";
        m["MC_HAND_DEPTH"] = "0.125";
        m["MC_RENDER_QUALITY"] = "1.0";
        m["MC_SHADOW_QUALITY"] = "1.0";
        m["MC_FXAA_LEVEL"] = "0";
        m["DYNAMIC_HANDLIGHT"] = "";
        const char* stages[] = {"NONE",
                                "SKY",
                                "SUNSET",
                                "CUSTOM_SKY",
                                "SUN",
                                "MOON",
                                "STARS",
                                "VOID",
                                "TERRAIN_SOLID",
                                "TERRAIN_CUTOUT_MIPPED",
                                "TERRAIN_CUTOUT",
                                "ENTITIES",
                                "BLOCK_ENTITIES",
                                "DESTROY",
                                "OUTLINE",
                                "DEBUG",
                                "HAND_SOLID",
                                "TERRAIN_TRANSLUCENT",
                                "TRIPWIRE",
                                "PARTICLES",
                                "CLOUDS",
                                "RAIN_SNOW",
                                "WORLD_BORDER",
                                "HAND_TRANSLUCENT"};
        for (int i = 0; i < static_cast<int>(sizeof(stages) / sizeof(stages[0])); i++)
            m[std::string("MC_RENDER_STAGE_") + stages[i]] = std::to_string(i);
        (void)opengl;
        return m;
    }
}
