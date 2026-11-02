#include "client/Benchmark.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace perlin::bench {
    namespace {
        double Percentile(const std::vector<double>& sorted, double p) {
            if (sorted.empty())
                return 0.0;
            const double at = p * static_cast<double>(sorted.size() - 1);
            const size_t lo = static_cast<size_t>(std::floor(at));
            const size_t hi = std::min(sorted.size() - 1, lo + 1);
            const double f = at - static_cast<double>(lo);
            return sorted[lo] * (1.0 - f) + sorted[hi] * f;
        }

        // the "x% low" of benchmarking tools: average fps over the slowest x% of frames (at least one frame)
        double LowFps(const std::vector<double>& sortedMs, double fraction) {
            if (sortedMs.empty())
                return 0.0;
            const size_t n = std::max<size_t>(1, static_cast<size_t>(std::ceil(sortedMs.size() * fraction)));
            double ms = 0;
            for (size_t i = sortedMs.size() - n; i < sortedMs.size(); i++)
                ms += sortedMs[i];
            ms /= static_cast<double>(n);
            return ms > 0 ? 1000.0 / ms : 0.0;
        }

        std::string Escape(const std::string& s) {
            std::string out;
            for (char c : s) {
                if (c == '"' || c == '\\') {
                    out += '\\';
                    out += c;
                } else if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
            }
            return out;
        }

        std::string Num(double v, int digits = 2) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.*f", digits, std::isfinite(v) ? v : 0.0);
            return buf;
        }
    }

    Stats Compute(std::vector<double> frameMs) {
        Stats s;
        std::erase_if(frameMs, [](double v) { return !(v > 0.0) || !std::isfinite(v); });
        s.frames = frameMs.size();
        if (frameMs.empty())
            return s;
        const double total = std::accumulate(frameMs.begin(), frameMs.end(), 0.0);
        s.seconds = total / 1000.0;
        s.averageFps = total > 0 ? 1000.0 * static_cast<double>(frameMs.size()) / total : 0.0;
        std::vector<double> sorted = frameMs;
        std::sort(sorted.begin(), sorted.end());
        s.bestMs = sorted.front();
        s.worstMs = sorted.back();
        s.medianMs = Percentile(sorted, 0.5);
        s.p95Ms = Percentile(sorted, 0.95);
        s.p99Ms = Percentile(sorted, 0.99);
        s.low1Fps = LowFps(sorted, 0.01);
        s.low01Fps = LowFps(sorted, 0.001);
        for (double ms : frameMs)
            if (ms > 2.0 * s.medianMs && ms > 33.3)
                s.hitches++;
        return s;
    }

    Pose PathAt(double t) {
        // a 96 block wide loop at 8 blocks a second, rising and falling 12 blocks, looking ahead and slightly down
        constexpr double kPi = 3.14159265358979323846;
        constexpr double radius = 96.0, speed = 8.0;
        const double angle = t * speed / radius;
        Pose p;
        p.x = radius * std::sin(angle);
        p.z = radius - radius * std::cos(angle);
        p.y = 24.0 + 12.0 * std::sin(t * 0.21);
        // heading along the circle: d/dt (sin, 1 - cos) = (cos, sin), swaying left and right; looking down a little
        const double dx = std::cos(angle), dz = std::sin(angle);
        p.yaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / kPi + 25.0 * std::sin(t * 0.37));
        p.pitch = static_cast<float>(-12.0 - 10.0 * std::sin(t * 0.29));
        return p;
    }

    void Run::Spawned(double x, double y, double z) {
        if (spawned)
            return;
        spawned = true;
        sx = x;
        sy = y;
        sz = z;
        phase = Phase::Warmup;
        elapsed = 0;
    }

    Pose Run::Frame(double frameSeconds) {
        Pose pose;
        if (!spawned || phase == Phase::Done)
            return pose;
        elapsed += frameSeconds;
        if (phase == Phase::Warmup && elapsed >= settings.warmupSeconds)
            phase = Phase::Recording;
        if (phase == Phase::Recording) {
            frameMs.push_back(frameSeconds * 1000.0);
            if (elapsed >= settings.warmupSeconds + settings.seconds)
                phase = Phase::Done;
        }
        pose = PathAt(elapsed);
        pose.x += sx;
        pose.y += sy;
        pose.z += sz;
        return pose;
    }

    float Run::Progress() const {
        const double total = settings.warmupSeconds + settings.seconds;
        return total > 0 ? static_cast<float>(std::clamp(elapsed / total, 0.0, 1.0)) : 1.0f;
    }

    std::string Summary(const Stats& s) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "%.0f fps average, %.0f fps 1%% low, %.0f fps 0.1%% low (%zu frames, %zu hitches)",
                      s.averageFps, s.low1Fps, s.low01Fps, s.frames, s.hitches);
        return buf;
    }

    std::string ToJson(const Report& r) {
        const Stats& s = r.stats;
        std::string j = "{\n";
        j += "  \"version\": 1,\n";
        j += "  \"build\": \"" + Escape(r.build) + "\",\n";
        j += "  \"platform\": \"" + Escape(r.platform) + "\",\n";
        j += "  \"renderer\": \"" + Escape(r.renderer) + "\",\n";
        j += "  \"gpu\": \"" + Escape(r.gpu) + "\",\n";
        j += "  \"shader_pack\": \"" + Escape(r.shaderPack) + "\",\n";
        j += "  \"resolution\": [" + std::to_string(r.width) + ", " + std::to_string(r.height) + "],\n";
        j += "  \"render_scale\": " + Num(r.renderScale) + ",\n";
        j += "  \"render_distance\": " + std::to_string(r.renderDistance) + ",\n";
        j += "  \"vsync\": " + std::string(r.vsync ? "true" : "false") + ",\n";
        j += "  \"max_fps\": " + std::to_string(r.maxFps) + ",\n";
        j += "  \"seed\": " + std::to_string(r.settings.seed) + ",\n";
        j += "  \"warmup_seconds\": " + Num(r.settings.warmupSeconds, 1) + ",\n";
        j += "  \"frames\": " + std::to_string(s.frames) + ",\n";
        j += "  \"seconds\": " + Num(s.seconds, 3) + ",\n";
        j += "  \"average_fps\": " + Num(s.averageFps) + ",\n";
        j += "  \"low_1_percent_fps\": " + Num(s.low1Fps) + ",\n";
        j += "  \"low_0_1_percent_fps\": " + Num(s.low01Fps) + ",\n";
        j += "  \"frame_ms\": {\"best\": " + Num(s.bestMs, 3) + ", \"median\": " + Num(s.medianMs, 3) +
             ", \"p95\": " + Num(s.p95Ms, 3) + ", \"p99\": " + Num(s.p99Ms, 3) +
             ", \"worst\": " + Num(s.worstMs, 3) + "},\n";
        j += "  \"hitches\": " + std::to_string(s.hitches) + "\n";
        j += "}\n";
        return j;
    }
}
