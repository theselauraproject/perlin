#pragma once
#include "world/movement/PlayerPhysics.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <string>

namespace perlin::audio {
    class MovementSounds {
    public:
        struct Port {
            std::function<bool(int x, int y, int z, std::string& blockName)> blockAt;
            std::function<bool(const std::string& blockName)> hasBlockSound;
            std::function<void(const std::string& event, const glm::dvec3& at)> playerEvent;
            std::function<void(const std::string& blockName, const std::string& event, const glm::dvec3& at)>
                blockEvent;
        };

        void Update(const physics::State& state, bool sneaking, bool spectator, float dt, const Port& port);

    private:
        std::string BlockUnder(const physics::State& st, const Port& port) const;

        physics::State last;
        bool haveLast = false;
        double walked = 0.0;
        double nextStep = 1.0;
        double swimClock = 0.0;
    };

    class MusicScheduler {
    public:
        using Random = std::function<int()>;

        explicit MusicScheduler(Random random) : random(std::move(random)) {}

        void Update(double now, bool wanted, const std::function<bool()>& play);

    private:
        Random random;
        double next = 0.0;
    };
}
