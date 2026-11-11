#pragma once
#include "client/ui/HudState.hpp"

#include <array>
#include <string>
#include <vector>

namespace perlin::ui {
    struct CreativeView {
        int tab = 0;
        std::array<std::string, 6> tabTitles;
        std::array<HudItem, 6> tabIcons;
        std::array<bool, 6> tabPresent{};
        std::array<HudItem, 45> grid;
        std::array<std::string, 45> gridNames;
        float scroll = 0.0f;
        bool scrollable = false;
        std::string search;
        std::array<HudItem, 36> slots;
        std::array<HudItem, 4> armor;
        HudItem offhand, cursor;
        std::string hovered;
        float mouseX = 0, mouseY = 0;
        std::string title;
        std::vector<HudEffect> effects;
        bool advancedTooltips = false;
    };

    inline constexpr int kCreativeW = 195;
    inline constexpr int kCreativeH = 136;
    inline constexpr int kCreativeTabs = 6;
    inline constexpr int kCreativeSearchTab = 4;
    inline constexpr int kCreativeSurvivalTab = 5;
    void CreativeTabPos(int tab, int& x, int& y, bool& top);
    void CreativeSlotPos(const std::string& id, bool survival, int& x, int& y);
    std::string DefaultCreativeScreenTemplate();
    std::string BuildCreativeScreenRml(const std::string& templateText, const CreativeView& view, int guiW,
                                       int guiH);
}
