#pragma once
#include <cstdint>
#include <string>
#include "client/systems/text/FormatColors.hpp"

#include <vector>

namespace perlin::ui {
    struct FormatRun {
        std::string text;
        bool hasColor = false;
        uint32_t rgb = 0xFFFFFF;
        bool bold = false, italic = false;
        bool lineBreak = false;
    };

    std::vector<FormatRun> ParseFormatCodes(const std::string& text, bool breaks);
    std::string StripFormatCodes(const std::string& text);
    bool FormatCodeColor(char code, uint32_t& rgb);
    bool IsFormatCode(const std::string& text, size_t i);

    struct FormatState {
        bool hasColor = false;
        uint32_t rgb = 0xFFFFFF;
        char colorCode = 0;
        bool bold = false, italic = false, obfuscated = false;

        bool Apply(uint32_t code);

        std::string Codes() const;
    };
}
