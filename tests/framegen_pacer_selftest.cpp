// Frame generation pacing: each real frame is followed by exactly total-1 generated frames, spaced evenly over
// the measured real-frame cycle, with indices 1..total-1, and a refused present ends the run for that frame.
#include "client/systems/renderer/FrameGenPacer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using namespace perlin;
using Clock = FrameGenPacer::Clock;

namespace {
    int failures = 0;
    void Check(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            failures++;
        }
    }
    double Ms(Clock::duration d) {
        return std::chrono::duration<double, std::milli>(d).count();
    }
}

int main() {
    {
        // unpaced (vsync on): all generated frames go out at once, in order, with the right indices
        FrameGenPacer p;
        for (int total = 2; total <= 4; total++) {
            p.RealFrame(Clock::now(), total, 10.0);
            std::vector<int> seen;
            const int shown = p.Pump(FrameGenPacer::Stage::AfterPresent, false, Clock::now(), [&](int i, int n, double) {
                Check(n == total, "total passed through");
                seen.push_back(i);
                return true;
            });
            Check(shown == total - 1, "total-1 generated frames per real frame");
            bool ordered = static_cast<int>(seen.size()) == total - 1;
            for (size_t k = 0; ordered && k < seen.size(); k++)
                ordered = seen[k] == static_cast<int>(k) + 1;
            Check(ordered, "indices run 1..total-1");
            Check(!p.Pending(), "nothing left after the run");
        }
        Check(p.GeneratedShown() == 1 + 2 + 3, "shown generated frames are counted");
    }
    {
        // off: a total of one never presents anything
        FrameGenPacer p;
        p.RealFrame(Clock::now(), 1, 10.0);
        int calls = 0;
        p.Pump(FrameGenPacer::Stage::AfterPresent, false, Clock::now(), [&](int, int, double) { return ++calls, true; });
        Check(calls == 0 && !p.Pending(), "no generated frames when off");
    }
    {
        // a refused present (no swapchain image) ends this frame's run and is not counted
        FrameGenPacer p;
        p.RealFrame(Clock::now(), 4, 10.0);
        int calls = 0;
        const int shown = p.Pump(FrameGenPacer::Stage::AfterPresent, false, Clock::now(), [&](int i, int, double) {
            calls++;
            return i < 2;
        });
        Check(shown == 1 && calls == 2 && !p.Pending(), "refused present stops the run");
        Check(p.GeneratedShown() == 1, "refused present not counted");
    }
    {
        // paced: at a steady 40 ms real cycle, a 2x generated frame lands about half a cycle after the real one
        FrameGenPacer p;
        const double cycle = 40.0;
        Clock::time_point real = Clock::now();
        std::vector<double> offsets;
        double phaseError = 0;
        for (int frame = 0; frame < 30; frame++) {
            const Clock::time_point due = real + std::chrono::microseconds(static_cast<int64_t>(cycle * 1000));
            FrameGenPacer::SleepUntil(due);
            real = Clock::now();
            p.RealFrame(real, 2, 5.0);
            p.Pump(FrameGenPacer::Stage::AfterPresent, true, real, [&](int, int, double) { return true; });
            if (p.Pending())
                p.Pump(FrameGenPacer::Stage::BeforeRender, true, Clock::now(), [&](int, int, double phase) {
                    const double at = Ms(Clock::now() - real);
                    offsets.push_back(at);
                    if (frame >= 15) // after the cycle estimate settles
                        phaseError = std::max(phaseError, std::fabs(phase * cycle - at));
                    return true;
                });
        }
        double late = 0;
        int counted = 0;
        for (size_t k = 15; k < offsets.size(); k++, counted++)
            late += offsets[k];
        const double mean = counted ? late / counted : 0.0;
        std::printf("paced 2x at %.0f ms: generated frame %.1f ms after the real one\n", cycle, mean);
        Check(counted > 0 && mean > cycle * 0.15 && mean < cycle * 0.75, "generated frame spaced into the cycle");
        // the image is extrapolated to when it is shown, not to a fixed half cycle (that mismatch judders)
        std::printf("largest phase error %.2f ms\n", phaseError);
        Check(phaseError < cycle * 0.1, "phase matches the present time");
    }
    std::printf("%s\n", failures ? "framegen pacer selftest FAILED" : "framegen pacer selftest ok");
    return failures ? 1 : 0;
}
