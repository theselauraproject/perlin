#include "client/application/ReplayHostAdapter.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/screen/ScreenManager.hpp"
#include "client/screen/screens/LoadingScreen.hpp"
#include "client/screen/screens/PauseScreen.hpp"
#include "client/screen/screens/TitleScreen.hpp"

namespace perlin {
    void ReplayHostAdapter::FramebufferSize(int& w, int& h) {
        deps.platform->GetFramebufferSize(w, h);
    }

    void ReplayHostAdapter::SetCursorCaptured(bool captured) {
        deps.platform->SetCursorCaptured(captured);
    }

    void ReplayHostAdapter::OpenUrl(const std::string& url) {
        deps.platform->OpenUrl(url);
    }

    bool ReplayHostAdapter::Running() const {
        return deps.platform->isRunning;
    }

    void ReplayHostAdapter::SetStatusHint(const std::string& text) {
        deps.setStatusHint(text);
    }

    bool ReplayHostAdapter::LoadingDone() const {
        return deps.loading.done;
    }

    void ReplayHostAdapter::ResetLoading() {
        deps.loading.done = false;
        deps.loading.startAt = {};
        deps.loading.readyArmed = false;
    }

    bool ReplayHostAdapter::PauseOpen() const {
        return deps.pause.IsOpen();
    }

    void ReplayHostAdapter::UpdatePause(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        deps.pause.UpdatePauseScreen(hud, live, fbW, fbH);
    }

    void ReplayHostAdapter::OpenPauseOptions(std::chrono::steady_clock::time_point now) {
        screen::PauseScreen& pause = deps.pause;
        deps.screens.Open(pause);
        pause.view = 1;
        pause.optionsUi.page = 0;
        pause.optionsUi.sub = 0;
        pause.openedAt = now;
        pause.signature.clear();
        pause.optionsUi.signature.clear();
        pause.leftDown = true;
    }

    void ReplayHostAdapter::ClosePause() {
        deps.screens.Close(deps.pause);
    }

    void ReplayHostAdapter::DeactivateTitle() {
        deps.titleScreen.active = false;
    }

    void ReplayHostAdapter::ReportOpenFailure(const std::string& message) {
        deps.title.replayMessage = message;
        deps.titleScreen.signature.clear();
    }

    void ReplayHostAdapter::ReturnToReplayList(ui::Hud& hud) {
        screen::TitleScreen& title = deps.titleScreen;
        title.returnToServers = false;
        title.returnToReplays = true;
        deps.leaveToServerList(hud);
        title.returnToServers = false;
        title.returnToReplays = true;
    }
}
