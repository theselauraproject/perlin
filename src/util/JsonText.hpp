#pragma once
#include <string>
#include <string_view>

namespace perlin::json {
    inline void AppendEscaped(std::string& out, std::string_view text) {
        constexpr char kHex[] = "0123456789abcdef";
        for (const char c : text) {
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += "\\u00";
                    out += kHex[(c >> 4) & 0xF];
                    out += kHex[c & 0xF];
                } else {
                    out += c;
                }
            }
        }
    }

    inline std::string Escape(std::string_view text) {
        std::string out;
        out.reserve(text.size() + 2);
        AppendEscaped(out, text);
        return out;
    }

    inline std::string Quote(std::string_view text) {
        std::string out;
        out.reserve(text.size() + 2);
        out += '"';
        AppendEscaped(out, text);
        out += '"';
        return out;
    }
}
