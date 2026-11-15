#pragma once
#include "client/screen/OverlayScreen.hpp"
#include <chrono>
#include <string>
#include <vector>
#include "client/ui/Scene.hpp"
#include "client/ui/UiTemplate.hpp"
#include <string>
#include <vector>

namespace perlin::ui {
    struct LoadingScreenText {
        std::string title;
        std::string message;
        std::string detail;
        float progress = -1.0f;
        std::string cancel;
        std::string warning;

        bool panorama = false;
    };
    inline constexpr const char* kLoadingCancelId = "cancel";

    struct DownloadRow {
        std::string label;
        std::string status;
        float progress = -1.0f;
        bool failed = false;
    };
    struct DownloadScreenText {
        std::string title, subtitle;
        std::vector<DownloadRow> rows;
        std::string retry, skip;
    };
    inline constexpr const char* kDownloadRetryId = "dl_retry";
    inline constexpr const char* kDownloadSkipId = "dl_skip";
    SceneDoc BuildDownloadScreen(const DownloadScreenText& text, int guiW, int guiH, double time);

    std::string DefaultLoadingScreenTemplate();
    SceneDoc BuildLoadingScreen(const std::string& templateText, const LoadingScreenText& text, int guiW,
                                int guiH);
}

namespace perlin::screen {
    struct LoadingScreen : OverlayScreen {
        using OverlayScreen::OverlayScreen;
        bool textMode = false, done = false, readyArmed = false, modHidden = false;
        std::chrono::steady_clock::time_point readyAt, startAt;
        int chunkBase = 0;
        ui::TemplateSlot tmpl;
    };
}
