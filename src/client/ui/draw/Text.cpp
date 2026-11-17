#include "client/ui/draw/Text.hpp"

#include "client/systems/text/McFont.hpp"
#include "client/ui/TenFont.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace perlin::ui::draw {
    Face FaceFromFamily(const std::string& family, float size, bool bold, bool italic) {
        Face face;
        face.ten = family.find("ten") != std::string::npos;
        face.shadow = family.find("shadow") != std::string::npos;
        face.outline = family.find("outline") != std::string::npos;
        face.bold = bold;
        face.italic = italic;
        face.size = std::max(size, 1.0f);
        if (const size_t at = family.rfind("-size-"); at != std::string::npos) {
            const float requested = std::strtof(family.c_str() + at + 6, nullptr);
            if (std::isfinite(requested) && requested > 0 && requested <= 4096)
                face.size = requested;
        }
        return face;
    }

    FontMetrics MetricsFor(const Face& face) {
        FontMetrics m;
        if (face.ten) {
            TenFont& ten = TenFont::Instance();
            ten.EnsureLoaded();
            const float s = face.size / TenFont::kEm;
            m.ascent = ten.Ascent(s);
            m.descent = ten.Descent(s);
            m.line = ten.LineHeight(s);
            return m;
        }
        const float s = face.size / 8.0f;
        m.ascent = 7.0f * s;
        m.descent = 2.0f * s;
        m.line = 9.0f * s;
        return m;
    }

    int StringWidth(const Face& face, const std::string& text) {
        if (face.ten) {
            TenFont& ten = TenFont::Instance();
            ten.EnsureLoaded();
            return static_cast<int>(std::lround(ten.Width(text, face.size / TenFont::kEm)));
        }
        const float s = face.size / 8.0f;
        const McFont& font = McFont::Instance();
        float width = 0;
        size_t i = 0;
        uint32_t cp;
        while (McFont::Next(text, i, cp))
            width += static_cast<float>(font.Get(cp).advance + (face.bold ? 1 : 0));
        return static_cast<int>(std::lround(width * s));
    }

    namespace {
        uint32_t Shade(uint32_t p) {
            const uint32_t r = (p & 0xFF) / 4, g = ((p >> 8) & 0xFF) / 4, b = ((p >> 16) & 0xFF) / 4;
            return (p & 0xFF000000u) | (b << 16) | (g << 8) | r;
        }

        uint32_t Black(uint32_t p) { return p & 0xFF000000u; }

        uint32_t Ink(uint32_t p, bool colored) {
            if (!colored)
                return p;
            const uint32_t a = p >> 24;
            return (a << 24) | (a << 16) | (a << 8) | a;
        }

        float EmitBitmap(const Face& f, const std::string& text, float x, float baseline, uint32_t main,
                         TextSink& sink) {
            const float s = f.size / 8.0f;
            const McFont& font = McFont::Instance();
            size_t gi = 0;
            uint32_t gc;
            while (McFont::Next(text, gi, gc))
                font.Get(gc);
            const uint32_t texture = sink.Texture("mcfont:atlas#" + std::to_string(font.Version()));
            const float device = static_cast<float>(std::max(1, sink.Device()));
            const float kGeo = std::max(1.0f, std::round(s * device)) / device;
            const auto snap = [&](float v) { return std::round(v * device) / device; };
            const auto quads = [&](float ox, float oy, uint32_t c) {
                float pen = 0;
                size_t i = 0;
                uint32_t cp;
                while (McFont::Next(text, i, cp)) {
                    const McFont::Glyph& g = font.Get(cp);
                    if (g.width > 0) {
                        const float gx = g.colored ? snap(x + pen + ox + g.left * kGeo) : x + pen + ox + g.left * kGeo;
                        const float gy = (g.colored ? snap(baseline - 7.0f * kGeo + oy) : baseline - 7.0f * kGeo + oy) +
                                         g.top * kGeo;
                        const float w = g.width * kGeo, h = g.height * kGeo;
                        const uint32_t ink = Ink(c, g.colored);
                        for (int pass = 0; pass < (f.bold ? 2 : 1); pass++) {
                            const float bx = gx + pass * kGeo;
                            if (!f.italic) {
                                sink.Quad(texture, bx, gy, w, h, g.u0, g.v0, g.u1, g.v1, ink);
                                continue;
                            }
                            const float top = (1.0f - 0.25f * g.top) * kGeo;
                            const float bottom = (1.0f - 0.25f * (g.top + g.height)) * kGeo;
                            const float xy[8] = {bx + top, gy, bx + w + top, gy, bx + w + bottom, gy + h, bx + bottom, gy + h};
                            const float uv[8] = {g.u0, g.v0, g.u1, g.v0, g.u1, g.v1, g.u0, g.v1};
                            sink.Skewed(texture, xy, uv, ink);
                        }
                    }
                    pen += static_cast<float>(g.advance + (f.bold ? 1 : 0)) * kGeo;
                }
                return pen;
            };
            if (f.shadow)
                quads(kGeo, kGeo, Shade(main));
            if (f.outline) {
                const uint32_t black = Black(main);
                quads(kGeo, 0, black);
                quads(-kGeo, 0, black);
                quads(0, kGeo, black);
                quads(0, -kGeo, black);
            }
            return quads(0, 0, main);
        }

        float EmitTen(const Face& f, const std::string& text, float x, float baseline, uint32_t main,
                      TextSink& sink) {
            TenFont& ten = TenFont::Instance();
            ten.EnsureLoaded();
            const float s = f.size / TenFont::kEm;
            const float device = static_cast<float>(std::max(1, sink.Device()));
            const int emPx = std::clamp(static_cast<int>(std::lround(f.size * device)), 4, 160);
            const TenFont::Page* page = ten.PageFor(emPx);
            if (!page || page->rgba.empty())
                return 0;
            const float k = f.size / static_cast<float>(page->emPx);
            size_t gi = 0;
            uint32_t gc;
            while (McFont::Next(text, gi, gc))
                if (!page->glyphs.contains(gc))
                    McFont::Instance().Get(gc);
            const uint32_t texture = sink.Texture("mcten:atlas#" + std::to_string(emPx));
            const uint32_t fallback = sink.Texture("mcfont:atlas#" + std::to_string(McFont::Instance().Version()));
            const auto snap = [&](float v) { return std::round(v * device) / device; };
            const auto quads = [&](float ox, float oy, uint32_t c) {
                const float baseX = snap(x + ox);
                const float baseY = snap(baseline + oy);
                float pen = 0;
                size_t i = 0;
                uint32_t cp;
                while (McFont::Next(text, i, cp)) {
                    const auto it = page->glyphs.find(cp);
                    if (it != page->glyphs.end()) {
                        const TenFont::Glyph& g = it->second;
                        if (g.w > 0 && g.h > 0)
                            sink.Quad(texture, baseX + pen + static_cast<float>(g.x) * k,
                                      baseY + static_cast<float>(g.y) * k, static_cast<float>(g.w) * k,
                                      static_cast<float>(g.h) * k, g.u0, g.v0, g.u1, g.v1, c);
                        pen += ten.Advance(cp, s);
                        continue;
                    }
                    const McFont::Glyph& mg = McFont::Instance().Get(cp);
                    if (mg.width > 0) {
                        const float ks = s * (TenFont::kEm / 8.0f);
                        sink.Quad(fallback, baseX + pen + mg.left * ks, baseY - 7.0f * ks + mg.top * ks,
                                  static_cast<float>(mg.width) * ks, static_cast<float>(mg.height) * ks, mg.u0,
                                  mg.v0, mg.u1, mg.v1, Ink(c, mg.colored));
                        pen += static_cast<float>(mg.advance) * ks;
                        continue;
                    }
                    pen += ten.Advance(cp, s);
                }
                return pen;
            };
            if (f.shadow)
                quads(k, k, Shade(main));
            if (f.outline) {
                const uint32_t black = Black(main);
                quads(k, 0, black);
                quads(-k, 0, black);
                quads(0, k, black);
                quads(0, -k, black);
            }
            return quads(0, 0, main);
        }
    }

    float EmitText(const Face& face, const std::string& text, float x, float baseline, Color color, TextSink& sink) {
        if (text.empty())
            return 0;
        const uint32_t main = PackPremultiplied(color);
        return face.ten ? EmitTen(face, text, x, baseline, main, sink) : EmitBitmap(face, text, x, baseline, main, sink);
    }

    uint32_t FontStamp() { return McFont::Instance().Version(); }
}
