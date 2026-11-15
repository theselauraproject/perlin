#pragma once
#include "client/screen/screens/AssetDownloadScreen.hpp"
#include "client/screen/screens/TitleScreen.hpp"
#include "client/screen/ScreenManager.hpp"
#include "client/screen/screens/ConnectionPorts.hpp"
#include "client/systems/config/Options.hpp"
#include "client/systems/input/Input.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/ui/PanoramaBackdrop.hpp"

namespace perlin::screen {
    class AssetDownloadController {
    public:
        using Clock = std::chrono::steady_clock;

        struct Deps {
            AssetDownloadScreen& asset;
            TitleScreen& titleScreen;
            Manager& screens;
            Input& input;
            Options& options;
            ConnectionPorts& ports;
        };

        explicit AssetDownloadController(Deps deps);
        AssetDownloadController(const AssetDownloadController&) = delete;
        AssetDownloadController& operator=(const AssetDownloadController&) = delete;

        bool Update(ui::Hud& hud, int fbW, int fbH);

    private:
        AssetDownloadScreen& asset;
        TitleScreen& titleScreen;
        Manager& screens;
        Input& input;
        Options& options;
        ConnectionPorts& ports;
    };
}
