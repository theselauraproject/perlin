#include "client/screen/screens/PauseScreen.hpp"

#include "client/systems/text/FormatCodes.hpp"
#include "client/ui/SceneKit.hpp"
#include "client/ui/TextEdit.hpp"

#include <algorithm>
#include <cmath>

namespace perlin::ui {
    namespace {
        SceneDoc MenuDoc(const std::string& dimClass, int guiW, int guiH) {
            SceneDoc doc;
            doc.scene.theme = Theme::Menu;
            const int alpha = dimClass == "dim70" ? 70 : dimClass == "dim140" ? 140 : dimClass == "dim150" ? 150 : 0;
            if (alpha > 0) {
                SceneBuilder b(doc.scene.nodes);
                kit::Dim(b, static_cast<float>(guiW), static_cast<float>(guiH), alpha);
            }
            return doc;
        }

        SceneNode& Btn(SceneBuilder& b, const std::string& id, float x, float y, float w, const std::string& label,
                       bool off) {
            return kit::Button(b, id, x, y, w, kit::Plain(label), off);
        }

        SceneNode& Text(SceneBuilder& b, float x, float y, float w, const std::string& text, const char* cls,
                        const char* color = nullptr, bool center = false) {
            std::string classes = std::string("t clip") + (*cls ? std::string(" ") + cls : std::string()) +
                                  (center ? " c" : "");
            return kit::Label(b, x, y, w, classes, kit::Formatted(text, color ? color : ""));
        }

        SceneNode& FormattedText(SceneBuilder& b, float x, float y, float w, const std::string& text) {
            return kit::Label(b, x, y, w, "t clip", kit::Formatted(text));
        }

        void Img(SceneBuilder& b, const std::string& src, float x, float y, float s) {
            if (src.empty())
                kit::Box(b, x, y, s, s, "noicon");
            else
                kit::Image(b, x, y, s, s, src);
        }

        void Bar(SceneBuilder& b, float x, float top, float h, float content, float scroll, float maxScroll) {
            if (maxScroll <= 0.0f)
                return;
            const float th = std::max(16.0f, h * h / content);
            const float ty = top + (h - th) * (std::clamp(scroll, 0.0f, maxScroll) / maxScroll);
            kit::Box(b, x, top, 6, h, "track");
            kit::Box(b, x, ty, 6, th, "thumb");
        }

        const char* BadgeColor(int c) {
            return c == 1 ? "#ff5555" : c == 2 ? "#55ff55" : c == 3 ? "#aaaaaa" : "#5555ff";
        }

        float BadgeWidth(const std::string& text) {
            return static_cast<float>(text.size()) * 6.0f + 6.0f;
        }

        void Badge(SceneBuilder& b, float x, float y, const std::string& text, int color) {
            const float w = BadgeWidth(text);
            kit::Box(b, x, y - 1, w, 11, "badge", {}, "4");
            Text(b, x + 3, y, w, text, "", BadgeColor(color));
        }

        void Rule(SceneBuilder& b, float x, float y, float w, float h, const char* cls) {
            kit::Box(b, x, y, w, h, cls);
        }

        void SelectionFrame(SceneBuilder& b, float x, float y, float w, float h, const char* cls) {
            kit::Box(b, x, y, w, h, cls);
            kit::Box(b, x, y, w, 1, "frame");
            kit::Box(b, x, y + h - 1.0f, w, 1, "frame");
            kit::Box(b, x, y, 1, h, "frame");
            kit::Box(b, x + w - 1.0f, y, 1, h, "frame");
        }

        float EstimateWrapHeight(const std::string& text, float width) {
            const int perLine = std::max(1, static_cast<int>(width / 6.0f));
            int lines = 0;
            size_t start = 0;
            while (start <= text.size()) {
                size_t end = text.find('\n', start);
                if (end == std::string::npos)
                    end = text.size();
                const int len = static_cast<int>(end - start);
                lines += std::max(1, (len + perLine - 1) / perLine);
                if (end == text.size())
                    break;
                start = end + 1;
            }
            return static_cast<float>(lines) * 9.0f;
        }
    }

