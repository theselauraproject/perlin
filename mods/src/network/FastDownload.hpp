#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

namespace perlin::download {
    struct Progress {
        std::atomic<uint64_t> done{0};
        std::atomic<uint64_t> total{0};
        std::atomic<bool> cancel{false};
    };

    struct Options {
        int connections = 8;
        uint64_t chunkBytes = 4ull << 20;
        int timeoutSecs = 30;
    };

    bool Fetch(const std::string& url, const std::filesystem::path& target, Progress* progress = nullptr,
               const Options& options = {});

    std::string Resolve(const std::string& url, int timeoutSecs = 20);
}
