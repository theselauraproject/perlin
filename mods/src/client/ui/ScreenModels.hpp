#pragma once
#include "client/ui/JsonScreen.hpp"

#include <string>
#include <vector>

// turns our state into the bindings bedrock's screen controllers would publish, one function per screen;
// only bindings vanilla files (or common server overrides) read are answered; the rest keep defaults
namespace perlin::ui {
    struct PausePlayer {
        std::string name;
        int permission = 1; // 0 visitor, 1 member, 2 operator
        bool local = false;
    };

    struct PauseModel {
        std::string playerName;
        std::string listTitle;
        std::vector<PausePlayer> players;
        bool multiplayer = false;
    };
    ScreenData PauseScreenData(const PauseModel& model);

    // slot bindings for container screens: `container` = open container's items, `inventory` = player's 36 slots
    // (hotbar first); item pictures come from JsonScreens::SetItems
    ScreenData ContainerScreenData(const std::vector<HudItem>& container,
                                   const std::vector<HudItem>& inventory, int selectedHotbar);
}
