#pragma once
#include <chrono>
#include <functional>

namespace perlin {
    class FrameGenPacer {
    public:
        using Clock = std::chrono::steady_clock;
        enum class Stage { AfterPresent, BeforeRender };
        using Present = std::function<bool(int index, int total)>;

        void RealFrame(Clock::time_point now, int total, double workMs);
        int Pump(Stage stage, bool paced, Clock::time_point idleUntil, const Present& present);
        void PreRenderMs(double ms) { preMs += (ms - preMs) * 0.1; }
        double TakeWaitedMs() {
            const double w = waitedMs;
            waitedMs = 0.0;
            return w;
        }

        int AutoTotal(double targetHz, Clock::time_point now);
        double WorkMs() const { return workMs; }
        bool Pending() const { return next > 0; }

        static void SleepUntil(Clock::time_point due);

    private:
        int total = 1, next = 0;
        Clock::time_point anchor, lastReal;
        double cycleMs = 16.0, workMs = 16.0, preMs = 4.0, waitedMs = 0.0;
        int autoTotal = 1, autoWant = 1;
        Clock::time_point autoSince;
    };
}
