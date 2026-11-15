#pragma once
#include "client/screen/screens/DisconnectScreen.hpp"
#include "client/screen/ScreenManager.hpp"
#include "client/screen/screens/ConnectionPorts.hpp"
#include "client/systems/config/Options.hpp"
#include "client/systems/input/Input.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/ui/PanoramaBackdrop.hpp"

namespace perlin::screen {
    class DisconnectController {
    public:
        using Clock = std::chrono::steady_clock;

        struct Deps {
            DisconnectScreen& disconnect;
            Manager& screens;
            Input& input;
            Options& options;
            ui::ChatLog& chat;
            ui::PanoramaBackdrop& panorama;
            ConnectionPorts& ports;
        };

        explicit DisconnectController(Deps deps);
        DisconnectController(const DisconnectController&) = delete;
        DisconnectController& operator=(const DisconnectController&) = delete;

        bool Update(ui::Hud& hud, int fbW, int fbH);

    private:
        DisconnectScreen& disconnect;
        Manager& screens;
        Input& input;
        Options& options;
        ui::ChatLog& chat;
        ui::PanoramaBackdrop& panorama;
        ConnectionPorts& ports;
    };
}
