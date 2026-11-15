#pragma once
#include "client/screen/OverlayScreen.hpp"
#include "client/screen/ScreenHost.hpp"
#include "client/screen/Frame.hpp"
#include "client/ui/Scene.hpp"
#include <chrono>
#include <string>
#include <vector>
#include <string>

namespace perlin::ui {
    inline constexpr const char* kBedLeaveId = "leave";
    SceneDoc BuildBedScreen(const std::string& label, int guiW, int guiH);
}

namespace perlin::screen {
    struct BedScreen : OverlayScreen {
        BedScreen(Id id, Traits traits, const Host& host) : OverlayScreen(id, traits), host(host) {}
        Claim Update(Frame& frame) override;


    private:
        void UpdateBed(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);
        const Host& host;
    };
}
