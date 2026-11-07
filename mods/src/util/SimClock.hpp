#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

namespace perlin {
    class SimClock {
    public:
        using clock = std::chrono::steady_clock;
        static clock::time_point Now() {
            if (!Mode().load(std::memory_order_relaxed))
                return clock::now();
            return clock::time_point(std::chrono::nanoseconds(Ns().load(std::memory_order_relaxed)));
        }
        static void EnterVirtual() {
            Ns().store(clock::now().time_since_epoch().count(), std::memory_order_relaxed);
            Mode().store(true, std::memory_order_relaxed);
        }
        static void ExitVirtual() { Mode().store(false, std::memory_order_relaxed); }
        static bool IsVirtual() { return Mode().load(std::memory_order_relaxed); }
        static void Advance(double ms) {
            if (ms > 0)
                Ns().fetch_add(static_cast<int64_t>(ms * 1.0e6), std::memory_order_relaxed);
        }

    private:
        static std::atomic<bool>& Mode() {
            static std::atomic<bool> v{false};
            return v;
        }
        static std::atomic<int64_t>& Ns() {
            static std::atomic<int64_t> v{0};
            return v;
        }
    };
}
