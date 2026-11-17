#pragma once
#include <chrono>
#include <cstdint>

namespace perlin::detail {
    using Clock = std::chrono::steady_clock;

    inline uint64_t MsNow() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
    }

    inline double SecondsSince(Clock::time_point since, Clock::time_point now = Clock::now()) {
        return std::chrono::duration<double>(now - since).count();
    }
}
