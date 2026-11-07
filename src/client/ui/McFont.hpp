#pragma once
#include <array>
#include <memory>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::ui {
    class McFont {
    public:
        struct Glyph {
            float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
            float width = 0;
            float advance = 0;
            float left = 0;
            bool colored = false;
            int cell = 8;
            float top = 0;
            float height = 8;
        };

        McFont();
        static McFont& Instance();

        bool LoadSheet(const uint8_t* rgba, int width, int height);
        void UseBuiltin();
        bool LoadUnihexFile(const std::string& path);
        struct RawSheet {
            const uint8_t* rgba = nullptr;
            int width = 0, height = 0;
        };
        bool LoadVanillaSheets(const RawSheet& accented, const RawSheet& nonlatin);
        bool HasVanillaSheets() const { return !sheets.empty(); }
        bool HasUnihex() const { return !hex.empty(); }
        bool AddTtf(const std::string& path);
        // bedrock glyph page (font/glyph_XX.png from a resource pack): 16x16 grid of U+XX00..U+XXFF in its own colours;
        // replaces any page already loaded for `page`
        void AddGlyphPage(uint32_t page, const std::vector<uint8_t>& rgba, int width, int height);
        void ClearGlyphPages();
        void UnloadLevelResources();
        size_t TtfCount() const { return ttfs.size(); }

        const Glyph& Get(uint32_t codepoint) const;
        float Width(const std::string& utf8, float scale = 1.0f) const;
        // §k: a random printable ascii character of the same advance, picked anew on every call
        uint32_t Obfuscated(uint32_t codepoint) const;
        static constexpr int kLineHeight = 9;
        static constexpr int kCell = 8;

        const std::vector<uint8_t>& Atlas() const { return atlas; }
        int AtlasWidth() const { return atlasWidth; }
        int AtlasHeight() const { return atlasHeight; }
        uint32_t Version() const { return version; }
        bool Builtin() const { return builtin; }

        static bool Next(const std::string& s, size_t& i, uint32_t& cp);

    private:
        void Build(const std::vector<uint8_t>& cells, int cellsAcross, int cellPx);
        const Glyph* Dynamic(uint32_t cp) const;
        const Glyph* FromSheet(uint32_t cp) const;
        struct TtfFont;
        bool RasterTtf(uint32_t cp, int cx, int cy, Glyph& out) const;
        std::vector<std::shared_ptr<TtfFont>> ttfs;
        std::vector<std::string> ttfPaths;
        struct HexGlyph {
            std::array<uint8_t, 32> rows{};
            bool wide = false;
        };
        struct Sheet {
            std::vector<uint8_t> rgba;
            int width = 0, height = 0, cellW = 0, cellH = 0, ascent = 7;
        };
        std::vector<Sheet> sheets;
        struct GlyphPage {
            std::vector<uint8_t> rgba;
            int width = 0, height = 0;
        };
        std::unordered_map<uint32_t, GlyphPage> glyphPages;
        const Glyph* FromGlyphPage(uint32_t cp) const;
        std::unordered_map<uint32_t, std::pair<uint16_t, uint16_t>> sheetChars;
        std::unordered_map<uint32_t, HexGlyph> hex;
        std::string hexPath;
        std::unordered_map<uint32_t, Glyph> mapped;
        mutable std::unordered_map<uint32_t, Glyph> dyn;
        bool ReserveGlyphCell(int w, int h, int& x, int& y) const;
        mutable int glyphShelfX = 0, glyphShelfY = 0, glyphShelfHeight = 0, dynLimitY = 0;
        mutable int dynNext = 0;
        int dynY0 = 0;
        mutable uint32_t dynVersion = 0;
        mutable std::vector<uint8_t> atlas;
        int atlasWidth = 0, atlasHeight = 0;
        std::vector<Glyph> glyphs;
        Glyph fallback;
        mutable uint32_t version = 1;
        bool builtin = true;
    };
}
