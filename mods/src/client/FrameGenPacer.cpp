#include "client/FrameGenPacer.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

namespace perlin {
    namespace {
        double Ms(FrameGenPacer::Clock::duration d) {
            return std::chrono::duration<double, std::milli>(d).count();
        }
        FrameGenPacer::Clock::duration FromMs(double ms) {
            return std::chrono::duration_cast<FrameGenPacer::Clock::duration>(
                std::chrono::duration<double, std::milli>(ms));
        }
    }

    void FrameGenPacer::SleepUntil(Clock::time_point due) {
        if (due - Clock::now() > std::chrono::microseconds(1500))
            std::this_thread::sleep_until(due - std::chrono::milliseconds(1));
        while (Clock::now() < due)
            std::this_thread::yield();
    }

    void FrameGenPacer::RealFrame(Clock::time_point now, int frames, double work) {
        workMs += (std::clamp(work, 0.5, 250.0) - workMs) * 0.1;
        if (lastReal != Clock::time_point{}) {
            const double cycle = Ms(now - lastReal);
            if (cycle > 0.5 && cycle < 250.0)
                cycleMs += (cycle - cycleMs) * 0.15;
            else
                cycleMs = workMs;
        }
        lastReal = anchor = now;
        total = std::clamp(frames, 1, 4);
        next = total > 1 ? 1 : 0;
    }

    int FrameGenPacer::Pump(Stage stage, bool paced, Clock::time_point idleUntil, const Present& present) {
        int shown = 0;
        const double interval = cycleMs / static_cast<double>(std::max(1, total));
        while (next > 0 && next < total) {
            const Clock::time_point due = anchor + FromMs(interval * next);
            const Clock::time_point now = Clock::now();
            if (paced && due > now) {
                if (stage == Stage::AfterPresent) {
                    if (due > idleUntil) {
                        const double late = Ms(now - due) + preMs;
                        if (late <= interval * 0.25)
                            break;
                    }
                    SleepUntil(due);
                } else {
                    const Clock::time_point until = std::min(due, now + FromMs(interval * 0.5));
                    SleepUntil(until);
                    waitedMs += Ms(Clock::now() - now);
                }
            }
            if (!present(next, total)) {
                next = 0;
                break;
            }
            shown++;
            next++;
        }
        if (next >= total)
            next = 0;
        return shown;
    }

    int FrameGenPacer::AutoTotal(double targetHz, Clock::time_point now) {
        const double baseFps = 1000.0 / std::max(0.5, workMs);
        const int want = baseFps < 15.0 || targetHz <= 0.0
                             ? 1
                             : std::clamp(static_cast<int>(std::ceil(targetHz / baseFps - 0.2)), 1, 4);
        if (want == autoTotal) {
            autoWant = want;
            return autoTotal;
        }
        if (want != autoWant) {
            autoWant = want;
            autoSince = now;
        } else if (now - autoSince > std::chrono::milliseconds(1500)) {
            autoTotal = want;
        }
        return autoTotal;
    }
}
