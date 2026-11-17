#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace perlin::bench {
    struct Settings {
        double seconds = 60.0;
        double warmupSeconds = 10.0;
        uint64_t seed = 8675309;
        bool exitWhenDone = false;
        std::string output;
    };

    struct Stats {
        size_t frames = 0;
        double seconds = 0;
        double averageFps = 0;
        double low1Fps = 0;
        double low01Fps = 0;
        double medianMs = 0, p95Ms = 0, p99Ms = 0, worstMs = 0, bestMs = 0;
        size_t hitches = 0;
    };

    Stats Compute(std::vector<double> frameMs);

    struct Pose {
        double x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0;
    };
    Pose PathAt(double t);

    struct Report {
        Settings settings;
        Stats stats;
        std::string build, platform, renderer, gpu, shaderPack;
        int width = 0, height = 0, renderDistance = 0;
        float renderScale = 1.0f;
        bool vsync = false;
        int maxFps = 0;
    };
    std::string ToJson(const Report& report);
    std::string Summary(const Stats& stats);

    enum class Phase { Idle, Joining, Warmup, Recording, Done };

    class Run {
    public:
        explicit Run(Settings s) : settings(std::move(s)) {}
        const Settings& Config() const { return settings; }
        Phase Current() const { return phase; }

        void Spawned(double x, double y, double z);
        bool HasSpawn() const { return spawned; }

        Pose Frame(double frameSeconds);
        const std::vector<double>& FrameTimes() const { return frameMs; }
        double Elapsed() const { return elapsed; }

        float Progress() const;

    private:
        Settings settings;
        Phase phase = Phase::Joining;
        bool spawned = false;
        double sx = 0, sy = 0, sz = 0;
        double elapsed = 0;
        std::vector<double> frameMs;
    };
}
