#pragma once
#include <atomic>
#include <cstdint>

namespace perlin::playdevice {
    // what the player is playing with right now, in the protocol's terms: servers sort touch, mouse and
    // gamepad players apart (queues, anti-cheat), so this must follow the real device
    enum Mode : uint8_t { Mouse = 1, Touch = 2, Gamepad = 3 };
    enum Interaction : uint8_t { TouchTap = 0, Crosshair = 1, Classic = 2 };

    inline std::atomic<uint8_t> mode{Mouse};
    inline std::atomic<uint8_t> interaction{Crosshair};

    inline void Set(Mode m, Interaction i) {
        mode.store(m, std::memory_order_relaxed);
        interaction.store(i, std::memory_order_relaxed);
    }
    inline Mode Current() { return static_cast<Mode>(mode.load(std::memory_order_relaxed)); }
    inline Interaction CurrentInteraction() {
        return static_cast<Interaction>(interaction.load(std::memory_order_relaxed));
    }
}
