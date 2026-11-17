#include "client/ui/draw/Color.hpp"

#include <cstdlib>
#include <string>

namespace perlin::ui::draw {
    namespace {
        int Hex(char c) {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        }
    }

    bool ParseColor(std::string_view text, Color& out) {
        if (text.size() < 4 || text[0] != '#')
            return false;
        const std::string_view h = text.substr(1);
        for (const char c : h)
            if (Hex(c) < 0)
                return false;
        if (h.size() == 6 || h.size() == 8) {
            out.r = static_cast<uint8_t>(Hex(h[0]) * 16 + Hex(h[1]));
            out.g = static_cast<uint8_t>(Hex(h[2]) * 16 + Hex(h[3]));
            out.b = static_cast<uint8_t>(Hex(h[4]) * 16 + Hex(h[5]));
            out.a = h.size() == 8 ? static_cast<uint8_t>(Hex(h[6]) * 16 + Hex(h[7])) : 255;
            return true;
        }
        if (h.size() == 3 || h.size() == 4) {
            out.r = static_cast<uint8_t>(Hex(h[0]) * 17);
            out.g = static_cast<uint8_t>(Hex(h[1]) * 17);
            out.b = static_cast<uint8_t>(Hex(h[2]) * 17);
            out.a = h.size() == 4 ? static_cast<uint8_t>(Hex(h[3]) * 17) : 255;
            return true;
        }
        return false;
    }

    uint32_t PackPremultiplied(Color c) {
        const auto mul = [&](uint8_t v) { return static_cast<uint32_t>((v * c.a + 127) / 255); };
        return (static_cast<uint32_t>(c.a) << 24) | (mul(c.b) << 16) | (mul(c.g) << 8) | mul(c.r);
    }
}
