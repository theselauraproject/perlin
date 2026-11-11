#include "client/ui/TenFont.hpp"
#include "client/ui/McFont.hpp"
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>

int main() {
    auto& font = perlin::ui::TenFont::Instance();
    font.EnsureLoaded();
    if (!font.Available())
        return 77;
    std::ifstream input(font.Path(), std::ios::binary);
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), {}};
    const auto u16 = [&](size_t at) { return uint16_t((uint16_t(bytes.at(at)) << 8) | bytes.at(at + 1)); };
    const auto u32 = [&](size_t at) { return (uint32_t(u16(at)) << 16) | u16(at + 2); };
    size_t head = 0, hhea = 0;
    for (size_t i = 0; i < u16(4); ++i) {
        const size_t record = 12 + i * 16;
        const std::string tag(bytes.begin() + record, bytes.begin() + record + 4);
        if (tag == "head")
            head = u32(record + 8);
        if (tag == "hhea")
            hhea = u32(record + 8);
    }
    if (!head || !hhea || !u16(head + 18))
        return 1;
    const float units = u16(head + 18);
    const float ascent = std::bit_cast<int16_t>(u16(hhea + 4));
    const float descent = -std::bit_cast<int16_t>(u16(hhea + 6));
    for (float scale : {0.5f, 0.75f, 1.0f, 1.5f, 2.0f}) {
        const float em = font.Em(scale);
        const float fallbackAdvance = perlin::ui::McFont::Instance().Get(0xe000).advance * em / 8.0f;
        if (std::fabs(font.Advance(0xe000, scale) - fallbackAdvance) > 1e-5f)
            return 1;
        if (std::fabs(font.Ascent(scale) - ascent * em / units) > 1e-5f ||
            std::fabs(font.Descent(scale) - descent * em / units) > 1e-5f || font.XHeight(scale) <= 0) {
            std::cerr << "font metrics differ from the installed font tables\n";
            return 1;
        }
    }
    return 0;
}
