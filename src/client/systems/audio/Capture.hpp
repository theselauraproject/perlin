#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct SDL_AudioStream;

namespace perlin::audio {
    class Capture {
    public:
        Capture() = default;
        ~Capture();
        Capture(const Capture&) = delete;
        Capture& operator=(const Capture&) = delete;

        static std::vector<std::string> Devices();

        bool Start(const std::string& device, int sampleRate, std::string* error = nullptr);
        void Stop();
        bool Active() const { return stream != nullptr; }
        const std::string& Device() const { return device; }
        int SampleRate() const { return rate; }
        size_t Read(std::vector<int16_t>& out);

    private:
        SDL_AudioStream* stream = nullptr;
        std::string device;
        int rate = 0;
        bool initialisedSubsystem = false;
    };
}
