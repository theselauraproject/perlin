#include "client/systems/javashaders/SamplerRename.hpp"

#include <algorithm>
#include <set>
#include <string>

namespace perlin::jshader {
    using glsl::IsOpaqueType;
    using glsl::IsQualifier;
    using glsl::IsType;
    using glsl::Tok;

    namespace {
        constexpr const char* kLocalPrefix = "iris_local_";

        struct SamplerUniforms {
            std::set<std::string> names;
            std::set<size_t> declarationTokens;

            bool Declares(size_t token) const { return declarationTokens.contains(token); }
            bool IsSampler(const Tok& t) const { return t.kind == Tok::Ident && names.contains(t.text); }
        };

        struct Scope {
            size_t begin, end;
            std::string name;
        };

        SamplerUniforms FindSamplerUniforms(const std::vector<Tok>& t) {
            SamplerUniforms found;
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
                        found.names.insert(t[k].text);
                        found.declarationTokens.insert(k);
                    }
                }
            }
            return found;
        }

        class TypeNames {
        public:
            explicit TypeNames(const std::vector<Tok>& tokens) : t(tokens) {
                for (size_t i = 0; i + 1 < t.size(); i++)
                    if (t[i].text == "struct" && t[i + 1].kind == Tok::Ident)
                        structs.insert(t[i + 1].text);
            }

            bool IsTypeAt(size_t k) const {
                return t[k].kind == Tok::Ident && !IsOpaqueType(t[k].text) &&
                       (IsType(t[k].text) || structs.contains(t[k].text));
            }

        private:
            const std::vector<Tok>& t;
            std::set<std::string> structs;
        };

        std::string Renamed(const std::string& name) {
            return kLocalPrefix + name;
        }

        void RenameMemberAccesses(std::vector<Tok>& t, const SamplerUniforms& samplers) {
            for (size_t i = 1; i < t.size(); i++)
                if (samplers.IsSampler(t[i]) && t[i - 1].text == ".")
                    t[i].text = Renamed(t[i].text);
        }

        size_t BlockEnd(const std::vector<Tok>& t, size_t open) {
            int depth = 0;
            size_t k = open;
            for (; k < t.size(); k++) {
                if (t[k].text == "{")
                    depth++;
                else if (t[k].text == "}" && --depth == 0)
                    break;
            }
            return std::min(k + 1, t.size());
        }

        size_t DeclarationStart(const std::vector<Tok>& t, size_t at, size_t headerStart) {
            size_t b = at;
            int d = 0;
            while (b > headerStart) {
                const std::string& y = t[b - 1].text;
                if (y == ")" || y == "]") {
                    d++;
                } else if (y == "(" || y == "[") {
                    if (d == 0)
                        break;
                    d--;
                } else if (d == 0 && (y == ";" || y == "{" || y == "}")) {
                    break;
                }
                b--;
            }
            while (b < at && t[b].kind == Tok::Ident && IsQualifier(t[b].text))
                b++;
            return b;
        }

        bool DeclaresLocal(const std::vector<Tok>& t, const TypeNames& types, size_t j, size_t headerStart) {
            if (j > 0 && types.IsTypeAt(j - 1))
                return true;
            if (j == 0 || t[j - 1].text != ",")
                return false;
            const size_t b = DeclarationStart(t, j, headerStart);
            return b < j && types.IsTypeAt(b);
        }

        std::vector<Scope> FindShadowingScopes(const std::vector<Tok>& t, const SamplerUniforms& samplers,
                                               const TypeNames& types) {
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
                    const size_t end = BlockEnd(t, i);
                    for (size_t j = headerStart; j < end; j++) {
                        if (!samplers.IsSampler(t[j]) || samplers.Declares(j) ||
                            t[j].text.starts_with(kLocalPrefix))
                            continue;
                        if (DeclaresLocal(t, types, j, headerStart))
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
            return scopes;
        }

        void RenameInScopes(std::vector<Tok>& t, const SamplerUniforms& samplers,
                            const std::vector<Scope>& scopes) {
            for (const Scope& sc : scopes)
                for (size_t j = sc.begin; j < sc.end; j++)
                    if (t[j].kind == Tok::Ident && t[j].text == sc.name && !samplers.Declares(j))
                        t[j].text = Renamed(sc.name);
        }

        bool AtTopLevel(const std::vector<Tok>& t, size_t j) {
            int depth = 0;
            for (size_t k = 0; k < j; k++) {
                if (t[k].text == "{")
                    depth++;
                else if (t[k].text == "}")
                    depth--;
            }
            return depth == 0;
        }

        bool InsideParameterList(const std::vector<Tok>& t, size_t j) {
            int open = 0;
            for (size_t k = j; k-- > 0 && t[k].text != ";" && t[k].text != "}" && t[k].text != "{";) {
                if (t[k].text == ")")
                    open--;
                else if (t[k].text == "(")
                    open++;
            }
            return open > 0;
        }

        void RenameGlobalShadows(std::vector<Tok>& t, const SamplerUniforms& samplers,
                                 const TypeNames& types) {
            for (size_t j = 1; j < t.size(); j++) {
                if (!samplers.IsSampler(t[j]) || samplers.Declares(j) || !types.IsTypeAt(j - 1))
                    continue;
                if (!AtTopLevel(t, j) || InsideParameterList(t, j))
                    continue;
                const std::string n = t[j].text;
                for (size_t k = 0; k < t.size(); k++)
                    if (t[k].kind == Tok::Ident && t[k].text == n && !samplers.Declares(k))
                        t[k].text = Renamed(n);
            }
        }
    }

    void RenameShadowedSamplers(std::vector<Tok>& t) {
        const SamplerUniforms samplers = FindSamplerUniforms(t);
        if (samplers.names.empty())
            return;
        const TypeNames types(t);
        RenameMemberAccesses(t, samplers);
        RenameInScopes(t, samplers, FindShadowingScopes(t, samplers, types));
        RenameGlobalShadows(t, samplers, types);
    }
}
