#include "client/resources/TexturePack.hpp"
#include "util/Resources.hpp"
#include "client/resources/TextureLoader.hpp"
#include "client/ui/Glint.hpp"
#include <chrono>
#include <algorithm>
#include "client/ui/RmlBackend.hpp"
#include "client/ui/JavaWidgets.hpp"
#include "client/ui/FormScreen.hpp"

#include "client/renderer/RenderContext.hpp"
#include "client/ui/McFont.hpp"
#include "client/ui/TenFont.hpp"
#include "client/ui/UiAssets.hpp"
#include "client/ui/UiSprite.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include "util/Log.hpp"

namespace perlin::ui {
    namespace {
        RenderContext* gRenderer = nullptr;
        int gScale = 1;
        bool gMeshStarved = false;
        UiLayer gLayer = UiLayer::Gui;
        bool gGlintPass = false;
        uint32_t gGlintTexture = 0;
        int gGlintWidth = 64;

        uint32_t GlintTexture() {
            if (gGlintTexture == 0 && gRenderer)
                gGlintTexture = CreateGlintTexture(*gRenderer, &gGlintWidth);
            return gGlintTexture;
        }

        uint32_t Pack(const Rml::ColourbPremultiplied& c) {
            return (static_cast<uint32_t>(c.alpha) << 24) | (static_cast<uint32_t>(c.blue) << 16) |
                   (static_cast<uint32_t>(c.green) << 8) | static_cast<uint32_t>(c.red);
        }

        Rml::ColourbPremultiplied Colour(float r, float g, float b, float a) {
            const auto byte = [](float v) {
                return static_cast<Rml::byte>(std::clamp(static_cast<int>(std::lround(v * 255.0f)), 0, 255));
            };
            return Rml::ColourbPremultiplied(byte(r * a), byte(g * a), byte(b * a), byte(a));
        }
    }

    class BgfxRenderInterface final : public Rml::RenderInterface {
    public:
        static constexpr Rml::CompiledGeometryHandle kEmptyGeometry =
            ~static_cast<Rml::CompiledGeometryHandle>(0);

        Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                    Rml::Span<const int> indices) override {
            std::vector<UiVertex> v(vertices.size());
            for (size_t i = 0; i < vertices.size(); i++) {
                v[i] = {vertices[i].position.x,   vertices[i].position.y,  0.0f,
                        Pack(vertices[i].colour), vertices[i].tex_coord.x, vertices[i].tex_coord.y};
            }
            std::vector<uint32_t> idx(indices.size());
            for (size_t i = 0; i < indices.size(); i++)
                idx[i] = static_cast<uint32_t>(indices[i]);
            const uint32_t mesh = gRenderer->UiCreateMesh(v.data(), static_cast<uint32_t>(v.size()),
                                                          idx.data(), static_cast<uint32_t>(idx.size()));
            if (!mesh && !v.empty() && !idx.empty())
                gMeshStarved = true;
            return mesh ? static_cast<Rml::CompiledGeometryHandle>(mesh) : kEmptyGeometry;
        }

