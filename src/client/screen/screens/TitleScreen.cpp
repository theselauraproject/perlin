#include "client/screen/screens/TitleScreen.hpp"
#include "util/Resources.hpp"
#include "client/systems/sprites/UiAssets.hpp"

#include "client/systems/text/FormatCodes.hpp"
#include "client/ui/SceneKit.hpp"
#include "client/ui/TextEdit.hpp"

#include <algorithm>
#include "client/systems/resources/TexturePack.hpp"
#include "util/TextMetrics.hpp"
#include <cctype>
#include <string>
#include <utility>

namespace perlin::ui {
    namespace {
        std::string N(int v) {
            return std::to_string(v);
        }

        constexpr int kRowW = 305;
        constexpr int kRowH = 36;

        using Sb = SceneBuilder;

        SceneNode& Fill(Sb& b, int x, int y, int w, int h, const char* color, int z) {
            return kit::Rect(b, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                             static_cast<float>(h), color, N(z));
        }

        SceneNode& Sprite(Sb& b, int x, int y, int w, int h, const std::string& src, const std::string& nine = {},
                          const std::string& cls = {}, const std::string& z = "auto") {
            SceneNode& n = kit::Image(b, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                                      static_cast<float>(h), src, z);
            n.nine = nine;
            n.cls = cls;
            return n;
        }

        void Txt(Sb& b, int x, int y, int w, std::vector<SceneRun> runs, const std::string& cls = {}, int z = 4,
                 bool clip = false) {
            SceneNode& node = kit::Label(b, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                                         std::string("txt") + (cls.empty() ? "" : " " + cls) + (clip ? " clip" : ""),
                                         std::move(runs), N(z));
            if (clip)
                node.h = 10;
        }

        void Tip(Sb& b, int buttonW, const std::string& text) {
            std::vector<std::string> lines;
            std::string line;
            const size_t perLine = static_cast<size_t>(std::max(12, (buttonW - 10) / 5));
            size_t at = 0;
            while (at < text.size()) {
                const size_t sp = text.find(' ', at);
                const std::string word =
                    text.substr(at, sp == std::string::npos ? std::string::npos : sp - at);
                if (!line.empty() && line.size() + 1 + word.size() > perLine) {
                    lines.push_back(line);
                    line.clear();
                }
                line += (line.empty() ? "" : " ") + word;
                if (sp == std::string::npos)
                    break;
                at = sp + 1;
            }
            if (!line.empty())
                lines.push_back(line);
            const int th = static_cast<int>(lines.size()) * 10 + 5;
            const int tw = buttonW;
            kit::Begin(b, 0, static_cast<float>(-th - 3), static_cast<float>(tw), static_cast<float>(th), "tip", {},
                       "60");
            Fill(b, 0, 0, tw, th, "#2d0a63", 0);
            Fill(b, 1, 1, tw - 2, th - 2, "#100010", 0);
            for (size_t i = 0; i < lines.size(); i++)
                Txt(b, 3, 3 + static_cast<int>(i) * 10, tw - 6, kit::Plain(lines[i], "#ffff55"), "ctr");
            b.End();
        }

        void Btn(Sb& b, const std::string& id, int x, int y, int w, const std::string& label, bool off = false,
                 const std::string& tip = {}) {
            kit::Begin(b, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), 20,
                       off ? "btn off" : "btn", off ? std::string() : id, "5");
            Sprite(b, 0, 0, w, 20, off ? "mcres:ui_menu_btn_off.png" : "mcres:ui_menu_btn.png", "3 3 3 3");
            if (!off)
                Sprite(b, 0, 0, w, 20, "mcres:ui_menu_btn_hi.png", "3 3 3 3", "h");
            Txt(b, 0, 6, w, kit::Plain(label), "ctr blab", 0);
            if (!tip.empty())
                Tip(b, w, tip);
            b.End();
        }

        void Head(Sb& b, const std::string& key, int x, int y, int size, const std::string& z = "auto") {
            if (!key.empty())
                Sprite(b, x, y, size, size, "mchead:" + key, {}, {}, z);
        }

