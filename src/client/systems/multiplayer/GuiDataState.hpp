#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

namespace perlin {
    // The producer side of GuiDataPickItem: a replacement notice, not a queue
    // or a request to select a different inventory slot. See the matched 2193
    // GuiData::showPopupNotice/getNewPopupNotice/tick evidence.
    class GuiDataState {
    public:
        using Clock = std::chrono::steady_clock;
        struct Notice {
            std::string name, effects;
            uint64_t serial = 0;
        };
        struct Flash {
            int32_t slot = -1;
            Clock::time_point at{};
        };

        void Pick(std::string name, std::string effects, int32_t slot,
                  Clock::time_point now = Clock::now()) {
            popup.name = std::move(name);
            popup.effects = std::move(effects);
            ++popup.serial;
            age = 0.0f;
            dirty = true;
            flash = {slot, now};
        }

        void Tick() {
            // Preserve the reference's single-precision accumulation and strict
            // comparison. This is the availability window, not a render fade.
            if (age < 2.0f)
                age += 0.05f;
        }

        bool TakeNotice(Notice& out) {
            if (!dirty || !(age < 2.0f))
                return false;
            out = popup;
            dirty = false;
            return true;
        }

        Flash GetFlash() const { return flash; }
        void Clear() { *this = GuiDataState{}; }

    private:
        Notice popup;
        Flash flash;
        float age = 2.0f;
        bool dirty = false;
    };
}
