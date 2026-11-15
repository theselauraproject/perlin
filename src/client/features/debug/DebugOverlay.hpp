#pragma once
#include "world/level/block/BlockPalette.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "client/systems/audio/SoundSystem.hpp"
#include "client/systems/camera/Camera.hpp"
#include "client/systems/config/Options.hpp"
#include "client/systems/entity/EntityRenderer.hpp"
#include "client/systems/input/Input.hpp"
#include "client/systems/input/InputManager.hpp"
#include "client/systems/model/HudState.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/player/Interaction.hpp"
#include "client/systems/renderer/FrameContext.hpp"
#include "client/systems/renderer/LevelRenderer.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/ui/DebugOverlay.hpp"
#include "client/ui/Hud.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::debug {
    struct State {
        bool on = false;
        bool chunkBorders = false;
        bool hitboxes = false;
        bool softPaused = false;
        bool advancedTooltips = false;
        int switcherIndex = -1;
        int fps = 0;
        uint64_t rateTx = 0, rateRx = 0;
        int txRate = 0, rxRate = 0;
        std::chrono::steady_clock::time_point rateAt;
        int heightSurface = 0;
        std::chrono::steady_clock::time_point heightAt;
        int sky = 15, block = 0;
        std::string customSky;
    };

    class Overlay {
    public:
        struct Deps {
            Options& options;
            BlockPalette& blockPalette;
            ui::UiAssets& uiAssets;
            Input& input;
            InputManager& inputManager;
            Camera& camera;
            ui::ChatLog& chat;
            ui::HudState& hudState;
            entity::EntityRenderer& entityRenderer;
            audio::SoundSystem& sounds;
            std::unique_ptr<LevelRenderer>& levelRenderer;
            std::unique_ptr<RenderContext>& renderer;
            std::unique_ptr<ui::Hud>& hud;
            const AimHit& aimHit;
            const uint64_t& hudTicks;
            std::function<void(int&, int&)> framebufferSize;
            std::function<void(const std::string&)> setClipboard;
            std::function<void(std::vector<std::string>&, std::vector<std::string>&)> applyModDebugLines;
        };

        explicit Overlay(Deps deps);
        Overlay(const Overlay&) = delete;
        Overlay& operator=(const Overlay&) = delete;

        void HandleKeys(FrameContext& f, Level* previewLevel);
        void Fill(ui::HudState& state, BedrockSession* live, Level* level);

        State state;

    private:
        void RayTargets(Level& level, glm::vec3 origin, glm::vec3 dir, double reach, ui::DebugInfo& info);

        Options& options;
        BlockPalette& blockPalette;
        ui::UiAssets& uiAssets;
        Input& input;
        InputManager& inputManager;
        Camera& camera;
        ui::ChatLog& chat;
        ui::HudState& hudState;
        entity::EntityRenderer& entityRenderer;
        audio::SoundSystem& sounds;
        std::unique_ptr<LevelRenderer>& levelRenderer;
        std::unique_ptr<RenderContext>& renderer;
        std::unique_ptr<ui::Hud>& hud;
        const AimHit& aimHit;
        const uint64_t& hudTicks;
        std::function<void(int&, int&)> framebufferSize;
        std::function<void(const std::string&)> setClipboard;
        std::function<void(std::vector<std::string>&, std::vector<std::string>&)> applyModDebugLines;

        std::unordered_map<uint16_t, int> blockKinds;
        std::chrono::steady_clock::time_point vramQueryAt;
        bool vramKnown = false;
        uint64_t vramDedicated = 0, vramShared = 0;
        std::vector<std::string> cachedLeft, cachedRight;
        std::chrono::steady_clock::time_point cachedAt;
        bool f3Was = false, combo = false;
        bool keyWas[256] = {};
        std::chrono::steady_clock::time_point cHeldSince;
        bool cHeld = false;
        int previousMode = static_cast<int>(ui::GameMode::Creative);
    };
}
