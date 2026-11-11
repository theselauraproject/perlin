#include "client/renderer/MaterialDb.hpp"

#include "client/ui/Json.hpp"
#include "MaterialCatalog.hpp"
#include "util/Strings.hpp"
#include "util/FileIo.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include "util/Log.hpp"

namespace perlin::material {
    namespace fs = std::filesystem;
    using Json = ui::Json;

    BlendFactor ParseBlendFactor(const std::string& raw, BlendFactor fallback) {
        const std::string s = str::Lower(raw);
        if (s == "zero")
            return BlendFactor::Zero;
        if (s == "one")
            return BlendFactor::One;
        if (s == "sourcecolor" || s == "srccolor")
            return BlendFactor::SrcColor;
        if (s == "oneminussourcecolor" || s == "oneminussrccolor")
            return BlendFactor::InvSrcColor;
        if (s == "sourcealpha" || s == "srcalpha")
            return BlendFactor::SrcAlpha;
        if (s == "oneminussourcealpha" || s == "oneminussrcalpha")
            return BlendFactor::InvSrcAlpha;
        if (s == "destcolor" || s == "dstcolor" || s == "destinationcolor")
            return BlendFactor::DstColor;
        if (s == "oneminusdestcolor" || s == "oneminusdstcolor" || s == "oneminusdestinationcolor")
            return BlendFactor::InvDstColor;
        if (s == "destalpha" || s == "dstalpha" || s == "destinationalpha")
            return BlendFactor::DstAlpha;
        if (s == "oneminusdestalpha" || s == "oneminusdstalpha" || s == "oneminusdestinationalpha")
            return BlendFactor::InvDstAlpha;
        if (s == "sourcealphasaturate")
            return BlendFactor::SrcAlphaSaturate;
        return fallback;
    }

    DepthFunc ParseDepthFunc(const std::string& raw) {
        const std::string s = str::Lower(raw);
        if (s == "less")
            return DepthFunc::Less;
        if (s == "equal")
            return DepthFunc::Equal;
        if (s == "always")
            return DepthFunc::Always;
        if (s == "greater")
            return DepthFunc::Greater;
        if (s == "greaterequal")
            return DepthFunc::GreaterEqual;
        if (s == "notequal")
            return DepthFunc::NotEqual;
        if (s == "never")
            return DepthFunc::Never;
        return DepthFunc::LessEqual;
    }

    StencilFunc ParseStencilFunc(const std::string& raw) {
        const std::string s = str::Lower(raw);
        if (s == "equal")
            return StencilFunc::Equal;
        if (s == "notequal")
            return StencilFunc::NotEqual;
        if (s == "less")
            return StencilFunc::Less;
        if (s == "lessequal")
            return StencilFunc::LessEqual;
        if (s == "greater")
            return StencilFunc::Greater;
        if (s == "greaterequal")
            return StencilFunc::GreaterEqual;
        if (s == "never")
            return StencilFunc::Never;
        return StencilFunc::Always;
    }

    StencilOp ParseStencilOp(const std::string& raw) {
        const std::string s = str::Lower(raw);
        if (s == "replace")
            return StencilOp::Replace;
        if (s == "zero")
            return StencilOp::Zero;
        if (s == "incr" || s == "increment")
            return StencilOp::Increment;
        if (s == "decr" || s == "decrement")
            return StencilOp::Decrement;
        if (s == "invert")
            return StencilOp::Invert;
        return StencilOp::Keep;
    }

    namespace {
        void StringList(const Json* arr, std::vector<std::string>& out) {
            if (!arr || !arr->IsArray())
                return;
            for (const Json& v : arr->array)
                if (v.IsString())
                    out.push_back(v.string);
        }

        void ApplyStencilFace(Stencil& st, const Json& face) {
            if (const Json* v = face.Find("stencilFunc"))
                st.func = ParseStencilFunc(v->StringOr(""));
            if (const Json* v = face.Find("stencilFailOp"))
                st.failOp = ParseStencilOp(v->StringOr(""));
            if (const Json* v = face.Find("stencilDepthFailOp"))
                st.depthFailOp = ParseStencilOp(v->StringOr(""));
            if (const Json* v = face.Find("stencilPassOp"))
                st.passOp = ParseStencilOp(v->StringOr(""));
            if (const Json* v = face.Find("stencilPass"))
                st.passOp = ParseStencilOp(v->StringOr(""));
        }

    }

