#pragma once
#include "client/features/mods/ModHost.hpp"
#include "client/features/replay/Studio.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/screen/ScreenHost.hpp"
#include "client/systems/camera/Camera.hpp"
#include "client/systems/entity/EntityRenderer.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/player/LocalPlayer.hpp"
#include "client/systems/renderer/FrameContext.hpp"
#include "client/systems/renderer/FrameGenPacer.hpp"
#include "client/systems/renderer/FrameLoop.hpp"
#include "client/systems/renderer/LevelRenderer.hpp"
#include "client/systems/replay/ReplayController.hpp"
#include "client/systems/sprites/UiAssets.hpp"

namespace perlin::worldview {
    class LevelPass {
    public:
        struct Deps {
            ui::UiAssets& uiAssets;
            ModHost& modHost;
            replay::Studio& replayStudio;
            Options& options;
            std::unique_ptr<RenderContext>& renderer;
            std::unique_ptr<LevelRenderer>& levelRenderer;
            ui::HudState& hudState;
            HandRenderer& handRenderer;
            entity::EntityRenderer& entityRenderer;
            FrameGenPacer& fgPacer;
            Camera& camera;
            LocalPlayer& player;
            bool& cameraDetached;
            std::unique_ptr<FrameLoop>& loop;
            bool& previewEntitiesSpawned;
            replay::ReplayController& replayCtl;
            std::unique_ptr<BedrockSession>& session;
            std::function<bool()> deathOpen;
            std::function<void()> requestQuit;
        };

        explicit LevelPass(Deps deps);
        LevelPass(const LevelPass&) = delete;
        LevelPass& operator=(const LevelPass&) = delete;

        void RenderLevel(FrameContext& f);
        void HandleLiveScreenshot(FrameContext& f);
        void RenderHand(FrameContext& f);


    private:

        ui::UiAssets& uiAssets;
        ModHost& modHost;
        replay::Studio& replayStudio;
        Options& options;
        std::unique_ptr<RenderContext>& renderer;
        std::unique_ptr<LevelRenderer>& levelRenderer;
        ui::HudState& hudState;
        HandRenderer& handRenderer;
        entity::EntityRenderer& entityRenderer;
        FrameGenPacer& fgPacer;
        Camera& camera;
        LocalPlayer& player;
        bool& cameraDetached;
        std::unique_ptr<FrameLoop>& loop;
        bool& previewEntitiesSpawned;
        replay::ReplayController& replayCtl;
        std::unique_ptr<BedrockSession>& session;
        std::function<bool()> deathOpen;
        std::function<void()> requestQuit;

        bool swung = false, shotDone = false;
        int shotFrames = 0;
        LoadedSkin replayHandSkin;
        uint64_t replayHandSerial = 0;
    };
}
