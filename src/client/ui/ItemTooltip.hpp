#pragma once

#include "client/systems/model/HudState.hpp"
#include "client/ui/Scene.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace perlin::ui {
    struct TooltipLine {
        std::string text;
        uint32_t color = 0xFFFFFF;
        bool italic = false;
    };

    int ItemRarity(const HudItem& item);

    std::string PrettyItemName(const std::string& id);

    void SetTooltipExtender(std::function<std::vector<std::string>(const HudItem&)> extender);
    std::vector<TooltipLine> BuildItemTooltip(const HudItem& item, bool advanced,
                                              const std::string& nameOverride = {});
    void PlaceTooltip(int mouseX, int mouseY, int guiW, int guiH, int& x, int& y);
    void AddTooltip(SceneBuilder& builder, const std::vector<TooltipLine>& lines, int mouseX, int mouseY, int guiW,
                    int guiH);
}
