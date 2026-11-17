#include "util/EnvVar.hpp"
#include "client/systems/diagnostics/BenchmarkDriver.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {
    using namespace perlin;
    using Clock = std::chrono::steady_clock;

    struct Effects {
        std::vector<std::string> log;
        void Add(const std::string& s) { log.push_back(s); }
    };

    struct Frame {
        Clock::time_point frameStart;
        bool liveExists, liveInitialized, sessionReady, hasSession;
        double x, y, z;
    };

    struct Original {
        Effects fx;
        std::unique_ptr<bench::Run> benchmark;
        Clock::time_point benchmarkLastFrame{};
        bool benchmarkJoinIssued = false;
        bool benchmarkWorldOnly = false;
        float yaw = 0, pitch = 0;

        void Start(bench::Settings s) {
            benchmark = std::make_unique<bench::Run>(std::move(s));
            benchmarkJoinIssued = false;
            benchmarkLastFrame = {};
        }
        void Hint(const std::string& s) { fx.Add("status:" + s); }
        void Finish() {
            fx.Add("finish");
            benchmark.reset();
        }
        const char* Env(const char* name) { return std::getenv(name); }

        void Update(const Frame& f) {
            if (!benchmark)
                return;
            const auto now = f.frameStart;
            const double frameSeconds = benchmarkLastFrame.time_since_epoch().count() == 0
                                            ? 0.0
                                            : std::chrono::duration<double>(now - benchmarkLastFrame).count();
            benchmarkLastFrame = now;
            bench::Run& run = *benchmark;
            if (run.Current() == bench::Phase::Done)
                return;
            if (!benchmarkJoinIssued) {
                benchmarkJoinIssued = true;
                if (const char* mode = Env("PERLIN_BENCHMARK_WORLD")) {
                    fx.Add("join:" + std::to_string(run.Config().seed) + ":" + std::to_string(std::atoi(mode)));
                    benchmarkWorldOnly = true;
                    return;
                }
                fx.Add("join:" + std::to_string(run.Config().seed) + ":6");
                return;
            }
            if (benchmarkWorldOnly) {
                if (f.liveExists && f.liveInitialized && f.sessionReady && f.hasSession) {
                    if (const char* cmds = Env("PERLIN_BENCHMARK_CMDS")) {
                        std::string list = cmds;
                        size_t start = 0;
                        while (start < list.size()) {
                            const size_t end = list.find(';', start);
                            const std::string c =
                                list.substr(start, end == std::string::npos ? std::string::npos : end - start);
                            if (!c.empty())
                                fx.Add("cmd:" + c);
                            if (end == std::string::npos)
                                break;
                            start = end + 1;
                        }
                    }
                    benchmarkWorldOnly = false;
                    benchmark.reset();
                }
                return;
            }
            if (!run.HasSpawn()) {
                if (f.liveExists && f.liveInitialized && f.sessionReady) {
                    run.Spawned(f.x, f.y, f.z);
                    Hint("Benchmark: warming up");
                }
                return;
            }
            if (!f.liveExists) {
                fx.Add("log:stopped: the world closed");
                Hint("Benchmark stopped: the world closed.");
                benchmark.reset();
                return;
            }
            const bench::Phase before = run.Current();
            const bench::Pose pose = run.Frame(frameSeconds);
            char buf[160];
            std::snprintf(buf, sizeof buf, "pose:%.9g,%.9g,%.9g,%.9g,%.9g", pose.x, pose.y, pose.z, pose.yaw,
                          std::clamp(pose.pitch, -89.0f, 89.0f));
            fx.Add(buf);
            if (before != run.Current() && run.Current() == bench::Phase::Recording)
                Hint("Benchmark: recording");
            if (run.Current() == bench::Phase::Done)
                Finish();
        }
    };

    struct Rewritten {
        Effects fx;
        std::unique_ptr<bench::Driver> benchmark;

        void Start(bench::Settings s) {
            bench::Driver::Environment env;
            if (const char* mode = std::getenv("PERLIN_BENCHMARK_WORLD"))
                env.worldOnlyMode = std::atoi(mode);
            if (const char* cmds = std::getenv("PERLIN_BENCHMARK_CMDS"))
                env.commands = cmds;
            benchmark = std::make_unique<bench::Driver>(std::move(s), std::move(env));
        }
        void Update(const Frame& f) {
            if (!benchmark)
                return;
            bench::Driver::Observation seen;
            seen.frameStart = f.frameStart;
            seen.liveExists = f.liveExists;
            seen.worldLive = f.liveExists && f.liveInitialized;
            seen.worldReady = f.sessionReady;
            seen.hasSession = f.hasSession;
            seen.x = f.x;
            seen.y = f.y;
            seen.z = f.z;
            const bench::Driver::Action action = benchmark->Step(seen);
            if (action.join)
                fx.Add("join:" + std::to_string(action.join->seed) + ":" + std::to_string(action.join->mode));
            for (const std::string& c : action.commands)
                fx.Add("cmd:" + c);
            if (action.log)
                fx.Add("log:" + *action.log);
            if (action.pose) {
                char buf[160];
                std::snprintf(buf, sizeof buf, "pose:%.9g,%.9g,%.9g,%.9g,%.9g", action.pose->x, action.pose->y,
                              action.pose->z, action.pose->yaw, std::clamp(action.pose->pitch, -89.0f, 89.0f));
                fx.Add(buf);
            }
            if (action.status)
                fx.Add("status:" + *action.status);
            if (action.ended)
                benchmark.reset();
            else if (action.finished) {
                fx.Add("finish");
                benchmark.reset();
            }
        }
    };
}

