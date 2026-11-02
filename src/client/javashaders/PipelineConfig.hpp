#pragma once
#include "client/javashaders/ProgramSet.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace perlin::jshader {
    constexpr int kMaxColorTex = 16;
    constexpr int kMaxShadowColor = 2;

    enum class BufferFormat {
        RGBA8,
        RGBA16,
        RGBA16F,
        RGBA32F,
        R8,
        RG8,
        R16F,
        RG16F,
        R32F,
        RGB8,
        RGB16F,
        R11F_G11F_B10F,
        RGB10_A2,
        RG16,
        R16,
        RGBA8_SNORM,
        RGB16,
        RGBA32I,
        RG32F
    };
    BufferFormat ParseBufferFormat(const std::string& text, BufferFormat fallback);

    struct BlendFactors {
        bool set = false;
        bool off = false;
        int src = 0, dst = 0, srcA = 0, dstA = 0;
    };

    struct AlphaTest {
        bool set = false;
        bool off = false;
        int func = 0x204;
        float ref = 0.1f;
    };

    struct BufferSetup {
        BufferFormat format = BufferFormat::RGBA8;
        bool clear = true;
        float clearColor[4] = {0, 0, 0, 0};
        bool mipmap = false;
        bool linear = true;
        float scaleX = 1.0f, scaleY = 1.0f;
        int fixedWidth = 0, fixedHeight = 0;
    };

    struct CustomTexture {
        std::string stage;
        std::string sampler;
        std::string path;
        std::string type = "texture2D";
    };

    // iris image.<name> = <sampler|none> <format> <internal format> <pixel type> <clear> <relative> <size...>
    struct CustomImage {
        std::string name, sampler;
        std::string format, internalFormat, pixelType; // gl names, lower case (rgba, rgba16f, half_float)
        int dims = 2;                                  // 1, 2 or 3
        int width = 1, height = 1, depth = 1;
        bool clear = false;
        bool relative = false;
        float relativeWidth = 1.0f, relativeHeight = 1.0f;
    };

    // iris bufferObject.<n> = <bytes> [<relative> <scaleX> <scaleY>]
    struct BufferObject {
        int index = 0;
        uint64_t size = 0;
        bool relative = false;
        float scaleX = 1.0f, scaleY = 1.0f;
    };

    struct ProgramConfig {
        BlendFactors blend;
        std::map<int, BlendFactors> blendPerBuffer;
        AlphaTest alpha;
        float scale = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
        bool hasScale = false;
        int explicitEnabled = -1;
        std::map<int, bool> flip;
        std::vector<int> drawBuffers;
    };

    struct PipelineConfig {
        std::array<BufferSetup, kMaxColorTex> color;
        int shadowMapResolution = 2048;
        float shadowDistance = 128.0f;
        float shadowIntervalSize = 2.0f;
        float shadowDistanceRenderMul = -1.0f;
        bool shadowHardwareFiltering[2] = {false, false};
        bool shadowFilterLinear[2] = {true, true};
        bool shadowMipmap[2] = {false, false};
        bool shadowColorLinear[2] = {true, true};
        BufferFormat shadowColorFormat[2] = {BufferFormat::RGBA8, BufferFormat::RGBA8};
        bool shadowColorClear[2] = {true, true};
        bool shadowEnabled = false;
        bool depthMipmap = false;
        float sunPathRotation = 0.0f;
        float eyeBrightnessHalflife = 10.0f, centerDepthHalflife = 1.0f, wetnessHalflife = 600.0f,
              drynessHalflife = 200.0f;
        float ambientOcclusionLevel = 1.0f;
        bool oldLighting = false, oldHandLight = true, separateAo = false, underwaterOverlay = true,
             sun = true, moon = true, vignette = true;
        bool rainDepth = false, beaconBeamDepth = false, dynamicHandLight = true, frustumCulling = true,
             shadowTranslucent = true;
        std::string clouds = "default";
        std::string cloudShadow;
        std::string particlesBeforeDeferred;
        std::map<std::string, ProgramConfig> programs;
        std::vector<CustomTexture> textures;
        std::vector<CustomImage> images;
        std::vector<BufferObject> bufferObjects;
        std::string noiseTexture;
        std::vector<std::pair<std::string, std::string>> customUniformProps;
        std::vector<std::string> problems;

        const ProgramConfig* Find(const std::string& program) const {
            const auto it = programs.find(program);
            return it == programs.end() ? nullptr : &it->second;
        }
    };

    PipelineConfig ParsePipelineConfig(ShaderPackInstance& pack);
    // the glsl image format qualifier of an iris internal format name ("rgba16f", "r16ui"; "rgba" -> "rgba8")
    std::string ImageFormatQualifier(const std::string& internalFormat);
    int GlEnumFromName(const std::string& name);
}
