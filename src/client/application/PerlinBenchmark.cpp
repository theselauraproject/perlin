#include <cstdlib>
#include "util/Env.hpp"
#include "client/application/PerlinFrame.hpp"
#include "client/application/PerlinInternal.hpp"
#include "client/services/update/Updater.hpp"
#include "util/Log.hpp"
#include "util/Platform.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>

namespace perlin {
    void Perlin::StartBenchmark(bench::Settings settings) {
#ifdef PERLIN_SINGLEPLAYER
        if (benchmark && benchmark->Active())
            return;
        Log("benchmark", "starting: {} s recorded after {} s of warmup, seed {}", settings.seconds,
            settings.warmupSeconds, settings.seed);

        if (session && hud)
            LeaveToServerList(*hud);
        if (singleplayerServer)
            singleplayerServer->Stop();
        singleplayerServer.reset();
        std::error_code ec;
        std::filesystem::remove_all(paths::DataDir() + "/benchmark-world", ec);
        bench::Driver::Environment environment;
        if (const char* mode = PERLIN_ENV("PERLIN_BENCHMARK_WORLD"))
            environment.worldOnlyMode = std::atoi(mode);
        if (const char* cmds = PERLIN_ENV("PERLIN_BENCHMARK_CMDS"))
            environment.commands = cmds;
        benchmark = std::make_unique<bench::Driver>(std::move(settings), std::move(environment));
#else
        (void)settings;
        SetStatusHint("This build has no singleplayer, which the benchmark needs.");
#endif
    }

    void Perlin::UpdateBenchmark(FrameContext& f) {
        if (!benchmark)
            return;
        bench::Driver::Observation seen;
        seen.frameStart = f.frameStart;
        seen.liveExists = f.live != nullptr;
        seen.worldLive = f.live && f.live->Initialized();
        seen.worldReady = sessionReady;
        seen.hasSession = session != nullptr;
        const physics::State& s = player.State();
        seen.x = s.x;
        seen.y = s.y;
        seen.z = s.z;
        {
            const uint64_t shownNow = fgPacer.GeneratedShown();
            seen.generatedFrames = static_cast<int>(shownNow - benchmarkGeneratedSeen);
            benchmarkGeneratedSeen = shownNow;
        }
        fgPacer.TrackPacing(true);
        const bench::Driver::Action action = benchmark->Step(seen);
        if (benchmark->Active())
            benchmark->Shown(fgPacer.TakeShownIntervals());
        if (action.join)
            JoinSingleplayer(paths::DataDir() + "/benchmark-world", action.join->seed, action.join->mode, false);
        for (const std::string& command : action.commands)
            session->SendCommand(command);
        if (action.log)
            Log("benchmark", "{}", *action.log);
        if (action.pose) {
            player.ModTeleport(action.pose->x, action.pose->y, action.pose->z, false);
            camera.yaw = action.pose->yaw;
            camera.pitch = std::clamp(action.pose->pitch, -89.0f, 89.0f);
            camera.mode = PerspectiveMode::FirstPerson;
        }
        if (action.status)
            SetStatusHint(*action.status);
        if (action.ended)
            benchmark.reset();
        else if (action.finished)
            FinishBenchmark();
    }

    void Perlin::FinishBenchmark() {
        if (!benchmark)
            return;
        bench::Report report;
        report.settings = benchmark->Current().Config();
        report.stats = bench::Compute(benchmark->Current().FrameTimes(), benchmark->Current().GeneratedFrames(),
                                      benchmark->Current().ShownIntervals());
        report.build = update::BuildLabel();
        report.platform = platform::kName;
        if (renderer) {
            report.renderer = renderer->RendererName();
            report.gpu = renderer->GpuDescription();
            report.shaderPack = renderer->JavaShaderPackActive() ? options.shaderPack : std::string();
        }
        int w = 0, h = 0;
        replayStudio.FramebufferSizeEffective(w, h);
        report.width = w;
        report.height = h;
        report.renderDistance = options.renderDistance;
        report.vsync = options.vsync;
        report.maxFps = options.maxFps;
        report.frameGen = std::max(1, fgActive);
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
