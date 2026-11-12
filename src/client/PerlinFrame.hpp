#pragma once

#include "client/PerlinInternal.hpp"
#include "util/Log.hpp"
#include <format>
#include <string>
#include <string_view>

namespace perlin {
    namespace frame {
        constexpr float kMaxFrameDelta = 0.1f;
        constexpr float kDefaultFrameDelta = 0.016f;
        constexpr float kNoonTick = 6000.0f;

        constexpr float kHorizonHeight = 63.0f;
        constexpr float kFlatHorizonHeight = -64.0f;
        constexpr auto kTitleInterval = std::chrono::milliseconds(500);
        constexpr auto kAnimationStep = std::chrono::milliseconds(200);

        class Profiler {
        public:
            enum Stage {
                Logic,
                LevelUpdate,
                LevelRender,
                Entities,
                Hand,
                Hud,
                EndFrame,
                Frame,
                Particles,
                Weather,
                kStages
            };

            static Profiler& Get() {
                static Profiler profiler;
                return profiler;
            }
            bool Enabled() const { return enabled; }
            void Add(Stage stage, double ms) {
                total[stage] += ms;
                last[stage] += ms;
            }

            void AddToAverage(Stage stage, double ms) { total[stage] += ms; }

            void FinishFrame(std::chrono::steady_clock::time_point frameStart) {
                if (!enabled)
                    return;
                const double frameMs =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart)
                        .count();
                total[Frame] += frameMs;
                last[Frame] = frameMs;
                for (int i = 0; i < kStages; i++) {
                    worst[i] = std::max(worst[i], last[i]);
                    last[i] = 0;
                }
                frameTimes.push_back(frameMs);
                if (++frames >= 240)
                    Report();
            }

        private:
            void Report() {
                std::ranges::sort(frameTimes);
                const size_t n = frameTimes.size();
                const double p99 = frameTimes[std::min(n - 1, static_cast<size_t>(n * 0.99))];
                // through the log, so it lands in perlin.log on windows, where a gui exe has no console
                std::string line = "avg ms:";
                for (int i = 0; i < kStages; i++)
                    line += std::format(" {}={:.2f}", kNames[i], total[i] / frames);
                line += " | worst ms:";
                for (int i = 0; i < kStages; i++)
                    line += std::format(" {}={:.2f}", kNames[i], worst[i]);
                line += std::format(" | avg {:.1f} fps, 1% low {:.1f} fps",
                                    total[Frame] > 0 ? 1000.0 * frames / total[Frame] : 0.0,
                                    p99 > 0 ? 1000.0 / p99 : 0.0);
                Log("profile", "{}", line);
                std::ranges::fill(total, 0.0);
                std::ranges::fill(worst, 0.0);
                frameTimes.clear();
                frames = 0;
            }

            static constexpr const char* kNames[kStages] = {
                "logic", "level update", "level render", "entities",  "hand",
                "hud",   "end frame",    "frame",        "particles", "weather"};
            bool enabled = PERLIN_ENV("PERLIN_PROFILE") != nullptr;
            double total[kStages] = {};
            double worst[kStages] = {};
            double last[kStages] = {};
            std::vector<double> frameTimes;
            int frames = 0;
        };

        class ProfileScope {
        public:
            explicit ProfileScope(Profiler::Stage stage) : stage(stage) {
                if (Profiler::Get().Enabled())
                    start = std::chrono::steady_clock::now();
            }
            ~ProfileScope() {
                if (Profiler::Get().Enabled())
                    Profiler::Get().Add(stage, std::chrono::duration<double, std::milli>(
                                                   std::chrono::steady_clock::now() - start)
                                                   .count());
            }
            ProfileScope(const ProfileScope&) = delete;
            ProfileScope& operator=(const ProfileScope&) = delete;

        private:
            Profiler::Stage stage;
            std::chrono::steady_clock::time_point start;
        };

