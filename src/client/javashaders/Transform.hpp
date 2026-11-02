#pragma once
#include "client/javashaders/Cond.hpp"
#include "client/javashaders/ProgramSet.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace perlin::jshader {
    constexpr int kAttribPosition = 0, kAttribNormal = 1, kAttribTangent = 2, kAttribColor0 = 4;
    constexpr int kAttribTexCoord0 = 10, kAttribTexCoord1 = 11, kAttribTexCoord2 = 12, kAttribTexCoord3 = 13;
    constexpr int kAttribTexCoord4 = 14, kAttribTexCoord5 = 15, kAttribTexCoord6 = 16, kAttribTexCoord15 = 25;
    constexpr uint32_t AttribBit(int attrib) {
        return 1u << attrib;
    }

    struct UniformDecl {
        std::string name;
        std::string type;
        std::string arraySize;
        int arrayCount = 0;
        int reg = 0, regs = 0, regsPerElement = 1;
        int components = 0;
    };

    struct SamplerDecl {
        std::string name;
        std::string type;
        int binding = 0;
        bool image = false;
        std::string imageFormat;
        std::string qualifiers;
    };

    struct InterfaceVar {
        std::string name;
        std::string type;
        int arrayCount = 0;
        std::string interpolation;
        int location = -1;
    };

    struct VertexInputs {
        bool position = false, color = false, uv0 = false, uv1 = false, uv2 = false, normal = false;
        bool mcEntity = false, mcMidTexCoord = false, atTangent = false, atMidBlock = false;
    };

    struct StageResult {
        Stage stage = Stage::Fragment;
        std::string glsl;
        std::vector<UniformDecl> uniforms;
        std::vector<SamplerDecl> samplers;
        std::vector<InterfaceVar> inputs, outputs;
        VertexInputs vertexInputs;
        std::vector<int> drawBuffers;
        std::string error;
        bool ok = true;
    };

    struct TransformOptions {
        MacroMap macros;
        std::string (*preprocess)(const std::string& text, Stage stage, std::string& error) = nullptr;
        std::map<std::string, int> fixedSamplerSlots;
        uint32_t availableAttribs = 0xFFFFFFFFu;
        bool flipVertexY = false;
        bool flipFragCoordY = false;
        bool zeroToOneDepth = true;
        // the rhi binds textures and samplers separately (set 1: texture 2n, sampler 2n+1): declare them that way and
        // let the pack's sampler names stand for the combined pair
        bool splitSamplers = false;
        // sample plain texture() / texture2D() calls at level 0 (full screen passes whose buffers have no mips)
        bool explicitLod = false;
        // custom image name -> binding in set 2 (the renderer's image table)
        std::map<std::string, int> imageSlots;
        // custom image name -> glsl format qualifier (r16ui ...), for images the pack declares without one
        std::map<std::string, std::string> imageFormats;
    };

    struct ProgramResult {
        StageResult vertex, fragment;
        std::vector<UniformDecl> uniforms;
        int uniformRegs = 0;
        std::vector<std::string> notes;
        bool ok() const { return vertex.ok && fragment.ok; }
    };

    ProgramResult TransformProgram(const ExpandedSource& vertexSource, const ExpandedSource& fragmentSource,
                                   const TransformOptions& options);

    // a compute program (.csh): the same uniform block / samplers / images as the graphics stages, plus the
    // dispatch size it asks for (iris: workGroups, else workGroupsRender against the render size, else 1:1)
    struct ComputeResult {
        StageResult stage;
        int uniformRegs = 0;
        uint32_t localSize[3] = {1, 1, 1};
        bool fixedGroups = false;
        uint32_t workGroups[3] = {1, 1, 1};
        float workGroupsRender[2] = {1.0f, 1.0f};
        bool ok() const { return stage.ok; }
    };
    ComputeResult TransformCompute(const ExpandedSource& source, const TransformOptions& options);

    const std::vector<UniformDecl>& BuiltinUniformDecls();
}
