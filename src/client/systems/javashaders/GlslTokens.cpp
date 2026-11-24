#include "client/systems/javashaders/GlslTokens.hpp"

#include <cctype>
#include <regex>
#include <unordered_set>

namespace perlin::jshader::glsl {
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
                    while (e < s.size() && (std::isdigit(static_cast<unsigned char>(s[e])) || s[e] == '.'))
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
        return t.starts_with("sampler") || t.starts_with("isampler") || t.starts_with("usampler") ||
               t.starts_with("image") || t.starts_with("iimage") || t.starts_with("uimage") ||
               t == "atomic_uint";
    }

    namespace {
        const std::unordered_set<std::string>& BuiltinTypes() {
            static const std::unordered_set<std::string> set = {
                "void",   "bool",   "int",    "uint",   "float",  "double", "vec2",   "vec3",   "vec4",
                "ivec2",  "ivec3",  "ivec4",  "uvec2",  "uvec3",  "uvec4",  "bvec2",  "bvec3",  "bvec4",
                "mat2",   "mat3",   "mat4",   "mat2x2", "mat2x3", "mat2x4", "mat3x2", "mat3x3", "mat3x4",
                "mat4x2", "mat4x3", "mat4x4", "dvec2",  "dvec3",  "dvec4"};
            return set;
        }
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

    namespace {
        const std::regex& ImageDeclRegex() {
            static const std::regex kDecl(
                R"((layout\s*\(\s*([A-Za-z0-9_]+)[^)]*\)\s*)?(?:\b(?:readonly|writeonly|coherent|volatile|restrict|uniform)\s+)+([iu]?image(?:2D|3D|2DArray))\s+([A-Za-z_][A-Za-z0-9_]*)\s*;)");
            return kDecl;
        }
        ImageDecl DeclOf(const std::smatch& m) {
            return {m[2].matched ? m[2].str() : std::string(), m[3].str(), m[4].str()};
        }
    }

    std::vector<ImageDecl> FindImageDeclarationsSlow(const std::string& text) {
        std::vector<ImageDecl> out;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), ImageDeclRegex()); it != std::sregex_iterator();
             ++it)
            out.push_back(DeclOf(*it));
        return out;
    }

    std::vector<ImageDecl> FindImageDeclarations(const std::string& text) {
        // A match always contains "image2D" or "image3D" and never a ';' before its own final one, nor '{' or
        // '}'. So every match lies inside the statement around such a word: from just after the previous ';',
        // '{' or '}' up to and including the next ';'. Matching there gives the same declarations in the same
        // order (a boundary character before the statement is a non-word character, as at the text's start).
        std::vector<ImageDecl> out;
        size_t done = 0; // statements are disjoint; skip words inside one already searched
        for (size_t at = text.find("image"); at != std::string::npos; at = text.find("image", at + 5)) {
            if (at + 7 > text.size() || text[at + 5] < '2' || text[at + 5] > '3' || text[at + 6] != 'D')
                continue;
            if (at < done)
                continue;
            const size_t open = text.find_last_of(";{}", at);
            const size_t from = open == std::string::npos ? 0 : open + 1;
            const size_t semi = text.find(';', at);
            if (semi == std::string::npos)
                break;
            done = semi + 1;
            const auto first = text.begin() + static_cast<std::ptrdiff_t>(from);
            const auto last = text.begin() + static_cast<std::ptrdiff_t>(semi + 1);
            for (auto it = std::sregex_iterator(first, last, ImageDeclRegex(),
                                                from == 0 ? std::regex_constants::match_default
                                                          : std::regex_constants::match_prev_avail);
                 it != std::sregex_iterator(); ++it)
                out.push_back(DeclOf(*it));
        }
        return out;
    }
}
