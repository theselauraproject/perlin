#pragma once
#include <cstdint>
#include <string>
#include <vector>

// benchmark mode: a fixed-seed singleplayer world, a scripted fly-through and the frame times it gave.
// perlin --benchmark [seconds] (or Options > Video > Run Benchmark); results land in <data dir>/benchmarks
namespace perlin::bench {
    struct Settings {
        double seconds = 60.0;       // recorded part of the run
        double warmupSeconds = 10.0; // flying, not recorded: chunks load and pipelines compile
        uint64_t seed = 8675309;     // same world every run
        bool exitWhenDone = false;   // --benchmark from the command line quits after writing the result
        std::string output;          // file to write; empty = <data dir>/benchmarks/<time>.json
    };

    struct Stats {
        size_t frames = 0;
        double seconds = 0;
        double averageFps = 0;
        double low1Fps = 0;  // average fps of the slowest 1% of frames
        double low01Fps = 0; // and of the slowest 0.1%
        double medianMs = 0, p95Ms = 0, p99Ms = 0, worstMs = 0, bestMs = 0;
        size_t hitches = 0; // frames over twice the median and over 33 ms
    };

    // from frame durations in milliseconds
    Stats Compute(std::vector<double> frameMs);

    // where the camera is t seconds into the run, relative to the spawn point: a slow climbing loop over the
    // terrain that turns through every direction (yaw / pitch in degrees as Camera has them: direction
    // (cos yaw cos pitch, sin pitch, sin yaw cos pitch))
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
        // the world is in: the fly-through starts from here
        void Spawned(double x, double y, double z);
        bool HasSpawn() const { return spawned; }
        // one frame of raw wall time; returns the pose to put the camera at (when spawned)
        Pose Frame(double frameSeconds);
        const std::vector<double>& FrameTimes() const { return frameMs; }
        double Elapsed() const { return elapsed; }
        // 0..1 through warmup + recording
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
