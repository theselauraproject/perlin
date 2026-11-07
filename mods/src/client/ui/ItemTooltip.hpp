#pragma once

#include "client/ui/HudState.hpp"

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
    // extra lines from mods, appended under every item tooltip
    void SetTooltipExtender(std::function<std::vector<std::string>(const HudItem&)> extender);
    std::vector<TooltipLine> BuildItemTooltip(const HudItem& item, bool advanced,
                                              const std::string& nameOverride = {});
    const char* TooltipStyles();
    void PlaceTooltip(int mouseX, int mouseY, int guiW, int guiH, int& x, int& y);
    std::string TooltipRml(const std::vector<TooltipLine>& lines, int mouseX, int mouseY, int guiW, int guiH);
}
