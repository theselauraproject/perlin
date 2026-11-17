#pragma once
#include <glm/gtc/type_ptr.hpp>
#include "client/features/debug/DebugOverlay.hpp"
#include "client/features/mods/ModHost.hpp"
#include "client/features/replay/Studio.hpp"
#include "client/features/worldview/ViewEnvironment.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/screen/ScreenHost.hpp"
#include "client/systems/camera/Camera.hpp"
#include "client/systems/gfx/Lightmap.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/player/LocalPlayer.hpp"
#include "client/systems/renderer/FogDefs.hpp"
#include "client/systems/renderer/FrameContext.hpp"
#include "client/systems/renderer/FrameLoop.hpp"
#include "client/systems/renderer/LevelRenderer.hpp"
#include "client/systems/replay/ReplayController.hpp"
#include "client/systems/resources/ServerPacks.hpp"
#include "client/systems/resources/ShaderPackCache.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "client/ui/Hud.hpp"

namespace perlin::worldview {
    class FrameParamsBuilder {
    public:
        struct Deps {
            ui::UiAssets& uiAssets;
            ServerPacks& serverPacks;
            debug::Overlay& debugOverlay;
            ModHost& modHost;
            replay::Studio& replayStudio;
            ViewEnvironment& view;
            Options& options;
            std::unique_ptr<RenderContext>& renderer;
            std::unique_ptr<LevelRenderer>& levelRenderer;
            std::unique_ptr<ui::Hud>& hud;
            ui::HudState& hudState;
            FogDefs& fogDefs;
            int& fgActive;
            Camera& camera;
            LocalPlayer& player;
            BlockLightFlicker& blockLight;
            double& tickDebt;
            float& rainLevel;
            float& previousRainLevel;
            float& thunderLevel;
            float& previousThunderLevel;
            std::unique_ptr<FrameLoop>& loop;
            float& hudPartial;
            ShaderPackCache& shaderPackCache;
            replay::ReplayController& replayCtl;
            std::unique_ptr<BedrockSession>& session;
            std::atomic<bool>& sessionReady;
            std::function<bool()> menuOpen;
        };

        explicit FrameParamsBuilder(Deps deps);
        FrameParamsBuilder(const FrameParamsBuilder&) = delete;
        FrameParamsBuilder& operator=(const FrameParamsBuilder&) = delete;

        void PrepareView(FrameContext& f);
        void BuildFrameParams(FrameContext& f);


    private:
        FogState ComputeFrameFog(FrameContext& f);
        void CollectDynamicLights(FrameContext& f);
        void FillSettingsParams(FrameContext& f);
        void FillPackAndLightParams(FrameContext& f);
        void FillCameraParams(FrameContext& f, const FogState& fog);
        void FillUpscaleParams(FrameContext& f);
        void FillSkyParams(FrameContext& f);
        void FillCloudParams(FrameContext& f);
        void FillOverlayParams(FrameContext& f);
        void FillTimeAndTileParams(FrameContext& f);
        void FillLightmapParams(FrameContext& f);

        ui::UiAssets& uiAssets;
        ServerPacks& serverPacks;
        debug::Overlay& debugOverlay;
        ModHost& modHost;
        replay::Studio& replayStudio;
        ViewEnvironment& view;
        Options& options;
        std::unique_ptr<RenderContext>& renderer;
        std::unique_ptr<LevelRenderer>& levelRenderer;
        std::unique_ptr<ui::Hud>& hud;
        ui::HudState& hudState;
        FogDefs& fogDefs;
        int& fgActive;
        Camera& camera;
        LocalPlayer& player;
        BlockLightFlicker& blockLight;
        double& tickDebt;
        float& rainLevel;
        float& previousRainLevel;
        float& thunderLevel;
        float& previousThunderLevel;
        std::unique_ptr<FrameLoop>& loop;
        float& hudPartial;
        ShaderPackCache& shaderPackCache;
        replay::ReplayController& replayCtl;
        std::unique_ptr<BedrockSession>& session;
        std::atomic<bool>& sessionReady;
        std::function<bool()> menuOpen;

        std::chrono::steady_clock::time_point fogCheck = std::chrono::steady_clock::time_point{};
        std::string hudPackKey;
        int once = 0;
        glm::vec3 lastCamera{0.0f};
        bool haveLastCamera = false;
        float dynamic = 1.0f;
        double frameMs = 16.0;
        std::chrono::steady_clock::time_point lastChange = std::chrono::steady_clock::time_point{};
        float waterTile = -1, flowTile = -1, lavaTile = -1;
        int tileScan = 0;
        std::vector<uint8_t> lastFoliage;
    };
}
