#include <cstdlib>
#include "util/Env.hpp"
#include "client/PerlinFrame.hpp"
#include "client/PerlinInternal.hpp"
#include "client/update/Updater.hpp"
#include "util/Log.hpp"
#include "util/Platform.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>

// benchmark mode: joins a fresh fixed-seed singleplayer world in spectator mode, flies a scripted loop and records
// every frame's wall time (Benchmark.hpp has the path, the statistics and the report)
namespace perlin {
    void Perlin::StartBenchmark(bench::Settings settings) {
#ifdef PERLIN_SINGLEPLAYER
        if (benchmark && benchmark->Current() != bench::Phase::Done)
            return;
        Log("benchmark", "starting: {} s recorded after {} s of warmup, seed {}", settings.seconds,
            settings.warmupSeconds, settings.seed);
        // a world of its own, made new each run so the generated terrain is the same every time
        if (session && hud)
            LeaveToServerList(*hud);
        if (singleplayerServer)
            singleplayerServer->Stop();
        singleplayerServer.reset();
        std::error_code ec;
        std::filesystem::remove_all(paths::DataDir() + "/benchmark-world", ec);
        benchmark = std::make_unique<bench::Run>(std::move(settings));
        benchmarkJoinIssued = false;
        benchmarkLastFrame = {};
#else
        (void)settings;
        SetStatusHint("This build has no singleplayer, which the benchmark needs.");
#endif
    }

    void Perlin::UpdateBenchmark(FrameContext& f) {
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
            // spectator: no gravity, no collisions, nothing in the hand
            benchmarkJoinIssued = true;
            // PERLIN_BENCHMARK_WORLD=<gamemode>: only open the benchmark world (developer testing with
            // --control-port), no camera path and no report
            if (const char* mode = PERLIN_ENV("PERLIN_BENCHMARK_WORLD")) {
                JoinSingleplayer(paths::DataDir() + "/benchmark-world", run.Config().seed, std::atoi(mode));
                benchmarkWorldOnly = true;
                return;
            }
            JoinSingleplayer(paths::DataDir() + "/benchmark-world", run.Config().seed, 6);
            return;
        }
        if (benchmarkWorldOnly) {
            // developer world: once in, run PERLIN_BENCHMARK_CMDS ("/time set noon;/tp @s 0 80 0") and hand over
            if (f.live && f.live->Initialized() && sessionReady && session) {
                if (const char* cmds = PERLIN_ENV("PERLIN_BENCHMARK_CMDS")) {
                    std::string list = cmds;
                    size_t start = 0;
                    while (start < list.size()) {
                        const size_t end = list.find(';', start);
                        const std::string c =
                            list.substr(start, end == std::string::npos ? std::string::npos : end - start);
                        if (!c.empty())
                            session->SendCommand(c);
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
            if (f.live && f.live->Initialized() && sessionReady) {
                const physics::State& s = player.State();
                run.Spawned(s.x, s.y, s.z);
                SetStatusHint("Benchmark: warming up");
            }
            return;
        }
        if (!f.live) {
            // the world went away under the run
            Log("benchmark", "stopped: the world closed");
            SetStatusHint("Benchmark stopped: the world closed.");
            benchmark.reset();
            return;
        }
        const bench::Phase before = run.Current();
        const bench::Pose pose = run.Frame(frameSeconds);
        player.ModTeleport(pose.x, pose.y, pose.z, false);
        camera.yaw = pose.yaw;
        camera.pitch = std::clamp(pose.pitch, -89.0f, 89.0f);
        camera.mode = PerspectiveMode::FirstPerson;
        if (before != run.Current() && run.Current() == bench::Phase::Recording)
            SetStatusHint("Benchmark: recording");
        if (run.Current() == bench::Phase::Done)
            FinishBenchmark();
    }

    void Perlin::FinishBenchmark() {
        if (!benchmark)
            return;
        bench::Report report;
        report.settings = benchmark->Config();
        report.stats = bench::Compute(benchmark->FrameTimes());
        report.build = update::BuildLabel();
        report.platform = platform::kName;
        if (renderer) {
            report.renderer = renderer->RendererName();
            report.gpu = renderer->GpuDescription();
            report.shaderPack = renderer->JavaShaderPackActive() ? options.shaderPack : std::string();
        }
        int w = 0, h = 0;
        FramebufferSizeEffective(w, h);
        report.width = w;
        report.height = h;
        report.renderDistance = options.renderDistance;
        report.vsync = options.vsync;
        report.maxFps = options.maxFps;
        const std::string json = bench::ToJson(report);
        std::string file = report.settings.output;
        if (file.empty()) {
            const std::string dir = paths::DataDir() + "/benchmarks";
            paths::EnsureDir(dir);
            const std::time_t t = std::time(nullptr);
            char stamp[32];
            std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H.%M.%S", std::localtime(&t));
            file = dir + "/" + stamp + ".json";
        }
        std::ofstream(file, std::ios::binary | std::ios::trunc) << json;
        const std::string summary = bench::Summary(report.stats);
        Log("benchmark", "{} -> {}", summary, file);
        std::printf("%s", json.c_str());
        std::fflush(stdout);
        SetStatusHint("Benchmark: " + summary);
        const bool exit = report.settings.exitWhenDone;
        benchmark.reset();
        if (exit && platform)
            platform->isRunning = false;
    }
}
