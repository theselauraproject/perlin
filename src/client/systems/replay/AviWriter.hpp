#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace perlin::replay {
    class AviMjpegWriter {
    public:
        ~AviMjpegWriter();
        bool Open(const std::string& path, int width, int height, double fps, int jpegQuality,
                  std::string& error);
        bool AddFrameBgra(const uint8_t* bgra);
        bool Close(std::string& error);
        void Abort();
        int Width() const { return w; }
        int Height() const { return h; }

    private:
        struct Index {
            uint32_t offset, size;
        };
        void Put32(uint32_t v);
        void Put16(uint16_t v);
        void PutTag(const char* t);
        void Patch32(long pos, uint32_t v);
        FILE* f = nullptr;
        int w = 0, h = 0, quality = 90;
        double fps = 30;
        long riffSizePos = 0, moviSizePos = 0, moviStart = 0, avihFramesPos = 0, strhLengthPos = 0,
             strhBufPos = 0;
        uint32_t maxFrame = 0;
        std::vector<Index> index;
        std::vector<uint8_t> rgb;
        std::vector<uint8_t> jpeg;
    };
}
