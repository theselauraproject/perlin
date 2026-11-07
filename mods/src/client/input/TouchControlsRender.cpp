#include "client/input/TouchControls.hpp"
#include "client/input/TouchLayout.hpp"
#include "client/renderer/RenderContext.hpp"
#include "client/ui/McFont.hpp"
#include "client/ui/UiAssets.hpp"
#include <algorithm>
#include <cmath>
namespace perlin {
    using namespace touchlayout;
    namespace {
        constexpr int kDiscTile = 64;

        enum S {
            Frame,
            Knob,
            Jump,
            JumpP,
            WaterUp,
            WaterUpP,
            FlyUp,
            FlyUpP,
            Sneak,
            SneakP,
            WaterDown,
            WaterDownP,
            FlyDown,
            FlyDownP,
            Dismount,
            DismountP,
            Attack,
            AttackP,
            Interact,
            InteractP,
            Sprint,
            SprintP,
            PauseIcon,
            ChatIcon,
            Dots,
            PerspectiveIcon,
            DebugIcon,
            PlayersIcon,
            HudIcon,
            SpriteCount
        };
        constexpr const char* kSpriteNames[SpriteCount] = {
            "textures/ui/joystick_frame", "textures/ui/joystick_knob",
            "textures/ui/jump",           "textures/ui/jump_pressed",
            "textures/ui/waterascend",    "textures/ui/waterascend_pressed",
            "textures/ui/flyingascend",   "textures/ui/flyingascend_pressed",
            "textures/ui/sneak",          "textures/ui/sneak_pressed",
            "textures/ui/waterdescend",   "textures/ui/waterdescend_pressed",
            "textures/ui/flyingdescend",  "textures/ui/flyingdescend_pressed",
            "textures/ui/dismount",       "textures/ui/dismount_pressed",
            "textures/ui/attack",         "textures/ui/attack_pressed",
            "textures/ui/interact",       "textures/ui/interact_pressed",
            "textures/ui/sprint",         "textures/ui/sprint_pressed",
            "textures/ui/pause_icon",     "textures/ui/comment",
            "textures/ui/more_dots",      "textures/ui/perspective",
            "textures/ui/debug_glyph",    "textures/ui/icon_multiplayer",
            "textures/ui/camera-small",
        };

        uint32_t Pack(float r, float g, float b, float a) {
            const auto byte = [](float v) {
                return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            return (byte(a) << 24) | (byte(b * a) << 16) | (byte(g * a) << 8) | byte(r * a);
        }

        struct Mesh {
            std::vector<UiVertex> vertices;
            std::vector<uint32_t> indices;

            void Quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                      uint32_t color) {
                const uint32_t base = static_cast<uint32_t>(vertices.size());
                vertices.push_back({x0, y0, 0.0f, color, u0, v0});
                vertices.push_back({x1, y0, 0.0f, color, u1, v0});
                vertices.push_back({x1, y1, 0.0f, color, u1, v1});
                vertices.push_back({x0, y1, 0.0f, color, u0, v1});
                for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                    indices.push_back(base + i);
            }
        };

