#pragma once
#include "client/systems/config/Options.hpp"
#include "client/systems/model/EntityTracker.hpp"
#include "client/systems/gfx/Frustum.hpp"
#include "client/systems/gfx/RenderContext.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/renderer/DynamicLights.hpp"
#include "client/systems/renderer/Environment.hpp"

#include <glm/glm.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace perlin {
    namespace frame {
        constexpr float kMaxFrameDelta = 0.1f;
        constexpr float kDefaultFrameDelta = 0.016f;
        constexpr float kNoonTick = 6000.0f;

        constexpr float kHorizonHeight = 63.0f;
        constexpr float kFlatHorizonHeight = -64.0f;
        constexpr auto kTitleInterval = std::chrono::milliseconds(500);
        constexpr auto kAnimationStep = std::chrono::milliseconds(200);
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
}
