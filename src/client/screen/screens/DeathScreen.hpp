#pragma once
#include "client/screen/OverlayScreen.hpp"
#include "client/screen/ScreenHost.hpp"
#include "client/screen/Frame.hpp"
#include "client/ui/Scene.hpp"
#include <chrono>
#include <string>
#include <vector>
#include <cstdint>
#include <string>

namespace perlin::ui {
    struct DeathScreenText {
        std::string title, cause, respawn, titleScreen;
        bool buttonsEnabled = true;
    };

    inline constexpr const char* kDeathRespawnId = "respawn";
    inline constexpr const char* kDeathTitleId = "title";

    SceneDoc BuildDeathScreen(const DeathScreenText& text, int guiW, int guiH);
}

namespace perlin::screen {
    struct DeathScreen : OverlayScreen {
        DeathScreen(Id id, Traits traits, const Host& host) : OverlayScreen(id, traits), host(host) {}
        Claim Update(Frame& frame) override;

        bool respawnSent = false, textMode = false, hoverLogged = false;
        std::chrono::steady_clock::time_point sentAt;

    private:
        void UpdateDeath(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);
        const Host& host;
    };
}
