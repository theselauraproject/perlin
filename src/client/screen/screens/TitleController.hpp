#pragma once
#include "client/screen/ScreenManager.hpp"
#include "client/screen/screens/ReplayLibrary.hpp"
#include "client/screen/screens/WorldLibrary.hpp"
#include "client/screen/screens/TitlePorts.hpp"
#include "client/screen/screens/TitleScreen.hpp"
#include "client/systems/config/Options.hpp"
#include "client/systems/input/Input.hpp"
#include "client/systems/input/InputManager.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/ui/JsonScreen.hpp"
#include "client/ui/PanoramaBackdrop.hpp"
#include "client/ui/TitleModel.hpp"

#include <chrono>
#include <string>

namespace perlin::screen {
    class TitleController {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        struct Deps {
            TitleScreen& titleScreen;
            ui::TitleModel& title;
            Options& options;
            Input& input;
            InputManager& inputManager;
            ui::PanoramaBackdrop& panorama;
            Manager& screens;
            ui::ChatLog& chat;
            ReplayLibrary& replayLibrary;
            WorldLibrary& worldLibrary;
            ui::UiAssets& uiAssets;
            ui::JsonScreens& jsonScreens;
            TitlePorts& ports;
        };

        explicit TitleController(Deps deps);
        TitleController(const TitleController&) = delete;
        TitleController& operator=(const TitleController&) = delete;

        bool Update(ui::Hud& hud, int fbW, int fbH);
        void HideOverlay(ui::Hud& hud);
        void Activate(const std::string& element, TimePoint now);

    private:
        void Show(TimePoint now);
        void ApplyTitlePreview(const std::string& page);
        void TitleCommit();
        void TitleJoinRow(int row);
        void TitleBack();
        void TitleAddOffline();
        void TitleEditServer(bool adding);
        void HandleTitleTyping();
        void ScrollTitleLists(int guiH);
        void SyncTitleAccounts();
        void SyncTitleServers();
        void RebuildSavedView();
        void LoadTitleLogo(TimePoint now);
        std::string TitleSignature(int guiW, int guiH);
        void RefreshTitleOverlay(ui::Hud& hud, int guiW, int guiH);
        void OpenTitleOptions(int view);
        void OpenServerTab(ui::ServerTab tab);
        void RefreshSavedServers();
        void ClickServerRow(int row, TimePoint now);
        void HandleTitleClick(const std::string& hover, TimePoint now);

        TitleScreen& titleScreen;
        ui::TitleModel& title;
        Options& options;
        Input& input;
        InputManager& inputManager;
        ui::PanoramaBackdrop& panorama;
        Manager& screens;
        ui::ChatLog& chat;
        ReplayLibrary& replayLibrary;
        WorldLibrary& worldLibrary;
        ui::UiAssets& uiAssets;
        ui::JsonScreens& jsonScreens;
        TitlePorts& ports;
        TimePoint logoRetryAt;
        bool logoBedrock = false;
        std::string dollKeyLast;
    };
}
