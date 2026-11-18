#pragma once

#include <atomic>
#include <string_view>

namespace perlin::ui {
    struct DialFrames {
        static inline std::atomic<int> compass{0};
        static inline std::atomic<int> recovery{0};
        static inline std::atomic<int> lodestone{0};
        static inline std::atomic<int> clock{0};
    };

    inline int DialFrameCount(std::string_view name) {
        if (name.starts_with("minecraft:"))
            name.remove_prefix(10);
        if (name == "clock")
            return 64;
        if (name == "compass" || name == "recovery_compass" || name == "lodestone_compass")
            return 32;
        return 0;
    }

    inline int DialFrame(std::string_view name) {
        if (name.starts_with("minecraft:"))
            name.remove_prefix(10);
        if (name == "clock")
            return DialFrames::clock.load(std::memory_order_relaxed);
        if (name == "compass")
            return DialFrames::compass.load(std::memory_order_relaxed);
        if (name == "recovery_compass")
            return DialFrames::recovery.load(std::memory_order_relaxed);
        if (name == "lodestone_compass")
            return DialFrames::lodestone.load(std::memory_order_relaxed);
        return -1;
    }

    inline std::string_view DialAtlas(std::string_view name) {
        if (name.starts_with("minecraft:"))
            name.remove_prefix(10);
        if (name == "clock")
            return "textures/items/watch_atlas";
        if (name == "compass")
            return "textures/items/compass_atlas";
        if (name == "recovery_compass")
            return "textures/items/recovery_compass_atlas";
        if (name == "lodestone_compass")
            return "textures/items/lodestonecompass_atlas";
        return {};
    }
}