    void Db::Apply(Def& def, const Json& json) {
        std::vector<std::string> list;
        if (const Json* v = json.Find("defines")) {
            def.defines.clear();
            def.removedDefines.clear();
            list.clear();
            StringList(v, list);
            def.defines.insert(list.begin(), list.end());
        }
        list.clear();
        StringList(json.Find("+defines"), list);
        def.defines.insert(list.begin(), list.end());
        for (const std::string& d : list)
            def.removedDefines.erase(d);
        list.clear();
        StringList(json.Find("-defines"), list);
        for (const std::string& d : list) {
            def.defines.erase(d);
            def.removedDefines.insert(d);
        }

        if (const Json* v = json.Find("states")) {
            def.states.clear();
            list.clear();
            StringList(v, list);
            def.states.insert(list.begin(), list.end());
        }
        list.clear();
        StringList(json.Find("+states"), list);
        def.states.insert(list.begin(), list.end());
        list.clear();
        StringList(json.Find("-states"), list);
        for (const std::string& s : list)
            def.states.erase(s);

        const auto readSamplers = [&](const Json* arr) {
            if (!arr || !arr->IsArray())
                return;
            for (const Json& s : arr->array) {
                if (!s.IsObject())
                    continue;
                Sampler sampler;
                sampler.index =
                    static_cast<int>(s.Find("samplerIndex") ? s.Find("samplerIndex")->NumberOr(0) : 0);
                if (const Json* f = s.Find("textureFilter"))
                    sampler.filter = f->StringOr("Point");
                if (const Json* w = s.Find("textureWrap"))
                    sampler.wrap = w->StringOr("Clamp");
                bool replaced = false;
                for (Sampler& existing : def.samplers)
                    if (existing.index == sampler.index) {
                        if (!s.Find("textureFilter"))
                            sampler.filter = existing.filter;
                        if (!s.Find("textureWrap"))
                            sampler.wrap = existing.wrap;
                        existing = sampler;
                        replaced = true;
                    }
                if (!replaced)
                    def.samplers.push_back(sampler);
            }
        };
        if (const Json* v = json.Find("samplerStates")) {
            def.samplers.clear();
            readSamplers(v);
        }
        readSamplers(json.Find("+samplerStates"));

        if (const Json* v = json.Find("vertexShader"))
            def.vertexShader = v->StringOr(def.vertexShader);
        if (const Json* v = json.Find("vrGeometryShader"))
            def.geometryShader = v->StringOr(def.geometryShader);
        if (const Json* v = json.Find("fragmentShader"))
            def.fragmentShader = v->StringOr(def.fragmentShader);
        if (const Json* v = json.Find("primitiveMode"))
            def.primitive = v->StringOr(def.primitive);
        if (const Json* v = json.Find("vertexFields"); v && v->IsArray()) {
            def.vertexFields.clear();
            for (const Json& f : v->array)
                if (const Json* name = f.Find("field"))
                    def.vertexFields.push_back(name->StringOr(""));
        }
        if (const Json* v = json.Find("blendSrc")) {
            def.blendSrc = ParseBlendFactor(v->StringOr(""), def.blendSrc);
            def.blendSet = true;
        }
        if (const Json* v = json.Find("blendDst")) {
            def.blendDst = ParseBlendFactor(v->StringOr(""), def.blendDst);
            def.blendSet = true;
        }
        if (const Json* v = json.Find("depthFunc"))
            def.depthFunc = ParseDepthFunc(v->StringOr(""));
        if (const Json* v = json.Find("depthBias"))
            def.depthBias = static_cast<float>(v->NumberOr(def.depthBias));
        if (const Json* v = json.Find("depthBiasOGL"))
            def.depthBiasOgl = static_cast<float>(v->NumberOr(def.depthBiasOgl));
        if (const Json* v = json.Find("slopeScaledDepthBias"))
            def.slopeScaledDepthBias = static_cast<float>(v->NumberOr(def.slopeScaledDepthBias));
        if (const Json* v = json.Find("msaaSupport")) {
            const std::string m = v->StringOr("Both");
            def.msaa = m != "NonMSAA";
        }
        if (const Json* v = json.Find("frontFace"); v && v->IsObject())
            ApplyStencilFace(def.stencil, *v);
        ApplyStencilFace(def.stencil, json);
        if (const Json* v = json.Find("stencilRef"))
            def.stencil.ref = static_cast<uint8_t>(v->NumberOr(def.stencil.ref));
        if (const Json* v = json.Find("stencilRefOverride"))
            def.stencil.ref = static_cast<uint8_t>(v->NumberOr(def.stencil.ref));
        if (const Json* v = json.Find("stencilReadMask"))
            def.stencil.readMask = static_cast<uint8_t>(v->NumberOr(def.stencil.readMask));
        if (const Json* v = json.Find("stencilWriteMask"))
            def.stencil.writeMask = static_cast<uint8_t>(v->NumberOr(def.stencil.writeMask));
        def.stencil.enabled = def.State("EnableStencilTest");
        def.stencil.write = def.State("StencilWrite");
    }

