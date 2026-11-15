#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace perlin::audio {
    struct Pcm {
        int rate = 44100;
        int channels = 1;
        std::vector<int16_t> samples;
        size_t Frames() const { return channels > 0 ? samples.size() / static_cast<size_t>(channels) : 0; }
        double Seconds() const { return rate > 0 ? static_cast<double>(Frames()) / rate : 0.0; }
    };
    using PcmPtr = std::shared_ptr<const Pcm>;

    bool DecodeSound(const std::vector<uint8_t>& bytes, const std::string& extension, Pcm& out,
                     std::string* error = nullptr);
}
