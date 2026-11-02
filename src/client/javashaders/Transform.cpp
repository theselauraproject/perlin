#include "client/javashaders/Transform.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace perlin::jshader {
    namespace {
        struct Tok {
            enum Kind { Ident, Number, Punct, Directive } kind = Punct;
            std::string text;
            std::string ws;
        };

        bool IsIdStart(char c) {
            return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
        }
        bool IsIdChar(char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        std::vector<Tok> Lex(const std::string& s) {
            std::vector<Tok> out;
            size_t i = 0;
            std::string ws;
            bool lineStart = true;
            while (i < s.size()) {
                const char c = s[i];
                if (c == '\n') {
                    ws += c;
                    i++;
                    lineStart = true;
                    continue;
                }
                if (c == ' ' || c == '\t' || c == '\r') {
                    ws += c;
                    i++;
                    continue;
                }
                if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
                    while (i < s.size() && s[i] != '\n')
                        i++;
                    ws += ' ';
                    continue;
                }
                if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
                    size_t end = s.find("*/", i + 2);
                    const size_t stop = end == std::string::npos ? s.size() : end + 2;
                    for (size_t k = i; k < stop; k++)
                        if (s[k] == '\n')
                            ws += '\n';
                    ws += ' ';
                    i = stop;
                    continue;
                }
                if (c == '#' && lineStart) {
                    size_t e = i;
                    while (e < s.size() && s[e] != '\n') {
                        if (s[e] == '\\' && e + 1 < s.size() && (s[e + 1] == '\n' || s[e + 1] == '\r')) {
                            e += 2;
                            if (e < s.size() && s[e - 1] == '\r' && s[e] == '\n')
                                e++;
                            continue;
                        }
                        e++;
                    }
                    out.push_back({Tok::Directive, s.substr(i, e - i), ws});
                    ws.clear();
                    i = e;
                    continue;
                }
                lineStart = false;
                if (IsIdStart(c)) {
                    size_t e = i;
                    while (e < s.size() && IsIdChar(s[e]))
                        e++;
                    out.push_back({Tok::Ident, s.substr(i, e - i), ws});
                    ws.clear();
                    i = e;
                    continue;
                }
                if (std::isdigit(static_cast<unsigned char>(c)) ||
                    (c == '.' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
                    size_t e = i;
                    if (c == '0' && e + 1 < s.size() && (s[e + 1] == 'x' || s[e + 1] == 'X')) {
                        e += 2;
                        while (e < s.size() && std::isxdigit(static_cast<unsigned char>(s[e])))
                            e++;
                    } else {
                        while (e < s.size() &&
                               (std::isdigit(static_cast<unsigned char>(s[e])) || s[e] == '.'))
                            e++;
                        if (e < s.size() && (s[e] == 'e' || s[e] == 'E')) {
                            size_t k = e + 1;
                            if (k < s.size() && (s[k] == '+' || s[k] == '-'))
                                k++;
                            if (k < s.size() && std::isdigit(static_cast<unsigned char>(s[k]))) {
                                e = k;
                                while (e < s.size() && std::isdigit(static_cast<unsigned char>(s[e])))
                                    e++;
                            }
                        }
                    }
                    while (e < s.size() && (s[e] == 'f' || s[e] == 'F' || s[e] == 'u' || s[e] == 'U' ||
                                            s[e] == 'l' || s[e] == 'L'))
                        e++;
                    out.push_back({Tok::Number, s.substr(i, e - i), ws});
                    ws.clear();
                    i = e;
                    continue;
                }
                out.push_back({Tok::Punct, std::string(1, c), ws});
                ws.clear();
                i++;
            }
            if (!ws.empty())
                out.push_back({Tok::Punct, "", ws});
            return out;
        }

        bool IsOpaqueType(const std::string& t) {
            return t.compare(0, 7, "sampler") == 0 || t.compare(0, 8, "isampler") == 0 ||
                   t.compare(0, 8, "usampler") == 0 || t.compare(0, 5, "image") == 0 ||
                   t.compare(0, 6, "iimage") == 0 || t.compare(0, 6, "uimage") == 0 || t == "atomic_uint";
        }

        const std::unordered_set<std::string>& BuiltinTypes() {
            static const std::unordered_set<std::string> set = {
                "void",   "bool",   "int",    "uint",   "float",  "double", "vec2",   "vec3",   "vec4",
                "ivec2",  "ivec3",  "ivec4",  "uvec2",  "uvec3",  "uvec4",  "bvec2",  "bvec3",  "bvec4",
                "mat2",   "mat3",   "mat4",   "mat2x2", "mat2x3", "mat2x4", "mat3x2", "mat3x3", "mat3x4",
                "mat4x2", "mat4x3", "mat4x4", "dvec2",  "dvec3",  "dvec4"};
            return set;
        }

        bool IsType(const std::string& t) {
            return BuiltinTypes().contains(t) || IsOpaqueType(t);
        }

        bool IsQualifier(const std::string& t) {
            static const std::unordered_set<std::string> set = {
                "const",    "uniform",   "varying",       "attribute", "in",        "out",      "inout",
                "flat",     "smooth",    "noperspective", "centroid",  "invariant", "precise",  "highp",
                "mediump",  "lowp",      "buffer",        "shared",    "coherent",  "volatile", "restrict",
                "readonly", "writeonly", "sample",        "patch"};
            return set.contains(t);
        }

        std::string Join(const std::vector<Tok>& t, size_t a, size_t b) {
            std::string out;
            for (size_t i = a; i < b && i < t.size(); i++) {
                out += t[i].ws;
                out += t[i].text;
            }
            return out;
        }

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

        struct BuiltinUse {
            bool modelView = false, modelViewInv = false, proj = false, projInv = false, normalMat = false;
            bool textureMat = false, fogColor = false, fogDensity = false, fogStart = false, fogEnd = false,
                 fogScale = false;
            bool fogCoord = false, fragCoord = false, shadow2D = false, shadow2DLod = false,
                 shadow2DProj = false;
            bool vertexIndex = false;
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
                // GL_ARB_shader_texture_lod / GL_EXT_gpu_shader4 spellings
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

        // a pack's local variable, parameter or struct member can share a name with one of its samplers (`float specular`
        // next to `uniform sampler2D specular`). the sampler names are macros here, so the other uses get renamed first
        void RenameShadowedSamplers(std::vector<Tok>& t) {
            std::set<std::string> names;
            std::set<size_t> declTokens;
            for (size_t i = 0; i < t.size(); i++) {
                if (t[i].kind != Tok::Ident || t[i].text != "uniform")
                    continue;
                bool opaque = false;
                for (size_t k = i + 1; k < t.size() && t[k].text != ";"; k++) {
                    if (t[k].kind != Tok::Ident)
                        continue;
                    if (IsOpaqueType(t[k].text)) {
                        opaque = true;
                        continue;
                    }
                    if (opaque && !IsQualifier(t[k].text) && !IsType(t[k].text)) {
                        names.insert(t[k].text);
                        declTokens.insert(k);
                    }
                }
            }
            if (names.empty())
                return;
            std::set<std::string> structs;
            for (size_t i = 0; i + 1 < t.size(); i++)
                if (t[i].text == "struct" && t[i + 1].kind == Tok::Ident)
                    structs.insert(t[i + 1].text);
            const auto typeLike = [&](size_t k) {
                return t[k].kind == Tok::Ident && !IsOpaqueType(t[k].text) &&
                       (IsType(t[k].text) || structs.contains(t[k].text));
            };
            const auto rename = [&](const std::string& n) { return "iris_local_" + n; };
            // members are always reached through '.', and a sampler never has any
            for (size_t i = 1; i < t.size(); i++)
                if (t[i].kind == Tok::Ident && names.contains(t[i].text) && t[i - 1].text == ".")
                    t[i].text = rename(t[i].text);

            struct Scope {
                size_t begin, end;
                std::string name;
            };
            std::vector<Scope> scopes;
            size_t headerStart = 0;
            int paren = 0;
            size_t i = 0;
            while (i < t.size()) {
                const std::string& x = t[i].text;
                if (t[i].kind == Tok::Directive) {
                    if (paren == 0)
                        headerStart = i + 1;
                    i++;
                    continue;
                }
                if (x == "(")
                    paren++;
                else if (x == ")")
                    paren--;
                if (x == "{" && paren == 0) {
                    int depth = 0;
                    size_t k = i;
                    for (; k < t.size(); k++) {
                        if (t[k].text == "{")
                            depth++;
                        else if (t[k].text == "}" && --depth == 0)
                            break;
                    }
                    const size_t end = std::min(k + 1, t.size());
                    for (size_t j = headerStart; j < end; j++) {
                        if (t[j].kind != Tok::Ident || !names.contains(t[j].text) || declTokens.contains(j) ||
                            t[j].text.compare(0, 11, "iris_local_") == 0)
                            continue;
                        bool declares = j > 0 && typeLike(j - 1);
                        if (!declares && j > 0 && t[j - 1].text == ",") {
                            // `float a, specular;` : the statement starts with a type
                            size_t b = j;
                            int d = 0;
                            while (b > headerStart) {
                                const std::string& y = t[b - 1].text;
                                if (y == ")" || y == "]")
                                    d++;
                                else if (y == "(" || y == "[") {
                                    if (d == 0)
                                        break;
                                    d--;
                                } else if (d == 0 && (y == ";" || y == "{" || y == "}"))
                                    break;
                                b--;
                            }
                            while (b < j && t[b].kind == Tok::Ident && IsQualifier(t[b].text))
                                b++;
                            declares = b < j && typeLike(b);
                        }
                        if (declares)
                            scopes.push_back({headerStart, end, t[j].text});
                    }
                    headerStart = end;
                    i = end;
                    continue;
                }
                if (x == ";" && paren == 0)
                    headerStart = i + 1;
                i++;
            }
            for (const Scope& sc : scopes)
                for (size_t j = sc.begin; j < sc.end; j++)
                    if (t[j].kind == Tok::Ident && t[j].text == sc.name && !declTokens.contains(j))
                        t[j].text = rename(sc.name);
            // declarations outside any block (globals, function names): rename everywhere but the uniform declaration
            for (size_t j = 1; j < t.size(); j++) {
                if (t[j].kind != Tok::Ident || !names.contains(t[j].text) || declTokens.contains(j) ||
                    !typeLike(j - 1))
                    continue;
                int depth = 0;
                for (size_t k = 0; k < j; k++) {
                    if (t[k].text == "{")
                        depth++;
                    else if (t[k].text == "}")
                        depth--;
                }
                if (depth != 0)
                    continue;
                int open = 0;
                for (size_t k = j; k-- > 0 && t[k].text != ";" && t[k].text != "}" && t[k].text != "{";) {
                    if (t[k].text == ")")
                        open--;
                    else if (t[k].text == "(")
                        open++;
                }
                if (open > 0)
                    continue; // a parameter: its scope is the function, handled above
                const std::string n = t[j].text;
                for (size_t k = 0; k < t.size(); k++)
                    if (t[k].kind == Tok::Ident && t[k].text == n && !declTokens.contains(k))
                        t[k].text = rename(n);
            }
        }

        // glslang refuses a sampler built from a texture and a sampler as a function argument, so user functions that take a
        // sampler get the two halves as separate parameters and their callers pass both
        void SplitSamplerParams(std::vector<Tok>& t) {
            std::set<std::string> globals;
            for (size_t i = 0; i < t.size(); i++) {
                if (t[i].kind != Tok::Ident || t[i].text != "uniform")
                    continue;
                bool opaque = false;
                for (size_t k = i + 1; k < t.size() && t[k].text != ";"; k++) {
                    if (t[k].kind != Tok::Ident)
                        continue;
                    if (IsOpaqueType(t[k].text) && t[k].text.compare(0, 5, "image") != 0)
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
                std::vector<int> params; // indexes of the sampler parameters
            };
            std::map<std::string, Fn> fns;
            std::set<std::string> paramNames;
            struct Def {
                size_t open, close, bodyEnd;
                std::vector<std::pair<size_t, size_t>> params; // type token, name token
            };
            std::vector<Def> defs;
            int brace = 0;
            for (size_t i = 0; i + 1 < t.size(); i++) {
                if (t[i].text == "{")
                    brace++;
                else if (t[i].text == "}")
                    brace--;
                if (brace != 0 || t[i].kind != Tok::Ident || t[i + 1].text != "(" || i == 0 ||
                    !(IsType(t[i - 1].text) || t[i - 1].text == "void"))
                    continue;
                // a function declaration or definition at file scope
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
            // callers first: the argument at a sampler position becomes the texture and sampler pair
            for (size_t i = 0; i + 1 < t.size(); i++) {
                if (t[i].kind != Tok::Ident || t[i + 1].text != "(")
                    continue;
                const auto it = fns.find(t[i].text);
                if (it == fns.end() || (i > 0 && (IsType(t[i - 1].text) || t[i - 1].text == "void")))
                    continue;
                int depth = 0, index = 0;
                size_t argStart = i + 2;
                for (size_t k = i + 1; k < t.size(); k++) {
                    const bool last = t[k].text == ")" && depth == 1;
                    if (t[k].text == "(" || t[k].text == "[")
                        depth++;
                    else if ((t[k].text == ")" || t[k].text == "]") && --depth == 0) {
                        if (std::find(it->second.params.begin(), it->second.params.end(), index) !=
                                it->second.params.end() &&
                            k == argStart + 1) {
                            Tok& a = t[argStart];
                            if (a.kind == Tok::Ident &&
                                (globals.contains(a.text) || paramNames.contains(a.text)))
                                a.text = a.text + "_iris_t, " + a.text + "_iris_s";
                        }
                        break;
                    }
                    if (depth == 1 && (t[k].text == "," || last)) {
                        if (std::find(it->second.params.begin(), it->second.params.end(), index) !=
                                it->second.params.end() &&
                            k == argStart + 1) {
                            Tok& a = t[argStart];
                            if (a.kind == Tok::Ident &&
                                (globals.contains(a.text) || paramNames.contains(a.text)))
                                a.text = a.text + "_iris_t, " + a.text + "_iris_s";
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
                    // uses inside the body (not the ones the callers pass on, those were already split)
                    for (size_t k = d.close; k < d.bodyEnd; k++)
                        if (t[k].kind == Tok::Ident && t[k].text == name)
                            t[k].text = type + "(" + name + "_iris_t, " + name + "_iris_s)";
                    t[pr.first].text = texture;
                    t[pr.second].text = name + "_iris_t, " +
                                        std::string(shadow ? "samplerShadow " : "sampler ") + name +
                                        "_iris_s";
                }
            }
        }

        // moves the struct definitions the body collected to the front
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
            while (i < end) {
                if (t[i].kind != Tok::Ident)
                    return d;
                DeclInfo::Name n;
                n.name = t[i].text;
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
            return std::find(d.qualifiers.begin(), d.qualifiers.end(), q) != d.qualifiers.end();
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
            const std::string pre = options.preprocess(BuildPrefix(options) + body, stage, error);
            if (pre.empty() && !error.empty()) {
                result.ok = false;
                result.error = "preprocess: " + error;
                return result;
            }

            std::vector<Tok> tokens = Lex(pre);
            for (Tok& tk : tokens) {
                if (tk.kind == Tok::Directive) {
                    std::string d = tk.text;
                    size_t a = d.find_first_not_of("# \t");
                    const std::string name = a == std::string::npos ? std::string() : d.substr(a, 8);
                    if (name.compare(0, 7, "version") == 0 || name.compare(0, 9, "extension") == 0 ||
                        name.compare(0, 4, "line") == 0 || name.compare(0, 6, "pragma") == 0) {
                        tk.text.clear();
                        tk.kind = Tok::Punct;
                    }
                }
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
                    (IsType(tokens[i - 1].text) || IsQualifier(tokens[i - 1].text)))
                    declaredNames.insert(tokens[i].text);

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
                } else if (id == "gl_Color") {
                    ctx.vin.color = true;
                    rep = "iris_Color";
                } else if (id == "gl_Normal") {
                    ctx.vin.normal = true;
                    rep = "iris_Normal";
                } else if (id == "gl_MultiTexCoord0") {
                    ctx.vin.uv0 = true;
                    rep = "vec4(iris_UV0, 0.0, 1.0)";
                } else if (id == "gl_MultiTexCoord1") {
                    // texture unit 1 is the lightmap in OptiFine/Iris
                    ctx.vin.uv2 = true;
                    rep = "vec4(iris_UV2, 0.0, 1.0)";
                } else if (id == "gl_MultiTexCoord2") {
                    ctx.vin.uv2 = true;
                    rep = "vec4(iris_UV2, 0.0, 1.0)";
                } else if (id.compare(0, 16, "gl_MultiTexCoord") == 0) {
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
                    // full screen passes read buffers without mips: a fixed level avoids implicit derivatives inside loops
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
                        // a qualifier (readonly buffer Name { ... }, shared float x[64]), not a variable name
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
                    // varyings can be struct typed, so the definitions have to come before the interface declarations
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
                            s.image = d.type.compare(0, 5, "image") == 0 ||
                                      d.type.compare(0, 6, "iimage") == 0 ||
                                      d.type.compare(0, 6, "uimage") == 0;
                            s.imageFormat = LayoutWithout(d.layout, "binding");
                            // memory qualifiers stay with the image (readonly / writeonly / coherent ...)
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
            addBuiltin(options.flipFragCoordY && ctx.use.fragCoord, "iris_ViewHeight");

            std::string flags;
            if (ctx.use.fogCoord)
                flags += "FOGCOORD;";
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

        std::string ReplaceFirst(std::string text, const std::string& from, const std::string& to) {
            const size_t at = text.find(from);
            if (at != std::string::npos)
                text.replace(at, from.size(), to);
            return text;
        }

        std::string ReplaceAll(std::string text, const std::string& from, const std::string& to) {
            size_t at = 0;
            while ((at = text.find(from, at)) != std::string::npos) {
                text.replace(at, from.size(), to);
                at += to.size();
            }
            return text;
        }
    }

    namespace {
        // shader storage blocks (iris bufferObject.<n>) bind at set 3, binding n; the pack only writes binding = n
        std::string AssignBufferSets(const std::string& glsl) {
            static const std::regex re(
                R"(layout\s*\(([^)]*)\)((?:\s+(?:readonly|writeonly|coherent|volatile|restrict))*)\s+buffer\b)");
            if (glsl.find("buffer") == std::string::npos)
                return glsl;
            std::string out;
            size_t last = 0;
            for (auto it = std::sregex_iterator(glsl.begin(), glsl.end(), re); it != std::sregex_iterator();
                 ++it) {
                const std::smatch& m = *it;
                std::string layout = m[1].str();
                if (layout.find("set") == std::string::npos)
                    layout = "set = 3, " + layout;
                out += glsl.substr(last, static_cast<size_t>(m.position(0)) - last);
                out += "layout(" + layout + ")" + m[2].str() + " buffer";
                last = static_cast<size_t>(m.position(0) + m.length(0));
            }
            out += glsl.substr(last);
            return out;
        }

        // the uniform block, the sampler / image declarations and the stage body (split by \x01)
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
                    const bool matrix = u.type.compare(0, 3, "mat") == 0;
                    int comps = 1;
                    if (u.type.size() > 3 && (u.type.compare(0, 3, "vec") == 0))
                        comps = u.type[3] - '0';
                    else if (u.type.size() > 4 &&
                             (u.type.compare(0, 4, "ivec") == 0 || u.type.compare(0, 4, "uvec") == 0 ||
                              u.type.compare(0, 4, "bvec") == 0))
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
                    } else {
                        globals += u.type + " " + u.name + " = " + unpack(member) + ";\n";
                    }
                }
                block += "};\n" + globals;
            }
            glsl = ReplaceFirst(glsl, "/*IRIS_UNIFORM_BLOCK*/", "");
            for (SamplerDecl& smp : r.samplers)
                smp.binding = samplerBindings.at(smp.name);
            std::string samplerText;
            for (const SamplerDecl& s : r.samplers) {
                if (options.splitSamplers && !s.image) {
                    // sampler2D -> texture2D, isampler2D -> itexture2D, sampler2DShadow -> texture2D + samplerShadow
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
                    // custom images (iris image.<name>) live in set 2 at the slot the renderer gave them
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
            glsl = ReplaceAll(glsl, "/*SAMPLERS*/", "");
            glsl = AssignBufferSets(glsl);
            // texture() on an integer sampler returns the nearest texel in gl; hlsl below sm 6.7 cannot sample integer
            // textures at all, so the call becomes the texelFetch it amounts to (every backend, same result)
            if (options.splitSamplers) {
                bool any = false;
                for (const SamplerDecl& smp : r.samplers) {
                    if (smp.image ||
                        (smp.type.compare(0, 8, "usampler") != 0 && smp.type.compare(0, 8, "isampler") != 0))
                        continue;
                    const std::string dim = smp.type.substr(8); // 1D 2D 3D 2DArray
                    if (dim != "1D" && dim != "2D" && dim != "3D")
                        continue;
                    const std::string t = smp.name + "_iris_t, " + smp.name + "_iris_s, ";
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

    ProgramResult TransformProgram(const ExpandedSource& vertexSource, const ExpandedSource& fragmentSource,
                                   const TransformOptions& options) {
        ProgramResult program;
        static const char* kDefaultVertex =
            "#version 120\nvarying vec4 color;\nvarying vec2 texcoord;\nvoid main(){ gl_Position = "
            "ftransform(); texcoord = gl_MultiTexCoord0.st; color = gl_Color; }\n";
        static const char* kDefaultFragment =
            "#version 120\nuniform sampler2D iris_texture_default;\nvarying vec4 color;\nvarying vec2 "
            "texcoord;\nvoid main(){ gl_FragData[0] = texture2D(iris_texture_default, texcoord) * color; }\n";
        const std::string vs = vertexSource.text.empty() ? kDefaultVertex : vertexSource.text;
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
        if (fogCoord) {
            Link& l = links["iris_FogFragCoord"];
            l.type = "float";
            l.inVertexOut = l.inFragmentIn = true;
        }
        std::set<int> taken;
        for (const auto& kv : links)
            if (kv.second.location >= 0) {
                const int slots = TypeSlots(kv.second.type) * std::max(1, kv.second.arrayCount);
                for (int s = 0; s < slots; s++)
                    taken.insert(kv.second.location + s);
            }
        int next = 0;
        for (auto& kv : links) {
            Link& l = kv.second;
            if (l.location >= 0)
                continue;
            const int slots = TypeSlots(l.type) * std::max(1, l.arrayCount);
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

        auto varyingDecl = [&](const std::string& name, const Link& l, bool out) {
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
                if (v.type.compare(0, 3, "vec") != 0 && v.type != "float")
                    fallback = v.type + "(0)";
                emitAttr(loc, v.type, v.name, fallback);
            }
            body = ReplaceAll(body, "/*ATTR*/", "");
            std::string varyings;
            for (const InterfaceVar& v : r.outputs) {
                const Link& l = links[v.name];
                varyings += varyingDecl(v.name, l, true);
            }
            if (fogCoord)
                varyings += varyingDecl("iris_FogFragCoord", links["iris_FogFragCoord"], true);
            // a varying the fragment shader reads but the vertex shader never declares (packs hide some behind
            // ifdefs): metal and d3d refuse such a pipeline, so the vertex shader writes it as zero
            std::string zeroFill;
            for (const auto& [linkName, l] : links) {
                if (!l.inFragmentIn || l.inVertexOut)
                    continue;
                varyings += varyingDecl(linkName, l, true);
                const std::string zero =
                    l.type.compare(0, 3, "mat") == 0 || l.type.compare(0, 3, "vec") == 0 || l.type == "float"
                        ? l.type + "(0.0)"
                        : l.type + "(0)";
                if (l.arrayCount > 0)
                    zeroFill += "    for (int i = 0; i < " + std::to_string(l.arrayCount) + "; i++) " +
                                linkName + "[i] = " + zero + ";\n";
                else
                    zeroFill += "    " + linkName + " = " + zero + ";\n";
            }
            body = ReplaceAll(body, "/*VARYING_OUT*/", "");
            std::string prelude;
            prelude += "#define PERLIN_VULKAN_GLSL 1\n";
            // iris FADE_VARIABLE: sodium's chunk fade-in; perlin shows chunks whole, which is a fade of 1
            if (body.find("mc_chunkFade") != std::string::npos &&
                attrs.find("mc_chunkFade") == std::string::npos)
                prelude += "const float mc_chunkFade = 1.0;\n";
            std::string footer = "\nvoid main() {\n" + zeroFill + "    iris_user_main();\n";
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
            std::string varyings;
            for (const InterfaceVar& v : r.inputs) {
                const Link& l = links[v.name];
                varyings += varyingDecl(v.name, l, false);
            }
            if (fogCoord)
                varyings += varyingDecl("iris_FogFragCoord", links["iris_FogFragCoord"], false);
            body = ReplaceAll(body, "/*VARYING_IN*/", "");
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
            }
            for (int i = 0; i < fragData; i++)
                r.drawBuffers.push_back(i);
            std::sort(r.drawBuffers.begin(), r.drawBuffers.end());
            r.drawBuffers.erase(std::unique(r.drawBuffers.begin(), r.drawBuffers.end()), r.drawBuffers.end());
            body = ReplaceAll(body, "/*FRAGOUT*/", "");

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
            std::string footer = "\nvoid main() {\n" + mainPrefix + "    iris_user_main();\n}\n";
            const std::string structs = HoistStructs(body);
            r.glsl = head + structs + varyings + outs + samplerText + prelude + fragCoord + body + footer;
            if (!mainPrefix.empty() && !r.glsl.contains("IrisGlobals")) {
                r.ok = false;
                r.error = "internal: iris_ViewHeight requested without uniform block";
            }
        }
        program.uniforms = program.vertex.uniforms;
        program.uniformRegs = 0;
        for (const UniformDecl& u : program.uniforms)
            program.uniformRegs += u.regs;
        return program;
    }

    ComputeResult TransformCompute(const ExpandedSource& source, const TransformOptions& options) {
        ComputeResult out;
        out.stage = TransformStage(Stage::Compute, source.text, options);
        StageResult& r = out.stage;
        if (!r.ok)
            return out;
        // the dispatch size is read from the preprocessed body, before the prelude is added
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
        std::string samplerText = prelude.substr(0, sep), body = prelude.substr(sep + 1);
        const std::string structs = HoistStructs(body);
        r.glsl = "#version 450\n#define PERLIN_VULKAN_GLSL 1\n" + structs + samplerText + body +
                 "\nvoid main() {\n    iris_user_main();\n}\n";
        return out;
    }
}
