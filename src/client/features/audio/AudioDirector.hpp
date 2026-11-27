#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "client/systems/audio/AudioEngine.hpp"
#include "client/systems/audio/MovementSounds.hpp"
#include "client/systems/audio/SoundSystem.hpp"
#include "client/systems/audio/Subtitles.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "client/systems/camera/Camera.hpp"
#include "client/systems/config/Options.hpp"
#include "client/systems/model/HudState.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "client/systems/player/LocalPlayer.hpp"

namespace perlin::audio {
    class AudioDirector {
    public:
        using Clock = std::chrono::steady_clock;

        struct LocalPlace {
            int x = 0, y = 0, z = 0;
            Clock::time_point at;
        };

        struct Deps {
            const Options& options;
            SoundSystem& sounds;
            AudioEngine& engine;
            const Camera& camera;
            const LocalPlayer& player;
            ui::HudState& hudState;
            BlockPalette& palette;
            std::function<void(const BedrockSession::SoundRequest&)> noteSound;
            std::function<std::string()> vanillaRoot;
            std::function<std::vector<std::string>()> packDirectories;
            std::function<int()> random;
        };

        explicit AudioDirector(Deps d) : deps(std::move(d)), music(deps.random) {}
        AudioDirector(const AudioDirector&) = delete;
        AudioDirector& operator=(const AudioDirector&) = delete;

        void Update(BedrockSession* live, double now, float dt);
        void NoteLocalPlace(int x, int y, int z, Clock::time_point at) { lastLocalPlace = {x, y, z, at}; }

    private:
        void OpenOnce();
        void UpdateListener(BedrockSession* live);
        void DispatchRequests(BedrockSession& live);
        void UpdateMovement(BedrockSession& live, float dt);
        void UpdateSubtitles();
        bool EchoesLocalPlace(const BedrockSession::SoundRequest& r, const std::string& lowered) const;

        Deps deps;
        Subtitles subtitles;
        MovementSounds movement;
        MusicScheduler music;
        LocalPlace lastLocalPlace;
        bool opened = false;
        bool hooked = false;
        BedrockSession* soundSession = nullptr;
        std::unordered_map<uint64_t, uint64_t> serverSounds;
        std::unordered_set<uint64_t> serverVoices;
        glm::dvec3 listenerPosition{0.0};
    };
}
