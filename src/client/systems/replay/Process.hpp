#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace perlin::replay {
    class Process {
    public:
        Process();
        ~Process();
        Process(const Process&) = delete;
        Process& operator=(const Process&) = delete;

        bool Start(const std::vector<std::string>& argv, bool pipeStdin, std::string& error);
        bool Running();
        bool WriteStdin(const void* data, size_t size);
        void CloseStdin();
        int Wait();
        void Kill();
        std::string Output();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    std::string RunCapture(const std::vector<std::string>& argv, int* exitCode = nullptr);
}
