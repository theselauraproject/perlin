#pragma once
#include "rhi/Types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace rhi {
    enum class ResourceKind : uint8_t {
        UniformBuffer,
        StorageBuffer,
        StorageBufferRW,
        SampledTexture,
        StorageTexture,
        Sampler
    };

    // one resource a source-form shader (wgsl, hlsl, msl) declares, with where its binding number sits in the text so a
    // backend can rewrite it
    struct ShaderResource {
        uint32_t set = 0;
        uint32_t binding = 0;
        ResourceKind kind = ResourceKind::UniformBuffer;
        TextureDim dim = TextureDim::Tex2D;
        // sampler compares (shadow sampler) / texture is a depth texture
        bool comparison = false;
        bool depth = false;
        std::string name;
        // the whole `register(...)` / `@binding(..) @group(..)` / `[[texture(..)]]` clause, for rewriting
        size_t clauseBegin = 0;
        size_t clauseEnd = 0;
        // hlsl register class letter (b t s u), 0 elsewhere
        char registerClass = 0;
        // msl: the argument table index written in the source ([[texture(n)]]), which equals binding unless a
        // `// perlin:msl-sets` line moved a range of indices to another set
        uint32_t nativeIndex = 0;
    };

    struct ShaderInterface {
        std::vector<ShaderResource> resources;
        bool usesPushConstants = false;

        const ShaderResource* Find(uint32_t set, uint32_t binding, ResourceKind kind) const;
    };

    // reads the resource declarations the slang compiler emitted. Only the dialect slang writes is understood.
    ShaderInterface ReflectShader(ShaderFormat format, std::string_view source);
}
