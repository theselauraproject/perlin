#pragma once

namespace perlin {
    class BedrockSession;
    namespace ui {
        class Hud;
    }
}

namespace perlin::screen {

    struct Frame {
        ui::Hud& hud;
        BedrockSession* live = nullptr;
        int fbW = 0, fbH = 0;
    };

}
