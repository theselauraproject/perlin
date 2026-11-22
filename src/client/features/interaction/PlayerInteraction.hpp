#pragma once
#include "client/features/audio/AudioDirector.hpp"
#include "client/systems/camera/Camera.hpp"
#include "client/systems/mods/ModManager.hpp"
#include "client/systems/config/Options.hpp"
#include "client/systems/audio/SoundSystem.hpp"
#include "client/systems/entity/EntityRenderer.hpp"
#include "client/systems/hand/HandRenderer.hpp"
#include "client/systems/input/Input.hpp"
#include "client/systems/input/InputManager.hpp"
#include "client/systems/model/HudState.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/particle/ParticleSystem.hpp"
#include "client/systems/player/HeldBuild.hpp"
#include "client/systems/player/Interaction.hpp"
#include "client/systems/player/LocalPlayer.hpp"
#include "client/systems/renderer/FrameContext.hpp"
#include "client/systems/renderer/LevelRenderer.hpp"
#include "client/systems/renderer/WeatherRenderer.hpp"
#include "client/systems/resources/ServerBlocks.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace perlin::interaction {
    struct LocalCrack {
        bool active = false;
        int x = 0, y = 0, z = 0, stage = 0;
        float progress = 0.0f;
    };

    struct Mining {
        bool breaking = false;
        int x = 0, y = 0, z = 0, face = 1;
        float progress = 0.0f;
        int delayTicks = 0;
        int hitTicks = 0;
        uint64_t creativeReadyAt = 0;
        double breakClock = 0, useClock = 0, swingClock = 0;
    };

    struct RecentBreak {
        int x = 0, y = 0, z = 0;
        std::chrono::steady_clock::time_point at;
    };

    struct View {
        const std::unique_ptr<Level>* previewLevel = nullptr;
        const std::vector<entity::Entity>* entities = nullptr;
        bool preview = false;
    };

    class Player {
    public:
        struct Deps {
            Options& options;
            BlockPalette& blockPalette;
            ServerBlocks& serverBlocks;
            Input& input;
            InputManager& inputManager;
            Camera& camera;
            LocalPlayer& player;
            ui::HudState& hudState;
            entity::EntityRenderer& entityRenderer;
            HandRenderer& handRenderer;
            audio::SoundSystem& sounds;
            audio::AudioDirector& audioDirector;
            particle::ParticleSystem& particles;
            std::unique_ptr<LevelRenderer>& levelRenderer;
            WeatherRenderer& weather;
            const float& rainLevel;
            const float& previousRainLevel;
            int& previewSlot;
            std::function<bool()> replayPlaying;
            std::function<bool(abi::InteractKind, int, int, int, int, uint64_t)> dispatchModInteract;
            std::function<void()> vibrateBlockBroken;
        };

        explicit Player(Deps deps);
        Player(const Player&) = delete;
        Player& operator=(const Player&) = delete;

        void UpdateGameInput(FrameContext& f, const View& frameView);
        void UpdateEatingAndDrinking(FrameContext& f, BedrockSession& live);
        void StopBreaking(BedrockSession* live);
        void SwingHand();
        void DropSelected(BedrockSession& live, bool wholeStack);
        void StopHeldBuild() { heldBuild.Stop(); }

        AimHit aimHit;
        uint32_t selfSwing = 0;
        LocalCrack localCrack;
        RecentBreak lastLocalBreak;
        bool usingItem = false;
        bool offhandUse = false;

    private:
        void UpdateHotbar(FrameContext& f);
        void UpdateTechnicalBlocks(const ui::HudItem& held, bool creative, BedrockSession* live);
        void UpdateAim(FrameContext& f, bool spectator, bool creative);
        glm::dvec3 AimDirection(const FrameContext& f) const;
        void ApplyTouchActions();
        Action touchHoldAction = Action::Count;
        void UpdateBlockBreaking(FrameContext& f, const ui::HudItem& held, bool creative);
        void UpdateBlockUse(FrameContext& f, const ui::HudItem& held, bool creative);
        void ReleaseHeldItem(BedrockSession* live, const ui::HudItem& held, const float eye[3]);

        Options& options;
        BlockPalette& blockPalette;
        ServerBlocks& serverBlocks;
        Input& input;
        InputManager& inputManager;
        Camera& camera;
        LocalPlayer& player;
        ui::HudState& hudState;
        entity::EntityRenderer& entityRenderer;
        HandRenderer& handRenderer;
        audio::SoundSystem& sounds;
        audio::AudioDirector& audioDirector;
        particle::ParticleSystem& particles;
        std::unique_ptr<LevelRenderer>& levelRenderer;
        WeatherRenderer& weather;
        const float& rainLevel;
        const float& previousRainLevel;
        int& previewSlot;
        std::function<bool()> replayPlaying;
        std::function<bool(abi::InteractKind, int, int, int, int, uint64_t)> dispatchModInteract;
        std::function<void()> vibrateBlockBroken;

        heldbuild::HeldBuild heldBuild;
        Mining mining;
        View view;
        float scrollAccum = 0.0f;
        int spectatorSpeed = 0;
        uint64_t useStartTick = 0;
        bool useConsumed = false, useWasHeld = false;

        std::chrono::steady_clock::time_point lastUseSent{};
        uint64_t lastBuildAt = 0;
        glm::ivec3 lastBuildCell{0, -1000, 0};
    };
}
