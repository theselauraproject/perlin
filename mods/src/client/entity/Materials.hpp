#pragma once
#include "client/renderer/MaterialDb.hpp"
#include "client/renderer/RenderStates.hpp"

#include <cstdint>
#include <string>

namespace perlin::entity {
    struct MaterialInfo {
        enum class Blend : uint8_t { Opaque, AlphaTest, Alpha, Additive };
        Blend blend = Blend::AlphaTest;
        bool depthWrite = true;
        bool noCull = false;
        uint8_t emissive = 0;
        bool hurtTint = true;
        bool resolved = false;
        bool customBlend = false;
        BlendFactor blendSrc = BlendFactor::SrcAlpha;
        BlendFactor blendDst = BlendFactor::InvSrcAlpha;
        DepthFunc depthFunc = DepthFunc::LessEqual;
        bool depthTest = true;
        bool colorWrite = true;
        bool alphaWrite = true;
        bool alphaToCoverage = false;
        CullMode cull = CullMode::Back;
        float depthBias = 0.0f;
        bool glint = false;
        bool skinning = false;
        bool colorMask = false;
        // the entity shader's per-face shading (top bright, sides darker) lives behind FANCY; a material that
        // removes it (pack holograms, text) draws flat
        bool faceShading = true;
    };

    MaterialInfo InfoFromDef(const material::Def& def);

    class MaterialLibrary {
    public:
        void Load(const std::string& root);
        void AddDirectory(const std::string& dir);
        MaterialInfo Find(const std::string& name) const;
        const material::Def* Def(const std::string& name) const { return db.Find(name); }
        size_t CustomCount() const { return db.Count(); }
        static MaterialInfo Builtin(const std::string& name);

    private:
        material::Db db;
    };
}