    PackScreenLayout ComputePackScreenLayout(int available, int selected, int guiW, int guiH) {
        PackScreenLayout l;
        const float cx = static_cast<float>(guiW) / 2.0f;
        l.panelW = std::min(200.0f, cx - 10.0f);
        l.leftX = cx - 5.0f - l.panelW;
        l.rightX = cx + 5.0f;
        l.top = 38.0f;
        l.bottom = static_cast<float>(guiH) - 32.0f;
        l.btnW = l.panelW;
        l.openX = l.leftX;
        l.doneX = l.rightX;
        l.btnY = static_cast<float>(guiH) - 26.0f;
        const float h = l.bottom - l.top;
        l.maxScrollA = std::max(0.0f, static_cast<float>(available) * l.rowH + 4.0f - h);
        l.maxScrollB = std::max(0.0f, static_cast<float>(selected) * l.rowH + 4.0f - h);
        return l;
    }

    SceneDoc BuildPackScreen(const std::vector<PackEntry>& available, const std::vector<PackEntry>& selected,
                             const std::string& title, const std::string& subtitle,
                             const std::string& availableLabel, const std::string& selectedLabel,
                             const std::string& open, const std::string& done, float scrollA, float scrollB,
                             int guiW, int guiH) {
        const PackScreenLayout l = ComputePackScreenLayout(static_cast<int>(available.size()),
                                                           static_cast<int>(selected.size()), guiW, guiH);
        const float h = l.bottom - l.top;
        SceneDoc doc = MenuDoc("dim150", guiW, guiH);
        SceneBuilder b(doc.scene.nodes);
        Text(b, 0, 5, static_cast<float>(guiW), title, "", nullptr, true);
        Text(b, 0, 15, static_cast<float>(guiW), subtitle, "g", nullptr, true);
        const auto chevron = [&](char dir, float x, float y, const char* cls) {
            for (int r = 0; r < 5; r++) {
                const int a = r, b2 = 8 - r;
                for (const int k : {a, b2}) {
                    if (r == 4 && k == b2)
                        continue;
                    float px = 0, py = 0;
                    if (dir == '>')
                        px = static_cast<float>(r), py = static_cast<float>(k);
                    else if (dir == '<')
                        px = static_cast<float>(4 - r), py = static_cast<float>(k);
                    else if (dir == '^')
                        px = static_cast<float>(k), py = static_cast<float>(4 - r);
                    else
                        px = static_cast<float>(k), py = static_cast<float>(r);
                    kit::Box(b, x + px * 2.0f, y + py * 2.0f, 2, 2, cls);
                }
            }
        };
        const auto column = [&](const std::vector<PackEntry>& items, float x, float scroll, float maxScroll,
                                const std::string& label, char prefix) {
            Text(b, x, 26, l.panelW, label, "", nullptr, true);
            kit::Box(b, x + l.panelW / 2.0f - static_cast<float>(label.size()) * 2.5f, 36,
                     static_cast<float>(label.size()) * 5.0f, 1, "sep");
            kit::Begin(b, x, l.top, l.panelW, h, "panel");
            for (int i = 0; i < static_cast<int>(items.size()); i++) {
                const float y = l.RowY(i, scroll) - l.top;
                if (y + l.rowH < 0.0f || y > h)
                    continue;
                const PackEntry& e = items[static_cast<size_t>(i)];
                const float textW = l.panelW - 44.0f - (maxScroll > 0 ? 8.0f : 0.0f);
                kit::Box(b, 2, y, l.panelW - 4.0f - (maxScroll > 0 ? 6.0f : 0.0f), 34, "prow",
                         std::string(1, prefix) + "p" + std::to_string(i));
                Img(b, e.icon, 4, y + 1, 32);
                FormattedText(b, 40, y + 2, textW, e.name);
                SceneNode& wrap = kit::Label(b, 40, y + 13, textW, "wrap", kit::Formatted(e.desc), "2");
                wrap.h = 18;
                const std::string id = std::string(1, prefix) + std::to_string(i);
                const auto zone = [&](const std::string& zid, float zx, float zy, float zw, float zh, char dir,
                                      float cx, float cy) {
                    kit::Begin(b, zx, zy, zw, zh, "zone", zid);
                    chevron(dir, cx, cy, "ch");
                    b.End();
                };
                if (e.canSelect) {
                    kit::Begin(b, 4, y + 1, 32, 32, "ico", id + "i", "8");
                    kit::Box(b, 0, 0, 32, 32, "ov");
                    chevron('>', 11, 7, "ch");
                    b.End();
                } else if (e.canUnselect || e.canUp || e.canDown) {
                    kit::Begin(b, 4, y + 1, 32, 32, "ico", {}, "8");
                    kit::Box(b, 0, 0, 32, 32, "ov");
                    if (e.canUnselect)
                        zone(id + "x", 0, 0, 16, 32, '<', 3, 7);
                    if (e.canUp)
                        zone(id + "u", 16, 0, 16, 16, '^', -1, 3);
                    if (e.canDown)
                        zone(id + "d", 16, 16, 16, 16, 'v', -1, 3);
                    b.End();
                }
            }
            Bar(b, l.panelW - 6.0f, 0, h, static_cast<float>(items.size()) * l.rowH + 4.0f, scroll, maxScroll);
            b.End();
        };
        column(available, l.leftX, scrollA, l.maxScrollA, availableLabel, 'a');
        column(selected, l.rightX, scrollB, l.maxScrollB, selectedLabel, 's');
        Btn(b, "packopen", l.openX, l.btnY, l.btnW, open, false);
        Btn(b, "packdone", l.doneX, l.btnY, l.btnW, done, false);
        return doc;
    }

