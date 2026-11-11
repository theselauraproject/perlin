#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "client/replay/Ffmpeg.hpp"

namespace perlin::replay::ffmpeg {
    bool LibavCompiledIn();
    Catalog QueryLibav();

    class LibavEncoder {
    public:
        LibavEncoder();
        ~LibavEncoder();
        bool Start(const Settings& s, const Catalog& cat, std::string& error);
        bool WriteFrame(const uint8_t* bgra, size_t bytes);
        bool Finish(std::string& error);
        void Abort();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
