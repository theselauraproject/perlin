#pragma once
#include <chrono>
#include <functional>
#include <unordered_map>
#include "client/systems/renderer/LightBandCache.hpp"
#include "client/features/debug/DebugOverlay.hpp"
#include "client/features/mods/ModHost.hpp"
#include "client/features/replay/Studio.hpp"
#include "client/systems/camera/Camera.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/renderer/FrameContext.hpp"
#include "client/systems/renderer/FrameLoop.hpp"
#include "client/systems/renderer/LevelRenderer.hpp"
#include "client/systems/replay/ReplayController.hpp"

namespace perlin::worldview {
    class ViewEnvironment {
    public:
        struct Deps {
            ModHost& modHost;
            BlockPalette& blockPalette;
            LightBandCache& lightCache;
            replay::Studio& replayStudio;
            std::unique_ptr<LevelRenderer>& levelRenderer;
            Camera& camera;
            std::unique_ptr<FrameLoop>& loop;
            debug::Overlay& debugOverlay;
            replay::ReplayController& replayCtl;
            std::unique_ptr<BedrockSession>& session;
            std::function<void()> dimensionChanged;
        };

        explicit ViewEnvironment(Deps deps);
        ViewEnvironment(const ViewEnvironment&) = delete;
        ViewEnvironment& operator=(const ViewEnvironment&) = delete;

        void UpdatePreviewWorld(FrameContext& f);
        void UpdateThirdPersonDistance(BedrockSession* live, float dt);
        void SampleEyeEnvironment(FrameContext& f);
        void ResetForConnect() {
            lastDimId = -1;
            portalTime = 0.0f;
        }

        bool headUnderwater = false;
        bool headInLava = false;
        float waterSurfaceAbove = 0.0f;
        float portalTime = 0.0f;

    private:
        ModHost& modHost;
        BlockPalette& blockPalette;
        LightBandCache& lightCache;
        replay::Studio& replayStudio;
        std::unique_ptr<LevelRenderer>& levelRenderer;
        Camera& camera;
        std::unique_ptr<FrameLoop>& loop;
        debug::Overlay& debugOverlay;
        replay::ReplayController& replayCtl;
        std::unique_ptr<BedrockSession>& session;
        std::function<void()> dimensionChanged;

        int lastDimId = -1;
        std::chrono::steady_clock::time_point lastNote{};
        std::chrono::steady_clock::time_point lastPortalAt{};
        bool edited = false;
    };
}