        void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                            Rml::TextureHandle texture) override {
            if (geometry == kEmptyGeometry)
                return;
            UiDraw draw;
            draw.mesh = static_cast<uint32_t>(geometry);
            draw.texture = static_cast<uint32_t>(texture);
            const float s = static_cast<float>(gScale);
            draw.model[0] = s;
            draw.model[5] = s;
            draw.model[10] = 1.0f;
            draw.model[12] = std::round(translation.x * s);
            draw.model[13] = std::round(translation.y * s);
            draw.scissor = scissorOn;
            for (int i = 0; i < 4; i++)
                draw.scissorRect[i] = scissor[i];
            draw.premultiplied = true;
            if (gGlintPass) {
                draw.glintTexture = GlintTexture();
                if (draw.glintTexture == 0)
                    return;
                const auto ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                          std::chrono::steady_clock::now().time_since_epoch())
                                                          .count());
                const GlintTransform g = MakeGlintTransform(ms);
                std::copy(g.matrix, g.matrix + 4, draw.glintMatrix);
                std::copy(g.offset, g.offset + 2, draw.glintOffset);
            }
            gRenderer->UiSubmit(gLayer, draw);
        }

        void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override {
            if (geometry == kEmptyGeometry)
                return;
            gRenderer->UiDestroyMesh(static_cast<uint32_t>(geometry));
        }

        Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
            Texture t;
            if (source == "mc:white") {
                t.width = t.height = 1;
                t.pixels = {255, 255, 255, 255};
            } else if (source.starts_with("mcfont:")) {
                const McFont& f = McFont::Instance();
                t.width = f.AtlasWidth();
                t.height = f.AtlasHeight();
                t.pixels = f.Atlas();
            } else if (source.starts_with("mcten:atlas#")) {
                const int emPx = std::atoi(source.c_str() + 12);
                const TenFont::Page* page = TenFont::Instance().PageFor(emPx);
                if (!page || page->rgba.empty())
                    return 0;
                t.width = page->width;
                t.height = page->height;
                t.pixels = page->rgba;
            } else if (source.starts_with("mcres:")) {
                std::string bytes;
                std::string stem = source.substr(6);
                bool fromPack = false;
                if (TexturePack::Instance().Active() && stem.starts_with("ui_")) {
                    std::string name = stem;
                    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".png") == 0)
                        name.resize(name.size() - 4);
                    if (name.starts_with("ui_menu_btn") || name == "ui_menu_logo") {

                        fromPack = PackSpriteNative(name + ".png", t);
                    } else {

                        std::string baseBytes;
                        Texture base;
                        if (resources::Read(stem, baseBytes) &&
                            LoadTextureMemory(reinterpret_cast<const unsigned char*>(baseBytes.data()),
                                              baseBytes.size(), base))
                            fromPack = PackSpriteFor(name + ".png", base.width, base.height, t);
                    }
                }
                if (!fromPack && (!resources::Read(source.substr(6), bytes) ||
                                  !LoadTextureMemory(reinterpret_cast<const unsigned char*>(bytes.data()),
                                                     bytes.size(), t)))
                    return 0;
            } else if (source.starts_with("mcurl:")) {
                std::string bytes;
                if (!FormImageBytes(source.substr(6), bytes) ||
                    !LoadTextureMemory(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), t))
                    return 0;
            } else if (source.starts_with("mchead:")) {
                if (!UiAssets::Instance().Head(source.substr(7), t))
                    return 0;
            } else if (source.starts_with("mcicon:")) {
                if (!UiAssets::Instance().ItemIcon(source.substr(7), t))
                    return 0;
            } else if (!UiAssets::Instance().Image(source, t)) {
                std::string tail = source;
                const size_t slash = tail.find_last_of("/\\:");
                if (slash != std::string::npos)
                    tail.erase(0, slash + 1);
                for (char& c : tail)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (tail == "null" || tail == "null.png" || tail == "null.tga")
                    return 0;
                Log("ui", "missing texture {}", source);
                t.width = t.height = 2;
                t.pixels = {248, 0, 248, 255, 0, 0, 0, 255, 0, 0, 0, 255, 248, 0, 248, 255};
            }
            dimensions = {t.width, t.height};
            Premultiply(t);
            return Upload(t);
        }

        Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                           Rml::Vector2i dimensions) override {
            Texture t;
            t.width = dimensions.x;
            t.height = dimensions.y;
            t.pixels.assign(source.begin(), source.end());
            return Upload(t);
        }

        void ReleaseTexture(Rml::TextureHandle texture) override {
            gRenderer->UiDestroyTexture(static_cast<uint32_t>(texture));
        }

        void EnableScissorRegion(bool enable) override { scissorOn = enable; }

        void SetScissorRegion(Rml::Rectanglei region) override {
            scissor[0] = region.Left() * gScale;
            scissor[1] = region.Top() * gScale;
            scissor[2] = region.Width() * gScale;
            scissor[3] = region.Height() * gScale;
        }

    private:
        static void Premultiply(Texture& t) {
            for (size_t i = 0; i + 3 < t.pixels.size(); i += 4) {
                const unsigned a = t.pixels[i + 3];
                if (a == 255)
                    continue;
                t.pixels[i] = static_cast<unsigned char>((t.pixels[i] * a + 127) / 255);
                t.pixels[i + 1] = static_cast<unsigned char>((t.pixels[i + 1] * a + 127) / 255);
                t.pixels[i + 2] = static_cast<unsigned char>((t.pixels[i + 2] * a + 127) / 255);
            }
        }

        static Rml::TextureHandle Upload(const Texture& t) {
            return static_cast<Rml::TextureHandle>(
                gRenderer->UiCreateTexture(t.width, t.height, t.pixels.data(), false));
        }

        bool scissorOn = false;
        int scissor[4] = {0, 0, 0, 0};
    };

    class GameSystemInterface final : public Rml::SystemInterface {
    public:
        double GetElapsedTime() override {
            using Clock = std::chrono::steady_clock;
            static const auto start = Clock::now();
            return std::chrono::duration<double>(Clock::now() - start).count();
        }

        void JoinPath(Rml::String& translated, const Rml::String&, const Rml::String& path) override {
            translated = path;
        }

        bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
            if (type > Rml::Log::LT_WARNING)
                return true;
            static std::unordered_map<std::string, uint64_t> seen;
            const uint64_t n = ++seen[message];
            if (n <= 3 || n % 100000 == 0)
                Log("rml", "{}{}", message,
                    (n > 3 ? " (repeated " + std::to_string(n) + " times)" : std::string()));
            return true;
        }
    };

    class McFontEngine final : public Rml::FontEngineInterface {
    public:
        bool LoadFontFace(const Rml::String&, int, bool, Rml::Style::FontWeight) override { return true; }
        bool LoadFontFace(Rml::Span<const Rml::byte>, int, const Rml::String&, Rml::Style::FontStyle,
                          Rml::Style::FontWeight, bool) override {
            return true;
        }

        Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style,
                                              Rml::Style::FontWeight weight, int size) override {
            Face face;
            face.ten = family.find("ten") != Rml::String::npos;
            face.shadow = family.find("shadow") != Rml::String::npos;
            face.outline = family.find("outline") != Rml::String::npos;
            face.bold = weight == Rml::Style::FontWeight::Bold;
            face.italic = style == Rml::Style::FontStyle::Italic;
            face.size = static_cast<float>(std::max(size, 1));
            if (const size_t at = family.rfind("-size-"); at != Rml::String::npos) {
                const float requested = std::strtof(family.c_str() + at + 6, nullptr);
                if (std::isfinite(requested) && requested > 0 && requested <= 4096)
                    face.size = requested;
            }
            for (size_t i = 0; i < faces.size(); i++)
                if (faces[i].ten == face.ten && faces[i].shadow == face.shadow &&
                    faces[i].outline == face.outline && faces[i].bold == face.bold &&
                    faces[i].italic == face.italic && faces[i].size == face.size)
                    return static_cast<Rml::FontFaceHandle>(i + 1);
            faces.push_back(face);
            return static_cast<Rml::FontFaceHandle>(faces.size());
        }

        Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle, const Rml::FontEffectList&) override {
            return 0;
        }

        const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override {
            Face& f = faces[handle - 1];
            if (f.ten) {
                TenFont::Instance().EnsureLoaded();
                const float s = static_cast<float>(f.size) / TenFont::kEm;
                const TenFont& ten = TenFont::Instance();
                f.metrics.size = f.size;
                f.metrics.ascent = ten.Ascent(s);
                f.metrics.descent = ten.Descent(s);
                f.metrics.line_spacing = ten.LineHeight(s);
                f.metrics.x_height = ten.XHeight(s);
                f.metrics.underline_position = 1.0f;
                f.metrics.underline_thickness = 1.0f;
                return f.metrics;
            }
            const float s = static_cast<float>(f.size) / 8.0f;
            f.metrics.size = f.size;
            f.metrics.ascent = 7.0f * s;
            f.metrics.descent = 2.0f * s;
            f.metrics.line_spacing = 9.0f * s;
            f.metrics.x_height = 6.0f * s;
            f.metrics.underline_position = 1.0f * s;
            f.metrics.underline_thickness = std::max(1.0f, s);
            return f.metrics;
        }

        int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string, const Rml::TextShapingContext&,
                           Rml::Character) override {
            const Face& f = faces[handle - 1];
            const std::string text(string.begin(), string.end());
            if (f.ten) {
                TenFont::Instance().EnsureLoaded();
                const float s = static_cast<float>(f.size) / TenFont::kEm;
                return static_cast<int>(std::lround(TenFont::Instance().Width(text, s)));
            }
            const float s = static_cast<float>(f.size) / 8.0f;
            const McFont& font = McFont::Instance();
            float width = 0;
            size_t i = 0;
            uint32_t cp;
            while (McFont::Next(text, i, cp))
                width += static_cast<float>(font.Get(cp).advance + (f.bold ? 1 : 0));
            return static_cast<int>(std::lround(width * s));
        }

        int GenerateString(Rml::RenderManager& renderManager, Rml::FontFaceHandle handle,
                           Rml::FontEffectsHandle, Rml::StringView string, Rml::Vector2f position,
                           Rml::ColourbPremultiplied colour, float opacity, const Rml::TextShapingContext&,
                           Rml::TexturedMeshList& meshes) override {
            const Face& f = faces[handle - 1];
            if (f.ten)
                return GenerateTenString(renderManager, f, string, position, colour, opacity, meshes);
            const float s = static_cast<float>(f.size) / 8.0f;
            const McFont& font = McFont::Instance();
            const std::string text(string.begin(), string.end());
            size_t glyphIndex = 0;
            uint32_t glyphCode;
            while (McFont::Next(text, glyphIndex, glyphCode))
                font.Get(glyphCode);
            Rml::Texture texture =
                renderManager.LoadTexture("mcfont:atlas#" + std::to_string(font.Version()));
            meshes.resize(meshes.size() + 1);
            Rml::TexturedMesh& out = meshes.back();
            out.texture = texture;

            const Rml::ColourbPremultiplied main = Fade(colour, opacity);
            const Rml::ColourbPremultiplied shade(static_cast<Rml::byte>(main.red / 4),
                                                  static_cast<Rml::byte>(main.green / 4),
                                                  static_cast<Rml::byte>(main.blue / 4), main.alpha);

            const float device = static_cast<float>(std::max(1, gScale));
            const float k = s;
            const float kGeo = std::max(1.0f, std::round(k * device)) / device;
            const auto snap = [&](float v) { return std::round(v * device) / device; };

            const auto quads = [&](float ox, float oy, const Rml::ColourbPremultiplied& c) {
                float pen = 0;
                size_t i = 0;
                uint32_t cp;
                while (McFont::Next(text, i, cp)) {
                    const McFont::Glyph& g = font.Get(cp);
                    if (g.width > 0) {
                        const float x = g.colored ? snap(position.x + pen + ox + g.left * kGeo)
                                                  : position.x + pen + ox + g.left * kGeo;
                        const float y = (g.colored ? snap(position.y - 7.0f * kGeo + oy)
                                                   : position.y - 7.0f * kGeo + oy) +
                                        g.top * kGeo;
                        const float w = g.width * kGeo, h = g.height * kGeo;
                        const Rml::ColourbPremultiplied ink =
                            g.colored ? Rml::ColourbPremultiplied(c.alpha, c.alpha, c.alpha, c.alpha) : c;
                        for (int pass = 0; pass < (f.bold ? 2 : 1); pass++) {
                            if (!f.italic) {
                                Rml::MeshUtilities::GenerateQuad(out.mesh, {x + pass * kGeo, y}, {w, h}, ink,
                                                                 {g.u0, g.v0}, {g.u1, g.v1});
                                continue;
                            }
                            const float top = (1.0f - 0.25f * g.top) * kGeo,
                                        bottom = (1.0f - 0.25f * (g.top + g.height)) * kGeo;
                            const int base = static_cast<int>(out.mesh.vertices.size());
                            const float bx = x + pass * kGeo;
                            out.mesh.vertices.push_back({{bx + top, y}, ink, {g.u0, g.v0}});
                            out.mesh.vertices.push_back({{bx + w + top, y}, ink, {g.u1, g.v0}});
                            out.mesh.vertices.push_back({{bx + w + bottom, y + h}, ink, {g.u1, g.v1}});
                            out.mesh.vertices.push_back({{bx + bottom, y + h}, ink, {g.u0, g.v1}});
                            for (int idx : {0, 2, 1, 0, 3, 2})
                                out.mesh.indices.push_back(base + idx);
                        }
                    }
                    pen += static_cast<float>(g.advance + (f.bold ? 1 : 0)) * kGeo;
                }
                return pen;
            };
            if (f.shadow)
                quads(kGeo, kGeo, shade);
            if (f.outline) {
                const Rml::ColourbPremultiplied black(0, 0, 0, main.alpha);
                quads(kGeo, 0, black);
                quads(-kGeo, 0, black);
                quads(0, kGeo, black);
                quads(0, -kGeo, black);
            }
            const float width = quads(0, 0, main);
            return static_cast<int>(std::lround(width));
        }

        int GetVersion(Rml::FontFaceHandle) override {
            return static_cast<int>(McFont::Instance().Version());
        }
        void ReleaseFontResources() override {}

    private:
        struct Face {
            bool ten = false;
            bool shadow = false;
            bool outline = false;
            bool bold = false;
            bool italic = false;
            float size = 8;
            Rml::FontMetrics metrics{};
        };

        static Rml::ColourbPremultiplied Fade(Rml::ColourbPremultiplied c, float opacity) {
            if (opacity < 1.0f) {
                c.red = static_cast<Rml::byte>(c.red * opacity);
                c.green = static_cast<Rml::byte>(c.green * opacity);
                c.blue = static_cast<Rml::byte>(c.blue * opacity);
                c.alpha = static_cast<Rml::byte>(c.alpha * opacity);
            }
            return c;
        }

        int GenerateTenString(Rml::RenderManager& renderManager, const Face& f, Rml::StringView string,
                              const Rml::Vector2f& position, Rml::ColourbPremultiplied colour, float opacity,
                              Rml::TexturedMeshList& meshes) {
            TenFont& ten = TenFont::Instance();
            ten.EnsureLoaded();
            const float s = static_cast<float>(f.size) / TenFont::kEm;
            const float device = static_cast<float>(std::max(1, gScale));
            const int emPx =
                std::clamp(static_cast<int>(std::lround(static_cast<float>(f.size) * device)), 4, 160);
            const TenFont::Page* page = ten.PageFor(emPx);
            if (!page || page->rgba.empty())
                return 0;
            const float k = static_cast<float>(f.size) / static_cast<float>(page->emPx);
            const std::string text(string.begin(), string.end());
            size_t glyphIndex = 0;
            uint32_t glyphCode;
            while (McFont::Next(text, glyphIndex, glyphCode))
                if (!page->glyphs.contains(glyphCode))
                    McFont::Instance().Get(glyphCode);
            Rml::Texture texture = renderManager.LoadTexture("mcten:atlas#" + std::to_string(emPx));
            meshes.resize(meshes.size() + 2);
            Rml::TexturedMesh& out = meshes[meshes.size() - 2];
            out.texture = texture;
            Rml::TexturedMesh& fallback = meshes[meshes.size() - 1];
            fallback.texture =
                renderManager.LoadTexture("mcfont:atlas#" + std::to_string(McFont::Instance().Version()));

            const Rml::ColourbPremultiplied main = Fade(colour, opacity);
            const Rml::ColourbPremultiplied shade(static_cast<Rml::byte>(main.red / 4),
                                                  static_cast<Rml::byte>(main.green / 4),
                                                  static_cast<Rml::byte>(main.blue / 4), main.alpha);
            const auto snap = [&](float v) { return std::round(v * device) / device; };
            const auto quads = [&](float ox, float oy, const Rml::ColourbPremultiplied& c) {
                const float baseX = snap(position.x + ox);
                const float baseY = snap(position.y + oy);
                float pen = 0;
                size_t i = 0;
                uint32_t cp;
                while (McFont::Next(text, i, cp)) {
                    const auto it = page->glyphs.find(cp);
                    if (it != page->glyphs.end()) {
                        const TenFont::Glyph& g = it->second;
                        const float x = baseX + pen + static_cast<float>(g.x) * k;
                        const float y = baseY + static_cast<float>(g.y) * k;
                        if (g.w > 0 && g.h > 0)
                            Rml::MeshUtilities::GenerateQuad(
                                out.mesh, {x, y}, {static_cast<float>(g.w) * k, static_cast<float>(g.h) * k},
                                c, {g.u0, g.v0}, {g.u1, g.v1});
                        pen += ten.Advance(cp, s);
                        continue;
                    }
                    const McFont::Glyph& mg = McFont::Instance().Get(cp);
                    if (mg.width > 0) {
                        const float ks = s * (TenFont::kEm / 8.0f);
                        const float x = baseX + pen + mg.left * ks;
                        const float y = baseY - 7.0f * ks + mg.top * ks;
                        Rml::MeshUtilities::GenerateQuad(
                            fallback.mesh, {x, y},
                            {static_cast<float>(mg.width) * ks, static_cast<float>(mg.height) * ks},
                            mg.colored ? Rml::ColourbPremultiplied(c.alpha, c.alpha, c.alpha, c.alpha) : c,
                            {mg.u0, mg.v0}, {mg.u1, mg.v1});
                        pen += static_cast<float>(mg.advance) * ks;
                        continue;
                    }
                    pen += ten.Advance(cp, s);
                }
                return pen;
            };
            if (f.shadow)
                quads(k, k, shade);
            if (f.outline) {
                const Rml::ColourbPremultiplied black(0, 0, 0, main.alpha);
                quads(k, 0, black);
                quads(-k, 0, black);
                quads(0, k, black);
                quads(0, -k, black);
            }
            const float width = quads(0, 0, main);
            return static_cast<int>(std::lround(width));
        }
        std::vector<Face> faces;
    };

    namespace {
        std::vector<float> Floats(const Rml::String& text) {
            std::vector<float> out;
            std::istringstream in(text);
            float v;
            while (in >> v)
                out.push_back(v);
            return out;
        }

        class ElementMcLayer : public Rml::Element {
        public:
            explicit ElementMcLayer(const Rml::String& tag) : Rml::Element(tag) {}

        protected:
            void OnRender() override {
                const Rml::String layer = GetAttribute<Rml::String>("layer", "gui");
                gLayer = layer == "top" ? UiLayer::Top : layer == "export" ? UiLayer::Export : UiLayer::Gui;
            }
        };

        class ElementMcImg : public Rml::Element {
        public:
            explicit ElementMcImg(const Rml::String& tag) : Rml::Element(tag) {}

        protected:
            void OnAttributeChange(const Rml::ElementAttributes& changed) override {
                Rml::Element::OnAttributeChange(changed);
                dirty = true;
                texture = {};
                loaded = false;
            }

            void OnResize() override { dirty = true; }

            void OnRender() override {
                Rml::RenderManager* manager = GetRenderManager();
                if (!manager)
                    return;
                if (!loaded) {
                    source = GetAttribute<Rml::String>("src", "");
                    if (source.empty())
                        return;
                    texture = manager->LoadTexture(source);
                    loaded = true;
                    dirty = true;
                }
                if (dirty)
                    Build(*manager);
                if (geometry)
                    geometry.Render(GetAbsoluteOffset(Rml::BoxArea::Border), texture);
            }

        private:
            void Build(Rml::RenderManager& manager) {
                dirty = false;
                Rml::Mesh mesh = geometry.Release(Rml::Geometry::ReleaseMode::ClearMesh);
                mesh.vertices.clear();
                mesh.indices.clear();
                const Rml::Vector2f size = GetBox().GetSize(Rml::BoxArea::Border);
                const Rml::Vector2i dims = texture.GetDimensions();
                if (size.x <= 0 || size.y <= 0 || dims.x <= 0 || dims.y <= 0)
                    return;
                const float tw = static_cast<float>(dims.x), th = static_cast<float>(dims.y);

                float cr = 1, cg = 1, cb = 1, ca = 1;
                if (HasAttribute("color")) {
                    const auto c = Floats(GetAttribute<Rml::String>("color", ""));
                    if (c.size() >= 3) {
                        cr = c[0];
                        cg = c[1];
                        cb = c[2];
                    }
                }
                if (HasAttribute("alpha"))
                    ca = GetAttribute<float>("alpha", 1.0f);
                const Rml::ColourbPremultiplied colour = Colour(cr, cg, cb, ca);

                float su = 0, sv = 0, sw = tw, sh = th;
                if (HasAttribute("uv")) {
                    const auto uv = Floats(GetAttribute<Rml::String>("uv", ""));
                    if (uv.size() >= 2) {
                        su = uv[0];
                        sv = uv[1];
                        sw = tw - su;
                        sh = th - sv;
                    }
                    if (uv.size() >= 4) {
                        if (uv[2] >= 0)
                            sw = uv[2];
                        if (uv[3] >= 0)
                            sh = uv[3];
                    }
                }

                float dx = 0, dy = 0, dw = size.x, dh = size.y;
                if (GetAttribute<int>("keep", 0) && !HasAttribute("nine") &&
                    !UiAssets::Instance().Sidecar(source).nine) {
                    const float ratio = sw / std::max(sh, 1.0f);
                    if (dw / dh > ratio) {
                        const float w = dh * ratio;
                        dx = (dw - w) * 0.5f;
                        dw = w;
                    } else {
                        const float h = dw / ratio;
                        dy = (dh - h) * 0.5f;
                        dh = h;
                    }
                }

                float cu0 = 0, cu1 = 1, cv0 = 0, cv1 = 1;
                if (HasAttribute("clip")) {
                    std::istringstream in(GetAttribute<Rml::String>("clip", ""));
                    std::string dir;
                    float ratio = 1;
                    in >> dir >> ratio;
                    ratio = std::clamp(ratio, 0.0f, 1.0f);
                    if (dir == "left")
                        cu1 = ratio;
                    else if (dir == "right")
                        cu0 = 1 - ratio;
                    else if (dir == "up")
                        cv1 = ratio;
                    else if (dir == "down")
                        cv0 = 1 - ratio;
                    else if (dir == "center") {
                        cu0 = (1 - ratio) * 0.5f;
                        cu1 = 1 - cu0;
                    }
                }

                const auto quad = [&](float x, float y, float w, float h, float u0, float v0, float u1,
                                      float v1) {
                    if (w <= 0 || h <= 0)
                        return;
                    const float vx0 = dx + dw * cu0, vx1 = dx + dw * cu1, vy0 = dy + dh * cv0,
                                vy1 = dy + dh * cv1;
                    float x0 = std::max(x, vx0), x1 = std::min(x + w, vx1);
                    float y0 = std::max(y, vy0), y1 = std::min(y + h, vy1);
                    if (x1 <= x0 || y1 <= y0)
                        return;
                    const float ku = (u1 - u0) / w, kv = (v1 - v0) / h;
                    Rml::MeshUtilities::GenerateQuad(mesh, {x0, y0}, {x1 - x0, y1 - y0}, colour,
                                                     {(u0 + (x0 - x) * ku) / tw, (v0 + (y0 - y) * kv) / th},
                                                     {(u0 + (x1 - x) * ku) / tw, (v0 + (y1 - y) * kv) / th});
                };

                bool nine = false;
                float slice[4] = {0, 0, 0, 0};
                float sliceScale = 1.0f;
                if (HasAttribute("nine")) {
                    const auto n = Floats(GetAttribute<Rml::String>("nine", ""));
                    if (n.size() >= 4) {
                        nine = true;
                        for (int i = 0; i < 4; i++)
                            slice[i] = n[i];
                    }
                } else {
                    const TextureSidecar sc = UiAssets::Instance().Sidecar(source);
                    if (sc.nine) {
                        nine = true;
                        for (int i = 0; i < 4; i++)
                            slice[i] = sc.slice[i];
                        if (sc.baseW > 0)
                            sliceScale = tw / static_cast<float>(sc.baseW);
                    }
                }
                const int tiled = GetAttribute<int>("tiled", 0);
                const float tileScale =
                    HasAttribute("tilescale") ? GetAttribute<float>("tilescale", 1.0f) : 1.0f;

                if (nine && !tiled) {
                    const float l = slice[0] * sliceScale, t = slice[1] * sliceScale,
                                r = slice[2] * sliceScale, b = slice[3] * sliceScale;
                    const float dl = slice[0], dt = slice[1], dr = slice[2], db = slice[3];
                    const float xs[4] = {dx, dx + dl, dx + dw - dr, dx + dw};
                    const float ys[4] = {dy, dy + dt, dy + dh - db, dy + dh};
                    const float us[4] = {su, su + l, su + sw - r, su + sw};
                    const float vs[4] = {sv, sv + t, sv + sh - b, sv + sh};
                    for (int j = 0; j < 3; j++)
                        for (int i = 0; i < 3; i++)
                            quad(xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j], us[i], vs[j], us[i + 1],
                                 vs[j + 1]);
                } else if (tiled) {
                    const float cellW = sw * tileScale, cellH = sh * tileScale;
                    const bool tx = tiled == 1 || tiled == 2, ty = tiled == 1 || tiled == 3;
                    const float stepX = tx ? cellW : dw, stepY = ty ? cellH : dh;
                    int guard = 0;
                    for (float y = 0; y < dh && guard < 8192; y += stepY)
                        for (float x = 0; x < dw && guard < 8192; x += stepX, guard++) {
                            const float w = std::min(stepX, dw - x), h = std::min(stepY, dh - y);
                            quad(dx + x, dy + y, w, h, su, sv, su + sw * (w / stepX), sv + sh * (h / stepY));
                        }
                } else {
                    quad(dx, dy, dw, dh, su, sv, su + sw, sv + sh);
                }
                geometry = manager.MakeGeometry(std::move(mesh));
            }

            Rml::Geometry geometry;
            Rml::Texture texture;
            Rml::String source;
            bool loaded = false;
            bool dirty = true;
        };

        class ElementMcItem : public Rml::Element {
        public:
            explicit ElementMcItem(const Rml::String& tag) : Rml::Element(tag) {}

        protected:
            void OnAttributeChange(const Rml::ElementAttributes& changed) override {
                Rml::Element::OnAttributeChange(changed);
                dirty = true;
                loaded = false;
            }
            void OnResize() override { dirty = true; }
            void OnRender() override {
                Rml::RenderManager* manager = GetRenderManager();
                if (!manager)
                    return;
                if (!loaded) {
                    texture = manager->LoadTexture("mcicon:" + GetAttribute<Rml::String>("key", ""));
                    loaded = true;
                    dirty = true;
                }
                if (dirty) {
                    dirty = false;
                    Rml::Mesh mesh = geometry.Release(Rml::Geometry::ReleaseMode::ClearMesh);
                    mesh.vertices.clear();
                    mesh.indices.clear();
                    const Rml::Vector2f size = GetBox().GetSize(Rml::BoxArea::Border);
                    const float alpha = HasAttribute("alpha") ? GetAttribute<float>("alpha", 1.0f) : 1.0f;
                    Rml::MeshUtilities::GenerateQuad(mesh, {0, 0}, size, Colour(1, 1, 1, alpha), {0, 0},
                                                     {1, 1});
                    geometry = manager->MakeGeometry(std::move(mesh));
                }
                if (geometry) {
                    geometry.Render(GetAbsoluteOffset(Rml::BoxArea::Border), texture);
                    if (HasAttribute("glint")) {
                        gGlintPass = true;
                        geometry.Render(GetAbsoluteOffset(Rml::BoxArea::Border), texture);
                        gGlintPass = false;
                    }
                }
            }

        private:
            Rml::Geometry geometry;
            Rml::Texture texture;
            bool loaded = false;
            bool dirty = true;
        };

        Rml::ElementInstancerGeneric<ElementMcImg> gImgInstancer;
        Rml::ElementInstancerGeneric<ElementMcItem> gItemInstancer;
        Rml::ElementInstancerGeneric<ElementMcLayer> gLayerInstancer;
    }

    RmlBackend::RmlBackend() = default;
    RmlBackend::~RmlBackend() = default;

    bool RmlBackend::Initialize(RenderContext& renderer) {
        if (ready)
            return true;
        gRenderer = &renderer;
        renderInterface = std::make_unique<BgfxRenderInterface>();
        systemInterface = std::make_unique<GameSystemInterface>();
        fontEngine = std::make_unique<McFontEngine>();
        Rml::SetSystemInterface(systemInterface.get());
        Rml::SetRenderInterface(renderInterface.get());
        Rml::SetFontEngineInterface(fontEngine.get());
        if (!Rml::Initialise()) {
            Log("ui", "RmlUi failed to initialise");
            return false;
        }
        Rml::Factory::RegisterElementInstancer("mcimg", &gImgInstancer);
        Rml::Factory::RegisterElementInstancer("mcitem", &gItemInstancer);
        Rml::Factory::RegisterElementInstancer("mclayer", &gLayerInstancer);
        context = Rml::CreateContext("hud", Rml::Vector2i(427, 240));
        if (!context) {
            Log("ui", "RmlUi context creation failed");
            Rml::Shutdown();
            return false;
        }
        ready = true;
        return true;
    }

    void RmlBackend::Shutdown() {
        if (!ready)
            return;
        if (context)
            context->UnloadAllDocuments();
        document = nullptr;
        overlay = nullptr;
        plugin = nullptr;
        serverLayer = nullptr;
        Rml::Shutdown();
        context = nullptr;
        ready = false;
        gRenderer = nullptr;
    }

    void RmlBackend::Resize(int w, int h, int s) {
        guiW = w;
        guiH = h;
        scale = std::max(1, s);
        gScale = scale;
        if (context)
            context->SetDimensions({w, h});
    }

    bool RmlBackend::SetDocument(const std::string& rml, std::string& error) {
        if (!context)
            return false;
        Rml::ElementDocument* next = context->LoadDocumentFromMemory(rml);
        if (!next) {
            error = "RmlUi could not parse the generated document";
            return false;
        }
        if (document)
            context->UnloadDocument(document);
        document = next;
        document->Show();
        if (plugin)
            plugin->PullToFront();
        if (overlay)
            overlay->PullToFront();
        return true;
    }

    bool RmlBackend::SetPluginLayer(const std::string& rml, std::string& error) {
        if (!context)
            return false;
        Rml::ElementDocument* next = context->LoadDocumentFromMemory(rml);
        if (!next) {
            error = "RmlUi could not parse the plugin layer";
            return false;
        }
        if (plugin)
            context->UnloadDocument(plugin);
        plugin = next;
        plugin->Show();
        if (overlay)
            overlay->PullToFront();
        return true;
    }

    bool RmlBackend::SetServerLayer(const std::string& rml, std::string& error) {
        if (!context)
            return false;
        Rml::ElementDocument* next = context->LoadDocumentFromMemory(rml);
        if (!next) {
            error = "RmlUi could not parse the server layer";
            return false;
        }
        if (serverLayer)
            context->UnloadDocument(serverLayer);
        serverLayer = next;
        serverLayer->Show();
        if (plugin)
            plugin->PullToFront();
        if (overlay)
            overlay->PullToFront();
        return true;
    }

    void RmlBackend::ClearServerLayer() {
        if (context && serverLayer)
            context->UnloadDocument(serverLayer);
        serverLayer = nullptr;
    }

    void RmlBackend::ClearPluginLayer() {
        if (context && plugin)
            context->UnloadDocument(plugin);
        plugin = nullptr;
    }

    bool RmlBackend::SetOverlay(const std::string& rml, std::string& error) {
        if (!context)
            return false;
        Rml::ElementDocument* next = context->LoadDocumentFromMemory(WithJavaWidgets(rml));
        if (!next) {
            error = "RmlUi could not parse the overlay document";
            return false;
        }
        if (overlay)
            context->UnloadDocument(overlay);
        overlay = next;
        overlay->Show();
        return true;
    }

    void RmlBackend::ClearOverlay() {
        if (context && overlay)
            context->UnloadDocument(overlay);
        overlay = nullptr;
    }

    void RmlBackend::MoveOverlayElement(const char* id, int guiX, int guiY) {
        if (!overlay)
            return;
        if (Rml::Element* e = overlay->GetElementById(id)) {
            e->SetProperty("left", std::to_string(guiX) + "px");
            e->SetProperty("top", std::to_string(guiY) + "px");
        }
    }

    void RmlBackend::SetOverlayAttribute(const char* id, const char* name, const std::string& value) {
        if (!overlay)
            return;
        if (Rml::Element* e = overlay->GetElementById(id);
            e && e->GetAttribute<Rml::String>(name, "") != value)
            e->SetAttribute(name, value);
    }

    void RmlBackend::MouseMove(float guiX, float guiY) {
        if (context)
            context->ProcessMouseMove(static_cast<int>(guiX), static_cast<int>(guiY), 0);
    }

    std::string RmlBackend::OverlayHover() const {
        if (!context || !overlay)
            return {};
        for (Rml::Element* e = context->GetHoverElement(); e; e = e->GetParentNode())
            if (!e->GetId().empty() && e->GetOwnerDocument() == overlay)
                return e->GetId();
        return {};
    }

    bool RmlBackend::OverlayHoverClickSound(std::string& name, float& volume, float& pitch) const {
        if (!context || !overlay)
            return false;
        static const char* const kNative[] = {"btn", "sbtn", "rbtn", "pg", "row", "ticon", "go", "acc"};
        for (Rml::Element* e = context->GetHoverElement(); e && e->GetOwnerDocument() == overlay;
             e = e->GetParentNode()) {
            if (e->IsClassSet("off"))
                return false;
            if (e->HasAttribute("snd")) {
                name = e->GetAttribute<Rml::String>("snd", "");
                volume = e->GetAttribute<float>("vol", 1.0f);
                pitch = e->GetAttribute<float>("pit", 1.0f);
                return !name.empty();
            }

            if (e->IsClassSet("bt"))
                return false;
            for (const char* cls : kNative)
                if (e->IsClassSet(cls)) {
                    name = "random.click";
                    volume = pitch = 1.0f;
                    return true;
                }
        }
        return false;
    }

    std::string RmlBackend::HoverLink() const {
        if (!context)
            return {};
        for (Rml::Element* e = context->GetHoverElement(); e; e = e->GetParentNode())
            if (e->GetId().starts_with("link:"))
                return e->GetId();
        return {};
    }

    void RmlBackend::Update() {
        if (context)
            context->Update();
    }

    void RmlBackend::Render() {
        if (!context)
            return;
        gScale = scale;
        gLayer = UiLayer::Gui;
        static int frameCounter = 0;
        if (gRenderer && (gRenderer->UiLiveMeshes() > 3500 || gMeshStarved)) {
            static int lastRelease = -1000;
            if (++frameCounter - lastRelease > (gMeshStarved ? 20 : 120)) {
                gMeshStarved = false;
                lastRelease = frameCounter;
                Log("render", "{} UI meshes alive; releasing RmlUi's compiled geometry",
                    gRenderer->UiLiveMeshes());
                Rml::ReleaseCompiledGeometry();
            }
        } else {
            ++frameCounter;
        }
        context->Render();
        gLayer = UiLayer::Gui;
    }

    void RmlBackend::UnloadDocuments() {
        if (!context)
            return;
        context->UnloadAllDocuments();
        document = overlay = plugin = serverLayer = nullptr;
        context->Update();
        ReloadTextures();
    }

    void RmlBackend::ReloadTextures() {
        if (!ready)
            return;
        Rml::ReleaseTextures();
        if (gGlintTexture != 0 && gRenderer) {
            gRenderer->UiDestroyTexture(gGlintTexture);
            gGlintTexture = 0;
        }
    }
}
