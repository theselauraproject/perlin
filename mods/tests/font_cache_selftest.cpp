#include "client/ui/McFont.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    auto& font = perlin::ui::McFont::Instance();
    font.UseBuiltin();
    std::vector<uint8_t> detailed(1024 * 1024 * 4, 0);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            const size_t offset = (static_cast<size_t>(y) * 1024 + x) * 4;
            detailed[offset] = static_cast<uint8_t>(x * 3);
            detailed[offset + 1] = static_cast<uint8_t>(y * 3);
            detailed[offset + 2] = 173;
            detailed[offset + 3] = (x % 4 == 1 && y % 4 == 1) ? 255 : 0;
        }
    font.AddGlyphPage(0xe0, detailed, 1024, 1024);
    const auto icon = font.Get(0xe000);
    if (icon.width != 64 || icon.height != 64 || icon.top != -28 || icon.left != 0 || icon.advance != 62 ||
        !icon.colored) {
        std::cerr << "private-use icon metrics differ from native scan and build rules\n";
        return 1;
    }
    const int ax = std::lround(icon.u0 * font.AtlasWidth());
    const int ay = std::lround(icon.v0 * font.AtlasHeight());
    bool exactPixels = std::lround((icon.u1 - icon.u0) * font.AtlasWidth()) == 64 &&
                       std::lround((icon.v1 - icon.v0) * font.AtlasHeight()) == 64;
    for (int y = 0; y < 64 && exactPixels; y++)
        for (int x = 0; x < 64 && exactPixels; x++)
            for (int c = 0; c < 4; c++)
                if (font.Atlas()[((static_cast<size_t>(ay + y) * font.AtlasWidth()) + ax + x) * 4 + c] !=
                    detailed[(static_cast<size_t>(y) * 1024 + x) * 4 + c])
                    exactPixels = false;
    if (!exactPixels) {
        std::cerr << "resource-pack icon pixels were resampled or lost\n";
        return 1;
    }
    if (font.Width("§m§0§0") != 0) {
        std::cerr << "color-code metadata was decoded as an icon\n";
        return 1;
    }
    font.AddGlyphPage(0xf9, detailed, 1024, 1024);
    const auto outsidePrivateIcons = font.Get(0xf900);
    if (outsidePrivateIcons.width != 8 || outsidePrivateIcons.height != 8 || outsidePrivateIcons.colored ||
        outsidePrivateIcons.advance != 8.625f) {
        std::cerr << "ordinary unicode glyph incorrectly used private-use sizing\n";
        return 1;
    }
    const int fallbackWidth = font.Get('?').width;
    std::vector<uint8_t> page(256 * 256 * 4, 255);
    font.AddGlyphPage(0xe0, page, 256, 256);
    const bool customGlyph = font.Get(0xe000).width == 16;
    const auto file = std::filesystem::temp_directory_path() /
                      ("perlin-font-cache-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".hex");
    std::ofstream(file) << "4E00:" << std::string(32, 'F') << '\n';
    const bool loaded = font.LoadUnihexFile(file.string()) && font.HasUnihex();
    font.UnloadLevelResources();
    font.UnloadLevelResources();
    const bool cleared = font.Builtin() && !font.HasUnihex() && !font.HasVanillaSheets() &&
                         font.TtfCount() == 0 && font.Get(0xe000).width == fallbackWidth;
    const bool reload = font.LoadUnihexFile(file.string()) && font.HasUnihex();
    std::filesystem::remove(file);
    if (!customGlyph || !loaded || !cleared || !reload)
        std::cerr << "font cache survived unload or failed to reload\n";
    return customGlyph && loaded && cleared && reload ? 0 : 1;
}