    ShaderScreenLayout ComputeShaderScreenLayout(int rows, int guiW, int guiH) {
        ShaderScreenLayout l;
        const float cx = static_cast<float>(guiW) / 2.0f;
        l.colW = std::min(300.0f, static_cast<float>(guiW) - 20.0f);
        l.colX = cx - l.colW / 2.0f;
        l.listTop = 32.0f;
        l.listBottom = static_cast<float>(guiH) - 52.0f;
        l.row1Y = l.listBottom + 6.0f;
        l.row2Y = l.row1Y + 24.0f;
        l.btnW1 = (l.colW - 4.0f) / 2.0f;
        l.openX = l.colX;
        l.settingsX = l.colX + l.btnW1 + 4.0f;
        l.thirdW = (l.colW - 8.0f) / 3.0f;
        l.cancelX = l.colX;
        l.applyX = l.colX + l.thirdW + 4.0f;
        l.doneX = l.colX + 2.0f * (l.thirdW + 4.0f);
        l.contentH = static_cast<float>(rows) * l.rowH + 12.0f;
        l.maxScroll = std::max(0.0f, l.contentH - (l.listBottom - l.listTop));
        return l;
    }

    SceneDoc BuildShaderScreen(const std::vector<std::string>& rows, int selectedRow, int toggleRow,
                               const std::string& title, const std::string& subtitle,
                               const std::string& dragText, const std::string& open,
                               const std::string& settings, const std::string& cancel,
                               const std::string& apply, const std::string& done, float scroll, int guiW,
                               int guiH, const std::string& progressText, float progress) {
        const ShaderScreenLayout l = ComputeShaderScreenLayout(static_cast<int>(rows.size()) + 1, guiW, guiH);
        const float h = l.listBottom - l.listTop;
        const float gw = static_cast<float>(guiW);
        SceneDoc doc = MenuDoc("dim140", guiW, guiH);
        SceneBuilder b(doc.scene.nodes);
        Text(b, 0, 8, gw, title, "", nullptr, true);
        Text(b, 0, 19, gw, subtitle, "g", nullptr, true);
        kit::Box(b, 0, l.listTop - 1.0f, gw, 1, "listline");
        kit::Box(b, 0, l.listBottom, gw, 1, "listline");
        kit::Box(b, 0, l.listTop, gw, h, "listback");
        kit::Begin(b, 0, l.listTop, gw, h, "listclip");
        for (int i = 0; i < static_cast<int>(rows.size()); i++) {
            const float y = l.RowY(i, scroll) - l.listTop;
            if (y < 0.0f || y + 18.0f > h)
                continue;
            const bool selected = i == selectedRow;
            const bool toggle = i == toggleRow;
            if (selected || toggle) {
                kit::Box(b, l.colX - 1, y - 1, l.colW + 2, 20, toggle ? "rowtoggle" : "rowselect");
                kit::Box(b, l.colX - 1, y - 1, l.colW + 2, 1, "frame");
                kit::Box(b, l.colX - 1, y + 18, l.colW + 2, 1, "frame");
                kit::Box(b, l.colX - 1, y - 1, 1, 20, "frame");
                kit::Box(b, l.colX + l.colW, y - 1, 1, 20, "frame");
            }
            kit::Box(b, l.colX, y, l.colW, 18, "srow", "sh" + std::to_string(i));
            Text(b, l.colX, y + 5, l.colW, rows[static_cast<size_t>(i)], "", selected && !toggle ? "#ffff55" : nullptr,
                 true);
        }
        Text(b, l.colX, l.RowY(static_cast<int>(rows.size()), scroll) - l.listTop + 5, l.colW, dragText, "g",
             nullptr, true);
        b.End();
        if (l.maxScroll > 0.0f)
            Bar(b, gw / 2.0f + l.colW / 2.0f + 6.0f, l.listTop, h, l.contentH, scroll, l.maxScroll);
        Btn(b, "shopen", l.openX, l.row1Y, l.btnW1, open, false);
        Btn(b, "shsettings", l.settingsX, l.row1Y, l.btnW1, settings, false);
        Btn(b, "shcancel", l.cancelX, l.row2Y, l.thirdW, cancel, false);
        Btn(b, "shapply", l.applyX, l.row2Y, l.thirdW, apply, false);
        Btn(b, "shdone", l.doneX, l.row2Y, l.thirdW, done, false);
        if (!progressText.empty() || progress >= 0.0f) {
            const float pw = std::min(gw - 20.0f, 300.0f);
            const float px = (gw - pw) / 2.0f;
            const float py = l.listTop + (l.listBottom - l.listTop) / 2.0f - 22.0f;
            kit::Box(b, px, py, pw, 44, "progresspanel");
            Text(b, px + 6, py + 7, pw - 12, progressText, "", nullptr, true);
            if (progress >= 0.0f) {
                const float bw = pw - 24.0f;
                kit::Box(b, px + 12, py + 26, bw, 6, "progresstrack");
                kit::Box(b, px + 12, py + 26, bw * std::clamp(progress, 0.0f, 1.0f), 6, "progressfill");
            }
        }
        return doc;
    }

