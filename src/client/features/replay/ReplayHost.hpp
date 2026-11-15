#pragma once
#include <chrono>
#include <cstdint>
#include <string>

namespace perlin {
    class BedrockSession;
}

namespace perlin::ui {
    class Hud;
}

namespace perlin::replay {
    class Host {
    public:
        virtual ~Host() = default;

        virtual void FramebufferSize(int& w, int& h) = 0;
        virtual void SetCursorCaptured(bool captured) = 0;
        virtual void OpenUrl(const std::string& url) = 0;
        virtual bool Running() const = 0;

        virtual void SetStatusHint(const std::string& text) = 0;

        virtual bool LoadingDone() const = 0;
        virtual void ResetLoading() = 0;

        virtual bool PauseOpen() const = 0;
        virtual void UpdatePause(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) = 0;
        virtual void OpenPauseOptions(std::chrono::steady_clock::time_point now) = 0;
        virtual void ClosePause() = 0;

        virtual void DeactivateTitle() = 0;
        virtual void ReportOpenFailure(const std::string& message) = 0;
        virtual void ReturnToReplayList(ui::Hud& hud) = 0;
    };
}
