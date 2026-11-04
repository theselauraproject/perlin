#include "client/ui/LoadingScreen.hpp"

#include "client/ui/RmlEmit.hpp"
#include "client/ui/FormatCodes.hpp"
#include "client/ui/UiTemplate.hpp"

#include <algorithm>
#include <cmath>

namespace perlin::ui {
    std::string DefaultLoadingScreenTemplate() {
        return R"PERLINRML(<rml>
<head>
<title>loading</title>
<style>
/* Edit this file while the game runs: it is reloaded when it changes. {{name}} placeholders are filled in by Perlin. */
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
mcimg { display: block; position: absolute; }
#title { left: 0px; top: {{title_y}}px; width: {{w}}px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; z-index: 3; }
#track { left: {{bar_x}}px; top: {{bar_y}}px; width: {{bar_w}}px; height: 4px; background-color: #1e1e1e; border: 1px #000000; display: {{bar_display}}; z-index: 3; }
#fill { left: 1px; top: 1px; width: {{fill_w}}px; height: 2px; background-color: #5ed15e; }
#message { left: 0px; top: {{msg_y}}px; width: {{w}}px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #c6c6c6; white-space: nowrap; z-index: 3; }
#detail { left: {{detail_x}}px; top: {{detail_y}}px; width: {{detail_w}}px; height: 27px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #8a8a8a; z-index: 3; }
.btn { width: 204px; height: 20px; background-color: #000000; z-index: 5; }
.btn div { position: absolute; }
.btn .rim { left: 1px; top: 1px; width: 201px; height: 16px; background-color: #aaaaaa; }
.btn .face { left: 2px; top: 2px; width: 200px; height: 15px; background-color: #6f6f6f; }
.btn .shade { left: 1px; top: 17px; width: 202px; height: 2px; background-color: #565656; }
.btn .edge { left: 202px; top: 1px; width: 1px; height: 16px; background-color: #565656; }
.btn .lbl { font-family: minecraft-shadow; left: 0px; top: 6px; width: 204px; height: 9px; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; }
.btn:hover { background-color: #ffffff; }
.btn:hover .rim { background-color: #afafaf; }
.btn:hover .face { background-color: #757575; }
.btn:hover .shade, .btn:hover .edge { background-color: #5c5c5c; }
</style>
</head>
<body>
  <mcimg src="textures/blocks/dirt" tiled="1" tilescale="{{tile_scale}}" color="0.25 0.25 0.25" style="left: 0px; top: 0px; width: {{w}}px; height: {{h}}px;"></mcimg>
  <div id="title">{{title}}</div>
  <div id="message">{{message}}</div>
  <div id="track"><div id="fill"></div></div>
  <div id="detail">{{detail}}</div>
  <div id="cancel" class="btn" style="left: {{btn_x}}px; top: {{btn_y}}px;">
    <div class="rim"></div><div class="face"></div><div class="shade"></div><div class="edge"></div>
    <div class="lbl">{{cancel}}</div>
  </div>
</body>
</rml>
)PERLINRML";
    }

    std::string BuildLoadingScreenRml(const std::string& templateText, const LoadingScreenText& t, int guiW,
                                      int guiH, double) {
        const int msgY = guiH / 2 - 24;
        const bool bar = t.progress >= 0.0f;
        const int barW = std::min(182, guiW - 40);
        const int barY = msgY + 14;
        const int detailY = bar ? barY + 10 : msgY + 14;
        const int btnY = std::min(guiH - 24, detailY + 36 + (t.warning.empty() ? 0 : 32));
        std::string d = templateText;
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "tile_scale", "2");
        ReplaceAll(d, "msg_y", std::to_string(msgY));
        ReplaceAll(d, "detail_x", std::to_string(std::max(10, (guiW - 300) / 2)));
        ReplaceAll(d, "detail_y", std::to_string(detailY));
        ReplaceAll(d, "detail_w", std::to_string(std::min(300, guiW - 20)));
        ReplaceAll(d, "btn_x", std::to_string(guiW / 2 - 102));
        ReplaceAll(d, "btn_y", std::to_string(btnY));
        ReplaceAll(d, "title_y", std::to_string(msgY - 16));
        ReplaceAll(d, "title", FormatCodesToRml(t.title));
        ReplaceAll(d, "bar_x", std::to_string((guiW - barW) / 2));
        ReplaceAll(d, "bar_y", std::to_string(barY));
        ReplaceAll(d, "bar_w", std::to_string(barW));
        ReplaceAll(d, "bar_display", bar ? "block" : "none");
        ReplaceAll(d, "fill_w",
                   std::to_string(
                       static_cast<int>(std::clamp(t.progress, 0.0f, 1.0f) * static_cast<float>(barW - 2))));
        ReplaceAll(d, "cancel", EscapeRml(t.cancel.empty() ? std::string("Cancel") : t.cancel));
        ReplaceAll(d, "message", FormatCodesToRml(t.message));
        ReplaceAll(d, "detail", FormatCodesToRml(t.detail));
        if (!t.warning.empty()) {
            const int w = std::min(guiW - 20, 330);
            const std::string note = "<div style=\"left: " + std::to_string((guiW - w) / 2) +
                                     "px; top: " + std::to_string(btnY - 30) +
                                     "px; width: " + std::to_string(w) +
                                     "px; height: 27px; font-family: minecraft-shadow; font-size: 8px; "
                                     "line-height: 9px; text-align: center; color: #ffff55; z-index: 4;\">" +
                                     EscapeRml(t.warning) + "</div>\n";
            const size_t at = d.rfind("</body>");
            if (at != std::string::npos)
                d.insert(at, note);
        }
        return d;
    }

    namespace {
        const char* kDownloadHead = R"(<rml><head><style>
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background-color: #16181f; }
div { display: block; position: absolute; }
.t { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; height: 9px; white-space: nowrap; color: #ffffff; }
.c { text-align: center; }
.dim { color: #9a9ca6; }
.bad { color: #ff6b6b; }
.btn { height: 20px; background-color: #000000; z-index: 5; }
.btn div { position: absolute; }
.btn .face { left: 1px; top: 1px; height: 18px; background-color: #3c3f4a; }
.btn:hover .face { background-color: #565a68; }
.btn .lbl { left: 0px; top: 6px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; }
</style></head><body>
<div style="left:0px;top:0px;width:{{w}}px;height:{{h}}px;decorator:vertical-gradient(#1d2230ff #101116ff);"></div>
)";
        const ScreenTemplateDefault kDownloadDefault("download_screen.rml", kDownloadHead);
    }

    std::string BuildDownloadScreenRml(const DownloadScreenText& t, int guiW, int guiH, double time) {
        const auto N = [](int v) { return std::to_string(v); };
        const int barW = std::min(guiW - 40, 240);
        const int x = (guiW - barW) / 2;
        const int rowsH = static_cast<int>(t.rows.size()) * 34;
        const bool buttons = !t.retry.empty() || !t.skip.empty();
        const int contentH = 28 + rowsH + (buttons ? 30 : 0);
        int y = std::max(10, (guiH - contentH) / 2);
        std::string d = ScreenTemplate("download_screen.rml", kDownloadHead);
        ReplaceAll(d, "w", N(guiW));
        ReplaceAll(d, "h", N(guiH));
        d += "<div class=\"t c\" style=\"left:0px;top:" + N(y) + "px;width:" + N(guiW) + "px;\">" +
             EscapeRml(t.title) + "</div>\n";
        d += "<div class=\"t c dim\" style=\"left:0px;top:" + N(y + 12) + "px;width:" + N(guiW) + "px;\">" +
             EscapeRml(t.subtitle) + "</div>\n";
        y += 30;
        for (const DownloadRow& r : t.rows) {
            d += "<div class=\"t\" style=\"left:" + N(x) + "px;top:" + N(y) + "px;width:" + N(barW) +
                 "px;\">" + EscapeRml(r.label) + "</div>\n";
            d += "<div class=\"t" + std::string(r.failed ? " bad" : " dim") + "\" style=\"left:" + N(x) +
                 "px;top:" + N(y) + "px;width:" + N(barW) + "px;text-align:right;\">" + EscapeRml(r.status) +
                 "</div>\n";
            const int by = y + 12;
            d += "<div style=\"left:" + N(x) + "px;top:" + N(by) + "px;width:" + N(barW) +
                 "px;height:5px;background-color:#05060a;\"></div>\n";
            const char* fill = r.failed ? "#c84a4a" : "#62d26a";
            if (r.progress >= 0.0f) {
                const int w =
                    static_cast<int>(std::clamp(r.progress, 0.0f, 1.0f) * static_cast<float>(barW - 2));
                d += "<div style=\"left:" + N(x + 1) + "px;top:" + N(by + 1) + "px;width:" + N(w) +
                     "px;height:3px;background-color:" + fill + ";\"></div>\n";
            } else {
                const int gw = std::max(12, barW / 5);
                const double phase = std::fmod(time * 0.8, 1.0);
                const int gx = x + 1 + static_cast<int>(phase * static_cast<double>(barW - 2 - gw));
                d += "<div style=\"left:" + N(gx) + "px;top:" + N(by + 1) + "px;width:" + N(gw) +
                     "px;height:3px;background-color:" + fill + ";\"></div>\n";
            }
            y += 34;
        }
        if (buttons) {
            const bool both = !t.retry.empty() && !t.skip.empty();
            const int bw = both ? 98 : 150;
            int bx = both ? guiW / 2 - 100 : guiW / 2 - bw / 2;
            for (const auto& [id, label] :
                 {std::pair<const char*, const std::string*>{kDownloadRetryId, &t.retry},
                  {kDownloadSkipId, &t.skip}}) {
                if (label->empty())
                    continue;
                d += "<div id=\"" + std::string(id) + "\" class=\"btn\" style=\"left:" + N(bx) +
                     "px;top:" + N(y + 4) + "px;width:" + N(bw) +
                     "px;\"><div class=\"face\" style=\"width:" + N(bw - 2) +
                     "px;\"></div><div class=\"lbl\" style=\"width:" + N(bw) + "px;\">" + EscapeRml(*label) +
                     "</div></div>\n";
                bx += bw + 4;
            }
        }
        d += "</body></rml>\n";
        return d;
    }
}
