#pragma once

#include <atomic>

namespace perlin::gpu {
    struct FrameNote {
        std::atomic<int> width{0}, height{0}, drawnChunks{0}, pendingUploads{0};
    };
    inline FrameNote& LastFrameNote() {
        static FrameNote note;
        return note;
    }
    inline std::atomic<bool>& CrashDiagnostics() {
        static std::atomic<bool> on{false};
        return on;
    }
    inline std::atomic<int>& SafeModeLevel() {
        static std::atomic<int> level{0};
        return level;
    }
}
