#pragma once
#include <array>
#include "client/systems/camera/CameraEffects.hpp"
#include "client/systems/config/Options.hpp"
#include "client/systems/input/Zoom.hpp"
#include "client/systems/model/EntityTracker.hpp"
#include "client/systems/player/DamageEffect.hpp"
#include "client/systems/renderer/FrameContext.hpp"
#include "util/Env.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace perlin {
    struct FrameLoop {
        explicit FrameLoop(const Options& options) : zoom(options.zoom), appliedVsync(options.vsync) {}

        Zoom zoom;
        float scopeFov = 1.0f;

        double replayActorClock = 0.0;
        std::chrono::steady_clock::time_point previous = std::chrono::steady_clock::now();
        const std::chrono::steady_clock::time_point startedAt = previous;
        uint64_t frameCounter = 0;
        int frames = 0;
        std::chrono::steady_clock::time_point titleAt = previous, animationAt = previous;
        std::chrono::steady_clock::time_point lastFrameEnd = previous;
        std::array<float, 4096> frameMs{};
        size_t frameMsHead = 0, frameMsCount = 0;
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
        std::chrono::steady_clock::time_point previewReadyAt = previous;
        size_t previewLastMeshed = 0;

        std::chrono::steady_clock::time_point handLightAt = previous;
        int handSky = 15, handBlock = 0;
        int waterVisionTime = 0;
        std::vector<entity::Entity> frameEntities;
    };
}
