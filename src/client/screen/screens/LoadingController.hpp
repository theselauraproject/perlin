#pragma once
#include "client/screen/screens/LoadingScreen.hpp"
#include "client/screen/screens/TitleScreen.hpp"
#include "client/systems/multiplayer/PackDownloader.hpp"
#include "client/screen/ScreenManager.hpp"
#include "client/screen/screens/ConnectionPorts.hpp"
#include "client/systems/config/Options.hpp"
#include "client/systems/input/Input.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/ui/PanoramaBackdrop.hpp"

namespace perlin::screen {
    class LoadingController {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        struct Deps {
            LoadingScreen& loading;
            TitleScreen& titleScreen;
            Manager& screens;
            Input& input;
            Options& options;
            ui::ChatLog& chat;
            ui::PanoramaBackdrop& panorama;
            ConnectionPorts& ports;
        };

        explicit LoadingController(Deps deps);
        LoadingController(const LoadingController&) = delete;
        LoadingController& operator=(const LoadingController&) = delete;

        bool Update(ui::Hud& hud, int fbW, int fbH);

    private:
        bool PollLeaseJoin(ui::Hud& hud);
        void ComposePackProgressText(ui::LoadingScreenText& t, const PackDownloader::Progress& progress,
                                     TimePoint now);
        bool EvaluateWorldLoad(ui::LoadingScreenText& t, TimePoint now);
        void Hide(ui::Hud& hud);
        bool Present(ui::Hud& hud, int fbW, int fbH, ui::LoadingScreenText& t, TimePoint now, bool preview);

        LoadingScreen& loading;
        TitleScreen& titleScreen;
        Manager& screens;
        Input& input;
        Options& options;
        ui::ChatLog& chat;
        ui::PanoramaBackdrop& panorama;
        ConnectionPorts& ports;
        uint64_t rateBytes = 0;
        TimePoint sampleAt;
        double rate = 0.0;
        bool rateInit = false;
    };
}
