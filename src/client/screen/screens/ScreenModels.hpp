#pragma once
#include "client/ui/JsonScreen.hpp"

#include <string>
#include <vector>

namespace perlin::ui {
    struct PausePlayer {
        std::string name;
        int permission = 1;
        bool local = false;
    };

    struct PauseModel {
        std::string playerName;
        std::string listTitle;
        std::vector<PausePlayer> players;
        bool multiplayer = false;
    };
    ScreenData PauseScreenData(const PauseModel& model);

    ScreenData ContainerScreenData(const std::vector<HudItem>& container,
                                   const std::vector<HudItem>& inventory, int selectedHotbar);
}
