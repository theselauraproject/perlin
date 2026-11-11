#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace perlin {
    struct ControlCommand {
        uint64_t id = 0;
        std::string op;
        std::vector<std::string> actions;
        int ticks = 0;
        float yaw = 0, pitch = 0, x = 0, y = 0, z = 0;
        std::string text;
        int key = 0, button = 0;
        bool down = false;
    };

    struct ControlActor {
        std::string runtimeId, uniqueId, vehicleUid, type;
        std::array<double, 3> position{}, velocity{}, lastServerPosition{};
        bool onGround = false, haveServerPosition = false;
        uint32_t moveSerial = 0, hurtSerial = 0;
    };

    struct ControlSnapshot {
        uint64_t tick = 0, authTick = 0, corrections = 0, applied = 0;
        bool connected = false, initialized = false, onGround = false;
        bool sprinting = false, sneaking = false, swimming = false, flying = false;
        bool cursorCaptured = false, textInput = false;
        bool inWater = false, inLava = false, onClimbable = false;
        bool vitalsKnown = false, dead = false;
        bool sleeping = false;
        bool dimensionChanging = false, columnSettled = false;
        int dimension = 0;
        uint64_t predictionHistorySize = 0;
        float health = 20, hunger = 20;
        float swimAmount = 0;
        int air = 300;
        std::array<double, 3> position{}, velocity{};
        float yaw = 0, pitch = 0;
        int connectionState = 0;
        std::string error, commandError;
        uint64_t commandReplies = 0;
        int lastCommandSuccess = 0;
        std::string lastCommandOutput;
        bool hasProbe = false, probeLoaded = false;
        std::array<int, 3> probePosition{};
        std::string probeName;
        std::vector<std::array<double, 6>> probeCollision;
        float movementSpeed = 0;
        std::vector<int> effectIds;
        bool serverSprinting = false;
        bool serverSwimming = false;
        std::array<std::string, 2> actorFlagWords;
        bool inScaffolding = false, overScaffolding = false, descendThroughBlock = false;
        float fallDistance = 0;
        std::array<float, 2> serverSize{0.6f, 1.8f};
        std::string movementAttributeJson;
        std::vector<ControlActor> actors;
    };

    class ClientControl {
    public:
        ClientControl();
        ~ClientControl();
        bool Start(int port);
        void Stop();
        const std::string& Token() const;
        std::vector<ControlCommand> TakeCommands();
        void Publish(const ControlSnapshot& state);
        bool TakeFrameRequest();
        void Frame(uint32_t width, uint32_t height, uint32_t pitch, const uint8_t* bgra, bool flipY);
        void Trace(const std::string& json);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