    ModScreenLayout ComputeModScreenLayout(int rows, const ModDetail& detail, int guiW, int guiH) {
        ModScreenLayout l;
        const float w = static_cast<float>(guiW);
        l.leftW = std::floor((w - 24.0f) / 2.0f);
        l.rightX = l.leftX + l.leftW + 8.0f;
        l.rightW = w - l.rightX - 8.0f;
        l.listBottom = static_cast<float>(guiH) - 32.0f;
        l.btnY = static_cast<float>(guiH) - 26.0f;
        l.btnW = std::min(200.0f, w / 2.0f - 8.0f);
        l.openX = w / 2.0f - 4.0f - l.btnW;
        l.doneX = w / 2.0f + 4.0f;
        l.detailBtnY = 58.0f;
        if (detail.valid && detail.hasSettings) {
            l.toggleW = std::floor((l.rightW - 8.0f) / 3.0f);
            l.toggleX = l.rightX;
            l.reloadX = l.rightX + l.toggleW + 4.0f;
            l.reloadW = l.toggleW;
            l.settingsX = l.reloadX + l.reloadW + 4.0f;
            l.settingsW = l.rightX + l.rightW - l.settingsX;
        } else {
            l.toggleW = std::floor((l.rightW - 4.0f) / 2.0f);
            l.toggleX = l.rightX;
            l.reloadX = l.rightX + l.toggleW + 4.0f;
            l.reloadW = l.rightW - l.toggleW - 4.0f;
        }
        l.detailTextTop = 82.0f;
        l.detailTextBottom = l.listBottom;
        l.maxScroll = std::max(0.0f, static_cast<float>(rows) * l.rowH + 4.0f - (l.listBottom - l.listTop));
        if (detail.valid)
            l.maxDetailScroll = std::max(0.0f, EstimateWrapHeight(detail.body, l.rightW - 20.0f) + 8.0f -
                                                   (l.detailTextBottom - l.detailTextTop));
        return l;
    }

