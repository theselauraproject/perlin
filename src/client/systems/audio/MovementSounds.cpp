#include "client/systems/audio/MovementSounds.hpp"

#include <cmath>

namespace perlin::audio {
    std::string MovementSounds::BlockUnder(const physics::State& st, const Port& port) const {
        for (double dy : {0.2, 0.6, 1.1}) {
            std::string name;
            if (!port.blockAt(static_cast<int>(std::floor(st.x)), static_cast<int>(std::floor(st.y - dy)),
                              static_cast<int>(std::floor(st.z)), name))
                continue;
            if (name != "minecraft:air" && port.hasBlockSound(name))
                return name;
        }
        return {};
    }

    void MovementSounds::Update(const physics::State& st, bool sneaking, bool spectator, float dt,
                                const Port& port) {
        if (!haveLast) {
            last = st;
            haveLast = true;
        }
        const glm::dvec3 feet(st.x, st.y, st.z);
        if (!spectator) {
            swimClock += dt;
            if (st.inWater && !last.inWater && std::fabs(st.vy) > 0.1)
                port.playerEvent("splash", feet);
            else if (st.inWater && swimClock > 0.45 && std::hypot(st.x - last.x, st.z - last.z) > 0.03) {
                swimClock = 0;
                port.playerEvent("swim", feet);
            }
            if (st.onGround && !last.onGround && !st.inWater) {
                const std::string under = BlockUnder(st, port);
                if (!under.empty())
                    port.blockEvent(under, last.y - st.y > 3.0 ? "fall" : "land", feet);
            } else if (!st.onGround && last.onGround && st.y > last.y && !st.inWater) {
                const std::string under = BlockUnder(st, port);
                if (!under.empty())
                    port.blockEvent(under, "jump", feet);
            }
            if (st.onGround && !st.inWater && !st.onClimbable && !sneaking) {
                walked += std::hypot(st.x - last.x, st.z - last.z) * 0.6;
                if (walked > nextStep) {
                    nextStep = std::floor(walked) + 1.0;
                    const std::string under = BlockUnder(st, port);
                    if (!under.empty())
                        port.blockEvent(under, "step", feet);
                }
            }
        }
        last = st;
    }

    void MusicScheduler::Update(double now, bool wanted, const std::function<bool()>& play) {
        if (!wanted)
            return;
        if (next == 0)
            next = now + 10.0 + (random() % 2000) / 100.0;
        if (now >= next) {
            const bool started = play();
            next = now + (started ? 60.0 + (random() % 12000) / 100.0 + 120.0 : 300.0);
        }
    }
}
