#pragma once
#include "client/screen/OverlayScreen.hpp"
#include "client/screen/ScreenHost.hpp"
#include "client/screen/Frame.hpp"
#include "client/screen/screens/ChestScreen.hpp"
#include "client/screen/screens/InventoryView.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include <chrono>
#include <string>
#include <vector>


namespace perlin::screen {
    struct CreativeState {
        int tab = 0;
        bool dollVisible = false;
        float scroll = 0.0f;
        bool dragging = false;
        std::string search, signature;
    };

    struct InventoryScreen : OverlayScreen {
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        InventoryScreen(Id id, Traits traits, const Host& host) : OverlayScreen(id, traits), host(host) {}
        Claim Update(Frame& frame) override;
        void Open(TimePoint now);

        CreativeState creative;
        bool rightDown = false, middleDown = false, previewOpened = false, bookInit = false;
        float mouseX = 0, mouseY = 0;
        int hoverAddr = -1, picked = -1, hover = -1;
        std::string hoverId;
        bool workbench = false, book = false, horse = false, chest = false;
        std::chrono::steady_clock::time_point mountAsk;
        int dragButton = 0;
        std::vector<int> dragSlots;
        int bookPage = 0;
        std::vector<BedrockSession::RecipeEntry> recipeAll;
        std::chrono::steady_clock::time_point recipeAt;
        float panel[4] = {};
        bool jsonPanel = false, closeClicked = false;
        std::string chestScreen, chestProblem;
        ui::ChestUi chestUi;

    private:
        void UpdateInventory(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);
        void UpdateCreative(ui::Hud& hud, BedrockSession* live, int fbW, int fbH);

        const Host& host;
        TimePoint lastWarm{};
        BedrockSession::CreativeCatalog creativeCatalog;
        uint32_t creativeCatalogVersion = ~0u;
    };
}
