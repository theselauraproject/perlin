#pragma once
#include <string_view>

namespace perlin::text {
    constexpr int DefaultGlyphWidth(char c) noexcept {
        switch (c) {
        case 'i': case '!': case '.': case ',': case ':': case ';': case '|': case '\'':
            return 2;
        case 'l': case '`':
            return 3;
        case 'I': case 't': case '[': case ']': case ' ':
            return 4;
        case 'f': case 'k': case '<': case '>': case '{': case '}': case '(': case ')': case '*': case '"':
            return 5;
        case '@': case '~':
            return 7;
        default:
            return 6;
        }
    }

    constexpr int DefaultTextWidth(std::string_view utf8) noexcept {
        int width = 0;
        for (const char c : utf8)
            if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
                width += DefaultGlyphWidth(c);
        return width;
    }
}
