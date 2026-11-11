#pragma once
#include "client/renderer/RenderStates.hpp"
#include "client/ui/Json.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::ui {
    class Json;
}

namespace perlin::material {
    struct Sampler {
        int index = 0;
        std::string filter = "Point";
        std::string wrap = "Clamp";
    };

    struct Stencil {
        bool enabled = false;
        bool write = false;
        StencilFunc func = StencilFunc::Always;
        StencilOp failOp = StencilOp::Keep;
        StencilOp depthFailOp = StencilOp::Keep;
        StencilOp passOp = StencilOp::Keep;
        uint8_t ref = 0;
        uint8_t readMask = 0xff;
        uint8_t writeMask = 0xff;
    };

    struct Def {
        std::string name;
        std::string sourceFamily;
        std::string sourcePass;
        std::string vertexShader;
        std::string geometryShader;
        std::string fragmentShader;
        std::string primitive = "Triangle";
        std::vector<std::string> vertexFields;
        std::set<std::string> defines;
        // defines a material took away from its parent ("-defines"): the game's engine-added ones (FANCY) are
        // not always in our resolved set, so their removal has to be remembered on its own
        std::set<std::string> removedDefines;
        std::set<std::string> states;
        std::vector<Sampler> samplers;
        BlendFactor blendSrc = BlendFactor::SrcAlpha;
        BlendFactor blendDst = BlendFactor::InvSrcAlpha;
        bool blendSet = false;
        DepthFunc depthFunc = DepthFunc::LessEqual;
        float depthBias = 0.0f;
        float depthBiasOgl = 0.0f;
        float slopeScaledDepthBias = 0.0f;
        Stencil stencil;
        bool msaa = true;
        std::map<std::string, Def> variants;

        bool Define(const std::string& d) const { return defines.contains(d); }
        bool State(const std::string& s) const { return states.contains(s); }
        bool Blending() const { return State("Blending"); }
        bool DepthWrite() const { return !State("DisableDepthWrite"); }
        bool DepthTest() const { return !State("DisableDepthTest"); }
        bool ColorWrite() const { return !State("DisableColorWrite"); }
        bool AlphaWrite() const { return !State("DisableAlphaWrite"); }
        bool NoCull() const { return State("DisableCulling"); }
        bool AlphaToCoverage() const { return State("EnableAlphaToCoverage"); }
        CullMode Cull() const {
            return NoCull() ? CullMode::None : State("InvertCulling") ? CullMode::Front : CullMode::Back;
        }
    };

    enum class Mode : uint8_t { Fancy, Sad, Gameface };

    class Db {
    public:
        void Load(Mode mode, const std::string& root);
        void AddText(const std::string& text, const std::set<std::string>& extraDefines);
        void AddPackDirectory(const std::string& dir);
        void Resolve();
        void InstallBuiltins();
        const Def* Find(const std::string& name) const;
        const Def* Variant(const std::string& name, const std::string& variant) const;
        size_t Count() const { return resolved.size(); }
        std::vector<std::string> Names() const;
        Mode CurrentMode() const { return mode; }

    private:
        struct Raw {
            std::string key;
            std::string name;
            std::string parent;
            std::set<std::string> extraDefines;
            const ui::Json* json = nullptr;
            size_t order = 0;
        };

        Mode mode = Mode::Fancy;
        std::vector<std::unique_ptr<ui::Json>> docs;
        std::vector<Raw> raws;
        std::unordered_map<std::string, size_t> rawIndex;
        std::unordered_map<std::string, Def> resolved;
        std::unordered_map<std::string, Def> builtins;
        std::unordered_map<std::string, int> resolving;

        void FillBuiltins();
        const Def* ResolveOne(const std::string& name);
        static void Apply(Def& def, const ui::Json& json);
    };

    class Registry {
    public:
        static Registry& Instance();
        void Configure(bool fancy);
        void SetRoot(const std::string& root);
        const Db& Get();
        uint32_t Generation() const { return generation; }
        Mode ModeValue() const { return fancy ? Mode::Fancy : Mode::Sad; }

    private:
        Db db;
        std::string root;
        bool loaded = false;
        bool fancy = true;
        uint32_t generation = 0;
    };

    BlendFactor ParseBlendFactor(const std::string& s, BlendFactor fallback);
    DepthFunc ParseDepthFunc(const std::string& s);
    StencilFunc ParseStencilFunc(const std::string& s);
    StencilOp ParseStencilOp(const std::string& s);
    std::string Lower(std::string s);
}
