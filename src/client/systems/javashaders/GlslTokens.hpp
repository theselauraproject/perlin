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

    // storage image declarations, "layout(rgba8) uniform image2D name;" and the like, in source order. Finds
    // exactly what one regex over the whole text finds, but only runs that regex on the statements that can
    // hold a declaration, which matters for packs whose expanded programs run to hundreds of kilobytes.
    struct ImageDecl {
        std::string layoutFirst; // first identifier inside layout(...), empty without a layout
        std::string type;        // image2D, uimage3D, ...
        std::string name;
    };
    std::vector<ImageDecl> FindImageDeclarations(const std::string& text);
    std::vector<ImageDecl> FindImageDeclarationsSlow(const std::string& text); // the whole-text regex, for tests
}
