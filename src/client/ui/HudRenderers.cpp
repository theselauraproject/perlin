#include "client/ui/HudRenderers.hpp"
#include "client/ui/UiExpr.hpp"
#include "client/ui/EffectData.hpp"
#include "client/ui/McFont.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace perlin::ui {
    int JavaRandom::Next(int bits) {
        state = (state * 0x5DEECE66Dull + 0xBull) & kMask;
        return static_cast<int>(static_cast<int64_t>(state) >> (48 - bits));
    }

    int JavaRandom::NextInt(int bound) {
        if (bound <= 0)
            return 0;
        int r = Next(31);
        const int m = bound - 1;
        if ((bound & m) == 0)
            return static_cast<int>((static_cast<int64_t>(bound) * static_cast<int64_t>(r)) >> 31);
        for (int u = r; u - (r = u % bound) + m < 0; u = Next(31)) {}
        return r;
    }

    namespace {
        void Tex(RmlWriter& w, const LNode& n, float x, float y, float size, const char* texture,
                 int zOffset = 0, float alpha = 1.0f) {
            ImageStyle st;
            st.alpha = alpha * n.alpha;
            w.Image(x, y, size, size, n.layer + zOffset, std::string("textures/ui/") + texture, st);
        }

        struct HeartSet {
            const char* full;
            const char* half;
            const char* flashFull;
            const char* flashHalf;
        };

        HeartSet HeartsFor(const HudState& s) {
            if (s.wither)
                return {"wither_heart", "wither_heart_half", "wither_heart_flash", "wither_heart_flash_half"};
            if (s.poison)
                return {"poison_heart", "poison_heart_half", "poison_heart_flash", "poison_heart_flash_half"};
            if (s.freezing)
                return {"freeze_heart", "freeze_heart_half", "freeze_heart_flash", "freeze_heart_flash_half"};
            return {"heart", "heart_half", "heart_flash", "heart_flash_half"};
        }

        int HealthRows(const HudState& s) {
            const int hearts = static_cast<int>(std::ceil((s.maxHealth + std::ceil(s.absorption)) / 2.0f));
            return std::max(1, (hearts + 9) / 10);
        }

        int RowHeight(const HudState& s) {
            return std::max(10 - (HealthRows(s) - 2), 3);
        }

        void Hearts(const HudState& s, const LNode& n, RmlWriter& w) {
            const int health = static_cast<int>(std::ceil(s.health));
            const int shown = static_cast<int>(std::ceil(s.displayHealth));
            const int maxHealth = static_cast<int>(std::ceil(s.maxHealth));
            int absorption = static_cast<int>(std::ceil(s.absorption));
            const int rows = HealthRows(s);
            const int rowHeight = RowHeight(s);
            const bool blinking = s.healthBlinkUntil > static_cast<int>(s.tick) &&
                                  ((s.healthBlinkUntil - static_cast<int>(s.tick)) / 3) % 2 == 1;
            const int regen =
                s.regeneration
                    ? static_cast<int>(s.tick % static_cast<uint64_t>(std::ceil(s.maxHealth + 5.0f)))
                    : -1;
            JavaRandom random(static_cast<int64_t>(s.tick) * 312871);
            const HeartSet set = HeartsFor(s);
            const int total = static_cast<int>(std::ceil((s.maxHealth + std::ceil(s.absorption)) / 2.0f));
            const int normalHearts = static_cast<int>(std::ceil(s.maxHealth / 2.0));
            int absorptionLeft = absorption;
            (void)rows;
            for (int i = total - 1; i >= 0; i--) {
                const int row = i / 10, col = i % 10;
                const float x = n.ax + col * 8;
                float y = n.ay - row * rowHeight;
                if (health + absorption <= 4)
                    y += random.NextInt(2);
                if (i < normalHearts && i == regen)
                    y -= 2;
                Tex(w, n, x, y, 9, blinking ? "heart_blink" : "heart_background", 0);
                const int twice = i * 2;
                if (i >= normalHearts) {
                    const int index = twice - maxHealth;
                    if (index < absorption) {
                        const bool half = index + 1 == absorption;
                        Tex(w, n, x, y, 9, half ? "absorption_heart_half" : "absorption_heart", 1);
                        (void)absorptionLeft;
                    }
                    continue;
                }
                if (blinking && twice < shown) {
                    const bool half = twice + 1 == shown;
                    Tex(w, n, x, y, 9, half ? set.flashHalf : set.flashFull, 1);
                }
                if (twice < health) {
                    const bool half = twice + 1 == health;
                    Tex(w, n, x, y, 9, half ? set.half : set.full, 2);
                }
            }
            if (s.hardcore) {}
        }

        void Armor(const HudState& s, const LNode& n, RmlWriter& w) {
            const int rows = HealthRows(s);
            const float y = n.ay - 10 - (rows - 1) * RowHeight(s);
            for (int i = 0; i < 10; i++) {
                const float x = n.ax + i * 8;
                const char* tex = i * 2 + 1 < s.armor ? "armor_full"
                                                      : (i * 2 + 1 == s.armor ? "armor_half" : "armor_empty");
                Tex(w, n, x, y, 9, tex);
            }
        }

        void Hunger(const HudState& s, const LNode& n, RmlWriter& w) {
            JavaRandom random(static_cast<int64_t>(s.tick) * 312871);
            const bool effect = s.hungerEffect;
            const float right = n.ax + 1;
            for (int i = 0; i < 10; i++) {
                const float x = right - i * 8 - 9;
                float y = n.ay;
                if (s.saturation <= 0.0f && s.tick % static_cast<uint64_t>(s.food * 3 + 1) == 0)
                    y += random.NextInt(3) - 1;
                Tex(w, n, x, y, 9, effect ? "hunger_effect_background" : "hunger_background", 0);
                const char* tex = nullptr;
                if (i * 2 + 1 < s.food)
                    tex = effect ? "hunger_effect_full" : "hunger_full";
                else if (i * 2 + 1 == s.food)
                    tex = effect ? "hunger_effect_half" : "hunger_half";
                if (tex)
                    Tex(w, n, x, y, 9, tex, 1);
            }
        }

        void Bubbles(const HudState& s, const LNode& n, RmlWriter& w) {
            if (!s.Survival() || !(s.inWater || s.air < s.maxAir))
                return;
            const int air = std::max(0, s.air);
            const int full = static_cast<int>(std::ceil((air - 2) * 10.0 / s.maxAir));
            const int partial = static_cast<int>(std::ceil(air * 10.0 / s.maxAir)) - full;
            const float right = n.ax + 1;
            for (int i = 0; i < full + partial; i++)
                Tex(w, n, right - i * 8 - 9, n.ay, 9, i < full ? "bubble" : "bubble_pop");
        }

        // totem / omen activation: icon rises from below, swells past the crosshair, fades
        // curve from java GameRenderer item activation
        void ScreenAnimation(const HudState& s, const LNode& n, RmlWriter& w) {
            if (s.screenAnimation < 0)
                return;
            const float f = std::clamp(s.screenAnimationT, 0.0f, 1.0f);
            const float f2 = f * f, f3 = f2 * f;
            const float rise = 10.25f * f3 * f2 - 24.95f * f2 * f2 + 25.5f * f3 - 13.8f * f2 + 4.0f * f;
            const float size = s.guiH * (0.12f + 0.55f * std::sin(rise * 3.14159265f));
            const float x = s.guiW * 0.5f - size * 0.5f;
            const float y = s.guiH * (0.5f + 0.6f * (1.0f - rise)) - size * 0.5f;
            const float alpha = f > 0.8f ? (1.0f - f) / 0.2f : 1.0f;
            if (s.screenAnimation == 1000) {
                w.Item(x, y, size, size, n.layer + 2, "minecraft:totem_of_undying;0", alpha);
                return;
            }
            EffectInfo info;
            if (!GetEffectInfo(s.screenAnimation, info))
                return;
            ImageStyle st;
            st.alpha = alpha;
            w.Image(x, y, size, size, n.layer + 2, std::string("mcres:effect_") + info.texture + ".png", st);
        }

        void Fire(const HudState& s, const LNode& n, RmlWriter& w) {
            ScreenAnimation(s, n, w);
            if (!s.burning)
                return;
            const float unit = s.guiH * 1.4286f;
            const float top = s.guiH * 0.2143f;
            ImageStyle st;
            st.u = 0;
            st.v = static_cast<float>(s.fireFrame * 16);
            st.uw = 16;
            st.vh = 16;
            st.alpha = 0.9f;
            // two animated layers at slightly different heights, mirrors the vanilla composite
            for (const float side : {-1.0f, 1.0f}) {
                w.Image(s.guiW * 0.5f + side * 0.24f * unit - unit * 0.5f, top, unit, unit, n.layer,
                        "textures/blocks/fire_1", st);
                ImageStyle inner = st;
                inner.alpha = 0.7f;
                w.Image(s.guiW * 0.5f + side * 0.18f * unit - unit * 0.4f, top + unit * 0.12f, unit * 0.8f,
                        unit * 0.8f, n.layer + 1, "textures/blocks/fire_0", inner);
            }
        }

        void Offhand(const HudState& s, float slotX, float slotY, float slotW, float slotH, int layer, float alpha,
                     RmlWriter& w) {
            const float unit = slotW / 20.0f;
            const float x = s.offhandRight ? slotX + slotW + unit + 7.0f * unit : slotX - unit - 29.0f * unit;
            const float y = slotY;
            ImageStyle st;
            st.alpha = alpha;
            w.Image(x, y, unit, slotH, layer, "textures/ui/hotbar_start_cap", st);
            w.Image(x + unit, y, slotW, slotH, layer, "textures/ui/hotbar_0", st);
            w.Image(x + unit + slotW, y, unit, slotH, layer, "textures/ui/hotbar_end_cap", st);
            const HudItem& item = s.offhand;
            const float itemSize = 16.0f * unit;
            const float itemX = x + 3.0f * unit, itemY = y + 3.0f * unit;
            w.Item(itemX, itemY, itemSize, itemSize, layer + 1, item.Key(), alpha, item.enchanted);
            if (item.count > 1) {
                const std::string text = std::to_string(item.count);
                TextStyle ts;
                ts.shadow = true;
                ts.scale = unit;
                const float tw = McFont::Instance().Width(text) * unit;
                w.Text(itemX + 17.0f * unit - tw, itemY + 9.0f * unit, tw + unit, 9.0f * unit, layer + 2, {text},
                       ts);
            }
        }

        void Hotbar(const HudState& s, const LNode& n, RmlWriter& w) {
            if (n.index < 0 || n.index > 8)
                return;
            ImageStyle st;
            st.alpha = n.alpha;
            w.Image(n.ax, n.ay, n.w, n.h, n.layer, "textures/ui/hotbar_" + std::to_string(n.index), st);
            if (!s.offhand.Empty() && n.index == (s.offhandRight ? 8 : 0))
                Offhand(s, n.ax, n.ay, n.w, n.h, n.layer, n.alpha, w);
        }

        void HotbarSlots(const HudState& s, const LNode& n, RmlWriter& w) {
            const float unit = n.h / 22.0f;
            const float slotW = 20.0f * unit;
            ImageStyle st;
            st.alpha = n.alpha;
            for (int i = 0; i < 9; i++)
                w.Image(n.ax + static_cast<float>(i) * slotW, n.ay, slotW, n.h, n.layer,
                        "textures/ui/hotbar_" + std::to_string(i), st);
            if (!s.offhand.Empty())
                Offhand(s, n.ax + static_cast<float>(s.offhandRight ? 8 : 0) * slotW, n.ay, slotW, n.h, n.layer, n.alpha,
                        w);
        }

        void InventoryItem(const HudState& s, const LNode& n, RmlWriter& w) {
            if (n.index < 0 || n.index > 8)
                return;
            const HudItem& item = s.hotbar[n.index];
            if (item.Empty())
                return;
            w.Item(n.ax, n.ay, n.w, n.h, n.layer, item.Key(), n.alpha, item.enchanted);
        }

        void ProgressBar(const HudState& s, const LNode& n, RmlWriter& w) {
            const Json* bag = n.Get("property_bag");
            const bool durability =
                bag && bag->Find("is_durability") && bag->Find("is_durability")->BoolOr(false);
            if (!durability)
                return;
            const Json* vis = n.bag.Find("#touch_progress_bar_visible");
            if (!vis || !Truthy(*vis))
                return;
            const Json* total = n.bag.Find("#progress_bar_total_amount");
            const Json* current = n.bag.Find("#progress_bar_current_amount");
            if (!total || !current || total->number <= 0)
                return;
            (void)s;
            const double max = total->number;
            const double damage = max - current->number;
            const double ratio = std::max(0.0, (max - damage) / max);
            const int width = static_cast<int>(std::lround(13.0 - damage * 13.0 / max));
            const double h = ratio / 3.0 * 6.0;
            const double f = h - std::floor(h);
            double r = 0, g = 0, b = 0;
            switch (static_cast<int>(h) % 6) {
            case 0:
                r = 1;
                g = f;
                b = 0;
                break;
            case 1:
                r = 1 - f;
                g = 1;
                b = 0;
                break;
            case 2:
                r = 0;
                g = 1;
                b = f;
                break;
            default:
                r = 0;
                g = 1;
                b = 1;
                break;
            }
            w.Rect(n.ax, n.ay, 13, 2, n.layer, 0, 0, 0, 1);
            w.Rect(n.ax, n.ay, static_cast<float>(width), 1, n.layer + 1, static_cast<float>(r),
                   static_cast<float>(g), static_cast<float>(b), 1);
        }
    }

    namespace {
        void Debug(const HudState& s, const LNode& n, RmlWriter& w) {
            if (!s.debug) {
                if (!s.moveStatus.empty()) {
                    TextStyle ms;
                    ms.shadow = true;
                    ms.r = ms.g = ms.b = 1.0f;
                    float y = 2.0f;
                    size_t from = 0;
                    while (from <= s.moveStatus.size()) {
                        const size_t nl = s.moveStatus.find('\n', from);
                        const std::string line = s.moveStatus.substr(
                            from, nl == std::string::npos ? std::string::npos : nl - from);
                        if (!line.empty())
                            w.Text(2.0f, y, McFont::Instance().Width(line) + 2.0f, 9, n.layer + 1, {line},
                                   ms);
                        y += 10.0f;
                        if (nl == std::string::npos)
                            break;
                        from = nl + 1;
                    }
                }
                return;
            }
            const McFont& font = McFont::Instance();
            TextStyle st;
            st.r = st.g = st.b = 224.0f / 255.0f;
            const float bg = 0x50 / 255.0f, bga = 0x90 / 255.0f;
            const auto column = [&](const std::vector<std::string>& lines, bool right) {
                for (size_t i = 0; i < lines.size(); i++) {
                    if (lines[i].empty())
                        continue;
                    const float width = font.Width(lines[i]);
                    const float y = 2.0f + 9.0f * static_cast<float>(i);
                    const float x = right ? s.guiW - 2.0f - width : 2.0f;
                    w.Rect(x - 1, y - 1, width + 1, 9, n.layer, bg, bg, bg, bga);
                    w.Text(x, y, width + 1, 9, n.layer + 1, {lines[i]}, st);
                }
            };
            column(s.debugLeft, false);
            column(s.debugRight, true);
        }

        void Subtitles(const HudState& s, const LNode& n, RmlWriter& w) {
            if (s.subtitles.empty())
                return;
            const McFont& font = McFont::Instance();
            const float arrowWidth = font.Width("<") + font.Width(" ") + font.Width(">") + font.Width(" ");
            float widest = 0.0f;
            for (const HudState::SubtitleLine& l : s.subtitles)
                widest = std::max(widest, font.Width(l.text));
            const float inner = widest + arrowWidth;
            const float boxW = inner + 4.0f;
            const float left = s.guiW - boxW - 2.0f;
            float y = s.guiH - 30.0f - 10.0f * static_cast<float>(s.subtitles.size());
            for (const HudState::SubtitleLine& l : s.subtitles) {
                w.Rect(left, y, boxW, 10.0f, n.layer, 0, 0, 0, 0.8f);
                TextStyle st;
                st.shadow = false;
                st.r = st.g = st.b = l.brightness;
                const float textW = font.Width(l.text);
                w.Text(left + 2.0f + (inner - textW) * 0.5f, y + 1.0f, textW + 2.0f, 9, n.layer + 1, {l.text},
                       st);
                if (l.direction < 0)
                    w.Text(left + 2.0f, y + 1.0f, 8, 9, n.layer + 1, {"<"}, st);
                else if (l.direction > 0)
                    w.Text(left + boxW - 2.0f - font.Width(">"), y + 1.0f, 8, 9, n.layer + 1, {">"}, st);
                y += 10.0f;
            }
        }

        void TabList(const HudState& s, const LNode& n, RmlWriter& w) {
            if (!s.tabOpen || s.tabEntries.empty())
                return;
            const McFont& font = McFont::Instance();
            const int count = std::min<int>(static_cast<int>(s.tabEntries.size()), 80);
            int columns = 1, rows = count;
            while (rows > 20) {
                columns++;
                rows = (count + columns - 1) / columns;
            }
            float widest = 0.0f;
            for (int i = 0; i < count; i++)
                widest = std::max(widest, font.Width(s.tabEntries[static_cast<size_t>(i)].name));
            const float maxColumn = (s.guiW - 50.0f) / static_cast<float>(columns);
            const float cell = std::min(9.0f + widest + 4.0f, maxColumn);
            const float total = cell * static_cast<float>(columns) + static_cast<float>(columns - 1) * 1.0f;
            const float x0 = std::floor((s.guiW - total) * 0.5f);
            const float y0 = 10.0f;
            const int z = n.layer + 6;
            w.Rect(x0 - 1, y0 - 1, total + 2, static_cast<float>(rows) * 9.0f + 1.0f, z, 0, 0, 0, 0.5f);
            for (int i = 0; i < count; i++) {
                const int col = i / rows, row = i % rows;
                const float x = x0 + static_cast<float>(col) * (cell + 1.0f),
                            y = y0 + static_cast<float>(row) * 9.0f;
                w.Rect(x, y, cell, 8.0f, z + 1, 1, 1, 1, 0.125f);
                const HudState::TabEntry& e = s.tabEntries[static_cast<size_t>(i)];
                ImageStyle st;
                w.Image(x, y, 8, 8, z + 2, "mchead:" + e.headKey, st);
                TextStyle ts;
                ts.shadow = true;
                ts.r = ts.g = ts.b = 1.0f;
                w.Text(x + 9.0f, y, cell - 9.0f - 1.0f, 9, z + 2, {e.name}, ts);
            }
        }

        void Chat(const HudState& s, const LNode& n, RmlWriter& w) {
            TabList(s, n, w);
            const McFont& font = McFont::Instance();
            const float width = 320.0f;
            w.BeginSection();
            for (const HudState::ChatLine& l : s.chatLines) {
                if (l.alpha * 255.0f <= 3.0f)
                    continue;
                const float top = 171.0f - static_cast<float>(l.row) * 9.0f;
                if (top < 0.0f)
                    continue;
                w.Rect(0, top, width + 8, 9, 1, 0, 0, 0, 0.5f * l.alpha);
                for (const HudState::ChatLine::Click& c : l.clicks) {
                    const bool url = c.command.starts_with("url:");
                    if (url)
                        w.Rect(4 + c.x, top + 9.0f, c.w, 1, 3, 0.33f, 0.55f, 1.0f, l.alpha);
                    if (url ? s.chatOpen : true)
                        w.Link(4 + c.x, top, c.w, 9, 4, c.command);
                }
                TextStyle st;
                st.shadow = true;
                st.a = l.alpha;
                w.Text(4, top + 1, width, 9, 2, {l.text}, st);
            }
            w.EndSection("chat_lines");
            w.BeginSection();
            if (s.chatScrolled && s.chatTotalLines > 0) {
                const float track = 180.0f;
                const float thumb =
                    std::max(4.0f, track * 20.0f / static_cast<float>(std::max(20, s.chatTotalLines)));
                const float pos = (track - thumb) * static_cast<float>(s.chatScroll) /
                                  static_cast<float>(std::max(1, s.chatTotalLines - 20));
                w.Rect(0, 180.0f - pos - thumb, 2, thumb, 1, 0.8f, 0.8f, 0.8f, 0.9f);
            }
            w.EndSection("chat_scroll");
            w.BeginSection();
            if (s.chatOpen) {
                const float barW = s.guiW - 4.0f;
                w.Rect(0, 0, barW, 12, 1, 0, 0, 0, 0.5f);
                const std::string& in = s.chatInput;
                const size_t caret = std::min(static_cast<size_t>(std::max(0, s.chatCaret)), in.size());
                const float room = s.guiW - 12.0f;
                size_t start = 0;
                while (start < caret && font.Width(in.substr(start, caret - start)) > room - 6) {
                    start++;
                    while (start < caret && (static_cast<unsigned char>(in[start]) & 0xC0) == 0x80)
                        start++;
                }
                const std::string before = in.substr(start, caret - start);
                const std::string after = in.substr(caret);
                TextStyle st;
                st.shadow = true;
                st.r = st.g = st.b = 224.0f / 255.0f;
                std::string shown;
                {
                    char cur = 'r';
                    const size_t end = in.size();
                    for (size_t k = start; k < end; k++) {
                        const char want =
                            in[k] == ' ' ? cur : (k < s.chatColors.size() ? s.chatColors[k] : 'r');
                        if (want != cur) {
                            shown += "\xC2\xA7";
                            shown += want;
                            cur = want;
                        }
                        shown += in[k];
                    }
                }
                if (s.chatSelEnd > s.chatSelStart) {
                    const size_t a = std::clamp(static_cast<size_t>(s.chatSelStart), start, in.size());
                    const size_t b = std::clamp(static_cast<size_t>(s.chatSelEnd), start, in.size());
                    if (b > a) {
                        const float x0 = 2.0f + font.Width(in.substr(start, a - start));
                        const float x1 = 2.0f + font.Width(in.substr(start, b - start));
                        w.Rect(x0, 1, x1 - x0, 10, 1, 0.24f, 0.42f, 1.0f, 0.7f);
                    }
                }
                w.Text(2, 2, room, 9, 2, {shown}, st);
                const float cx = 2.0f + font.Width(before);
                if (s.chatCaretVisible) {
                    if (caret >= in.size())
                        w.Text(cx, 2, 8, 9, 3, {"_"}, st);
                    else
                        w.Rect(cx, 1, 1, 10, 3, 0.82f, 0.82f, 0.82f, 1.0f);
                }
            }
            if (s.chatOpen && !s.chatGhost.empty()) {
                TextStyle gs;
                gs.shadow = true;
                gs.r = gs.g = gs.b = 128.0f / 255.0f;
                const std::string& in = s.chatInput;
                w.Text(2.0f + font.Width(in), 2, 200, 9, 2, {s.chatGhost}, gs);
            }
            if (s.chatOpen && !s.chatSuggestions.empty()) {
                float boxW = 0;
                for (const std::string& g : s.chatSuggestions)
                    boxW = std::max(boxW, font.Width(g));
                float x =
                    2.0f + font.Width(s.chatInput.substr(
                               0, std::min(static_cast<size_t>(s.chatSuggestStart), s.chatInput.size())));
                x = std::clamp(x, 0.0f, std::max(0.0f, s.guiW - 4.0f - boxW - 1.0f));
                const float rows = static_cast<float>(s.chatSuggestions.size());
                w.Rect(x - 1.0f, -rows * 12.0f - 2.0f, boxW + 3.0f, rows * 12.0f + 2.0f, 4, 0, 0, 0,
                       208.0f / 255.0f);
                for (size_t i = 0; i < s.chatSuggestions.size(); i++) {
                    TextStyle ts;
                    ts.shadow = true;
                    const bool sel = static_cast<int>(i) == s.chatSuggestSelected;
                    if (sel) {
                        ts.r = 1.0f;
                        ts.g = 1.0f;
                        ts.b = 0.0f;
                    } else
                        ts.r = ts.g = ts.b = 170.0f / 255.0f;
                    w.Text(x + 1.0f - 1.0f + 0.0f, -rows * 12.0f + 1.0f + static_cast<float>(i) * 12.0f,
                           boxW + 1.0f, 9, 5, {s.chatSuggestions[i]}, ts);
                }
                const auto dots = [&](float y) {
                    for (float dx = 0; dx < boxW + 1.0f; dx += 2.0f)
                        w.Rect(x + dx, y, 1, 1, 6, 1, 1, 1, 1);
                };
                if (s.chatSuggestMore[0])
                    dots(-rows * 12.0f - 1.0f);
                if (s.chatSuggestMore[1])
                    dots(-1.0f);
            } else if (s.chatOpen && !s.chatHint.empty()) {
                TextStyle ts;
                ts.shadow = true;
                ts.r = ts.g = ts.b = 119.0f / 255.0f;
                w.Text(2.0f + font.Width(s.chatInput) + font.Width(" "), 2, 160, 9, 2, {s.chatHint}, ts);
            }
            w.EndSection("chat_input");
        }
    }

    namespace {
        uint32_t BrightnessNinety(uint32_t rgb) {
            const float r = static_cast<float>((rgb >> 16) & 255) / 255.0f,
                        g = static_cast<float>((rgb >> 8) & 255) / 255.0f,
                        b = static_cast<float>(rgb & 255) / 255.0f;
            const float mx = std::max({r, g, b}), mn = std::min({r, g, b});
            const float d = mx - mn;
            float h = 0.0f;
            if (d > 0.0f) {
                if (mx == r)
                    h = std::fmod((g - b) / d, 6.0f);
                else if (mx == g)
                    h = (b - r) / d + 2.0f;
                else
                    h = (r - g) / d + 4.0f;
                h /= 6.0f;
                if (h < 0.0f)
                    h += 1.0f;
            }
            const float sat = mx > 0.0f ? d / mx : 0.0f;
            const float v = 0.9f;
            const float c = v * sat;
            const float hp = h * 6.0f;
            const float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
            float rr = 0, gg = 0, bb = 0;
            if (hp < 1) {
                rr = c;
                gg = x;
            } else if (hp < 2) {
                rr = x;
                gg = c;
            } else if (hp < 3) {
                gg = c;
                bb = x;
            } else if (hp < 4) {
                gg = x;
                bb = c;
            } else if (hp < 5) {
                rr = x;
                bb = c;
            } else {
                rr = c;
                bb = x;
            }
            const float m = v - c;
            const auto to8 = [&](float q) {
                return static_cast<uint32_t>(std::clamp(std::lround((q + m) * 255.0f), 0l, 255l));
            };
            return (to8(rr) << 16) | (to8(gg) << 8) | to8(bb);
        }

        void MobEffects(const HudState& s, const LNode& n, RmlWriter& w) {
            int beneficial = 0, harmful = 0;
            for (const HudEffect& e : s.effects) {
                EffectInfo info;
                if (!GetEffectInfo(e.id, info))
                    continue;
                const int slot = info.harmful ? harmful++ : beneficial++;
                const float x = n.ax + n.w - 25.0f * static_cast<float>(slot + 1);
                const float y = info.harmful ? 26.0f : 1.0f;
                ImageStyle bg;
                bg.u = e.ambient ? 24.0f : 0.0f;
                bg.v = 0.0f;
                bg.uw = 24.0f;
                bg.vh = 24.0f;
                w.Image(x, y, 24, 24, n.layer, "mcres:ui_hud_effect.png", bg);
                ImageStyle icon;
                w.Image(x + 3.0f, y + 3.0f, 18, 18, n.layer + 1,
                        std::string("mcres:effect_") + info.texture + ".png", icon);
            }
        }

        void LocatorBar(const HudState& s, const LNode& n, RmlWriter& w) {
            if (!s.locatorActive)
                return;
            const float x0 = n.ax + std::floor((n.w - 182.0f) / 2.0f);
            const float y0 = n.ay + n.h - 5.0f;
            ImageStyle bg;
            bg.hasNine = true;
            bg.nine[0] = 5;
            bg.nine[1] = 1;
            bg.nine[2] = 5;
            bg.nine[3] = 1;
            w.Image(x0, y0, 182, 5, n.layer, "textures/ui/locator_bg", bg);
            const float center = std::ceil((n.w - 9.0f) / 2.0f) + n.ax;
            for (const LocatorMarker& m : s.locator) {
                const int index =
                    m.distance < 128.0f ? 0
                    : m.distance >= 332.0f
                        ? 3
                        : 1 + std::min(1, static_cast<int>((m.distance - 128.0f) / (332.0f - 128.0f) * 2.0f));
                const float x = center + std::floor(m.angle * 173.0f / 2.0f / 60.0f);
                ImageStyle dot;
                dot.r = static_cast<float>((m.color >> 16) & 255) / 255.0f;
                dot.g = static_cast<float>((m.color >> 8) & 255) / 255.0f;
                dot.b = static_cast<float>(m.color & 255) / 255.0f;
                w.Image(x, y0 - 2.0f, 9, 9, n.layer + 1,
                        "textures/ui/locator_bar_dot_" + std::to_string(index), dot);
                if (m.pitch != 0) {
                    ImageStyle arrow;
                    w.Image(x + 1.0f, y0 + (m.pitch > 0 ? -6.0f : 6.0f), 7, 5, n.layer + 2,
                            m.pitch > 0 ? "textures/ui/locator_arrow_up" : "textures/ui/locator_arrow_down",
                            arrow);
                }
            }
        }
    }

    uint32_t LocatorColorFor(const std::array<uint8_t, 16>& id) {
        uint64_t msb = 0, lsb = 0;
        for (int i = 0; i < 8; i++)
            msb = (msb << 8) | id[static_cast<size_t>(i)];
        for (int i = 8; i < 16; i++)
            lsb = (lsb << 8) | id[static_cast<size_t>(i)];
        const uint64_t hilo = msb ^ lsb;
        const uint32_t hash = static_cast<uint32_t>(hilo >> 32) ^ static_cast<uint32_t>(hilo);
        return BrightnessNinety(hash & 0xFFFFFFu);
    }

    void RegisterHudRenderers(RmlEmitter& emitter, const HudState& state, const UiServices&) {
        const HudState* s = &state;
        // every vanilla element goes through the mod filter: may hide it or shift its node
        const auto Reg = [&emitter](const char* name, std::function<void(const LNode&, RmlWriter&)> draw) {
            emitter.Register(name, [name, draw = std::move(draw)](const LNode& n, RmlWriter& w) {
                float dx = 0, dy = 0;
                if (!FilterHudElement(name, dx, dy))
                    return;
                if (dx == 0 && dy == 0) {
                    draw(n, w);
                    return;
                }
                // nodes own children, can't be copied: shift for this draw, put back
                LNode& moved = const_cast<LNode&>(n);
                moved.x += dx;
                moved.ax += dx;
                moved.y += dy;
                moved.ay += dy;
                draw(moved, w);
                moved.x -= dx;
                moved.ax -= dx;
                moved.y -= dy;
                moved.ay -= dy;
            });
        };
        Reg("heart_renderer", [s](const LNode& n, RmlWriter& w) { Hearts(*s, n, w); });
        Reg("armor_renderer", [s](const LNode& n, RmlWriter& w) { Armor(*s, n, w); });
        Reg("hunger_renderer", [s](const LNode& n, RmlWriter& w) { Hunger(*s, n, w); });
        Reg("bubbles_renderer", [s](const LNode& n, RmlWriter& w) { Bubbles(*s, n, w); });
        Reg("hotbar_renderer", [s](const LNode& n, RmlWriter& w) { Hotbar(*s, n, w); });
        Reg("hotbar_slots_renderer", [s](const LNode& n, RmlWriter& w) { HotbarSlots(*s, n, w); });
        Reg("inventory_item_renderer", [s](const LNode& n, RmlWriter& w) { InventoryItem(*s, n, w); });
        Reg("debug_renderer", [s](const LNode& n, RmlWriter& w) { Debug(*s, n, w); });
        Reg("chat_renderer", [s](const LNode& n, RmlWriter& w) { Chat(*s, n, w); });
        Reg("subtitle_renderer", [s](const LNode& n, RmlWriter& w) { Subtitles(*s, n, w); });
        Reg("fire_renderer", [s](const LNode& n, RmlWriter& w) { Fire(*s, n, w); });
        Reg("progress_bar_renderer", [s](const LNode& n, RmlWriter& w) { ProgressBar(*s, n, w); });
        Reg("locator_bar", [s](const LNode& n, RmlWriter& w) { LocatorBar(*s, n, w); });
        Reg("mob_effects_renderer", [s](const LNode& n, RmlWriter& w) { MobEffects(*s, n, w); });
        for (const char* name :
             {"horse_jump_renderer", "dash_renderer", "vignette_renderer", "camera_renderer",
              "hud_player_renderer", "horse_heart_renderer", "cooldown_renderer", "editor_gizmo_renderer",
              "editor_compass_renderer", "editor_volume_highlight_renderer", "progress_indicator_renderer"})
            emitter.Register(name, [](const LNode&, RmlWriter&) {});
    }

    namespace {
        HudElementFilter& ElementFilter() {
            static HudElementFilter filter;
            return filter;
        }
        std::atomic<uint64_t> gFilterVersion{0};
    }

    uint64_t HudFilterVersion() {
        return gFilterVersion.load();
    }
    void BumpHudFilter() {
        gFilterVersion++;
    }

    void SetHudElementFilter(HudElementFilter filter) {
        ElementFilter() = std::move(filter);
    }

    bool FilterHudElement(std::string_view renderer, float& dx, float& dy) {
        const HudElementFilter& f = ElementFilter();
        return !f || f(renderer, dx, dy);
    }
}
