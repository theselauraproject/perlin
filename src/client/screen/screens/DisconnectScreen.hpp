#pragma once
#include "client/screen/OverlayScreen.hpp"
#include <chrono>
#include <string>
#include <vector>
#include "client/ui/Scene.hpp"
#include <string>

namespace perlin::ui {
    struct DisconnectScreenText {
        std::string title, reason, code, rejoin, quit;
        bool rejoinEnabled = true;

        std::string extra;
        bool panorama = false;
    };
    inline constexpr const char* kDisconnectRejoinId = "rejoin";
    inline constexpr const char* kDisconnectExtraId = "dontask";
    inline constexpr const char* kDisconnectQuitId = "quit";

    bool LooksLikeBan(const std::string& text);

    SceneDoc BuildDisconnectScreen(const DisconnectScreenText& text, int guiW,
                                   int guiH);
}

namespace perlin::screen {
    struct DisconnectScreen : OverlayScreen {
        using OverlayScreen::OverlayScreen;
        bool textMode = false;
    };
}
