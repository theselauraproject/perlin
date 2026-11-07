#include "client/ui/TenFont.hpp"

#include "client/ui/McFont.hpp"

#include <stb_truetype.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include "util/Log.hpp"

namespace perlin::ui {
    namespace fs = std::filesystem;

    struct TenFont::Impl {
        std::vector<unsigned char> data;
        stbtt_fontinfo info{};
        float ascent = 0, descent = 0;
    };

    namespace {
        // bedrock installs under `base`: <base>/<a>/<b>/.../resource_packs/vanilla/font/
        void Collect(const fs::path& base, int depth, std::vector<fs::path>& out) {
            std::error_code ec;
            if (depth < 0 || !fs::is_directory(base, ec))
                return;
            const fs::path candidate = base / "resource_packs" / "vanilla" / "font" / "minecraft-ten.ttf";
            if (fs::is_regular_file(candidate, ec)) {
                out.push_back(candidate);
                return;
            }
            for (fs::directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec))
                if (it->is_directory(ec))
                    Collect(it->path(), depth - 1, out);
        }

        std::vector<fs::path> Candidates() {
            std::vector<fs::path> out;
            if (const char* env = std::getenv("PERLIN_TEN_FONT"); env && *env)
                out.emplace_back(env);
            const char* home = std::getenv("HOME");
            if (!home)
                return out;
            const fs::path h(home);
            std::vector<fs::path> found;
            // bedrock on linux: games/<channel>/<version>/data/...
            Collect(h / ".local/share/bedrock-on-linux/games", 4, found);
            // mcpelauncher: versions/<version>/assets/...
            Collect(h / ".local/share/mcpelauncher/versions", 3, found);
            Collect(h / ".var/app/io.mrarm.mcpelauncher/data/mcpelauncher/versions", 3, found);
            // newest first: dotted version paths sort later
            std::sort(found.begin(), found.end(),
                      [](const fs::path& a, const fs::path& b) { return a.string() > b.string(); });
            out.insert(out.end(), found.begin(), found.end());
            return out;
        }

