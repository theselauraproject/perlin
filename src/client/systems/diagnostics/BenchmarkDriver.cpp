#include "client/systems/diagnostics/BenchmarkDriver.hpp"

#include <cstdlib>

namespace perlin::bench {
    Driver::Action Driver::Step(const Observation& o) {
        Action action;
        const double frameSeconds = lastFrame.time_since_epoch().count() == 0
                                        ? 0.0
                                        : std::chrono::duration<double>(o.frameStart - lastFrame).count();
        lastFrame = o.frameStart;
        if (run.Current() == Phase::Done)
            return action;
        if (!joinIssued) {
            joinIssued = true;
            if (environment.worldOnlyMode) {
                action.join = JoinRequest{run.Config().seed, *environment.worldOnlyMode};
                worldOnly = true;
                return action;
            }
            action.join = JoinRequest{run.Config().seed, 6};
            return action;
        }
        if (worldOnly) {
            if (o.worldLive && o.worldReady && o.hasSession) {
                const std::string& list = environment.commands;
                size_t start = 0;
                while (start < list.size()) {
                    const size_t end = list.find(';', start);
                    const std::string c =
                        list.substr(start, end == std::string::npos ? std::string::npos : end - start);
                    if (!c.empty())
                        action.commands.push_back(c);
                    if (end == std::string::npos)
                        break;
                    start = end + 1;
                }
                worldOnly = false;
                action.ended = true;
            }
            return action;
        }
        if (!run.HasSpawn()) {
            if (o.worldLive && o.worldReady) {
                run.Spawned(o.x, o.y, o.z);
                action.status = "Benchmark: warming up";
            }
            return action;
        }
        if (!o.liveExists) {
            action.log = "stopped: the world closed";
            action.status = "Benchmark stopped: the world closed.";
            action.ended = true;
            return action;
        }
        const Phase before = run.Current();
        action.pose = run.Frame(frameSeconds);
        if (before != run.Current() && run.Current() == Phase::Recording)
            action.status = "Benchmark: recording";
        if (run.Current() == Phase::Done)
            action.finished = true;
        return action;
    }
}