        void HeadBtn(Sb& b, const std::string& id, int x, int y, int w, const std::string& label,
                     const std::string& head) {
            kit::Begin(b, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), 20, "btn", id, "5");
            Sprite(b, 0, 0, w, 20, "mcres:ui_menu_btn.png", "3 3 3 3");
            Sprite(b, 0, 0, w, 20, "mcres:ui_menu_btn_hi.png", "3 3 3 3", "h");
            const int textX = head.empty() ? 6 : 20;
            Head(b, head, 4, 4, 12);
            Txt(b, textX, 6, w - textX - 4, kit::Plain(label), "blab", 0);
            b.End();
        }

        void IconBtn(Sb& b, const std::string& id, int x, int y, const std::string& icon) {
            kit::Begin(b, static_cast<float>(x), static_cast<float>(y), 20, 20, "btn", id, "5");
            Sprite(b, 0, 0, 20, 20, "mcres:ui_menu_btn.png", "3 3 3 3");
            Sprite(b, 0, 0, 20, 20, "mcres:ui_menu_btn_hi.png", "3 3 3 3", "h");
            Sprite(b, 4, 4, 12, 12, icon);
            b.End();
        }

        void PingBars(Sb& b, int x, int y, ServerEntry::Status status, int pingMs, int spinner) {
            using St = ServerEntry::Status;
            const bool pinging = status == St::Pinging || status == St::Unknown;
            int level = 0;
            const char* color = "#55ff55";
            std::string tip;
            if (pinging) {
                level = 1 + spinner % 5;
                color = "#808080";
                tip = "Pinging...";
            } else if (status == St::Offline) {
                color = "#ff5555";
                tip = "No connection";
            } else {
                level = pingMs < 150 ? 5 : pingMs < 300 ? 4 : pingMs < 600 ? 3 : pingMs < 1000 ? 2 : 1;
                color = level >= 4 ? "#55ff55" : (level == 3 ? "#ffff55" : "#ff5555");
                tip = N(pingMs) + "ms";
            }
            kit::Begin(b, static_cast<float>(x), static_cast<float>(y), 10, 8, "pg");
            std::string sprite;
            if (!pinging) {
                sprite = status == St::Offline || level <= 2
                             ? "textures/ui/Ping_Red"
                             : (level == 3 ? "textures/ui/Ping_Yellow" : "textures/ui/Ping_Green");
                int iw = 0, ih = 0;
                if (!UiAssets::Instance().ImageSize(sprite, iw, ih))
                    sprite.clear();
            } else if (TexturePack::Instance().Active()) {
                const std::string name = "textures/ui/ping_anim_" + N(5 - level);
                if (TexturePack::Instance().Has(name + ".png"))
                    sprite = name;
            }
            if (!sprite.empty()) {
                Sprite(b, 0, 0, 10, 8, sprite);
            } else {
                static const int kHeights[5] = {2, 3, 5, 6, 8};
                for (int i = 0; i < 5; i++) {
                    const bool lit = i < level;
                    Fill(b, i * 2, 8 - kHeights[i], 2, kHeights[i], lit ? color : "#303030", 0);
                }
                if (status == St::Offline)
                    Txt(b, 3, 0, 6, kit::Plain("x", "#ff5555"), "", 4);
            }
            const int tw = static_cast<int>(tip.size()) * 5 + 8;
            kit::Begin(b, static_cast<float>(10 - tw), 11, static_cast<float>(tw), 13, "pt", {}, "60");
            Fill(b, 0, 0, tw, 13, "#2d0a63", 0);
            Fill(b, 1, 1, tw - 2, 11, "#100010", 0);
            Txt(b, 4, 2, tw - 6, kit::Plain(tip));
            b.End();
            b.End();
        }

        std::string Fit(const std::string& text, int widthPx) {
            int width = 0;
            const int limit = std::max(12, widthPx - 2);
            size_t cut = std::string::npos;
            for (size_t i = 0; i < text.size(); i++) {
                if (IsFormatCode(text, i)) {
                    i += 2;
                    continue;
                }
                if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80)
                    continue;
                const int glyph = perlin::text::DefaultGlyphWidth(text[i]) + 1;
                if (width + glyph + 6 <= limit)
                    cut = i + 1;
                width += glyph;
                if (width > limit) {
                    size_t end = cut == std::string::npos ? 0 : cut;
                    while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
                        end++;
                    return text.substr(0, end) + "\xC2\xA7r...";
                }
            }
            return text;
        }

        void RowText(Sb& b, int x, int y, int w, const std::string& text, const std::string& cls = "") {
            Txt(b, x, y, w, kit::Formatted(Fit(text, w)), cls, 4, true);
        }

        void Text(Sb& b, int x, int y, int w, const std::string& text, const std::string& cls = "") {
            Txt(b, x, y, w, kit::Formatted(text), cls);
        }

        void Plain(Sb& b, int x, int y, int w, const std::string& text, const std::string& cls = "", int z = 4) {
            Txt(b, x, y, w, kit::Plain(text), cls, z);
        }

        void Dirt(Sb& b, int x, int y, int w, int h, const char* shade, const std::string& z = "auto") {
            SceneNode& n = Sprite(b, x, y, w, h, "textures/blocks/dirt", {}, {}, z);
            n.tiled = "1";
            n.tileScale = "2";
            n.tint = shade;
        }

        void Backdrop(Sb& b, const TitleModel& m, int w, int h) {
            if (!m.nativePanorama)
                Dirt(b, 0, 0, w, h, "0.25 0.25 0.25");
            else
                Fill(b, 0, 0, w, h, "#00000040", 0);
        }

        void ListBand(Sb& b, const TitleModel& m, int w, int top, int bottom) {
            if (!m.nativePanorama) {
                Dirt(b, 0, top, w, bottom - top, "0.12 0.12 0.12", "1");
                kit::Gradient(b, 0, static_cast<float>(top), static_cast<float>(w), 4,
                              "vertical-gradient(#000000ff #00000000)", "2");
                kit::Gradient(b, 0, static_cast<float>(bottom - 4), static_cast<float>(w), 4,
                              "vertical-gradient(#00000000 #000000ff)", "2");
                Fill(b, 0, top - 1, w, 1, "#000000", 3);
                Fill(b, 0, bottom, w, 1, "#000000", 3);
                return;
            }
            Fill(b, 0, top, w, bottom - top, "#00000070", 1);
            Fill(b, 0, top - 2, w, 1, "#ffffff33", 3);
            Fill(b, 0, top - 1, w, 1, "#000000bf", 3);
            Fill(b, 0, bottom, w, 1, "#000000bf", 3);
            Fill(b, 0, bottom + 1, w, 1, "#ffffff33", 3);
        }

        void Field(Sb& b, const std::string& id, int x, int y, int w, const std::string& value, bool focused,
                   bool caret) {
            kit::Begin(b, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), 20, "field", id, "5");
            Sprite(b, 0, 0, w, 20, focused ? "mcres:ui_menu_field_hi.png" : "mcres:ui_menu_field.png", "1 1 1 1");
            Txt(b, 4, 6, w - 8, focused ? TextFieldRuns(value, caret) : kit::Plain(value), "val", 4, true);
            b.End();
        }
        void Panel(Sb& b, int x, int y, int w, int h) {
            Fill(b, x - 2, y - 2, w + 4, h + 4, "#000000", 11);
            Fill(b, x - 1, y - 1, w + 2, h + 2, "#e0e0e0", 11);
            Fill(b, x + 1, y + 1, w - 2, h - 2, "#373737", 11);
            Fill(b, x + 2, y + 2, w - 4, h - 4, "#2b2b2b", 11);
        }

        void Shade(Sb& b, int w, int h) {
            Fill(b, 0, 0, w, h, "#000000a0", 10);
        }

        void Panorama(Sb& b, int w, int h) {
            const int face = std::max(h * 5 / 4, 64);
            const int top = -(face - h) / 2;
            kit::Begin(b, 0, 0, static_cast<float>(w), static_cast<float>(h), "clip panoclip");
            kit::Begin(b, 0, static_cast<float>(top), static_cast<float>(face * 5), static_cast<float>(face), {},
                       "pano");
            for (int i = 0; i < 5; i++)
                Sprite(b, i * face, 0, face, face, "textures/ui/panorama_" + N(i % 4));
            b.End();
            b.End();
        }

        void PanoramaFallback(Sb& b, const TitleModel& m, int w, int h) {
            if (!m.nativePanorama)
                Panorama(b, w, h);
        }

        void SmallBtn(Sb& b, const std::string& id, int x, int y, int w, const std::string& label) {
            kit::Begin(b, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), 20, "sbtn", id, "15");
            Sprite(b, 0, 0, w, 20, "mcres:ui_menu_btn.png", "3 3 3 3");
            Sprite(b, 0, 0, w, 20, "mcres:ui_menu_btn_hi.png", "3 3 3 3", "h");
            Txt(b, 0, 6, w, kit::Plain(label), "ctr blab", 0);
            b.End();
        }

        void Accounts(Sb& b, const TitleModel& m, int w, int h) {
            PanoramaFallback(b, m, w, h);
            Shade(b, w, h);
            const int pw = 240;
            const int rowH = 24;
            const int count = static_cast<int>(m.accounts.size());
            const int rows = std::max(1, std::min(count, TitleAccountRows(h)));
            const int scroll = std::clamp(m.accountScroll, 0, std::max(0, count - rows));
            const int ph = 46 + rows * rowH + 56;
            const int px = w / 2 - pw / 2, py = std::max(20, h / 2 - ph / 2 + 8);
            Panel(b, px, py, pw, ph);
            Fill(b, px + pw / 2 - 40, py - 16, 80, 16, "#e0e0e0", 11);
            Fill(b, px + pw / 2 - 39, py - 15, 78, 16, "#2b2b2b", 12);
            Plain(b, px + pw / 2 - 40, py - 11, 80, "Accounts", "ctr", 13);
            Fill(b, px + pw / 2 - 22, py - 1, 44, 1, "#ffffff", 13);
            const int labelX = m.accountHead.empty() ? 10 : 30;
            if (!m.accountHead.empty())
                Head(b, m.accountHead, px + 10, py + 11, 14, "13");
            Plain(b, px + labelX, py + 14, pw - labelX - 10, m.accountLabel, "dim", 13);
            Fill(b, px + 2, py + 35, pw - 4, 1, "#555555", 12);
            int y = py + 41;
            if (m.accounts.empty())
                Plain(b, px, y + 6, pw, "No accounts yet", "ctr gray", 13);
            if (count > rows) {
                const int trackH = rows * rowH - 2;
                const int thumbH = std::max(8, trackH * rows / count);
                const int thumbY = (trackH - thumbH) * scroll / std::max(1, count - rows);
                Fill(b, px + pw - 6, y, 2, trackH, "#1a1a1a", 13);
                Fill(b, px + pw - 6, y + thumbY, 2, thumbH, "#a0a0a0", 13);
            }
            for (int i = scroll; i < count && i < scroll + rows; i++, y += rowH) {
                const AccountRow& a = m.accounts[static_cast<size_t>(i)];
                kit::Begin(b, static_cast<float>(px + 6), static_cast<float>(y), static_cast<float>(pw - 36),
                           static_cast<float>(rowH - 2), "acc", std::string(kAccountRowPrefix) + N(i), "13");
                Fill(b, 0, 0, pw - 36, rowH - 2, "#ffffff20", 0).cls = "hl";
                if (a.active)
                    Fill(b, 0, 0, 2, rowH - 2, "#55ff55", 0);
                if (!a.headKey.empty()) {
                    Head(b, a.headKey, 5, 3, 16);
                } else {
                    const std::string initial =
                        a.name.empty()
                            ? "?"
                            : std::string(
                                  1, static_cast<char>(std::toupper(static_cast<unsigned char>(a.name[0]))));
                    Fill(b, 5, 3, 16, 16, a.microsoft ? "#3c8527" : "#5a5a5a", 0);
                    Plain(b, 5, 7, 16, initial, "ctr");
                }
                Txt(b, 26, 2, pw - 70,
                    kit::Plain(a.name.empty() ? (a.microsoft ? "(gamertag not known yet)" : "?") : a.name), "", 4,
                    true);
                const std::string kind = std::string(a.microsoft ? "Microsoft account" : "Offline account") +
                                         (a.active ? " - in use" : "");
                Plain(b, 26, 12, pw - 70, kind, a.active ? "green" : "gray");
                b.End();
                SmallBtn(b, std::string(kAccountRemovePrefix) + N(i), px + pw - 28, y + 1, 20, "x");
            }
            const int by = py + ph - 50;
            Fill(b, px + 2, by - 6, pw - 4, 1, "#555555", 12);
            SmallBtn(b, kAccountAddMicrosoft, px + 8, by, (pw - 20) / 2, "Add Microsoft");
            SmallBtn(b, kAccountAddOffline, px + 12 + (pw - 20) / 2, by, (pw - 20) / 2, "Add Offline");
            SmallBtn(b, kAccountDone, px + 8, by + 24, pw - 16, "Done");
        }

        void AccountName(Sb& b, const TitleModel& m, int w, int h) {
            PanoramaFallback(b, m, w, h);
            Shade(b, w, h);
            const int pw = 240, ph = 104;
            const int px = w / 2 - pw / 2, py = std::max(8, h / 2 - ph / 2);
            Panel(b, px, py, pw, ph);
            Plain(b, px, py + 10, pw, "Add Offline Account", "ctr", 13);
            Plain(b, px + 12, py + 28, pw - 24, "Username", "dim", 13);
            kit::Begin(b, 0, 0, 0, 0, {}, {}, "14");
            Field(b, kAccountNameField, px + 12, py + 40, pw - 24, m.accountName, true, m.caret);
            b.End();
            SmallBtn(b, kAccountNameDone, px + 8, py + ph - 28, (pw - 20) / 2, "Add");
            SmallBtn(b, kAccountCancel, px + 12 + (pw - 20) / 2, py + ph - 28, (pw - 20) / 2, "Cancel");
        }

        void AccountSignIn(Sb& b, const TitleModel& m, int w, int h) {
            PanoramaFallback(b, m, w, h);
            Shade(b, w, h);
            const int pw = 260, ph = 120;
            const int px = w / 2 - pw / 2, py = std::max(8, h / 2 - ph / 2);
            Panel(b, px, py, pw, ph);
            const auto line = [&](int y, const std::string& text, const std::string& cls) {
                Plain(b, px, py + y, pw, text, "ctr " + cls, 13);
            };
            line(10, "Sign in with Microsoft", "");
            if (m.signInCode.empty()) {
                line(40, m.signInStatus.empty() ? "Contacting Microsoft..." : m.signInStatus, "gray");
            } else {
                line(30, "Open this page in your browser:", "dim");
                line(42, m.signInUrl, "yellow");
                line(58, "and enter the code:", "dim");
                line(70, m.signInCode, "green");
                if (!m.signInStatus.empty())
                    line(84, m.signInStatus, "gray");
            }
            SmallBtn(b, kAccountCancel, px + pw / 2 - 50, py + ph - 26, 100, "Cancel");
        }

        void Main(Sb& b, const TitleModel& m, int w, int h) {
            if (!m.nativePanorama)
                Panorama(b, w, h);
            const int cx = w / 2;
            if (!m.logoSrc.empty()) {
                const int lw = std::clamp(m.logoW, 1, 256), lh = std::clamp(m.logoH, 1, 64);
                Sprite(b, cx - lw / 2, 30 + (64 - lh) / 2, lw, lh, m.logoSrc, {}, {}, "3");
            } else {
                Sprite(b, cx - 128, 30, 256, 64, "mcres:ui_menu_logo.png", {}, {}, "3");
            }
            const int j = h / 4 + 48;
            Btn(b, kTitleSingle, cx - 100, j, 200, "Singleplayer", false, kSingleplayerTip);
            Btn(b, kTitleMulti, cx - 100, j + 24, 200, "Multiplayer");
            Btn(b, kTitleMods, cx - 100, j + 48, 200, "Mods");
            IconBtn(b, kTitleWardrobe, cx + 104, j + 48, "textures/ui/glyph_skin_pack.png");
            Btn(b, kTitleReplays, cx - 100, j + 72, 200, "Replays");
            Btn(b, kTitleOptions, cx - 100, j + 102, 98, "Options...");
            Btn(b, kTitleQuit, cx + 2, j + 102, 98, "Quit Game");
            if (!m.accountUser.empty()) {
                const int textX = m.accountHead.empty() ? 6 : 20;
                const int bw = std::clamp(textX + text::DefaultTextWidth(m.accountUser) + 7, 40, std::max(40, w - 8));
                HeadBtn(b, kTitleAccounts, 4, 4, bw, m.accountUser, m.accountHead);
            }
            if (m.updateAction)
                Btn(b, kTitleUpdate, 2, h - 34, std::clamp(text::DefaultTextWidth(m.updateText) + 12, 120, w / 2), m.updateText);
            else if (!m.updateText.empty())
                Text(b, 2, h - 20, w / 2, m.updateText);
            Text(b, 2, h - 10, w / 2, m.version);
            Plain(b, w / 2, h - 10, w / 2 - 1, m.copyright, "right");
        }

        void SelectionFrame(Sb& b, int rowX, int y) {
            Fill(b, rowX - 2, y - 2, kRowW + 4, kRowH, "#ffffff", 2);
            Fill(b, rowX - 1, y - 1, kRowW + 2, kRowH - 2, "#000000", 2);
        }

        void Servers(Sb& b, const TitleModel& m, int w, int h) {
            Backdrop(b, m, w, h);
            const int cx = w / 2;
            const int listTop = TitleListTop(), listBottom = TitleListBottom(h);
            ListBand(b, m, w, listTop, listBottom);
            {
                static const struct {
                    ServerTab tab;
                    const char* label;
                } kTabs[] = {{ServerTab::Lan, "LAN"},
                             {ServerTab::Friends, "Friends"},
                             {ServerTab::Realms, "Realms"},
                             {ServerTab::Saved, "Servers"}};
                const int tabW = 70, gap = 2;
                const int x0 = cx - (4 * tabW + 3 * gap) / 2;
                for (int t = 0; t < 4; t++)
                    Btn(b, std::string(kServerTabPrefix) + N(static_cast<int>(kTabs[t].tab)),
                        x0 + t * (tabW + gap), 6, tabW, kTabs[t].label, m.tab == kTabs[t].tab);
            }
            const std::vector<ServerEntry>& list = m.Shown();
            const int rows = TitleVisibleRows(h);
            const int rowX = cx - kRowW / 2;
            int y = listTop + 4;
            for (int i = m.scroll; i < static_cast<int>(list.size()) && i < m.scroll + rows;
                 i++, y += kRowH) {
                const ServerEntry& s = list[static_cast<size_t>(i)];
                if (m.tab == ServerTab::Saved && m.friendsOpen[3] && m.savedOffset > 1 &&
                    i == m.savedOffset && i > m.scroll) {
                    constexpr int kGroupGap = 12;
                    if (y + kGroupGap + kRowH - 4 > listBottom)
                        break;
                    Fill(b, rowX, y + kGroupGap / 2 - 1, kRowW, 1, "#3a3a3a", 2);
                    y += kGroupGap;
                }
                if (i == m.selected)
                    SelectionFrame(b, rowX, y);
                const std::string rowZ = N(6 + rows - (i - m.scroll));
                const std::string rowId = std::string(kServerRowPrefix) + N(i);
                kit::Begin(b, static_cast<float>(rowX), static_cast<float>(y), kRowW, kRowH - 4, "row", rowId, rowZ);
                if (s.kind == ServerEntry::Kind::Header) {
                    const bool open = m.friendsOpen[std::clamp<long long>(s.realmId, 0, 3)];
                    Sprite(b, 4, 11, 8, 8,
                           open ? "textures/ui/chevron_white_down.png" : "textures/ui/chevron_white_up.png");
                    Text(b, 18, 11, kRowW - 20, s.name,
                         s.realmId == 2 ? "gray" : (s.realmId == 3 ? "yellow" : "green"));
                    b.End();
                    continue;
                }
                Fill(b, 0, 0, 32, 32, "#3a2f26", 0);
                if (!s.icon.empty()) {
                    SceneNode& icon = Sprite(b, 0, 0, 32, 32, "mchead:" + s.icon);
                    if (s.offline)
                        icon.alpha = "0.5";
                }
                if (Joinable(s)) {
                    kit::Begin(b, 0, 0, 32, 32, "go", std::string(kServerJoinPrefix) + N(i));
                    Sprite(b, 0, 0, 32, 32, "mcres:ui_menu_join_hi.png");
                    b.End();
                }
                RowText(b, 35, 1, kRowW - 140, s.name.empty() ? "Minecraft Server" : s.name);
                using St = ServerEntry::Status;
                if (s.kind != ServerEntry::Kind::Server) {
                    const int labelRight = s.pingable ? 22 : 5;
                    Plain(b, kRowW - labelRight - 100, 1, 100,
                          s.pingable && s.status == ServerEntry::Status::Online
                              ? N(s.players) + "/" + N(s.maxPlayers)
                              : s.right,
                          std::string("right ") + (s.offline ? "dark" : "green"));
                    if (s.pingable)
                        PingBars(b, kRowW - 15, 1, s.status, s.pingMs, m.spinner);
                    RowText(b, 35, 12, kRowW - 40, s.motd.empty() ? " " : s.motd, s.offline ? "dark" : "gray");
                    RowText(b, 35, 23, kRowW - 40, s.note, "dark");
                    b.End();
                    continue;
                }
                if (s.status == St::Online)
                    Plain(b, kRowW - 122, 1, 100, N(s.players) + "/" + N(s.maxPlayers), "right gray");
                PingBars(b, kRowW - 15, 1, s.status, s.pingMs, m.spinner);
                if (s.status == St::Online) {
                    RowText(b, 35, 12, kRowW - 40, s.motd.empty() ? "A Minecraft Server" : s.motd, "gray");
                    RowText(b, 35, 23, kRowW - 40, s.address + (s.version.empty() ? "" : "  -  " + s.version),
                            "dark");
                } else if (s.status == St::Offline) {
                    RowText(b, 35, 12, kRowW - 40, "Can't connect to server", "red");
                    RowText(b, 35, 23, kRowW - 40, s.address, "dark");
                } else {
                    RowText(b, 35, 12, kRowW - 40, s.address, "gray");
                }
                b.End();
            }
            if (list.empty() && m.tab != ServerTab::Saved) {
                std::string msg = m.tabMessage;
                const bool busy = msg.starts_with("Scanning") || msg.starts_with("Loading");
                if (busy) {
                    while (!msg.empty() && msg.back() == '.')
                        msg.pop_back();
                    msg += std::string(static_cast<size_t>(m.spinner & 3), '.');
                }
                Plain(b, 0, listTop + 28, w, msg, "ctr");
            } else if (list.empty()) {
                Plain(b, 0, listTop + 28, w,
                      "Scanning for games on your local network" + std::string(static_cast<size_t>(m.spinner & 3), '.'),
                      "ctr");
                static const char* const kSpin[] = {"O o o", "o O o", "o o O", "o O o"};
                Plain(b, 0, listTop + 40, w, kSpin[m.spinner & 3], "ctr gray");
            }
            if (!m.joinMessage.empty())
                Plain(b, 0, h - 62, w, m.joinMessage, "ctr yellow");
            const bool has = m.selected >= 0 && m.selected < static_cast<int>(list.size()) &&
                             Joinable(list[static_cast<size_t>(m.selected)]);
            const bool hasRow = m.selected >= 0 && m.selected < static_cast<int>(list.size()) &&
                                (m.tab != ServerTab::Saved || m.SavedIndex() >= 0);
            const bool saved = m.tab == ServerTab::Saved;
            const int r1 = h - 52, r2 = h - 28;
            Btn(b, has ? kServerJoin : "", cx - 154, r1, 100,
                m.tab == ServerTab::Friends ? "Join Friend"
                                            : (m.tab == ServerTab::Realms ? "Join Realm" : "Join Server"),
                !has);
            Btn(b, kServerDirect, cx - 50, r1, 100, "Direct Connection");
            if (m.tab == ServerTab::Realms)
                Btn(b, kServerAddRealm, cx + 54, r1, 100, "Add Realm");
            else
                Btn(b, saved ? kServerAdd : "", cx + 54, r1, 100, "Add Server", !saved);
            Btn(b, hasRow && saved ? kServerEdit : "", cx - 154, r2, 72, "Edit", !(hasRow && saved));
            Btn(b, hasRow && saved ? kServerDelete : "", cx - 74, r2, 72, "Delete", !(hasRow && saved));
            Btn(b, kServerRefresh, cx + 4, r2, 72, "Refresh");
            Btn(b, kServerCancel, cx + 82, r2, 72, "Cancel");
        }

        void Edit(Sb& b, const TitleModel& m, int w, int h) {
            Backdrop(b, m, w, h);
            const int cx = w / 2;
            Plain(b, 0, 17, w, m.adding ? "Add Server" : "Edit Server Info", "ctr");
            Text(b, cx - 100, 53, 200, "Server Name", "dim");
            Field(b, kEditName, cx - 100, 66, 200, m.name, m.focus == 0, m.caret);
            Text(b, cx - 100, 94, 200, "Server Address", "dim");
            Field(b, kEditAddress, cx - 100, 106, 200, m.address, m.focus == 1, m.caret);
            Btn(b, kEditDone, cx - 100, h / 4 + 96 + 18, 200, "Done");
            Btn(b, kEditCancel, cx - 100, h / 4 + 120 + 18, 200, "Cancel");
        }

        void Direct(Sb& b, const TitleModel& m, int w, int h) {
            Backdrop(b, m, w, h);
            const int cx = w / 2;
            Plain(b, 0, 20, w, "Direct Connection", "ctr");
            Text(b, cx - 100, 94, 200, "Server Address", "dim");
            Field(b, kEditAddress, cx - 100, 106, 200, m.address, true, m.caret);
            Btn(b, kEditDone, cx - 100, h / 4 + 96 + 12, 200, "Join Server");
            Btn(b, kEditCancel, cx - 100, h / 4 + 120 + 12, 200, "Cancel");
        }

        void AddRealm(Sb& b, const TitleModel& m, int w, int h) {
            Backdrop(b, m, w, h);
            const int cx = w / 2;
            Plain(b, 0, 20, w, "Add Realm", "ctr");
            Text(b, cx - 100, 94, 200, "Realm invite code or link", "dim");
            Field(b, kEditAddress, cx - 100, 106, 200, m.address, true, m.caret);
            if (!m.joinMessage.empty())
                Plain(b, 0, 132, w, m.joinMessage, "ctr yellow");
            Btn(b, kEditDone, cx - 100, h / 4 + 96 + 12, 200, "Add Realm");
            Btn(b, kEditCancel, cx - 100, h / 4 + 120 + 12, 200, "Cancel");
        }

        void PlayGlyph(Sb& b) {
            for (int i = 0; i < 12; i++) {
                const int w = i < 6 ? i + 1 : 12 - i;
                Fill(b, 11, 10 + i, w * 2, 1, "#ffffff", 3);
            }
        }

        void Replays(Sb& b, const TitleModel& m, int w, int h) {
            Backdrop(b, m, w, h);
            const int cx = w / 2;
            const int listTop = TitleListTop(), listBottom = TitleListBottom(h);
            ListBand(b, m, w, listTop, listBottom);
            Plain(b, 0, 12, w, "Select Replay", "ctr");
            const int rows = TitleVisibleRows(h);
            const int rowX = cx - kRowW / 2;
            int y = listTop + 4;
            const int count = static_cast<int>(m.replays.size());
            for (int i = m.replayScroll; i < count && i < m.replayScroll + rows; i++, y += kRowH) {
                const ReplayEntry& r = m.replays[static_cast<size_t>(i)];
                if (i == m.replaySelected)
                    SelectionFrame(b, rowX, y);
                kit::Begin(b, static_cast<float>(rowX), static_cast<float>(y), kRowW, kRowH - 4, "row",
                           std::string(kReplayRowPrefix) + N(i), N(6 + rows - (i - m.replayScroll)));
                Fill(b, 0, 0, 32, 32, r.broken ? "#5a2a2a" : "#2a3d4f", 0);
                if (!r.broken)
                    PlayGlyph(b);
                Text(b, 35, 1, kRowW - 40, r.name.empty() ? r.file : r.name);
                Text(b, 35, 12, kRowW - 40, r.line1, r.broken ? "red" : "gray");
                Text(b, 35, 23, kRowW - 40, r.line2, "dark");
                b.End();
            }
            if (m.replays.empty()) {
                Plain(b, 0, listTop + 28, w, "No replays yet", "ctr");
                Plain(b, 0, listTop + 42, w,
                      "Use Start Replay in the pause menu while playing, or drop a .perlinreplay file on this window",
                      "ctr gray");
            }
            if (!m.replayMessage.empty())
                Plain(b, 0, h - 62, w, m.replayMessage, "ctr yellow");
            const bool has = m.replaySelected >= 0 && m.replaySelected < count;
            const bool playable = has && !m.replays[static_cast<size_t>(m.replaySelected)].broken;
            static const char* const kSort[] = {"Created", "Name", "Server", "Duration", "Size"};
            const int r1 = h - 52, r2 = h - 28;
            Btn(b, playable ? kReplayPlay : "", cx - 154, r1, 100, "Open Replay", !playable);
            Btn(b, kReplayFolder, cx - 50, r1, 100, "Open Folder");
            Btn(b, kReplaySort, cx + 54, r1, 100, std::string("Sort: ") + kSort[std::clamp(m.replaySort, 0, 4)]);
            Btn(b, has ? kReplayRename : "", cx - 154, r2, 72, "Rename", !has);
            Btn(b, has ? kReplayDelete : "", cx - 74, r2, 72, "Delete", !has);
            Btn(b, kReplayDirection, cx + 4, r2, 72, m.replaySortDesc ? "Newest" : "Oldest");
            Btn(b, kReplayDone, cx + 82, r2, 72, "Done");
        }

        void ReplayRename(Sb& b, const TitleModel& m, int w, int h) {
            Backdrop(b, m, w, h);
            const int cx = w / 2;
            Plain(b, 0, 17, w, "Edit Replay", "ctr");
            Text(b, cx - 100, 53, 200, "Replay Name", "dim");
            Field(b, kReplayNameField, cx - 100, 66, 200, m.replayName, true, m.caret);
            Btn(b, kReplayRenameOk, cx - 100, h / 4 + 96 + 18, 200, "Save Changes");
            Btn(b, kReplayDialogCancel, cx - 100, h / 4 + 120 + 18, 200, "Cancel");
        }

        void ReplayDelete(Sb& b, const TitleModel& m, int w, int h) {
            Backdrop(b, m, w, h);
            const int cx = w / 2;
            const std::string name =
                m.replaySelected >= 0 && m.replaySelected < static_cast<int>(m.replays.size())
                    ? m.replays[static_cast<size_t>(m.replaySelected)].name
                    : std::string();
            Plain(b, 0, 40, w, "Are you sure you want to delete this replay?", "ctr");
            Plain(b, 0, 56, w, name, "ctr yellow");
            Plain(b, 0, 70, w, "You won't be able to recover it", "ctr gray");
            Btn(b, kReplayDeleteOk, cx - 100, h / 4 + 96 + 18, 200, "Delete");
            Btn(b, kReplayDialogCancel, cx - 100, h / 4 + 120 + 18, 200, "Cancel");
        }
    }

    int TitleRowHeight() {
        return kRowH;
    }
    int TitleListTop() {
        return 32;
    }
    int TitleListBottom(int guiH) {
        return guiH - 64;
    }
    int TitleVisibleRows(int guiH) {
        return std::max(1, (TitleListBottom(guiH) - TitleListTop() - 4) / kRowH);
    }

    SceneDoc BuildTitleScene(const TitleModel& m, int guiW, int guiH) {
        SceneDoc doc;
        doc.scene.theme = Theme::Title;
        doc.scene.panoSpan = static_cast<float>(std::max(guiH * 5 / 4, 64) * 4);
        SceneBuilder b(doc.scene.nodes);
        switch (m.page) {
        case TitlePage::Main:
            Main(b, m, guiW, guiH);
            break;
        case TitlePage::Servers:
            Servers(b, m, guiW, guiH);
            break;
        case TitlePage::Edit:
            Edit(b, m, guiW, guiH);
            break;
        case TitlePage::Direct:
            Direct(b, m, guiW, guiH);
            break;
        case TitlePage::AddRealm:
            AddRealm(b, m, guiW, guiH);
            break;
        case TitlePage::Accounts:
            Accounts(b, m, guiW, guiH);
            break;
        case TitlePage::AccountName:
            AccountName(b, m, guiW, guiH);
            break;
        case TitlePage::AccountSignIn:
            AccountSignIn(b, m, guiW, guiH);
            break;
        case TitlePage::Replays:
            Replays(b, m, guiW, guiH);
            break;
        case TitlePage::ReplayRename:
            ReplayRename(b, m, guiW, guiH);
            break;
        case TitlePage::ReplayDelete:
            ReplayDelete(b, m, guiW, guiH);
            break;
        }
        return doc;
    }

    int TitleAccountRows(int guiH) {
        return std::max(1, (guiH - 20 - 6 - 102) / 24);
    }
}