    SceneDoc BuildModScreen(const std::vector<ModEntry>& rows, int selected, const ModDetail& detail,
                            const std::string& title, const std::string& search, bool caret,
                            const std::string& showing, const std::string& hint, const std::string& open,
                            const std::string& done, float scroll, float detailScroll, int guiW, int guiH) {
        const ModScreenLayout l = ComputeModScreenLayout(static_cast<int>(rows.size()), detail, guiW, guiH);
        const float listH = l.listBottom - l.listTop;
        SceneDoc doc = MenuDoc("dim140", guiW, guiH);
        SceneBuilder b(doc.scene.nodes);
        Text(b, l.leftX, 6, l.leftW, title, "", nullptr, true);
        kit::Box(b, l.leftX, l.searchY, l.leftW, l.searchH, "searchedge", {}, "3");
        kit::Box(b, l.leftX + 1, l.searchY + 1, l.leftW - 2, l.searchH - 2, "searchfill", {}, "3");
        kit::Label(b, l.leftX + 5, l.searchY + 5, l.leftW - 10, "t clip", search.empty() ? kit::Plain(hint, "#808080")
                                                                                        : TextFieldRuns(search, caret),
                   "4");
        Text(b, l.leftX, l.searchY + l.searchH + 6, l.leftW, showing, "");
        kit::Begin(b, l.leftX, l.listTop, l.leftW, listH, "panel");
        for (int i = 0; i < static_cast<int>(rows.size()); i++) {
            const float y = l.RowY(i, scroll) - l.listTop;
            if (y + l.rowH < 0.0f || y > listH)
                continue;
            const ModEntry& e = rows[static_cast<size_t>(i)];
            const float rowW = l.leftW - 2.0f - (l.maxScroll > 0 ? 8.0f : 0.0f);
            if (i == selected) {
                kit::Box(b, 1, y, rowW, 34, "modframe");
                kit::Box(b, 2, y + 1, rowW - 2, 32, "sel");
            }
            kit::Box(b, 1, y, rowW, 34, "prow", "md" + std::to_string(i));
            Img(b, e.icon, 4, y + 1, 32);
            FormattedText(b, 40, y + 2, rowW - 40.0f - (e.badge.empty() ? 0.0f : BadgeWidth(e.badge) + 6.0f), e.name);
            if (!e.badge.empty())
                Badge(b, 40.0f + std::min(static_cast<float>(e.name.size()) * 6.0f + 6.0f, rowW - 40.0f - BadgeWidth(e.badge)),
                      y + 2, e.badge, e.badgeColor);
            SceneNode& wrap = kit::Label(b, 40, y + 13, rowW - 44.0f, "wrap", kit::Formatted(e.desc), "2");
            wrap.h = 18;
        }
        Bar(b, l.leftW - 6.0f, 0, listH, static_cast<float>(rows.size()) * l.rowH + 4.0f, scroll, l.maxScroll);
        b.End();
        if (detail.valid) {
            Img(b, detail.icon, l.rightX, 18, 32);
            Text(b, l.rightX + 36, 20, l.rightW - 36, detail.name, "");
            if (!detail.badge.empty())
                Badge(b, l.rightX + 36 + std::min(static_cast<float>(detail.name.size()) * 6.0f + 6.0f,
                                                   l.rightW - 36.0f - BadgeWidth(detail.badge)),
                      20, detail.badge, detail.badgeColor);
            Text(b, l.rightX + 36, 31, l.rightW - 36, detail.version, "g");
            Text(b, l.rightX + 36, 42, l.rightW - 36, detail.author, "g");
            Btn(b, "mdtoggle", l.toggleX, l.detailBtnY - 1, l.toggleW, detail.toggleLabel, false);
            Btn(b, "mdreload", l.reloadX, l.detailBtnY - 1, l.reloadW, detail.reloadLabel, !detail.toggleEnabled);
            if (detail.hasSettings)
                Btn(b, "mdsettings", l.settingsX, l.detailBtnY - 1, l.settingsW, detail.settingsLabel, false);
            const float th = l.detailTextBottom - l.detailTextTop;
            kit::Begin(b, l.rightX, l.detailTextTop, l.rightW, th, "panel");
            SceneNode& body = kit::Label(b, 6, 4.0f - detailScroll, l.rightW - 20.0f, "wrap open",
                                         ParseFormatLines(detail.body, ""));
            body.h = std::round(EstimateWrapHeight(detail.body, l.rightW - 20.0f) + 9.0f);
            Bar(b, l.rightW - 6.0f, 0, th, EstimateWrapHeight(detail.body, l.rightW - 20.0f) + 8.0f, detailScroll,
                l.maxDetailScroll);
            b.End();
        }
        Btn(b, "mdopen", l.openX, l.btnY, l.btnW, open, false);
        Btn(b, "mddone", l.doneX, l.btnY, l.btnW, done, false);
        return doc;
    }