    void Db::AddText(const std::string& text, const std::set<std::string>& extraDefines) {
        auto doc = std::make_unique<Json>();
        std::string error;
        if (!Json::Parse(text, *doc, error)) {
            Log("material", "parse failure: {}", error);
            return;
        }
        const Json* table = doc->Find("materials");
        if (!table)
            table = doc.get();
        if (!table->IsObject())
            return;
        for (const auto& [key, value] : table->object) {
            if (key == "version" || !value.IsObject())
                continue;
            Raw raw;
            raw.key = key;
            const size_t colon = key.find(':');
            raw.name = key.substr(0, colon);
            raw.parent = colon == std::string::npos ? std::string() : key.substr(colon + 1);
            raw.extraDefines = extraDefines;
            raw.json = &value;
            raw.order = raws.size();
            rawIndex[raw.name] = raws.size();
            raws.push_back(std::move(raw));
        }
        docs.push_back(std::move(doc));
    }

    void Db::Load(Mode newMode, const std::string& root) {
        mode = newMode;
        docs.clear();
        raws.clear();
        rawIndex.clear();
        resolved.clear();
        resolving.clear();
        if (root.empty())
            return;
        const bool fancy = mode == Mode::Fancy;
        const std::set<std::string> fancyDefine = {"FANCY"};
        std::error_code ec;
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(fs::path(root) / "materials", ec))
            if (e.is_regular_file() && e.path().extension() == ".material")
                files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const fs::path& p : files) {
            const std::string stem = p.stem().string();
            if (stem == "gameface" || stem == "hologram" || (stem == "fancy" && !fancy) ||
                (stem == "sad" && fancy))
                continue;
            const bool defineFancy =
                fancy && (stem == "fancy" || stem == "entity" || stem == "terrain" || stem == "portal");
            AddText(file::Read(p), defineFancy ? fancyDefine : std::set<std::string>());
        }
    }

    void Db::AddPackDirectory(const std::string& dir) {
        std::error_code ec;
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(fs::path(dir) / "materials", ec))
            if (e.is_regular_file() && e.path().extension() == ".material")
                files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const fs::path& p : files)
            AddText(file::Read(p), {});
    }

    const Def* Db::ResolveOne(const std::string& name) {
        if (const auto it = resolved.find(name); it != resolved.end())
            return &it->second;
        const auto raw = rawIndex.find(name);
        if (raw == rawIndex.end()) {
            if (const auto b = builtins.find(str::Lower(name)); b != builtins.end())
                return &b->second;
            return nullptr;
        }
        if (resolving[name]++ > 0)
            return nullptr;
        const Raw& r = raws[raw->second];
        Def def;
        if (!r.parent.empty()) {
            const Def* parent = ResolveOne(r.parent);
            if (!parent)
                parent = ResolveOne(str::Lower(r.parent));
            if (parent) {
                def = *parent;
                def.variants.clear();
            } else {
                Log("material", "{} has unknown parent {}", r.name, r.parent);
            }
        }
        def.name = r.name;
        Apply(def, *r.json);
        def.defines.insert(r.extraDefines.begin(), r.extraDefines.end());
        std::map<std::string, const Json*> collected;
        std::vector<const Raw*> chain;
        for (const Raw* cur = &r; cur;) {
            chain.push_back(cur);
            if (cur->parent.empty() || chain.size() > 32)
                break;
            const auto next = rawIndex.find(cur->parent);
            cur = next == rawIndex.end() ? nullptr : &raws[next->second];
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const Json* variants = (*it)->json->Find("variants");
            if (!variants || !variants->IsArray())
                continue;
            for (const Json& entry : variants->array)
                if (entry.IsObject())
                    for (const auto& [vname, vjson] : entry.object)
                        collected[vname] = &vjson;
        }
        for (const auto& [vname, vjson] : collected) {
            Def v = def;
            v.variants.clear();
            v.name = r.name + "." + vname;
            Apply(v, *vjson);
            def.variants[vname] = std::move(v);
        }
        resolving[name] = 0;
        return &(resolved[name] = std::move(def));
    }

    void Db::Resolve() {
        resolved.clear();
        resolving.clear();
        FillBuiltins();
        for (const Raw& r : raws)
            ResolveOne(r.name);
    }

    namespace {
        struct Builtin {
            const char* name;
            const char* states;
            BlendFactor src;
            BlendFactor dst;
            DepthFunc depth;
            const char* defines;
        };

        std::set<std::string> Split(const char* s) {
            std::set<std::string> out;
            std::string cur;
            for (const char* p = s;; p++) {
                if (*p == ',' || *p == 0) {
                    if (!cur.empty())
                        out.insert(cur);
                    cur.clear();
                    if (*p == 0)
                        break;
                } else {
                    cur += *p;
                }
            }
            return out;
        }
    }

    void Db::FillBuiltins() {
        builtins.clear();
        for (const SourcePass& source : kSourcePasses) {
            Def d;
            d.name = std::string(source.family) + "." + source.pass;
            d.sourceFamily = source.family;
            d.sourcePass = source.pass;
            d.vertexShader = source.vertex;
            d.fragmentShader = source.fragment;
            const std::string pass = source.pass;
            if (pass.contains("AlphaTest"))
                d.defines.insert("ALPHA_TEST");
            if (pass.contains("Transparent")) {
                d.states.insert("Blending");
                d.states.insert("DisableDepthWrite");
                d.blendSet = true;
            }
            if (pass.contains("DepthOnly")) {
                d.states.insert("DisableColorWrite");
                d.states.insert("DisableAlphaWrite");
            }
            if (pass.contains("Emissive"))
                d.defines.insert("USE_EMISSIVE");
            if (pass.contains("EmissiveOnly"))
                d.defines.insert("USE_ONLY_EMISSIVE");
            if (pass.contains("ColorMask"))
                d.defines.insert("USE_COLOR_MASK");
            if (pass.contains("Glint"))
                d.defines.insert("GLINT");
            builtins.emplace(str::Lower(d.name), std::move(d));
        }
        const auto alias = [&](const char* name, const char* family, const char* pass) -> Def& {
            const std::string key = str::Lower(std::string(family) + "." + pass);
            const auto source = builtins.find(key);
            if (source == builtins.end())
                throw std::runtime_error("Missing upstream material pass: " + key);
            Def d = source->second;
            d.name = name;
            return builtins.insert_or_assign(name, std::move(d)).first->second;
        };
        using B = BlendFactor;
        const bool fancy = mode == Mode::Fancy;
        const Builtin table[] = {
            {"clouds", fancy ? "Blending,DisableAlphaWrite" : "DisableAlphaWrite", B::SrcAlpha,
             B::InvSrcAlpha, DepthFunc::LessEqual, ""},
            {"sun_moon", "DisableDepthWrite,DisableAlphaWrite,Blending", B::SrcAlpha, B::One,
             DepthFunc::LessEqual, ""},
            {"stars", "DisableDepthWrite,DisableAlphaWrite,Blending", B::InvDstColor, B::One,
             DepthFunc::LessEqual, ""},
            {"ui_crosshair", "Blending", B::InvDstColor, B::InvSrcColor, DepthFunc::LessEqual, ""},
            {"particles_opaque", "DisableAlphaWrite", B::SrcAlpha, B::InvSrcAlpha, DepthFunc::LessEqual,
             "ENABLE_FOG"},
            {"particles_alpha", "DisableAlphaWrite", B::SrcAlpha, B::InvSrcAlpha, DepthFunc::LessEqual,
             "ALPHA_TEST,ENABLE_FOG"},
            {"particles_blend", "Blending,DisableCulling,DisableDepthWrite", B::SrcAlpha, B::InvSrcAlpha,
             DepthFunc::LessEqual, "ENABLE_FOG"},
            {"particles_add", "Blending,DisableCulling,DisableDepthWrite", B::SrcAlpha, B::One,
             DepthFunc::LessEqual, "ENABLE_FOG"},
            {"cracks_overlay", "Blending,DisableDepthWrite,DisableAlphaWrite", B::DstColor, B::Zero,
             DepthFunc::LessEqual, ""},
            {"name_tag", "Blending,DisableDepthWrite", B::SrcAlpha, B::InvSrcAlpha, DepthFunc::Always, ""},
            {"name_tag_depth_tested", "Blending,DisableDepthWrite", B::SrcAlpha, B::InvSrcAlpha,
             DepthFunc::LessEqual, ""},
            {"name_tag_text", "Blending", B::SrcAlpha, B::InvSrcAlpha, DepthFunc::Always, ""},
            {"name_text_depth_tested", "Blending", B::SrcAlpha, B::InvSrcAlpha, DepthFunc::LessEqual, ""},
            {"precipitation", "DisableCulling,DisableDepthWrite,Blending", B::SrcAlpha, B::InvSrcAlpha,
             DepthFunc::LessEqual, "COMFORT_MODE"},
            {"lightning", "DisableCulling,Blending", B::SrcAlpha, B::One, DepthFunc::LessEqual, ""},
            {"entity", "", B::SrcAlpha, B::InvSrcAlpha, DepthFunc::LessEqual, "USE_OVERLAY"},
            {"entity_alphatest", "DisableCulling", B::SrcAlpha, B::InvSrcAlpha, DepthFunc::LessEqual,
             "USE_OVERLAY,ALPHA_TEST"},
            {"entity_alphablend", "Blending", B::SrcAlpha, B::InvSrcAlpha, DepthFunc::LessEqual,
             "USE_OVERLAY"},
            {"slime_outer", "Blending", B::SrcAlpha, B::InvSrcAlpha, DepthFunc::LessEqual, "USE_OVERLAY"},
            {"entity_multitexture", "", B::SrcAlpha, B::InvSrcAlpha, DepthFunc::LessEqual,
             "USE_OVERLAY,MULTI_COLOR_TINT"},
            {"entity_emissive_alpha_one_sided", "Blending", B::SrcAlpha, B::InvSrcAlpha, DepthFunc::LessEqual,
             "USE_OVERLAY,USE_EMISSIVE,ALPHA_TEST"},
            {"entity_emissive", "", B::SrcAlpha, B::InvSrcAlpha, DepthFunc::LessEqual,
             "USE_OVERLAY,USE_EMISSIVE"},
            {"entity_emissive_alpha", "DisableCulling,Blending", B::SrcAlpha, B::InvSrcAlpha,
             DepthFunc::LessEqual, "USE_OVERLAY,USE_EMISSIVE,ALPHA_TEST"},
        };
        for (const Builtin& b : table) {
            const std::string name = b.name;
            const char* family = name == "clouds"                 ? "Clouds"
                                 : name == "sun_moon"             ? "SunMoon"
                                 : name == "stars"                ? "Stars"
                                 : name == "ui_crosshair"         ? "UISprite"
                                 : name.starts_with("particles_") ? "Particle"
                                 : name == "cracks_overlay"       ? "Cracks"
                                 : name.starts_with("name_")      ? "NameTag"
                                 : name == "precipitation"        ? "Weather"
                                                                  : "Actor";
            const char* pass =
                name == "ui_crosshair" ? "Transparent"
                : name == "entity" || name == "entity_multitexture" || name == "entity_emissive" ? "Opaque"
                : name == "particles_alpha" || name == "particles_opaque" || name == "entity_alphatest"
                    ? "AlphaTest"
                    : "Transparent";
            Def& d = alias(b.name, family, pass);
            d.states = Split(b.states);
            d.defines = Split(b.defines);
            d.blendSrc = b.src;
            d.blendDst = b.dst;
            d.blendSet = true;
            d.depthFunc = b.depth;
        }
        struct Alias {
            const char* name;
            const char* family;
            const char* pass;
        };
        const Alias aliases[] = {
            {"terrain_base", "RenderChunk", "Opaque"},
            {"terrain_opaque", "RenderChunk", "Opaque"},
            {"terrain_opaque_seasons", "RenderChunk", "Opaque"},
            {"terrain_blend", "RenderChunk", "Transparent"},
            {"terrain_blend_far", "RenderChunk", "Transparent"},
            {"terrain_blend_no_culling", "RenderChunk", "Transparent"},
            {"terrain_doubleside", "RenderChunk", "Opaque"},
            {"terrain_alpha", "RenderChunk", "AlphaTest"},
            {"terrain_alpha_single_side", "RenderChunk", "AlphaTest"},
            {"terrain_alpha_seasons", "RenderChunk", "AlphaTest"},
            {"entity_static", "Actor", "Opaque"},
            {"entity_glint", "Entity", "AlphaTestGlint"},
            {"entity_emissive_layer", "Entity", "Emissive"},
            {"entity_emissive_layer_alpha_test", "Entity", "AlphaTestEmissiveOnly"},
            {"spider", "Entity", "AlphaTestEmissiveOnly"},
            {"enderman", "Entity", "AlphaTestEmissiveOnly"},
            {"sheep", "Entity", "AlphaTestColorMask"},
            {"arrow", "Actor", "AlphaTest"},
            {"skyplane", "Sky", "Opaque"},
            {"end_sky", "EndSky", "Opaque"},
            {"portal", "EndPortal", "Opaque"},
        };
        for (const Alias& a : aliases) {
            Def& d = alias(a.name, a.family, a.pass);
            if (d.sourceFamily == "Actor" || d.sourceFamily == "Entity")
                d.defines.insert("USE_OVERLAY");
            if (d.sourceFamily == "RenderChunk") {
                d.states.insert("DisableAlphaWrite");
                if (fancy)
                    d.defines.insert("FANCY");
                if (d.Define("ALPHA_TEST"))
                    d.states.insert("DisableCulling");
                if (d.name.contains("seasons"))
                    d.defines.insert("SEASONS");
            }
            if (d.name == "terrain_blend_no_culling" || d.name == "arrow")
                d.states.insert("DisableCulling");
            if (d.name == "skyplane" || d.name == "end_sky") {
                d.states.insert("DisableDepthWrite");
                d.states.insert("DisableAlphaWrite");
            }
        }
        const std::pair<const char*, const char*> entityAliases[] = {
            {"agent", "entity"},
            {"allay", "entity_alphatest"},
            {"alpha_block", "entity_static"},
            {"alpha_block_color", "entity_static"},
            {"armadillo", "entity_alphatest"},
            {"armor", "entity_alphatest"},
            {"armor_enchanted", "entity_alphatest"},
            {"armor_leather", "entity"},
            {"armor_leather_enchanted", "entity"},
            {"armor_stand", "entity_alphatest"},
            {"axolotl", "entity_alphatest"},
            {"axolotl_limbs", "entity"},
            {"banner", "entity_alphatest"},
            {"banner_pole", "entity_alphatest"},
            {"bat", "entity_alphatest"},
            {"bat_v2", "entity_alphatest"},
            {"beacon_beam", "entity"},
            {"beacon_beam_transparent", "entity"},
            {"bed", "entity_alphatest"},
            {"bee", "entity_alphatest"},
            {"bell", "entity"},
            {"blaze_body", "entity_emissive"},
            {"blaze_head", "entity_emissive_alpha"},
            {"boat", "entity"},
            {"bogged", "entity_alphatest"},
            {"bogged_clothes", "entity_alphatest"},
            {"breeze", "entity_alphatest"},
            {"breeze_eyes", "entity_emissive_layer_alpha_test"},
            {"breeze_wind", "entity_static"},
            {"breeze_wind_charge", "entity_static"},
            {"camel", "entity_alphatest"},
            {"camera", "entity"},
            {"cat", "entity"},
            {"chalkboard", "entity"},
            {"charged_creeper", "entity_static"},
            {"chest", "entity"},
            {"chest_boat", "entity"},
            {"chicken", "entity"},
            {"chicken_cold", "entity_alphatest"},
            {"chicken_legs", "entity_alphatest"},
            {"clownfish", "entity_alphatest"},
            {"cod", "entity_alphatest"},
            {"conduit", "entity_alphatest"},
            {"conduit_wind", "entity_alphatest"},
            {"copper_golem", "entity"},
            {"copper_golem_eyes", "entity_emissive_layer_alpha_test"},
            {"copper_golem_flower", "entity_alphatest"},
            {"cow", "entity"},
            {"cow_cold", "entity_alphatest"},
            {"creaking", "entity_alphatest"},
            {"creaking_eyes", "entity_emissive_layer_alpha_test"},
            {"creeper", "entity"},
            {"cushion", "entity"},
            {"dolphin", "entity"},
            {"dragon_head", "entity_alphatest"},
            {"dragon_head_glint", "entity_glint"},
            {"drowned", "entity_emissive_alpha"},
            {"egg", "entity_alphatest"},
            {"elytra", "entity_alphatest"},
            {"elytra_glint", "entity_alphatest"},
            {"enchanting_table_book", "entity"},
            {"ender_crystal", "entity_alphatest"},
            {"ender_dragon", "entity_emissive_alpha_one_sided"},
            {"ender_pearl", "entity_alphatest"},
            {"enderman_invisible", "entity_emissive_alpha"},
            {"endermite", "entity_alphatest"},
            {"entity_alphablend_nocolor", "entity_alphablend"},
            {"entity_alphatest_change_color", "entity"},
            {"entity_alphatest_change_color_glint", "entity"},
            {"entity_alphatest_glint", "entity_alphatest"},
            {"entity_alphatest_glint_item", "entity_alphatest"},
            {"entity_alphatest_multicolor_tint", "entity"},
            {"entity_alphatest_one_sided", "entity"},
            {"entity_beam", "entity"},
            {"entity_beam_additive", "entity_alphablend"},
            {"entity_change_color", "entity"},
            {"entity_change_color_glint", "entity_glint"},
            {"entity_custom", "entity"},
            {"entity_dissolve_layer0", "entity"},
            {"entity_dissolve_layer1", "entity"},
            {"entity_emissive_layer_alpha_blend", "entity"},
            {"entity_flat_color_line", "entity_alphatest"},
            {"entity_lead_base", "entity"},
            {"entity_loyalty_rope", "entity_glint"},
            {"entity_multitexture_alpha_test", "entity_multitexture"},
            {"entity_multitexture_alpha_test_color_mask", "entity_multitexture"},
            {"entity_multitexture_color_mask", "entity_multitexture"},
            {"entity_multitexture_masked", "entity_alphatest"},
            {"entity_multitexture_multiplicative_blend", "entity"},
            {"entity_nocull", "entity"},
            {"evoker", "entity_alphatest"},
            {"experience_orb", "entity"},
            {"eye_of_ender_signal", "entity_alphatest"},
            {"fang", "entity_alphatest"},
            {"fireball", "entity_alphatest"},
            {"fireworks_rocket", "entity_alphatest"},
            {"fishing_hook", "entity_alphatest"},
            {"fox", "entity"},
            {"frog", "entity_alphatest"},
            {"ghast", "entity_emissive_alpha"},
            {"glow_squid", "entity_emissive"},
            {"goat", "entity_alphatest"},
            {"guardian", "entity_alphatest"},
            {"guardian_ghost", "entity_alphablend"},
            {"harness", "entity_alphatest"},
            {"hoglin", "entity_alphatest"},
            {"horse", "entity_alphatest"},
            {"horse_leather_armor", "entity_multitexture"},
            {"husk", "entity_alphatest"},
            {"husk_clothes", "entity_alphatest"},
            {"iron_golem", "entity_alphatest"},
            {"item_in_hand", "entity"},
            {"item_in_hand_entity_alphatest", "entity_alphatest"},
            {"item_in_hand_entity_alphatest_color", "entity_alphatest"},
            {"item_in_hand_entity_alphatest_one_sided_color", "entity"},
            {"item_in_hand_glint", "entity"},
            {"item_in_hand_multicolor_tint", "entity"},
            {"leash_knot", "entity"},
            {"lingering_potion_enchanted", "entity_alphatest"},
            {"llama", "entity_multitexture"},
            {"llama_spit", "entity"},
            {"magma_cube", "entity_emissive_alpha"},
            {"map", "entity_alphatest"},
            {"map_decoration", "entity_alphatest"},
            {"map_marker", "entity_alphatest"},
            {"minecart", "entity_alphatest"},
            {"mob_head", "entity_alphatest"},
            {"mob_head_glint", "entity_glint"},
            {"mooshroom", "entity"},
            {"mooshroom_mushrooms", "entity_alphatest"},
            {"moving_block", "entity_alphatest"},
            {"moving_block_alpha", "entity_alphatest"},
            {"moving_block_alpha_seasons", "entity_alphatest"},
            {"moving_block_alpha_single_side", "entity_alphatest"},
            {"moving_block_blend", "entity_alphatest"},
            {"moving_block_double_side", "entity_alphatest"},
            {"moving_block_seasons", "entity_alphatest"},
            {"nautilus", "entity_alphatest"},
            {"nautilus_armor", "entity_alphatest"},
            {"npc", "entity_alphatest"},
            {"ocelot", "entity_alphatest"},
            {"on_screen_effect", "entity_alphatest"},
            {"opaque_block", "entity_static"},
            {"opaque_block_color", "entity_static"},
            {"opaque_block_color_uv2", "entity_static"},
            {"panda", "entity"},
            {"parrot", "entity_alphatest"},
            {"pattern", "entity_alphatest"},
            {"pattern_glint", "entity_alphatest"},
            {"phantom", "entity_emissive_alpha"},
            {"phantom_invisible", "entity_emissive_alpha"},
            {"pig", "entity"},
            {"pig_v3", "entity_alphatest"},
            {"piglin", "entity_alphatest"},
            {"piglin_brute", "entity_alphatest"},
            {"pillager", "entity_alphatest"},
            {"piston_arm", "entity"},
            {"player", "entity"},
            {"player_alphatest", "entity_alphatest"},
            {"player_animated", "entity_alphatest"},
            {"player_spectator", "entity_alphablend"},
            {"polar_bear", "entity"},
            {"pufferfish", "entity_alphatest"},
            {"rabbit", "entity"},
            {"ravager", "entity_alphatest"},
            {"saddle", "entity_alphatest"},
            {"salmon", "entity_alphatest"},
            {"shield", "entity"},
            {"shield_glint", "entity_glint"},
            {"shulker", "entity_alphatest"},
            {"shulker_box", "entity_alphatest"},
            {"shulker_bullet", "entity_alphatest"},
            {"sign", "entity_static"},
            {"silverfish", "entity"},
            {"silverfish_layers", "entity_alphatest"},
            {"skeleton", "entity_alphatest"},
            {"slime", "entity_alphatest"},
            {"sniffer", "entity_alphatest"},
            {"snow_golem", "entity"},
            {"snow_golem_pumpkin", "entity"},
            {"snowball", "entity_alphatest"},
            {"spider_invisible", "entity_emissive_alpha"},
            {"splash_potion_enchanted", "entity_alphatest"},
            {"squid", "entity"},
            {"stray", "entity_alphatest"},
            {"stray_clothes", "entity_alphatest"},
            {"strider", "entity_alphatest"},
            {"sulfur_cube", "entity_alphatest"},
            {"sulfur_cube_outer", "entity"},
            {"tadpole", "entity_alphatest"},
            {"trial_spawner", "entity_alphatest"},
            {"trident", "entity"},
            {"trident_glint", "entity_glint"},
            {"trident_riptide", "entity_alphatest"},
            {"tropicalfish", "entity"},
            {"turtle", "entity"},
            {"vex", "entity_alphatest"},
            {"villager", "entity_alphatest"},
            {"villager_v2", "entity_alphatest"},
            {"villager_v2_masked", "entity_alphatest"},
            {"vindicator", "entity_alphatest"},
            {"wandering_trader", "entity_alphatest"},
            {"warden", "entity_alphatest"},
            {"warden_bioluminescent_layer", "entity"},
            {"witch", "entity_alphatest"},
            {"wither_boss", "entity_alphatest"},
            {"wither_boss_armor", "entity_static"},
            {"wither_skull", "entity_alphatest"},
            {"wolf", "entity"},
            {"wolf_armor", "entity"},
            {"xp_bottle", "entity_alphatest"},
            {"zoglin", "entity_alphatest"},
            {"zombie", "entity_alphatest"},
            {"zombie_villager", "entity_alphatest"},
            {"zombie_villager_v2", "entity_alphatest"},
            {"zombie_villager_v2_masked", "entity_alphatest"},
        };
        for (const auto& [name, target] : entityAliases) {
            Def d = builtins.at(target);
            d.name = name;
            builtins.emplace(name, std::move(d));
        }
    }

    void Db::InstallBuiltins() {
        if (builtins.empty())
            FillBuiltins();
        for (const auto& [k, v] : builtins)
            resolved.emplace(k, v);
    }

    const Def* Db::Find(const std::string& name) const {
        if (const auto it = resolved.find(name); it != resolved.end())
            return &it->second;
        if (const auto it = resolved.find(str::Lower(name)); it != resolved.end())
            return &it->second;
        return nullptr;
    }

    const Def* Db::Variant(const std::string& name, const std::string& variant) const {
        const Def* d = Find(name);
        if (!d)
            return nullptr;
        if (const auto it = d->variants.find(variant); it != d->variants.end())
            return &it->second;
        return d;
    }

    std::vector<std::string> Db::Names() const {
        std::vector<std::string> out;
        out.reserve(resolved.size());
        for (const auto& [k, v] : resolved)
            out.push_back(k);
        std::sort(out.begin(), out.end());
        return out;
    }

    Registry& Registry::Instance() {
        static Registry registry;
        return registry;
    }

    void Registry::SetRoot(const std::string& newRoot) {
        if (newRoot == root)
            return;
        root = newRoot;
        loaded = false;
    }

    void Registry::Configure(bool wantFancy) {
        if (loaded && fancy == wantFancy)
            return;
        fancy = wantFancy;
        loaded = false;
    }

    const Db& Registry::Get() {
        if (!loaded) {
            db.Load(fancy ? Mode::Fancy : Mode::Sad, root);
            db.Resolve();
            db.InstallBuiltins();
            loaded = true;
            generation++;
        }
        return db;
    }
}
