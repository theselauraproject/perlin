#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace perlin::replay::ffmpeg {
    class Downloader {
    public:
        ~Downloader();
        static bool Supported();
        void Start();
        bool Running() const { return running.load(); }
        bool Finished() const { return finished.load(); }
        bool Succeeded() const { return succeeded.load(); }
        float Progress() const;
        std::string Status();
        void Cancel() { cancel = true; }
        void Join();

    private:
        void Run();
        std::atomic<bool> running{false}, finished{false}, succeeded{false}, cancel{false};
        std::atomic<long long> done{0}, total{0};
        std::mutex mutex;
        std::string status;
        std::thread worker;
    };
}