    WardrobeLayout ComputeWardrobeLayout(int skinCards, int capeCards, bool skinOpen, bool capeOpen,
                                         int firstLine, int guiW, int guiH) {
        WardrobeLayout l;
        l.doneW = 200.0f;
        l.doneX = (static_cast<float>(guiW) - l.doneW) / 2.0f;
        l.doneY = static_cast<float>(guiH) - 26.0f;
        l.hintY = l.doneY - 11.0f;
        l.slimW = 110.0f;
        l.slimY = l.hintY - 24.0f;
        l.nameY = 20.0f;
        l.prevY = 31.0f;
        l.prevH = std::max(60.0f, l.slimY - 13.0f - l.prevY);
        l.prevW = std::floor(l.prevH * 0.69f);
        l.rightW = std::max(l.slimW, l.prevW + 10.0f);
        l.rightX = static_cast<float>(guiW) - l.rightW - 10.0f;
        l.prevX = l.rightX + (l.rightW - l.prevW) / 2.0f;
        l.slimX = l.rightX + (l.rightW - l.slimW) / 2.0f;
        l.listX = 12.0f;
        l.listW = l.rightX - 14.0f - l.listX;
        l.listTop = 20.0f;
        l.listBottom = l.hintY - 3.0f;
        l.cols = std::max(1, static_cast<int>((l.listW + l.gap) / (l.cardW + l.gap)));
        const float rowH = l.cardH + 2.0f;
        std::vector<WardrobeLine> all;
        const auto addSection = [&](int headKind, int count, bool open) {
            all.push_back({headKind, 0, 0});
            const int rows = std::max(1, (count + l.cols - 1) / l.cols);
            const int shown = open ? rows : 0;
            for (int r = 0; r < shown; r++)
                all.push_back({headKind + 1, r, 0});
        };
        addSection(0, skinCards, skinOpen);
        addSection(2, capeCards, capeOpen);
        const auto height = [&](const WardrobeLine& line) {
            return line.kind % 2 == 0 ? l.headH + 2.0f : rowH;
        };
        const float avail = l.listBottom - l.listTop;
        l.totalLines = static_cast<int>(all.size());
        l.maxFirst = 0;
        float tail = 0.0f;
        for (int i = l.totalLines - 1; i >= 0; i--) {
            tail += height(all[static_cast<size_t>(i)]);
            if (tail > avail) {
                l.maxFirst = i + 1;
                break;
            }
        }
        l.maxFirst = std::min(l.maxFirst, l.totalLines - 1);
        l.firstLine = std::clamp(firstLine, 0, l.maxFirst);
        float y = l.listTop;
        for (int i = l.firstLine; i < l.totalLines; i++) {
            WardrobeLine line = all[static_cast<size_t>(i)];
            const float h = height(line);
            if (y + h > l.listBottom + 0.5f)
                break;
            line.y = y;
            l.lines.push_back(line);
            y += h;
        }
        return l;
    }

