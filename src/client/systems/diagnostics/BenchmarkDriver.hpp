#pragma once
#include "client/systems/diagnostics/Benchmark.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace perlin::bench {
    class Driver {
    public:
        using TimePoint = std::chrono::steady_clock::time_point;

        struct Environment {
            std::optional<int> worldOnlyMode;
            std::string commands;
        };

        struct Observation {
            TimePoint frameStart;
            bool liveExists = false;
            bool worldLive = false;
            bool worldReady = false;
            bool hasSession = false;
            double x = 0, y = 0, z = 0;
        };

        struct JoinRequest {
            uint64_t seed = 0;
            int mode = 0;
        };

        struct Action {
            std::optional<JoinRequest> join;
            std::vector<std::string> commands;
            std::optional<std::string> status;
            std::optional<std::string> log;
            std::optional<Pose> pose;
            bool finished = false;
            bool ended = false;
        };

        Driver(Settings settings, Environment environment)
            : run(std::move(settings)), environment(std::move(environment)) {}

        const Run& Current() const { return run; }
        bool Active() const { return run.Current() != Phase::Done; }

        Action Step(const Observation& observation);

    private:
        Run run;
        Environment environment;
        TimePoint lastFrame{};
        bool joinIssued = false;
        bool worldOnly = false;
    };
}
