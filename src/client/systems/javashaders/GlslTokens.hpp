#pragma once
#include <string>
#include <vector>

namespace perlin::jshader::glsl {
    struct Tok {
        enum Kind { Ident, Number, Punct, Directive } kind = Punct;
        std::string text;
        std::string ws;
    };

    bool IsIdStart(char c);
    bool IsIdChar(char c);
    std::vector<Tok> Lex(const std::string& s);
    std::string Join(const std::vector<Tok>& t, size_t a, size_t b);
    bool IsOpaqueType(const std::string& t);
    bool IsType(const std::string& t);
    bool IsQualifier(const std::string& t);
}
