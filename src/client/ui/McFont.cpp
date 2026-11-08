#include "client/ui/McFont.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <fstream>
#include <iterator>

namespace perlin::ui {
    namespace {
#include "client/ui/FontProviderTables.inc"
        const uint32_t kCp437High[128] = {
            0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF,
            0x00EE, 0x00EC, 0x00C4, 0x00C5, 0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
            0x00FF, 0x00D6, 0x00DC, 0x00F8, 0x00A3, 0x00D8, 0x00D7, 0x0192, 0x00E1, 0x00ED, 0x00F3, 0x00FA,
            0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x00AE, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
            0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557,
            0x255D, 0x255C, 0x255B, 0x2510, 0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
            0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559,
            0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
            0x03B1, 0x03B2, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x03BC, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4,
            0x221E, 0x2205, 0x2208, 0x2229, 0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
            0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x0000};
        const uint8_t kFont5x7[95][5] = {
            {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00}, {0x00, 0x07, 0x00, 0x07, 0x00},
            {0x14, 0x7F, 0x14, 0x7F, 0x14}, {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
            {0x36, 0x49, 0x56, 0x20, 0x50}, {0x00, 0x08, 0x07, 0x03, 0x00}, {0x00, 0x1C, 0x22, 0x41, 0x00},
            {0x00, 0x41, 0x22, 0x1C, 0x00}, {0x2A, 0x1C, 0x7F, 0x1C, 0x2A}, {0x08, 0x08, 0x3E, 0x08, 0x08},
            {0x00, 0x80, 0x70, 0x30, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x00, 0x60, 0x60, 0x00},
            {0x20, 0x10, 0x08, 0x04, 0x02}, {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
            {0x72, 0x49, 0x49, 0x49, 0x46}, {0x21, 0x41, 0x49, 0x4D, 0x33}, {0x18, 0x14, 0x12, 0x7F, 0x10},
            {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x31}, {0x41, 0x21, 0x11, 0x09, 0x07},
            {0x36, 0x49, 0x49, 0x49, 0x36}, {0x46, 0x49, 0x49, 0x29, 0x1E}, {0x00, 0x00, 0x14, 0x00, 0x00},
            {0x00, 0x40, 0x34, 0x00, 0x00}, {0x00, 0x08, 0x14, 0x22, 0x41}, {0x14, 0x14, 0x14, 0x14, 0x14},
            {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x59, 0x09, 0x06}, {0x3E, 0x41, 0x5D, 0x59, 0x4E},
            {0x7C, 0x12, 0x11, 0x12, 0x7C}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
            {0x7F, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
            {0x3E, 0x41, 0x41, 0x51, 0x73}, {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
            {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
            {0x7F, 0x02, 0x1C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
            {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
            {0x26, 0x49, 0x49, 0x49, 0x32}, {0x03, 0x01, 0x7F, 0x01, 0x03}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
            {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},
            {0x03, 0x04, 0x78, 0x04, 0x03}, {0x61, 0x59, 0x49, 0x4D, 0x43}, {0x00, 0x7F, 0x41, 0x41, 0x41},
            {0x02, 0x04, 0x08, 0x10, 0x20}, {0x41, 0x41, 0x41, 0x7F, 0x00}, {0x04, 0x02, 0x01, 0x02, 0x04},
            {0x40, 0x40, 0x40, 0x40, 0x40}, {0x00, 0x03, 0x07, 0x08, 0x00}, {0x20, 0x54, 0x54, 0x78, 0x40},
            {0x7F, 0x28, 0x44, 0x44, 0x38}, {0x38, 0x44, 0x44, 0x44, 0x28}, {0x38, 0x44, 0x44, 0x28, 0x7F},
            {0x38, 0x54, 0x54, 0x54, 0x18}, {0x00, 0x08, 0x7E, 0x09, 0x02}, {0x18, 0xA4, 0xA4, 0x9C, 0x78},
            {0x7F, 0x08, 0x04, 0x04, 0x78}, {0x00, 0x44, 0x7D, 0x40, 0x00}, {0x20, 0x40, 0x40, 0x3D, 0x00},
            {0x7F, 0x10, 0x28, 0x44, 0x00}, {0x00, 0x41, 0x7F, 0x40, 0x00}, {0x7C, 0x04, 0x78, 0x04, 0x78},
            {0x7C, 0x08, 0x04, 0x04, 0x78}, {0x38, 0x44, 0x44, 0x44, 0x38}, {0xFC, 0x18, 0x24, 0x24, 0x18},
            {0x18, 0x24, 0x24, 0x18, 0xFC}, {0x7C, 0x08, 0x04, 0x04, 0x08}, {0x48, 0x54, 0x54, 0x54, 0x24},
            {0x04, 0x04, 0x3F, 0x44, 0x24}, {0x3C, 0x40, 0x40, 0x20, 0x7C}, {0x1C, 0x20, 0x40, 0x20, 0x1C},
            {0x3C, 0x40, 0x30, 0x40, 0x3C}, {0x44, 0x28, 0x10, 0x28, 0x44}, {0x4C, 0x90, 0x90, 0x90, 0x7C},
            {0x44, 0x64, 0x54, 0x4C, 0x44}, {0x00, 0x08, 0x36, 0x41, 0x00}, {0x00, 0x00, 0x77, 0x00, 0x00},
            {0x00, 0x41, 0x36, 0x08, 0x00}, {0x02, 0x01, 0x02, 0x04, 0x02},
        };
    }

    McFont::McFont() {
        UseBuiltin();
    }

    McFont& McFont::Instance() {
        static McFont font;
        return font;
    }

    bool McFont::Next(const std::string& s, size_t& i, uint32_t& cp) {
        if (i >= s.size())
            return false;
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            cp = c;
            i += 1;
        } else if ((c >> 5) == 0x6 && i + 1 < s.size()) {
            cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            i += 2;
        } else if ((c >> 4) == 0xE && i + 2 < s.size()) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
            i += 3;
        } else if ((c >> 3) == 0x1E && i + 3 < s.size()) {
            cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
                 ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
            i += 4;
        } else {
            cp = c;
            i += 1;
        }
        return true;
    }

    void McFont::UnloadLevelResources() {
        glyphPages.clear();
        sheets.clear();
        sheetChars.clear();
        hex.clear();
        hexPath.clear();
        ttfs.clear();
        ttfPaths.clear();
        UseBuiltin();
    }

    void McFont::UseBuiltin() {
        builtin = true;
        constexpr int cellsAcross = 16, cellPx = 8;
        std::vector<uint8_t> cells(static_cast<size_t>(cellsAcross * cellPx) * cellsAcross * cellPx * 4, 0);
        const int stride = cellsAcross * cellPx;
        for (int ch = 0x20; ch <= 0x7E; ch++) {
            const uint8_t* columns = kFont5x7[ch - 0x20];
            int first = 0, last = 4;
            while (first < 5 && columns[first] == 0)
                first++;
            while (last >= 0 && columns[last] == 0)
                last--;
            const int cx = (ch % cellsAcross) * cellPx, cy = (ch / cellsAcross) * cellPx;
            if (first <= last) {
                for (int col = first; col <= last; col++)
                    for (int row = 0; row < 8; row++)
                        if (columns[col] & (1 << row)) {
                            const size_t o =
                                (static_cast<size_t>(cy + row) * stride + (cx + col - first)) * 4;
                            cells[o] = cells[o + 1] = cells[o + 2] = cells[o + 3] = 255;
                        }
            }
        }
        Build(cells, cellsAcross, cellPx);
        glyphs[0x20].width = 3;
        glyphs[0x20].advance = 4;
        version++;
    }

    bool McFont::LoadSheet(const uint8_t* rgba, int width, int height) {
        if (!rgba || width < 16 || width != height || width % 16 != 0)
            return false;
        const int cellPx = width / 16;
        std::vector<uint8_t> copy(rgba, rgba + static_cast<size_t>(width) * height * 4);
        Build(copy, 16, cellPx);
        builtin = false;
        glyphs[0x20].width = 3;
        glyphs[0x20].advance = 4;
        version++;
        return true;
    }

    void McFont::Build(const std::vector<uint8_t>& cells, int cellsAcross, int cellPx) {
        const int sheet = cellsAcross * cellPx;
        atlasWidth = std::max(1024, sheet);
        atlasHeight = atlasWidth * 2;
        atlas.assign(static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight) * 4, 0);
        for (int y = 0; y < sheet; y++)
            std::memcpy(&atlas[static_cast<size_t>(y) * static_cast<size_t>(atlasWidth) * 4],
                        &cells[static_cast<size_t>(y) * static_cast<size_t>(sheet) * 4],
                        static_cast<size_t>(sheet) * 4);
        dynY0 = ((sheet + 15) / 16) * 16;
        dynNext = 0;
        glyphShelfX = glyphShelfHeight = 0;
        glyphShelfY = dynLimitY = atlasHeight;
        dyn.clear();
        glyphs.assign(256, Glyph{});
        const int stride = atlasWidth;
        for (int ch = 0; ch < 256; ch++) {
            const int cx = (ch % cellsAcross) * cellPx, cy = (ch / cellsAcross) * cellPx;
            int ink = 0;
            for (int col = cellPx - 1; col >= 0 && ink == 0; col--)
                for (int row = 0; row < cellPx; row++)
                    if (atlas[(static_cast<size_t>(cy + row) * stride + (cx + col)) * 4 + 3] > 0) {
                        ink = col + 1;
                        break;
                    }
            Glyph& g = glyphs[static_cast<size_t>(ch)];
            const int scale = std::max(1, cellPx / 8);
            g.width = (ink + scale - 1) / scale;
            g.advance = ink == 0 ? 0 : g.width + 1;
            g.cell = kCell;
            g.u0 = static_cast<float>(cx) / atlasWidth;
            g.v0 = static_cast<float>(cy) / atlasHeight;
            g.u1 = static_cast<float>(cx + (ink == 0 ? cellPx : ink)) / atlasWidth;
            g.v1 = static_cast<float>(cy + cellPx) / atlasHeight;
        }
        mapped.clear();
        for (int i = 0x80; i < 0x100; i++) {
            const Glyph g = glyphs[static_cast<size_t>(i)];
            glyphs[static_cast<size_t>(i)] = Glyph{};
            if (g.advance > 0 && kCp437High[i - 0x80] != 0)
                mapped[kCp437High[i - 0x80]] = g;
        }
        fallback = glyphs['?'];
    }

    bool McFont::LoadVanillaSheets(const RawSheet& accented, const RawSheet& nonlatin) {
        struct Source {
            const RawSheet& raw;
            const uint32_t* chars;
            int cols, rows, cellH, ascent;
        };
        const Source sources[2] = {{nonlatin, kNonlatinChars, 16, 67, 8, 7},
                                   {accented, kAccentedChars, 16, 75, 12, 10}};
        std::vector<Sheet> loaded;
        std::unordered_map<uint32_t, std::pair<uint16_t, uint16_t>> table;
        for (const Source& src : sources) {
            if (!src.raw.rgba || src.raw.width <= 0 || src.raw.height <= 0)
                continue;
            const int w = src.raw.width, h = src.raw.height;
            Sheet sheet;
            sheet.width = w;
            sheet.height = h;
            sheet.cellW = w / src.cols;
            sheet.cellH = src.cellH;
            sheet.ascent = src.ascent;
            sheet.rgba.assign(src.raw.rgba,
                              src.raw.rgba + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
            if (sheet.cellW <= 0 || sheet.cellW > 16 || sheet.cellH > 16 || h < src.rows * src.cellH)
                continue;
            const uint16_t index = static_cast<uint16_t>(loaded.size());
            for (int cell = 0; cell < src.cols * src.rows; cell++) {
                const uint32_t cp = src.chars[cell];
                if (cp == 0 || table.contains(cp))
                    continue;
                table[cp] = {index, static_cast<uint16_t>(cell)};
            }
            loaded.push_back(std::move(sheet));
        }
        if (loaded.empty())
            return false;
        sheets = std::move(loaded);
        sheetChars = std::move(table);
        dyn.clear();
        dynNext = 0;
        glyphShelfX = glyphShelfHeight = 0;
        glyphShelfY = dynLimitY = atlasHeight;
        for (int y = dynY0; y < atlasHeight; y++)
            std::memset(&atlas[static_cast<size_t>(y) * static_cast<size_t>(atlasWidth) * 4], 0,
                        static_cast<size_t>(atlasWidth) * 4);
        version++;
        return true;
    }

    const McFont::Glyph* McFont::FromSheet(uint32_t cp) const {
        const auto found = sheetChars.find(cp);
        if (found == sheetChars.end())
            return nullptr;
        const Sheet& sheet = sheets[found->second.first];
        const int cell = found->second.second;
        const int col = cell % 16, row = cell / 16;
        const int sx = col * sheet.cellW, sy = row * sheet.cellH;
        int ink = 0;
        for (int x = sheet.cellW - 1; x >= 0 && ink == 0; x--)
            for (int y = 0; y < sheet.cellH; y++)
                if (sheet.rgba[(static_cast<size_t>(sy + y) * static_cast<size_t>(sheet.width) +
                                static_cast<size_t>(sx + x)) *
                                   4 +
                               3] > 0) {
                    ink = x + 1;
                    break;
                }
        if (ink == 0)
            return nullptr;
        const int perRow = atlasWidth / 16;
        const int rows = (dynLimitY - dynY0) / 16;
        if (dynNext >= perRow * rows)
            return nullptr;
        const int cx = (dynNext % perRow) * 16, cy = dynY0 + (dynNext / perRow) * 16;
        dynNext++;
        for (int y = 0; y < sheet.cellH; y++)
            std::memcpy(&atlas[(static_cast<size_t>(cy + y) * static_cast<size_t>(atlasWidth) +
                                static_cast<size_t>(cx)) *
                               4],
                        &sheet.rgba[(static_cast<size_t>(sy + y) * static_cast<size_t>(sheet.width) +
                                     static_cast<size_t>(sx)) *
                                    4],
                        static_cast<size_t>(sheet.cellW) * 4);
        Glyph g;
        g.width = ink;
        g.advance = ink + 1;
        g.cell = kCell;
        g.top = static_cast<float>(7 - sheet.ascent);
        g.height = static_cast<float>(sheet.cellH);
        g.u0 = static_cast<float>(cx) / static_cast<float>(atlasWidth);
        g.v0 = static_cast<float>(cy) / static_cast<float>(atlasHeight);
        g.u1 = static_cast<float>(cx + ink) / static_cast<float>(atlasWidth);
        g.v1 = static_cast<float>(cy + sheet.cellH) / static_cast<float>(atlasHeight);
        version++;
        return &dyn.emplace(cp, g).first->second;
    }

    bool McFont::LoadUnihexFile(const std::string& path) {
        if (!hex.empty() && path == hexPath) {
            dyn.clear();
            dynNext = 0;
            glyphShelfX = glyphShelfHeight = 0;
            glyphShelfY = dynLimitY = atlasHeight;
            version++;
            return true;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        std::unordered_map<uint32_t, HexGlyph> parsed;
        std::string line;
        const auto digit = [](char ch) -> int {
            if (ch >= '0' && ch <= '9')
                return ch - '0';
            if (ch >= 'A' && ch <= 'F')
                return ch - 'A' + 10;
            if (ch >= 'a' && ch <= 'f')
                return ch - 'a' + 10;
            return -1;
        };
        while (std::getline(in, line)) {
            const size_t colon = line.find(':');
            if (colon == std::string::npos || colon == 0)
                continue;
            uint32_t cp = 0;
            bool ok = true;
            for (size_t i = 0; i < colon && ok; i++) {
                const int d = digit(line[i]);
                if (d < 0)
                    ok = false;
                else
                    cp = cp * 16 + static_cast<uint32_t>(d);
            }
            size_t len = line.size();
            while (len > colon + 1 &&
                   (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' '))
                len--;
            const size_t digits = len - colon - 1;
            if (!ok || (digits != 32 && digits != 64))
                continue;
            HexGlyph g;
            g.wide = digits == 64;
            for (size_t i = 0; i < digits / 2 && ok; i++) {
                const int hi = digit(line[colon + 1 + i * 2]), lo = digit(line[colon + 2 + i * 2]);
                if (hi < 0 || lo < 0)
                    ok = false;
                else
                    g.rows[i] = static_cast<uint8_t>(hi * 16 + lo);
            }
            if (ok)
                parsed[cp] = g;
        }
        if (parsed.empty())
            return false;
        hex = std::move(parsed);
        hexPath = path;
        dyn.clear();
        dynNext = 0;
        glyphShelfX = glyphShelfHeight = 0;
        glyphShelfY = dynLimitY = atlasHeight;
        for (int y = dynY0; y < atlasHeight; y++)
            std::memset(&atlas[static_cast<size_t>(y) * static_cast<size_t>(atlasWidth) * 4], 0,
                        static_cast<size_t>(atlasWidth) * 4);
        version++;
        return true;
    }

    struct McFont::TtfFont {
        std::vector<unsigned char> data;
        stbtt_fontinfo info{};
    };

    bool McFont::AddTtf(const std::string& path) {
        for (const std::string& known : ttfPaths)
            if (known == path)
                return true;
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        auto font = std::make_shared<TtfFont>();
        font->data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (font->data.size() < 1024)
            return false;
        const int offset = stbtt_GetFontOffsetForIndex(font->data.data(), 0);
        if (offset < 0 || !stbtt_InitFont(&font->info, font->data.data(), offset))
            return false;
        ttfs.push_back(std::move(font));
        ttfPaths.push_back(path);
        dyn.clear();
        dynNext = 0;
        glyphShelfX = glyphShelfHeight = 0;
        glyphShelfY = dynLimitY = atlasHeight;
        for (int y = dynY0; y < atlasHeight; y++)
            std::memset(&atlas[static_cast<size_t>(y) * static_cast<size_t>(atlasWidth) * 4], 0,
                        static_cast<size_t>(atlasWidth) * 4);
        version++;
        return true;
    }

    bool McFont::RasterTtf(uint32_t cp, int cx, int cy, Glyph& out) const {
        for (const std::shared_ptr<TtfFont>& font : ttfs) {
            const int index = stbtt_FindGlyphIndex(&font->info, static_cast<int>(cp));
            if (index == 0)
                continue;
            int ascent = 0, descent = 0, gap = 0;
            stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &gap);
            const float total = static_cast<float>(ascent - descent);
            const float scale = total > 0
                                    ? std::min(stbtt_ScaleForPixelHeight(&font->info, 14.0f), 15.0f / total)
                                    : stbtt_ScaleForPixelHeight(&font->info, 14.0f);
            const int baseline = static_cast<int>(std::lround(static_cast<float>(ascent) * scale)) + 1;
            int x0, y0, x1, y1;
            stbtt_GetGlyphBitmapBox(&font->info, index, scale, scale, &x0, &y0, &x1, &y1);
            const int w = x1 - x0, h = y1 - y0;
            int advance = 0, bearing = 0;
            stbtt_GetGlyphHMetrics(&font->info, index, &advance, &bearing);
            int ink = 0;
            if (w > 0 && h > 0 && w <= 32 && h <= 32) {
                std::vector<unsigned char> bitmap(static_cast<size_t>(w) * static_cast<size_t>(h));
                stbtt_MakeGlyphBitmap(&font->info, bitmap.data(), w, h, w, scale, scale, index);
                for (int row = 0; row < h; row++)
                    for (int col = 0; col < w; col++) {
                        const int px = x0 + col, py = baseline + y0 + row;
                        if (px < 0 || px >= 16 || py < 0 || py >= 16)
                            continue;
                        if (bitmap[static_cast<size_t>(row) * static_cast<size_t>(w) +
                                   static_cast<size_t>(col)] < 110)
                            continue;
                        uint8_t* dst =
                            &atlas[(static_cast<size_t>(cy + py) * static_cast<size_t>(atlasWidth) +
                                    static_cast<size_t>(cx + px)) *
                                   4];
                        dst[0] = dst[1] = dst[2] = dst[3] = 255;
                        ink = std::max(ink, px + 1);
                    }
            }

            if (ink == 0) {
                out.width = 0;
                out.advance =
                    std::max(2, static_cast<int>(std::lround(static_cast<float>(advance) * scale / 2.0f)));
            } else {
                out.width = (ink + 1) / 2;
                out.advance =
                    std::max(out.width + 1.0f,
                             static_cast<float>(std::lround(static_cast<float>(advance) * scale / 2.0f)));
            }
            return true;
        }
        return false;
    }

    void McFont::AddGlyphPage(uint32_t page, const std::vector<uint8_t>& rgba, int width, int height) {
        if (width < 16 || height < 16 ||
            rgba.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
            return;
        glyphPages[page] = {rgba, width, height};
        for (uint32_t cp = page << 8; cp < ((page + 1) << 8); cp++)
            dyn.erase(cp);
        version++;
    }

    void McFont::ClearGlyphPages() {
        for (const auto& [page, sheet] : glyphPages)
            for (uint32_t cp = page << 8; cp < ((page + 1) << 8); cp++)
                dyn.erase(cp);
        glyphPages.clear();
        version++;
    }

    bool McFont::ReserveGlyphCell(int w, int h, int& x, int& y) const {
        if (w > atlasWidth || w <= 0 || h <= 0)
            return false;
        if (glyphShelfX + w > atlasWidth || h > glyphShelfHeight) {
            const int perRow = atlasWidth / 16;
            const int usedBottom = dynY0 + ((dynNext + perRow - 1) / perRow) * 16;
            if (dynLimitY - h < usedBottom)
                return false;
            glyphShelfY = dynLimitY - h;
            dynLimitY = glyphShelfY;
            glyphShelfX = 0;
            glyphShelfHeight = h;
        }
        x = glyphShelfX;
        y = glyphShelfY;
        glyphShelfX += w;
        return true;
    }

    const McFont::Glyph* McFont::FromGlyphPage(uint32_t cp) const {
        const auto page = glyphPages.find(cp >> 8);
        if (page == glyphPages.end() || atlasWidth < 16)
            return nullptr;
        const GlyphPage& gp = page->second;
        const int cellW = gp.width / 16, cellH = gp.height / 16;
        const int sx0 = static_cast<int>(cp & 15) * cellW, sy0 = static_cast<int>((cp >> 4) & 15) * cellH;
        int first = cellW, last = -1;
        for (int y = 0; y < cellH; y++)
            for (int x = 0; x < cellW; x++)
                if (gp.rgba[(static_cast<size_t>(sy0 + y) * gp.width + sx0 + x) * 4 + 3] != 0) {
                    first = std::min(first, x);
                    last = std::max(last, x);
                }
        if (last < first)
            return nullptr;
        int cx = 0, cy = 0;
        if (!ReserveGlyphCell(cellW, cellH, cx, cy))
            return nullptr;

        for (int y = 0; y < cellH; y++)
            std::memcpy(&atlas[(static_cast<size_t>(cy + y) * atlasWidth + cx) * 4],
                        &gp.rgba[(static_cast<size_t>(sy0 + y) * gp.width + sx0) * 4],
                        static_cast<size_t>(cellW) * 4);
        Glyph g;

        const bool nativeSize = cp >= 0xe000 && cp <= 0xf8ff;
        const float unit = nativeSize ? 1.0f : 8.0f / static_cast<float>(cellW);
        g.width = nativeSize ? static_cast<float>(cellW) : 8.0f;
        g.height = g.width;
        g.advance = static_cast<float>(last - first + 1) * unit + 1.0f;
        g.left = static_cast<float>(1 - first) * unit;
        g.top = nativeSize ? (8.0f - g.height) * 0.5f : 0.0f;
        g.colored = nativeSize;
        g.cell = kCell;
        g.u0 = static_cast<float>(cx) / static_cast<float>(atlasWidth);
        g.v0 = static_cast<float>(cy) / static_cast<float>(atlasHeight);
        g.u1 = static_cast<float>(cx + cellW) / static_cast<float>(atlasWidth);
        g.v1 = static_cast<float>(cy + cellH) / static_cast<float>(atlasHeight);
        version++;
        return &dyn.emplace(cp, g).first->second;
    }

    const McFont::Glyph* McFont::Dynamic(uint32_t cp) const {
        if (const auto it = dyn.find(cp); it != dyn.end())
            return &it->second;
        if (const Glyph* g = FromGlyphPage(cp))
            return g;
        if (const Glyph* g = FromSheet(cp))
            return g;
        if (!ttfs.empty()) {
            const int perRow = atlasWidth / 16;
            const int rows = (dynLimitY - dynY0) / 16;
            if (dynNext < perRow * rows) {
                const int cx = (dynNext % perRow) * 16, cy = dynY0 + (dynNext / perRow) * 16;
                Glyph g;
                if (RasterTtf(cp, cx, cy, g)) {
                    dynNext++;
                    g.cell = kCell;
                    g.u0 = static_cast<float>(cx) / static_cast<float>(atlasWidth);
                    g.v0 = static_cast<float>(cy) / static_cast<float>(atlasHeight);
                    g.u1 = static_cast<float>(cx + g.width * 2) / static_cast<float>(atlasWidth);
                    g.v1 = static_cast<float>(cy + 16) / static_cast<float>(atlasHeight);
                    version++;
                    return &dyn.emplace(cp, g).first->second;
                }
            }
        }
        const auto src = hex.find(cp);
        if (src == hex.end())
            return nullptr;
        const int perRow = atlasWidth / 16;
        const int rows = (dynLimitY - dynY0) / 16;
        if (dynNext >= perRow * rows)
            return nullptr;
        const int cx = (dynNext % perRow) * 16, cy = dynY0 + (dynNext / perRow) * 16;
        dynNext++;
        const HexGlyph& hg = src->second;
        const int bytesPerRow = hg.wide ? 2 : 1;
        int ink = 0;
        for (int row = 0; row < 16; row++) {
            for (int col = 0; col < bytesPerRow * 8; col++) {
                const uint8_t byte = hg.rows[static_cast<size_t>(row * bytesPerRow + col / 8)];
                if (byte & (0x80 >> (col % 8))) {
                    uint8_t* px = &atlas[(static_cast<size_t>(cy + row) * static_cast<size_t>(atlasWidth) +
                                          static_cast<size_t>(cx + col)) *
                                         4];
                    px[0] = px[1] = px[2] = px[3] = 255;
                    ink = std::max(ink, col + 1);
                }
            }
        }
        Glyph g;
        if (ink == 0) {
            g.width = 0;
            g.advance = 4;
        } else {
            g.width = (ink + 1) / 2;
            g.advance = g.width + 1;
        }
        g.cell = kCell;
        g.u0 = static_cast<float>(cx) / static_cast<float>(atlasWidth);
        g.v0 = static_cast<float>(cy) / static_cast<float>(atlasHeight);
        g.u1 = static_cast<float>(cx + g.width * 2) / static_cast<float>(atlasWidth);
        g.v1 = static_cast<float>(cy + 16) / static_cast<float>(atlasHeight);
        version++;
        return &dyn.emplace(cp, g).first->second;
    }

    const McFont::Glyph& McFont::Get(uint32_t cp) const {

        if (cp == '\n' || cp == '\r') {
            static const Glyph empty{};
            return empty;
        }
        if (cp == '\t')
            return glyphs[' '];
        if (cp < glyphs.size() && (glyphs[cp].advance > 0 || cp == ' '))
            return glyphs[cp];
        if (cp == ' ')
            return glyphs[' '];
        if (cp >= 0x80) {
            if (glyphPages.contains(cp >> 8))
                if (const Glyph* g = Dynamic(cp))
                    return *g;
            if (const auto it = mapped.find(cp); it != mapped.end())
                return it->second;
            if (const Glyph* g = Dynamic(cp))
                return *g;
        }
        return fallback;
    }

    uint32_t McFont::Obfuscated(uint32_t codepoint) const {
        const float advance = Get(codepoint).advance;
        static uint32_t state = 0x2545F491u;
        uint32_t same[95];
        int count = 0;
        for (uint32_t c = 33; c < 127; c++)
            if (Get(c).advance == advance)
                same[count++] = c;
        if (count == 0)
            return codepoint;
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return same[state % static_cast<uint32_t>(count)];
    }

    float McFont::Width(const std::string& s, float scale) const {
        float width = 0;
        bool bold = false;
        size_t i = 0;
        uint32_t cp;
        while (Next(s, i, cp)) {
            if (cp == 0xA7) {
                uint32_t code;
                if (Next(s, i, code)) {
                    if (code == 'l' || code == 'L')
                        bold = true;
                    else if (code == 'r' || code == 'R')
                        bold = false;
                }
                continue;
            }
            width += static_cast<float>(Get(cp).advance + (bold ? 1 : 0));
        }
        return width * scale;
    }
}
