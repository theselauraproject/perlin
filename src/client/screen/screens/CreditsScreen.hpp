#pragma once
#include "client/ui/Scene.hpp"
#include "client/screen/OverlayScreen.hpp"
#include "client/screen/ScreenHost.hpp"
#include "client/screen/Frame.hpp"
#include <chrono>
#include <string>
#include <vector>
#include <string>

namespace perlin::ui {

    SceneDoc BuildCreditsScreen(int guiW, int guiH, int scroll, const std::string& player);
    int CreditsHeight();
}

namespace perlin::screen {
    struct CreditsScreen : OverlayScreen {
        CreditsScreen(Id id, Traits traits, const Host& host) : OverlayScreen(id, traits), host(host) {}
        Claim Update(Frame& frame) override;

        double scroll = 0;
        std::chrono::steady_clock::time_point last;

    private:
        void UpdateCredits(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);
        const Host& host;
    };
}
