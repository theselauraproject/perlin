#include "client/systems/javashaders/GlslTokens.hpp"
#include "client/systems/javashaders/SamplerRename.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace perlin::jshader;
using glsl::IsOpaqueType;
using glsl::IsQualifier;
using glsl::IsType;
using glsl::Tok;

namespace {
    int failures = 0;

    void ReferenceRename(std::vector<Tok>& t) {
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
                        t[j].text.starts_with("iris_local_"))
                        continue;
                    bool declares = j > 0 && typeLike(j - 1);
                    if (!declares && j > 0 && t[j - 1].text == ",") {

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
                continue;
            const std::string n = t[j].text;
            for (size_t k = 0; k < t.size(); k++)
                if (t[k].kind == Tok::Ident && t[k].text == n && !declTokens.contains(k))
                    t[k].text = rename(n);
        }
    }

    std::string Render(const std::vector<Tok>& t) {
        return glsl::Join(t, 0, t.size());
    }

    class Gen {
    public:
        explicit Gen(unsigned seed) : rng(seed) {
            if (seed % 7 == 0)
                names[1] = "iris_local_shadowtex0";
            stray = seed % 11 == 0;
        }

        std::string Shader() {
            std::string s = pick(kVersions);
            const int samplers = 1 + roll(3);
            for (int i = 0; i < samplers; i++)
                s += "uniform " + std::string(pick(kQualifiers)) + pick(kSamplerTypes) + " " +
                     names[static_cast<size_t>(i)] +
                     (roll(5) == 0 ? ", " + names[static_cast<size_t>((i + 1) % 4)] : "") + ";\n";
            if (roll(3) == 0)
                s += "struct Light { vec3 dir; float " + names[static_cast<size_t>(roll(4))] + "; };\n";
            if (roll(4) == 0)
                s += "vec4 " + names[static_cast<size_t>(roll(4))] + " = vec4(0.0);\n";
            const int items = 2 + roll(6);
            for (int i = 0; i < items; i++)
                s += Item();
            if (stray)
                s += "}\n" + std::string(pick(kTypes)) + " " + Name() + ";\n";
            return s;
        }

    private:
        static constexpr const char* kVersions[3] = {"#version 120\n",
                                                     "#version 330 compatibility\n#define X 1\n", ""};
        static constexpr const char* kQualifiers[3] = {"", "highp ", "layout(binding = 2) "};
        static constexpr const char* kSamplerTypes[4] = {"sampler2D", "sampler2DShadow", "usampler2D",
                                                         "samplerCube"};
        static constexpr const char* kTypes[5] = {"float", "vec3", "vec4", "int", "Light"};
        std::vector<std::string> names = {"colortex0", "shadowtex0", "depthtex1", "noisetex"};
        bool stray = false;
        std::mt19937 rng;

        int roll(int n) { return static_cast<int>(rng() % static_cast<unsigned>(n)); }
        template <size_t N> const char* pick(const char* const (&a)[N]) {
            return a[static_cast<size_t>(roll(static_cast<int>(N)))];
        }
        const std::string& Name() { return names[static_cast<size_t>(roll(4))]; }

        std::string Expr() {
            switch (roll(6)) {
            case 0:
                return "texture2D(" + Name() + ", uv)";
            case 1:
                return Name() + ".x";
            case 2:
                return "light." + Name();
            case 3:
                return Name() + " * 2.0";
            case 4:
                return "vec3(" + Name() + ")";
            default:
                return "1.0";
            }
        }

        std::string Local() {
            const char* type = pick(kTypes);
            switch (roll(5)) {
            case 0:
                return std::string("    ") + type + " " + Name() + " = " + Expr() + ";\n";
            case 1:
                return std::string("    ") + type + " a, " + Name() + " = 1.0;\n";
            case 2:
                return std::string("    const ") + type + " " + Name() + " = 2.0, b = " + Expr() + ";\n";
            case 3:
                return roll(2) ? "    " + Name() + " = " + Expr() + ";\n"
                               : std::string("    const float k = 1.0, ") + Name() + " = 2.0;\n";
            default:
                return "    float t = " + Expr() + ";\n";
            }
        }

        std::string Item() {
            switch (roll(6)) {
            case 0: {
                std::string s = std::string(pick(kTypes)) + " f" + std::to_string(roll(9)) + "(";
                const int params = roll(3);
                for (int i = 0; i < params; i++)
                    s += (i ? ", " : "") + std::string(roll(2) ? "in " : "") + pick(kTypes) + " " + Name();
                s += ") {\n";
                const int body = roll(5);
                for (int i = 0; i < body; i++)
                    s += Local();
                s += roll(2) ? "    if (a > 0.0) {\n        " + std::string(pick(kTypes)) + " " + Name() +
                                   " = 0.0;\n    }\n"
                             : "";
                s += "    return " + Expr() + ";\n}\n";
                return s;
            }
            case 1:
                return std::string(pick(kTypes)) + " " + Name() + (roll(2) ? " = 1.0" : "") + ";\n";
            case 2:
                return "void main() {\n" + Local() + Local() + "    gl_FragColor = vec4(" + Expr() +
                       ");\n}\n";
            case 3:
                return "#define S(x) texture2D(" + Name() + ", x)\n";
            case 4:
                return std::string("float g(sampler2D ") + Name() + ", vec2 p) { return texture2D(" + Name() +
                       ", p).r; }\n";
            default:
                return std::string(pick(kTypes)) + " " + Name() + "[2];\n";
            }
        }
    };
}

int main() {
    int renamedCases = 0;
    for (unsigned seed = 1; seed <= 30000; seed++) {
        Gen gen(seed);
        const std::string source = gen.Shader();
        std::vector<Tok> expected = glsl::Lex(source);
        std::vector<Tok> actual = expected;
        ReferenceRename(expected);
        RenameShadowedSamplers(actual);
        const std::string a = Render(expected), b = Render(actual);
        if (a != b) {
            std::printf("FAIL seed %u\n--- source\n%s--- reference\n%s--- actual\n%s\n", seed, source.c_str(),
                        a.c_str(), b.c_str());
            failures++;
            if (failures > 3)
                break;
        }
        if (a.find("iris_local_") != std::string::npos)
            renamedCases++;
    }
    if (renamedCases < 3000) {
        std::printf("FAIL generator rarely exercises renaming (%d cases)\n", renamedCases);
        failures++;
    }
    std::printf("renamed in %d of 30000 generated shaders\n", renamedCases);
    std::printf(failures ? "sampler_rename_selftest FAILED\n" : "sampler_rename_selftest passed\n");
    return failures ? 1 : 0;
}
