#pragma once
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
    std::string BuildDownloadScreenRml(const DownloadScreenText& text, int guiW, int guiH, double time);

    std::string MenuBackdropRml(bool panorama);

    std::string DefaultLoadingScreenTemplate();
    std::string BuildLoadingScreenRml(const std::string& templateText, const LoadingScreenText& text,
                                      int guiW, int guiH, double time);
}
