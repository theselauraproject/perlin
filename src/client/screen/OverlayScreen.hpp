#pragma once
#include "client/screen/Frame.hpp"
#include "client/screen/Screen.hpp"

#include <chrono>
#include <functional>
#include <string>

namespace perlin::screen {

    class CallbackScreen : public Screen {
    public:
        using UpdateFn = std::function<Claim(Frame&)>;
        using OpenFn = std::function<bool()>;

        using Screen::Screen;

        bool IsOpen() const override { return isOpen ? isOpen() : Screen::IsOpen(); }
        Claim Update(Frame& frame) override { return update ? update(frame) : Claim::None; }

        UpdateFn update;
        OpenFn isOpen;
    };

    class OverlayScreen : public CallbackScreen {
    public:
        using CallbackScreen::CallbackScreen;

        bool leftDown = false;
        std::string signature;
        std::chrono::steady_clock::time_point openedAt;
    };

}
