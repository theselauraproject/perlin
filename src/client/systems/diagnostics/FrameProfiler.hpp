#pragma once
#include "util/Env.hpp"
#include "util/Log.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace perlin::frame {
        class Profiler {
        public:
            enum Stage {
                Logic,
                LevelUpdate,
                LevelRender,
                Entities,
                Hand,
                Hud,
                EndFrame,
                Frame,
                Particles,
                Weather,
                Input,
                Session,
                Prep,
                Tick,
                Gpu,
                kStages
            };

            static Profiler& Get() {
                static Profiler profiler;
                return profiler;
            }
            bool Enabled() const { return enabled; }
            void Add(Stage stage, double ms) {
                total[stage] += ms;
                last[stage] += ms;
            }

            void AddToAverage(Stage stage, double ms) { total[stage] += ms; }

            bool FinishFrame(std::chrono::steady_clock::time_point frameStart) {
                if (!enabled)
                    return false;
                const double frameMs =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart)
                        .count();
                total[Frame] += frameMs;
                last[Frame] = frameMs;
                if (typical > 0.0 && frameMs > typical * 2.0 && frameMs > typical + 6.0) {
                    std::string line = "hitch " + std::to_string(static_cast<int>(frameMs)) + " ms (typical " +
                                       std::to_string(static_cast<int>(typical)) + "):";
                    char buf[64];
                    for (int i = 0; i < kStages; i++)
                        if (i != Frame && last[i] >= 1.0) {
                            std::snprintf(buf, sizeof(buf), " %s=%.1f", kNames[i], last[i]);
                            line += buf;
                        }
                    Log("profile", "{}", line);
                }
                typical = typical <= 0.0 ? frameMs : typical * 0.95 + std::min(frameMs, typical * 3.0) * 0.05;
                for (int i = 0; i < kStages; i++) {
                    worst[i] = std::max(worst[i], last[i]);
                    last[i] = 0;
                }
                frameTimes.push_back(frameMs);
                if (++frames < 240)
                    return false;
                Report();
                return true;
            }

        private:
            void Report() {
                std::ranges::sort(frameTimes);
                const size_t n = frameTimes.size();
                const double p99 = frameTimes[std::min(n - 1, static_cast<size_t>(n * 0.99))];
                char buf[96];
                std::string line = "avg ms:";
                for (int i = 0; i < kStages; i++) {
                    std::snprintf(buf, sizeof(buf), " %s=%.2f", kNames[i], total[i] / frames);
                    line += buf;
                }
                line += " | worst ms:";
                for (int i = 0; i < kStages; i++) {
                    std::snprintf(buf, sizeof(buf), " %s=%.2f", kNames[i], worst[i]);
                    line += buf;
                }
                std::snprintf(buf, sizeof(buf), " | avg %.1f fps, 1%% low %.1f fps",
                              total[Frame] > 0 ? 1000.0 * frames / total[Frame] : 0.0, p99 > 0 ? 1000.0 / p99 : 0.0);
                line += buf;
                Log("profile", "{}", line);
                std::ranges::fill(total, 0.0);
                std::ranges::fill(worst, 0.0);
                frameTimes.clear();
                frames = 0;
            }

            static constexpr const char* kNames[kStages] = {
                "logic",     "level update", "level render", "entities", "hand", "hud",  "end frame", "frame",
                "particles", "weather",      "input",        "session",  "prep", "tick", "gpu"};
            bool enabled = PERLIN_ENV("PERLIN_PROFILE") != nullptr;
            double total[kStages] = {};
            double worst[kStages] = {};
            double last[kStages] = {};
            double typical = 0.0;
            std::vector<double> frameTimes;
            int frames = 0;
        };

        class ProfileScope {
        public:
            explicit ProfileScope(Profiler::Stage stage) : stage(stage) {
                if (Profiler::Get().Enabled())
                    start = std::chrono::steady_clock::now();
            }
            ~ProfileScope() {
                if (Profiler::Get().Enabled())
                    Profiler::Get().Add(stage, std::chrono::duration<double, std::milli>(
                                                   std::chrono::steady_clock::now() - start)
                                                   .count());
            }
            ProfileScope(const ProfileScope&) = delete;
            ProfileScope& operator=(const ProfileScope&) = delete;

        private:
            Profiler::Stage stage;
            std::chrono::steady_clock::time_point start;
        };
}
