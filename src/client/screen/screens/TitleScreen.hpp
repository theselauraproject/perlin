#pragma once
#include "client/screen/OverlayScreen.hpp"
#include "client/ui/Scene.hpp"
#include "client/ui/TitleModel.hpp"
#include <chrono>

namespace perlin::ui {
    int TitleRowHeight();
    int TitleListTop();
    int TitleListBottom(int guiH);
    int TitleVisibleRows(int guiH);
    int TitleAccountRows(int guiH);

    SceneDoc BuildTitleScene(const TitleModel& model, int guiW, int guiH);
}

namespace perlin::screen {
    struct TitleScreen : OverlayScreen {
        using OverlayScreen::OverlayScreen;
        bool active = false;
        bool options = false;
        bool returnToServers = false, returnToReplays = false;
        int openView = 1;
        int clickRow = -1;
        std::chrono::steady_clock::time_point clickAt;
        ui::TitlePage lastPage = ui::TitlePage::Main;
    };
}
