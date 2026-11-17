#pragma once
#include "client/features/debug/DebugOverlay.hpp"
#include "client/features/interaction/PlayerInteraction.hpp"
#include "client/features/mods/ModHost.hpp"
#include "client/features/replay/Studio.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/screen/ScreenHost.hpp"
#include "client/systems/audio/SoundSystem.hpp"
#include "client/systems/camera/Camera.hpp"
#include "client/systems/entity/EntityRenderer.hpp"
#include "client/systems/particle/DataParticles.hpp"
#include "client/systems/particle/ParticleSystem.hpp"
#include "client/systems/player/LocalPlayer.hpp"
#include "client/systems/renderer/DynamicLights.hpp"
#include "client/systems/renderer/FrameContext.hpp"
#include "client/systems/renderer/FrameLoop.hpp"
#include "client/systems/renderer/LevelRenderer.hpp"
#include "client/systems/renderer/LightBandCache.hpp"
#include "client/systems/renderer/WeatherRenderer.hpp"
#include "client/systems/replay/ReplayController.hpp"
#include "client/systems/resources/ServerPacks.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "world/level/block/BlockPalette.hpp"

namespace perlin::worldview {
    class EntityPass {
    public:
        struct Deps {
            BlockPalette& blockPalette;
            ui::UiAssets& uiAssets;
            ServerPacks& serverPacks;
            debug::Overlay& debugOverlay;
            interaction::Player& interaction;
            LightBandCache& lightCache;
            ModHost& modHost;
            replay::Studio& replayStudio;
            Options& options;
            std::unique_ptr<RenderContext>& renderer;
            std::unique_ptr<LevelRenderer>& levelRenderer;
            ui::HudState& hudState;
            HandRenderer& handRenderer;
            entity::EntityRenderer& entityRenderer;
            WeatherRenderer& weather;
            particle::ParticleSystem& particles;
            particle::DataParticles& dataParticles;
            const Level*& particleLevel;
            audio::SoundSystem& sounds;
            int& fgActive;
            Camera& camera;
            LocalPlayer& player;
            bool& cameraDetached;
            std::unique_ptr<FrameLoop>& loop;
            entity::EntityTracker& previewEntities;
            std::optional<entity::Entity>& dollSelf;
            replay::ReplayController& replayCtl;
        };

        explicit EntityPass(Deps deps);
        EntityPass(const EntityPass&) = delete;
        EntityPass& operator=(const EntityPass&) = delete;

        void RenderEntities(FrameContext& f);

        bool previewEntitiesSpawned = false;


    private:
        struct EntityLightEntry {
            std::chrono::steady_clock::time_point at;
            glm::vec3 colour{1.0f};
            int sky = 0, block = 0;
        };

        std::vector<entity::Entity> CollectFrameEntities(FrameContext& f);
        entity::Entity BuildSelfEntity(FrameContext& f);
        void FilterEntitiesForReplay(std::vector<entity::Entity>& entities);
        entity::EntityRenderer::FrameIn BuildEntityFrameIn(FrameContext& f, bool firstPerson);
        void AttachBlockCracks(FrameContext& f, entity::EntityRenderer::FrameIn& ein);
        void AttachEntityLighting(FrameContext& f, Level& entityLevel, entity::EntityRenderer::FrameIn& ein);
        void RenderBlockEntityPass(FrameContext& f, const entity::EntityRenderer::FrameIn& ein);
        void TickParticles(FrameContext& f, Level& entityLevel, entity::EntityRenderer::FrameIn& ein,
                               const std::vector<entity::Entity>& entities);
        void RenderWeather(FrameContext& f);
        void SpawnPreviewEntities();

        BlockPalette& blockPalette;
        ui::UiAssets& uiAssets;
        ServerPacks& serverPacks;
        debug::Overlay& debugOverlay;
        interaction::Player& interaction;
        LightBandCache& lightCache;
        ModHost& modHost;
        replay::Studio& replayStudio;
        Options& options;
        std::unique_ptr<RenderContext>& renderer;
        std::unique_ptr<LevelRenderer>& levelRenderer;
        ui::HudState& hudState;
        HandRenderer& handRenderer;
        entity::EntityRenderer& entityRenderer;
        WeatherRenderer& weather;
        particle::ParticleSystem& particles;
        particle::DataParticles& dataParticles;
        const Level*& particleLevel;
        audio::SoundSystem& sounds;
        int& fgActive;
        Camera& camera;
        LocalPlayer& player;
        bool& cameraDetached;
        std::unique_ptr<FrameLoop>& loop;
        entity::EntityTracker& previewEntities;
        std::optional<entity::Entity>& dollSelf;
        replay::ReplayController& replayCtl;

        std::unordered_map<int64_t, EntityLightEntry> entityLight;
        std::shared_ptr<const entity::SkinData> selfSkinData;
        std::string selfSkinKey;
        std::vector<uint64_t> previewWalk;
        double particleClock = 0;
        std::vector<particle::Quad> particleQuads;
        std::vector<entity::EntityRenderer::FrameIn::FireSprite> fireSprites;
        std::unordered_map<uint64_t, glm::dvec3> base;
        std::vector<SpecialBlock> specialBlocks;
        std::vector<entity::BlockEntityDraw> blockEntityDraws;
        std::vector<int8_t> solidById;
        double burstClock = 0;
        std::chrono::steady_clock::time_point dataParticleCheck{};
        double nextRainSound = 0.0;
    };
}
