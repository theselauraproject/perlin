#include "client/javashaders/GlslangBridge.hpp"
#include "client/javashaders/Biomes.hpp"

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <cctype>
#include <cstdlib>
#include <mutex>

#if PERLIN_SPIRV_OPT
#include <spirv-tools/optimizer.hpp>
#endif

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

        void OptimizeSpirv(std::vector<uint32_t>& spirv) {
#if PERLIN_SPIRV_OPT
            static const bool off = std::getenv("PERLIN_JSHADER_NO_OPT") != nullptr;
            if (off || spirv.empty())
                return;
            spvtools::Optimizer optimizer(SPV_ENV_VULKAN_1_1);
            optimizer.SetMessageConsumer([](spv_message_level_t, const char*, const spv_position_t&, const char*) {});
            optimizer.RegisterPerformancePasses();
            spvtools::OptimizerOptions options;
            options.set_run_validator(false);
            std::vector<uint32_t> optimized;
            if (optimizer.Run(spirv.data(), spirv.size(), &optimized, options) && !optimized.empty())
                spirv.swap(optimized);
#else
            (void)spirv;
#endif
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
        OptimizeSpirv(spirv);
        return !spirv.empty();
    }

    bool HlslToSpirv(const std::string& hlslIn, Stage stage, std::vector<uint32_t>& spirv, std::string& log) {
        Init();

        std::string hlsl;
        hlsl.reserve(hlslIn.size() + 1024);
        size_t at = 0;
        bool inEntryStruct = false;
        while (at < hlslIn.size()) {
            size_t nl = hlslIn.find('\n', at);
            if (nl == std::string::npos)
                nl = hlslIn.size();
            std::string line = hlslIn.substr(at, nl - at);
            if (line.rfind("struct SPIRV_Cross_", 0) == 0)
                inEntryStruct = true;
            else if (inEntryStruct && line.rfind("};", 0) == 0)
                inEntryStruct = false;
            else if (inEntryStruct) {
                const size_t sem = line.find(": TEXCOORD");
                if (sem != std::string::npos) {
                    const int loc = std::atoi(line.c_str() + sem + 10);
                    const size_t first = line.find_first_not_of(" \t");
                    line.insert(first, "[[vk::location(" + std::to_string(loc) + ")]] ");
                }
            }
            hlsl += line;
            hlsl += '\n';
            at = nl + 1;
        }
        glslang::TShader shader(Lang(stage));
        const char* src = hlsl.c_str();
        shader.setStrings(&src, 1);
        shader.setEnvInput(glslang::EShSourceHlsl, Lang(stage), glslang::EShClientVulkan, 100);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
        shader.setEntryPoint("main");
        shader.setSourceEntryPoint("main");
        const EShMessages messages =
            static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl);
        if (!shader.parse(GetDefaultResources(), 450, false, messages)) {
            log = shader.getInfoLog();
            return false;
        }
        glslang::TProgram program;
        program.addShader(&shader);
        if (!program.link(messages) || !program.mapIO()) {
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

            "SEPARATE_HARDWARE_SAMPLERS", "ENTITY_TRANSLUCENT",  "REVERSED_CULLING",
            "BLOCK_EMISSION_ATTRIBUTE",   "CAN_DISABLE_WEATHER", "FADE_VARIABLE",
            "PER_BUFFER_BLENDING"};
        return flags;
    }

    MacroMap EngineMacros(int mcVersion, bool opengl, const EngineEnvironment& env) {
        MacroMap m;
        m["MC_VERSION"] = std::to_string(mcVersion);
        m["MC_GL_VERSION"] = "460";
        m["MC_GLSL_VERSION"] = "460";

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

        const auto& biomes = JavaBiomes();
        for (size_t i = 0; i < biomes.size(); i++) {
            std::string name = biomes[i].name;
            for (char& ch : name)
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            m["BIOME_" + name] = std::to_string(i);
        }
        const auto& cats = BiomeCategoryNames();
        for (size_t i = 0; i < cats.size(); i++)
            m["CAT_" + cats[i]] = std::to_string(i);
        m["PPT_NONE"] = "0";
        m["PPT_RAIN"] = "1";
        m["PPT_SNOW"] = "2";
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
