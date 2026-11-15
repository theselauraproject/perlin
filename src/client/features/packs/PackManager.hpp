#pragma once
#include "client/systems/sprites/UiAssets.hpp"
#include "client/systems/resources/TexturePack.hpp"
#include "client/systems/resources/ServerPacks.hpp"
#include "client/systems/resources/ServerBlocks.hpp"
#include "client/ui/JsonScreen.hpp"
#include "client/systems/text/McFont.hpp"
#include "client/systems/resources/ShaderPack.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "world/level/lighting/LightRegistry.hpp"
#include "client/systems/audio/AudioEngine.hpp"
#include "client/systems/audio/SoundSystem.hpp"
#include "client/systems/config/Options.hpp"
#include "client/systems/entity/EntityRenderer.hpp"
#include "client/systems/gfx/RenderContext.hpp"
#include "client/systems/hand/HandRenderer.hpp"
#include "client/systems/model/HudState.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/player/HeldBuild.hpp"
#include "client/systems/renderer/LevelRenderer.hpp"
#include "client/systems/resources/ShaderPackCache.hpp"
#include "client/ui/ChatLog.hpp"
#include "client/ui/Hud.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace perlin::packs {
    class PackManager {
    public:
        struct Deps {
            Options& options;
            ui::UiAssets& uiAssets;
            TexturePack& texturePack;
            ServerPacks& serverPacks;
            ServerBlocks& serverBlocks;
            ui::JsonScreens& jsonScreens;
            ui::McFont& mcFont;
            ShaderPack& shaderPack;
            BlockPalette& blockPalette;
            LightRegistry& lightRegistry;
            std::unique_ptr<RenderContext>& renderer;
            std::unique_ptr<LevelRenderer>& levelRenderer;
            std::unique_ptr<ui::Hud>& hud;
            entity::EntityRenderer& entityRenderer;
            HandRenderer& handRenderer;
            ui::HudState& hudState;
            audio::AudioEngine& audioEngine;
            audio::SoundSystem& sounds;
            ui::ChatLog& chat;
            const uint64_t& hudTicks;
            ShaderPackCache& shaderPackCache;
            std::function<void()> stopHeldBuild;
            std::function<std::vector<std::string>()> modTexturePacks;
            std::function<void()> resourcesReloaded;
            std::function<void()> resetLevelScreens;
        };

        explicit PackManager(Deps deps);
        PackManager(const PackManager&) = delete;
        PackManager& operator=(const PackManager&) = delete;

        void ApplyTexturePacks();
        void CommitTexturePacks(const std::vector<std::string>& pending);
        void ApplyShaderPack();
        void UnloadServerPacks(bool leavingLevel = false);
        void PollJavaPack(BedrockSession* live);
        void PollHotReload();

        bool TakeLevelUnloadPending() { return std::exchange(levelUnloadPending, false); }
        void RequestLevelUnload() { levelUnloadPending = true; }
        void MarkTexturesStale() { hotTexStamp = "reload"; }
        std::string& JavaPackPending() { return javaPackPending; }

    private:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        struct HotStamps {
            std::string tex, shader;
        };

        Options& options;
        ui::UiAssets& uiAssets;
        TexturePack& texturePack;
        ServerPacks& serverPacks;
        ServerBlocks& serverBlocks;
        ui::JsonScreens& jsonScreens;
        ui::McFont& mcFont;
        ShaderPack& shaderPack;
        BlockPalette& blockPalette;
        LightRegistry& lightRegistry;
        std::unique_ptr<RenderContext>& renderer;
        std::unique_ptr<LevelRenderer>& levelRenderer;
        std::unique_ptr<ui::Hud>& hud;
        entity::EntityRenderer& entityRenderer;
        HandRenderer& handRenderer;
        ui::HudState& hudState;
        audio::AudioEngine& audioEngine;
        audio::SoundSystem& sounds;
        std::function<void()> stopHeldBuild;
        ui::ChatLog& chat;
        const uint64_t& hudTicks;
        ShaderPackCache& shaderPackCache;
        std::function<std::vector<std::string>()> modTexturePacks;
        std::function<void()> resourcesReloaded;
        std::function<void()> resetLevelScreens;

        std::string javaPackPending;
        int javaPackDimension = 0;
        bool startupChecked = false;
        bool levelUnloadPending = false;
        std::future<HotStamps> hotScan;
        TimePoint hotPollAt;
        std::string hotTexStamp, hotShaderStamp;
    };
}
