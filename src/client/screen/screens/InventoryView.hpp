#pragma once
#include "client/ui/Scene.hpp"
#include <chrono>
#include <string>
#include <vector>
#include "client/systems/model/HudState.hpp"
#include <array>
#include <string>
#include <vector>
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/ui/UiTemplate.hpp"

namespace perlin::ui {
    struct RecipeCard {
        HudItem out;
        bool craftable = false;
        int index = 0;
    };

    struct InventoryView {
        std::array<HudItem, 9> work;
        bool workbench = false;
        bool bookOpen = false;
        std::vector<RecipeCard> recipes;
        int bookPage = 0, bookPages = 1;
        std::string hoveredId;
        std::vector<HudEffect> effects;
        std::array<HudItem, 36> slots;
        std::array<HudItem, 4> armor;
        std::array<HudItem, 4> craft;
        HudItem offhand, output, cursor;
        int selectedHotbar = 0;
        int hovered = -1;
        float mouseX = 0, mouseY = 0;
        std::string title;
        bool horse = false;
        int horseChest = 0;
        std::array<HudItem, 17> horseItems;
        int chestRows = 0;
        int furnace = -1;
        float cook = 0, burn = 0;
        std::array<HudItem, 54> chestItems;
        bool advancedTooltips = false;
    };

    inline constexpr int kInvPanelW = 176;
    inline constexpr int kInvPanelH = 166;
    inline constexpr int kInvAddrArmor = 36, kInvAddrOffhand = 40, kInvAddrCraft = 41, kInvAddrOutput = 45,
                         kInvAddrWork = 47, kInvAddrHorse = 56, kInvAddrChest = 73;
    void InventorySlotPos(int addr, int& x, int& y, bool workbench = false, int chestRows = 0);
    int InventoryPanelH(const InventoryView& view);
    void AddEffectsPanel(SceneBuilder& builder, const std::vector<HudEffect>& effects, int x, int y, int guiW);
    void BeginSlot(SceneBuilder& builder, const std::string& id, int x, int y);
    void AddSlotItem(SceneBuilder& builder, const HudItem& item, int countX, int countY);
    void EndSlot(SceneBuilder& builder);
    int InventoryPanelX(const InventoryView& view, int guiW);
    inline constexpr int kBookCols = 5, kBookRows = 4;

    std::string DefaultInventoryScreenTemplate();
    SceneDoc BuildInventoryScreen(const std::string& templateText, const InventoryView& view, int guiW, int guiH);
    void AddInventoryCursor(SceneBuilder& builder, const InventoryView& view);
    void AddInventoryTooltip(SceneBuilder& builder, const InventoryView& view, int guiW, int guiH);
    int InventoryAddrFromId(const std::string& id);
}
