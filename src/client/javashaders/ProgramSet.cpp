#include "client/javashaders/ProgramSet.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace perlin::jshader {
    namespace {
        const std::vector<ProgramInfo> kPrograms = {
            {"shadow", ""},
            {"shadow_solid", "shadow"},
            {"shadow_cutout", "shadow"},
            {"shadow_water", "shadow"},
            {"shadow_entities", "shadow"},
            {"shadow_lightning", "shadow"},
            {"shadow_block", "shadow"},
            {"gbuffers_basic", ""},
            {"gbuffers_line", "gbuffers_basic"},
            {"gbuffers_textured", "gbuffers_basic"},
            {"gbuffers_textured_lit", "gbuffers_textured"},
            {"gbuffers_skybasic", "gbuffers_basic"},
            {"gbuffers_skytextured", "gbuffers_textured"},
            {"gbuffers_clouds", "gbuffers_textured"},
            {"gbuffers_terrain", "gbuffers_textured_lit"},
            {"gbuffers_terrain_solid", "gbuffers_terrain"},
            {"gbuffers_terrain_cutout", "gbuffers_terrain"},
            {"gbuffers_damagedblock", "gbuffers_terrain"},
            {"gbuffers_water", "gbuffers_terrain"},
            {"gbuffers_block", "gbuffers_terrain"},
            {"gbuffers_block_translucent", "gbuffers_block"},
            {"gbuffers_beaconbeam", "gbuffers_textured"},
            {"gbuffers_item", "gbuffers_textured_lit"},
            {"gbuffers_entities", "gbuffers_textured_lit"},
            {"gbuffers_entities_translucent", "gbuffers_entities"},
            {"gbuffers_entities_glowing", "gbuffers_entities"},
            {"gbuffers_armor_glint", "gbuffers_textured"},
            {"gbuffers_spidereyes", "gbuffers_textured"},
            {"gbuffers_hand", "gbuffers_textured_lit"},
            {"gbuffers_hand_water", "gbuffers_hand"},
            {"gbuffers_weather", "gbuffers_textured_lit"},
            {"gbuffers_particles", "gbuffers_textured_lit"},
            {"gbuffers_particles_translucent", "gbuffers_particles"},
            {"gbuffers_lightning", "gbuffers_textured"},
            {"dh_terrain", ""},
            {"dh_water", ""},
            {"dh_shadow", ""},
            {"final", ""},
        };

        struct NumParser {
            const std::string& s;
            size_t i = 0;
            bool ok = true;
            explicit NumParser(const std::string& s) : s(s) {}
            void Skip() {
                while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
                    i++;
            }
            double Expr() {
                double v = Term();
                for (;;) {
                    Skip();
                    if (i < s.size() && s[i] == '+') {
                        i++;
                        v += Term();
                    } else if (i < s.size() && s[i] == '-') {
                        i++;
                        v -= Term();
                    } else
                        break;
                }
                return v;
            }
            double Term() {
                double v = Unary();
                for (;;) {
                    Skip();
                    if (i < s.size() && s[i] == '*') {
                        i++;
                        v *= Unary();
                    } else if (i < s.size() && s[i] == '/') {
                        i++;
                        const double d = Unary();
                        v = d != 0 ? v / d : 0;
                    } else
                        break;
                }
                return v;
            }
            double Unary() {
                Skip();
                if (i < s.size() && s[i] == '-') {
                    i++;
                    return -Unary();
                }
                if (i < s.size() && s[i] == '+') {
                    i++;
                    return Unary();
                }
                if (i < s.size() && s[i] == '(') {
                    i++;
                    double v = Expr();
                    Skip();
                    if (i < s.size() && s[i] == ')')
                        i++;
                    return v;
                }
                char* end = nullptr;
                const double v = std::strtod(s.c_str() + i, &end);
                if (end == s.c_str() + i) {
                    ok = false;
                    return 0;
                }
                i = static_cast<size_t>(end - s.c_str());
                if (i < s.size() && (s[i] == 'f' || s[i] == 'F'))
                    i++;
                return v;
            }
        };

        std::string SubstituteMacros(std::string text, const MacroMap& macros) {
            for (int pass = 0; pass < 8; pass++) {
                std::string out;
                bool changed = false;
                for (size_t i = 0; i < text.size();) {
                    if (std::isalpha(static_cast<unsigned char>(text[i])) || text[i] == '_') {
                        size_t j = i;
                        while (j < text.size() &&
                               (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_'))
                            j++;
                        const std::string word = text.substr(i, j - i);
                        const auto it = macros.find(word);
                        if (it != macros.end() && !it->second.empty()) {
                            out += "(" + it->second + ")";
                            changed = true;
                        } else {
                            out += word;
                        }
                        i = j;
                    } else {
                        out += text[i++];
                    }
                }
                text = std::move(out);
                if (!changed)
                    break;
            }
            return text;
        }

        bool IsDirectiveName(const std::string& n) {
            static const char* const kNames[] = {"shadowMapResolution",
                                                 "shadowDistance",
                                                 "shadowDistanceRenderMul",
                                                 "shadowIntervalSize",
                                                 "sunPathRotation",
                                                 "noiseTextureResolution",
                                                 "wetnessHalflife",
                                                 "drynessHalflife",
                                                 "eyeBrightnessHalflife",
                                                 "centerDepthHalflife",
                                                 "ambientOcclusionLevel",
                                                 "generateShadowMipmap",
                                                 "generateShadowColorMipmap",
                                                 "shadowHardwareFiltering",
                                                 "shadowHardwareFiltering0",
                                                 "shadowHardwareFiltering1",
                                                 "shadowtex0Mipmap",
                                                 "shadowtex1Mipmap",
                                                 "shadowtex0Nearest",
                                                 "shadowtex1Nearest",
                                                 "shadowcolor0Mipmap",
                                                 "shadowcolor1Mipmap",
                                                 "shadowcolor0Nearest",
                                                 "shadowcolor1Nearest",
                                                 "shadowcolor0Format",
                                                 "shadowcolor1Format",
                                                 "shadowcolor0Clear",
                                                 "shadowcolor1Clear",
                                                 "shadowcolor0ClearColor",
                                                 "shadowcolor1ClearColor",
                                                 "superSamplingLevel",
                                                 "gaux1Format",
                                                 "gaux2Format",
                                                 "gaux3Format",
                                                 "gaux4Format",
                                                 "gcolorFormat",
                                                 "gdepthFormat",
                                                 "gnormalFormat",
                                                 "compositeFormat",
                                                 "gcolorMipmapEnabled",
                                                 "gdepthMipmapEnabled",
                                                 "gnormalMipmapEnabled",
                                                 "compositeMipmapEnabled",
                                                 "gaux1MipmapEnabled",
                                                 "gaux2MipmapEnabled",
                                                 "gaux3MipmapEnabled",
                                                 "gaux4MipmapEnabled",
                                                 "gcolorClear",
                                                 "gdepthClear",
                                                 "gnormalClear",
                                                 "compositeClear",
                                                 "gaux1Clear",
                                                 "gaux2Clear",
                                                 "gaux3Clear",
                                                 "gaux4Clear",
                                                 "gcolorClearColor",
                                                 "gdepthClearColor",
                                                 "gnormalClearColor",
                                                 "compositeClearColor",
                                                 "gaux1ClearColor",
                                                 "gaux2ClearColor",
                                                 "gaux3ClearColor",
                                                 "gaux4ClearColor",
                                                 "shadowtex0Format",
                                                 "voxelDistance"};
            for (const char* k : kNames)
                if (n == k)
                    return true;
            if (n.compare(0, 8, "colortex") == 0 || n.compare(0, 8, "depthtex") == 0)
                return true;
            return false;
        }
    }

    double Directives::Number(const std::string& name, double fallback) const {
        const auto it = constants.find(name);
        if (it == constants.end())
            return fallback;
        NumParser p(it->second);
        const double v = p.Expr();
        return p.ok ? v : fallback;
    }

    bool Directives::Bool(const std::string& name, bool fallback) const {
        const auto it = constants.find(name);
        if (it == constants.end())
            return fallback;
        std::string v = str::Trim(it->second);
        while (!v.empty() && v.front() == '(' && v.back() == ')')
            v = str::Trim(v.substr(1, v.size() - 2));
        if (v == "true" || v == "1")
            return true;
        if (v == "false" || v == "0")
            return false;
        return fallback;
    }

    std::string Directives::Text(const std::string& name, const std::string& fallback) const {
        const auto it = constants.find(name);
        if (it == constants.end())
            return fallback;
        std::string v = str::Trim(it->second);
        while (!v.empty() && v.front() == '(' && v.back() == ')')
            v = str::Trim(v.substr(1, v.size() - 2));
        return v;
    }

    const std::vector<ProgramInfo>& StandardPrograms() {
        return kPrograms;
    }

    std::string FallbackFor(const std::string& name) {
        for (const ProgramInfo& p : kPrograms)
            if (name == p.name)
                return p.fallback;
        return {};
    }

    ShaderPackInstance::ShaderPackInstance(std::shared_ptr<PackSource> source, OptionValues values,
                                           MacroMap engine)
        : pack(std::move(source)), overrides(std::move(values)), engineAll(std::move(engine)) {
        engineMacros = engineAll;
        packName = pack->Name();
        Reload(overrides);
    }

    void ShaderPackInstance::Reload(OptionValues values) {
        overrides = std::move(values);
        cache.clear();
        directivesLoaded = false;
        directives = {};
        firstError.clear();
        ScanOptions();
        LoadProperties();
        LoadIdTables();
        LoadLang();
    }

    void ShaderPackInstance::SetDimension(const std::string& folder) {
        if (folder == dimension)
            return;
        dimension = folder;
        cache.clear();
        directivesLoaded = false;
    }

    void ShaderPackInstance::ScanOptions() {
        options.clear();
        IncludeContext ctx;
        ctx.pack = pack.get();
        ctx.options = &options;
        for (const std::string& file : pack->List("")) {
            const size_t dot = file.find_last_of('.');
            if (dot == std::string::npos)
                continue;
            const std::string ext = file.substr(dot);
            if (ext != ".fsh" && ext != ".vsh" && ext != ".gsh" && ext != ".csh" && ext != ".glsl" &&
                ext != ".settings" && ext != ".h" && ext != ".inc" && ext != ".sh")
                continue;
            std::string text;
            if (pack->Read(file, text))
                ScanOptionsInText(ctx, file, text);
        }
        engineMacros = engineAll;
        macros = engineAll;
        for (const ShaderOption& o : options) {
            const auto ov = overrides.find(o.name);
            switch (o.kind) {
            case ShaderOption::Kind::Toggle: {
                bool on = o.defaultOn;
                if (ov != overrides.end())
                    on = ov->second == "true" || ov->second == "1" || ov->second == "on";
                if (on)
                    macros[o.name] = "";
                break;
            }
            default:
                macros[o.name] = ov != overrides.end() ? ov->second : o.defaultValue;
                break;
            }
        }
    }

    void ShaderPackInstance::LoadProperties() {
        std::string text;
        props = {};
        rawProps = {};
        activeFeatures.clear();
        missingFeatures.clear();
        if (pack->Read("shaders.properties", text)) {
            props = ParseProperties(text, macros);
            rawProps = ParseProperties(text, {}, false);
        }
        // iris expands macros in values too (size.buffer.colortex1 = REFLECTION_RES REFLECTION_RES)
        const auto expand = [&](std::string value) {
            for (int pass = 0; pass < 4; pass++) {
                std::string out;
                bool changed = false;
                for (size_t i = 0; i < value.size();) {
                    if (std::isalpha(static_cast<unsigned char>(value[i])) || value[i] == '_') {
                        size_t j = i;
                        while (j < value.size() &&
                               (std::isalnum(static_cast<unsigned char>(value[j])) || value[j] == '_'))
                            j++;
                        const std::string word = value.substr(i, j - i);
                        const auto it = macros.find(word);
                        // a word glued to a dot or slash is part of a name (colortex1.foo), not a macro use
                        const bool glued = (i > 0 && (value[i - 1] == '.' || value[i - 1] == '/')) ||
                                           (j < value.size() && (value[j] == '.' || value[j] == '/'));
                        if (it != macros.end() && !it->second.empty() && !glued) {
                            out += it->second;
                            changed = true;
                        } else {
                            out += word;
                        }
                        i = j;
                    } else {
                        out += value[i++];
                    }
                }
                value = std::move(out);
                if (!changed)
                    break;
            }
            return value;
        };
        for (auto& [key, value] : props.entries) {
            if (key.compare(0, 6, "screen") == 0 || key.compare(0, 8, "profile.") == 0 || key == "sliders" ||
                key.compare(0, 5, "iris.") == 0)
                continue;
            value = expand(value);
            props.map[key] = value;
        }
        // feature flags: shaders.properties saw every flag the renderer has; the shaders only see the optional
        // ones the pack lists (iris ShaderPack)
        const auto upper = [](std::string s) {
            for (char& c : s)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return s;
        };
        std::set<std::string> optional;
        for (const std::string& f : SplitWords(rawProps.Get("iris.features.optional")))
            optional.insert(upper(f));
        for (const std::string& f : SplitWords(rawProps.Get("iris.features.required"))) {
            const std::string u = upper(f);
            if (!engineAll.contains("IRIS_FEATURE_" + u))
                missingFeatures.push_back(u);
        }
        for (auto it = engineMacros.begin(); it != engineMacros.end();) {
            if (it->first.compare(0, 13, "IRIS_FEATURE_") == 0) {
                const std::string flag = it->first.substr(13);
                if (optional.contains(flag)) {
                    activeFeatures.insert(flag);
                    ++it;
                    continue;
                }
                macros.erase(it->first);
                it = engineMacros.erase(it);
                continue;
            }
            ++it;
        }
        if (props.Bool("supportsColorCorrection", false)) {
            const char* spaces[] = {"SRGB", "DCI_P3", "DISPLAY_P3", "REC2020", "ADOBE_RGB"};
            for (int i = 0; i < 5; i++) {
                engineMacros[std::string("COLOR_SPACE_") + spaces[i]] = std::to_string(i);
                macros[std::string("COLOR_SPACE_") + spaces[i]] = std::to_string(i);
            }
        }
        if (!missingFeatures.empty() && firstError.empty()) {
            firstError = "the pack needs features this renderer lacks:";
            for (const std::string& f : missingFeatures)
                firstError += " " + f;
        }
    }

    void ShaderPackInstance::LoadIdTables() {
        blockIds.clear();
        entityIds.clear();
        itemIds.clear();
        auto load = [&](const char* file, const char* prefix, std::map<int, std::vector<std::string>>& out) {
            std::string text;
            if (!pack->Read(file, text))
                return;
            const Properties p = ParseProperties(text, macros);
            for (const auto& e : p.entries) {
                if (e.first.compare(0, std::char_traits<char>::length(prefix), prefix) != 0)
                    continue;
                const std::string idText = e.first.substr(std::char_traits<char>::length(prefix));
                char* end = nullptr;
                const long id = std::strtol(idText.c_str(), &end, 10);
                if (end == idText.c_str())
                    continue;
                auto& list = out[static_cast<int>(id)];
                for (const std::string& w : SplitWords(e.second))
                    list.push_back(w);
            }
        };
        load("block.properties", "block.", blockIds);
        load("entity.properties", "entity.", entityIds);
        load("item.properties", "item.", itemIds);
    }

    void ShaderPackInstance::LoadLang() {
        lang.clear();
        for (const std::string& file : pack->List("lang")) {
            if (file.size() < 6 || file.compare(file.size() - 5, 5, ".lang") != 0)
                continue;
            std::string text;
            if (!pack->Read(file, text))
                continue;
            std::string locale = file.substr(file.find_last_of('/') + 1);
            locale.resize(locale.size() - 5);
            const Properties p = ParseProperties(text, {}, false);
            lang[locale] = p.map;
        }
    }

    std::string ShaderPackInstance::Lang(const std::string& key, const std::string& locale) const {
        for (const std::string& l : {locale, std::string("en_US"), std::string("en_us")}) {
            const auto it = lang.find(l);
            if (it == lang.end())
                continue;
            const auto v = it->second.find(key);
            if (v != it->second.end())
                return v->second;
        }
        return {};
    }

    std::string ShaderPackInstance::FindFile(const std::string& base, const char* ext) const {
        if (!dimension.empty()) {
            const std::string inDim = dimension + "/" + base + ext;
            if (pack->Exists(inDim))
                return inDim;
        }
        const std::string root = base + ext;
        return pack->Exists(root) ? root : std::string();
    }

    bool ShaderPackInstance::ProgramExists(const std::string& name) const {
        return !FindFile(name, ".fsh").empty() || !FindFile(name, ".vsh").empty() ||
               !FindFile(name, ".csh").empty();
    }

    bool ShaderPackInstance::ProgramEnabled(const std::string& name) const {
        // iris: program.<dimension folder>/<name>.enabled, or program.<name>.enabled for every dimension
        auto it = dimension.empty() ? props.map.end()
                                    : props.map.find("program." + dimension + "/" + name + ".enabled");
        if (it == props.map.end())
            it = props.map.find("program." + name + ".enabled");
        if (it == props.map.end())
            return true;
        const std::string& v = it->second;
        if (v == "true")
            return true;
        if (v == "false")
            return false;
        return EvalCondition(v, macros) != 0;
    }

    std::vector<std::string> ShaderPackInstance::PassChain(const std::string& base) const {
        std::vector<std::string> out;
        for (int i = 0; i < 100; i++) {
            const std::string name = i == 0 ? base : base + std::to_string(i);
            if (ProgramExists(name))
                out.push_back(name);
        }
        return out;
    }

    std::shared_ptr<ProgramSource> ShaderPackInstance::Program(const std::string& name) {
        const auto it = cache.find(name);
        if (it != cache.end())
            return it->second;
        auto program = Load(name);
        cache[name] = program;
        return program;
    }

    static void ParseDrawBuffers(const std::string& text, MacroMap macros, ProgramSource& out) {
        if (!text.contains("DRAWBUFFERS") && !text.contains("RENDERTARGETS"))
            return;
        const std::string active = ResolveConditionals(text, macros);
        size_t pos = 0;
        std::vector<int> result;
        bool found = false;
        while (pos < active.size()) {
            const size_t d = active.find("DRAWBUFFERS", pos);
            const size_t r = active.find("RENDERTARGETS", pos);
            const size_t at = std::min(d, r);
            if (at == std::string::npos)
                break;
            const bool isDraw = at == d;
            size_t i = at + (isDraw ? 11 : 13);
            while (i < active.size() && (active[i] == ' ' || active[i] == '\t'))
                i++;
            if (i >= active.size() || active[i] != ':') {
                pos = i;
                continue;
            }
            i++;
            std::vector<int> list;
            if (isDraw) {
                while (i < active.size() && active[i] == ' ')
                    i++;
                while (i < active.size() && active[i] >= '0' && active[i] <= '9')
                    list.push_back(active[i++] - '0');
            } else {
                while (i < active.size() && active[i] != '*' && active[i] != '\n') {
                    if (active[i] >= '0' && active[i] <= '9') {
                        int v = 0;
                        while (i < active.size() && active[i] >= '0' && active[i] <= '9')
                            v = v * 10 + (active[i++] - '0');
                        list.push_back(v);
                    } else {
                        i++;
                    }
                }
            }
            result = list;
            found = true;
            pos = i;
        }
        if (found) {
            out.drawBuffers = result;
            out.hasDrawBuffersTag = true;
        }
    }

    std::shared_ptr<ProgramSource> ShaderPackInstance::Load(const std::string& name) {
        auto program = std::make_shared<ProgramSource>();
        program->name = name;
        std::string use = name;
        for (int guard = 0; guard < 12; guard++) {
            if (ProgramExists(use))
                break;
            const std::string fb = FallbackFor(use);
            if (fb.empty())
                return program;
            use = fb;
        }
        if (!ProgramExists(use))
            return program;
        program->sourceName = use;
        program->found = true;

        IncludeContext ctx;
        ctx.pack = pack.get();
        ctx.overrides = &overrides;
        std::vector<ShaderOption> scratch;
        ctx.options = &scratch;
        auto expand = [&](const char* ext, ExpandedSource& out) {
            const std::string file = FindFile(use, ext);
            if (file.empty())
                return;
            out = ExpandIncludes(ctx, file);
            if (!out.ok && firstError.empty())
                firstError = out.error;
        };
        expand(".vsh", program->vertex);
        expand(".gsh", program->geometry);
        expand(".fsh", program->fragment);
        ParseDrawBuffers(program->fragment.text, macros, *program);
        const std::string csh = FindFile(use, ".csh");
        if (!csh.empty()) {
            program->compute.emplace_back();
            program->compute.back() = ExpandIncludes(ctx, csh);
        }
        for (char letter = 'a'; letter <= 'z'; letter++) {
            const std::string file = FindFile(use + "_" + std::string(1, letter), ".csh");
            if (file.empty())
                continue; // iris takes every letter that exists, gaps included
            program->compute.push_back(ExpandIncludes(ctx, file));
        }
        return program;
    }

    const Directives& ShaderPackInstance::GetDirectives() {
        if (directivesLoaded)
            return directives;
        directivesLoaded = true;
        directives = {};
        std::vector<std::string> names;
        names.reserve(kPrograms.size());
        for (const ProgramInfo& p : kPrograms)
            names.push_back(p.name);
        for (const char* base : {"setup", "begin", "prepare", "deferred", "composite", "shadowcomp"})
            for (const std::string& n : PassChain(base))
                names.push_back(n);
        names.push_back("final");
        for (const std::string& n : names) {
            if (!ProgramExists(n))
                continue;
            auto src = Program(n);
            std::vector<const ExpandedSource*> parts{&src->fragment, &src->vertex};
            for (const auto& c : src->compute)
                parts.push_back(&c);
            for (const ExpandedSource* part : parts) {
                if (part->text.empty())
                    continue;
                MacroMap local = macros;
                const std::string active = ResolveConditionals(part->text, local);
                std::istringstream in(active);
                std::string line;
                while (std::getline(in, line)) {
                    const std::string t = str::Trim(line);
                    if (t.compare(0, 6, "const ") != 0)
                        continue;
                    const size_t eq = t.find('=');
                    const size_t semi = t.find(';', eq == std::string::npos ? 0 : eq);
                    if (eq == std::string::npos || semi == std::string::npos)
                        continue;
                    const std::string left = str::Trim(t.substr(6, eq - 6));
                    const size_t sp = left.find_last_of(" \t");
                    if (sp == std::string::npos)
                        continue;
                    const std::string var = str::Trim(left.substr(sp + 1));
                    if (!IsDirectiveName(var))
                        continue;
                    directives.constants[var] =
                        SubstituteMacros(str::Trim(t.substr(eq + 1, semi - eq - 1)), local);
                }
            }
        }
        return directives;
    }
}
