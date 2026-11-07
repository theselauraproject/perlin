#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace perlin::ui {
    std::string FormatCodesToRml(const std::string& text, float alpha = 1.0f);
    std::string FormatCodesToRmlLines(const std::string& text);
    std::string FormatCodesToRml(const std::string& text, float alpha, bool breaks);
    std::string StripFormatCodes(const std::string& text);
    bool FormatCodeColor(char code, uint32_t& rgb);
    // replaces every pack override; an empty list restores the built-in palette
    void SetFormatColorOverrides(const std::vector<std::pair<char, uint32_t>>& colors);
    bool IsFormatCode(const std::string& text, size_t i);

    // running section-sign state as the game's font renderer keeps it: a colour leaves bold, italic and
    // obfuscated on (java resets them), only §r clears; m and n are the redstone/copper colours, not strike and
    // underline
    struct FormatState {
        bool hasColor = false;
        uint32_t rgb = 0xFFFFFF;
        char colorCode = 0;
        bool bold = false, italic = false, obfuscated = false;
        // false for a code the game ignores (it still takes the character after the section sign)
        bool Apply(uint32_t code);
        // the codes that put a fresh line into this state, for text carried over a wrap
        std::string Codes() const;
    };
}
