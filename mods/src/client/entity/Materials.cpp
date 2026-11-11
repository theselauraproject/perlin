#include "client/entity/Materials.hpp"
#include "util/Strings.hpp"

namespace perlin::entity {
    namespace {}

    MaterialInfo MaterialLibrary::Builtin(const std::string& rawName) {
        static const material::Db defaults = [] {
            material::Db db;
            db.Load(material::Mode::Fancy, "");
            db.Resolve();
            db.InstallBuiltins();
            return db;
        }();
        const material::Def* def = defaults.Find(rawName);
        if (!def)
            def = defaults.Find("entity_alphatest");
        return InfoFromDef(*def);
    }

    MaterialInfo InfoFromDef(const material::Def& def) {
        MaterialInfo m;
        m.resolved = true;
        const bool blending = def.Blending();
        const bool additive = blending && def.blendDst == BlendFactor::One;
        if (blending)
            m.blend = additive ? MaterialInfo::Blend::Additive : MaterialInfo::Blend::Alpha;
        else if (def.Define("ALPHA_TEST"))
            m.blend = MaterialInfo::Blend::AlphaTest;
        else
            m.blend = !def.sourcePass.empty() || str::Lower(def.name).starts_with("opaque")
                          ? MaterialInfo::Blend::Opaque
                          : MaterialInfo::Blend::AlphaTest;
        m.customBlend = blending && def.blendSet;
        m.blendSrc = def.blendSrc;
        m.blendDst = def.blendDst;
        m.depthFunc = def.depthFunc;
        m.depthTest = def.DepthTest();
        m.depthWrite = def.DepthWrite() && !additive;
        m.colorWrite = def.ColorWrite();
        m.alphaWrite = def.AlphaWrite();
        m.alphaToCoverage = def.AlphaToCoverage();
        m.cull = def.Cull();
        m.noCull = def.NoCull();
        m.depthBias = def.depthBias;
        m.skinning = def.Define("USE_SKINNING");
        m.glint = def.Define("GLINT");
        m.hurtTint = def.Define("USE_OVERLAY") && !additive;
        m.emissive = def.Define("USE_ONLY_EMISSIVE") || additive ? 2 : def.Define("USE_EMISSIVE") ? 1 : 0;
        m.faceShading = !def.removedDefines.contains("FANCY");
        if (def.Define("USE_COLOR_MASK") || str::Lower(def.name) == "sheep") {
            m.colorMask = true;
            if (!blending)
                m.blend = MaterialInfo::Blend::AlphaTest;
        }
        return m;
    }

    void MaterialLibrary::Load(const std::string& root) {
        material::Registry::Instance().SetRoot(root);
        db.Load(material::Registry::Instance().ModeValue(), root);
        db.Resolve();
        db.InstallBuiltins();
    }

    void MaterialLibrary::AddDirectory(const std::string& dir) {
        db.AddPackDirectory(dir);
        db.Resolve();
        db.InstallBuiltins();
    }

    MaterialInfo MaterialLibrary::Find(const std::string& name) const {
        if (const material::Def* d = db.Find(name))
            return InfoFromDef(*d);
        return Builtin(str::Lower(name));
    }
}