        uint32_t NextCodepoint(const std::string& s, size_t& i) {
            uint32_t cp = 0;
            if (!McFont::Next(s, i, cp))
                return 0;
            return cp;
        }
    }

    bool IsTenFontType(const std::string& fontType) {
        std::string lower;
        for (char c : fontType)
            if (c != '_' && c != ' ' && c != '-')
                lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return lower == "minecraftten";
    }

    TenFont& TenFont::Instance() {
        static TenFont font;
        return font;
    }

    bool TenFont::Load() {
        std::lock_guard lock(mutex);
        return LoadLocked();
    }

    void TenFont::EnsureLoaded() {
        if (attempted.load(std::memory_order_acquire))
            return;
        std::lock_guard lock(mutex);
        LoadLocked();
    }

    bool TenFont::LoadLocked() {
        if (ready || tried) {
            attempted.store(true, std::memory_order_release);
            return ready;
        }
        tried = true;
        attempted.store(true, std::memory_order_release);
        for (const fs::path& p : Candidates()) {
            std::ifstream in(p, std::ios::binary);
            if (!in)
                continue;
            auto loaded = std::make_shared<Impl>();
            loaded->data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            const int offset = stbtt_GetFontOffsetForIndex(loaded->data.data(), 0);
            if (offset < 0 || !stbtt_InitFont(&loaded->info, loaded->data.data(), offset))
                continue;
            int asc = 0, desc = 0, gap = 0;
            stbtt_GetFontVMetrics(&loaded->info, &asc, &desc, &gap);
            const float em = 1.0f / stbtt_ScaleForMappingEmToPixels(&loaded->info, 1.0f);
            if (em > 0.0f) {
                loaded->ascent = static_cast<float>(asc) / em;
                loaded->descent = static_cast<float>(-desc) / em;
            }
            impl = std::move(loaded);
            path = p.string();
            ready = true;
            Log("ui", "MinecraftTen font from the Bedrock install: {}", path);
            return true;
        }
        Log("ui",
            "no Bedrock install with minecraft-ten.ttf found; MinecraftTen labels use the regular font");
        return false;
    }

    float TenFont::Ascent(float scale) const {
        return impl ? impl->ascent * Em(scale) : 0.0f;
    }
    float TenFont::Descent(float scale) const {
        return impl ? impl->descent * Em(scale) : 0.0f;
    }

    float TenFont::XHeight(float scale) const {
        if (!impl)
            return 0.0f;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        if (!stbtt_GetCodepointBox(&impl->info, 'x', &x0, &y0, &x1, &y1))
            return 0.0f;
        return static_cast<float>(y1 - y0) * stbtt_ScaleForMappingEmToPixels(&impl->info, Em(scale));
    }

    float TenFont::Advance(uint32_t cp, float scale) const {
        if (!impl)
            return 0.0f;
        if (cp < 32 || cp >= 0x180 || stbtt_FindGlyphIndex(&impl->info, static_cast<int>(cp)) == 0)
            return static_cast<float>(McFont::Instance().Get(cp).advance) * Em(scale) / 8.0f;
        int advance = 0, bearing = 0;
        stbtt_GetCodepointHMetrics(&impl->info, static_cast<int>(cp), &advance, &bearing);
        return static_cast<float>(advance) * stbtt_ScaleForMappingEmToPixels(&impl->info, Em(scale));
    }

    float TenFont::Width(const std::string& utf8, float scale) const {
        float width = 0.0f;
        size_t i = 0;
        bool skip = false;
        while (i < utf8.size()) {
            const uint32_t cp = NextCodepoint(utf8, i);
            if (cp == 0)
                break;
            // format codes (section sign + char) take no space
            if (skip) {
                skip = false;
                continue;
            }
            if (cp == 0xA7) {
                skip = true;
                continue;
            }
            float adv = Advance(cp, scale);
            // server icons have no heading-face advance: measure the bitmap fallback the draw uses
            if (adv == 0.0f && cp != ' ')
                adv = static_cast<float>(McFont::Instance().Get(cp).advance) * scale * (kEm / 8.0f);
            width += adv;
        }
        return width;
    }

    const TenFont::Page* TenFont::FindPage(int emPx) const {
        std::lock_guard lock(mutex);
        const auto it = pages.find(emPx);
        return it == pages.end() ? nullptr : it->second.get();
    }

    const TenFont::Page* TenFont::PageFor(int emPx) {
        if (!impl || emPx <= 0)
            return nullptr;
        emPx = std::min(emPx, 160);
        {
            std::lock_guard lock(mutex);
            if (const auto it = pages.find(emPx); it != pages.end())
                return it->second.get();
        }
        auto page = std::make_unique<Page>();
        page->emPx = emPx;
        page->version = ++pageVersion;
        const float scale = stbtt_ScaleForMappingEmToPixels(&impl->info, static_cast<float>(emPx));

        // latin, latin-1, latin extended-a: what servers use in headings
        std::vector<uint32_t> codepoints;
        for (uint32_t cp = 32; cp < 0x180; cp++)
            if (stbtt_FindGlyphIndex(&impl->info, static_cast<int>(cp)) != 0)
                codepoints.push_back(cp);

        struct Raster {
            uint32_t cp;
            int x0, y0, w, h;
            std::vector<unsigned char> alpha;
        };
        std::vector<Raster> rasters;
        int cellW = 1, cellH = 1;
        for (uint32_t cp : codepoints) {
            Raster r{cp, 0, 0, 0, 0, {}};
            int x1 = 0, y1 = 0;
            stbtt_GetCodepointBitmapBox(&impl->info, static_cast<int>(cp), scale, scale, &r.x0, &r.y0, &x1,
                                        &y1);
            r.w = std::max(0, x1 - r.x0);
            r.h = std::max(0, y1 - r.y0);
            if (r.w > 0 && r.h > 0) {
                r.alpha.resize(static_cast<size_t>(r.w) * static_cast<size_t>(r.h));
                stbtt_MakeCodepointBitmap(&impl->info, r.alpha.data(), r.w, r.h, r.w, scale, scale,
                                          static_cast<int>(cp));
            }
            cellW = std::max(cellW, r.w + 2);
            cellH = std::max(cellH, r.h + 2);
            rasters.push_back(std::move(r));
        }
        const int across =
            std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(rasters.size())))));
        page->width = across * cellW;
        page->height = ((static_cast<int>(rasters.size()) + across - 1) / across) * cellH;
        page->height = std::max(page->height, 1);
        page->rgba.assign(static_cast<size_t>(page->width) * static_cast<size_t>(page->height) * 4, 0);
        for (size_t i = 0; i < rasters.size(); i++) {
            const Raster& r = rasters[i];
            const int cx = static_cast<int>(i % static_cast<size_t>(across)) * cellW + 1;
            const int cy = static_cast<int>(i / static_cast<size_t>(across)) * cellH + 1;
            for (int y = 0; y < r.h; y++)
                for (int x = 0; x < r.w; x++) {
                    // heading face stays smooth: keep truetype coverage as-is
                    const unsigned char a =
                        r.alpha[static_cast<size_t>(y) * static_cast<size_t>(r.w) + static_cast<size_t>(x)];
                    const uint8_t v = a;
                    uint8_t* px =
                        &page->rgba[(static_cast<size_t>(cy + y) * static_cast<size_t>(page->width) +
                                     static_cast<size_t>(cx + x)) *
                                    4];
                    px[0] = px[1] = px[2] = 255;
                    px[3] = v;
                }
            Glyph g;
            g.x = r.x0;
            g.y = r.y0;
            g.w = r.w;
            g.h = r.h;
            g.u0 = static_cast<float>(cx) / static_cast<float>(page->width);
            g.v0 = static_cast<float>(cy) / static_cast<float>(page->height);
            g.u1 = static_cast<float>(cx + r.w) / static_cast<float>(page->width);
            g.v1 = static_cast<float>(cy + r.h) / static_cast<float>(page->height);
            page->glyphs.emplace(r.cp, g);
        }
        std::lock_guard lock(mutex);
        auto& slot = pages[emPx];
        if (!slot)
            slot = std::move(page);
        return slot.get();
    }
}
