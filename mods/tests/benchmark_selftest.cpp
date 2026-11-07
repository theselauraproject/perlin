// statistics and path of the benchmark mode
#include "client/Benchmark.hpp"

#include <cmath>
#include <cstdio>

using namespace perlin::bench;

namespace {
    int failures = 0;
    void Check(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            failures++;
        }
    }
    bool Near(double a, double b, double eps = 1e-6) {
        return std::fabs(a - b) <= eps;
    }
}

int main() {
    // 990 frames at 10 ms and 10 at 40 ms: 1% low is the 10 slowest = 25 fps
    std::vector<double> ms(990, 10.0);
    ms.insert(ms.end(), 10, 40.0);
    const Stats s = Compute(ms);
    Check(s.frames == 1000, "frame count");
    Check(Near(s.averageFps, 1000.0 * 1000.0 / (990 * 10.0 + 400.0)), "average fps");
    Check(Near(s.low1Fps, 25.0), "1% low");
    Check(Near(s.low01Fps, 25.0), "0.1% low");
    Check(Near(s.medianMs, 10.0), "median");
    Check(Near(s.worstMs, 40.0) && Near(s.bestMs, 10.0), "extremes");
    Check(s.hitches == 10, "hitches");
    Check(Compute({}).frames == 0, "empty");

    // the run warms up, then records exactly the asked span
    Settings set;
    set.seconds = 2.0;
    set.warmupSeconds = 1.0;
    Run run(set);
    Check(run.Current() == Phase::Joining, "starts joining");
    run.Frame(0.016);
    Check(run.FrameTimes().empty(), "nothing before spawn");
    run.Spawned(100, 64, -20);
    int frames = 0;
    while (run.Current() != Phase::Done && frames < 10000) {
        const Pose p = run.Frame(0.01);
        Check(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.yaw), "pose finite");
        frames++;
    }
    Check(run.Current() == Phase::Done, "finishes");
    Check(run.FrameTimes().size() >= 198 && run.FrameTimes().size() <= 202, "records two seconds");

    // the path starts at the spawn point, above it, and is continuous
    const Pose a = PathAt(0.0), b = PathAt(0.01);
    Check(Near(a.x, 0.0) && Near(a.z, 0.0) && a.y > 10.0, "path start");
    Check(std::fabs(a.x - b.x) < 0.2 && std::fabs(a.z - b.z) < 0.2, "path continuous");
    Check(a.pitch < 0.0f, "looks down");

    const std::string json = ToJson(Report{});
    Check(json.find("\"low_1_percent_fps\"") != std::string::npos, "json fields");
    std::printf("%s\n", failures ? "benchmark selftest FAILED" : "benchmark selftest ok");
    return failures ? 1 : 0;
}
