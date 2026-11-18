#include <cstdio>
#include <cstdlib>
#include "client/systems/javashaders/Transform.hpp"
#include "client/systems/javashaders/GlslTokens.hpp"
#include "client/systems/javashaders/SamplerRename.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace perlin::jshader {
    namespace {
        using glsl::Tok;
        using glsl::IsIdChar;
        using glsl::IsIdStart;
        using glsl::IsOpaqueType;
        using glsl::IsQualifier;
        using glsl::IsType;
        using glsl::Join;
        using glsl::Lex;

        int TypeSlots(const std::string& type) {
            if (type == "mat2" || type == "mat2x2" || type == "mat2x3" || type == "mat2x4")
                return 2;
            if (type == "mat3" || type == "mat3x2" || type == "mat3x3" || type == "mat3x4")
                return 3;
            if (type == "mat4" || type == "mat4x2" || type == "mat4x3" || type == "mat4x4")
                return 4;
            if (type == "dvec3" || type == "dvec4")
                return 2;
            return 1;
        }

        int StructSlots(const std::string& source, const std::string& name, int depth = 0) {
            if (depth > 4)
                return 0;
            static const std::regex kBlock(R"(\bstruct\s+(\w+)\s*\{([^}]*)\})");
            for (auto it = std::sregex_iterator(source.begin(), source.end(), kBlock);
                 it != std::sregex_iterator(); ++it) {
                if ((*it)[1].str() != name)
                    continue;
                int total = 0;
                std::stringstream members((*it)[2].str());
                std::string decl;
                while (std::getline(members, decl, ';')) {
                    std::istringstream words(decl);
                    std::string type, word;
                    std::vector<std::string> parts;
                    while (words >> word)
                        parts.push_back(word);
                    while (!parts.empty() && (parts.front() == "flat" || parts.front() == "highp" ||
                                              parts.front() == "mediump" || parts.front() == "lowp" ||
                                              parts.front() == "smooth" || parts.front() == "const"))
                        parts.erase(parts.begin());
                    if (parts.size() < 2)
                        continue;
                    type = parts[0];
                    std::string rest;
                    for (size_t i = 1; i < parts.size(); i++)
                        rest += parts[i] + " ";
                    int per = TypeSlots(type);
                    if (per == 1)
                        if (const int nested = StructSlots(source, type, depth + 1); nested > 0)
                            per = nested;
                    std::stringstream names(rest);
                    std::string one;
                    while (std::getline(names, one, ',')) {
                        int count = 1;
                        if (const size_t b = one.find('['); b != std::string::npos)
                            count = std::max(1, std::atoi(one.c_str() + b + 1));
                        total += per * count;
                    }
                }
                return total;
            }
            return 0;
        }

        struct VaryingPiece {
            std::string suffix, type, access;
        };

        std::string StripGlslComments(std::string text) {
            for (size_t at = text.find("/*"); at != std::string::npos; at = text.find("/*", at)) {
                const size_t end = text.find("*/", at + 2);
                text.erase(at, end == std::string::npos ? std::string::npos : end + 2 - at);
            }
            for (size_t at = text.find("//"); at != std::string::npos; at = text.find("//", at)) {
                const size_t end = text.find('\n', at);
                text.erase(at, end == std::string::npos ? std::string::npos : end - at);
            }
            return text;
        }

        bool IsPlainVaryingType(const std::string& t) {
            static const std::set<std::string> ok = {"float", "vec2", "vec3", "vec4", "int",
                                                     "ivec2", "ivec3", "ivec4", "uint", "uvec2",
                                                     "uvec3", "uvec4", "bool"};
            return ok.contains(t);
        }

        std::vector<VaryingPiece> FlattenVarying(const std::string& source, const std::string& type) {
            std::vector<VaryingPiece> out;
            if (type.starts_with("mat")) {
                int cols = 0, rows = 0;
                if (type.size() == 4) {
                    cols = rows = type[3] - '0';
                } else if (type.size() == 6 && type[4] == 'x') {
                    cols = type[3] - '0';
                    rows = type[5] - '0';
                }
                if (cols < 2 || cols > 4 || rows < 2 || rows > 4)
                    return {};
                for (int c = 0; c < cols; c++)
                    out.push_back({"c" + std::to_string(c), "vec" + std::to_string(rows),
                                   "[" + std::to_string(c) + "]"});
                return out;
            }
            static const std::regex kBlock(R"(\bstruct\s+(\w+)\s*\{([^}]*)\})");
            for (auto it = std::sregex_iterator(source.begin(), source.end(), kBlock);
                 it != std::sregex_iterator(); ++it) {
                if ((*it)[1].str() != type)
                    continue;
                std::string body = StripGlslComments((*it)[2].str());
                std::stringstream members(body);
                std::string decl;
                while (std::getline(members, decl, ';')) {
                    std::istringstream words(decl);
                    std::vector<std::string> parts;
                    for (std::string w; words >> w;)
                        parts.push_back(w);
                    while (!parts.empty() && (parts.front() == "flat" || parts.front() == "highp" ||
                                              parts.front() == "mediump" || parts.front() == "lowp" ||
                                              parts.front() == "smooth" || parts.front() == "const"))
                        parts.erase(parts.begin());
                    if (parts.empty())
                        continue;
                    if (parts.size() < 2 || !IsPlainVaryingType(parts[0]))
                        return {};
                    std::string rest;
                    for (size_t i = 1; i < parts.size(); i++)
                        rest += parts[i];
                    std::stringstream names(rest);
                    for (std::string one; std::getline(names, one, ',');) {
                        if (one.empty() || one.contains('['))
                            return {};
                        out.push_back({one, parts[0], "." + one});
                    }
                }
                return out;
            }
            return {};
        }

        struct BuiltinUse {
            bool modelView = false, modelViewInv = false, proj = false, projInv = false, normalMat = false;
            bool textureMat = false, fogColor = false, fogDensity = false, fogStart = false, fogEnd = false,
                 fogScale = false, colorMod = false;
            bool fogCoord = false, fragCoord = false, shadow2D = false, shadow2DLod = false,
                 shadow2DProj = false;
            bool vertexIndex = false, frontColorIn = false;
            int fragData = 0;
            bool fragColor = false;
        };

        const std::map<std::string, std::string>& TextureFunctionMap() {
            static const std::map<std::string, std::string> m = {
                {"texture1D", "texture"},
                {"texture2D", "texture"},
                {"texture3D", "texture"},
                {"textureCube", "texture"},
                {"texture1DLod", "textureLod"},
                {"texture2DLod", "textureLod"},
                {"texture3DLod", "textureLod"},
                {"textureCubeLod", "textureLod"},
                {"texture1DProj", "textureProj"},
                {"texture2DProj", "textureProj"},
                {"texture3DProj", "textureProj"},
                {"texture2DProjLod", "textureProjLod"},
                {"texture2DGrad", "textureGrad"},
                {"texture3DGrad", "textureGrad"},
                {"texelFetch2D", "texelFetch"},
                {"texelFetch3D", "texelFetch"},
                {"texelFetch1D", "texelFetch"},
                {"textureSize2D", "textureSize"},
                {"textureSize3D", "textureSize"},

                {"texture2DGradARB", "textureGrad"},
                {"texture3DGradARB", "textureGrad"},
                {"textureCubeGradARB", "textureGrad"},
                {"texture2DLodARB", "textureLod"},
                {"texture3DLodARB", "textureLod"},
                {"textureCubeLodARB", "textureLod"},
                {"texture2DProjLodARB", "textureProjLod"},
                {"texture2DProjGradARB", "textureProjGrad"},
                {"texture1DGradARB", "textureGrad"},
                {"texture1DLodARB", "textureLod"},
                {"texture2DGradEXT", "textureGrad"},
                {"texture2DLodEXT", "textureLod"},
                {"textureCubeLodEXT", "textureLod"},
                {"textureCubeGradEXT", "textureGrad"},
                {"texture2DProjGrad", "textureProjGrad"},
                {"texture1DGrad", "textureGrad"},
                {"textureCubeGrad", "textureGrad"},
                {"texture1DProjLod", "textureProjLod"},
                {"texture3DProjLod", "textureProjLod"},
                {"textureSize1D", "textureSize"},
                {"textureSizeCube", "textureSize"},
                {"texture2DArray", "texture"},
                {"texture2DArrayLod", "textureLod"}};
            return m;
        }

        const std::unordered_set<std::string>& NewKeywords() {
            static const std::unordered_set<std::string> s = {
                "sample",   "input",      "output",    "filter",    "common",   "partition", "active",
                "buffer",   "patch",      "shared",    "coherent",  "restrict", "readonly",  "writeonly",
                "resource", "subroutine", "interface", "inline",    "noinline", "public",    "static",
                "extern",   "external",   "packed",    "template",  "this",     "union",     "enum",
                "typedef",  "class",      "using",     "namespace", "cast",     "goto",      "long",
                "short",    "half",       "fixed",     "unsigned",  "sizeof",   "superp",    "row_major",
                "smooth"};
            return s;
        }

        const std::unordered_set<std::string>& Builtin450Functions() {
            static const std::unordered_set<std::string> s = {"round",
                                                              "roundEven",
                                                              "trunc",
                                                              "sinh",
                                                              "cosh",
                                                              "tanh",
                                                              "asinh",
                                                              "acosh",
                                                              "atanh",
                                                              "isnan",
                                                              "isinf",
                                                              "transpose",
                                                              "inverse",
                                                              "determinant",
                                                              "outerProduct",
                                                              "modf",
                                                              "frexp",
                                                              "ldexp",
                                                              "fma",
                                                              "findLSB",
                                                              "findMSB",
                                                              "bitCount",
                                                              "mix",
                                                              "smoothstep",
                                                              "step",
                                                              "clamp",
                                                              "lessThan",
                                                              "greaterThan",
                                                              "equal",
                                                              "notEqual",
                                                              "any",
                                                              "all",
                                                              "not",
                                                              "dFdx",
                                                              "dFdy",
                                                              "fwidth",
                                                              "noise1",
                                                              "noise2",
                                                              "noise3",
                                                              "noise4",
                                                              "reflect",
                                                              "refract",
                                                              "faceforward",
                                                              "distance",
                                                              "length",
                                                              "normalize",
                                                              "cross",
                                                              "dot",
                                                              "pow",
                                                              "exp",
                                                              "log",
                                                              "exp2",
                                                              "log2",
                                                              "sqrt",
                                                              "inversesqrt",
                                                              "abs",
                                                              "sign",
                                                              "floor",
                                                              "ceil",
                                                              "fract",
                                                              "mod",
                                                              "min",
                                                              "max",
                                                              "radians",
                                                              "degrees",
                                                              "sin",
                                                              "cos",
                                                              "tan",
                                                              "asin",
                                                              "acos",
                                                              "atan",
                                                              "texture",
                                                              "textureLod",
                                                              "textureProj",
                                                              "textureGrad",
                                                              "texelFetch",
                                                              "textureSize",
                                                              "packUnorm4x8",
                                                              "unpackUnorm4x8",
                                                              "packHalf2x16",
                                                              "unpackHalf2x16",
                                                              "uaddCarry",
                                                              "usubBorrow",
                                                              "bitfieldExtract",
                                                              "bitfieldInsert",
                                                              "bitfieldReverse",
                                                              "floatBitsToInt",
                                                              "floatBitsToUint",
                                                              "intBitsToFloat",
                                                              "uintBitsToFloat"};
            return s;
        }

        std::string Slot(int n) {
            return std::to_string(n);
        }
    }

    const std::vector<UniformDecl>& BuiltinUniformDecls() {
        static const std::vector<UniformDecl> decls = {
            {"iris_ModelViewMat", "mat4", "", 0}, {"iris_ModelViewMatInverse", "mat4", "", 0},
            {"iris_ProjMat", "mat4", "", 0},      {"iris_ProjMatInverse", "mat4", "", 0},
            {"iris_NormalMat", "mat3", "", 0},    {"iris_TextureMat", "mat4", "2", 2},
            {"iris_FogColor", "vec4", "", 0},     {"iris_FogDensity", "float", "", 0},
            {"iris_FogStart", "float", "", 0},    {"iris_FogEnd", "float", "", 0},
            {"iris_FogScale", "float", "", 0},    {"iris_ViewHeight", "float", "", 0},
            {"iris_ColorModulator", "vec4", "", 0},
        };
        return decls;
    }

    namespace {
        struct Statement {
            size_t begin = 0, end = 0;
            bool function = false;
        };

        struct Prepared {
            Stage stage;
            std::vector<Tok> tokens;
            std::vector<Statement> statements;
            int version = 120;
            bool compat = false;
        };

        void SplitSamplerParams(std::vector<Tok>& t) {
            std::set<std::string> globals;
            for (size_t i = 0; i < t.size(); i++) {
                if (t[i].kind != Tok::Ident || t[i].text != "uniform")
                    continue;
                bool opaque = false;
                for (size_t k = i + 1; k < t.size() && t[k].text != ";"; k++) {
                    if (t[k].kind != Tok::Ident)
                        continue;
                    if (IsOpaqueType(t[k].text) && !t[k].text.starts_with("image"))
                        opaque = true;
                    else if (opaque && !IsQualifier(t[k].text) && !IsType(t[k].text) &&
                             t[k].text != "texture")
                        globals.insert(t[k].text);
                }
            }
            const auto samplerType = [](const std::string& x) {
                return IsOpaqueType(x) && x.contains("sampler");
            };
            struct Fn {
                std::vector<int> params;
            };
            std::map<std::string, Fn> fns;
            std::set<std::string> paramNames;
            struct Def {
                size_t open, close, bodyEnd;
                std::vector<std::pair<size_t, size_t>> params;
            };
            std::vector<Def> defs;
            const auto returnType = [&](size_t i) {
                if (i == 0)
                    return false;
                size_t j = i - 1;
                if (t[j].text == "]") {
                    while (j > 0 && t[j].text != "[")
                        j--;
                    if (j == 0)
                        return false;
                    j--;
                }
                return IsType(t[j].text) || t[j].text == "void";
            };
            int brace = 0;
            for (size_t i = 0; i + 1 < t.size(); i++) {
                if (t[i].text == "{")
                    brace++;
                else if (t[i].text == "}")
                    brace--;
                if (brace != 0 || t[i].kind != Tok::Ident || t[i + 1].text != "(" || !returnType(i))
                    continue;

                Def d{i + 1, 0, 0, {}};
                int depth = 0, index = 0;
                size_t k = i + 1;
                bool any = false;
                std::vector<int> positions;
                size_t typeTok = 0;
                for (; k < t.size(); k++) {
                    if (t[k].text == "(")
                        depth++;
                    else if (t[k].text == ")" && --depth == 0)
                        break;
                    else if (depth == 1 && t[k].text == ",")
                        index++;
                    else if (depth == 1 && t[k].kind == Tok::Ident && samplerType(t[k].text)) {
                        typeTok = k;
                        if (k + 1 < t.size() && t[k + 1].kind == Tok::Ident) {
                            d.params.emplace_back(k, k + 1);
                            positions.push_back(index);
                            any = true;
                        }
                    }
                }
                (void)typeTok;
                if (!any)
                    continue;
                d.close = k;
                size_t e = k + 1;
                if (e < t.size() && t[e].text == "{") {
                    int b = 0;
                    for (; e < t.size(); e++) {
                        if (t[e].text == "{")
                            b++;
                        else if (t[e].text == "}" && --b == 0)
                            break;
                    }
                    e++;
                }
                d.bodyEnd = std::min(e, t.size());
                fns[t[i].text].params = positions;
                for (const auto& pr : d.params)
                    paramNames.insert(t[pr.second].text);
                defs.push_back(d);
            }
            if (defs.empty())
                return;

            for (size_t i = 0; i + 1 < t.size(); i++) {
                if (t[i].kind != Tok::Ident || t[i + 1].text != "(")
                    continue;
                const auto it = fns.find(t[i].text);
                if (it == fns.end() || returnType(i))
                    continue;
                int depth = 0, index = 0;
                size_t argStart = i + 2;
                for (size_t k = i + 1; k < t.size(); k++) {
                    const bool last = t[k].text == ")" && depth == 1;
                    if (t[k].text == "(" || t[k].text == "[")
                        depth++;
                    else if ((t[k].text == ")" || t[k].text == "]") && --depth == 0) {
                        if (std::ranges::find(it->second.params, index) != it->second.params.end() &&
                            k == argStart + 1) {
                            Tok& a = t[argStart];
                            if (a.kind == Tok::Ident &&
                                (globals.contains(a.text) || paramNames.contains(a.text) || a.text == "iris_texture" || a.text == "texture"))
                                a.text = (a.text == "texture" ? std::string("iris_texture") : a.text) + "_iris_t, " +
                                         (a.text == "texture" ? std::string("iris_texture") : a.text) + "_iris_s";
                        }
                        break;
                    }
                    if (depth == 1 && (t[k].text == "," || last)) {
                        if (std::ranges::find(it->second.params, index) != it->second.params.end() &&
                            k == argStart + 1) {
                            Tok& a = t[argStart];
                            if (a.kind == Tok::Ident &&
                                (globals.contains(a.text) || paramNames.contains(a.text) || a.text == "iris_texture" || a.text == "texture"))
                                a.text = (a.text == "texture" ? std::string("iris_texture") : a.text) + "_iris_t, " +
                                         (a.text == "texture" ? std::string("iris_texture") : a.text) + "_iris_s";
                        }
                        index++;
                        argStart = k + 1;
                    }
                }
            }
            for (const Def& d : defs) {
                for (const auto& pr : d.params) {
                    const std::string type = t[pr.first].text, name = t[pr.second].text;
                    std::string texture = type;
                    const bool shadow =
                        texture.size() > 6 && texture.compare(texture.size() - 6, 6, "Shadow") == 0;
                    if (shadow)
                        texture.erase(texture.size() - 6);
                    texture.replace(texture.find("sampler"), 7, "texture");

                    int depth = 0, shadowedFrom = -1, pendingShadow = -1;
                    for (size_t k = d.close; k < d.bodyEnd; k++) {
                        if (t[k].text == "{") {
                            depth++;
                        } else if (t[k].text == "}") {
                            depth--;
                            if (shadowedFrom >= 0 && depth < shadowedFrom)
                                shadowedFrom = -1;
                        } else if (t[k].text == ";" && pendingShadow >= 0 && depth == pendingShadow) {
                            shadowedFrom = pendingShadow;
                            pendingShadow = -1;
                        }
                        if (t[k].kind != Tok::Ident || t[k].text != name)
                            continue;
                        if (shadowedFrom < 0 && pendingShadow < 0 && depth > 0 && k > 0 &&
                            t[k - 1].kind == Tok::Ident && IsType(t[k - 1].text) && !samplerType(t[k - 1].text)) {
                            pendingShadow = depth;
                            continue;
                        }
                        if (shadowedFrom >= 0)
                            continue;
                        t[k].text = type + "(" + name + "_iris_t, " + name + "_iris_s)";
                    }
                    t[pr.first].text = texture;
                    t[pr.second].text = name + "_iris_t, " +
                                        std::string(shadow ? "samplerShadow " : "sampler ") + name +
                                        "_iris_s";
                }
            }
        }

        std::string HoistStructs(std::string& body) {
            std::string structs;
            for (;;) {
                const size_t a = body.find("/*STRUCT_BEGIN*/");
                if (a == std::string::npos)
                    break;
                const size_t b = body.find("/*STRUCT_END*/", a);
                if (b == std::string::npos)
                    break;
                structs += body.substr(a + 16, b - a - 16) + "\n";
                body.erase(a, b + 14 - a);
            }
            return structs;
        }

        std::vector<Statement> SplitStatements(const std::vector<Tok>& t) {
            std::vector<Statement> out;
            size_t i = 0;
            while (i < t.size()) {
                if (t[i].kind == Tok::Directive) {
                    out.push_back({i, i + 1, false});
                    i++;
                    continue;
                }
                if (t[i].text.empty()) {
                    i++;
                    continue;
                }
                const size_t start = i;
                int paren = 0, bracket = 0;
                bool sawParen = false;
                bool finished = false;
                while (i < t.size() && !finished) {
                    const std::string& x = t[i].text;
                    if (t[i].kind == Tok::Directive)
                        break;
                    if (x == "(") {
                        paren++;
                        sawParen = true;
                    } else if (x == ")")
                        paren--;
                    else if (x == "[")
                        bracket++;
                    else if (x == "]")
                        bracket--;
                    else if (x == ";" && paren == 0 && bracket == 0) {
                        i++;
                        out.push_back({start, i, false});
                        finished = true;
                        break;
                    } else if (x == "{" && paren == 0) {
                        const bool isFunction = sawParen && i > start && t[i - 1].text == ")";
                        int depth = 0;
                        size_t k = i;
                        for (; k < t.size(); k++) {
                            if (t[k].text == "{")
                                depth++;
                            else if (t[k].text == "}") {
                                depth--;
                                if (depth == 0) {
                                    k++;
                                    break;
                                }
                            }
                        }
                        i = k;
                        if (isFunction) {
                            out.push_back({start, i, true});
                            finished = true;
                            break;
                        }
                        continue;
                    }
                    i++;
                }
                if (!finished && i > start)
                    out.push_back({start, i, false});
            }
            return out;
        }

        size_t MatchClose(const std::vector<Tok>& t, size_t open) {
            const std::string o = t[open].text;
            const std::string c = o == "(" ? ")" : o == "[" ? "]" : "}";
            int depth = 0;
            for (size_t i = open; i < t.size(); i++) {
                if (t[i].text == o)
                    depth++;
                else if (t[i].text == c) {
                    depth--;
                    if (depth == 0)
                        return i;
                }
            }
            return t.size();
        }

        bool ParseIntLiteral(const std::string& s, int& out) {
            if (s.empty())
                return false;
            char* end = nullptr;
            const long v = std::strtol(s.c_str(), &end, 0);
            while (end && *end && (*end == 'u' || *end == 'U'))
                end++;
            if (end == s.c_str() || (end && *end))
                return false;
            out = static_cast<int>(v);
            return true;
        }

        std::string StripSpace(const std::string& s) {
            std::string out;
            for (char c : s)
                if (!std::isspace(static_cast<unsigned char>(c)))
                    out += c;
            return out;
        }

        struct DeclInfo {
            std::vector<std::string> qualifiers;
            std::string layout;
            std::string type;
            struct Name {
                std::string name;
                std::string arraySize;
                std::string init;
            };
            std::vector<Name> names;
            bool valid = false;
        };

        DeclInfo ParseDeclaration(const std::vector<Tok>& t, size_t begin, size_t end) {
            DeclInfo d;
            size_t i = begin;
            while (i < end) {
                const std::string& x = t[i].text;
                if (x == "layout" && i + 1 < end && t[i + 1].text == "(") {
                    const size_t close = MatchClose(t, i + 1);
                    d.layout = Join(t, i + 2, close);
                    i = close + 1;
                    continue;
                }
                if (t[i].kind == Tok::Ident && IsQualifier(x)) {
                    d.qualifiers.push_back(x);
                    i++;
                    continue;
                }
                break;
            }
            if (i >= end || t[i].kind != Tok::Ident)
                return d;
            if (t[i].text == "struct")
                return d;
            d.type = t[i].text;
            i++;
            std::string typeArray;
            if (i < end && t[i].text == "[") {
                const size_t close = MatchClose(t, i);
                typeArray = StripSpace(Join(t, i + 1, close));
                i = close + 1;
            }
            while (i < end) {
                if (t[i].kind != Tok::Ident)
                    return d;
                DeclInfo::Name n;
                n.name = t[i].text;
                n.arraySize = typeArray;
                i++;
                while (i < end && t[i].text == "[") {
                    const size_t close = MatchClose(t, i);
                    n.arraySize = StripSpace(Join(t, i + 1, close));
                    i = close + 1;
                }
                if (i < end && t[i].text == "=") {
                    i++;
                    size_t k = i;
                    int depth = 0;
                    for (; k < end; k++) {
                        if (t[k].text == "(" || t[k].text == "[" || t[k].text == "{")
                            depth++;
                        else if (t[k].text == ")" || t[k].text == "]" || t[k].text == "}")
                            depth--;
                        else if ((t[k].text == "," || t[k].text == ";") && depth == 0)
                            break;
                    }
                    n.init = Join(t, i, k);
                    i = k;
                }
                d.names.push_back(n);
                if (i < end && t[i].text == ",") {
                    i++;
                    continue;
                }
                break;
            }
            d.valid = !d.names.empty();
            return d;
        }

        bool HasQualifier(const DeclInfo& d, const char* q) {
            return std::ranges::find(d.qualifiers, q) != d.qualifiers.end();
        }

        std::string LayoutWithout(const std::string& layout, const char* key, int* value = nullptr) {
            std::vector<std::string> parts;
            std::string cur;
            for (char c : layout) {
                if (c == ',') {
                    parts.push_back(cur);
                    cur.clear();
                } else
                    cur += c;
            }
            if (!cur.empty())
                parts.push_back(cur);
            std::string out;
            for (const std::string& p : parts) {
                const std::string s = StripSpace(p);
                if (s.compare(0, std::char_traits<char>::length(key), key) == 0 &&
                    s.size() > std::char_traits<char>::length(key) &&
                    s[std::char_traits<char>::length(key)] == '=') {
                    if (value)
                        *value = std::atoi(s.c_str() + std::char_traits<char>::length(key) + 1);
                    continue;
                }
                if (!s.empty()) {
                    if (!out.empty())
                        out += ", ";
                    out += s;
                }
            }
            return out;
        }

        struct StageContext {
            Stage stage;
            const TransformOptions* options;
            std::string error;
            BuiltinUse use;
            VertexInputs vin;
            std::vector<UniformDecl> uniforms;
            std::vector<SamplerDecl> samplers;
            std::vector<InterfaceVar> inputs, outputs;
            std::vector<std::string> bodyDecls;
            std::set<std::string> userFunctions;
            std::vector<int> fragmentLocations;
        };

        std::string ReplaceSampler(const std::string& type) {
            return type;
        }
    }

    namespace {
        std::string BuildPrefix(const TransformOptions& options) {
            std::string out = "#version 450\n";
            for (const auto& m : options.macros)
                out += "#define " + m.first + " " + m.second + "\n";
            return out;
        }

        bool UnsupportedExtension(std::string_view name) {
            static constexpr std::string_view kPrefixes[] = {
                "GL_EXT_shader_explicit_arithmetic_types", "GL_EXT_shader_16bit_storage",
                "GL_EXT_shader_8bit_storage",              "GL_EXT_shader_atomic_float",
                "GL_NV_",                                  "GL_AMD_",
                "GL_INTEL_",                               "GL_ARB_gpu_shader_int64"};
            for (const std::string_view prefix : kPrefixes)
                if (name.starts_with(prefix))
                    return true;
            return false;
        }

        std::string HideUnsupportedExtensions(const std::string& line) {
            std::string out;
            out.reserve(line.size() + 32);
            for (size_t i = 0; i < line.size();) {
                if (line.compare(i, 3, "GL_") == 0 &&
                    (i == 0 || !(std::isalnum(static_cast<unsigned char>(line[i - 1])) || line[i - 1] == '_'))) {
                    size_t e = i;
                    while (e < line.size() && (std::isalnum(static_cast<unsigned char>(line[e])) || line[e] == '_'))
                        e++;
                    const std::string_view word(line.data() + i, e - i);
                    if (UnsupportedExtension(word))
                        out += "PERLIN_NO_";
                    out += word;
                    i = e;
                    continue;
                }
                out += line[i++];
            }
            return out;
        }

        bool ExtractVersion(const std::string& src, int& version, bool& compat, std::string& body) {
            std::istringstream in(src);
            std::string line;
            body.clear();
            version = 110;
            compat = false;
            while (std::getline(in, line)) {
                size_t a = line.find_first_not_of(" \t");
                if (a != std::string::npos && line[a] == '#') {
                    size_t b = line.find_first_not_of(" \t", a + 1);
                    if (b != std::string::npos && line.compare(b, 7, "version") == 0) {
                        std::istringstream vs(line.substr(b + 7));
                        std::string v, profile;
                        vs >> v >> profile;
                        version = std::atoi(v.c_str());
                        compat = profile == "compatibility";
                        body += "\n";
                        continue;
                    }
                    if (b != std::string::npos && line.compare(b, 9, "extension") == 0) {
                        body += "\n";
                        continue;
                    }
                    if (b != std::string::npos && (line.compare(b, 2, "if") == 0 || line.compare(b, 4, "elif") == 0) &&
                        line.find("GL_") != std::string::npos)
                        line = HideUnsupportedExtensions(line);
                }
                body += line;
                body += '\n';
            }
            return true;
        }

        int ArrayCountOf(const std::string& expr) {
            int n = 0;
            if (ParseIntLiteral(expr, n))
                return n;
            return -1;
        }

        int EvalSize(const std::string& expr, const std::map<std::string, int>& consts) {
            std::string e = StripSpace(expr);
            int n = 0;
            if (ParseIntLiteral(e, n))
                return n;
            const auto it = consts.find(e);
            if (it != consts.end())
                return it->second;
            for (char op : {'*', '+', '-', '/'}) {
                const size_t at = e.find(op);
                if (at != std::string::npos && at > 0) {
                    const int a = EvalSize(e.substr(0, at), consts), b = EvalSize(e.substr(at + 1), consts);
                    if (a < 0 || b < 0)
                        return -1;
                    return op == '*' ? a * b : op == '+' ? a + b : op == '-' ? a - b : (b ? a / b : -1);
                }
            }
            return -1;
        }
    }

    namespace {
        std::string AllowRedefinition(const std::string& text) {
            std::string out;
            out.reserve(text.size() + 4096);
            size_t pos = 0;
            while (pos < text.size()) {
                size_t end = text.find('\n', pos);
                if (end == std::string::npos)
                    end = text.size();
                const std::string line = text.substr(pos, end - pos);
                size_t a = line.find_first_not_of(" \t");
                if (a != std::string::npos && line[a] == '#') {
                    size_t b = line.find_first_not_of(" \t", a + 1);
                    if (b != std::string::npos && line.compare(b, 6, "define") == 0) {
                        size_t c = line.find_first_not_of(" \t", b + 6);
                        if (c != std::string::npos) {
                            size_t d = c;
                            while (d < line.size() && (std::isalnum(static_cast<unsigned char>(line[d])) || line[d] == '_'))
                                d++;
                            if (d > c)
                                out += "#undef " + line.substr(c, d - c) + "\n";
                        }
                    }
                }
                out += line;
                out += '\n';
                pos = end + 1;
            }
            return out;
        }
    }

    namespace {
        std::string DropRepeatedGlobalConsts(const std::string& text) {
            if (!text.contains("const"))
                return text;
            std::set<std::string> seen;
            std::string out;
            out.reserve(text.size());
            int depth = 0;
            size_t pos = 0;
            while (pos < text.size()) {
                size_t end = text.find('\n', pos);
                if (end == std::string::npos)
                    end = text.size();
                const std::string_view line(text.data() + pos, end - pos);
                pos = end + 1;
                bool drop = false;
                if (depth == 0) {
                    size_t i = line.find_first_not_of(" \t");
                    if (i != std::string_view::npos && line.substr(i, 6) == "const " && line.find('{') == std::string_view::npos &&
                        line.find('(') == std::string_view::npos && !line.empty() &&
                        line.find_last_not_of(" \t\r") != std::string_view::npos &&
                        line[line.find_last_not_of(" \t\r")] == ';' && line.find(',') == std::string_view::npos) {
                        const size_t eq = line.find('=');
                        if (eq != std::string_view::npos) {
                            size_t e = line.find_last_not_of(" \t", eq - 1);
                            size_t n = e;
                            while (n > 0 && (std::isalnum(static_cast<unsigned char>(line[n - 1])) || line[n - 1] == '_'))
                                n--;
                            if (e != std::string_view::npos && n <= e) {
                                const std::string name(line.substr(n, e - n + 1));
                                drop = !seen.insert(name).second;
                            }
                        }
                    }
                }
                for (char c : line) {
                    if (c == '{')
                        depth++;
                    else if (c == '}' && depth > 0)
                        depth--;
                }
                if (!drop)
                    out += line;
                out += '\n';
            }
            return out;
        }

        bool KnownEngineSampler(std::string_view n) {
            const auto numbered = [&](std::string_view prefix, int count) {
                if (!n.starts_with(prefix) || n.size() == prefix.size())
                    return false;
                const std::string_view rest = n.substr(prefix.size());
                if (rest.size() > 2 || !std::all_of(rest.begin(), rest.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
                    return false;
                return std::atoi(std::string(rest).c_str()) < count;
            };
            return numbered("colortex", 16) || numbered("depthtex", 3) || numbered("shadowtex", 2) ||
                   numbered("shadowcolor", 2) || numbered("gaux", 5) || n == "noisetex" || n == "gcolor" ||
                   n == "gdepth" || n == "gnormal" || n == "gdepthtex" || n == "composite" || n == "gtexture";
        }

        bool IsDeclaredIn(const std::string& text, const std::string& name) {
            static const std::set<std::string, std::less<>> kDeclarers = {
                "uniform", "float", "int", "uint", "bool", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4",
                "uvec2", "uvec3", "uvec4", "bvec2", "bvec3", "bvec4", "mat2", "mat3", "mat4", "double",
                "sampler2D", "sampler2DShadow", "sampler3D", "usampler2D", "isampler2D", "define", "in", "out",
                "varying", "attribute", "highp", "mediump", "lowp", "const", "flat", "smooth", "noperspective"};
            const auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
            for (size_t at = text.find(name); at != std::string::npos; at = text.find(name, at + 1)) {
                if ((at > 0 && word(text[at - 1])) || (at + name.size() < text.size() && word(text[at + name.size()])))
                    continue;
                size_t e = at;
                while (e > 0 && (text[e - 1] == ' ' || text[e - 1] == '\t'))
                    e--;
                size_t b = e;
                while (b > 0 && word(text[b - 1]))
                    b--;
                if (b < e && kDeclarers.contains(std::string_view(text.data() + b, e - b)))
                    return true;
            }
            return false;
        }

        bool UsesWord(const std::string& text, const std::string& name) {
            const auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
            for (size_t at = text.find(name); at != std::string::npos; at = text.find(name, at + 1))
                if (!(at > 0 && word(text[at - 1])) && !(at + name.size() < text.size() && word(text[at + name.size()])))
                    return true;
            return false;
        }

        std::string DeclareMissingBuiltins(const std::string& text) {
            static const char* const kCalls[] = {"texture",      "textureLod",  "texelFetch",     "texture2D",
                                                 "texture2DLod", "textureSize", "textureGather", "textureGrad",
                                                 "textureOffset", "texelFetchOffset", "textureLodOffset"};
            static const std::pair<const char*, const char*> kUniforms[] = {
                {"frameTimeCounter", "float"}, {"frameTime", "float"}, {"rainStrength", "float"},
                {"wetness", "float"}, {"thunderStrength", "float"}, {"sunAngle", "float"}, {"shadowAngle", "float"},
                {"near", "float"}, {"far", "float"}, {"aspectRatio", "float"}, {"viewWidth", "float"},
                {"viewHeight", "float"}, {"eyeAltitude", "float"}, {"blindness", "float"}, {"nightVision", "float"},
                {"screenBrightness", "float"}, {"centerDepthSmooth", "float"}, {"darknessFactor", "float"},
                {"playerMood", "float"}, {"worldTime", "int"}, {"worldDay", "int"}, {"moonPhase", "int"},
                {"frameCounter", "int"}, {"isEyeInWater", "int"}, {"heldItemId", "int"}, {"heldItemId2", "int"},
                {"heldBlockLightValue", "int"}, {"heldBlockLightValue2", "int"}, {"eyeBrightness", "ivec2"},
                {"eyeBrightnessSmooth", "ivec2"}, {"atlasSize", "ivec2"}, {"cameraPosition", "vec3"},
                {"previousCameraPosition", "vec3"}, {"sunPosition", "vec3"}, {"moonPosition", "vec3"},
                {"shadowLightPosition", "vec3"}, {"upPosition", "vec3"}, {"fogColor", "vec3"}, {"skyColor", "vec3"},
                {"gbufferModelView", "mat4"}, {"gbufferModelViewInverse", "mat4"}, {"gbufferProjection", "mat4"},
                {"gbufferProjectionInverse", "mat4"}, {"gbufferPreviousModelView", "mat4"},
                {"gbufferPreviousProjection", "mat4"}, {"shadowModelView", "mat4"},
                {"shadowModelViewInverse", "mat4"}, {"shadowProjection", "mat4"}, {"shadowProjectionInverse", "mat4"},
                {"entityId", "int"}, {"blockEntityId", "int"}, {"currentRenderedItemId", "int"}, {"renderStage", "int"},
                {"fogMode", "int"}, {"fogStart", "float"}, {"fogEnd", "float"}, {"fogDensity", "float"},
                {"entityColor", "vec4"}, {"lightningBoltPosition", "vec4"}, {"hideGUI", "int"}};
            std::set<std::string> samplers;
            for (const char* call : kCalls) {
                const std::string needle = std::string(call) + "(";
                for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
                    if (at > 0 && (std::isalnum(static_cast<unsigned char>(text[at - 1])) || text[at - 1] == '_'))
                        continue;
                    size_t i = text.find_first_not_of(" \t\n", at + needle.size());
                    size_t e = i;
                    while (e < text.size() && (std::isalnum(static_cast<unsigned char>(text[e])) || text[e] == '_'))
                        e++;
                    if (i == std::string::npos || e == i)
                        continue;
                    const std::string name = text.substr(i, e - i);
                    if (KnownEngineSampler(name))
                        samplers.insert(name);
                }
            }
            std::string declarations;
            for (const std::string& name : samplers)
                if (!IsDeclaredIn(text, name))
                    declarations += "uniform sampler2D " + name + ";\n";
            for (const auto& [name, type] : kUniforms)
                if (UsesWord(text, name) && !IsDeclaredIn(text, name))
                    declarations += std::string("uniform ") + type + " " + name + ";\n";
            if (declarations.empty())
                return text;
            const size_t first = text.find_first_not_of(" \t\n");
            size_t insertAt = 0;
            if (first != std::string::npos && text.compare(first, 8, "#version") == 0)
                insertAt = text.find('\n', first) + 1;
            return text.substr(0, insertAt) + declarations + text.substr(insertAt);
        }

        std::string ReplaceWord(const std::string& text, std::string_view from, std::string_view to) {
            std::string out;
            out.reserve(text.size());
            const auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
            size_t pos = 0;
            for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + from.size())) {
                if ((at > 0 && word(text[at - 1])) || (at + from.size() < text.size() && word(text[at + from.size()])))
                    continue;
                out.append(text, pos, at - pos);
                out += to;
                pos = at + from.size();
            }
            out.append(text, pos);
            return out;
        }

        std::string ChunkOffsetSemantics(std::string text) {
            if (!UsesWord(text, "chunkOffset"))
                return text;
            text = ReplaceWord(text, "chunkOffset", "iris_ChunkOffset");
            text = ReplaceWord(text, "modelViewMatrixInverse", "iris_ChunkModelViewInverse");
            text = ReplaceWord(text, "modelViewMatrix", "iris_ChunkModelView");
            std::string decls;
            for (const auto& [name, type] : {std::pair<const char*, const char*>{"iris_ChunkOffset", "vec3"},
                                             {"iris_ChunkModelView", "mat4"},
                                             {"iris_ChunkModelViewInverse", "mat4"}})
                if (UsesWord(text, name) && !IsDeclaredIn(text, name))
                    decls += std::string("uniform ") + type + " " + name + ";\n";
            if (decls.empty())
                return text;
            size_t at = 0;
            if (text.starts_with("#version")) {
                at = text.find('\n');
                at = at == std::string::npos ? text.size() : at + 1;
            }
            while (true) {
                const size_t lineEnd = text.find('\n', at);
                const std::string line = text.substr(at, lineEnd == std::string::npos ? std::string::npos : lineEnd - at);
                const size_t first = line.find_first_not_of(" \t");
                if (first == std::string::npos || !line.substr(first).starts_with("#extension") || lineEnd == std::string::npos)
                    break;
                at = lineEnd + 1;
            }
            return text.insert(at, decls);
        }

        std::string PolyfillExtensions(std::string text) {
            std::string defines;
            if (text.contains("max3(") || text.contains("min3(") || text.contains("mid3(")) {
                defines += "#define max3(a, b, c) max(a, max(b, c))\n";
                defines += "#define min3(a, b, c) min(a, min(b, c))\n";
                defines += "#define mid3(a, b, c) max(min(a, b), min(max(a, b), c))\n";
            }
            if (text.contains("subgroup") || text.contains("gl_Subgroup") || text.contains("gl_NumSubgroups")) {
                static const char* const kIdentity[] = {
                    "subgroupBroadcastFirst", "subgroupAdd", "subgroupMul", "subgroupMin", "subgroupMax",
                    "subgroupAnd", "subgroupOr", "subgroupXor", "subgroupInclusiveAdd", "subgroupInclusiveMul",
                    "subgroupInclusiveMin", "subgroupInclusiveMax", "subgroupInclusiveAnd", "subgroupInclusiveOr",
                    "subgroupInclusiveXor", "subgroupAll", "subgroupAny", "subgroupClusteredAdd",
                    "subgroupClusteredMin", "subgroupClusteredMax"};
                for (const char* fn : kIdentity)
                    if (text.contains(fn))
                        defines += std::string("#define ") + fn + "(x) (x)\n";
                static const char* const kSecondArg[] = {"subgroupBroadcast", "subgroupShuffle", "subgroupShuffleXor",
                                                         "subgroupShuffleUp", "subgroupShuffleDown"};
                for (const char* fn : kSecondArg)
                    if (text.contains(fn))
                        defines += std::string("#define ") + fn + "(x, y) (x)\n";
                if (text.contains("subgroupExclusiveAdd"))
                    defines += "#define subgroupExclusiveAdd(x) ((x) - (x))\n";
                if (text.contains("subgroupAllEqual"))
                    defines += "#define subgroupAllEqual(x) true\n";
                if (text.contains("subgroupElect"))
                    defines += "#define subgroupElect() true\n";
                if (text.contains("subgroupBallotExclusiveBitCount"))
                    defines += "#define subgroupBallotExclusiveBitCount(v) 0u\n";
                if (text.contains("subgroupBallotInclusiveBitCount"))
                    defines += "#define subgroupBallotInclusiveBitCount(v) ((v).x & 1u)\n";
                if (text.contains("subgroupBallotFindLSB"))
                    defines += "#define subgroupBallotFindLSB(v) 0u\n";
                if (text.contains("subgroupBallotFindMSB"))
                    defines += "#define subgroupBallotFindMSB(v) 0u\n";
                if (text.contains("subgroupBallotBitExtract"))
                    defines += "#define subgroupBallotBitExtract(v, i) ((((v).x >> (i)) & 1u) != 0u)\n";
                if (text.contains("subgroupInverseBallot"))
                    defines += "#define subgroupInverseBallot(v) (((v).x & 1u) != 0u)\n";
                if (text.contains("subgroupBallotBitCount"))
                    defines += "#define subgroupBallotBitCount(v) uint(bitCount((v).x))\n";
                if (text.contains("subgroupBallot"))
                    defines += "#define subgroupBallot(b) uvec4((b) ? 1u : 0u, 0u, 0u, 0u)\n";
                if (text.contains("subgroupBarrier"))
                    defines += "#define subgroupBarrier() memoryBarrierShared()\n";
                if (text.contains("subgroupMemoryBarrier"))
                    defines += "#define subgroupMemoryBarrierShared() memoryBarrierShared()\n"
                               "#define subgroupMemoryBarrier() memoryBarrier()\n";
                text = ReplaceWord(text, "gl_SubgroupSize", "1u");
                text = ReplaceWord(text, "gl_SubgroupInvocationID", "0u");
                text = ReplaceWord(text, "gl_SubgroupEqMask", "uvec4(1u, 0u, 0u, 0u)");
                text = ReplaceWord(text, "gl_SubgroupLtMask", "uvec4(0u)");
                text = ReplaceWord(text, "gl_SubgroupLeMask", "uvec4(1u, 0u, 0u, 0u)");
                text = ReplaceWord(text, "gl_SubgroupID", "gl_LocalInvocationIndex");
                text = ReplaceWord(text, "gl_NumSubgroups",
                                   "(gl_WorkGroupSize.x * gl_WorkGroupSize.y * gl_WorkGroupSize.z)");
            }
            if (defines.empty())
                return text;
            return defines + text;
        }

        std::string BalanceConditionals(const std::string& text) {
            std::string out;
            out.reserve(text.size());
            int depth = 0;
            size_t pos = 0;
            while (pos < text.size()) {
                size_t end = text.find('\n', pos);
                if (end == std::string::npos)
                    end = text.size();
                const std::string line = text.substr(pos, end - pos);
                pos = end + 1;
                const size_t a = line.find_first_not_of(" \t");
                if (a != std::string::npos && line[a] == '#') {
                    const size_t b = line.find_first_not_of(" \t", a + 1);
                    const auto is = [&](const char* word) {
                        const size_t n = std::strlen(word);
                        return b != std::string::npos && line.compare(b, n, word) == 0 &&
                               (b + n >= line.size() || !std::isalnum(static_cast<unsigned char>(line[b + n])));
                    };
                    if (is("if") || is("ifdef") || is("ifndef")) {
                        depth++;
                    } else if (is("endif")) {
                        if (depth == 0) {
                            out += '\n';
                            continue;
                        }
                        depth--;
                    } else if ((is("else") || is("elif")) && depth == 0) {
                        out += '\n';
                        continue;
                    }
                }
                out += line;
                out += '\n';
            }
            while (depth-- > 0)
                out += "#endif\n";
            return out;
        }

        std::string RepairConditionals(const std::string& text) {
            std::set<std::string> empty;
            {
                size_t at = 0;
                while (at < text.size()) {
                    size_t end = text.find('\n', at);
                    if (end == std::string::npos)
                        end = text.size();
                    const std::string_view line(text.data() + at, end - at);
                    const size_t h = line.find_first_not_of(" \t");
                    if (h != std::string_view::npos && line[h] == '#') {
                        const size_t d = line.find_first_not_of(" \t", h + 1);
                        if (d != std::string_view::npos && line.substr(d, 6) == "define") {
                            size_t n = line.find_first_not_of(" \t", d + 6);
                            size_t e = n;
                            while (e < line.size() && (std::isalnum(static_cast<unsigned char>(line[e])) || line[e] == '_'))
                                e++;
                            if (n != std::string_view::npos && e > n &&
                                line.find_first_not_of(" \t\r", e) == std::string_view::npos)
                                empty.insert(std::string(line.substr(n, e - n)));
                        }
                    }
                    at = end + 1;
                }
            }
            const auto fillEmpty = [&](const std::string& expr) {
                std::string result;
                bool afterDefined = false;
                for (size_t i = 0; i < expr.size();) {
                    if (std::isalpha(static_cast<unsigned char>(expr[i])) || expr[i] == '_') {
                        size_t e = i;
                        while (e < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[e])) || expr[e] == '_'))
                            e++;
                        const std::string word = expr.substr(i, e - i);
                        result += !afterDefined && empty.contains(word) ? std::string("1") : word;
                        afterDefined = word == "defined";
                        i = e;
                        continue;
                    }
                    if (expr[i] != ' ' && expr[i] != '\t' && expr[i] != '(')
                        afterDefined = false;
                    result += expr[i++];
                }
                return result;
            };
            std::map<std::string, std::string> numeric;
            const auto numberLiteral = [](std::string_view v) {
                size_t i = 0;
                if (i < v.size() && (v[i] == '-' || v[i] == '+'))
                    i++;
                size_t digits = 0;
                while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) {
                    i++;
                    digits++;
                }
                if (i < v.size() && v[i] == '.') {
                    i++;
                    while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) {
                        i++;
                        digits++;
                    }
                }
                if (i < v.size() && (v[i] == 'f' || v[i] == 'F'))
                    i++;
                return digits > 0 && i == v.size();
            };
            const auto scaleFloats = [&](const std::string& expr) {
                std::string sub;
                bool afterDefined = false, anyFloat = false;
                for (size_t i = 0; i < expr.size();) {
                    if (std::isalpha(static_cast<unsigned char>(expr[i])) || expr[i] == '_') {
                        size_t e = i;
                        while (e < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[e])) || expr[e] == '_'))
                            e++;
                        const std::string word = expr.substr(i, e - i);
                        const auto it = afterDefined ? numeric.end() : numeric.find(word);
                        sub += it != numeric.end() ? "(" + it->second + ")" : word;
                        afterDefined = word == "defined";
                        i = e;
                        continue;
                    }
                    if (expr[i] != ' ' && expr[i] != '\t' && expr[i] != '(')
                        afterDefined = false;
                    sub += expr[i++];
                }
                std::string scaled;
                for (size_t i = 0; i < sub.size();) {
                    const bool startsNumber =
                        std::isdigit(static_cast<unsigned char>(sub[i])) ||
                        (sub[i] == '.' && i + 1 < sub.size() && std::isdigit(static_cast<unsigned char>(sub[i + 1])));
                    const bool partOfWord =
                        i > 0 && (std::isalnum(static_cast<unsigned char>(sub[i - 1])) || sub[i - 1] == '_');
                    if (!startsNumber || partOfWord || (sub.compare(i, 2, "0x") == 0 || sub.compare(i, 2, "0X") == 0)) {
                        scaled += sub[i++];
                        continue;
                    }
                    size_t e = i;
                    while (e < sub.size() && (std::isdigit(static_cast<unsigned char>(sub[e])) || sub[e] == '.'))
                        e++;
                    const std::string number = sub.substr(i, e - i);
                    if (number.contains('.'))
                        anyFloat = true;
                    while (e < sub.size() && (sub[e] == 'f' || sub[e] == 'F' || sub[e] == 'u' || sub[e] == 'U'))
                        e++;
                    scaled += std::to_string(std::llround(std::strtod(number.c_str(), nullptr) * 1000.0));
                    i = e;
                }
                return anyFloat ? scaled : expr;
            };
            std::string out;
            out.reserve(text.size());
            size_t pos = 0;
            while (pos < text.size()) {
                size_t end = text.find('\n', pos);
                if (end == std::string::npos)
                    end = text.size();
                std::string line = text.substr(pos, end - pos);
                size_t a = line.find_first_not_of(" \t");
                if (a != std::string::npos && line[a] == '#') {
                    size_t b = line.find_first_not_of(" \t", a + 1);
                    if (b != std::string::npos && (line.compare(b, 6, "define") == 0 || line.compare(b, 5, "undef") == 0)) {
                        const bool define = line.compare(b, 6, "define") == 0;
                        size_t n = line.find_first_not_of(" \t", b + (define ? 6 : 5));
                        size_t e = n;
                        while (e < line.size() && (std::isalnum(static_cast<unsigned char>(line[e])) || line[e] == '_'))
                            e++;
                        if (n != std::string::npos && e > n) {
                            const std::string name = line.substr(n, e - n);
                            std::string value = define && (e >= line.size() || line[e] != '(') ? line.substr(e) : std::string();
                            if (const size_t c = value.find("//"); c != std::string::npos)
                                value.resize(c);
                            const size_t v0 = value.find_first_not_of(" \t\r");
                            const size_t v1 = value.find_last_not_of(" \t\r");
                            value = v0 == std::string::npos ? std::string() : value.substr(v0, v1 - v0 + 1);
                            if (define && numberLiteral(value))
                                numeric[name] = value;
                            else
                                numeric.erase(name);
                        }
                    }
                    const bool isIf = b != std::string::npos && line.compare(b, 2, "if") == 0 &&
                                      (b + 2 >= line.size() || !std::isalpha(static_cast<unsigned char>(line[b + 2])) ||
                                       line.compare(b, 5, "ifdef") == 0 || line.compare(b, 6, "ifndef") == 0);
                    const bool isElif = b != std::string::npos && line.compare(b, 4, "elif") == 0;
                    if ((isIf && line.compare(b, 5, "ifdef") != 0 && line.compare(b, 6, "ifndef") != 0) || isElif) {
                        {
                            const size_t kw = b + (isElif ? 4 : 2);
                            std::string expr = line.substr(kw);
                            if (const size_t c = expr.find("//"); c != std::string::npos)
                                expr.resize(c);
                            if (!empty.empty())
                                expr = fillEmpty(expr);
                            line = line.substr(0, kw) + scaleFloats(expr);
                        }
                        int depth = 0;
                        bool bad = false;
                        for (char c : line.substr(b)) {
                            if (c == '(')
                                depth++;
                            else if (c == ')' && --depth < 0)
                                bad = true;
                        }
                        if (depth != 0 || bad)
                            line = line.substr(0, b) + (isElif ? "elif 0" : "if 0");
                    }
                }
                out += line;
                out += '\n';
                pos = end + 1;
            }
            return out;
        }
    }

    namespace {
        bool IsIrisVertexAttribute(const std::string& name) {
            return name == "vaPosition" || name == "vaColor" || name == "vaUV0" || name == "vaUV2" ||
                   name == "vaNormal";
        }
    }

    namespace {
        std::string RenameGlMacros(const std::string& text) {
            static const std::regex define(R"(#\s*define\s+(GL_[A-Za-z0-9_]*))");
            if (!text.contains("GL_"))
                return text;
            std::set<std::string> names;
            for (auto it = std::sregex_iterator(text.begin(), text.end(), define); it != std::sregex_iterator(); ++it)
                names.insert((*it)[1].str());
            std::string out = text;
            for (const std::string& name : names) {
                std::string result;
                size_t pos = 0;
                while (true) {
                    const size_t at = out.find(name, pos);
                    if (at == std::string::npos) {
                        result += out.substr(pos);
                        break;
                    }
                    const bool before = at > 0 && (std::isalnum(static_cast<unsigned char>(out[at - 1])) || out[at - 1] == '_');
                    const size_t endAt = at + name.size();
                    const bool after = endAt < out.size() && (std::isalnum(static_cast<unsigned char>(out[endAt])) || out[endAt] == '_');
                    result += out.substr(pos, at - pos);
                    result += before || after ? name : "P" + name;
                    pos = endAt;
                }
                out = std::move(result);
            }
            return out;
        }
    }

    namespace {
        std::string StripFunctionSemicolons(const std::string& text) {
            std::string out;
            out.reserve(text.size());
            std::vector<bool> functionBody;
            int depth = 0;
            bool afterFunctionClose = false;
            for (size_t i = 0; i < text.size(); i++) {
                const char c = text[i];
                if (afterFunctionClose) {
                    if (c == ';') {
                        afterFunctionClose = false;
                        continue;
                    }
                    if (!std::isspace(static_cast<unsigned char>(c)))
                        afterFunctionClose = false;
                }
                if (c == '{') {
                    size_t j = out.size();
                    while (j > 0 && std::isspace(static_cast<unsigned char>(out[j - 1])))
                        j--;
                    functionBody.push_back(depth == 0 && j > 0 && out[j - 1] == ')');
                    depth++;
                } else if (c == '}' && depth > 0) {
                    depth--;
                    const bool wasFunction = functionBody.back();
                    functionBody.pop_back();
                    if (wasFunction)
                        afterFunctionClose = true;
                }
                out += c;
            }
            return out;
        }
    }

    namespace {
        std::string AttributesToUniforms(const std::string& text) {
            std::string out = text;
            for (const char* name : {"blockEntityId", "entityId", "currentRenderedItemId"}) {
                for (const char* kw : {"attribute", "in"}) {
                    const std::string needle = std::string(kw) + " int " + name + ";";
                    size_t at = 0;
                    while ((at = out.find(needle, at)) != std::string::npos) {
                        const bool startOk = at == 0 || !(std::isalnum(static_cast<unsigned char>(out[at - 1])) || out[at - 1] == '_');
                        if (startOk)
                            out.replace(at, std::strlen(kw), "uniform");
                        at += 7;
                    }
                }
            }
            return out;
        }
    }

    namespace {
        StageResult TransformStage(Stage stage, const std::string& rawSource,
                                   const TransformOptions& options) {
            StageResult result;
            result.stage = stage;
            if (!options.preprocess) {
                result.ok = false;
                result.error = "no preprocessor available";
                return result;
            }
            int version = 120;
            bool compat = false;
            std::string body;
            ExtractVersion(rawSource, version, compat, body);

            std::string error;
            std::string source = BuildPrefix(options) + body;
            std::string pre = options.preprocess(source, stage, error);
            bool redefined = false, glRenamed = false, repaired = false, balanced = false;
            while (pre.empty() && !error.empty()) {
                if (!redefined && error.find("Macro redefined") != std::string::npos) {
                    redefined = true;
                    source = AllowRedefinition(source);
                } else if (!glRenamed && error.find("names beginning with \"GL_\"") != std::string::npos) {
                    glRenamed = true;
                    source = RenameGlMacros(source);
                } else if (!repaired && error.find("preprocessor evaluation") != std::string::npos) {
                    repaired = true;
                    source = RepairConditionals(source);
                } else if (!balanced && (error.find("mismatched statements") != std::string::npos ||
                                         error.find("missing #endif") != std::string::npos ||
                                         error.find("#else after #else") != std::string::npos)) {
                    balanced = true;
                    source = BalanceConditionals(source);
                } else {
                    break;
                }
                pre = options.preprocess(source, stage, error);
            }
            if (pre.empty() && !error.empty()) {
                if (const char* dumpTo = std::getenv("JSHADER_PRE_DUMP"))
                    std::ofstream(dumpTo, std::ios::binary) << source;
                result.ok = false;
                result.error = "preprocess: " + error;
                return result;
            }

            pre = AttributesToUniforms(pre);
            pre = StripFunctionSemicolons(pre);
            pre = DropRepeatedGlobalConsts(pre);
            pre = DeclareMissingBuiltins(pre);
            pre = ChunkOffsetSemantics(std::move(pre));
            pre = PolyfillExtensions(std::move(pre));
            std::vector<Tok> tokens = Lex(pre);
            for (Tok& tk : tokens) {
                if (tk.kind == Tok::Directive) {
                    std::string d = tk.text;
                    size_t a = d.find_first_not_of("# \t");
                    const std::string name = a == std::string::npos ? std::string() : d.substr(a, 8);
                    if (name.starts_with("version") || name.starts_with("extension") ||
                        name.starts_with("line") || name.starts_with("pragma")) {
                        tk.text.clear();
                        tk.kind = Tok::Punct;
                    }
                }
            }
            for (size_t k = 0; k < tokens.size(); k++) {
                if (tokens[k].kind != Tok::Ident)
                    continue;
                if (tokens[k].text == "highp" || tokens[k].text == "mediump" || tokens[k].text == "lowp") {
                    tokens[k].text.clear();
                    tokens[k].kind = Tok::Punct;
                } else if (tokens[k].text == "precision") {
                    size_t e = k;
                    while (e < tokens.size() && tokens[e].text != ";")
                        e++;
                    for (size_t q = k; q <= e && q < tokens.size(); q++) {
                        tokens[q].text.clear();
                        tokens[q].kind = Tok::Punct;
                    }
                    k = e;
                }
            }
            {
                static const std::set<std::string, std::less<>> kLateBuiltins = {
                    "packSnorm4x8",  "unpackSnorm4x8",  "packUnorm4x8",  "unpackUnorm4x8",  "packSnorm2x16",
                    "unpackSnorm2x16", "packUnorm2x16", "unpackUnorm2x16", "packHalf2x16",    "unpackHalf2x16"};
                std::set<std::string> userDefined;
                int depth = 0;
                for (size_t k = 0; k + 1 < tokens.size(); k++) {
                    if (tokens[k].text == "{")
                        depth++;
                    else if (tokens[k].text == "}")
                        depth--;
                    if (depth == 0 && tokens[k].kind == Tok::Ident && tokens[k + 1].text == "(" && k > 0 &&
                        (IsType(tokens[k - 1].text) || tokens[k - 1].text == "void") &&
                        kLateBuiltins.contains(tokens[k].text))
                        userDefined.insert(tokens[k].text);
                }
                if (!userDefined.empty())
                    for (Tok& tk : tokens)
                        if (tk.kind == Tok::Ident && userDefined.contains(tk.text))
                            tk.text = "iris_user_" + tk.text;
            }
            RenameShadowedSamplers(tokens);
            if (options.splitSamplers)
                SplitSamplerParams(tokens);
            const std::vector<Statement> statements = SplitStatements(tokens);

            StageContext ctx;
            ctx.stage = stage;
            ctx.options = &options;

            std::set<std::string> renameFunctions;
            if (version < 130) {
                for (const Statement& st : statements) {
                    if (!st.function)
                        continue;
                    size_t p = st.begin;
                    while (p < st.end && tokens[p].text != "(")
                        p++;
                    if (p > st.begin && tokens[p - 1].kind == Tok::Ident &&
                        Builtin450Functions().contains(tokens[p - 1].text))
                        renameFunctions.insert(tokens[p - 1].text);
                }
            }
            for (const Statement& st : statements) {
                if (!st.function)
                    continue;
                size_t p = st.begin;
                while (p < st.end && tokens[p].text != "(")
                    p++;
                if (p > st.begin && tokens[p - 1].kind == Tok::Ident)
                    ctx.userFunctions.insert(tokens[p - 1].text);
            }
            const bool userDefinesTexture = ctx.userFunctions.contains("texture");

            std::set<std::string> declaredNames;
            for (size_t i = 1; i < tokens.size(); i++)
                if (tokens[i].kind == Tok::Ident && tokens[i - 1].kind == Tok::Ident &&
                    (IsType(tokens[i - 1].text) || IsQualifier(tokens[i - 1].text))) {
                    if (IsIrisVertexAttribute(tokens[i].text) && i >= 2 &&
                        (tokens[i - 2].text == "in" || tokens[i - 2].text == "attribute"))
                        continue;
                    declaredNames.insert(tokens[i].text);
                }

            std::map<std::string, std::string> constInts;
            std::vector<std::string> out(tokens.size());
            std::set<size_t> lodClose;
            const auto closeOf = [&](size_t open) {
                int depth = 0;
                for (size_t k = open; k < tokens.size(); k++) {
                    if (tokens[k].text == "(")
                        depth++;
                    else if (tokens[k].text == ")" && --depth == 0)
                        return k;
                }
                return tokens.size() - 1;
            };
            const auto lodArgs = [&](size_t name) {
                int depth = 0, args = 1;
                for (size_t k = name + 1; k < tokens.size(); k++) {
                    if (tokens[k].text == "(" || tokens[k].text == "[")
                        depth++;
                    else if (tokens[k].text == ")" || tokens[k].text == "]") {
                        if (--depth == 0)
                            return args;
                    } else if (depth == 1 && tokens[k].text == ",")
                        args++;
                }
                return 0;
            };
            for (size_t i = 0; i < tokens.size(); i++) {
                Tok& tk = tokens[i];
                if (tk.kind != Tok::Ident) {
                    out[i] = tk.ws + tk.text;
                    continue;
                }
                const std::string& id = tk.text;
                const bool nextCall = i + 1 < tokens.size() && tokens[i + 1].text == "(";
                const bool afterDot = i > 0 && tokens[i - 1].text == ".";
                std::string rep;
                if (afterDot) {
                    rep = id;
                } else if (renameFunctions.contains(id)) {
                    rep = "iris_" + id;
                } else if (!declaredNames.contains(id) &&
                           (id == "projectionMatrix" || id == "modelViewMatrix" || id == "normalMatrix" ||
                            id == "modelViewMatrixInverse" || id == "projectionMatrixInverse" ||
                            id == "textureMatrix" || id == "chunkOffset" || id == "vaPosition" ||
                            id == "vaColor" || id == "vaUV0" || id == "vaUV2" || id == "vaNormal")) {
                    if (id == "projectionMatrix") {
                        ctx.use.proj = true;
                        rep = "iris_ProjMat";
                    } else if (id == "modelViewMatrix") {
                        ctx.use.modelView = true;
                        rep = "iris_ModelViewMat";
                    } else if (id == "normalMatrix") {
                        ctx.use.normalMat = true;
                        rep = "iris_NormalMat";
                    } else if (id == "modelViewMatrixInverse") {
                        ctx.use.modelViewInv = true;
                        rep = "iris_ModelViewMatInverse";
                    } else if (id == "projectionMatrixInverse") {
                        ctx.use.projInv = true;
                        rep = "iris_ProjMatInverse";
                    } else if (id == "textureMatrix") {
                        ctx.use.textureMat = true;
                        rep = "iris_TextureMat[0]";
                    } else if (id == "chunkOffset")
                        rep = "vec3(0.0)";
                    else if (id == "vaPosition") {
                        ctx.vin.position = true;
                        rep = "iris_Position";
                    } else if (id == "vaColor") {
                        ctx.vin.color = true;
                        rep = "iris_Color";
                    } else if (id == "vaUV0") {
                        ctx.vin.uv0 = true;
                        rep = "iris_UV0";
                    } else if (id == "vaUV2") {
                        ctx.vin.uv2 = true;
                        rep = "ivec2(iris_UV2)";
                    } else {
                        ctx.vin.normal = true;
                        rep = "iris_Normal";
                    }
                } else if (id == "gl_Vertex") {
                    ctx.vin.position = true;
                    rep = "vec4(iris_Position, 1.0)";
                } else if (id == "gl_Color" && stage == Stage::Fragment) {
                    ctx.use.frontColorIn = true;
                    rep = "iris_FrontColorIn";
                } else if (id == "gl_Color") {
                    ctx.vin.color = true;
                    ctx.use.colorMod = true;
                    rep = "(iris_Color * iris_ColorModulator)";
                } else if (id == "gl_Normal") {
                    ctx.vin.normal = true;
                    rep = "iris_Normal";
                } else if (id == "gl_MultiTexCoord0") {
                    ctx.vin.uv0 = true;
                    rep = "vec4(iris_UV0, 0.0, 1.0)";
                } else if (id == "gl_MultiTexCoord1") {

                    ctx.vin.uv2 = true;
                    rep = "vec4(iris_UV2, 0.0, 1.0)";
                } else if (id == "gl_MultiTexCoord2") {
                    ctx.vin.uv2 = true;
                    rep = "vec4(iris_UV2, 0.0, 1.0)";
                } else if (id.starts_with("gl_MultiTexCoord")) {
                    rep = "vec4(0.0, 0.0, 0.0, 1.0)";
                } else if (id == "gl_ModelViewMatrix") {
                    ctx.use.modelView = true;
                    rep = "iris_ModelViewMat";
                } else if (id == "gl_ModelViewMatrixInverse") {
                    ctx.use.modelViewInv = true;
                    rep = "iris_ModelViewMatInverse";
                } else if (id == "gl_ProjectionMatrix") {
                    ctx.use.proj = true;
                    rep = "iris_ProjMat";
                } else if (id == "gl_ProjectionMatrixInverse") {
                    ctx.use.projInv = true;
                    rep = "iris_ProjMatInverse";
                } else if (id == "gl_ModelViewProjectionMatrix") {
                    ctx.use.modelView = ctx.use.proj = true;
                    rep = "(iris_ProjMat * iris_ModelViewMat)";
                } else if (id == "gl_NormalMatrix") {
                    ctx.use.normalMat = true;
                    rep = "iris_NormalMat";
                } else if (id == "gl_TextureMatrix") {
                    ctx.use.textureMat = true;
                    rep = "iris_TextureMat";
                } else if (id == "ftransform" && nextCall) {
                    ctx.use.modelView = ctx.use.proj = true;
                    ctx.vin.position = true;
                    rep = "(iris_ProjMat * iris_ModelViewMat * vec4(iris_Position, 1.0))";
                    if (i + 2 < tokens.size() && tokens[i + 2].text == ")") {
                        out[i] = tk.ws + rep;
                        out[i + 1].clear();
                        out[i + 2].clear();
                        tokens[i + 1].text.clear();
                        tokens[i + 2].text.clear();
                        i += 2;
                        continue;
                    }
                } else if (id == "gl_Fog" && i + 2 < tokens.size() && tokens[i + 1].text == ".") {
                    const std::string field = tokens[i + 2].text;
                    if (field == "color") {
                        ctx.use.fogColor = true;
                        rep = "iris_FogColor";
                    } else if (field == "density") {
                        ctx.use.fogDensity = true;
                        rep = "iris_FogDensity";
                    } else if (field == "start") {
                        ctx.use.fogStart = true;
                        rep = "iris_FogStart";
                    } else if (field == "end") {
                        ctx.use.fogEnd = true;
                        rep = "iris_FogEnd";
                    } else if (field == "scale") {
                        ctx.use.fogScale = true;
                        rep = "iris_FogScale";
                    } else
                        rep = "0.0";
                    out[i] = tk.ws + rep;
                    out[i + 1].clear();
                    out[i + 2].clear();
                    tokens[i + 1].text.clear();
                    tokens[i + 2].text.clear();
                    i += 2;
                    continue;
                } else if (id == "gl_FrontColor" && stage == Stage::Vertex) {
                    rep = "iris_FrontColorSink";
                } else if (id == "gl_FogFragCoord") {
                    ctx.use.fogCoord = true;
                    rep = "iris_FogFragCoord";
                } else if (id == "gl_FragCoord") {
                    ctx.use.fragCoord = true;
                    rep = options.flipFragCoordY ? "iris_FragCoord" : "gl_FragCoord";
                } else if (id == "gl_FragData" && i + 3 < tokens.size() && tokens[i + 1].text == "[" &&
                           tokens[i + 3].text == "]") {
                    const int idx = std::atoi(tokens[i + 2].text.c_str());
                    ctx.use.fragData = std::max(ctx.use.fragData, idx + 1);
                    out[i] = tk.ws + "iris_FragData" + Slot(idx);
                    out[i + 1].clear();
                    out[i + 2].clear();
                    out[i + 3].clear();
                    tokens[i + 1].text.clear();
                    tokens[i + 2].text.clear();
                    tokens[i + 3].text.clear();
                    i += 3;
                    continue;
                } else if (id == "gl_FragColor") {
                    ctx.use.fragColor = true;
                    ctx.use.fragData = std::max(ctx.use.fragData, 1);
                    rep = "iris_FragData0";
                } else if (id == "gl_VertexID") {
                    rep = "gl_VertexIndex";
                } else if (id == "gl_InstanceID") {
                    rep = "gl_InstanceIndex";
                } else if (id == "shadow2D" && nextCall) {
                    ctx.use.shadow2D = true;
                    rep = "iris_shadow2D";
                } else if (id == "shadow2DLod" && nextCall) {
                    ctx.use.shadow2DLod = true;
                    rep = "iris_shadow2DLod";
                } else if (id == "shadow2DProj" && nextCall) {
                    ctx.use.shadow2DProj = true;
                    rep = "iris_shadow2DProj";
                } else if (options.explicitLod && stage == Stage::Fragment && nextCall &&
                           !ctx.userFunctions.contains(id) &&
                           (id == "texture2D" || (id == "texture" && !userDefinesTexture)) &&
                           lodArgs(i) == 2) {

                    rep = "textureLod";
                    lodClose.insert(closeOf(i + 1));
                } else if (TextureFunctionMap().contains(id) && nextCall && !ctx.userFunctions.contains(id)) {
                    rep = TextureFunctionMap().at(id);
                } else if (id == "texture" && !nextCall && !userDefinesTexture) {
                    rep = "iris_texture";
                } else if (NewKeywords().contains(id) && !nextCall) {
                    if (id == "smooth") {
                        const bool qualifierUse =
                            i + 1 < tokens.size() && tokens[i + 1].kind == Tok::Ident &&
                            (IsType(tokens[i + 1].text) || IsQualifier(tokens[i + 1].text));
                        rep = qualifierUse ? id : "iris_kw_" + id;
                    } else if (id == "sample" && i + 1 < tokens.size() && tokens[i + 1].kind == Tok::Ident &&
                               IsType(tokens[i + 1].text)) {
                        rep = id;
                    } else if ((id == "buffer" || id == "shared" || id == "coherent" || id == "restrict" ||
                                id == "readonly" || id == "writeonly" || id == "patch") &&
                               i + 1 < tokens.size() && tokens[i + 1].kind == Tok::Ident) {

                        rep = id;
                    } else {
                        rep = "iris_kw_" + id;
                    }
                } else {
                    rep = id;
                }
                out[i] = tk.ws + rep;
            }
            for (size_t k : lodClose)
                out[k] = ", 0.0" + out[k];

            std::string bodyText;
            std::map<std::string, int> arrayConsts;
            std::set<std::string> seenUniforms, seenSamplers;
            bool hasMain = false;
            std::string uniformBlockPlaceholder = "/*IRIS_UNIFORM_BLOCK*/";
            bool blockPlaced = false;
            std::vector<std::string> vertexAttribLines;
            std::set<std::string> userAttribNames;

            auto emitRange = [&](size_t a, size_t b) {
                std::string s;
                for (size_t k = a; k < b; k++)
                    s += out[k];
                return s;
            };

            for (const Statement& st : statements) {
                if (st.function) {
                    size_t p = st.begin;
                    while (p < st.end && tokens[p].text != "(")
                        p++;
                    std::string text = emitRange(st.begin, st.end);
                    if (p > st.begin && tokens[p - 1].text == "main") {
                        hasMain = true;
                        size_t at = text.find("main");
                        if (at != std::string::npos)
                            text.replace(at, 4, "iris_user_main");
                    }
                    bodyText += text;
                    continue;
                }
                if (tokens[st.begin].kind == Tok::Directive) {
                    bodyText += out[st.begin];
                    continue;
                }
                if (tokens[st.begin].text == "struct" && !st.function) {

                    bodyText += "/*STRUCT_BEGIN*/" + emitRange(st.begin, st.end) + "/*STRUCT_END*/";
                    continue;
                }
                const DeclInfo d = ParseDeclaration(tokens, st.begin, st.end);
                if (!d.valid) {
                    bodyText += emitRange(st.begin, st.end);
                    continue;
                }
                const bool isUniform = HasQualifier(d, "uniform");
                const bool isVarying = HasQualifier(d, "varying");
                const bool isAttribute = HasQualifier(d, "attribute");
                const bool isIn = HasQualifier(d, "in");
                const bool isOut = HasQualifier(d, "out");
                const bool isConst = HasQualifier(d, "const");

                if (isConst && !isUniform && d.type == "int") {
                    for (const auto& n : d.names) {
                        int v = 0;
                        if (n.arraySize.empty() && ParseIntLiteral(StripSpace(n.init), v))
                            arrayConsts[n.name] = v;
                    }
                }

                if (isUniform) {
                    const std::string typeName = d.type == "texture" ? "iris_texture" : d.type;
                    if (IsOpaqueType(d.type)) {
                        for (const auto& n : d.names) {
                            std::string name = n.name;
                            if (name == "texture")
                                name = "iris_texture";
                            if (!seenSamplers.insert(name).second)
                                continue;
                            SamplerDecl s;
                            s.name = name;
                            s.type = d.type;
                            s.image = d.type.starts_with("image") || d.type.starts_with("iimage") ||
                                      d.type.starts_with("uimage");
                            s.imageFormat = LayoutWithout(d.layout, "binding");

                            for (const char* q :
                                 {"readonly", "writeonly", "coherent", "volatile", "restrict"})
                                if (HasQualifier(d, q))
                                    s.qualifiers += std::string(q) + " ";
                            ctx.samplers.push_back(s);
                        }
                        bodyText += "/*SAMPLERS*/";
                        continue;
                    }
                    for (const auto& n : d.names) {
                        if (!seenUniforms.insert(n.name).second)
                            continue;
                        UniformDecl u;
                        u.name = n.name;
                        u.type = typeName;
                        u.arraySize = n.arraySize;
                        u.arrayCount = n.arraySize.empty() ? 0 : EvalSize(n.arraySize, arrayConsts);
                        if (!n.arraySize.empty() && u.arrayCount < 0) {
                            result.ok = false;
                            result.error =
                                "uniform array size is not a constant: " + n.name + "[" + n.arraySize + "]";
                        }
                        ctx.uniforms.push_back(u);
                    }
                    if (!blockPlaced) {
                        blockPlaced = true;
                        bodyText += uniformBlockPlaceholder;
                    }
                    continue;
                }

                bool toFragmentOut = false;
                const bool vertexStage = stage == Stage::Vertex;
                if (isAttribute || (vertexStage && isIn)) {
                    std::string layoutRest = d.layout;
                    int loc = -1;
                    layoutRest = LayoutWithout(d.layout, "location", &loc);
                    for (const auto& n : d.names) {
                        if (IsIrisVertexAttribute(n.name))
                            continue;
                        InterfaceVar v;
                        v.name = n.name;
                        v.type = d.type;
                        v.arrayCount = n.arraySize.empty() ? 0 : EvalSize(n.arraySize, arrayConsts);
                        v.location = loc;
                        ctx.inputs.push_back(v);
                        userAttribNames.insert(n.name);
                        if (n.name == "mc_Entity")
                            ctx.vin.mcEntity = true;
                        else if (n.name == "mc_midTexCoord")
                            ctx.vin.mcMidTexCoord = true;
                        else if (n.name == "at_tangent")
                            ctx.vin.atTangent = true;
                        else if (n.name == "at_midBlock")
                            ctx.vin.atMidBlock = true;
                    }
                    bodyText += "/*ATTR*/";
                    continue;
                }
                if (isVarying) {
                    toFragmentOut = false;
                }
                const bool outputDecl = isOut || (isVarying && vertexStage);
                const bool inputDecl = isIn || (isVarying && !vertexStage);
                if (outputDecl || inputDecl) {
                    int loc = -1;
                    LayoutWithout(d.layout, "location", &loc);
                    std::string interp;
                    for (const std::string& q : d.qualifiers)
                        if (q == "flat" || q == "centroid" || q == "noperspective" || q == "smooth")
                            interp = q == "smooth" ? "" : q;
                    for (const auto& n : d.names) {
                        InterfaceVar v;
                        v.name = n.name;
                        v.type = d.type;
                        v.arrayCount = n.arraySize.empty() ? 0 : EvalSize(n.arraySize, arrayConsts);
                        v.interpolation = interp;
                        v.location = loc;
                        if (stage == Stage::Fragment && outputDecl)
                            ctx.outputs.push_back(v);
                        else if (outputDecl)
                            ctx.outputs.push_back(v);
                        else
                            ctx.inputs.push_back(v);
                    }
                    bodyText += outputDecl ? (stage == Stage::Fragment ? "/*FRAGOUT*/" : "/*VARYING_OUT*/")
                                           : "/*VARYING_IN*/";
                    continue;
                }
                (void)toFragmentOut;
                bodyText += emitRange(st.begin, st.end);
            }
            if (!hasMain) {
                result.ok = false;
                result.error = "no main()";
            }

            result.uniforms = ctx.uniforms;
            result.samplers = ctx.samplers;
            result.inputs = ctx.inputs;
            result.outputs = ctx.outputs;
            result.vertexInputs = ctx.vin;
            result.glsl = bodyText;

            auto addBuiltin = [&](bool used, const char* name) {
                if (!used)
                    return;
                for (const UniformDecl& b : BuiltinUniformDecls())
                    if (b.name == name) {
                        for (const UniformDecl& u : result.uniforms)
                            if (u.name == name)
                                return;
                        result.uniforms.push_back(b);
                    }
            };
            addBuiltin(ctx.use.modelView, "iris_ModelViewMat");
            addBuiltin(ctx.use.modelViewInv, "iris_ModelViewMatInverse");
            addBuiltin(ctx.use.proj, "iris_ProjMat");
            addBuiltin(ctx.use.projInv, "iris_ProjMatInverse");
            addBuiltin(ctx.use.normalMat, "iris_NormalMat");
            addBuiltin(ctx.use.textureMat, "iris_TextureMat");
            addBuiltin(ctx.use.fogColor, "iris_FogColor");
            addBuiltin(ctx.use.fogDensity, "iris_FogDensity");
            addBuiltin(ctx.use.fogStart, "iris_FogStart");
            addBuiltin(ctx.use.fogEnd, "iris_FogEnd");
            addBuiltin(ctx.use.fogScale, "iris_FogScale");
            addBuiltin(ctx.use.colorMod, "iris_ColorModulator");
            addBuiltin(options.flipFragCoordY && ctx.use.fragCoord, "iris_ViewHeight");

            std::string flags;
            if (ctx.use.fogCoord)
                flags += "FOGCOORD;";
            if (ctx.use.frontColorIn)
                flags += "FRONTCOLOR;";
            if (ctx.use.fragCoord)
                flags += "FRAGCOORD;";
            if (ctx.use.shadow2D)
                flags += "SHADOW2D;";
            if (ctx.use.shadow2DLod)
                flags += "SHADOW2DLOD;";
            if (ctx.use.shadow2DProj)
                flags += "SHADOW2DPROJ;";
            if (ctx.use.fragData)
                flags += "FRAGDATA=" + std::to_string(ctx.use.fragData) + ";";
            result.glsl = "/*FLAGS:" + flags + "*/" + result.glsl;
            return result;
        }

        bool HasFlag(const std::string& glsl, const std::string& flag) {
            const size_t end = glsl.find("*/");
            if (end == std::string::npos)
                return false;
            return glsl.substr(0, end).contains(flag);
        }

        int FlagNumber(const std::string& glsl, const std::string& flag) {
            const size_t end = glsl.find("*/");
            const size_t at = glsl.find(flag + "=");
            if (end == std::string::npos || at == std::string::npos || at > end)
                return 0;
            return std::atoi(glsl.c_str() + at + flag.size() + 1);
        }

        std::string StripFlags(const std::string& glsl) {
            if (glsl.compare(0, 9, "/*FLAGS:") != 0)
                return glsl;
            const size_t end = glsl.find("*/");
            return end == std::string::npos ? glsl : glsl.substr(end + 2);
        }

    }

    namespace {
        bool DeclaresGlobal(std::string_view line, std::string_view name) {
            const auto blank = [](char c) { return c == ' ' || c == '\t'; };
            size_t i = 0;
            while (i < line.size() && blank(line[i]))
                i++;
            const auto token = [&](size_t from) {
                size_t end = from;
                while (end < line.size() && !blank(line[end]))
                    end++;
                return line.substr(from, end - from);
            };
            const auto isType = [](std::string_view t) {
                if (t == "float" || t == "int" || t == "uint" || t == "bool")
                    return true;
                if (t.size() == 5 && (t[0] == 'i' || t[0] == 'u' || t[0] == 'b') && t.substr(1, 3) == "vec")
                    return t[4] >= '2' && t[4] <= '4';
                if (t.size() == 4 && (t.starts_with("vec") || t.starts_with("mat")))
                    return t[3] >= '2' && t[3] <= '4';
                return false;
            };
            std::string_view type = token(i);
            if (type == "const" && i + 5 < line.size() && blank(line[i + 5])) {
                size_t j = i + 5;
                while (j < line.size() && blank(line[j]))
                    j++;
                if (isType(token(j))) {
                    i = j;
                    type = token(j);
                }
            }
            if (!isType(type))
                return false;
            i += type.size();
            if (i >= line.size() || !blank(line[i]))
                return false;
            while (i < line.size() && blank(line[i]))
                i++;
            if (line.substr(i, name.size()) != name)
                return false;
            i += name.size();
            while (i < line.size() && blank(line[i]))
                i++;
            return i < line.size() && (line[i] == '=' || line[i] == ';');
        }

        bool RedeclaredAsGlobal(const std::string& glsl, const std::string& name) {
            if (name.empty() || glsl.find(name) == std::string::npos)
                return false;
            int depth = 0;
            size_t pos = 0;
            const std::string_view text(glsl);
            while (pos < text.size()) {
                size_t end = text.find('\n', pos);
                if (end == std::string::npos)
                    end = text.size();
                const std::string_view line = text.substr(pos, end - pos);
                if (depth == 0 && DeclaresGlobal(line, name))
                    return true;
                for (char c : line) {
                    if (c == '{')
                        depth++;
                    else if (c == '}' && depth > 0)
                        depth--;
                }
                pos = end + 1;
            }
            return false;
        }
    }

    namespace {

        std::string AssignBufferSets(const std::string& glsl) {
            static const std::regex re(
                R"(layout\s*\(([^)]*)\)((?:\s+(?:readonly|writeonly|coherent|volatile|restrict))*)\s+buffer\b)");
            const auto mayMatch = [&] {
                const auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
                for (size_t at = glsl.find("buffer"); at != std::string::npos; at = glsl.find("buffer", at + 1))
                    if (at > 0 && std::isspace(static_cast<unsigned char>(glsl[at - 1])) &&
                        (at + 6 >= glsl.size() || !word(glsl[at + 6])))
                        return true;
                return false;
            };
            if (!mayMatch())
                return glsl;
            std::string out;
            size_t last = 0;
            for (auto it = std::sregex_iterator(glsl.begin(), glsl.end(), re); it != std::sregex_iterator();
                 ++it) {
                const std::smatch& m = *it;
                std::string layout = m[1].str();
                if (!layout.contains("set"))
                    layout = "set = 3, " + layout;
                out += glsl.substr(last, static_cast<size_t>(m.position(0)) - last);
                out += "layout(" + layout + ")" + m[2].str() + " buffer";
                last = static_cast<size_t>(m.position(0) + m.length(0));
            }
            out += glsl.substr(last);
            return out;
        }

        std::string SharedPrelude(StageResult& r, const TransformOptions& options,
                                  const std::map<std::string, int>& samplerBindings) {
            std::string glsl = StripFlags(r.glsl);
            std::string block;
            if (!r.uniforms.empty()) {
                std::string globals;
                int reg = 0;
                block = "layout(std140, set = 0, binding = 0) uniform IrisGlobals {\n";
                for (UniformDecl& u : r.uniforms) {
                    const std::string member = "iris_u_" + u.name;
                    const int count = std::max(1, u.arrayCount);
                    const bool matrix = u.type.starts_with("mat");
                    int comps = 1;
                    if (u.type.size() > 3 && (u.type.starts_with("vec")))
                        comps = u.type[3] - '0';
                    else if (u.type.size() > 4 && (u.type.starts_with("ivec") || u.type.starts_with("uvec") ||
                                                   u.type.starts_with("bvec")))
                        comps = u.type[4] - '0';
                    u.reg = reg;
                    u.regsPerElement = matrix ? 4 : 1;
                    u.regs = u.regsPerElement * count;
                    u.components = matrix ? (u.type == "mat2" ? 4 : u.type == "mat3" ? 9 : 16) : comps;
                    reg += u.regs;
                    block += std::string("    ") + (matrix ? "mat4 " : "vec4 ") + member;
                    if (count > 1 || u.arrayCount > 0)
                        block += "[" + std::to_string(count) + "]";
                    block += ";\n";
                    auto unpack = [&](const std::string& ref) -> std::string {
                        const std::string& t = u.type;
                        if (t == "float")
                            return ref + ".x";
                        if (t == "int")
                            return "int(" + ref + ".x)";
                        if (t == "uint")
                            return "uint(" + ref + ".x)";
                        if (t == "bool")
                            return "(" + ref + ".x != 0.0)";
                        if (t == "vec2")
                            return ref + ".xy";
                        if (t == "vec3")
                            return ref + ".xyz";
                        if (t == "vec4" || t == "mat4")
                            return ref;
                        if (t == "mat3" || t == "mat2")
                            return t + "(" + ref + ")";
                        if (t == "ivec2" || t == "ivec3" || t == "ivec4" || t == "uvec2" || t == "uvec3" ||
                            t == "uvec4")
                            return t + "(" + ref +
                                   (t.back() == '2'   ? ".xy"
                                    : t.back() == '3' ? ".xyz"
                                                      : "") +
                                   ")";
                        if (t == "bvec2" || t == "bvec3" || t == "bvec4")
                            return "notEqual(" + ref +
                                   (t.back() == '2'   ? ".xy"
                                    : t.back() == '3' ? ".xyz"
                                                      : "") +
                                   ", vec" + t.back() + "(0.0))";
                        return ref;
                    };
                    if (u.arrayCount > 0) {
                        globals += u.type + " " + u.name + "[" + std::to_string(count) + "] = " + u.type +
                                   "[" + std::to_string(count) + "](";
                        for (int i = 0; i < count; i++) {
                            if (i)
                                globals += ", ";
                            globals += unpack(member + "[" + std::to_string(i) + "]");
                        }
                        globals += ");\n";
                    } else if (!RedeclaredAsGlobal(glsl, u.name)) {
                        globals += u.type + " " + u.name + " = " + unpack(member) + ";\n";
                    }
                }
                block += "};\n" + globals;
            }
            glsl = str::ReplaceFirst(glsl, "/*IRIS_UNIFORM_BLOCK*/", "");
            for (SamplerDecl& smp : r.samplers)
                smp.binding = samplerBindings.at(smp.name);
            std::string samplerText;
            for (const SamplerDecl& s : r.samplers) {
                if (options.splitSamplers && !s.image) {

                    std::string texture = s.type;
                    const bool shadow =
                        texture.size() > 6 && texture.compare(texture.size() - 6, 6, "Shadow") == 0;
                    if (shadow)
                        texture.erase(texture.size() - 6);
                    const size_t at = texture.find("sampler");
                    if (at == std::string::npos) {
                        r.ok = false;
                        r.error = "unsupported sampler type '" + s.type + "'";
                        continue;
                    }
                    texture.replace(at, 7, "texture");
                    samplerText += "layout(set = 1, binding = " + std::to_string(s.binding * 2) +
                                   ") uniform " + texture + " " + s.name + "_iris_t;\n";
                    samplerText += "layout(set = 1, binding = " + std::to_string(s.binding * 2 + 1) +
                                   ") uniform " + (shadow ? "samplerShadow " : "sampler ") + s.name +
                                   "_iris_s;\n";
                    samplerText += "#define " + s.name + " " + s.type + "(" + s.name + "_iris_t, " + s.name +
                                   "_iris_s)\n";
                    continue;
                }
                if (s.image) {

                    const auto slot = options.imageSlots.find(s.name);
                    if (slot == options.imageSlots.end()) {
                        r.ok = false;
                        r.error = "image '" + s.name + "' is not declared by the pack (image." + s.name + ")";
                        continue;
                    }
                    samplerText += "layout(set = 2, binding = " + std::to_string(slot->second);
                    std::string format = s.imageFormat;
                    if (format.empty())
                        if (const auto f = options.imageFormats.find(s.name); f != options.imageFormats.end())
                            format = f->second;
                    if (!format.empty())
                        samplerText += ", " + format;
                    samplerText += ") uniform " + s.qualifiers + s.type + " " + s.name + ";\n";
                    continue;
                }
                samplerText += "layout(set = 0, binding = " + std::to_string(s.binding);
                samplerText += ") uniform " + s.type + " " + s.name + ";\n";
            }
            glsl = str::ReplaceAll(glsl, "/*SAMPLERS*/", "");
            glsl = AssignBufferSets(glsl);

            if (options.splitSamplers) {
                bool any = false;
                for (const SamplerDecl& smp : r.samplers) {
                    if (smp.image || (!smp.type.starts_with("usampler") && !smp.type.starts_with("isampler")))
                        continue;
                    const std::string dim = smp.type.substr(8);
                    if (dim != "1D" && dim != "2D" && dim != "3D")
                        continue;
                    const std::string t = smp.name + "_iris_t, " + smp.name + "_iris_s, ";
                    if (!glsl.contains(smp.name))
                        continue;
                    const std::regex call("\\b(texture|textureLod)\\s*\\(\\s*" + smp.name + "\\s*,");
                    std::string replaced;
                    size_t last = 0;
                    for (auto it = std::sregex_iterator(glsl.begin(), glsl.end(), call);
                         it != std::sregex_iterator(); ++it) {
                        const std::smatch& m = *it;
                        replaced += glsl.substr(last, static_cast<size_t>(m.position(0)) - last);
                        replaced += (m[1].str() == "texture" ? "iris_inearest_" : "iris_inearestlod_") + dim +
                                    "(" + smp.type + ", " + t;
                        last = static_cast<size_t>(m.position(0) + m.length(0));
                        any = true;
                    }
                    replaced += glsl.substr(last);
                    glsl = std::move(replaced);
                }
                if (any)
                    samplerText +=
                        R"(#define iris_inearest_1D(T, t, s, c) texelFetch(T(t, s), clamp(int(floor((c) * float(textureSize(T(t, s), 0)))), 0, textureSize(T(t, s), 0) - 1), 0)
#define iris_inearest_2D(T, t, s, c) texelFetch(T(t, s), clamp(ivec2(floor((c) * vec2(textureSize(T(t, s), 0)))), ivec2(0), textureSize(T(t, s), 0) - 1), 0)
#define iris_inearest_3D(T, t, s, c) texelFetch(T(t, s), clamp(ivec3(floor((c) * vec3(textureSize(T(t, s), 0)))), ivec3(0), textureSize(T(t, s), 0) - 1), 0)
#define iris_inearestlod_1D(T, t, s, c, l) texelFetch(T(t, s), clamp(int(floor((c) * float(textureSize(T(t, s), int(l))))), 0, textureSize(T(t, s), int(l)) - 1), int(l))
#define iris_inearestlod_2D(T, t, s, c, l) texelFetch(T(t, s), clamp(ivec2(floor((c) * vec2(textureSize(T(t, s), int(l))))), ivec2(0), textureSize(T(t, s), int(l)) - 1), int(l))
#define iris_inearestlod_3D(T, t, s, c, l) texelFetch(T(t, s), clamp(ivec3(floor((c) * vec3(textureSize(T(t, s), int(l))))), ivec3(0), textureSize(T(t, s), int(l)) - 1), int(l))
)";
            }
            return block + samplerText + "\x01" + glsl;
        }
    }

    namespace {
        std::string RepackBlockComponents(const std::string& text) {
            if (!text.contains("component"))
                return text;
            static const std::regex kBlock(R"(\b(in|out)(\s+)(\w+)\s*\{)");
            static const std::regex kMemberLayout(R"(layout\s*\(([^)]*)\)\s*)");
            static const std::regex kLocation(R"(location\s*=\s*(\d+))");
            std::string out;
            size_t pos = 0;
            for (auto it = std::sregex_iterator(text.begin(), text.end(), kBlock); it != std::sregex_iterator(); ++it) {
                const size_t start = static_cast<size_t>(it->position(0));
                if (start < pos)
                    continue;
                const size_t open = start + static_cast<size_t>(it->length(0)) - 1;
                const size_t close = text.find('}', open);
                if (close == std::string::npos)
                    break;
                const std::string body = text.substr(open + 1, close - open - 1);
                if (!body.contains("component"))
                    continue;
                int lowest = -1;
                std::string cleaned;
                size_t at = 0;
                for (auto m = std::sregex_iterator(body.begin(), body.end(), kMemberLayout); m != std::sregex_iterator(); ++m) {
                    const std::string args = (*m)[1].str();
                    std::smatch loc;
                    if (!std::regex_search(args, loc, kLocation))
                        continue;
                    const int l = std::atoi(loc[1].str().c_str());
                    lowest = lowest < 0 ? l : std::min(lowest, l);
                    cleaned += body.substr(at, static_cast<size_t>(m->position(0)) - at);
                    at = static_cast<size_t>(m->position(0) + m->length(0));
                }
                cleaned += body.substr(at);
                if (lowest < 0)
                    continue;
                size_t lineStart = text.rfind('\n', start);
                lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
                const std::string before = text.substr(lineStart, start - lineStart);
                out += text.substr(pos, start - pos);
                if (!before.contains("layout"))
                    out += "layout(location = " + std::to_string(lowest) + ") ";
                out += text.substr(start, open + 1 - start) + cleaned + "}";
                pos = close + 1;
            }
            if (pos == 0)
                return text;
            out += text.substr(pos);
            return out;
        }
    }

    ProgramResult TransformProgram(const ExpandedSource& vertexSource, const ExpandedSource& fragmentSource,
                                   const TransformOptions& options) {
        ProgramResult program;
        static const char* kDefaultVertex =
            "#version 120\nvarying vec4 color;\nvarying vec2 texcoord;\nvoid main(){ gl_Position = "
            "ftransform(); texcoord = gl_MultiTexCoord0.st; color = gl_Color; }\n";
        static const char* kDefaultFragment =
            "#version 120\nuniform sampler2D iris_texture_default;\nvarying vec4 color;\nvarying vec2 "
            "texcoord;\nvoid main(){ gl_FragData[0] = texture2D(iris_texture_default, texcoord) * color; }\n";
        std::string vs = vertexSource.text.empty() ? kDefaultVertex : vertexSource.text;
        {
            static const std::regex instanceUniform(R"(uniform\s+(?:highp\s+|mediump\s+|lowp\s+)?int\s+instanceId\s*;)");
            if (vs.contains("instanceId"))
                vs = std::regex_replace(vs, instanceUniform, "#define instanceId gl_InstanceID\n");
        }
        const std::string fs = fragmentSource.text.empty() ? kDefaultFragment : fragmentSource.text;
        program.vertex = TransformStage(Stage::Vertex, vs, options);
        program.fragment = TransformStage(Stage::Fragment, fs, options);
        if (!program.ok())
            return program;

        {
            std::vector<UniformDecl> merged = program.vertex.uniforms;
            for (const UniformDecl& u : program.fragment.uniforms) {
                bool found = false;
                for (const UniformDecl& m : merged)
                    if (m.name == u.name) {
                        found = true;
                        if (m.type != u.type || m.arrayCount != u.arrayCount) {
                            program.fragment.ok = false;
                            program.fragment.error =
                                "uniform '" + u.name + "' is declared with different types in the two stages";
                        }
                    }
                if (!found)
                    merged.push_back(u);
            }
            program.uniforms = std::move(merged);
            program.vertex.uniforms = program.uniforms;
            program.fragment.uniforms = program.uniforms;
            if (!program.ok())
                return program;
        }

        std::map<std::string, int> samplerBindings;
        {
            std::set<int> used;
            for (const StageResult* st : {&program.vertex, &program.fragment})
                for (const SamplerDecl& smp : st->samplers) {
                    const auto fixed = options.fixedSamplerSlots.find(smp.name);
                    if (fixed != options.fixedSamplerSlots.end()) {
                        samplerBindings[smp.name] = fixed->second;
                        used.insert(fixed->second);
                    }
                }
            int cursor = 0;
            for (const StageResult* st : {&program.vertex, &program.fragment})
                for (const SamplerDecl& smp : st->samplers) {
                    if (samplerBindings.contains(smp.name))
                        continue;
                    while (used.contains(cursor))
                        cursor++;
                    samplerBindings[smp.name] = cursor;
                    used.insert(cursor);
                }
        }

        struct Link {
            std::string type, interp;
            int arrayCount = 0;
            int location = -1;
            bool inVertexOut = false, inFragmentIn = false;
        };
        std::map<std::string, Link> links;
        bool fogCoord =
            HasFlag(program.vertex.glsl, "FOGCOORD") || HasFlag(program.fragment.glsl, "FOGCOORD");
        for (const InterfaceVar& v : program.vertex.outputs) {
            Link& l = links[v.name];
            l.type = v.type;
            l.interp = v.interpolation;
            l.arrayCount = v.arrayCount;
            l.inVertexOut = true;
            if (v.location >= 0)
                l.location = v.location;
        }
        for (const InterfaceVar& v : program.fragment.inputs) {
            Link& l = links[v.name];
            if (l.type.empty()) {
                l.type = v.type;
                l.arrayCount = v.arrayCount;
            }
            if (l.interp.empty())
                l.interp = v.interpolation;
            l.inFragmentIn = true;
            if (v.location >= 0 && l.location < 0)
                l.location = v.location;
        }
        const bool frontColor = HasFlag(program.fragment.glsl, "FRONTCOLOR");
        if (frontColor) {
            Link& l = links["iris_FrontColorIn"];
            l.type = "vec4";
            l.inVertexOut = l.inFragmentIn = true;
        }
        if (fogCoord) {
            Link& l = links["iris_FogFragCoord"];
            l.type = "float";
            l.inVertexOut = l.inFragmentIn = true;
        }
        const std::string allGlsl = program.vertex.glsl + "\n" + program.fragment.glsl;
        const auto piecesOf = [&](const Link& l) {
            if (l.arrayCount > 0 || IsPlainVaryingType(l.type))
                return std::vector<VaryingPiece>{};
            return FlattenVarying(allGlsl, l.type);
        };
        const auto slotsOf = [&](const Link& l) {
            if (const auto pieces = piecesOf(l); !pieces.empty())
                return static_cast<int>(pieces.size());
            int per = TypeSlots(l.type);
            if (per == 1)
                if (const int members = StructSlots(allGlsl, l.type); members > 0)
                    per = members;
            return per * std::max(1, l.arrayCount);
        };
        std::set<int> taken;
        for (const auto& kv : links)
            if (kv.second.location >= 0) {
                const int slots = slotsOf(kv.second);
                for (int s = 0; s < slots; s++)
                    taken.insert(kv.second.location + s);
            }
        int next = 0;
        for (auto& kv : links) {
            Link& l = kv.second;
            if (l.location >= 0)
                continue;
            const int slots = slotsOf(l);
            for (;;) {
                bool free = true;
                for (int s = 0; s < slots; s++)
                    if (taken.contains(next + s)) {
                        free = false;
                        break;
                    }
                if (free)
                    break;
                next++;
            }
            l.location = next;
            for (int s = 0; s < slots; s++)
                taken.insert(next + s);
            next += slots;
        }

        auto varyingDecl = [&](const std::string& name, const Link& l, bool out,
                               std::string* copy = nullptr, bool zero = false) {
            if (const auto pieces = piecesOf(l); !pieces.empty()) {
                std::string s = l.type + " " + name + ";\n";
                for (size_t i = 0; i < pieces.size(); i++) {
                    const std::string piece = name + "__" + pieces[i].suffix;
                    s += "layout(location = " + std::to_string(l.location + static_cast<int>(i)) + ") ";
                    if (!l.interp.empty())
                        s += l.interp + " ";
                    s += std::string(out ? "out " : "in ") + pieces[i].type + " " + piece + ";\n";
                    if (copy) {
                        const bool real = pieces[i].type == "float" || pieces[i].type.starts_with("vec");
                        const std::string zeroValue = real ? pieces[i].type + "(0.0)" : pieces[i].type + "(0)";
                        if (out)
                            *copy += "    " + piece + " = " +
                                     (zero ? zeroValue : name + pieces[i].access) + ";\n";
                        else
                            *copy += "    " + name + pieces[i].access + " = " + piece + ";\n";
                    }
                }
                return s;
            }
            if (l.arrayCount > 0 && l.type.starts_with("mat") && l.type.size() >= 4) {
                const int cols = l.type[3] - '0';
                const int rows = l.type.size() == 6 && l.type[4] == 'x' ? l.type[5] - '0' : cols;
                const std::string colType = "vec" + std::to_string(rows);
                std::string s = l.type + " " + name + "[" + std::to_string(l.arrayCount) + "];\n";
                for (int i = 0; i < l.arrayCount; i++)
                    for (int j = 0; j < cols; j++) {
                        const std::string piece = name + "__m" + std::to_string(i) + "_" + std::to_string(j);
                        const std::string access = "[" + std::to_string(i) + "][" + std::to_string(j) + "]";
                        s += "layout(location = " + std::to_string(l.location + i * cols + j) + ") ";
                        if (!l.interp.empty())
                            s += l.interp + " ";
                        s += std::string(out ? "out " : "in ") + colType + " " + piece + ";\n";
                        if (copy) {
                            if (out)
                                *copy += "    " + piece + " = " + (zero ? colType + "(0.0)" : name + access) + ";\n";
                            else
                                *copy += "    " + name + access + " = " + piece + ";\n";
                        }
                    }
                return s;
            }
            std::string s = "layout(location = " + std::to_string(l.location) + ") ";
            if (!l.interp.empty())
                s += l.interp + " ";
            s += out ? "out " : "in ";
            s += l.type + " " + name;
            if (l.arrayCount > 0)
                s += "[" + std::to_string(l.arrayCount) + "]";
            return s + ";\n";
        };

        const auto sharedPrelude = [&](StageResult& r, bool) {
            return SharedPrelude(r, options, samplerBindings);
        };

        {
            StageResult& r = program.vertex;
            std::string head = "#version 450\n";
            std::string text = sharedPrelude(r, true);
            const size_t sep = text.find('\x01');
            std::string samplerText = text.substr(0, sep), body = text.substr(sep + 1);
            std::string attrs;
            const VertexInputs& vi = r.vertexInputs;
            auto have = [&](int loc) { return (options.availableAttribs >> loc) & 1u; };
            auto emitAttr = [&](int loc, const std::string& type, const std::string& name,
                                const std::string& fallback) {
                if (have(loc))
                    attrs += "layout(location = " + std::to_string(loc) + ") in " + type + " " + name + ";\n";
                else
                    attrs += type + " " + name + " = " + fallback + ";\n";
            };
            if (vi.position)
                emitAttr(kAttribPosition, "vec3", "iris_Position", "vec3(0.0)");
            if (vi.color)
                emitAttr(kAttribColor0, "vec4", "iris_Color", "vec4(1.0)");
            if (vi.uv0)
                emitAttr(kAttribTexCoord0, "vec2", "iris_UV0", "vec2(0.0)");
            if (vi.uv1)
                emitAttr(kAttribTexCoord1, "vec2", "iris_UV1", "vec2(0.0)");
            if (vi.normal)
                emitAttr(kAttribNormal, "vec3", "iris_Normal", "vec3(0.0, 1.0, 0.0)");
            if (vi.uv2)
                emitAttr(kAttribTexCoord5, "vec2", "iris_UV2", "vec2(240.0)");
            int userAttrib = 0;
            for (const InterfaceVar& v : r.inputs) {
                int loc;
                if (v.name == "mc_Entity")
                    loc = kAttribTexCoord2;
                else if (v.name == "mc_midTexCoord")
                    loc = kAttribTexCoord3;
                else if (v.name == "at_tangent")
                    loc = kAttribTangent;
                else if (v.name == "at_midBlock")
                    loc = kAttribTexCoord4;
                else
                    loc = kAttribTexCoord6 + userAttrib++;
                if (loc > kAttribTexCoord15) {
                    r.ok = false;
                    r.error = "too many vertex attributes";
                }
                std::string fallback = v.type + "(0.0)";
                if (v.name == "mc_Entity")
                    fallback = v.type == "float" ? "-1.0" : v.type + "(-1.0)";
                else if (v.name == "at_tangent")
                    fallback = v.type == "vec4" ? "vec4(1.0, 0.0, 0.0, 1.0)" : fallback;
                if (!v.type.starts_with("vec") && v.type != "float")
                    fallback = v.type + "(0)";
                emitAttr(loc, v.type, v.name, fallback);
            }
            body = str::ReplaceAll(body, "/*ATTR*/", "");
            std::string varyings, flatCopy;
            for (const InterfaceVar& v : r.outputs) {
                const Link& l = links[v.name];
                varyings += varyingDecl(v.name, l, true, &flatCopy);
            }
            if (fogCoord)
                varyings += varyingDecl("iris_FogFragCoord", links["iris_FogFragCoord"], true);
            if (frontColor)
                varyings += varyingDecl("iris_FrontColorIn", links["iris_FrontColorIn"], true);

            std::string zeroFill;
            for (const auto& [linkName, l] : links) {
                if (!l.inFragmentIn || l.inVertexOut)
                    continue;
                varyings += varyingDecl(linkName, l, true, &flatCopy, true);
                if (!piecesOf(l).empty())
                    continue;
                const std::string zero =
                    l.type.starts_with("mat") || l.type.starts_with("vec") || l.type == "float"
                        ? l.type + "(0.0)"
                        : l.type + "(0)";
                if (l.arrayCount > 0)
                    zeroFill += "    for (int i = 0; i < " + std::to_string(l.arrayCount) + "; i++) " +
                                linkName + "[i] = " + zero + ";\n";
                else
                    zeroFill += "    " + linkName + " = " + zero + ";\n";
            }
            body = str::ReplaceAll(body, "/*VARYING_OUT*/", "");
            std::string prelude;
            prelude += "#define PERLIN_VULKAN_GLSL 1\n";
            if (HasFlag(r.glsl, "SHADOW2D"))
                prelude += "#define iris_shadow2D(s, c) vec4(textureLod(s, c, 0.0))\n";
            if (HasFlag(r.glsl, "SHADOW2DLOD"))
                prelude += "#define iris_shadow2DLod(s, c, l) vec4(textureLod(s, c, l))\n";
            if (HasFlag(r.glsl, "SHADOW2DPROJ"))
                prelude += "#define iris_shadow2DProj(s, c) vec4(textureProj(s, c))\n";

            if (body.contains("iris_FrontColorSink"))
                prelude += "vec4 iris_FrontColorSink;\n";
            if (body.contains("mc_chunkFade") && !attrs.contains("mc_chunkFade"))
                prelude += "const float mc_chunkFade = 1.0;\n";
            std::string footer = "\nvoid main() {\n" + zeroFill + "    iris_user_main();\n" + flatCopy;
            if (frontColor)
                footer += std::string("    iris_FrontColorIn = ") +
                          (body.contains("iris_FrontColorSink") ? "iris_FrontColorSink" : "vec4(1.0)") + ";\n";
            if (options.flipVertexY)
                footer += "    gl_Position.y = -gl_Position.y;\n";
            if (options.zeroToOneDepth)
                footer += "    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;\n";
            footer += "}\n";
            const std::string structs = HoistStructs(body);
            r.glsl = head + structs + attrs + varyings + samplerText + prelude + body + footer;
        }
        {
            StageResult& r = program.fragment;
            const std::string flagsSource = r.glsl;
            std::string head = "#version 450\n";
            std::string text = sharedPrelude(r, false);
            const size_t sep = text.find('\x01');
            std::string samplerText = text.substr(0, sep), body = text.substr(sep + 1);
            std::string varyings, flatLoad;
            for (const InterfaceVar& v : r.inputs) {
                const Link& l = links[v.name];
                varyings += varyingDecl(v.name, l, false, &flatLoad);
            }
            if (fogCoord)
                varyings += varyingDecl("iris_FogFragCoord", links["iris_FogFragCoord"], false);
            if (frontColor)
                varyings += varyingDecl("iris_FrontColorIn", links["iris_FrontColorIn"], false);
            body = str::ReplaceAll(body, "/*VARYING_IN*/", "");
            std::string outs;
            std::set<int> usedLoc;
            for (const InterfaceVar& v : r.outputs)
                if (v.location >= 0)
                    usedLoc.insert(v.location);
            int loc = 0;
            std::vector<std::pair<int, InterfaceVar>> placed;
            for (const InterfaceVar& v : r.outputs) {
                int l = v.location;
                if (l < 0) {
                    while (usedLoc.contains(loc))
                        loc++;
                    l = loc;
                    usedLoc.insert(l);
                }
                placed.emplace_back(l, v);
            }
            const int fragData = FlagNumber(flagsSource, "FRAGDATA");
            for (int i = 0; i < fragData; i++) {
                if (usedLoc.contains(i) && !HasFlag(flagsSource, "NEVER")) {
                    bool user = false;
                    for (auto& p : placed)
                        if (p.first == i)
                            user = true;
                    if (user)
                        continue;
                }
                outs += "layout(location = " + std::to_string(i) + ") out vec4 iris_FragData" +
                        std::to_string(i) + ";\n";
                usedLoc.insert(i);
            }
            for (auto& p : placed) {
                outs += "layout(location = " + std::to_string(p.first) + ") out " + p.second.type + " " +
                        p.second.name;
                if (p.second.arrayCount > 0)
                    outs += "[" + std::to_string(p.second.arrayCount) + "]";
                outs += ";\n";
                r.drawBuffers.push_back(p.first);
                if (!UsesWord(body, p.second.name))
                    r.silentOutputs.push_back(p.first);
            }
            for (int i = 0; i < fragData; i++) {
                r.drawBuffers.push_back(i);
                if (!usedLoc.contains(i) || std::ranges::any_of(placed, [&](const auto& p) { return p.first == i; }))
                    continue;
                if (!UsesWord(body, "iris_FragData" + std::to_string(i)) && !body.contains("iris_FragData["))
                    r.silentOutputs.push_back(i);
            }
            std::ranges::sort(r.drawBuffers);
            r.drawBuffers.erase(std::unique(r.drawBuffers.begin(), r.drawBuffers.end()), r.drawBuffers.end());
            body = str::ReplaceAll(body, "/*FRAGOUT*/", "");

            std::string prelude;
            if (HasFlag(flagsSource, "SHADOW2D"))
                prelude += options.explicitLod ? "#define iris_shadow2D(s, c) vec4(textureLod(s, c, 0.0))\n"
                                               : "#define iris_shadow2D(s, c) vec4(texture(s, c))\n";
            if (HasFlag(flagsSource, "SHADOW2DLOD"))
                prelude += "#define iris_shadow2DLod(s, c, l) vec4(textureLod(s, c, l))\n";
            if (HasFlag(flagsSource, "SHADOW2DPROJ"))
                prelude += "#define iris_shadow2DProj(s, c) vec4(textureProj(s, c))\n";
            std::string fragCoord;
            std::string mainPrefix;
            if (options.flipFragCoordY && HasFlag(flagsSource, "FRAGCOORD")) {
                fragCoord = "vec4 iris_FragCoord;\n";
                mainPrefix = "    iris_FragCoord = vec4(gl_FragCoord.x, iris_ViewHeight - gl_FragCoord.y, "
                             "gl_FragCoord.z, gl_FragCoord.w);\n";
            }

            std::string alphaTest;
            if (!options.alphaTest.empty() && outs.contains(" out vec4 iris_FragData0;"))
                alphaTest = "    if (!(iris_FragData0.a " + options.alphaTest + ")) discard;\n";
            std::string footer =
                "\nvoid main() {\n" + mainPrefix + flatLoad + "    iris_user_main();\n" + alphaTest + "}\n";
            const std::string structs = HoistStructs(body);
            r.glsl = head + structs + varyings + outs + samplerText + prelude + fragCoord + body + footer;
            if (!mainPrefix.empty() && !r.glsl.contains("IrisGlobals")) {
                r.ok = false;
                r.error = "internal: iris_ViewHeight requested without uniform block";
            }
        }
        program.vertex.glsl = RepackBlockComponents(program.vertex.glsl);
        program.fragment.glsl = RepackBlockComponents(program.fragment.glsl);
        program.uniforms = program.vertex.uniforms;
        program.uniformRegs = 0;
        for (const UniformDecl& u : program.uniforms)
            program.uniformRegs += u.regs;
        return program;
    }

    namespace {
        std::string ClampSubscripts(const std::string& text, const std::map<std::string, int>& arrays,
                                    size_t skipFrom, size_t skipTo, int depth) {
            if (depth > 8)
                return text;
            const auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
            std::string out;
            size_t i = 0;
            while (i < text.size()) {
                if (!word(text[i]) || (i > 0 && word(text[i - 1]))) {
                    out += text[i++];
                    continue;
                }
                size_t e = i;
                while (e < text.size() && word(text[e]))
                    e++;
                const std::string name = text.substr(i, e - i);
                const auto it = arrays.find(name);
                out += name;
                if (it == arrays.end() || (i >= skipFrom && i < skipTo) || (i > 0 && text[i - 1] == '.')) {
                    i = e;
                    continue;
                }
                size_t at = e;
                for (int dim = 0; dim < it->second; dim++) {
                    size_t open = at;
                    while (open < text.size() && std::isspace(static_cast<unsigned char>(text[open])))
                        open++;
                    if (open >= text.size() || text[open] != '[')
                        break;
                    int level = 0;
                    size_t close = open;
                    for (; close < text.size(); close++) {
                        if (text[close] == '[')
                            level++;
                        else if (text[close] == ']' && --level == 0)
                            break;
                    }
                    if (close >= text.size())
                        break;
                    const std::string inner = ClampSubscripts(text.substr(open + 1, close - open - 1), arrays,
                                                              std::string::npos, std::string::npos, depth + 1);
                    std::string len = name;
                    for (int k = 0; k < dim; k++)
                        len += "[0]";
                    out += text.substr(at, open - at) + "[clamp(int(" + inner + "), 0, " + len + ".length() - 1)]";
                    at = close + 1;
                }
                i = at;
            }
            return out;
        }

        std::string ClampSharedIndices(const std::string& body) {
            if (!body.contains("shared"))
                return body;
            static const std::regex decl(R"(\bshared\s+(?:(?:highp|mediump|lowp|precise)\s+)*\w+\s+(\w+)\s*((?:\[[^\]\[;]*\]\s*)+);)");
            std::map<std::string, int> arrays;
            std::vector<std::pair<size_t, size_t>> decls;
            for (auto it = std::sregex_iterator(body.begin(), body.end(), decl); it != std::sregex_iterator(); ++it) {
                const std::string dims = (*it)[2].str();
                arrays[(*it)[1].str()] = static_cast<int>(std::ranges::count(dims, '['));
                decls.emplace_back(static_cast<size_t>(it->position(0)),
                                   static_cast<size_t>(it->position(0) + it->length(0)));
            }
            if (arrays.empty())
                return body;
            std::string out;
            size_t last = 0;
            for (const auto& [from, to] : decls) {
                out += ClampSubscripts(body.substr(last, from - last), arrays, std::string::npos, std::string::npos, 0);
                out += body.substr(from, to - from);
                last = to;
            }
            out += ClampSubscripts(body.substr(last), arrays, std::string::npos, std::string::npos, 0);
            return out;
        }
    }

    ComputeResult TransformCompute(const ExpandedSource& source, const TransformOptions& options) {
        ComputeResult out;
        out.stage = TransformStage(Stage::Compute, source.text, options);
        StageResult& r = out.stage;
        if (!r.ok)
            return out;

        const std::string& text = r.glsl;
        std::smatch m;
        static const std::regex local(R"(local_size_([xyz])\s*=\s*([0-9]+))");
        for (auto it = std::sregex_iterator(text.begin(), text.end(), local); it != std::sregex_iterator();
             ++it) {
            const int axis = (*it)[1].str()[0] - 'x';
            out.localSize[axis] = static_cast<uint32_t>(std::max(1, std::atoi((*it)[2].str().c_str())));
        }
        static const std::regex groups(
            R"(const\s+ivec3\s+workGroups\s*=\s*ivec3\s*\(\s*([0-9]+)\s*,\s*([0-9]+)\s*,\s*([0-9]+)\s*\))");
        static const std::regex render(
            R"(const\s+vec2\s+workGroupsRender\s*=\s*vec2\s*\(\s*([0-9.eE+-]+)\s*,\s*([0-9.eE+-]+)\s*\))");
        if (std::regex_search(text, m, groups)) {
            out.fixedGroups = true;
            for (int a = 0; a < 3; a++)
                out.workGroups[a] = static_cast<uint32_t>(std::atoi(m[a + 1].str().c_str()));
        } else if (std::regex_search(text, m, render)) {
            out.workGroupsRender[0] = static_cast<float>(std::atof(m[1].str().c_str()));
            out.workGroupsRender[1] = static_cast<float>(std::atof(m[2].str().c_str()));
        }
        std::map<std::string, int> bindings;
        {
            std::set<int> used;
            for (const SamplerDecl& smp : r.samplers)
                if (const auto fixed = options.fixedSamplerSlots.find(smp.name);
                    fixed != options.fixedSamplerSlots.end()) {
                    bindings[smp.name] = fixed->second;
                    used.insert(fixed->second);
                }
            int cursor = 0;
            for (const SamplerDecl& smp : r.samplers) {
                if (bindings.contains(smp.name))
                    continue;
                while (used.contains(cursor))
                    cursor++;
                bindings[smp.name] = cursor;
                used.insert(cursor);
            }
        }
        const std::string prelude = SharedPrelude(r, options, bindings);
        for (const UniformDecl& u : r.uniforms)
            out.uniformRegs = std::max(out.uniformRegs, u.reg + u.regs);
        const size_t sep = prelude.find('\x01');
        std::string samplerText = prelude.substr(0, sep), body = ClampSharedIndices(prelude.substr(sep + 1));
        const std::string structs = HoistStructs(body);
        std::string shadowPrelude;
        if (HasFlag(text, "SHADOW2D"))
            shadowPrelude += "#define iris_shadow2D(s, c) vec4(textureLod(s, c, 0.0))\n";
        if (HasFlag(text, "SHADOW2DLOD"))
            shadowPrelude += "#define iris_shadow2DLod(s, c, l) vec4(textureLod(s, c, l))\n";
        if (HasFlag(text, "SHADOW2DPROJ"))
            shadowPrelude += "#define iris_shadow2DProj(s, c) vec4(textureProj(s, c))\n";
        r.glsl = "#version 450\n#define PERLIN_VULKAN_GLSL 1\n" + structs + samplerText + shadowPrelude + body +
                 "\nvoid main() {\n    iris_user_main();\n}\n";
        return out;
    }
}
