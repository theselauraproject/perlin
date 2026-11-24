#pragma once
#include <algorithm>
#include <chrono>
#include <utility>
#include <functional>
#include <vector>

namespace perlin {
    class FrameGenPacer {
    public:
        using Clock = std::chrono::steady_clock;
        enum class Stage { AfterPresent, BeforeRender };
        // phase is where in the real-frame cycle the generated frame is actually shown: paced frames are extrapolated
        // to the moment they present, so a frame that goes out early or late still lines up with real motion
        using Present = std::function<bool(int index, int total, double phase)>;

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
        // generated frames shown since start; lets the benchmark and counters see what reached the screen
        uint64_t GeneratedShown() const { return generatedShown; }

        static void SleepUntil(Clock::time_point due);

        // Milliseconds between consecutive frames reaching the screen, real or generated, since the last take:
        // the pacing a player sees. Kept only once asked for (the benchmark), at most a few thousand.
        void TrackPacing(bool on) { tracking = on; }
        std::vector<double> TakeShownIntervals() { return std::exchange(shownIntervals, {}); }
        // How far into a generated frame's interval the pre-render wait may run, as a fraction of it (0 = present
        // as soon as possible, 1 = wait the whole interval). Waiting spaces frames evenly but delays the next real
        // frame by as much. Default 0.5; PERLIN_FG_WAIT overrides it.
        void SetWaitFraction(double f) { waitFraction = std::clamp(f, 0.0, 1.0); }

    private:
        int total = 1, next = 0;
        uint64_t generatedShown = 0;
        bool tracking = false;
        std::vector<double> shownIntervals;
        Clock::time_point lastShown;
        double waitFraction = 0.5;
        void NoteShown(Clock::time_point t);
        Clock::time_point anchor, lastReal;
        double cycleMs = 16.0, workMs = 16.0, preMs = 4.0, waitedMs = 0.0;
        int autoTotal = 1, autoWant = 1;
        Clock::time_point autoSince;
    };
}
