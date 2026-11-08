#pragma once
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <utility>
#include <vector>

namespace perlin::ui {

    namespace format_colors {

        inline constexpr uint32_t kSet = 0x1000000u;
        inline std::array<std::atomic<uint32_t>, 128> table{};

        inline size_t Index(char code) {
            return static_cast<size_t>(std::tolower(static_cast<unsigned char>(code))) & 0x7F;
        }
    }

    inline void SetFormatColorOverrides(const std::vector<std::pair<char, uint32_t>>& colors) {
        for (auto& v : format_colors::table)
            v.store(0, std::memory_order_relaxed);
        for (const auto& [code, rgb] : colors)
            format_colors::table[format_colors::Index(code)].store((rgb & 0xFFFFFFu) | format_colors::kSet,
                                                                   std::memory_order_relaxed);
    }

    inline bool FormatColorOverride(char code, uint32_t& rgb) {
        const uint32_t o = format_colors::table[format_colors::Index(code)].load(std::memory_order_relaxed);
        if (!(o & format_colors::kSet))
            return false;
        rgb = o & 0xFFFFFFu;
        return true;
    }
}
