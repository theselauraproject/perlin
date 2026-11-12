// Drives the render auto-tuner with simulated frame times and checks what it picks.
#include "client/renderer/gpu/RenderTuner.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>

using perlin::gpu::RenderTuner;

namespace {
    int failures = 0;
    void Check(bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok)
            failures++;
    }

    // a frame on a pretend machine: occlusion costs 1 ms, face culling saves 0.6 ms, the rest change nothing
    double FrameMs(const RenderTuner& t, std::mt19937& rng, double base) {
        std::normal_distribution<double> noise(0.0, 0.25);
        double ms = base + noise(rng);
        if (t.On(RenderTuner::GpuCull) && t.On(RenderTuner::Occlusion))
            ms += 1.0;
        if (t.On(RenderTuner::GpuCull) && !t.On(RenderTuner::FaceCull))
            ms += 0.6;
        // an occasional hitch, which the medians must ignore
        if (std::uniform_int_distribution<int>(0, 99)(rng) == 0)
            ms += 40.0;
        return ms;
    }

    void Run(RenderTuner& t, std::mt19937& rng, int frames) {
        for (int i = 0; i < frames; i++)
            t.Advance(true, FrameMs(t, rng, 4.0));
    }
} // namespace

int main() {
    const std::string save =
        (std::filesystem::temp_directory_path() / "perlin-render-tune-test.txt").string();
    std::filesystem::remove(save);
    conduit::adapter_info adapter;
    adapter.name = "Test GPU";
    adapter.type = conduit::adapter_type::discrete;

    // first run: everything gets measured
    {
        RenderTuner t;
        t.Init(adapter, save);
        Check(t.On(RenderTuner::Occlusion), "occlusion starts on for a discrete gpu");
        std::mt19937 rng(7);
        // leaving the world before settling starts nothing
        for (int i = 0; i < 300; i++)
            t.Advance(i % 2 == 0, 4.0);
        Check(!t.Testing(), "no trial until the world has settled");
        Run(t, rng, 20000);
        Check(!t.Testing(), "the trials finish");
        Check(t.On(RenderTuner::GpuCull), "gpu culling stays on (no difference)");
        Check(!t.On(RenderTuner::Occlusion), "occlusion goes off (it costs 1 ms)");
        Check(t.On(RenderTuner::FaceCull), "face culling stays on (it saves 0.6 ms)");
        Check(t.On(RenderTuner::ShadowCull) && t.On(RenderTuner::EntityCull) && t.On(RenderTuner::GpuUi),
              "neutral features stay on");
        Check(std::filesystem::exists(save), "the result is saved");
    }
    // second run: the saved result applies at once
    {
        RenderTuner t;
        t.Init(adapter, save);
        Check(!t.On(RenderTuner::Occlusion) && t.On(RenderTuner::FaceCull), "the saved result is loaded");
        std::mt19937 rng(9);
        Run(t, rng, 2000);
        Check(!t.Testing(), "no trials once saved");
    }
    // another gpu gets its own trial
    {
        conduit::adapter_info other = adapter;
        other.name = "Other GPU";
        other.type = conduit::adapter_type::integrated;
        RenderTuner t;
        t.Init(other, save);
        Check(!t.On(RenderTuner::Occlusion), "occlusion starts off on an integrated gpu");
        std::mt19937 rng(11);
        Run(t, rng, 700);
        Check(t.Testing(), "a new gpu is measured");
    }
    // frame time tied by vsync: gpu time decides
    {
        std::vector<RenderTuner::Sample> on, off;
        for (int i = 0; i < 200; i++) {
            on.push_back({16.67, 9.0});
            off.push_back({16.67, 7.5});
        }
        const auto v = RenderTuner::Decide(on, off);
        Check(v.decided && !v.on && v.byGpu, "under vsync a feature that costs gpu time goes off");
        for (auto& s : off)
            s.gpuMs = 8.9;
        Check(RenderTuner::Decide(on, off).on, "a small gpu difference keeps the feature on");
    }
    std::filesystem::remove(save);
    std::printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
