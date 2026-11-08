#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::ui {
    class TenFont {
    public:

        static constexpr float kEm = 10.0f;

        static TenFont& Instance();

        bool Load();

        void EnsureLoaded();
        bool Available() const { return ready; }
        const std::string& Path() const { return path; }

        float Em(float scale) const { return kEm * scale; }
        float Ascent(float scale) const;
        float Descent(float scale) const;
        float XHeight(float scale) const;
        float LineHeight(float scale) const { return 9.0f * scale; }
        float Advance(uint32_t cp, float scale) const;
        float Width(const std::string& utf8, float scale) const;

        struct Glyph {
            float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
            int x = 0, y = 0, w = 0, h = 0;
        };

        struct Page {
            int emPx = 0;
            int width = 0, height = 0;
            uint32_t version = 0;
            std::vector<uint8_t> rgba;
            std::unordered_map<uint32_t, Glyph> glyphs;
        };
        const Page* PageFor(int emPx);
        const Page* FindPage(int emPx) const;

    private:
        struct Impl;
        bool LoadLocked();
        std::shared_ptr<Impl> impl;
        std::string path;
        std::atomic<bool> ready{false};
        std::atomic<bool> attempted{false};
        bool tried = false;
        mutable std::mutex mutex;
        std::unordered_map<int, std::unique_ptr<Page>> pages;
        uint32_t pageVersion = 0;
    };

    bool IsTenFontType(const std::string& fontType);
}
