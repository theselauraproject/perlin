#include "client/screen/screens/LoadingScreen.hpp"
#include "util/Resources.hpp"

#include "client/ui/SceneEmit.hpp"
#include "client/systems/text/FormatCodes.hpp"
#include "client/ui/SceneKit.hpp"

#include <algorithm>
#include <cmath>

namespace perlin::ui {
    SceneDoc BuildLoadingScreen(const LoadingScreenText& t, int guiW, int guiH) {
        const int msgY = guiH / 2 - 24;
        const bool bar = t.progress >= 0.0f;
        const int barW = std::min(182, guiW - 40);
        const int barY = msgY + 14;
        const int detailY = bar ? barY + 10 : msgY + 14;
        const int btnY = std::min(guiH - 24, detailY + 36 + (t.warning.empty() ? 0 : 32));
        const int warnW = std::min(guiW - 20, 330);
        SceneDoc doc;
        doc.scene.theme = Theme::Loading;
        doc.scene.sprites = true;
        SceneBuilder b(doc.scene.nodes);
        const float w = static_cast<float>(guiW), h = static_cast<float>(guiH);
        kit::Backdrop(b, w, h, t.panorama);
        kit::Label(b, 0, static_cast<float>(msgY - 16), w, "ctr nowrap", ParseFormatRuns(t.title, "#ffffff"), "3");
        kit::Label(b, 0, static_cast<float>(msgY), w, "ctr nowrap", ParseFormatRuns(t.message, "#c6c6c6"), "3");
        if (bar) {
            const float bx = static_cast<float>((guiW - barW) / 2);
            const float by = static_cast<float>(barY);
            kit::Rect(b, bx, by, static_cast<float>(barW + 2), 6, "#000000", "3");
            kit::Rect(b, bx + 1, by + 1, static_cast<float>(barW), 4, "#1e1e1e", "3");
            const int fill = static_cast<int>(std::clamp(t.progress, 0.0f, 1.0f) * static_cast<float>(barW - 2));
            kit::Rect(b, bx + 2, by + 2, static_cast<float>(fill), 2, "#5ed15e", "3");
        }
        kit::Label(b, static_cast<float>(std::max(10, (guiW - 300) / 2)), static_cast<float>(detailY),
                   static_cast<float>(std::min(300, guiW - 20)), "ctr", ParseFormatRuns(t.detail, "#8a8a8a"), "3");
        kit::Button(b, kLoadingCancelId, static_cast<float>(guiW / 2 - 102), static_cast<float>(btnY), 204,
                    kit::Plain(t.cancel.empty() ? std::string("Cancel") : t.cancel));
        if (!t.warning.empty())
            kit::Label(b, static_cast<float>((guiW - warnW) / 2), static_cast<float>(btnY - 30),
                       static_cast<float>(warnW), "ctr", kit::Plain(t.warning, "#ffff55"), "4");
        return doc;
    }

    SceneDoc BuildDownloadScreen(const DownloadScreenText& t, int guiW, int guiH, double time) {
        const int barW = std::min(guiW - 40, 240);
        const int x = (guiW - barW) / 2;
        const int rowsH = static_cast<int>(t.rows.size()) * 42;
        const bool buttons = !t.retry.empty() || !t.skip.empty();
        const int contentH = 28 + rowsH + (buttons ? 30 : 0);
        int y = std::max(10, (guiH - contentH) / 2);
        SceneDoc doc;
        doc.scene.theme = Theme::Download;
        SceneBuilder b(doc.scene.nodes);
        const float w = static_cast<float>(guiW), h = static_cast<float>(guiH);
        kit::Gradient(b, 0, 0, w, h, "vertical-gradient(#1d2230ff #101116ff)", "1");
        kit::Label(b, 0, static_cast<float>(y), w, "ctr", kit::Plain(t.title), "3");
        kit::Label(b, 0, static_cast<float>(y + 12), w, "ctr dim", kit::Plain(t.subtitle), "3");
        y += 30;
        for (const DownloadRow& r : t.rows) {
            const int by = y + 12;
            int fillX = x + 1, fillW = 0;
            if (r.progress >= 0.0f) {
                fillW = static_cast<int>(std::clamp(r.progress, 0.0f, 1.0f) * static_cast<float>(barW - 2));
            } else {
                const int gw = std::max(12, barW / 5);
                const double phase = std::fmod(time * 0.8, 1.0);
                fillX = x + 1 + static_cast<int>(phase * static_cast<double>(barW - 2 - gw));
                fillW = gw;
            }
            const float fx = static_cast<float>(x), bw = static_cast<float>(barW);
            kit::Label(b, fx, static_cast<float>(y), bw, "nowrap", kit::Plain(r.label), "3");
            kit::Label(b, fx, static_cast<float>(y + 20), bw, r.failed ? "right bad nowrap" : "right dim nowrap",
                       kit::Plain(r.status), "3");
            kit::Rect(b, fx, static_cast<float>(by), bw, 5, "#05060a", "3", "track");
            kit::Rect(b, static_cast<float>(fillX), static_cast<float>(by + 1), static_cast<float>(fillW), 3,
                      r.failed ? "#c84a4a" : "#62d26a", "3", r.failed ? "fill bad" : "fill");
            y += 42;
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
                kit::Button(b, id, static_cast<float>(bx), static_cast<float>(y + 4), static_cast<float>(bw),
                            kit::Plain(*label));
                bx += bw + 4;
            }
        }
        return doc;
    }
}
