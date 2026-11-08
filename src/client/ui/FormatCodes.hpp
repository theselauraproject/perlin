#pragma once
#include <cstdint>
#include <string>
#include "client/ui/FormatColors.hpp"

namespace perlin::ui {
    std::string FormatCodesToRml(const std::string& text, float alpha = 1.0f);
    std::string FormatCodesToRmlLines(const std::string& text);
    std::string FormatCodesToRml(const std::string& text, float alpha, bool breaks);
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