    SceneDoc BuildWardrobeScene(const std::vector<WardrobeCard>& skins, const std::vector<WardrobeCard>& capes,
                                bool skinOpen, bool capeOpen, int firstLine, const std::string& previewImg,
                                const std::string& previewName, bool slim, bool canTrashSkin, bool canTrashCape,
                                const WardrobeText& t, int guiW, int guiH) {
        const WardrobeLayout l =
            ComputeWardrobeLayout(static_cast<int>(skins.size()), static_cast<int>(capes.size()), skinOpen,
                                  capeOpen, firstLine, guiW, guiH);
        SceneDoc doc = MenuDoc("dim70", guiW, guiH);
        SceneBuilder b(doc.scene.nodes);
        const float gw = static_cast<float>(guiW);
        Text(b, 0, 6, gw, t.title, "", nullptr, true);
        for (const WardrobeLine& line : l.lines) {
            const bool isSkin = line.kind < 2;
            const std::vector<WardrobeCard>& cards = isSkin ? skins : capes;
            if (line.kind % 2 == 0) {
                const bool open = isSkin ? skinOpen : capeOpen;
                const std::string count = std::to_string(cards.size() - 1);
                kit::Box(b, l.listX, line.y, l.ToggleW(), l.headH, "prow", isSkin ? "wdhs" : "wdhc");
                kit::Image(b, l.listX + 4.0f, line.y + 6.0f, 8, 8,
                           open ? "textures/ui/chevron_white_down.png" : "textures/ui/chevron_white_up.png", "8");
                Text(b, l.listX + 18.0f, line.y + 6.0f, l.ToggleW() - 20.0f,
                     (isSkin ? t.skins : t.capes) + " [" + count + "]", "", isSkin ? "#ff99ff" : "#ffcc55");
                const bool canTrash = isSkin ? canTrashSkin : canTrashCape;
                Btn(b, isSkin ? "wdtrashs" : "wdtrashc", l.TrashX(), line.y, l.trashW, "", !canTrash);
                kit::Image(b, l.TrashX() + (l.trashW - 10.0f) / 2.0f, line.y + 4.0f, 10, 11,
                           "textures/ui/icon_trash.png", "8");
                Btn(b, isSkin ? "wdaddskin" : "wdaddcape", l.AddX(), line.y, l.addW, isSkin ? t.addSkin : t.addCape,
                    false);
                kit::Image(b, l.AddX() + 6.0f, line.y + 5.0f, 9, 9, "textures/ui/plus.png", "8");
            } else {
                for (int c = 0; c < l.cols; c++) {
                    const int i = line.row * l.cols + c;
                    if (i >= static_cast<int>(cards.size()))
                        break;
                    const WardrobeCard& card = cards[static_cast<size_t>(i)];
                    const float x = l.CardX(i), y = line.y;
                    kit::Box(b, x, y, l.cardW, l.cardH, card.selected ? "prow cardsel" : "prow",
                             std::string(isSkin ? "wds" : "wdp") + std::to_string(i));
                    if (card.selected) {
                        kit::Box(b, x, y, l.cardW, 1, "cardline");
                        kit::Box(b, x, y + l.cardH - 1, l.cardW, 1, "cardline");
                        kit::Box(b, x, y, 1, l.cardH, "cardline");
                        kit::Box(b, x + l.cardW - 1, y, 1, l.cardH, "cardline");
                    }
                    if (!card.img.empty())
                        kit::Image(b, x + (l.cardW - 36.0f) / 2.0f, y + 2.0f, 36, 52, card.img);
                    Text(b, x, y + l.cardH - 11.0f, l.cardW, card.name, "", card.selected ? "#ffff55" : nullptr, true);
                }
            }
        }
        if (l.maxFirst > 0)
            Bar(b, l.listX + l.listW + 3.0f, l.listTop, l.listBottom - l.listTop,
                (l.listBottom - l.listTop) * static_cast<float>(l.totalLines) /
                    static_cast<float>(l.totalLines - l.maxFirst),
                static_cast<float>(l.firstLine), static_cast<float>(l.maxFirst));
        Text(b, l.rightX, l.nameY, l.rightW, previewName, "", nullptr, true);
        if (!previewImg.empty())
            kit::Image(b, l.prevX, l.prevY, l.prevW, l.prevH, previewImg);
        Text(b, l.rightX, l.prevY + l.prevH + 1.0f, l.rightW, t.dragHint, "g", nullptr, true);
        Btn(b, "wdslim", l.slimX, l.slimY, l.slimW, t.slim + ": " + (slim ? t.slimOn : t.slimOff), false);
        const float hintW = static_cast<float>(t.dropHint.size()) * 6.0f + 10.0f;
        kit::Box(b, (gw - hintW) / 2.0f, l.hintY - 2.0f, hintW, 12, "hintback");
        Text(b, 0, l.hintY, gw, t.dropHint, "g", nullptr, true);
        Btn(b, "wddone", l.doneX, l.doneY, l.doneW, t.done, false);
        return doc;
    }

