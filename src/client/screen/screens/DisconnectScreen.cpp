#include "client/screen/screens/DisconnectScreen.hpp"
#include "util/Resources.hpp"

#include "client/systems/text/FormatCodes.hpp"
#include "client/screen/screens/LoadingScreen.hpp"
#include "client/ui/UiLayout.hpp"
#include "client/ui/SceneKit.hpp"

#include <algorithm>
#include <cctype>

namespace perlin::ui {
    bool LooksLikeBan(const std::string& text) {
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return lower.contains("ban");
    }

    SceneDoc BuildDisconnectScreen(const DisconnectScreenText& t, int guiW, int guiH) {
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
        doc.scene.theme = Theme::Disconnect;
        doc.scene.sprites = true;
        SceneBuilder b(doc.scene.nodes);
        const float w = static_cast<float>(guiW), h = static_cast<float>(guiH);
        kit::Backdrop(b, w, h, t.panorama);
        kit::Label(b, 0, static_cast<float>(titleY), w, "ctr nowrap", ParseFormatRuns(t.title, "#ffffff"), "3");
        kit::Label(b, static_cast<float>((guiW - textW) / 2), static_cast<float>(textY), static_cast<float>(textW),
                   "ctr", ParseFormatLines(t.reason, "#ffffff"), "3").h = static_cast<float>(textH);
        kit::Label(b, 0, static_cast<float>(codeY), w, "ctr nowrap", kit::Plain(t.code, "#8a8a8a"), "3");
        const float bx = static_cast<float>(guiW / 2 - 102);
        kit::Button(b, kDisconnectRejoinId, bx, static_cast<float>(y1), 204, kit::Plain(t.rejoin),
                    !t.rejoinEnabled);
        kit::Button(b, kDisconnectQuitId, bx, static_cast<float>(y1 + 24), 204, kit::Plain(t.quit));
        if (!t.extra.empty())
            kit::Button(b, kDisconnectExtraId, bx, static_cast<float>(y1 + 48), 204, kit::Plain(t.extra));
        return doc;
    }
}
