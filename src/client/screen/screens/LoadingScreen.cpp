#include "client/screen/screens/LoadingScreen.hpp"
#include "util/Resources.hpp"

#include "client/ui/SceneEmit.hpp"
#include "client/systems/text/FormatCodes.hpp"
#include "client/ui/UiTemplate.hpp"

#include <algorithm>
#include <cmath>

namespace perlin::ui {
    std::string DefaultLoadingScreenTemplate() {
        return ui::ReadMarkup("ui/screens/loading_screen.rml");
    }

    SceneDoc BuildLoadingScreen(const std::string& templateText, const LoadingScreenText& t, int guiW,
                                int guiH) {
        const int msgY = guiH / 2 - 24;
        const bool bar = t.progress >= 0.0f;
        const int barW = std::min(182, guiW - 40);
        const int barY = msgY + 14;
        const int detailY = bar ? barY + 10 : msgY + 14;
        const int btnY = std::min(guiH - 24, detailY + 36 + (t.warning.empty() ? 0 : 32));
        const int warnW = std::min(guiW - 20, 330);
        SceneDoc doc;
        doc.rml = templateText;
        auto& vars = doc.scene.vars;
        vars["w"] = std::to_string(guiW);
        vars["h"] = std::to_string(guiH);
        vars["tile_scale"] = "2";
        vars["msg_y"] = std::to_string(msgY);
        vars["detail_x"] = std::to_string(std::max(10, (guiW - 300) / 2));
        vars["detail_y"] = std::to_string(detailY);
        vars["detail_w"] = std::to_string(std::min(300, guiW - 20));
        vars["btn_x"] = std::to_string(guiW / 2 - 102);
        vars["btn_y"] = std::to_string(btnY);
        vars["title_y"] = std::to_string(msgY - 16);
        vars["bar_x"] = std::to_string((guiW - barW) / 2);
        vars["bar_y"] = std::to_string(barY);
        vars["bar_w"] = std::to_string(barW);
        vars["fill_w"] = std::to_string(
            static_cast<int>(std::clamp(t.progress, 0.0f, 1.0f) * static_cast<float>(barW - 2)));
        vars["cancel"] = t.cancel.empty() ? std::string("Cancel") : t.cancel;
        vars["warning"] = t.warning;
        vars["warn_x"] = std::to_string((guiW - warnW) / 2);
        vars["warn_y"] = std::to_string(btnY - 30);
        vars["warn_w"] = std::to_string(warnW);
        doc.scene.flags["panorama"] = t.panorama;
        doc.scene.flags["bar"] = bar;
        doc.scene.flags["has_warning"] = !t.warning.empty();
        doc.scene.texts["title_runs"] = ParseFormatRuns(t.title, "#ffffff");
        doc.scene.texts["message_runs"] = ParseFormatRuns(t.message, "#c6c6c6");
        doc.scene.texts["detail_runs"] = ParseFormatRuns(t.detail, "#8a8a8a");
        return doc;
    }

    namespace {
        const ScreenTemplateDefault kDownloadDefault("ui/screens/download_screen.rml");
    }

    SceneDoc BuildDownloadScreen(const DownloadScreenText& t, int guiW, int guiH, double time) {
        const int barW = std::min(guiW - 40, 240);
        const int x = (guiW - barW) / 2;
        const int rowsH = static_cast<int>(t.rows.size()) * 42;
        const bool buttons = !t.retry.empty() || !t.skip.empty();
        const int contentH = 28 + rowsH + (buttons ? 30 : 0);
        int y = std::max(10, (guiH - contentH) / 2);
        SceneDoc doc;
        doc.rml = ScreenTemplate("ui/screens/download_screen.rml");
        auto& vars = doc.scene.vars;
        vars["w"] = std::to_string(guiW);
        vars["h"] = std::to_string(guiH);
        vars["title"] = t.title;
        vars["subtitle"] = t.subtitle;
        vars["title_y"] = std::to_string(y);
        vars["subtitle_y"] = std::to_string(y + 12);
        y += 30;
        auto& rows = doc.scene.rows["rows"];
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
            rows.push_back({{"x", std::to_string(x)},
                            {"w", std::to_string(barW)},
                            {"label_y", std::to_string(y)},
                            {"status_y", std::to_string(y + 20)},
                            {"bar_y", std::to_string(by)},
                            {"fill_x", std::to_string(fillX)},
                            {"fill_y", std::to_string(by + 1)},
                            {"fill_w", std::to_string(fillW)},
                            {"label", r.label},
                            {"status", r.status},
                            {"failed", SceneFlag(r.failed)}});
            y += 42;
        }
        auto& buttonRows = doc.scene.rows["buttons"];
        if (buttons) {
            const bool both = !t.retry.empty() && !t.skip.empty();
            const int bw = both ? 98 : 150;
            int bx = both ? guiW / 2 - 100 : guiW / 2 - bw / 2;
            for (const auto& [id, label] :
                 {std::pair<const char*, const std::string*>{kDownloadRetryId, &t.retry},
                  {kDownloadSkipId, &t.skip}}) {
                if (label->empty())
                    continue;
                buttonRows.push_back({{"id", id},
                                      {"x", std::to_string(bx)},
                                      {"y", std::to_string(y + 4)},
                                      {"w", std::to_string(bw)},
                                      {"face_w", std::to_string(bw - 2)},
                                      {"label", *label}});
                bx += bw + 4;
            }
        }
        return doc;
    }
}
