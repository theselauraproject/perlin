#pragma once
#include "network/FastDownload.hpp"

#include <atomic>
#include <cstdint>

namespace perlin::assets {
    enum class Stage : int { Idle, Checking, Downloading, Extracting, Done, Failed };

    struct Job {
        std::atomic<Stage> stage{Stage::Idle};
        download::Progress bytes;
        std::atomic<uint64_t> filesDone{0};
        std::atomic<uint64_t> filesTotal{0};
    };

    inline Job& Samples() {
        static Job job;
        return job;
    }

    inline Job& Client() {
        static Job job;
        return job;
    }

    inline bool Busy(const Job& job) {
        const Stage s = job.stage.load();
        return s == Stage::Checking || s == Stage::Downloading || s == Stage::Extracting;
    }
}