        // entities whose right-click replaces eating or using the held item: mounts and rideables
        inline bool TakesItemUse(std::string_view type) {
            if (type == "minecraft:cushion")
                return true;
            if (type.contains("boat") || type.contains("raft"))
                return !type.contains("chest");
            if (type.contains("minecart"))
                return type == "minecraft:minecart";
            return type == "minecraft:horse" || type == "minecraft:donkey" || type == "minecraft:mule" ||
                   type == "minecraft:camel" || type == "minecraft:strider" || type == "minecraft:pig" ||
                   type == "minecraft:skeleton_horse" || type == "minecraft:zombie_horse";
        }

        inline bool IsInteractable(std::string_view type) {
            if (type == "minecraft:cushion")
                return true;
            if (type.contains("boat") || type.contains("raft"))
                return !type.contains("chest");
            if (type.contains("minecart"))
                return type == "minecraft:minecart";
            // the game sends the interact for any entity and the server decides what it does: custom entities
            // (loot chests, npcs), villagers, feeding and leashing all depend on it
            static constexpr std::string_view kInert[] = {"minecraft:item",
                                                          "minecraft:xp_orb",
                                                          "minecraft:arrow",
                                                          "minecraft:snowball",
                                                          "minecraft:egg",
                                                          "minecraft:ender_pearl",
                                                          "minecraft:fireball",
                                                          "minecraft:small_fireball",
                                                          "minecraft:thrown_trident",
                                                          "minecraft:splash_potion",
                                                          "minecraft:lingering_potion",
                                                          "minecraft:xp_bottle",
                                                          "minecraft:fishing_hook",
                                                          "minecraft:lightning_bolt",
                                                          "minecraft:area_effect_cloud",
                                                          "minecraft:falling_block",
                                                          "minecraft:tnt"};
            for (std::string_view inert : kInert)
                if (type == inert)
                    return false;
            // players: right-click in a fight must stay an item use (eating, bows, rods)
            return !type.empty() && type != "minecraft:player";
        }
    }

    struct FrameContext {
        std::chrono::steady_clock::time_point frameStart;
        float dt = frame::kDefaultFrameDelta;
        int width = 0, height = 0;
        BedrockSession* live = nullptr;
        bool earlyItemUse = false, blockedFoodUse = false;
        double worldTicks = frame::kNoonTick;
        bool underwater = false, eyeInLava = false;
        std::string eyeBiome = "minecraft:plains";
        float partial = 0.0f, rain = 0.0f, thunder = 0.0f;
        Dimension dimension = Dimension::Overworld;
        Environment env;
        glm::mat4 handViewEffect{1.0f};
        glm::mat4 viewProjection{1.0f};
        Frustum frustum{};
        glm::vec3 eye{0.0f};
        GraphicsSettings graphics;
        std::vector<dynlight::Light> dyn;
        int dynMode = 0;
        FrameParams params{};
        std::optional<entity::Entity> fpSelf;
    };

    struct Perlin::FrameLoop {
        explicit FrameLoop(const Options& options) : zoom(options.zoom), appliedVsync(options.vsync) {}

        Zoom zoom;
        float scopeFov = 1.0f;

        double replayActorClock = 0.0;
        TimePoint previous = Clock::now();
        const TimePoint startedAt = previous;
        uint64_t frameCounter = 0;
        int frames = 0;
        TimePoint titleAt = previous, animationAt = previous;
        uint64_t animationStep = 0;
        bool appliedVsync;

        DamageEffect damage;
        uint32_t lastHurtCount = 0, lastHurtSerial = 0;
        const BedrockSession* hurtSession = nullptr;

        const bool preview = PERLIN_ENV("PERLIN_PREVIEW") != nullptr;
        std::unique_ptr<Level> previewLevel;
        double previewTicks = frame::kNoonTick;
        std::string previewShot;
        double previewWait = 15.0;
        int previewShotFrames = -1;
        TimePoint previewReadyAt = previous;
        size_t previewLastMeshed = 0;

        TimePoint handLightAt = previous;
        int handSky = 15, handBlock = 0;
        int waterVisionTime = 0;
        std::vector<entity::Entity> frameEntities;
    };
}
