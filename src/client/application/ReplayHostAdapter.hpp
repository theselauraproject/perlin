#pragma once
#include "client/features/replay/ReplayHost.hpp"

#include <functional>

class AppPlatform;

namespace perlin::screen {
    class Manager;
    struct PauseScreen;
    struct LoadingScreen;
    struct TitleScreen;
}

namespace perlin::ui {
    struct TitleModel;
}

namespace perlin {
    class ReplayHostAdapter final : public replay::Host {
    public:
        struct Deps {
            AppPlatform*& platform;
            screen::Manager& screens;
            screen::PauseScreen& pause;
            screen::LoadingScreen& loading;
            screen::TitleScreen& titleScreen;
            ui::TitleModel& title;
            std::function<void(const std::string&)> setStatusHint;
            std::function<void(ui::Hud&)> leaveToServerList;
        };

        explicit ReplayHostAdapter(Deps d) : deps(std::move(d)) {}

        void FramebufferSize(int& w, int& h) override;
        void SetCursorCaptured(bool captured) override;
        void OpenUrl(const std::string& url) override;
        bool Running() const override;
        void SetStatusHint(const std::string& text) override;
        bool LoadingDone() const override;
        void ResetLoading() override;
        bool PauseOpen() const override;
        void UpdatePause(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) override;
        void OpenPauseOptions(std::chrono::steady_clock::time_point now) override;
        void ClosePause() override;
        void DeactivateTitle() override;
        void ReportOpenFailure(const std::string& message) override;
        void ReturnToReplayList(ui::Hud& hud) override;

    private:
        Deps deps;
    };
}
