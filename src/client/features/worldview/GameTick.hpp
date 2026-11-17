#pragma once
#include "client/features/mods/ModHost.hpp"
#include "client/platform/Application.hpp"
#include "client/screen/ScreenHost.hpp"
#include "client/systems/audio/SoundSystem.hpp"
#include "client/systems/camera/Camera.hpp"
#include "client/systems/entity/EntityRenderer.hpp"
#include "client/systems/gfx/Lightmap.hpp"
#include "client/systems/player/AmmoRules.hpp"
#include "client/systems/player/LocalPlayer.hpp"
#include "client/systems/renderer/FrameContext.hpp"
#include "client/systems/renderer/FrameLoop.hpp"

namespace perlin::worldview {
    class GameTick {
    public:
        struct Deps {
            ModHost& modHost;
            Input& input;
            InputManager& inputManager;
            ui::HudState& hudState;
            HandRenderer& handRenderer;
            audio::SoundSystem& sounds;
            Camera& camera;
            LocalPlayer& player;
            BlockLightFlicker& blockLight;
            double& tickDebt;
            float& rainLevel;
            float& previousRainLevel;
            float& thunderLevel;
            float& previousThunderLevel;
            std::unique_ptr<FrameLoop>& loop;
            std::function<void()> tickHud;
        };

        explicit GameTick(Deps deps);
        GameTick(const GameTick&) = delete;
        GameTick& operator=(const GameTick&) = delete;

        void TickGame(FrameContext& f);


    private:

        ModHost& modHost;
        Input& input;
        InputManager& inputManager;
        ui::HudState& hudState;
        HandRenderer& handRenderer;
        audio::SoundSystem& sounds;
        Camera& camera;
        LocalPlayer& player;
        BlockLightFlicker& blockLight;
        double& tickDebt;
        float& rainLevel;
        float& previousRainLevel;
        float& thunderLevel;
        float& previousThunderLevel;
        std::unique_ptr<FrameLoop>& loop;
        std::function<void()> tickHud;

    };
}
