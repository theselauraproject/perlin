#pragma once
#include <cstdint>
#include <string_view>

namespace perlin::ui::draw {
    struct Color {
        uint8_t r = 255, g = 255, b = 255, a = 255;
        bool operator==(const Color&) const = default;
    };

    constexpr Color Rgb(uint32_t rgb, uint8_t a = 255) {
        return {static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8), static_cast<uint8_t>(rgb), a};
    }

    bool ParseColor(std::string_view text, Color& out);
    uint32_t PackPremultiplied(Color c);
}
