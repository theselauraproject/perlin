#include "client/application/ModWindow.hpp"
#include "client/platform/AppPlatform.hpp"

namespace perlin {
    bool ModWindow::Available() const { return platform != nullptr; }

    void ModWindow::Quit() {
        if (platform)
            platform->isRunning = false;
    }

    void ModWindow::SetClipboard(const std::string& text) {
        if (platform)
            platform->SetClipboard(text);
    }

    std::string ModWindow::GetClipboard() { return platform ? platform->GetClipboard() : std::string{}; }

    void ModWindow::OpenUrl(const std::string& url) {
        if (platform)
            platform->OpenUrl(url);
    }

    void ModWindow::SetTitle(const std::string& title) {
        if (platform)
            platform->SetTitle(title);
    }

    void ModWindow::ReleaseCursor() {
        if (platform)
            platform->SetCursorCaptured(false);
    }

    bool ModWindow::Focused() { return platform ? platform->WindowFocused() : true; }

    bool ModWindow::Minimized() { return platform ? platform->WindowMinimized() : false; }
}