int main() {
    std::mt19937 rng(7);
    int failures = 0;
    long frames = 0, finishes = 0;
    const char* cmdChoices[] = {"", "time set day", "a;b;;c", ";x;", "weather clear;"};
    for (int scenario = 0; scenario < 400; scenario++) {
        const int envMode = rng() % 3;
        if (envMode == 0) {
            perlin::env::Unset("PERLIN_BENCHMARK_WORLD");
        } else {
            perlin::env::Set("PERLIN_BENCHMARK_WORLD", envMode == 1 ? "3" : "12");
        }
        const int cmdMode = rng() % 6;
        if (cmdMode == 5)
            perlin::env::Unset("PERLIN_BENCHMARK_CMDS");
        else
            perlin::env::Set("PERLIN_BENCHMARK_CMDS", cmdChoices[cmdMode]);
        bench::Settings s;
        s.seconds = 0.5 + (rng() % 40) / 10.0;
        s.warmupSeconds = (rng() % 30) / 10.0;
        s.seed = rng();
        Original orig;
        Rewritten fresh;
        orig.Start(s);
        fresh.Start(s);
        auto t = Clock::time_point(std::chrono::seconds(100 + rng() % 1000));
        int steps = 800 + rng() % 800;
        for (int i = 0; i < steps; i++) {
            t += std::chrono::microseconds(1000 + rng() % 40000);
            Frame f{t, rng() % 300 != 0, rng() % 4 != 0, rng() % 4 != 0, rng() % 8 != 0,
                    (rng() % 1000) / 10.0, (rng() % 200) / 10.0, (rng() % 1000) / 10.0};
            if (i < 3)
                f.liveExists = f.liveInitialized = f.sessionReady = true;
            orig.Update(f);
            fresh.Update(f);
            frames++;
            if (orig.fx.log != fresh.fx.log || bool(orig.benchmark) != bool(fresh.benchmark)) {
                failures++;
                std::printf("scenario %d frame %d diverged\n", scenario, i);
                break;
            }
        }
        for (const std::string& e : orig.fx.log)
            if (e == "finish")
                finishes++;
    }
    if (finishes < 20) {
        failures++;
        std::printf("too few finished runs: %ld\n", finishes);
    }
    std::printf("%ld frames, %ld finished runs, %d failures\n", frames, finishes, failures);
    return failures ? 1 : 0;
}
