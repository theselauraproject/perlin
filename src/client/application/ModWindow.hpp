#pragma once
#include "client/features/mods/ModHost.hpp"

class AppPlatform;

namespace perlin {
    class ModWindow final : public ModHost::Window {
    public:
        explicit ModWindow(AppPlatform*& platform) : platform(platform) {}

        bool Available() const override;
        void Quit() override;
        void SetClipboard(const std::string& text) override;
        std::string GetClipboard() override;
        void OpenUrl(const std::string& url) override;
        void SetTitle(const std::string& title) override;
        void ReleaseCursor() override;
        bool Focused() override;
        bool Minimized() override;

    private:
        AppPlatform*& platform;
    };
}
