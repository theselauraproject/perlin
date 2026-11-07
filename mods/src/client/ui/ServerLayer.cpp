#include "client/ui/ServerLayer.hpp"
#include "client/ui/UiTemplate.hpp"

#include "client/ui/FormatCodes.hpp"
#include "client/ui/McFont.hpp"
#include "client/ui/RmlEmit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace perlin::ui {
    namespace {
        std::string F(float v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
            return buf;
        }

        const char* BarColor(int c) {
            // packet color 0 is gray, vanilla order gray/blue/red/green/yellow/purple/white
            static const char* const colors[] = {"#808080", "#5555ff", "#ff5555", "#55ff55",
                                                 "#ffff55", "#aa00aa", "#ffffff", "#ff55ff"};
            return colors[std::clamp(c, 0, 7)];
        }

        std::string Text(const std::string& t, float x, float y, float w, const char* align,
                         const char* color = "#ffffff") {
            return "<div class=\"t\" style=\"left:" + F(x) + "px;top:" + F(y) + "px;width:" + F(w) +
                   "px;height:9px;text-align:" + align + ";color:" + color + ";\">" + FormatCodesToRml(t) +
                   "</div>\n";
        }

        std::string Rect(float x, float y, float w, float h, const std::string& style) {
            return "<div style=\"left:" + F(x) + "px;top:" + F(y) + "px;width:" + F(w) + "px;height:" + F(h) +
                   "px;" + style + "\"></div>\n";
        }
    }

    namespace {
        const char* kServerLayerHead =
            "<rml>\n<head>\n<title>server</title>\n<style>\n"
            "body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; "
            "padding: 0; background: transparent; overflow: hidden; }\n"
            "div { display: block; position: absolute; }\n"
            ".t { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; white-space: nowrap; "
            "color: #ffffff; z-index: 3; }\n"
            "</style>\n</head>\n<body>\n";
        const ScreenTemplateDefault kServerLayerDefault("server_layer.rml", kServerLayerHead);
    }

    std::string BuildServerLayerRml(const ServerLayerData& data, int guiW, int guiH) {
        std::string out = ScreenTemplate("server_layer.rml", kServerLayerHead);
        ReplaceAll(out, "w", std::to_string(guiW));
        ReplaceAll(out, "h", std::to_string(guiH));
        float y = 12.0f;
        for (const auto& bar : data.bars) {
            const float w = 182.0f, x = (guiW - w) / 2.0f;
            out += Text(bar.title, x - 100, y - 10, w + 200, "center");
            out += Rect(x, y, w, 5, "background-color:#202020e0;z-index:1;");
            out += Rect(x, y, w * std::clamp(bar.progress, 0.0f, 1.0f), 5,
                        std::string("background-color:") + BarColor(bar.color) + ";z-index:2;");
            if (bar.overlay > 0) {
                const int segments = bar.overlay == 1   ? 6
                                     : bar.overlay == 2 ? 10
                                     : bar.overlay == 3 ? 12
                                                        : 20;
                for (int i = 1; i < segments; i++)
                    out += Rect(x + w * static_cast<float>(i) / static_cast<float>(segments), y, 1, 5,
                                "background-color:#000000a0;z-index:4;");
            }
            y += 19.0f;
        }
        if (data.scoreboard) {
            const McFont& font = McFont::Instance();
            float width = font.Width(StripFormatCodes(data.scoreboardTitle));
            for (const auto& row : data.rows)
                width = std::max(width, font.Width(StripFormatCodes(row.first)) + 10.0f +
                                            font.Width(std::to_string(row.second)));
            width += 6.0f;
            const float height = static_cast<float>((data.rows.size() + 1) * 9 + 2);
            const float x = static_cast<float>(guiW) - width - 2.0f;
            // anchored upper-right below bossbars, vanilla positions it near the top edge
            const float top = 4.0f;
            out += Rect(x, top, width, height, "background-color:#00000066;z-index:1;");
            out += Rect(x, top, width, 10, "background-color:#00000040;z-index:1;");
            out += Text(data.scoreboardTitle, x, top + 1, width, "center");
            float ry = top + 11.0f;
            for (const auto& row : data.rows) {
                out += Text(row.first, x + 3, ry, width - 6, "left");
                out += Text(std::to_string(row.second), x + 3, ry, width - 6, "right", "#ff5555");
                ry += 9.0f;
            }
        }
        if (data.toast) {
            const McFont& font = McFont::Instance();
            const float w = std::max(font.Width(StripFormatCodes(data.toastTitle)),
                                     font.Width(StripFormatCodes(data.toastMessage))) +
                            16.0f;
            const float x = (static_cast<float>(guiW) - w) / 2.0f;
            out += Rect(x, 4, w, 26, "background-color:#101010e6;z-index:1;");
            out += Rect(x, 4, w, 1, "background-color:#ffffff80;z-index:2;");
            out += Text(data.toastTitle, x, 8, w, "center", "#ffff55");
            out += Text(data.toastMessage, x, 19, w, "center");
        }
        out += "</body>\n</rml>\n";
        return out;
    }

    std::string ServerLayerSignature(const ServerLayerData& data, int guiW, int guiH) {
        std::string s = std::to_string(guiW) + "x" + std::to_string(guiH) + "|";
        for (const auto& b : data.bars)
            s += b.title + "," + std::to_string(static_cast<int>(b.progress * 182.0f)) + "," +
                 std::to_string(b.color) + "," + std::to_string(b.overlay) + ";";
        if (data.scoreboard) {
            s += "|sb:" + data.scoreboardTitle;
            for (const auto& r : data.rows)
                s += "," + r.first + "=" + std::to_string(r.second);
        }
        if (data.toast)
            s += "|toast:" + data.toastTitle + data.toastMessage;
        return s;
    }
}