        void Disc(Mesh& m, float cx, float cy, float r, uint32_t color, bool ring) {
            const float u0 = ring ? 0.5f : 0.0f, u1 = ring ? 1.0f : 0.5f;
            m.Quad(cx - r, cy - r, cx + r, cy + r, u0, 0.0f, u1, 1.0f, color);
        }
    }
    void TouchControls::Draw(RenderContext& renderer, bool visible) {
        std::lock_guard lock(mutex);
        if ((!visible && !editing) || width <= 0 || height <= 0)
            return;

        if (!discTexture) {
            std::vector<uint8_t> pixels(static_cast<size_t>(kDiscTile) * 2 * kDiscTile * 4, 0);
            for (int y = 0; y < kDiscTile; y++) {
                for (int x = 0; x < kDiscTile * 2; x++) {
                    const bool ring = x >= kDiscTile;
                    const float px =
                        (static_cast<float>(ring ? x - kDiscTile : x) + 0.5f) / kDiscTile * 2.0f - 1.0f;
                    const float py = (static_cast<float>(y) + 0.5f) / kDiscTile * 2.0f - 1.0f;
                    const float d = std::sqrt(px * px + py * py);
                    float a = 0.0f;
                    if (ring) {
                        const float outer = std::clamp((1.0f - d) * 16.0f, 0.0f, 1.0f);
                        const float inner = std::clamp((d - 0.86f) * 16.0f, 0.0f, 1.0f);
                        a = outer * inner;
                    } else {
                        a = std::clamp((1.0f - d) * 16.0f, 0.0f, 1.0f);
                    }
                    uint8_t* p = &pixels[(static_cast<size_t>(y) * kDiscTile * 2 + x) * 4];
                    const uint8_t v = static_cast<uint8_t>(a * 255.0f + 0.5f);
                    p[0] = p[1] = p[2] = v;
                    p[3] = v;
                }
            }
            discTexture = renderer.UiCreateTexture(kDiscTile * 2, kDiscTile, pixels.data(), true);
        }
        const ui::McFont& font = ui::McFont::Instance();
        if (!fontTexture || fontVersion != font.Version()) {
            if (fontTexture)
                renderer.UiDestroyTexture(fontTexture);
            std::vector<uint8_t> atlas = font.Atlas();
            for (size_t i = 0; i + 3 < atlas.size(); i += 4) {
                const unsigned a = atlas[i + 3];
                atlas[i] = static_cast<uint8_t>((atlas[i] * a + 127) / 255);
                atlas[i + 1] = static_cast<uint8_t>((atlas[i + 1] * a + 127) / 255);
                atlas[i + 2] = static_cast<uint8_t>((atlas[i + 2] * a + 127) / 255);
            }
            fontTexture =
                renderer.UiCreateTexture(font.AtlasWidth(), font.AtlasHeight(), atlas.data(), false);
            fontVersion = font.Version();
        }

        if (sprites.empty())
            sprites.resize(SpriteCount);
        for (int i = 0; i < SpriteCount; i++) {
            Sprite& sp = sprites[i];
            if (sp.id) {
                continue;
            }
            if (sp.nextTry > 0) {
                sp.nextTry--;
                continue;
            }
            Texture t;
            if (!ui::UiAssets::Instance().Image(kSpriteNames[i], t) || t.width <= 0 || t.height <= 0 ||
                t.pixels.size() < static_cast<size_t>(t.width) * t.height * 4) {
                sp.nextTry = 120;
                continue;
            }
            for (size_t k = 0; k + 3 < t.pixels.size(); k += 4) {
                const unsigned a = t.pixels[k + 3];
                t.pixels[k] = static_cast<unsigned char>((t.pixels[k] * a + 127) / 255);
                t.pixels[k + 1] = static_cast<unsigned char>((t.pixels[k + 1] * a + 127) / 255);
                t.pixels[k + 2] = static_cast<unsigned char>((t.pixels[k + 2] * a + 127) / 255);
            }
            sp.w = t.width;
            sp.h = t.height;
            sp.id = renderer.UiCreateTexture(t.width, t.height, t.pixels.data(), false);
        }

        const float u = static_cast<float>(std::min(width, height));
        Mesh discs, text;
        std::vector<Mesh> icons(SpriteCount);
        auto icon = [&](int sprite, float cx, float cy, float halfW, float halfH, float alpha) {
            icons[sprite].Quad(cx - halfW, cy - halfH, cx + halfW, cy + halfH, 0.0f, 0.0f, 1.0f, 1.0f,
                               Pack(1, 1, 1, alpha));
        };
        auto label = [&](const char* str, float cx, float cy, float scale, float alpha) {
            const std::string s = str;
            float total = 0.0f;
            size_t k = 0;
            uint32_t cp = 0;
            std::vector<uint32_t> cps;
            while (k < s.size() && ui::McFont::Next(s, k, cp)) {
                cps.push_back(cp);
                total += static_cast<float>(font.Get(cp).advance) * scale;
            }
            float pen = cx - total * 0.5f;
            const float top = cy - 4.0f * scale;
            for (uint32_t c : cps) {
                const ui::McFont::Glyph& g = font.Get(c);
                if (g.width > 0)
                    text.Quad(pen + g.left * scale, top + g.top * scale, pen + (g.left + g.width) * scale,
                              top + (g.top + g.height) * scale, g.u0, g.v0, g.u1, g.v1, Pack(1, 1, 1, alpha));
                pen += static_cast<float>(g.advance) * scale;
            }
        };
        auto have = [&](int sprite) { return sprites[sprite].id != 0; };

        const float base = std::clamp(state.opacity, 0.2f, 1.0f);
        {
            const float radius = stickR;
            const float cx = stickCX, cy = stickCY;
            const float a = stickActive ? 1.0f : 0.8f;
            if (have(Frame))
                icon(Frame, cx, cy, radius, radius, base * a);
            else {
                Disc(discs, cx, cy, radius, Pack(1, 1, 1, 0.10f * base), false);
                Disc(discs, cx, cy, radius, Pack(1, 1, 1, 0.45f * base), true);
            }
            const float kr = radius * 0.46f;
            const float kx = cx + (stickActive ? knobX : 0.0f), ky = cy + (stickActive ? knobY : 0.0f);
            if (have(Knob))
                icon(Knob, kx, ky, kr, kr, base * (stickActive ? 1.0f : 0.85f));
            else
                Disc(discs, kx, ky, kr, Pack(1, 1, 1, 0.40f * base), false);
            if (editing && editSelected == ButtonCount)
                Disc(discs, cx, cy, radius * 1.08f, Pack(1, 1, 0.3f, 0.95f), true);
        }

        for (int i = 0; i < ButtonCount; i++) {
            const Layout& l = layout[i];
            if (!l.visible)
                continue;
            const bool active = down[i];
            const float alpha = (active ? 1.0f : 0.9f) * base;
            int sprite = -1;
            int glyph = -1;
            switch (i) {
            case Jump:
                sprite = state.inWater  ? (active ? WaterUpP : WaterUp)
                         : state.flying ? (active ? FlyUpP : FlyUp)
                                        : (active ? JumpP : S::Jump);
                break;
            case Sneak:
                sprite = state.riding    ? (active ? DismountP : Dismount)
                         : state.flying  ? (active ? FlyDownP : FlyDown)
                         : state.inWater ? (active ? WaterDownP : WaterDown)
                                         : (active ? SneakP : S::Sneak);
                break;
            case Attack:
                sprite = active ? AttackP : S::Attack;
                break;
            case Use:
                sprite = active ? InteractP : Interact;
                break;
            case Sprint:
                sprite = active ? SprintP : S::Sprint;
                break;
            case Perspective:
                sprite = PerspectiveIcon;
                break;
            case Pause:
                glyph = PauseIcon;
                break;
            case Chat:
                glyph = ChatIcon;
                break;
            case Debug:
                glyph = DebugIcon;
                break;
            case Players:
                glyph = PlayersIcon;
                break;
            case HideHud:
                glyph = HudIcon;
                break;
            case Inventory:
                glyph = Dots;
                break;
            default:
                break;
            }
            const bool selected = editing && editSelected == i;
            if (sprite >= 0 && have(sprite)) {
                icon(sprite, l.cx, l.cy, l.r, l.r, alpha);
                if (active && i == Perspective)
                    Disc(discs, l.cx, l.cy, l.r, Pack(1, 1, 1, 0.25f), false);
            } else if (glyph >= 0 && have(Knob) && have(glyph)) {
                icon(Knob, l.cx, l.cy, l.r, l.r, alpha);
                if (active)
                    icon(Knob, l.cx, l.cy, l.r, l.r, 0.35f);
                const Sprite& sp = sprites[glyph];
                const float k = std::max(1.0f, std::floor(l.r * 1.1f / std::max(sp.w, sp.h)));
                icon(glyph, l.cx, l.cy, sp.w * k * 0.5f, sp.h * k * 0.5f, 1.0f);
            } else {
                const float shade = active ? 1.0f : 0.1f;
                Disc(discs, l.cx, l.cy, l.r,
                     Pack(shade, shade, shade, (active ? 0.55f : (l.system ? 0.55f : 0.32f)) * base), false);
                Disc(discs, l.cx, l.cy, l.r, Pack(1, 1, 1, active ? 0.9f : 0.55f), true);
                label(l.label, l.cx, l.cy, std::max(1.0f, std::floor(l.r / (l.system ? 11.0f : 9.0f))),
                      0.95f);
            }
            if (selected)
                Disc(discs, l.cx, l.cy, l.r * 1.12f, Pack(1, 1, 0.3f, 0.95f), true);
        }

        if (editing) {
            ToolBox boxes[4];
            ToolBoxes(width, height, boxes);
            for (const ToolBox& b : boxes) {
                const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f;
                if (have(Knob))
                    icons[Knob].Quad(b.x, b.y, b.x + b.w, b.y + b.h, 0.0f, 0.0f, 1.0f, 1.0f,
                                     Pack(1, 1, 1, 0.95f));
                else
                    Disc(discs, cx, cy, b.h * 0.5f, Pack(0.1f, 0.1f, 0.1f, 0.7f), false);
                label(b.label, cx, cy, std::max(1.0f, std::floor(b.h / 22.0f)), 1.0f);
            }
            label("Drag buttons to move them. Select one and use - / + to resize.",
                  static_cast<float>(width) * 0.5f, static_cast<float>(height) * 0.5f - u * 0.12f,
                  std::max(1.0f, std::floor(u / 220.0f)), 1.0f);
        }

        auto submit = [&](Mesh& mesh, uint32_t texture) {
            if (mesh.vertices.empty() || !texture)
                return;
            const uint32_t id =
                renderer.UiCreateFrameMesh(mesh.vertices.data(), static_cast<uint32_t>(mesh.vertices.size()),
                                           mesh.indices.data(), static_cast<uint32_t>(mesh.indices.size()));
            if (!id)
                return;
            UiDraw draw;
            draw.mesh = id;
            draw.texture = texture;
            draw.premultiplied = true;
            renderer.UiSubmit(UiLayer::Top, draw);
            renderer.UiDestroyMesh(id);
        };
        submit(discs, discTexture);
        for (int i = 0; i < SpriteCount; i++)
            submit(icons[i], sprites[i].id);
        submit(text, fontTexture);
    }
}
