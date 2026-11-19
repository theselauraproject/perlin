#include "client/systems/entity/Materials.hpp"
#include "client/systems/gfx/MaterialDb.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {
    int failures = 0;

    void Expect(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            failures++;
        }
    }

    const char* kSample = R"MAT({
  "materials": {
    "version": "1.0.0",
    "base": {
      "+defines": [ "A", "B" ],
      "+states": [ "Blending", "DisableDepthWrite" ],
      "blendSrc": "SourceAlpha",
      "blendDst": "OneMinusSrcAlpha",
      "depthFunc": "Always",
      "vertexShader": "shaders/x.vertex",
      "vertexFields": [ { "field": "Position" }, { "field": "UV0" } ],
      "+samplerStates": [ { "samplerIndex": 0, "textureFilter": "Point" } ],
      "variants": [
        { "skinning": { "+defines": [ "USE_SKINNING" ], "+states": [ "DisableCulling" ], "vertexFields": [ { "field": "Position" }, { "field": "BoneId0" }, { "field": "UV0" } ] } }
      ]
    },
    "child:base": {
      "-defines": [ "A" ],
      "-states": [ "DisableDepthWrite" ],
      "blendDst": "One",
      "frontFace": { "stencilFunc": "Equal", "stencilPassOp": "Replace" },
      "+states": [ "EnableStencilTest" ],
      "stencilRef": 2
    },
    "grand:child": { "+defines": [ "C" ], "primitiveMode": "Line" },
    "late:missing_parent": { "+defines": [ "Z" ] }
  }
})MAT";

    const char* kUnwrapped = R"MAT({
  "sun_like": { "states": [ "Blending" ], "blendSrc": "SourceAlpha", "blendDst": "One" }
})MAT";
}

