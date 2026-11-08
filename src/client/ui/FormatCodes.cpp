#include "client/ui/FormatCodes.hpp"

#include "client/ui/RmlEmit.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace perlin::ui {
    static bool BuiltinFormatCodeColor(char code, uint32_t& rgb);

    bool FormatCodeColor(char code, uint32_t& rgb) {
        if (!BuiltinFormatCodeColor(code, rgb))
            return false;
        FormatColorOverride(code, rgb);
        return true;
    }

    static bool BuiltinFormatCodeColor(char code, uint32_t& rgb) {
        switch (std::tolower(static_cast<unsigned char>(code))) {
        case '0':
            rgb = 0x000000;
            return true;
        case '1':
            rgb = 0x0000AA;
            return true;
        case '2':
            rgb = 0x00AA00;
            return true;
        case '3':
            rgb = 0x00AAAA;
            return true;
        case '4':
            rgb = 0xAA0000;
            return true;
        case '5':
            rgb = 0xAA00AA;
            return true;
        case '6':
            rgb = 0xFFAA00;
            return true;
        case '7':
            rgb = 0xAAAAAA;
            return true;
        case '8':
            rgb = 0x555555;
            return true;
        case '9':
            rgb = 0x447FFF;
            return true;
        case 'a':
            rgb = 0x55FF55;
            return true;
        case 'b':
            rgb = 0x55FFFF;
            return true;
        case 'c':
            rgb = 0xFF5555;
            return true;
        case 'd':
            rgb = 0xFF55FF;
            return true;
        case 'e':
            rgb = 0xFFFF55;
            return true;
        case 'f':
            rgb = 0xFFFFFF;
            return true;
        case 'g':
            rgb = 0xDDD605;
            return true;
        case 'h':
            rgb = 0xD9CCB8;
            return true;
        case 'i':
            rgb = 0xA9B4B7;
            return true;
        case 'j':
            rgb = 0x8F727D;
            return true;
        case 'm':
            rgb = 0xEE222C;
            return true;
        case 'n':
            rgb = 0xC87363;
            return true;
        case 'p':
            rgb = 0xFFBF1E;
            return true;
        case 'q':
            rgb = 0x13A045;
            return true;
        case 's':
            rgb = 0x5FECFF;
            return true;
        case 't':
            rgb = 0x577BFF;
            return true;
        case 'u':
            rgb = 0xB66CDD;
            return true;
        case 'v':
            rgb = 0xFF6A00;
            return true;
        default:
            return false;
        }
    }

    bool FormatState::Apply(uint32_t code) {
        if (code >= 0x80)
            return false;
        const char c = static_cast<char>(std::tolower(static_cast<int>(code)));
        uint32_t color = 0;
        if (FormatCodeColor(c, color)) {
            hasColor = true;
            rgb = color;
            colorCode = c;
            return true;
        }
        switch (c) {
        case 'l':
            bold = true;
            return true;
        case 'o':
            italic = true;
            return true;
        case 'k':
            obfuscated = true;
            return true;
        case 'r':
            *this = FormatState{};
            return true;
        default:
            return false;
        }
    }

    std::string FormatState::Codes() const {
        std::string out;
        const auto add = [&](char c) {
            out += "\xC2\xA7";
            out += c;
        };
        if (hasColor)
            add(colorCode);
        if (bold)
            add('l');
        if (italic)
            add('o');
        if (obfuscated)
            add('k');
        return out;
    }

    namespace {

        size_t CodepointBytes(const std::string& s, size_t i) {
            const unsigned char b = static_cast<unsigned char>(s[i]);
            const size_t n = b < 0x80           ? 1
                             : (b >> 5) == 0x6  ? 2
                             : (b >> 4) == 0xE  ? 3
                             : (b >> 3) == 0x1E ? 4
                                                : 1;
            return std::min(n, s.size() - i);
        }

        bool IsSection(const std::string& s, size_t i) {
            return i + 2 < s.size() && static_cast<unsigned char>(s[i]) == 0xC2 &&
                   static_cast<unsigned char>(s[i + 1]) == 0xA7;
        }
    }

    bool IsFormatCode(const std::string& text, size_t i) {
        return IsSection(text, i);
    }

    std::string StripFormatCodes(const std::string& text) {
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size();) {
            if (IsSection(text, i)) {
                i += 2 + CodepointBytes(text, i + 2);
                continue;
            }
            out += text[i++];
        }
        return out;
    }

    namespace {
        std::string Scramble(const std::string& in) {
            static const char* const kClasses[] = {
                "il!|'.,:;", "tI[] ", "fk<>(){}*\"",
                "abcdeghjmnopqrsuvwxyz0123456789ABCDEFGHJKLMNOPQRSTUVWXYZ"};
            static uint32_t state = 0x9E3779B9u;
            std::string out = in;
            for (char& c : out) {
                for (const char* cls : kClasses) {
                    const std::string_view set(cls);
                    if (!set.contains(c))
                        continue;
                    state ^= state << 13;
                    state ^= state >> 17;
                    state ^= state << 5;
                    c = set[state % set.size()];
                    break;
                }
            }
            return out;
        }
    }

    std::string FormatCodesToRml(const std::string& text, float alpha) {
        return FormatCodesToRml(text, alpha, false);
    }

    std::string FormatCodesToRmlLines(const std::string& text) {
        return FormatCodesToRml(text, 1.0f, true);
    }

    std::string FormatCodesToRml(const std::string& text, float alpha, bool breaks) {
        std::string out;
        std::string chunk;
        FormatState st;
        const int alphaByte = std::clamp(static_cast<int>(std::lround(alpha * 255.0f)), 0, 255);
        // RmlUi drops a text node that is only whitespace, so "§fa§f §fb" lost its space. A run of
        // spaces is carried into the next visible run (a space has no colour), or emitted with
        // the last one at the very end.
        std::string carry;
        const auto flush = [&](bool last = false) {
            if (chunk.empty())
                return;
            if (!last && chunk.find_first_not_of(" \t") == std::string::npos) {
                carry += chunk;
                chunk.clear();
                return;
            }
            if (!carry.empty()) {
                chunk.insert(0, carry);
                carry.clear();
            }
            std::string style;
            if (st.hasColor) {
                char buf[32];
                if (alphaByte < 255)
                    std::snprintf(buf, sizeof buf, "color:#%06X%02X;", st.rgb & 0xFFFFFFu, alphaByte);
                else
                    std::snprintf(buf, sizeof buf, "color:#%06X;", st.rgb & 0xFFFFFFu);
                style += buf;
            }
            if (st.bold)
                style += "font-weight:bold;";
            if (st.italic)
                style += "font-style:italic;";
            if (!style.empty())
                out += "<span style=\"" + style + "\">";
            out += EscapeRml(st.obfuscated ? Scramble(chunk) : chunk);
            if (!style.empty())
                out += "</span>";
            chunk.clear();
        };
        for (size_t i = 0; i < text.size();) {
            if (IsSection(text, i)) {
                flush();
                st.Apply(static_cast<unsigned char>(text[i + 2]));
                i += 2 + CodepointBytes(text, i + 2);
                continue;
            }

            if (i + 1 < text.size() && static_cast<unsigned char>(text[i]) == 0xC2 &&
                static_cast<unsigned char>(text[i + 1]) == 0xA7) {
                i += 2;
                continue;
            }
            const char c = text[i++];
            if (c == '\r')
                continue;
            if (c == '\n') {
                if (breaks) {
                    flush();
                    out += "<br/>";
                } else
                    chunk += ' ';
                continue;
            }
            chunk += c;
        }
        chunk.insert(0, carry);
        flush(true);
        return out;
    }
}