    SceneDoc BuildLanguageScreen(const std::vector<std::string>& rows, int selectedRow, const std::string& title,
                                 const std::string& subtitle, const std::string& done, float scroll, int guiW,
                                 int guiH) {
        const ShaderScreenLayout l = ComputeShaderScreenLayout(static_cast<int>(rows.size()), guiW, guiH);
        const float h = l.listBottom - l.listTop;
        const float gw = static_cast<float>(guiW);
        SceneDoc doc = MenuDoc("dim140", guiW, guiH);
        SceneBuilder b(doc.scene.nodes);
        Text(b, 0, 8, gw, title, "", nullptr, true);
        Text(b, 0, 19, gw, subtitle, "g", nullptr, true);
        kit::Box(b, 0, l.listTop - 1.0f, gw, 1, "listline");
        kit::Box(b, 0, l.listBottom, gw, 1, "listline");
        kit::Box(b, 0, l.listTop, gw, h, "listback");
        kit::Begin(b, 0, l.listTop, gw, h, "listclip");
        for (int i = 0; i < static_cast<int>(rows.size()); i++) {
            const float y = l.RowY(i, scroll) - l.listTop;
            if (y < 0.0f || y + 18.0f > h)
                continue;
            const bool selected = i == selectedRow;
            if (selected) {
                kit::Box(b, l.colX - 1, y - 1, l.colW + 2, 20, "rowselect");
                kit::Box(b, l.colX - 1, y - 1, l.colW + 2, 1, "frame");
                kit::Box(b, l.colX - 1, y + 18, l.colW + 2, 1, "frame");
                kit::Box(b, l.colX - 1, y - 1, 1, 20, "frame");
                kit::Box(b, l.colX + l.colW, y - 1, 1, 20, "frame");
            }
            kit::Box(b, l.colX, y, l.colW, 18, "srow", "lg" + std::to_string(i));
            Text(b, l.colX, y + 5, l.colW, rows[static_cast<size_t>(i)], "", selected ? "#ffff55" : nullptr, true);
        }
        b.End();
        if (l.maxScroll > 0.0f)
            Bar(b, gw / 2.0f + l.colW / 2.0f + 6.0f, l.listTop, h, l.contentH, scroll, l.maxScroll);
        Btn(b, "lgdone", l.colX + (l.colW - 200.0f) / 2.0f, l.row1Y + 12.0f, 200, done, false);
        return doc;
    }
}
