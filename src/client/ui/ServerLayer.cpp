#include "client/ui/ServerLayer.hpp"
#include "client/ui/UiTemplate.hpp"

#include "client/systems/text/FormatCodes.hpp"
#include "client/systems/text/McFont.hpp"
#include "client/ui/SceneKit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace perlin::ui {
    namespace {
        const char* BarColor(int c) {

            static const char* const colors[] = {"#808080", "#5555ff", "#ff5555", "#55ff55",
                                                 "#ffff55", "#aa00aa", "#ffffff", "#ff55ff"};
            return colors[std::clamp(c, 0, 7)];
        }

        void Text(SceneBuilder& out, const std::string& t, float x, float y, float w, const char* cls,
                  const char* color = "#ffffff") {
            kit::Label(out, x, y, w, std::string("t ") + cls, kit::Formatted(t, color), "3");
        }
    }

    namespace {
        const ScreenTemplateDefault kServerLayerDefault("ui/overlays/server_layer.rml");
    }

    SceneDoc BuildServerLayer(const ServerLayerData& data, int guiW, int guiH) {
        SceneDoc doc;
        doc.rml = ScreenTemplate("ui/overlays/server_layer.rml");
        doc.scene.vars["w"] = std::to_string(guiW);
        doc.scene.vars["h"] = std::to_string(guiH);
        SceneBuilder out(doc.scene.lists["body"]);
        float y = 12.0f;
        for (const auto& bar : data.bars) {
            const float w = 182.0f, x = (guiW - w) / 2.0f;
            Text(out, bar.title, x - 100, y - 10, w + 200, "c");
            kit::Rect(out, x, y, w, 5, "#202020e0", "1");
            kit::Rect(out, x, y, w * std::clamp(bar.progress, 0.0f, 1.0f), 5, BarColor(bar.color), "2");
            if (bar.overlay > 0) {
                const int segments = bar.overlay == 1   ? 6
                                     : bar.overlay == 2 ? 10
                                     : bar.overlay == 3 ? 12
                                                        : 20;
                for (int i = 1; i < segments; i++)
                    kit::Rect(out, x + w * static_cast<float>(i) / static_cast<float>(segments), y, 1, 5,
                              "#000000a0", "4");
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

            const float top = 4.0f;
            kit::Rect(out, x, top, width, height, "#00000066", "1");
            kit::Rect(out, x, top, width, 10, "#00000040", "1");
            Text(out, data.scoreboardTitle, x, top + 1, width, "c");
            float ry = top + 11.0f;
            for (const auto& row : data.rows) {
                Text(out, row.first, x + 3, ry, width - 6, "");
                Text(out, std::to_string(row.second), x + 3, ry, width - 6, "r", "#ff5555");
                ry += 9.0f;
            }
        }
        if (data.toast) {
            const McFont& font = McFont::Instance();
            const float w = std::max(font.Width(StripFormatCodes(data.toastTitle)),
                                     font.Width(StripFormatCodes(data.toastMessage))) +
                            16.0f;
            const float x = (static_cast<float>(guiW) - w) / 2.0f;
            kit::Rect(out, x, 4, w, 26, "#101010e6", "1");
            kit::Rect(out, x, 4, w, 1, "#ffffff80", "2");
            Text(out, data.toastTitle, x, 8, w, "c", "#ffff55");
            Text(out, data.toastMessage, x, 19, w, "c");
        }
        return doc;
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
