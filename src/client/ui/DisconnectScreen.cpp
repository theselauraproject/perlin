#include "client/ui/DisconnectScreen.hpp"

#include "client/ui/FormatCodes.hpp"
#include "client/ui/RmlEmit.hpp"
#include "client/ui/UiLayout.hpp"
#include "client/ui/UiTemplate.hpp"

#include <algorithm>
#include <cctype>

namespace perlin::ui {
    bool LooksLikeBan(const std::string& text) {
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return lower.contains("ban");
    }

    std::string DefaultDisconnectScreenTemplate() {
        return R"PERLINRML(<rml>
<head>
<title>disconnect</title>
<style>
/* Edit this file while the game runs: it is reloaded when it changes. {{name}} placeholders are filled in by Perlin. */
body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
mcimg { display: block; position: absolute; }
#heading { left: 0px; top: {{title_y}}px; width: {{w}}px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; z-index: 3; }
#reason { left: {{text_x}}px; top: {{text_y}}px; width: {{text_w}}px; height: {{text_h}}px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; z-index: 3; }
#code { left: 0px; top: {{code_y}}px; width: {{w}}px; height: 9px; font-family: minecraft-shadow; font-size: 8px; line-height: 9px; text-align: center; color: #8a8a8a; white-space: nowrap; z-index: 3; }
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
.btn.off, .btn.off:hover { background-color: #000000; }
.btn.off .rim, .btn.off .face, .btn.off .shade, .btn.off .edge { background-color: #2c2c2c; }
.btn.off .lbl { color: #a0a0a0; }
</style>
</head>
<body>
  <mcimg src="textures/blocks/dirt" tiled="1" tilescale="{{tile_scale}}" color="0.25 0.25 0.25" style="left: 0px; top: 0px; width: {{w}}px; height: {{h}}px;"></mcimg>
  <div id="heading">{{title}}</div>
  <div id="reason">{{reason}}</div>
  <div id="code">{{code}}</div>
  <div id="rejoin" class="btn{{rejoin_off}}" style="left: {{btn_x}}px; top: {{y1}}px;">
    <div class="rim"></div><div class="face"></div><div class="shade"></div><div class="edge"></div>
    <div class="lbl">{{rejoin}}</div>
  </div>
  <div id="quit" class="btn" style="left: {{btn_x}}px; top: {{y2}}px;">
    <div class="rim"></div><div class="face"></div><div class="shade"></div><div class="edge"></div>
    <div class="lbl">{{quit}}</div>
  </div>
</body>
</rml>
)PERLINRML";
    }

    std::string BuildDisconnectScreenRml(const std::string& templateText, const DisconnectScreenText& t,
                                         int guiW, int guiH) {
        const int textW = std::min(guiW - 40, 320);
        const int perLine = std::max(8, textW / 5);
        int lines = 0;
        std::string body;
        {
            size_t start = 0;
            const std::string text = t.reason;
            while (start <= text.size()) {
                size_t end = text.find('\n', start);
                if (end == std::string::npos)
                    end = text.size();
                const std::string line = text.substr(start, end - start);
                lines +=
                    std::max(1, static_cast<int>((StripFormatCodes(line).size() + perLine - 1) / perLine));
                if (!body.empty())
                    body += "<br/>";
                body += FormatCodesToRml(line);
                start = end + 1;
            }
        }
        const int textH = lines * 9;
        const int titleY = std::max(10, guiH / 2 - 40 - textH / 2);
        const int textY = titleY + 20;
        const int codeY = textY + textH + 8;
        const int y1 = codeY + 22;

        std::string d = templateText;
        ReplaceAll(d, "w", std::to_string(guiW));
        ReplaceAll(d, "h", std::to_string(guiH));
        ReplaceAll(d, "tile_scale", "2");
        ReplaceAll(d, "title_y", std::to_string(titleY));
        ReplaceAll(d, "text_x", std::to_string((guiW - textW) / 2));
        ReplaceAll(d, "text_y", std::to_string(textY));
        ReplaceAll(d, "text_w", std::to_string(textW));
        ReplaceAll(d, "text_h", std::to_string(textH));
        ReplaceAll(d, "code_y", std::to_string(codeY));
        ReplaceAll(d, "btn_x", std::to_string(guiW / 2 - 102));
        ReplaceAll(d, "y1", std::to_string(y1));
        ReplaceAll(d, "y2", std::to_string(y1 + 24));
        ReplaceAll(d, "rejoin_off", t.rejoinEnabled ? "" : " off");
        ReplaceAll(d, "rejoin", EscapeRml(t.rejoin));
        ReplaceAll(d, "quit", EscapeRml(t.quit));
        ReplaceAll(d, "title", FormatCodesToRml(t.title));
        ReplaceAll(d, "code", EscapeRml(t.code));
        ReplaceAll(d, "reason", body);
        // added here rather than in the template so edited disconnect_screen.rml files get it too
        if (!t.extra.empty()) {
            const std::string row =
                "  <div id=\"" + std::string(kDisconnectExtraId) +
                "\" class=\"btn\" style=\"left: " + std::to_string(guiW / 2 - 102) +
                "px; top: " + std::to_string(y1 + 48) +
                "px;\">\n    <div class=\"rim\"></div><div class=\"face\"></div><div class=\"shade\"></div>"
                "<div class=\"edge\"></div>\n    <div class=\"lbl\">" +
                EscapeRml(t.extra) + "</div>\n  </div>\n";
            const size_t at = d.rfind("</body>");
            if (at != std::string::npos)
                d.insert(at, row);
        }
        return d;
    }
}
