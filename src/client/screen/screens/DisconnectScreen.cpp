#include "client/screen/screens/DisconnectScreen.hpp"
#include "util/Resources.hpp"

#include "client/systems/text/FormatCodes.hpp"
#include "client/screen/screens/LoadingScreen.hpp"
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
        return ui::ReadMarkup("ui/screens/disconnect_screen.rml");
    }

    SceneDoc BuildDisconnectScreen(const std::string& templateText, const DisconnectScreenText& t, int guiW,
                                   int guiH) {
        const int textW = std::min(guiW - 40, 320);
        const int perLine = std::max(8, textW / 5);
        int lines = 0;
        {
            size_t start = 0;
            const std::string& text = t.reason;
            while (start <= text.size()) {
                size_t end = text.find('\n', start);
                if (end == std::string::npos)
                    end = text.size();
                const std::string line = text.substr(start, end - start);
                lines +=
                    std::max(1, static_cast<int>((StripFormatCodes(line).size() + perLine - 1) / perLine));
                start = end + 1;
            }
        }
        const int textH = lines * 9;
        const int titleY = std::max(10, guiH / 2 - 40 - textH / 2);
        const int textY = titleY + 20;
        const int codeY = textY + textH + 8;
        const int y1 = codeY + 22;

        SceneDoc doc;
        doc.rml = templateText;
        auto& vars = doc.scene.vars;
        vars["w"] = std::to_string(guiW);
        vars["h"] = std::to_string(guiH);
        vars["tile_scale"] = "2";
        vars["title_y"] = std::to_string(titleY);
        vars["text_x"] = std::to_string((guiW - textW) / 2);
        vars["text_y"] = std::to_string(textY);
        vars["text_w"] = std::to_string(textW);
        vars["text_h"] = std::to_string(textH);
        vars["code_y"] = std::to_string(codeY);
        vars["btn_x"] = std::to_string(guiW / 2 - 102);
        vars["y1"] = std::to_string(y1);
        vars["y2"] = std::to_string(y1 + 24);
        vars["y3"] = std::to_string(y1 + 48);
        vars["rejoin"] = t.rejoin;
        vars["quit"] = t.quit;
        vars["code"] = t.code;
        vars["extra"] = t.extra;
        doc.scene.flags["panorama"] = t.panorama;
        doc.scene.flags["rejoin_off"] = !t.rejoinEnabled;
        doc.scene.flags["has_extra"] = !t.extra.empty();
        doc.scene.texts["title_runs"] = ParseFormatRuns(t.title, "#ffffff");
        doc.scene.texts["reason_runs"] = ParseFormatLines(t.reason, "#ffffff");
        return doc;
    }
}