int main() {
    using namespace perlin;
    material::Db db;
    db.AddText(kSample, {});
    db.AddText(kUnwrapped, {"FANCY"});
    db.Resolve();

    const material::Def* base = db.Find("base");
    Expect(base && base->Define("A") && base->Define("B") && base->Blending() && !base->DepthWrite() &&
               base->depthFunc == DepthFunc::Always && base->samplers.size() == 1,
           "base");
    const material::Def* child = db.Find("child");
    Expect(child && !child->Define("A") && child->Define("B") && child->DepthWrite() &&
               child->blendDst == BlendFactor::One && child->blendSrc == BlendFactor::SrcAlpha &&
               child->stencil.enabled && child->stencil.ref == 2 &&
               child->stencil.func == StencilFunc::Equal && child->stencil.passOp == StencilOp::Replace,
           "child overrides");
    const material::Def* grand = db.Find("grand");
    Expect(grand && grand->Define("C") && grand->Define("B") && grand->primitive == "Line" &&
               grand->vertexShader == "shaders/x.vertex",
           "grandchild");
    const material::Def* skinned = db.Variant("child", "skinning");
    Expect(skinned && skinned->Define("USE_SKINNING") && skinned->NoCull() &&
               skinned->vertexFields.size() == 3,
           "variant");
    const material::Def* baseVariant = db.Variant("base", "skinning");
    Expect(baseVariant && baseVariant->Define("USE_SKINNING") && baseVariant->NoCull(), "base variant");
    const material::Def* unwrapped = db.Find("sun_like");
    Expect(unwrapped && unwrapped->Define("FANCY") && unwrapped->blendDst == BlendFactor::One,
           "unwrapped file and list defines");
    Expect(db.Find("late") && db.Find("late")->Define("Z"), "unknown parent tolerated");

    material::Db empty;
    empty.Load(material::Mode::Fancy, "");
    empty.Resolve();
    empty.InstallBuiltins();
    const material::Def* clouds = empty.Find("clouds");
    Expect(clouds && clouds->Blending() && !clouds->AlphaWrite(), "builtin fancy clouds");
    material::Db sad;
    sad.Load(material::Mode::Sad, "");
    sad.Resolve();
    sad.InstallBuiltins();
    Expect(sad.Find("clouds") && !sad.Find("clouds")->Blending(), "builtin fast clouds");
    const material::Def* crack = empty.Find("cracks_overlay");
    Expect(crack && crack->blendSrc == BlendFactor::DstColor && crack->blendDst == BlendFactor::Zero,
           "builtin cracks");
    const material::Def* stars = empty.Find("stars");
    Expect(stars && stars->blendSrc == BlendFactor::InvDstColor, "builtin stars");
    const material::Def* add = empty.Find("particles_add");
    Expect(add && add->blendDst == BlendFactor::One && !add->DepthWrite(), "builtin particles add");

    const material::Def* terrain = empty.Find("terrain_opaque");
    Expect(terrain && terrain->sourceFamily == "RenderChunk" && terrain->sourcePass == "Opaque" &&
               terrain->vertexShader == "materials/RenderChunk/Opaque.Vertex.glsl",
           "terrain references upstream shader pass");
    const material::Def* cutout = empty.Find("RenderChunk.AlphaTest");
    Expect(cutout && cutout->Define("ALPHA_TEST"), "upstream pass lookup is case insensitive");
    const material::Def* depthOnly = empty.Find("RenderChunk.DepthOnlyOpaque");
    Expect(depthOnly && !depthOnly->ColorWrite() && !depthOnly->AlphaWrite() && depthOnly->DepthWrite(),
           "upstream depth-only pass");
    const material::Def* sheep = empty.Find("sheep");
    Expect(sheep && sheep->sourceFamily == "Entity" && entity::InfoFromDef(*sheep).colorMask,
           "upstream color mask pass");
    Expect(entity::MaterialLibrary::Builtin("entity_glint").glint, "fallback uses source glint pass");
    Expect(entity::MaterialLibrary::Builtin("creeper").resolved,
           "legacy entity alias resolves through upstream");
    using Info = entity::MaterialInfo;
    for (const char* mob : {"spider", "enderman", "phantom", "ghast", "magma_cube", "drowned", "blaze_head"}) {
        const Info m = entity::MaterialLibrary::Builtin(mob);
        Expect(m.blend == Info::Blend::AlphaTest && m.emissive == 1 && m.noCull && m.depthWrite,
               "vanilla emissive_alpha mobs are lit cutouts with glowing eyes");
    }
    for (const char* mob : {"spider_invisible", "enderman_invisible", "phantom_invisible"})
        Expect(entity::MaterialLibrary::Builtin(mob).emissive == 3, "invisible variants draw only emissive texels");
    Expect(entity::MaterialLibrary::Builtin("blaze_body").emissive == 1 &&
               entity::MaterialLibrary::Builtin("blaze_body").blend == Info::Blend::Opaque,
           "entity_emissive stays opaque");
    Expect(entity::MaterialLibrary::Builtin("cow").blend == Info::Blend::Opaque &&
               entity::MaterialLibrary::Builtin("cow").emissive == 0,
           "plain entity is opaque");
    Expect(entity::MaterialLibrary::Builtin("zombie").blend == Info::Blend::AlphaTest &&
               entity::MaterialLibrary::Builtin("zombie").noCull,
           "entity_alphatest is a double sided cutout");
    Expect(entity::MaterialLibrary::Builtin("creaking_eyes").emissive == 2 &&
               entity::MaterialLibrary::Builtin("creaking_eyes").depthFunc == DepthFunc::Equal &&
               !entity::MaterialLibrary::Builtin("creaking_eyes").colorMask,
           "emissive layers are fullbright overlays");
    Expect(entity::MaterialLibrary::Builtin("warden_bioluminescent_layer").blend == Info::Blend::Alpha &&
               entity::MaterialLibrary::Builtin("warden_bioluminescent_layer").emissive == 2,
           "warden glow layer blends");
    Expect(entity::MaterialLibrary::Builtin("charged_creeper").blend == Info::Blend::Additive,
           "charged creeper is additive");
    Expect(entity::MaterialLibrary::Builtin("cat").colorMask && entity::MaterialLibrary::Builtin("wolf").colorMask,
           "change_color mobs use the color mask");
    {
        material::Db vanilla;
        vanilla.Load(material::Mode::Fancy, "");
        vanilla.Resolve();
        vanilla.InstallBuiltins();
        const material::Def* spider = vanilla.Find("spider");
        Expect(spider && spider->sourceFamily == "Entity" && spider->sourcePass == "AlphaTestEmissive" &&
                   !spider->Blending(),
               "spider maps to Entity.AlphaTestEmissive");
        const material::Def* invisible = vanilla.Find("enderman_invisible");
        Expect(invisible && invisible->sourcePass == "AlphaTestEmissiveOnly", "invisible maps to EmissiveOnly");
        vanilla.AddText(R"MAT({"materials":{"my_glow:spider":{"+states":["Blending"]}}})MAT", {});
        vanilla.Resolve();
        vanilla.InstallBuiltins();
        const material::Def* glow = vanilla.Find("my_glow");
        Expect(glow && glow->sourcePass == "AlphaTestEmissive" && glow->Blending() &&
                   entity::InfoFromDef(*glow).emissive == 1,
               "pack materials inherit builtin entity semantics");
    }
    Expect(entity::MaterialLibrary::Builtin("unknown_pack_material").blend ==
               entity::MaterialInfo::Blend::AlphaTest,
           "unknown entities have a source-backed cutout fallback");
    empty.AddText(R"MAT({"materials":{"pack_terrain:RenderChunk.Opaque":{"+states":["DisableCulling"]}}})MAT",
                  {});
    empty.Resolve();
    empty.InstallBuiltins();
    const material::Def* packTerrain = empty.Find("pack_terrain");
    Expect(packTerrain && packTerrain->sourceFamily == "RenderChunk" && packTerrain->NoCull(),
           "pack inherits upstream pass");
    empty.AddText(R"MAT({"materials":{"terrain_opaque":{"+defines":["PACK_OVERRIDE"]}}})MAT", {});
    empty.Resolve();
    empty.InstallBuiltins();
    Expect(empty.Find("terrain_opaque")->Define("PACK_OVERRIDE"), "pack overrides source alias");
    add = empty.Find("particles_add");

    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "perlin_material_selftest";
    fs::create_directories(dir / "materials");
    {
        std::ofstream f(dir / "materials" / "entity.material");
        f << R"MAT({"materials":{"version":"1.0.0","entity_static":{"+defines":["X"]},"entity:entity_static":{"+defines":["USE_OVERLAY"]}}})MAT";
        std::ofstream g(dir / "materials" / "gameface.material");
        g << R"MAT({"materials":{"ST_Standard":{}}})MAT";
    }
    material::Db fromDisk;
    fromDisk.Load(material::Mode::Fancy, dir.string());
    fromDisk.Resolve();
    fromDisk.InstallBuiltins();
    const material::Def* entity = fromDisk.Find("entity");
    Expect(entity && entity->Define("X") && entity->Define("USE_OVERLAY") && entity->Define("FANCY"),
           "directory load with fancy define");
    Expect(!fromDisk.Find("ST_Standard"), "gameface skipped");
    Expect(fromDisk.Find("clouds") != nullptr, "builtins fill gaps");
    fs::remove_all(dir);

    const entity::MaterialInfo info = entity::InfoFromDef(*add);
    Expect(info.blend == entity::MaterialInfo::Blend::Additive && info.emissive == 2 && !info.depthWrite,
           "info from def");

    if (failures == 0)
        std::printf("material_selftest OK\n");
    return failures == 0 ? 0 : 1;
}
