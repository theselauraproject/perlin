#include "client/systems/input/TouchControls.hpp"
#include "client/systems/text/FontTexture.hpp"
#include "client/systems/input/TouchLayout.hpp"
#include "client/systems/gfx/RenderContext.hpp"
#include "client/systems/text/McFont.hpp"
#include "client/systems/sprites/UiAssets.hpp"
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
            DUp,
            DUpP,
            DDown,
            DDownP,
            DLeft,
            DLeftP,
            DRight,
            DRightP,
            DUpLeft,
            DUpLeftP,
            DUpRight,
            DUpRightP,
            DSneak,
            DSneakP,
            CJump,
            CJumpP,
            CFlyUp,
            CFlyUpP,
            CFlyDown,
            CFlyDownP,
            CWaterUp,
            CWaterUpP,
            CWaterDown,
            CWaterDownP,
            CDismount,
            CDismountP,
            Hotbar8,
            HotbarEndCap,
            InvButton,
            InvButtonP,
            Elipses,
            GuiSheet,
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
            "textures/gui/controls/up",              "textures/gui/controls/up_pressed",
            "textures/gui/controls/down",            "textures/gui/controls/down_pressed",
            "textures/gui/controls/left",            "textures/gui/controls/left_pressed",
            "textures/gui/controls/right",           "textures/gui/controls/right_pressed",
            "textures/gui/controls/up_left",         "textures/gui/controls/up_left_pressed",
            "textures/gui/controls/up_right",        "textures/gui/controls/up_right_pressed",
            "textures/gui/controls/sneak",           "textures/gui/controls/sneak_pressed",
            "textures/gui/controls/jump",            "textures/gui/controls/jump_pressed",
            "textures/gui/controls/flyingascend",    "textures/gui/controls/flyingascend_pressed",
            "textures/gui/controls/flyingdescend",   "textures/gui/controls/flyingdescend_pressed",
            "textures/gui/controls/waterascend",     "textures/gui/controls/waterascend_pressed",
            "textures/gui/controls/waterdescend",    "textures/gui/controls/waterdescend_pressed",
            "textures/gui/controls/dismount",        "textures/gui/controls/dismount_pressed",
            "textures/ui/hotbar_8",                  "textures/ui/hotbar_end_cap",
            "textures/ui/button_borderless_light",   "textures/ui/button_borderless_lightpressed",
            "textures/ui/elipses",
            "textures/gui/gui",
        };

        std::vector<uint8_t> TopButtonPixels(const Texture& t) {
            const int w = t.width, h = t.height;
            std::vector<uint8_t> px(static_cast<size_t>(w) * 4 * h * 4, 0);
            const int ex0 = w * 2 / 18, ex1 = w * 16 / 18, ey0 = h * 3 / 18, ey1 = h * 13 / 18;
            const uint8_t* face = &t.pixels[(static_cast<size_t>(ey0) * w + ex0) * 4];
            for (int tile = 0; tile < 4; tile++) {
                const bool eye = tile >= 2;
                const bool pressed = (tile & 1) != 0;
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) {
                        const uint8_t* src = &t.pixels[(static_cast<size_t>(y) * w + x) * 4];
                        if (!eye && x >= ex0 && x < ex1 && y >= ey0 && y < ey1)
                            src = face;
                        uint8_t* d = &px[(static_cast<size_t>(y) * w * 4 + tile * w + x) * 4];
                        for (int c = 0; c < 3; c++)
                            d[c] = static_cast<uint8_t>(pressed ? src[c] * 3 / 4 : src[c]);
                        d[3] = src[3];
                    }
            }
            return px;
        }

        std::vector<uint8_t> GuiButtonPixels(const Texture& t, int& tile) {
            const int s = std::max(1, t.width / 256);
            tile = 18 * s;
            std::vector<uint8_t> px(static_cast<size_t>(tile) * 4 * tile * 4, 0);
            const int srcX[2] = {200 * s, 200 * s}, srcY[2] = {64 * s, 82 * s};
            for (int k = 0; k < 4; k++) {
                const int which = k / 2;
                const bool pressed = (k & 1) != 0;
                for (int y = 0; y < tile; y++)
                    for (int x = 0; x < tile; x++) {
                        const int sx = srcX[which] + x, sy = srcY[which] + y;
                        if (sx >= t.width || sy >= t.height)
                            continue;
                        const uint8_t* src = &t.pixels[(static_cast<size_t>(sy) * t.width + sx) * 4];
                        uint8_t* d = &px[(static_cast<size_t>(y) * tile * 4 + k * tile + x) * 4];
                        for (int c = 0; c < 3; c++)
                            d[c] = static_cast<uint8_t>(pressed ? src[c] * 3 / 4 : src[c]);
                        d[3] = src[3];
                    }
            }
            return px;
        }

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
        ui::SyncFontTexture(renderer, font, fontSync, true);
        fontTexture = fontSync.handle;

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
                sp.nextTry = ++sp.tries >= 3 ? 1 << 30 : 600;
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
            if (i == GuiSheet && !guiTexture && t.width >= 256 && t.height >= 256) {
                int tile = 18;
                const std::vector<uint8_t> px = GuiButtonPixels(t, tile);
                guiTexture = renderer.UiCreateTexture(tile * 4, tile, px.data(), false);
            }
            if (i == PerspectiveIcon && !topTexture) {
                const std::vector<uint8_t> px = TopButtonPixels(t);
                topTexture = renderer.UiCreateTexture(t.width * 4, t.height, px.data(), false);
                topW = t.width;
                topH = t.height;
            }
            if (i == GuiSheet) {
                sp.id = guiTexture ? guiTexture : 1;
                continue;
            }
            sp.id = renderer.UiCreateTexture(t.width, t.height, t.pixels.data(), false);
        }

        const float u = static_cast<float>(std::min(width, height));
        Mesh discs, text, top, guiTop, ring;
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
        auto inked = [&](const char* str, float cx, float cy, float scale, float alpha) {
            const std::string s = str;
            size_t k = 0;
            uint32_t cp = 0;
            std::vector<uint32_t> cps;
            float pen = 0.0f, x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
            while (k < s.size() && ui::McFont::Next(s, k, cp)) {
                cps.push_back(cp);
                const ui::McFont::Glyph& g = font.Get(cp);
                if (g.width > 0) {
                    x0 = std::min(x0, pen + g.left);
                    x1 = std::max(x1, pen + g.left + g.width);
                    y0 = std::min(y0, static_cast<float>(g.top));
                    y1 = std::max(y1, static_cast<float>(g.top + g.height));
                }
                pen += static_cast<float>(g.advance);
            }
            if (x1 < x0)
                return;
            const float ox = std::round(cx - (x0 + x1) * 0.5f * scale);
            const float oy = std::round(cy - (y0 + y1) * 0.5f * scale);
            pen = 0.0f;
            for (uint32_t c : cps) {
                const ui::McFont::Glyph& g = font.Get(c);
                if (g.width > 0)
                    text.Quad(ox + (pen + g.left) * scale, oy + g.top * scale, ox + (pen + g.left + g.width) * scale,
                              oy + (g.top + g.height) * scale, g.u0, g.v0, g.u1, g.v1, Pack(1, 1, 1, alpha));
                pen += static_cast<float>(g.advance);
            }
        };
        auto have = [&](int sprite) { return sprites[sprite].id != 0; };
        const auto dim = [&](bool hidden) { return hidden ? 0.35f : 1.0f; };

        const float base = std::clamp(state.opacity, 0.2f, 1.0f);
        const bool stickShown = !stickHidden || editing;
        if (state.dpad && stickShown) {
            const float c = dpadCell;
            const float pitch = dpadPitch;
            const float a = base * dim(stickHidden);
            auto cell = [&](int sprite, int pressed, bool on, int col, int row) {
                const float x = stickCX + (static_cast<float>(col) - 1.0f) * pitch;
                const float y = stickCY + (static_cast<float>(row) - 1.0f) * pitch;
                const int which = on ? pressed : sprite;
                if (have(which))
                    icon(which, x, y, c * 0.5f, c * 0.5f, a);
                else
                    Disc(discs, x, y, c * 0.45f, Pack(1, 1, 1, (on ? 0.5f : 0.25f) * a), false);
            };
            const bool fwd = (dpadDir & 1) != 0;
            cell(DUp, DUpP, fwd && !(dpadDir & 12), 1, 0);
            cell(DDown, DDownP, (dpadDir & 2) != 0, 1, 2);
            cell(DLeft, DLeftP, (dpadDir & 4) != 0 && !fwd, 0, 1);
            cell(DRight, DRightP, (dpadDir & 8) != 0 && !fwd, 2, 1);
            if (fwd || stickActive) {
                cell(DUpLeft, DUpLeftP, fwd && (dpadDir & 4), 0, 0);
                cell(DUpRight, DUpRightP, fwd && (dpadDir & 8), 2, 0);
            }
            if (editing && editSelected == ButtonCount)
                Disc(discs, stickCX, stickCY, pitch * 1.75f, Pack(1, 1, 0.3f, 0.95f), true);
        } else if (stickShown) {
            const float radius = stickR;
            const float cx = stickCX, cy = stickCY;
            const float a = (stickActive ? 1.0f : 0.8f) * dim(stickHidden);
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

        auto frame = [&](const Layout& l, bool active, bool eye, float alpha) {
            const float half = l.r;
            const float u0 = (eye ? 0.5f : 0.0f) + (active ? 0.25f : 0.0f);
            top.Quad(l.cx - half, l.cy - half, l.cx + half, l.cy + half, u0, 0.0f, u0 + 0.25f, 1.0f,
                     Pack(1, 1, 1, alpha));
        };
        auto nine = [&](int sprite, float x0, float y0, float x1, float y1, float k, float alpha) {
            const float xs[4] = {x0, x0 + k, x1 - k, x1}, ys[4] = {y0, y0 + k, y1 - k, y1};
            const float us[4] = {0.0f, 0.25f, 0.75f, 1.0f};
            for (int yy = 0; yy < 3; yy++)
                for (int xx = 0; xx < 3; xx++)
                    icons[sprite].Quad(xs[xx], ys[yy], xs[xx + 1], ys[yy + 1], us[xx], us[yy], us[xx + 1],
                                       us[yy + 1], Pack(1, 1, 1, alpha));
        };

        for (int i = 0; i < ButtonCount; i++) {
            const Layout& l = layout[i];
            if (!l.visible)
                continue;
            const bool active = down[i];
            const float alpha = (active ? 1.0f : 0.9f) * base;
            int sprite = -1;
            int glyph = -1;
            if (i == Inventory && hotbarH > 0.0f) {
                const float k = hotbarH / 22.0f;
                const float x0 = hotbarX + hotbarW - k, y0 = hotbarY;
                if (have(Hotbar8))
                    icon(Hotbar8, x0 + 10.0f * k, y0 + 11.0f * k, 10.0f * k, 11.0f * k, 1.0f);
                if (have(HotbarEndCap))
                    icon(HotbarEndCap, x0 + 20.5f * k, y0 + 11.0f * k, 0.5f * k, 11.0f * k, 1.0f);
                const float bx = x0 + 10.0f * k, by = y0 + 11.0f * k;
                const int b = active ? InvButtonP : InvButton;
                if (have(b))
                    nine(b, bx - 8.0f * k, by - 8.0f * k, bx + 8.0f * k, by + 8.0f * k, k, 0.45f);
                if (have(Elipses))
                    icon(Elipses, bx, by, 7.0f * k, 2.0f * k, 0.65f);
                if (editing && editSelected == i)
                    Disc(discs, l.cx, l.cy, l.r * 1.25f, Pack(1, 1, 0.3f, 0.95f), true);
                continue;
            }
            const bool topBar =
                i == Pause || i == Chat || i == Perspective || i == Debug || i == HideHud || i == Players;
            if (topBar && topTexture) {
                const float fa = alpha * dim(l.hidden);
                const bool gui = guiTexture && (i == Pause || i == Chat);
                if (gui) {
                    const float u0 = (i == Chat ? 0.5f : 0.0f) + (active ? 0.25f : 0.0f);
                    guiTop.Quad(l.cx - l.r, l.cy - l.r, l.cx + l.r, l.cy + l.r, u0, 0.0f, u0 + 0.25f, 1.0f,
                                Pack(1, 1, 1, fa));
                } else {
                    frame(l, active, i == Perspective, fa);
                }
                const float k = l.r * 2.0f / static_cast<float>(std::max(1, topH));
                const float cy = l.cy - 1.5f * k;
                const auto glyphAt = [&](int g) {
                    if (!have(g))
                        return;
                    const Sprite& sp = sprites[g];
                    icon(g, l.cx, cy, sp.w * k * 0.5f, sp.h * k * 0.5f, dim(l.hidden));
                };
                switch (i) {
                case Pause:
                    if (!gui)
                        glyphAt(PauseIcon);
                    break;
                case Chat:
                    if (!gui)
                        glyphAt(ChatIcon);
                    break;
                case Players:
                    glyphAt(PlayersIcon);
                    break;
                case Debug:
                    inked("F3", l.cx, cy, k, dim(l.hidden));
                    break;
                case HideHud:
                    inked("F1", l.cx, cy, k, dim(l.hidden));
                    break;
                default:
                    break;
                }
                if (editing && editSelected == i)
                    Disc(discs, l.cx, l.cy, l.r * 1.25f, Pack(1, 1, 0.3f, 0.95f), true);
                continue;
            }
            switch (i) {
            case Jump:
                if (state.dpad)
                    sprite = state.inWater  ? (active ? CWaterUpP : CWaterUp)
                             : state.flying ? (active ? CFlyUpP : CFlyUp)
                                            : (active ? CJumpP : CJump);
                else
                    sprite = state.inWater  ? (active ? WaterUpP : WaterUp)
                             : state.flying ? (active ? FlyUpP : FlyUp)
                                            : (active ? JumpP : S::Jump);
                break;
            case Sneak:
                if (state.dpad)
                    sprite = state.riding    ? (active ? CDismountP : CDismount)
                             : state.flying  ? (active ? CFlyDownP : CFlyDown)
                             : state.inWater ? (active ? CWaterDownP : CWaterDown)
                                             : (active ? DSneakP : DSneak);
                else
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
                icon(sprite, l.cx, l.cy, l.r, l.r, alpha * dim(l.hidden));
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

        if (!editing && state.interaction != 1 && state.circle && aimFinger >= 0) {
            for (const Finger& f : fingers) {
                if (f.id != aimFinger || !f.aim)
                    continue;
                const float hold = std::max(0.05f, state.holdDelay);
                const float progress =
                    f.holding ? 1.0f
                              : std::clamp(std::chrono::duration<float>(std::chrono::steady_clock::now() - f.downAt)
                                                   .count() /
                                               hold,
                                           0.0f, 1.0f);
                const float R = std::max(6.0f, 0.045f * u * state.circleSize);
                const float t = std::max(2.0f, std::round(R * 0.16f));
                const int segments = 48;
                const float fill = f.moved && !f.holding ? 0.0f : progress;
                const float thin = std::max(1.0f, std::round(t * 0.4f));
                for (int k = 0; k < segments; k++) {
                    const float a0 = -1.5707963f + 6.2831853f * static_cast<float>(k) / segments;
                    const float a1 = -1.5707963f + 6.2831853f * static_cast<float>(k + 1) / segments;
                    const float mid = (static_cast<float>(k) + 0.5f) / segments;
                    const float w = mid <= fill ? t : thin;
                    const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
                    const uint32_t base = static_cast<uint32_t>(ring.vertices.size());
                    const uint32_t white = Pack(1, 1, 1, 1);
                    ring.vertices.push_back({f.lastX + c0 * (R - w), f.lastY + s0 * (R - w), 0.0f, white, 0.25f, 0.5f});
                    ring.vertices.push_back({f.lastX + c0 * R, f.lastY + s0 * R, 0.0f, white, 0.25f, 0.5f});
                    ring.vertices.push_back({f.lastX + c1 * R, f.lastY + s1 * R, 0.0f, white, 0.25f, 0.5f});
                    ring.vertices.push_back({f.lastX + c1 * (R - w), f.lastY + s1 * (R - w), 0.0f, white, 0.25f, 0.5f});
                    for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                        ring.indices.push_back(base + i);
                }
            }
        }

        if (editing) {
            ToolBox boxes[kToolCount];
            const char* selName = SelectedName();
            const auto selIt = selName ? editLayout.find(selName) : editLayout.end();
            ToolBoxes(width, height, boxes, selIt != editLayout.end() && selIt->second.hidden);
            for (const ToolBox& b : boxes) {
                const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f;
                if (have(Knob))
                    icons[Knob].Quad(b.x, b.y, b.x + b.w, b.y + b.h, 0.0f, 0.0f, 1.0f, 1.0f,
                                     Pack(1, 1, 1, 0.95f));
                else
                    Disc(discs, cx, cy, b.h * 0.5f, Pack(0.1f, 0.1f, 0.1f, 0.7f), false);
                label(b.label, cx, cy, std::max(1.0f, std::floor(b.h / 22.0f)), 1.0f);
            }
            label("Drag to move. Select a button, then - / + to resize or Hide to hide it.",
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
        submit(top, topTexture);
        submit(guiTop, guiTexture);
        for (int i = 0; i < SpriteCount; i++)
            submit(icons[i], sprites[i].id);
        submit(text, fontTexture);
        if (!ring.vertices.empty() && discTexture) {
            const uint32_t id =
                renderer.UiCreateFrameMesh(ring.vertices.data(), static_cast<uint32_t>(ring.vertices.size()),
                                           ring.indices.data(), static_cast<uint32_t>(ring.indices.size()));
            if (id) {
                UiDraw draw;
                draw.mesh = id;
                draw.texture = discTexture;
                draw.premultiplied = false;
                draw.customBlend = true;
                draw.blendSrc = BlendFactor::InvDstColor;
                draw.blendDst = BlendFactor::InvSrcColor;
                draw.alphaWrite = false;
                renderer.UiSubmit(UiLayer::Top, draw);
                renderer.UiDestroyMesh(id);
            }
        }
    }
}
