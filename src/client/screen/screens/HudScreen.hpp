#pragma once
#include "client/screen/ScreenHost.hpp"
#include "client/screen/screens/FormScreen.hpp"
#include "client/screen/screens/InventoryScreen.hpp"
#include "client/screen/screens/PauseScreen.hpp"
#include "client/systems/camera/Camera.hpp"
#include "client/systems/entity/EntityRenderer.hpp"
#include "client/systems/renderer/LevelRenderer.hpp"
#include "client/screen/ScreenManager.hpp"
#include "client/screen/screens/CreativeScreen.hpp"
#include "client/systems/replay/ReplayController.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/renderer/FrameContext.hpp"
#include "client/systems/renderer/FrameLoop.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "client/ui/Hud.hpp"
#include "client/ui/HudAnimation.hpp"

namespace perlin::screen {
    class HudScreen {
    public:
        struct Deps {
            ui::UiAssets& uiAssets;
            const bool& headUnderwater;
            ui::HudAnimation& hudAnim;
            Options& options;
            InputManager& inputManager;
            std::unique_ptr<ui::Hud>& hud;
            ui::HudState& hudState;
            Camera& camera;
            std::unique_ptr<FrameLoop>& loop;
            float& hudPartial;
            int& previewSlot;
            ui::ChatLog& chat;
            screen::FormScreen& form;
            screen::InventoryScreen& inventory;
            screen::PauseScreen& pause;
            std::unique_ptr<BedrockSession>& session;
            std::function<void(ui::HudState&)> applyModSlotOverlays;
            std::function<void(ui::HudState&)> applyModHud;
            std::function<bool()> worldCovered;
            std::function<bool()> replayHidesHud;
            std::unique_ptr<RenderContext>& renderer;
            std::unique_ptr<LevelRenderer>& levelRenderer;
            entity::EntityRenderer& entityRenderer;
            std::optional<entity::Entity>& dollSelf;
            screen::Manager& screens;
            replay::ReplayController& replayCtl;
            std::function<void(int, int)> drawModTint;
            std::function<void(int, int, bool)> drawModGfx;
        };

        explicit HudScreen(Deps deps);
        HudScreen(const HudScreen&) = delete;
        HudScreen& operator=(const HudScreen&) = delete;

        void TickHud();
        void EnsureUi(FrameContext& f);
        void Present(FrameContext& f, int fbW, int fbH);
        void FillFrameHud(FrameContext& f);


    private:
        void FillHudChat(ui::HudState& s);
        void FillHudSidebar(ui::HudState& s, BedrockSession* live, bool preview);
        void FillPreviewHud(ui::HudState& s);
        void FillHudVitals(ui::HudState& s, BedrockSession& live);
        void FillHudScreenAnimation(ui::HudState& s, BedrockSession& live);
        void FillHudInventory(ui::HudState& s, BedrockSession& live);
        void FillHudLocator(ui::HudState& s, BedrockSession& live);
        void FillHudTabList(ui::HudState& s, BedrockSession& live);
        void FillHudHeldName(ui::HudState& s);
        void FillHudTitleText(ui::HudState& s, BedrockSession& live);
        void FillHudState(ui::HudState& s, BedrockSession* live, bool preview);

        ui::UiAssets& uiAssets;
        const bool& headUnderwater;
        ui::HudAnimation& hudAnim;
        Options& options;
        InputManager& inputManager;
        std::unique_ptr<ui::Hud>& hud;
        ui::HudState& hudState;
        Camera& camera;
        std::unique_ptr<FrameLoop>& loop;
        float& hudPartial;
        int& previewSlot;
        ui::ChatLog& chat;
        screen::FormScreen& form;
        screen::InventoryScreen& inventory;
        screen::PauseScreen& pause;
        std::unique_ptr<BedrockSession>& session;
        std::function<void(ui::HudState&)> applyModSlotOverlays;
        std::function<void(ui::HudState&)> applyModHud;
        std::function<bool()> worldCovered;
        std::function<bool()> replayHidesHud;
        std::unique_ptr<RenderContext>& renderer;
        std::unique_ptr<LevelRenderer>& levelRenderer;
        entity::EntityRenderer& entityRenderer;
        int lastGuiW = 0;
        std::optional<entity::Entity>& dollSelf;
        screen::Manager& screens;
        replay::ReplayController& replayCtl;
        std::function<void(int, int)> drawModTint;
        std::function<void(int, int, bool)> drawModGfx;
        double hudRetry = 0;
        bool hudBootstrap = false;

        BedrockSession::ScreenAnimation screenAnimation;
        void DrawPaperDoll(FrameContext& f, int fbW, int fbH);
        bool PaperDollActive(BedrockSession& live);
        struct {
            std::chrono::steady_clock::time_point last;
            double remaining = 0.0;
            uint64_t armor = 0;
            bool started = false;
            uint32_t hurtSeen = 0, hurtShown = 0;
            std::chrono::steady_clock::time_point hurtAt;
        } paperDollTimer;
        std::unordered_set<std::string> logged;
    };
}
