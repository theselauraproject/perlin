#include "client/ui/FormatCodes.hpp"
#include "client/ui/PauseScreen.hpp"

#include "client/ui/RmlEmit.hpp"
#include "client/ui/TextEdit.hpp"
#include "client/ui/UiTemplate.hpp"

#include <algorithm>
#include <cmath>

namespace perlin::ui {
    namespace {
        const char* kMenuHead = R"PERLINRML(<rml>
<head>
<title>screen</title>
<style>
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
div, mcimg { display: block; position: absolute; }
#dim { left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; background-color: rgba(0,0,0,{{dim}}); }
.t { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; height: 9px; color: #ffffff; white-space: nowrap; }
.g { color: #a0a0a0; }
.c { text-align: center; }
.btn { height: 20px; background-color: #000000; z-index: 5; }
.btn div { position: absolute; }
.btn .rim { left: 1px; top: 1px; height: 16px; background-color: #aaaaaa; }
.btn .face { left: 2px; top: 2px; height: 15px; background-color: #6f6f6f; }
.btn .shade { left: 1px; top: 17px; height: 2px; background-color: #565656; }
.btn .edge { top: 1px; width: 1px; height: 16px; background-color: #565656; }
.btn .lbl { font-family: minecraft-shadow; left: 0px; top: 6px; height: 9px; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; }
.btn:hover { background-color: #ffffff; }
.btn:hover .rim { background-color: #afafaf; }
.btn:hover .face { background-color: #757575; }
.btn:hover .shade, .btn:hover .edge { background-color: #5c5c5c; }
.btn.off, .btn.off:hover { background-color: #000000; }
.btn.off .rim, .btn.off .face, .btn.off .shade, .btn.off .edge { background-color: #2c2c2c; }
.btn.off .lbl { color: #a0a0a0; }
.mini { height: 14px; background-color: #000000; z-index: 6; }
.mini div { position: absolute; }
.mini .face { left: 1px; top: 1px; height: 11px; background-color: #6f6f6f; }
.mini .lbl { font-family: minecraft-shadow; left: 0px; top: 3px; height: 9px; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; }
.mini:hover { background-color: #ffffff; }
.mini:hover .face { background-color: #8a8a8a; }
.mini.off, .mini.off:hover { background-color: #000000; }
.mini.off .face { background-color: #2c2c2c; }
.mini.off .lbl { color: #606060; }
.panel { overflow: hidden; background-color: rgba(0,0,0,170); }
.prow { height: 34px; background-color: rgba(0,0,0,0); }
.prow:hover { background-color: rgba(255,255,255,40); }
.sel { background-color: #000000; }
.sep { height: 1px; background-color: rgba(255,255,255,60); }
.sepb { height: 1px; background-color: rgba(0,0,0,191); }
.track { background-color: #000000; width: 6px; }
.thumb { background-color: #808080; width: 6px; }
.wrap { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; color: #a0a0a0; white-space: normal; overflow: hidden; }
.ico { width: 32px; height: 32px; z-index: 8; }
.ico .ov { display: none; left: 0px; top: 0px; width: 32px; height: 32px; background-color: rgba(144,144,144,160); }
.ico:hover .ov { display: block; }
.ico .ch, .ico .zone { display: none; }
.ico:hover .ch { display: block; background-color: #a0a0a0; }
.ico:hover .zone { display: block; }
.zone:hover .ch { background-color: #ffffff; }
.srow { height: 18px; background-color: rgba(0,0,0,0); }
.srow:hover { background-color: rgba(255,255,255,30); }
</style>
</head>
<body>
<div id="dim"></div>
)PERLINRML";
        const ScreenTemplateDefault kMenuHeadDefault("menu_screen.rml", kMenuHead);
    }

    namespace {
        std::string N(float v) {
            return std::to_string(static_cast<int>(std::lround(v)));
        }

        std::string Styles(int guiW, int guiH, int dim) {
            std::string d = ScreenTemplate("menu_screen.rml", kMenuHead);
            ReplaceAll(d, "w", std::to_string(guiW));
            ReplaceAll(d, "h", std::to_string(guiH));
            ReplaceAll(d, "dim", std::to_string(dim));
            return d;
        }

        std::string Btn(const std::string& id, float x, float y, float w, const std::string& label,
                        bool off) {
            std::string b = "<div id=\"" + id + "\" class=\"btn" + (off ? " off" : "") +
                            "\" style=\"left: " + N(x) + "px; top: " + N(y) + "px; width: " + N(w) + "px;\">";
            b += "<div class=\"rim\" style=\"width: " + N(w - 3) +
                 "px;\"></div><div class=\"face\" style=\"width: " + N(w - 4) + "px;\"></div>";
            b += "<div class=\"shade\" style=\"width: " + N(w - 2) +
                 "px;\"></div><div class=\"edge\" style=\"left: " + N(w - 2) + "px;\"></div>";
            b += "<div class=\"lbl\" style=\"width: " + N(w) + "px;\">" + EscapeRml(label) + "</div></div>\n";
            return b;
        }

        std::string Mini(const std::string& id, float x, float y, float w, const std::string& label,
                         bool off) {
            return "<div id=\"" + id + "\" class=\"mini" + (off ? " off" : "") + "\" style=\"left: " + N(x) +
                   "px; top: " + N(y) + "px; width: " + N(w) +
                   "px;\"><div class=\"face\" style=\"width: " + N(w - 2) +
                   "px;\"></div><div class=\"lbl\" style=\"width: " + N(w) + "px;\">" + EscapeRml(label) +
                   "</div></div>\n";
        }

        std::string Text(float x, float y, float w, const std::string& text, const char* cls,
                         const char* color = nullptr, bool center = false) {
            return "<div class=\"t " + std::string(cls) + (center ? " c" : "") + "\" style=\"left: " + N(x) +
                   "px; top: " + N(y) + "px; width: " + N(w) + "px;" +
                   (color ? std::string(" color: ") + color + ";" : std::string()) + " overflow: hidden;\">" +
                   FormatCodesToRml(text) + "</div>\n";
        }

        std::string FormattedText(float x, float y, float w, const std::string& text) {
            return "<div class=\"t\" style=\"left: " + N(x) + "px; top: " + N(y) + "px; width: " + N(w) +
                   "px; overflow: hidden;\">" + FormatCodesToRml(text) + "</div>\n";
        }

        std::string Img(const std::string& src, float x, float y, float s) {
            if (src.empty())
                return "<div style=\"left: " + N(x) + "px; top: " + N(y) + "px; width: " + N(s) +
                       "px; height: " + N(s) + "px; background-color: #555555;\"></div>\n";
            return "<mcimg src=\"" + EscapeRml(src) + "\" style=\"left: " + N(x) + "px; top: " + N(y) +
                   "px; width: " + N(s) + "px; height: " + N(s) + "px;\"></mcimg>\n";
        }

        std::string Bar(float x, float top, float h, float content, float scroll, float maxScroll) {
            if (maxScroll <= 0.0f)
                return {};
            const float th = std::max(16.0f, h * h / content);
            const float ty = top + (h - th) * (std::clamp(scroll, 0.0f, maxScroll) / maxScroll);
            return "<div class=\"track\" style=\"left: " + N(x) + "px; top: " + N(top) +
                   "px; height: " + N(h) + "px;\"></div>\n<div class=\"thumb\" style=\"left: " + N(x) +
                   "px; top: " + N(ty) + "px; height: " + N(th) + "px;\"></div>\n";
        }

        const char* BadgeColor(int c) {
            return c == 1 ? "#ff5555" : c == 2 ? "#55ff55" : c == 3 ? "#aaaaaa" : "#5555ff";
        }

        std::string Badge(float x, float y, const std::string& text, int color) {
            const float w = static_cast<float>(text.size()) * 6.0f + 6.0f;
            return "<div style=\"left: " + N(x) + "px; top: " + N(y - 1) + "px; width: " + N(w) +
                   "px; height: 11px; background-color: rgba(0,0,0,150); z-index: 4;\"></div>\n" +
                   Text(x + 3, y, w, text, "", BadgeColor(color));
        }

        float BadgeWidth(const std::string& text) {
            return static_cast<float>(text.size()) * 6.0f + 6.0f;
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

    std::string BuildPackScreenRml(const std::vector<PackEntry>& available,
                                   const std::vector<PackEntry>& selected, const std::string& title,
                                   const std::string& subtitle, const std::string& availableLabel,
                                   const std::string& selectedLabel, const std::string& open,
                                   const std::string& done, float scrollA, float scrollB, int guiW,
                                   int guiH) {
        const PackScreenLayout l = ComputePackScreenLayout(static_cast<int>(available.size()),
                                                           static_cast<int>(selected.size()), guiW, guiH);
        const float h = l.bottom - l.top;
        std::string d = Styles(guiW, guiH, 150);
        const float cx = static_cast<float>(guiW) / 2.0f;
        d += Text(0, 5, static_cast<float>(guiW), title, "", nullptr, true);
        d += Text(0, 15, static_cast<float>(guiW), subtitle, "g", nullptr, true);
        const auto chevron = [&](char dir, float x, float y, const char* cls) {
            std::string out;
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
                    out += "<div class=\"" + std::string(cls) + "\" style=\"left: " + N(x + px * 2.0f) +
                           "px; top: " + N(y + py * 2.0f) + "px; width: 2px; height: 2px;\"></div>";
                }
            }
            return out;
        };
        const auto column = [&](const std::vector<PackEntry>& items, float x, float scroll, float maxScroll,
                                const std::string& label, char prefix) {
            d += Text(x, 26, l.panelW, label, "", nullptr, true);
            d += "<div class=\"sep\" style=\"left: " +
                 N(x + l.panelW / 2.0f - static_cast<float>(label.size()) * 2.5f) + "px; top: " + N(36) +
                 "px; width: " + N(static_cast<float>(label.size()) * 5.0f) + "px;\"></div>\n";
            d += "<div class=\"panel\" style=\"left: " + N(x) + "px; top: " + N(l.top) +
                 "px; width: " + N(l.panelW) + "px; height: " + N(h) + "px;\">\n";
            for (int i = 0; i < static_cast<int>(items.size()); i++) {
                const float y = l.RowY(i, scroll) - l.top;
                if (y + l.rowH < 0.0f || y > h)
                    continue;
                const PackEntry& e = items[static_cast<size_t>(i)];
                const float textW = l.panelW - 44.0f - (maxScroll > 0 ? 8.0f : 0.0f);
                d += "<div id=\"" + std::string(1, prefix) + "p" + std::to_string(i) +
                     "\" class=\"prow\" style=\"left: 2px; top: " + N(y) +
                     "px; width: " + N(l.panelW - 4.0f - (maxScroll > 0 ? 6.0f : 0.0f)) + "px;\"></div>\n";
                d += Img(e.icon, 4, y + 1, 32);
                d += FormattedText(40, y + 2, textW, e.name);
                d += "<div class=\"wrap\" style=\"left: 40px; top: " + N(y + 13) + "px; width: " + N(textW) +
                     "px; height: 18px; z-index: 2;\">" + FormatCodesToRml(e.desc) + "</div>\n";
                const std::string id = std::string(1, prefix) + std::to_string(i);
                if (e.canSelect) {
                    d += "<div id=\"" + id + "i\" class=\"ico\" style=\"left: 4px; top: " + N(y + 1) +
                         "px;\"><div class=\"ov\"></div>" + chevron('>', 11, 7, "ch") + "</div>\n";
                } else if (e.canUnselect || e.canUp || e.canDown) {
                    std::string zones;
                    if (e.canUnselect)
                        zones +=
                            "<div id=\"" + id +
                            "x\" class=\"zone\" style=\"left: 0px; top: 0px; width: 16px; height: 32px;\">" +
                            chevron('<', 3, 7, "ch") + "</div>";
                    if (e.canUp)
                        zones +=
                            "<div id=\"" + id +
                            "u\" class=\"zone\" style=\"left: 16px; top: 0px; width: 16px; height: 16px;\">" +
                            chevron('^', -1, 3, "ch") + "</div>";
                    if (e.canDown)
                        zones += "<div id=\"" + id +
                                 "d\" class=\"zone\" style=\"left: 16px; top: 16px; width: 16px; height: "
                                 "16px;\">" +
                                 chevron('v', -1, 3, "ch") + "</div>";
                    d += "<div class=\"ico\" style=\"left: 4px; top: " + N(y + 1) +
                         "px;\"><div class=\"ov\"></div>" + zones + "</div>\n";
                }
            }
            d += Bar(l.panelW - 6.0f, 0, h, static_cast<float>(items.size()) * l.rowH + 4.0f, scroll,
                     maxScroll);
            d += "</div>\n";
        };
        column(available, l.leftX, scrollA, l.maxScrollA, availableLabel, 'a');
        column(selected, l.rightX, scrollB, l.maxScrollB, selectedLabel, 's');
        (void)cx;
        d += Btn("packopen", l.openX, l.btnY, l.btnW, open, false);
        d += Btn("packdone", l.doneX, l.btnY, l.btnW, done, false);
        d += "</body>\n</rml>\n";
        return d;
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

    std::string BuildShaderScreenRml(const std::vector<std::string>& rows, int selectedRow, int toggleRow,
                                     const std::string& title, const std::string& subtitle,
                                     const std::string& dragText, const std::string& open,
                                     const std::string& settings, const std::string& cancel,
                                     const std::string& apply, const std::string& done, float scroll,
                                     int guiW, int guiH, const std::string& progressText, float progress) {
        const ShaderScreenLayout l = ComputeShaderScreenLayout(static_cast<int>(rows.size()) + 1, guiW, guiH);
        const float h = l.listBottom - l.listTop;
        std::string d = Styles(guiW, guiH, 140);
        d += Text(0, 8, static_cast<float>(guiW), title, "", nullptr, true);
        d += Text(0, 19, static_cast<float>(guiW), subtitle, "g", nullptr, true);
        d += "<div style=\"left: 0px; top: " + N(l.listTop - 1.0f) +
             "px; width: " + N(static_cast<float>(guiW)) +
             "px; height: 1px; background-color: rgba(255,255,255,200);\"></div>\n";
        d += "<div style=\"left: 0px; top: " + N(l.listBottom) + "px; width: " + N(static_cast<float>(guiW)) +
             "px; height: 1px; background-color: rgba(255,255,255,200);\"></div>\n";
        d += "<div style=\"left: 0px; top: " + N(l.listTop) + "px; width: " + N(static_cast<float>(guiW)) +
             "px; height: " + N(h) + "px; background-color: rgba(0,0,0,60);\"></div>\n";
        d += "<div style=\"left: 0px; top: " + N(l.listTop) + "px; width: " + N(static_cast<float>(guiW)) +
             "px; height: " + N(h) + "px; overflow: hidden;\">\n";
        for (int i = 0; i < static_cast<int>(rows.size()); i++) {
            const float y = l.RowY(i, scroll) - l.listTop;
            if (y < 0.0f || y + 18.0f > h)
                continue;
            const bool selected = i == selectedRow;
            const bool toggle = i == toggleRow;
            if (selected || toggle)
                d +=
                    "<div style=\"left: " + N(l.colX - 1) + "px; top: " + N(y - 1) +
                    "px; width: " + N(l.colW + 2) + "px; height: 20px; background-color: " +
                    (toggle ? "rgba(0,0,0,150)" : "rgba(0,0,0,110)") + ";\"></div>\n" +
                    "<div style=\"left: " + N(l.colX - 1) + "px; top: " + N(y - 1) +
                    "px; width: " + N(l.colW + 2) + "px; height: 1px; background-color: #d0d0d0;\"></div>\n" +
                    "<div style=\"left: " + N(l.colX - 1) + "px; top: " + N(y + 18) +
                    "px; width: " + N(l.colW + 2) + "px; height: 1px; background-color: #d0d0d0;\"></div>\n" +
                    "<div style=\"left: " + N(l.colX - 1) + "px; top: " + N(y - 1) +
                    "px; width: 1px; height: 20px; background-color: #d0d0d0;\"></div>\n" +
                    "<div style=\"left: " + N(l.colX + l.colW) + "px; top: " + N(y - 1) +
                    "px; width: 1px; height: 20px; background-color: #d0d0d0;\"></div>\n";
            d += "<div id=\"sh" + std::to_string(i) + "\" class=\"srow\" style=\"left: " + N(l.colX) +
                 "px; top: " + N(y) + "px; width: " + N(l.colW) + "px;\"></div>\n";
            d += Text(l.colX, y + 5, l.colW, rows[static_cast<size_t>(i)], "",
                      selected && !toggle ? "#ffff55" : nullptr, true);
        }
        d += Text(l.colX, l.RowY(static_cast<int>(rows.size()), scroll) - l.listTop + 5, l.colW, dragText,
                  "g", nullptr, true);
        d += "</div>\n";
        if (l.maxScroll > 0.0f)
            d += Bar(static_cast<float>(guiW) / 2.0f + l.colW / 2.0f + 6.0f, l.listTop, h, l.contentH, scroll,
                     l.maxScroll);
        d += Btn("shopen", l.openX, l.row1Y, l.btnW1, open, false);
        d += Btn("shsettings", l.settingsX, l.row1Y, l.btnW1, settings, false);
        d += Btn("shcancel", l.cancelX, l.row2Y, l.thirdW, cancel, false);
        d += Btn("shapply", l.applyX, l.row2Y, l.thirdW, apply, false);
        d += Btn("shdone", l.doneX, l.row2Y, l.thirdW, done, false);
        if (!progressText.empty() || progress >= 0.0f) {

            const float pw = std::min(static_cast<float>(guiW) - 20.0f, 300.0f);
            const float px = (static_cast<float>(guiW) - pw) / 2.0f;
            const float py = l.listTop + (l.listBottom - l.listTop) / 2.0f - 22.0f;
            d += "<div style=\"left: " + N(px) + "px; top: " + N(py) + "px; width: " + N(pw) +
                 "px; height: 44px; background-color: rgba(0,0,0,215); border-width: 1px; border-color: "
                 "#a0a0a0;\"></div>\n";
            d += Text(px + 6, py + 7, pw - 12, progressText, "", nullptr, true);
            if (progress >= 0.0f) {
                const float bw = pw - 24.0f;
                d += "<div style=\"left: " + N(px + 12) + "px; top: " + N(py + 26) + "px; width: " + N(bw) +
                     "px; height: 6px; background-color: #404040;\"></div>\n";
                d += "<div style=\"left: " + N(px + 12) + "px; top: " + N(py + 26) +
                     "px; width: " + N(bw * std::clamp(progress, 0.0f, 1.0f)) +
                     "px; height: 6px; background-color: #80ff20;\"></div>\n";
            }
        }
        d += "</body>\n</rml>\n";
        return d;
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

    std::string BuildModScreenRml(const std::vector<ModEntry>& rows, int selected, const ModDetail& detail,
                                  const std::string& title, const std::string& search, bool caret,
                                  const std::string& showing, const std::string& hint,
                                  const std::string& open, const std::string& done, float scroll,
                                  float detailScroll, int guiW, int guiH) {
        const ModScreenLayout l = ComputeModScreenLayout(static_cast<int>(rows.size()), detail, guiW, guiH);
        const float listH = l.listBottom - l.listTop;
        std::string d = Styles(guiW, guiH, 140);
        d += Text(l.leftX, 6, l.leftW, title, "", nullptr, true);
        d += "<div style=\"left: " + N(l.leftX) + "px; top: " + N(l.searchY) + "px; width: " + N(l.leftW) +
             "px; height: " + N(l.searchH) +
             "px; background-color: #a0a0a0; z-index: 3;\"></div>\n<div style=\"left: " + N(l.leftX + 1) +
             "px; top: " + N(l.searchY + 1) + "px; width: " + N(l.leftW - 2) +
             "px; height: " + N(l.searchH - 2) + "px; background-color: #000000; z-index: 3;\"></div>\n";
        d += "<div class=\"t\" style=\"left: " + N(l.leftX + 5) + "px; top: " + N(l.searchY + 5) +
             "px; width: " + N(l.leftW - 10) + "px; z-index: 4; overflow: hidden;" +
             (search.empty() ? std::string(" color: #808080;") : std::string()) + "\">" +
             (search.empty() ? EscapeRml(hint) : TextFieldRml(search, caret)) + "</div>\n";
        d += Text(l.leftX, l.searchY + l.searchH + 6, l.leftW, showing, "");
        d += "<div class=\"panel\" style=\"left: " + N(l.leftX) + "px; top: " + N(l.listTop) +
             "px; width: " + N(l.leftW) + "px; height: " + N(listH) + "px;\">\n";
        for (int i = 0; i < static_cast<int>(rows.size()); i++) {
            const float y = l.RowY(i, scroll) - l.listTop;
            if (y + l.rowH < 0.0f || y > listH)
                continue;
            const ModEntry& e = rows[static_cast<size_t>(i)];
            const float rowW = l.leftW - 2.0f - (l.maxScroll > 0 ? 8.0f : 0.0f);
            if (i == selected)
                d += "<div style=\"left: 1px; top: " + N(y) + "px; width: " + N(rowW) +
                     "px; height: 34px; background-color: #ffffff;\"></div>\n<div style=\"left: 2px; top: " +
                     N(y + 1) + "px; width: " + N(rowW - 2) +
                     "px; height: 32px; background-color: #000000;\"></div>\n";
            d += "<div id=\"md" + std::to_string(i) + "\" class=\"prow\" style=\"left: 1px; top: " + N(y) +
                 "px; width: " + N(rowW) + "px;\"></div>\n";
            d += Img(e.icon, 4, y + 1, 32);
            d += FormattedText(40, y + 2,
                               rowW - 40.0f - (e.badge.empty() ? 0.0f : BadgeWidth(e.badge) + 6.0f), e.name);
            if (!e.badge.empty())
                d += Badge(40.0f + std::min(static_cast<float>(e.name.size()) * 6.0f + 6.0f,
                                            rowW - 40.0f - BadgeWidth(e.badge)),
                           y + 2, e.badge, e.badgeColor);
            d += "<div class=\"wrap\" style=\"left: 40px; top: " + N(y + 13) +
                 "px; width: " + N(rowW - 44.0f) + "px; height: 18px; z-index: 2;\">" +
                 FormatCodesToRml(e.desc) + "</div>\n";
        }
        d += Bar(l.leftW - 6.0f, 0, listH, static_cast<float>(rows.size()) * l.rowH + 4.0f, scroll,
                 l.maxScroll);
        d += "</div>\n";
        if (detail.valid) {
            d += Img(detail.icon, l.rightX, 18, 32);
            d += Text(l.rightX + 36, 20, l.rightW - 36, detail.name, "");
            if (!detail.badge.empty())
                d += Badge(l.rightX + 36 +
                               std::min(static_cast<float>(detail.name.size()) * 6.0f + 6.0f,
                                        l.rightW - 36.0f - BadgeWidth(detail.badge)),
                           20, detail.badge, detail.badgeColor);
            d += Text(l.rightX + 36, 31, l.rightW - 36, detail.version, "g");
            d += Text(l.rightX + 36, 42, l.rightW - 36, detail.author, "g");
            d += Btn("mdtoggle", l.toggleX, l.detailBtnY - 1, l.toggleW, detail.toggleLabel, false);
            d += Btn("mdreload", l.reloadX, l.detailBtnY - 1, l.reloadW, detail.reloadLabel,
                     !detail.toggleEnabled);
            if (detail.hasSettings)
                d += Btn("mdsettings", l.settingsX, l.detailBtnY - 1, l.settingsW, detail.settingsLabel,
                         false);
            const float th = l.detailTextBottom - l.detailTextTop;
            d += "<div class=\"panel\" style=\"left: " + N(l.rightX) + "px; top: " + N(l.detailTextTop) +
                 "px; width: " + N(l.rightW) + "px; height: " + N(th) + "px;\">\n";
            d += "<div class=\"wrap\" style=\"left: 6px; top: " + N(4.0f - detailScroll) +
                 "px; width: " + N(l.rightW - 20.0f) +
                 "px; height: " + N(EstimateWrapHeight(detail.body, l.rightW - 20.0f) + 9.0f) +
                 "px; color: #e0e0e0; overflow: visible;\">" + FormatCodesToRmlLines(detail.body) +
                 "</div>\n";
            d += Bar(l.rightW - 6.0f, 0, th, EstimateWrapHeight(detail.body, l.rightW - 20.0f) + 8.0f,
                     detailScroll, l.maxDetailScroll);
            d += "</div>\n";
        }
        d += Btn("mdopen", l.openX, l.btnY, l.btnW, open, false);
        d += Btn("mddone", l.doneX, l.btnY, l.btnW, done, false);
        d += "</body>\n</rml>\n";
        return d;
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

    std::string BuildWardrobeRml(const std::vector<WardrobeCard>& skins,
                                 const std::vector<WardrobeCard>& capes, bool skinOpen, bool capeOpen,
                                 int firstLine, const std::string& previewImg, const std::string& previewName,
                                 bool slim, bool canTrashSkin, bool canTrashCape, const WardrobeText& t,
                                 int guiW, int guiH) {
        const WardrobeLayout l =
            ComputeWardrobeLayout(static_cast<int>(skins.size()), static_cast<int>(capes.size()), skinOpen,
                                  capeOpen, firstLine, guiW, guiH);
        std::string d = Styles(guiW, guiH, 70);
        const auto box = [&](float x, float y, float w, float h, const char* color) {
            return "<div style=\"left: " + N(x) + "px; top: " + N(y) + "px; width: " + N(w) +
                   "px; height: " + N(h) + "px; background-color: " + color + ";\"></div>\n";
        };
        const auto icon = [&](const char* src, float x, float y, float w, float h) {
            return "<mcimg src=\"" + std::string(src) + "\" style=\"left: " + N(x) + "px; top: " + N(y) +
                   "px; width: " + N(w) + "px; height: " + N(h) + "px; z-index: 8;\"></mcimg>\n";
        };
        d += Text(0, 6, static_cast<float>(guiW), t.title, "", nullptr, true);
        for (const WardrobeLine& line : l.lines) {
            const bool isSkin = line.kind < 2;
            const std::vector<WardrobeCard>& cards = isSkin ? skins : capes;
            if (line.kind % 2 == 0) {
                const bool open = isSkin ? skinOpen : capeOpen;
                const std::string count = std::to_string(cards.size() - 1);
                d += "<div id=\"" + std::string(isSkin ? "wdhs" : "wdhc") +
                     "\" class=\"prow\" style=\"left: " + N(l.listX) + "px; top: " + N(line.y) +
                     "px; width: " + N(l.ToggleW()) + "px; height: " + N(l.headH) + "px;\"></div>\n";
                d += icon(open ? "textures/ui/chevron_white_down.png" : "textures/ui/chevron_white_up.png",
                          l.listX + 4.0f, line.y + 6.0f, 8.0f, 8.0f);
                d += Text(l.listX + 18.0f, line.y + 6.0f, l.ToggleW() - 20.0f,
                          (isSkin ? t.skins : t.capes) + " [" + count + "]", "",
                          isSkin ? "#ff99ff" : "#ffcc55");
                const bool canTrash = isSkin ? canTrashSkin : canTrashCape;
                d += Btn(isSkin ? "wdtrashs" : "wdtrashc", l.TrashX(), line.y, l.trashW, "", !canTrash);
                d += icon("textures/ui/icon_trash.png", l.TrashX() + (l.trashW - 10.0f) / 2.0f, line.y + 4.0f,
                          10.0f, 11.0f);
                d += Btn(isSkin ? "wdaddskin" : "wdaddcape", l.AddX(), line.y, l.addW,
                         isSkin ? t.addSkin : t.addCape, false);
                d += icon("textures/ui/plus.png", l.AddX() + 6.0f, line.y + 5.0f, 9.0f, 9.0f);
            } else {
                for (int c = 0; c < l.cols; c++) {
                    const int i = line.row * l.cols + c;
                    if (i >= static_cast<int>(cards.size()))
                        break;
                    const WardrobeCard& card = cards[static_cast<size_t>(i)];
                    const float x = l.CardX(i), y = line.y;
                    d += "<div id=\"" + std::string(isSkin ? "wds" : "wdp") + std::to_string(i) +
                         "\" class=\"prow\" style=\"left: " + N(x) + "px; top: " + N(y) +
                         "px; width: " + N(l.cardW) + "px; height: " + N(l.cardH) + "px;" +
                         (card.selected ? " background-color: rgba(255,255,255,34);" : "") + "\"></div>\n";
                    if (card.selected) {
                        d += box(x, y, l.cardW, 1, "#f0f0f0");
                        d += box(x, y + l.cardH - 1, l.cardW, 1, "#f0f0f0");
                        d += box(x, y, 1, l.cardH, "#f0f0f0");
                        d += box(x + l.cardW - 1, y, 1, l.cardH, "#f0f0f0");
                    }
                    if (!card.img.empty())
                        d += "<mcimg src=\"" + EscapeRml(card.img) +
                             "\" style=\"left: " + N(x + (l.cardW - 36.0f) / 2.0f) +
                             "px; top: " + N(y + 2.0f) + "px; width: 36px; height: 52px;\"></mcimg>\n";
                    d += Text(x, y + l.cardH - 11.0f, l.cardW, card.name, "",
                              card.selected ? "#ffff55" : nullptr, true);
                }
            }
        }
        if (l.maxFirst > 0)
            d += Bar(l.listX + l.listW + 3.0f, l.listTop, l.listBottom - l.listTop,
                     (l.listBottom - l.listTop) * static_cast<float>(l.totalLines) /
                         static_cast<float>(l.totalLines - l.maxFirst),
                     static_cast<float>(l.firstLine), static_cast<float>(l.maxFirst));
        d += Text(l.rightX, l.nameY, l.rightW, previewName, "", nullptr, true);
        if (!previewImg.empty())
            d += "<mcimg src=\"" + EscapeRml(previewImg) + "\" style=\"left: " + N(l.prevX) +
                 "px; top: " + N(l.prevY) + "px; width: " + N(l.prevW) + "px; height: " + N(l.prevH) +
                 "px;\"></mcimg>\n";
        d += Text(l.rightX, l.prevY + l.prevH + 1.0f, l.rightW, t.dragHint, "g", nullptr, true);
        d += Btn("wdslim", l.slimX, l.slimY, l.slimW, t.slim + ": " + (slim ? t.slimOn : t.slimOff), false);
        const float hintW = static_cast<float>(t.dropHint.size()) * 6.0f + 10.0f;
        d += box((static_cast<float>(guiW) - hintW) / 2.0f, l.hintY - 2.0f, hintW, 12.0f, "rgba(0,0,0,130)");
        d += Text(0, l.hintY, static_cast<float>(guiW), t.dropHint, "g", nullptr, true);
        d += Btn("wddone", l.doneX, l.doneY, l.doneW, t.done, false);
        d += "</body>\n</rml>\n";
        return d;
    }

    std::string BuildLanguageScreenRml(const std::vector<std::string>& rows, int selectedRow,
                                       const std::string& title, const std::string& subtitle,
                                       const std::string& done, float scroll, int guiW, int guiH) {
        const ShaderScreenLayout l = ComputeShaderScreenLayout(static_cast<int>(rows.size()), guiW, guiH);
        const float h = l.listBottom - l.listTop;
        std::string d = Styles(guiW, guiH, 140);
        d += Text(0, 8, static_cast<float>(guiW), title, "", nullptr, true);
        d += Text(0, 19, static_cast<float>(guiW), subtitle, "g", nullptr, true);
        d += "<div style=\"left: 0px; top: " + N(l.listTop - 1.0f) +
             "px; width: " + N(static_cast<float>(guiW)) +
             "px; height: 1px; background-color: rgba(255,255,255,200);\"></div>\n";
        d += "<div style=\"left: 0px; top: " + N(l.listBottom) + "px; width: " + N(static_cast<float>(guiW)) +
             "px; height: 1px; background-color: rgba(255,255,255,200);\"></div>\n";
        d += "<div style=\"left: 0px; top: " + N(l.listTop) + "px; width: " + N(static_cast<float>(guiW)) +
             "px; height: " + N(h) + "px; background-color: rgba(0,0,0,60);\"></div>\n";
        d += "<div style=\"left: 0px; top: " + N(l.listTop) + "px; width: " + N(static_cast<float>(guiW)) +
             "px; height: " + N(h) + "px; overflow: hidden;\">\n";
        for (int i = 0; i < static_cast<int>(rows.size()); i++) {
            const float y = l.RowY(i, scroll) - l.listTop;
            if (y < 0.0f || y + 18.0f > h)
                continue;
            const bool selected = i == selectedRow;
            if (selected)
                d +=
                    "<div style=\"left: " + N(l.colX - 1) + "px; top: " + N(y - 1) +
                    "px; width: " + N(l.colW + 2) +
                    "px; height: 20px; background-color: rgba(0,0,0,110);\"></div>\n" +
                    "<div style=\"left: " + N(l.colX - 1) + "px; top: " + N(y - 1) +
                    "px; width: " + N(l.colW + 2) + "px; height: 1px; background-color: #d0d0d0;\"></div>\n" +
                    "<div style=\"left: " + N(l.colX - 1) + "px; top: " + N(y + 18) +
                    "px; width: " + N(l.colW + 2) + "px; height: 1px; background-color: #d0d0d0;\"></div>\n" +
                    "<div style=\"left: " + N(l.colX - 1) + "px; top: " + N(y - 1) +
                    "px; width: 1px; height: 20px; background-color: #d0d0d0;\"></div>\n" +
                    "<div style=\"left: " + N(l.colX + l.colW) + "px; top: " + N(y - 1) +
                    "px; width: 1px; height: 20px; background-color: #d0d0d0;\"></div>\n";
            d += "<div id=\"lg" + std::to_string(i) + "\" class=\"srow\" style=\"left: " + N(l.colX) +
                 "px; top: " + N(y) + "px; width: " + N(l.colW) + "px;\"></div>\n";
            d += Text(l.colX, y + 5, l.colW, rows[static_cast<size_t>(i)], "", selected ? "#ffff55" : nullptr,
                      true);
        }
        d += "</div>\n";
        if (l.maxScroll > 0.0f)
            d += Bar(static_cast<float>(guiW) / 2.0f + l.colW / 2.0f + 6.0f, l.listTop, h, l.contentH, scroll,
                     l.maxScroll);
        d += Btn("lgdone", l.colX + (l.colW - 200.0f) / 2.0f, l.row1Y + 12.0f, 200.0f, done, false);
        d += "</body>\n</rml>\n";
        return d;
    }
}
