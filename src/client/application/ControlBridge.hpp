#pragma once
#include "client/services/control/ClientControl.hpp"
#include "client/systems/camera/Camera.hpp"
#include "client/systems/input/InputManager.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/player/LocalPlayer.hpp"
#include "client/systems/renderer/LevelRenderer.hpp"
#include "client/systems/gfx/RenderContext.hpp"
#include "client/systems/input/Input.hpp"

#include <cstdint>
#include <memory>

class AppPlatform;

namespace perlin {
    class ControlBridge {
    public:
        struct Context {
            Input& input;
            InputManager& inputManager;
            LocalPlayer& player;
            Camera& camera;
            AppPlatform* platform;
            BedrockSession* session;
            bool sessionReady;
            LevelRenderer* levelRenderer;
            RenderContext* renderer;
        };

        bool Start(uint16_t port);
        void Stop();
        bool Running() const { return control != nullptr; }
        const std::string& Token() const;

        void Poll(const Context& context);
        void Publish(const Context& context);
        void Trace(uint64_t tick, const physics::State& state, const physics::Input& input);

    private:
        std::shared_ptr<ClientControl> control;
        ControlSnapshot state;
    };
}
