#include "client/ui/PauseScreen.hpp"
#include <cmath>
#include <algorithm>

#include "client/ui/RmlEmit.hpp"
#include "client/ui/FormatCodes.hpp"
#include "client/ui/UiTemplate.hpp"
#include "client/ui/TextEdit.hpp"

namespace perlin::ui {
    std::string DefaultPauseScreenTemplate() {
        return R"PERLINRML(<!--perlin-ui:pause-11-->
<rml>
<head>
<title>pause</title>
<style>
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
div, mcimg { display: block; position: absolute; }
mcitem { display: block; position: absolute; }
mcimg { display: block; position: absolute; }
#dim { left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; background-color: rgba(0,0,0,64); }
#heading { left: 0px; top: 40px; width: {{w}}px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; z-index: 3; }
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
</style>
</head>
<body>
  <div id="dim"></div>
  <div id="heading">{{title}}</div>
  <div id="resume" class="btn" style="left: {{x1}}px; top: {{y1}}px; width: 204px;">
    <div class="rim" style="width: 201px;"></div><div class="face" style="width: 200px;"></div><div class="shade" style="width: 202px;"></div><div class="edge" style="left: 202px;"></div>
    <div class="lbl" style="width: 204px;">{{back}}</div>
  </div>
  <div id="feedback" class="btn" style="left: {{x1}}px; top: {{y2}}px; width: 100px;">
    <div class="rim" style="width: 97px;"></div><div class="face" style="width: 96px;"></div><div class="shade" style="width: 98px;"></div><div class="edge" style="left: 98px;"></div>
    <div class="lbl" style="width: 100px;">{{feedback}}</div>
  </div>
  <div id="players" class="btn" style="left: {{x2}}px; top: {{y2}}px; width: 100px;">
    <div class="rim" style="width: 97px;"></div><div class="face" style="width: 96px;"></div><div class="shade" style="width: 98px;"></div><div class="edge" style="left: 98px;"></div>
    <div class="lbl" style="width: 100px;">{{players}}</div>
  </div>
  <div id="options" class="btn" style="left: {{x1}}px; top: {{y3}}px; width: 100px;">
    <div class="rim" style="width: 97px;"></div><div class="face" style="width: 96px;"></div><div class="shade" style="width: 98px;"></div><div class="edge" style="left: 98px;"></div>
    <div class="lbl" style="width: 100px;">{{options}}</div>
  </div>
  <div id="wardrobe" class="btn" style="left: {{xw}}px; top: {{y3}}px; width: 20px;">
    <div class="rim" style="width: 17px;"></div><div class="face" style="width: 16px;"></div><div class="shade" style="width: 18px;"></div><div class="edge" style="left: 18px;"></div>
    <mcimg src="textures/ui/glyph_skin_pack.png" style="left: 4px; top: 4px; width: 12px; height: 12px; z-index: 3;"/>
  </div>
  <div id="world" class="btn" style="left: {{xr}}px; top: {{y3}}px; width: 100px;">
    <div class="rim" style="width: 97px;"></div><div class="face" style="width: 96px;"></div><div class="shade" style="width: 98px;"></div><div class="edge" style="left: 98px;"></div>
    <div class="lbl" style="width: 100px;">{{world}}</div>
  </div>
  <div id="mods" class="btn" style="left: {{x1}}px; top: {{y4}}px; width: 204px;">
    <div class="rim" style="width: 201px;"></div><div class="face" style="width: 200px;"></div><div class="shade" style="width: 202px;"></div><div class="edge" style="left: 202px;"></div>
    <div class="lbl" style="width: 204px;">{{mods}}</div>
  </div>
  <div id="quit" class="btn" style="left: {{x1}}px; top: {{y6}}px; width: 204px;">
    <div class="rim" style="width: 201px;"></div><div class="face" style="width: 200px;"></div><div class="shade" style="width: 202px;"></div><div class="edge" style="left: 202px;"></div>
    <div class="lbl" style="width: 204px;">{{quit}}</div>
  </div>
</body>
</rml>
)PERLINRML";
    }

    namespace {
        std::string Button(const std::string& id, float x, float y, float w, const std::string& label,
                           bool off) {
            const auto px = [](float v) { return std::to_string(static_cast<int>(std::lround(v))); };
            std::string d = "<div id=\"" + id + "\" class=\"btn" + (off ? " off" : "") +
                            "\" style=\"left: " + px(x) + "px; top: " + px(y) + "px; width: " + px(w) +
                            "px;\">";
            d += "<div class=\"rim\" style=\"width: " + px(w - 3) +
                 "px;\"></div><div class=\"face\" style=\"width: " + px(w - 4) + "px;\"></div>";
            d += "<div class=\"shade\" style=\"width: " + px(w - 2) +
                 "px;\"></div><div class=\"edge\" style=\"left: " + px(w - 2) + "px;\"></div>";
            d +=
                "<div class=\"lbl\" style=\"width: " + px(w) + "px;\">" + EscapeRml(label) + "</div></div>\n";
            return d;
        }

        std::string Slider(const std::string& id, float x, float y, float w, const std::string& label,
                           float fraction) {
            const auto px = [](float v) { return std::to_string(static_cast<int>(std::lround(v))); };
            const float handle = x + 1.0f + std::clamp(fraction, 0.0f, 1.0f) * (w - 10.0f);
            std::string d = "<div id=\"" + id + "\" class=\"btn track\" style=\"left: " + px(x) +
                            "px; top: " + px(y) + "px; width: " + px(w) + "px;\">";
            d += "<div class=\"rim\" style=\"width: " + px(w - 3) +
                 "px;\"></div><div class=\"face\" style=\"width: " + px(w - 4) + "px;\"></div>";
            d += "<div class=\"lbl\" style=\"width: " + px(w) + "px; z-index: 2;\">" + EscapeRml(label) +
                 "</div>";
            d += "<div class=\"knob\" style=\"left: " + px(handle - x) +
                 "px; top: 0px;\"><div class=\"krim\"></div><div class=\"kface\"></div><div "
                 "class=\"kshade\"></div></div></div>\n";
            return d;
        }
    }

    OptionsLayout ComputeOptionsLayout(const std::vector<OptionsRow>& rows, int guiW, int guiH,
                                       float scroll) {
        OptionsLayout l;
        l.count = static_cast<int>(rows.size());
        l.x.assign(rows.size(), 0.0f);
        l.y.assign(rows.size(), 0.0f);
        l.rowW.assign(rows.size(), l.w);
        l.visible.assign(rows.size(), 1);
        const float start = std::max(36.0f, static_cast<float>(guiH) / 6.0f - 12.0f);
        const float cx = static_cast<float>(guiW) / 2.0f;
        float y = start;
        int col = 0;
        for (size_t i = 0; i < rows.size(); i++) {
            if (rows[i].header) {
                if (col == 1) {
                    y += 24.0f;
                    col = 0;
                }
                l.x[i] = cx - 155.0f;
                l.y[i] = y + 4.0f;
                y += 22.0f;
                continue;
            }
            if (rows[i].tab > 0) {
                if (col == 1) {
                    y += 24.0f;
                    col = 0;
                }
                l.x[i] = cx - 155.0f + static_cast<float>(rows[i].tab - 1) * 105.0f;
                l.rowW[i] = 100.0f;
                l.y[i] = y;
                if (rows[i].tab == 3)
                    y += 28.0f;
                continue;
            }
            if (rows[i].wide) {
                if (col == 1) {
                    y += 24.0f;
                    col = 0;
                }
                l.x[i] = cx - 155.0f;
                l.rowW[i] = 310.0f;
                l.y[i] = y;
                y += 24.0f;
                continue;
            }
            l.x[i] = cx - 155.0f + static_cast<float>(col) * 160.0f;
            l.y[i] = y;
            if (++col == 2) {
                col = 0;
                y += 24.0f;
            }
        }
        if (col == 1)
            y += 24.0f;
        l.listTop = start;
        l.listBottom = static_cast<float>(guiH) - 32.0f;
        l.contentH = y - start;
        l.scrolling = y + 12.0f + 20.0f > static_cast<float>(guiH) - 6.0f;
        l.maxScroll = l.scrolling ? std::max(0.0f, l.contentH - (l.listBottom - l.listTop)) : 0.0f;
        const float offset = l.scrolling ? std::clamp(scroll, 0.0f, l.maxScroll) : 0.0f;
        for (size_t i = 0; i < rows.size(); i++) {
            l.y[i] -= offset;
            const float height = rows[i].header ? 9.0f : 20.0f;
            if (l.scrolling && (l.y[i] < l.listTop - 1.0f || l.y[i] + height > l.listBottom + 1.0f))
                l.visible[i] = 0;
        }
        l.doneW = 200.0f;
        l.doneX = cx - 100.0f;
        l.doneY = l.scrolling ? static_cast<float>(guiH) - 27.0f
                              : std::min(y + 12.0f, static_cast<float>(guiH) - 27.0f);
        return l;
    }

    namespace {
        const char* kOptionsHead = R"PERLINRML(<rml>
<head>
<title>options</title>
<style>
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
#dim { left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; background-color: rgba(0,0,0,128); }
#heading { left: 0px; top: 15px; width: {{w}}px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; z-index: 3; }
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
.btn.track .rim { background-color: #2c2c2c; }
.btn.track .face { background-color: #1f1f1f; }
.btn.track:hover { background-color: #ffffff; }
.btn.track:hover .rim { background-color: #2c2c2c; }
.btn.track:hover .face { background-color: #1f1f1f; }
.knob { width: 8px; height: 20px; z-index: 1; }
.knob .krim { left: 0px; top: 0px; width: 8px; height: 20px; background-color: #000000; }
.knob .kface { left: 1px; top: 1px; width: 6px; height: 16px; background-color: #aaaaaa; }
.knob .kshade { left: 1px; top: 17px; width: 6px; height: 2px; background-color: #565656; }
.btn.track:hover .kface { background-color: #bdbdbd; }
</style>
</head>
<body>
<div id="dim"></div>
<div id="heading">{{title}}</div>
)PERLINRML";
        const ScreenTemplateDefault kOptionsHeadDefault("options_screen.rml", kOptionsHead);
    }

    std::string BuildOptionsScreenRml(const std::vector<OptionsRow>& rows, const std::string& title,
                                      const std::string& done, float scroll, int guiW, int guiH, int hover) {
        const OptionsLayout l = ComputeOptionsLayout(rows, guiW, guiH, scroll);
        std::string d = ScreenTemplate("options_screen.rml", kOptionsHead);
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "title", EscapeRml(title));
        for (int i = 0; i < l.count; i++) {
            const OptionsRow& row = rows[static_cast<size_t>(i)];
            if (!l.visible[static_cast<size_t>(i)])
                continue;
            if (row.header) {
                d += "<div style=\"left: 0px; top: " +
                     std::to_string(static_cast<int>(l.y[static_cast<size_t>(i)])) +
                     "px; width: " + std::to_string(guiW) +
                     "px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; "
                     "text-align: center; color: #ffff55; white-space: nowrap;\">" +
                     EscapeRml(row.label) + "</div>\n";
                continue;
            }
            const std::string label = row.value.empty() ? row.label : row.label + ": " + row.value;
            if (row.fraction >= 0.0f)
                d += Slider("row" + std::to_string(i), l.x[static_cast<size_t>(i)],
                            l.y[static_cast<size_t>(i)], l.rowW[static_cast<size_t>(i)], label, row.fraction);
            else
                d += Button("row" + std::to_string(i), l.x[static_cast<size_t>(i)],
                            l.y[static_cast<size_t>(i)], l.rowW[static_cast<size_t>(i)], label, row.locked);
            if (!row.icon.empty()) {
                const std::string at =
                    "left: " + std::to_string(static_cast<int>(l.x[static_cast<size_t>(i)]) + 3) +
                    "px; top: " + std::to_string(static_cast<int>(l.y[static_cast<size_t>(i)]) + 2) +
                    "px; width: 16px; height: 16px; z-index: 9;";
                if (row.icon.starts_with("item:"))
                    d += "<mcitem key=\"" + EscapeRml(row.icon.substr(5)) + ";0\" style=\"" + at +
                         "\"></mcitem>\n";
                else
                    d += "<mcimg src=\"" + EscapeRml(row.icon) + "\" style=\"" + at + "\"></mcimg>\n";
            }
        }
        if (hover >= 0 && hover < l.count && l.visible[static_cast<size_t>(hover)]) {
            const OptionsRow& row = rows[static_cast<size_t>(hover)];
            if (!row.tip.empty() || row.impact > 0) {
                const float tw = 190.0f;
                const int perLine = static_cast<int>((tw - 8.0f) / 5.0f);
                int lines = 0;
                {
                    int col = 0;
                    lines = 1;
                    size_t p = 0;
                    while (p < row.tip.size()) {
                        size_t e = row.tip.find(' ', p);
                        if (e == std::string::npos)
                            e = row.tip.size();
                        const int len = static_cast<int>(e - p);
                        if (col > 0 && col + 1 + len > perLine) {
                            lines++;
                            col = len;
                        } else {
                            col += (col > 0 ? 1 : 0) + len;
                        }
                        p = e + 1;
                    }
                    if (row.tip.empty())
                        lines = 0;
                }
                const float th = static_cast<float>(lines) * 10.0f + (row.impact > 0 ? 12.0f : 0.0f) + 8.0f;
                float tx = std::min(l.x[static_cast<size_t>(hover)] +
                                        l.rowW[static_cast<size_t>(hover)] / 2.0f - tw / 2.0f,
                                    static_cast<float>(guiW) - tw - 4.0f);
                tx = std::max(tx, 4.0f);
                float ty = l.y[static_cast<size_t>(hover)] + l.h + 2.0f;
                if (ty + th > static_cast<float>(guiH) - 2.0f)
                    ty = l.y[static_cast<size_t>(hover)] - th - 2.0f;
                const auto px = [](float v) { return std::to_string(static_cast<int>(std::lround(v))); };
                d += "<div style=\"left: " + px(tx) + "px; top: " + px(ty) + "px; width: " + px(tw) +
                     "px; height: " + px(th) + "px; background-color: #100010; z-index: 20;\"></div>\n";
                d += "<div style=\"left: " + px(tx + 1) + "px; top: " + px(ty + 1) +
                     "px; width: " + px(tw - 2) + "px; height: " + px(th - 2) +
                     "px; background-color: #2d0a63; z-index: 21;\"></div>\n";
                d += "<div style=\"left: " + px(tx + 2) + "px; top: " + px(ty + 2) +
                     "px; width: " + px(tw - 4) + "px; height: " + px(th - 4) +
                     "px; background-color: #100010; z-index: 22;\"></div>\n";
                if (lines > 0)
                    d += "<div style=\"left: " + px(tx + 4) + "px; top: " + px(ty + 4) +
                         "px; width: " + px(tw - 8) +
                         "px; font-family: minecraft-shadow; font-size: 8px; line-height: 10px; color: "
                         "#ffffff; z-index: 23;\">" +
                         EscapeRml(row.tip) + "</div>\n";
                if (row.impact > 0) {
                    static const char* kNames[] = {"", "Low", "Medium", "High"};
                    static const char* kColors[] = {"#ffffff", "#55ff55", "#ffff55", "#ff5555"};
                    const int im = std::clamp(row.impact, 1, 3);
                    d += "<div style=\"left: " + px(tx + 4) + "px; top: " +
                         px(ty + 4 + static_cast<float>(lines) * 10.0f + (lines > 0 ? 2.0f : 0.0f)) +
                         "px; width: " + px(tw - 8) +
                         "px; font-family: minecraft-shadow; font-size: 8px; line-height: 10px; color: "
                         "#aaaaaa; white-space: nowrap; z-index: 23;\">Performance Impact: <span "
                         "style=\"color: " +
                         kColors[im] + ";\">" + kNames[im] + "</span></div>\n";
                }
            }
        }
        if (l.scrolling && l.maxScroll > 0.0f) {
            const float listH = l.listBottom - l.listTop;
            const float th = std::max(16.0f, listH * listH / l.contentH);
            const float ty = l.listTop + (listH - th) * (std::clamp(scroll, 0.0f, l.maxScroll) / l.maxScroll);
            d += "<div style=\"left: " + std::to_string(guiW / 2 + 160) +
                 "px; top: " + std::to_string(static_cast<int>(l.listTop)) +
                 "px; width: 6px; height: " + std::to_string(static_cast<int>(listH)) +
                 "px; background-color: #000000;\"></div>\n";
            d += "<div style=\"left: " + std::to_string(guiW / 2 + 160) +
                 "px; top: " + std::to_string(static_cast<int>(ty)) +
                 "px; width: 6px; height: " + std::to_string(static_cast<int>(th)) +
                 "px; background-color: #808080;\"></div>\n";
        }
        d += Button(kOptionsDoneId, l.doneX, l.doneY, l.doneW, done, false);
        d += "</body>\n</rml>\n";
        return d;
    }

    KeybindLayout ComputeKeybindLayout(int rows, int guiW, int guiH) {
        KeybindLayout l;
        l.listBottom = static_cast<float>(guiH) - 32.0f;
        const float cx = static_cast<float>(guiW) / 2.0f;
        l.labelX = cx - 160.0f;
        l.keyX = cx + 20.0f;
        l.resetX = cx + 100.0f;
        l.doneW = 200.0f;
        l.doneX = cx - 100.0f;
        l.doneY = static_cast<float>(guiH) - 26.0f;
        l.contentH = static_cast<float>(rows) * l.pitch + 12.0f;
        l.maxScroll = std::max(0.0f, l.contentH - (l.listBottom - l.listTop));
        return l;
    }

    namespace {
        const char* kKeybindsHead = R"PERLINRML(<rml>
<head>
<title>keybinds</title>
<style>
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
#dim { left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; background-color: rgba(0,0,0,128); }
#heading { left: 0px; top: 12px; width: {{w}}px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; z-index: 3; }
#list { overflow: hidden; background-color: rgba(0,0,0,112); }
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
.row { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; height: 9px; color: #ffffff; white-space: nowrap; }
.head { text-align: center; }
.sep { height: 1px; background-color: rgba(255,255,255,51); }
.sepb { height: 1px; background-color: rgba(0,0,0,191); }
.track { background-color: rgba(0,0,0,255); width: 6px; }
.thumb { background-color: #808080; width: 6px; }
</style>
</head>
<body>
<div id="dim"></div>
<div id="heading">{{title}}</div>
)PERLINRML";
        const ScreenTemplateDefault kKeybindsHeadDefault("keybinds_screen.rml", kKeybindsHead);
    }

    std::string BuildKeybindsScreenRml(const std::vector<KeybindRow>& rows, const std::string& title,
                                       const std::string& done, const std::string& reset, float scroll,
                                       int guiW, int guiH) {
        const KeybindLayout l = ComputeKeybindLayout(static_cast<int>(rows.size()), guiW, guiH);
        const auto px = [](float v) { return std::to_string(static_cast<int>(std::lround(v))); };
        const float listH = l.listBottom - l.listTop;
        std::string d = ScreenTemplate("keybinds_screen.rml", kKeybindsHead);
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "title", EscapeRml(title));
        d += "<div id=\"list\" style=\"left: 0px; top: " + px(l.listTop) + "px; width: " + px(guiW) +
             "px; height: " + px(listH) + "px;\">\n";
        const auto key = [&](int i, int which, float x, float w, const std::string& label, bool off,
                             const char* color) {
            const std::string id = "kb" + std::to_string(which) + "_" + std::to_string(i);
            const float y = l.RowY(i, scroll) - l.listTop;
            std::string b = "<div id=\"" + id + "\" class=\"btn" + (off ? " off" : "") +
                            "\" style=\"left: " + px(x) + "px; top: " + px(y) + "px; width: " + px(w) +
                            "px;\">";
            b += "<div class=\"rim\" style=\"width: " + px(w - 3) +
                 "px;\"></div><div class=\"face\" style=\"width: " + px(w - 4) + "px;\"></div>";
            b += "<div class=\"shade\" style=\"width: " + px(w - 2) +
                 "px;\"></div><div class=\"edge\" style=\"left: " + px(w - 2) + "px;\"></div>";
            b += "<div class=\"lbl\" style=\"width: " + px(w) + "px;" +
                 (color ? std::string(" color: ") + color + ";" : std::string()) + "\">" + EscapeRml(label) +
                 "</div></div>\n";
            return b;
        };
        for (int i = 0; i < static_cast<int>(rows.size()); i++) {
            const KeybindRow& r = rows[static_cast<size_t>(i)];
            const float y = l.RowY(i, scroll) - l.listTop;
            if (y < 0.0f || y + 20.0f > listH)
                continue;
            if (r.header) {
                d += "<div class=\"row head\" style=\"left: 0px; top: " + px(y + 6.0f) +
                     "px; width: " + px(guiW) + "px;\">" + EscapeRml(r.label) + "</div>\n";
                continue;
            }
            d += "<div class=\"row\" style=\"left: " + px(l.labelX) + "px; top: " + px(y + 6.0f) +
                 "px; width: 175px;\">" + EscapeRml(r.label) + "</div>\n";
            d += key(i, 0, l.keyX, l.keyW, r.value, false,
                     r.color == 1   ? "#ff5555"
                     : r.color == 2 ? "#ffff55"
                                    : nullptr);
            d += key(i, 1, l.resetX, l.resetW, reset, !r.canReset, nullptr);
        }
        d += "</div>\n";
        d += "<div class=\"sepb\" style=\"left: 0px; top: " + px(l.listTop - 1.0f) +
             "px; width: " + px(guiW) + "px;\"></div>\n";
        d += "<div class=\"sep\" style=\"left: 0px; top: " + px(l.listTop - 2.0f) + "px; width: " + px(guiW) +
             "px;\"></div>\n";
        d += "<div class=\"sepb\" style=\"left: 0px; top: " + px(l.listBottom) + "px; width: " + px(guiW) +
             "px;\"></div>\n";
        d += "<div class=\"sep\" style=\"left: 0px; top: " + px(l.listBottom + 1.0f) +
             "px; width: " + px(guiW) + "px;\"></div>\n";
        if (l.maxScroll > 0.0f) {
            const float th = std::max(16.0f, listH * listH / l.contentH);
            const float ty = l.listTop + (listH - th) * (scroll / l.maxScroll);
            d += "<div class=\"track\" style=\"left: " + px(guiW / 2 + 160) + "px; top: " + px(l.listTop) +
                 "px; height: " + px(listH) + "px;\"></div>\n";
            d += "<div class=\"thumb\" style=\"left: " + px(guiW / 2 + 160) + "px; top: " + px(ty) +
                 "px; height: " + px(th) + "px;\"></div>\n";
        }
        d += Button(kKeybindDoneId, l.doneX, l.doneY, l.doneW, done, false);
        d += "</body>\n</rml>\n";
        return d;
    }

    namespace {
        const char* kListHead = R"PERLINRML(<rml>
<head>
<title>list</title>
<style>
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
#dim { left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; background-color: rgba(0,0,0,{{dim}}); }
#heading { left: 0px; top: 12px; width: {{w}}px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; z-index: 3; }
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
.row { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; height: 9px; color: #ffffff; white-space: nowrap; }
.sub { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; height: 9px; color: #a0a0a0; white-space: nowrap; }
.head { text-align: center; color: #ffff55; }
.sep { height: 1px; background-color: rgba(255,255,255,51); }
.sepb { height: 1px; background-color: rgba(0,0,0,191); }
)PERLINRML";
        const ScreenTemplateDefault kListHeadDefault("list_screen.rml", kListHead);
    }

    namespace {
        std::string Num(float v) {
            return std::to_string(static_cast<int>(std::lround(v)));
        }

        std::string ListStyles(int guiW, int guiH, int dim) {
            std::string d = ScreenTemplate("list_screen.rml", kListHead);
            ReplaceAll(d, "w", std::to_string(guiW));
            ReplaceAll(d, "h", std::to_string(guiH));
            ReplaceAll(d, "dim", std::to_string(dim));
            return d;
        }

        std::string ButtonBox(const std::string& id, float x, float y, float w, const std::string& label,
                              bool off, const char* color) {
            std::string b = "<div id=\"" + id + "\" class=\"btn" + (off ? " off" : "") +
                            "\" style=\"left: " + Num(x) + "px; top: " + Num(y) + "px; width: " + Num(w) +
                            "px;\">";
            b += "<div class=\"rim\" style=\"width: " + Num(w - 3) +
                 "px;\"></div><div class=\"face\" style=\"width: " + Num(w - 4) + "px;\"></div>";
            b += "<div class=\"shade\" style=\"width: " + Num(w - 2) +
                 "px;\"></div><div class=\"edge\" style=\"left: " + Num(w - 2) + "px;\"></div>";
            b += "<div class=\"lbl\" style=\"width: " + Num(w) + "px;" +
                 (color ? std::string(" color: ") + color + ";" : std::string()) + "\">" + EscapeRml(label) +
                 "</div></div>\n";
            return b;
        }
    }

    float ListLayout::ButtonX(const ListRow& row, int index) const {
        float x = rightX;
        for (int i = static_cast<int>(row.buttons.size()) - 1; i >= index; i--)
            x -= row.buttons[static_cast<size_t>(i)].w +
                 (i == static_cast<int>(row.buttons.size()) - 1 ? 0.0f : 4.0f);
        return x;
    }

    ListLayout ComputeListLayout(int rows, int guiW, int guiH) {
        ListLayout l;
        l.listBottom = static_cast<float>(guiH) - 32.0f;
        const float cx = static_cast<float>(guiW) / 2.0f;
        l.labelX = cx - 160.0f;
        l.rightX = cx + 160.0f;
        l.doneW = 200.0f;
        l.doneX = cx - 100.0f;
        l.doneY = static_cast<float>(guiH) - 26.0f;
        l.contentH = static_cast<float>(rows) * l.pitch + 12.0f;
        l.maxScroll = std::max(0.0f, l.contentH - (l.listBottom - l.listTop));
        return l;
    }

    std::string BuildListScreenRml(const std::vector<ListRow>& rows, const std::string& title,
                                   const std::string& done, float scroll, int guiW, int guiH) {
        const ListLayout l = ComputeListLayout(static_cast<int>(rows.size()), guiW, guiH);
        const float listH = l.listBottom - l.listTop;
        std::string d = ListStyles(guiW, guiH, 128);
        d += "</style>\n</head>\n<body>\n<div id=\"dim\"></div>\n<div id=\"heading\">" + EscapeRml(title) +
             "</div>\n";
        d += "<div style=\"left: 0px; top: " + Num(l.listTop) + "px; width: " + Num(guiW) +
             "px; height: " + Num(listH) + "px; background-color: rgba(0,0,0,112);\"></div>\n";
        for (int i = 0; i < static_cast<int>(rows.size()); i++) {
            const ListRow& r = rows[static_cast<size_t>(i)];
            const float y = l.RowY(i, scroll);
            if (y < l.listTop || y + 20.0f > l.listBottom)
                continue;
            if (r.header) {
                d += "<div class=\"row head\" style=\"left: 0px; top: " + Num(y + 6.0f) +
                     "px; width: " + Num(guiW) + "px;\">" + EscapeRml(r.label) + "</div>\n";
                continue;
            }
            const char* color = r.labelColor == 1   ? "#ff5555"
                                : r.labelColor == 2 ? "#55ff55"
                                : r.labelColor == 3 ? "#aaaaaa"
                                                    : nullptr;
            d += "<div class=\"row\" style=\"left: " + Num(l.labelX) +
                 "px; top: " + Num(r.sub.empty() ? y + 6.0f : y + 1.0f) + "px; width: 190px;" +
                 (color ? std::string(" color: ") + color + ";" : std::string()) + "\">" +
                 EscapeRml(r.label) + "</div>\n";
            if (!r.sub.empty())
                d += "<div class=\"sub\" style=\"left: " + Num(l.labelX) + "px; top: " + Num(y + 11.0f) +
                     "px; width: 190px;\">" + EscapeRml(r.sub) + "</div>\n";
            for (int b = 0; b < static_cast<int>(r.buttons.size()); b++) {
                const ListButton& btn = r.buttons[static_cast<size_t>(b)];
                d += ButtonBox("lb" + std::to_string(b) + "_" + std::to_string(i), l.ButtonX(r, b), y, btn.w,
                               btn.text, !btn.enabled,
                               btn.color == 1   ? "#ff5555"
                               : btn.color == 2 ? "#ffff55"
                               : btn.color == 3 ? "#55ff55"
                                                : nullptr);
            }
        }
        d += "<div class=\"sepb\" style=\"left: 0px; top: " + Num(l.listTop - 1.0f) +
             "px; width: " + Num(guiW) + "px;\"></div>\n";
        d += "<div class=\"sep\" style=\"left: 0px; top: " + Num(l.listTop - 2.0f) +
             "px; width: " + Num(guiW) + "px;\"></div>\n";
        d += "<div class=\"sepb\" style=\"left: 0px; top: " + Num(l.listBottom) + "px; width: " + Num(guiW) +
             "px;\"></div>\n";
        d += "<div class=\"sep\" style=\"left: 0px; top: " + Num(l.listBottom + 1.0f) +
             "px; width: " + Num(guiW) + "px;\"></div>\n";
        if (l.maxScroll > 0.0f) {
            const float th = std::max(16.0f, listH * listH / l.contentH);
            const float ty = l.listTop + (listH - th) * (scroll / l.maxScroll);
            d += "<div style=\"left: " + Num(guiW / 2 + 164) + "px; top: " + Num(l.listTop) +
                 "px; width: 6px; height: " + Num(listH) + "px; background-color: #000000;\"></div>\n";
            d += "<div style=\"left: " + Num(guiW / 2 + 164) + "px; top: " + Num(ty) +
                 "px; width: 6px; height: " + Num(th) + "px; background-color: #808080;\"></div>\n";
        }
        d += ButtonBox("listdone", l.doneX, l.doneY, l.doneW, done, false, nullptr);
        d += "</body>\n</rml>\n";
        return d;
    }

    WorldLayout ComputeWorldLayout(const std::vector<WorldRow>& rows, int guiW, int guiH, float scroll) {
        WorldLayout l;
        l.panelW = std::min(300.0f, static_cast<float>(guiW) - 20.0f);
        l.panelX = (static_cast<float>(guiW) - l.panelW) / 2.0f;
        l.listTop = 28.0f;
        l.listBottom = static_cast<float>(guiH) - 34.0f;
        l.doneW = 200.0f;
        l.doneX = static_cast<float>(guiW) / 2.0f - 100.0f;
        l.doneY = static_cast<float>(guiH) - 27.0f;
        float y = 0.0f;
        for (const WorldRow& r : rows) {
            float h = 32.0f;
            switch (r.kind) {
            case WorldRow::Kind::Section:
                h = 26.0f;
                break;
            case WorldRow::Kind::Toggle:
                h = 32.0f;
                break;
            case WorldRow::Kind::Segmented:
                h = r.desc.empty() ? 42.0f : 54.0f;
                break;
            case WorldRow::Kind::Slider:
                h = 46.0f;
                break;
            case WorldRow::Kind::Number:
                h = 58.0f;
                break;
            case WorldRow::Kind::Button:
                h = 30.0f;
                break;
            }
            l.y.push_back(y);
            l.h.push_back(h);
            y += h;
        }
        l.contentH = y;
        l.maxScroll = std::max(0.0f, y - (l.listBottom - l.listTop));
        const float offset = std::clamp(scroll, 0.0f, l.maxScroll);
        for (float& v : l.y)
            v = l.listTop + v - offset;
        return l;
    }

    std::string BuildWorldScreenRml(const std::vector<WorldRow>& rows, const std::string& title,
                                    const std::string& done, float scroll, int guiW, int guiH) {
        const WorldLayout l = ComputeWorldLayout(rows, guiW, guiH, scroll);
        std::string d = ListStyles(guiW, guiH, 160);
        d += ".wl { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; height: 9px; color: "
             "#ffffff; white-space: nowrap; }\n";
        d += ".wd { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; height: 9px; color: "
             "#b4b4b4; white-space: nowrap; }\n";
        d += ".seg { text-align: center; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; "
             "height: 9px; white-space: nowrap; }\n";
        d += "</style>\n</head>\n<body>\n<div id=\"dim\"></div>\n<div id=\"heading\">" + EscapeRml(title) +
             "</div>\n";
        d += "<div style=\"left: " + Num(l.panelX) + "px; top: " + Num(l.listTop) +
             "px; width: " + Num(l.panelW) + "px; height: " + Num(l.listBottom - l.listTop) +
             "px; background-color: #484848;\"></div>\n";
        const float px = l.panelX;
        for (size_t i = 0; i < rows.size(); i++) {
            const WorldRow& r = rows[i];
            const float y = l.y[i], h = l.h[i];
            if (y < l.listTop - 0.5f || y + h > l.listBottom + 0.5f)
                continue;
            const char* labelColor = r.enabled ? "#ffffff" : "#8a8a8a";
            if (r.kind != WorldRow::Kind::Section)
                d += "<div style=\"left: " + Num(px) + "px; top: " + Num(y + h - 1.0f) +
                     "px; width: " + Num(l.panelW) + "px; height: 1px; background-color: #2e2e2e;\"></div>\n";
            switch (r.kind) {
            case WorldRow::Kind::Section:
                d += "<div class=\"wl\" style=\"left: " + Num(px + 8.0f) + "px; top: " + Num(y + 4.0f) +
                     "px; color: #ffffff;\">" + EscapeRml(r.label) + "</div>\n";
                if (!r.desc.empty())
                    d += "<div class=\"wd\" style=\"left: " + Num(px + 8.0f) + "px; top: " + Num(y + 14.0f) +
                         "px;\">" + EscapeRml(r.desc) + "</div>\n";
                break;
            case WorldRow::Kind::Toggle: {
                d += "<div class=\"wl\" style=\"left: " + Num(px + 8.0f) + "px; top: " + Num(y + 6.0f) +
                     "px; color: " + labelColor + ";\">" + EscapeRml(r.label) + "</div>\n";
                d += "<div class=\"wd\" style=\"left: " + Num(px + 8.0f) + "px; top: " + Num(y + 17.0f) +
                     "px; width: " + Num(l.panelW - 60.0f) + "px; overflow: hidden;\">" + EscapeRml(r.desc) +
                     "</div>\n";
                const float sx = px + l.panelW - 8.0f - 36.0f, sy = y + 8.0f;
                d += "<div style=\"left: " + Num(sx) + "px; top: " + Num(sy) +
                     "px; width: 36px; height: 16px; background-color: #1e1e1e;\"></div>\n";
                d += "<div style=\"left: " + Num(sx + 1.0f) + "px; top: " + Num(sy + 1.0f) +
                     "px; width: 34px; height: 14px; background-color: " + (r.on ? "#3b8526" : "#555555") +
                     ";\"></div>\n";
                const float tx = r.on ? sx + 18.0f : sx + 1.0f;
                d += "<div style=\"left: " + Num(tx) + "px; top: " + Num(sy + 1.0f) +
                     "px; width: 17px; height: 14px; background-color: #8a8a8a;\"></div>\n";
                d += "<div style=\"left: " + Num(tx + 1.0f) + "px; top: " + Num(sy + 1.0f) +
                     "px; width: 15px; height: 12px; background-color: #d0d0d0;\"></div>\n";
                d += "<div class=\"seg\" style=\"left: " + Num(r.on ? sx + 1.0f : sx + 18.0f) +
                     "px; top: " + Num(sy + 4.0f) + "px; width: 17px; color: #ffffff;\">" +
                     (r.on ? "I" : "O") + "</div>\n";
                break;
            }
            case WorldRow::Kind::Segmented: {
                d += "<div class=\"wl\" style=\"left: " + Num(px + 8.0f) + "px; top: " + Num(y + 5.0f) +
                     "px; color: " + labelColor + ";\">" + EscapeRml(r.label) + "</div>\n";
                const float segW =
                    (l.panelW - 16.0f) / static_cast<float>(std::max<size_t>(1, r.options.size()));
                for (size_t k = 0; k < r.options.size(); k++) {
                    const bool sel = static_cast<int>(k) == r.selected;
                    const float bx = px + 8.0f + static_cast<float>(k) * segW;
                    d += "<div style=\"left: " + Num(bx) + "px; top: " + Num(y + 16.0f) +
                         "px; width: " + Num(segW) +
                         "px; height: 20px; background-color: #1e1e1e;\"></div>\n";
                    d += "<div style=\"left: " + Num(bx + 1.0f) + "px; top: " + Num(y + 17.0f) +
                         "px; width: " + Num(segW - 2.0f) +
                         "px; height: 18px; background-color: " + (sel ? "#3b8526" : "#d0d0d0") +
                         ";\"></div>\n";
                    d += "<div class=\"seg\" style=\"left: " + Num(bx) + "px; top: " + Num(y + 22.0f) +
                         "px; width: " + Num(segW) + "px; color: " + (sel ? "#ffffff" : "#303030") + ";\">" +
                         EscapeRml(r.options[k]) + "</div>\n";
                }
                if (!r.desc.empty())
                    d += "<div class=\"wd\" style=\"left: " + Num(px + 8.0f) + "px; top: " + Num(y + 40.0f) +
                         "px; width: " + Num(l.panelW - 16.0f) + "px; overflow: hidden;\">" +
                         EscapeRml(r.desc) + "</div>\n";
                break;
            }
            case WorldRow::Kind::Slider: {
                d += "<div class=\"wl\" style=\"left: " + Num(px + 8.0f) + "px; top: " + Num(y + 5.0f) +
                     "px; color: " + labelColor + ";\">" + EscapeRml(r.label) + " " + EscapeRml(r.value) +
                     "</div>\n";
                d += "<div class=\"wd\" style=\"left: " + Num(px + 8.0f) + "px; top: " + Num(y + 15.0f) +
                     "px; width: " + Num(l.panelW - 16.0f) + "px; overflow: hidden;\">" + EscapeRml(r.desc) +
                     "</div>\n";
                const float tw = l.panelW - 16.0f;
                d += "<div style=\"left: " + Num(px + 8.0f) + "px; top: " + Num(y + 28.0f) +
                     "px; width: " + Num(tw) + "px; height: 8px; background-color: #1e1e1e;\"></div>\n";
                d += "<div style=\"left: " + Num(px + 9.0f) + "px; top: " + Num(y + 29.0f) +
                     "px; width: " + Num((tw - 2.0f) * std::clamp(r.fraction, 0.0f, 1.0f)) +
                     "px; height: 6px; background-color: #3b8526;\"></div>\n";
                const float hx = px + 8.0f + (tw - 8.0f) * std::clamp(r.fraction, 0.0f, 1.0f);
                d += "<div style=\"left: " + Num(hx) + "px; top: " + Num(y + 25.0f) +
                     "px; width: 8px; height: 14px; background-color: #1e1e1e;\"></div>\n";
                d += "<div style=\"left: " + Num(hx + 1.0f) + "px; top: " + Num(y + 26.0f) +
                     "px; width: 6px; height: 12px; background-color: #d0d0d0;\"></div>\n";
                break;
            }
            case WorldRow::Kind::Number: {
                d += "<div class=\"wl\" style=\"left: " + Num(px + 8.0f) + "px; top: " + Num(y + 5.0f) +
                     "px; color: " + labelColor + ";\">" + EscapeRml(r.label) + "</div>\n";
                d += "<div style=\"left: " + Num(px + 8.0f) + "px; top: " + Num(y + 16.0f) +
                     "px; width: " + Num(l.panelW - 16.0f) +
                     "px; height: 20px; background-color: " + (r.editing ? "#ffffff" : "#1e1e1e") +
                     ";\"></div>\n";
                d += "<div style=\"left: " + Num(px + 9.0f) + "px; top: " + Num(y + 17.0f) +
                     "px; width: " + Num(l.panelW - 18.0f) +
                     "px; height: 18px; background-color: #313233;\"></div>\n";
                d += "<div class=\"wl\" style=\"left: " + Num(px + 14.0f) + "px; top: " + Num(y + 22.0f) +
                     "px;\">" + (r.editing ? TextFieldRml(r.value, true) : EscapeRml(r.value)) + "</div>\n";
                d += "<div class=\"wd\" style=\"left: " + Num(px + 8.0f) + "px; top: " + Num(y + 41.0f) +
                     "px; width: " + Num(l.panelW - 16.0f) + "px; overflow: hidden;\">" + EscapeRml(r.desc) +
                     "</div>\n";
                break;
            }
            case WorldRow::Kind::Button:
                d += ButtonBox("wb" + std::to_string(i), px + 8.0f, y + 4.0f, 150.0f, r.label, !r.enabled,
                               nullptr);
                break;
            }
        }
        if (l.maxScroll > 0.0f) {
            const float listH = l.listBottom - l.listTop;
            const float th = std::max(16.0f, listH * listH / l.contentH);
            const float ty = l.listTop + (listH - th) * (std::clamp(scroll, 0.0f, l.maxScroll) / l.maxScroll);
            d += "<div style=\"left: " + Num(l.panelX + l.panelW + 2.0f) + "px; top: " + Num(ty) +
                 "px; width: 4px; height: " + Num(th) + "px; background-color: #808080;\"></div>\n";
        }
        d += ButtonBox("worlddone", l.doneX, l.doneY, l.doneW, done, false, nullptr);
        d += "</body>\n</rml>\n";
        return d;
    }

    std::string BuildPauseScreenRml(const std::string& templateText, const PauseScreenText& t, int guiW,
                                    int guiH) {
        std::string d = templateText;
        const int top = (guiH - (t.showReplay ? 178 : 154)) / 4;
        const int quitY = t.showReplay ? top + 174 : top + 150;
        const int x1 = guiW / 2 - 102;
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "x1", std::to_string(x1));
        ReplaceAll(d, "x2", std::to_string(x1 + 104));
        ReplaceAll(d, "y1", std::to_string(top + 54));
        ReplaceAll(d, "y2", std::to_string(top + 78));
        ReplaceAll(d, "y3", std::to_string(top + 102));
        ReplaceAll(d, "y4", std::to_string(top + 126));
        ReplaceAll(d, "y5", std::to_string(top + 150));
        ReplaceAll(d, "y6", std::to_string(quitY));
        ReplaceAll(d, "xw", std::to_string(x1 + 208));
        ReplaceAll(d, "xr", std::to_string(x1 + 104));
        ReplaceAll(d, "mods", EscapeRml(t.mods));
        ReplaceAll(d, "title", EscapeRml(t.title));
        ReplaceAll(d, "back", EscapeRml(t.back));
        ReplaceAll(d, "feedback", EscapeRml(t.feedback));
        ReplaceAll(d, "players", EscapeRml(t.players));
        ReplaceAll(d, "options", EscapeRml(t.options));
        ReplaceAll(d, "world", EscapeRml(t.world));
        ReplaceAll(d, "quit", EscapeRml(t.quit));
        if (t.showReplay) {
            const size_t at = d.find("<div id=\"quit\"");
            if (at != std::string::npos)
                d.insert(at,
                         Button(kPauseReplayStartId, static_cast<float>(x1), static_cast<float>(top + 150),
                                100.0f, t.replayStart, t.replayRecording) +
                             Button(kPauseReplayStopId, static_cast<float>(x1 + 104),
                                    static_cast<float>(top + 150), 100.0f, t.replayStop, !t.replayRecording));
        }
        if (t.showPanel) {
            const int pw = 150;
            const int rows = std::max<int>(1, static_cast<int>(t.panel.size()));
            const int shown = std::min(rows, (guiH - 40) / 14);
            const int ph = 22 + shown * 14 + 4;
            const int px = guiW - pw - 6;
            const int py = std::max(4, (guiH - ph) / 2);
            std::string b = "<div id=\"pp\" style=\"left: " + std::to_string(px) +
                            "px; top: " + std::to_string(py) + "px; width: " + std::to_string(pw) +
                            "px; height: " + std::to_string(ph) +
                            "px; background-color: #000000; z-index: 6;\">";
            b += "<div style=\"left: 1px; top: 1px; width: " + std::to_string(pw - 2) +
                 "px; height: " + std::to_string(ph - 2) + "px; background-color: #c6c6c6;\"></div>";
            b += "<div style=\"left: 1px; top: 1px; width: " + std::to_string(pw - 3) +
                 "px; height: " + std::to_string(ph - 3) + "px; background-color: #ffffff;\"></div>";
            b += "<div style=\"left: 2px; top: 2px; width: " + std::to_string(pw - 4) +
                 "px; height: " + std::to_string(ph - 4) + "px; background-color: #373737;\"></div>";
            b += "<div style=\"left: 0px; top: 8px; width: " + std::to_string(pw) +
                 "px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; "
                 "text-align: center; color: #ffffff; white-space: nowrap;\">" +
                 EscapeRml(t.panelTitle) + "</div>";
            for (int i = 0; i < shown && i < static_cast<int>(t.panel.size()); i++) {
                const PauseScreenText::PanelRow& r = t.panel[static_cast<size_t>(i)];
                const char* color = r.perm >= 2 ? "#ffaa00" : (r.perm == 0 ? "#aaaaaa" : "#55ff55");
                const std::string& role =
                    r.perm >= 2 ? t.roleOperator : (r.perm == 0 ? t.roleVisitor : t.roleMember);
                const int ry = 22 + i * 14;
                b += "<div style=\"left: 8px; top: " + std::to_string(ry) +
                     "px; width: 80px; height: 9px; font-family: minecraft-shadow; font-size: 8px; "
                     "line-height: 9px; color: #ffffff; white-space: nowrap; overflow: hidden;\">" +
                     FormatCodesToRml(r.name) + "</div>";
                b += "<div style=\"left: 88px; top: " + std::to_string(ry) +
                     "px; width: " + std::to_string(pw - 96) +
                     "px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; "
                     "text-align: right; color: " +
                     color + "; white-space: nowrap;\">" + EscapeRml(role) + "</div>";
            }
            b += "</div>\n";
            const size_t at = d.rfind("</body>");
            if (at != std::string::npos)
                d.insert(at, b);
        }
        return d;
    }
}
