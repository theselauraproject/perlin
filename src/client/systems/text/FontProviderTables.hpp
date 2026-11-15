#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace perlin::ui {
    struct FontProviderTables {
        static constexpr size_t kNonlatinCells = 1072;
        static constexpr size_t kAccentedCells = 1200;
        std::vector<uint32_t> nonlatin;
        std::vector<uint32_t> accented;
    };

    bool ParseFontProviderTables(std::string_view text, FontProviderTables& out, std::string& error);
    bool LoadFontProviderTables(FontProviderTables& out, std::string& error);
}
