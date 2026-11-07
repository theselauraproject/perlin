#include "client/ui/FormatCodes.hpp"

#include "client/ui/RmlEmit.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace perlin::ui {
    namespace {
        // pack colours ($<c>_color_format, $material_*_color in ui/_global_variables.json); read from render threads.
        // bit 24 marks a set entry, so the zero-initialised table means "no override"
        constexpr uint32_t kSet = 0x1000000u;
        std::array<std::atomic<uint32_t>, 128> colorOverrides{};
    }

    void SetFormatColorOverrides(const std::vector<std::pair<char, uint32_t>>& colors) {
        for (auto& v : colorOverrides)
            v.store(0, std::memory_order_relaxed);
        for (const auto& [code, rgb] : colors)
            colorOverrides[static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(code))) & 0x7F].store(
                (rgb & 0xFFFFFFu) | kSet, std::memory_order_relaxed);
    }

    static bool BuiltinFormatCodeColor(char code, uint32_t& rgb);

    bool FormatCodeColor(char code, uint32_t& rgb) {
        if (!BuiltinFormatCodeColor(code, rgb))
            return false;
        const uint32_t o =
            colorOverrides[static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(code))) & 0x7F].load(
                std::memory_order_relaxed);
        if (o & kSet)
            rgb = o & 0xFFFFFFu;
        return true;
    }

    // 26.50 palette: blue and the material colours (h..v) were recoloured in that release
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
        // bytes of the UTF-8 sequence starting at s[i] (1 for a stray continuation byte)
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
        const auto flush = [&] {
            if (chunk.empty())
                return;
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
            // a section sign as the last character: nothing to apply
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
        flush();
        return out;
    }
}
