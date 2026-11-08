#pragma once
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

    std::string DefaultDisconnectScreenTemplate();
    std::string BuildDisconnectScreenRml(const std::string& templateText, const DisconnectScreenText& text,
                                         int guiW, int guiH);
}
