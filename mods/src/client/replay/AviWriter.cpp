#include "client/replay/AviWriter.hpp"

#include <algorithm>
#include <cmath>

#include <stb_image_write.h>

namespace perlin::replay {
    AviMjpegWriter::~AviMjpegWriter() {
        Abort();
    }

    void AviMjpegWriter::Put32(uint32_t v) {
        const uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
                              static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
        std::fwrite(b, 1, 4, f);
    }
    void AviMjpegWriter::Put16(uint16_t v) {
        const uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
        std::fwrite(b, 1, 2, f);
    }
    void AviMjpegWriter::PutTag(const char* t) {
        std::fwrite(t, 1, 4, f);
    }
    void AviMjpegWriter::Patch32(long pos, uint32_t v) {
        const long cur = std::ftell(f);
        std::fseek(f, pos, SEEK_SET);
        Put32(v);
        std::fseek(f, cur, SEEK_SET);
    }

    bool AviMjpegWriter::Open(const std::string& path, int width, int height, double framesPerSecond,
                              int jpegQuality, std::string& error) {
        Abort();
        f = std::fopen(path.c_str(), "wb");
        if (!f) {
            error = "cannot create " + path;
            return false;
        }
        w = width;
        h = height;
        fps = framesPerSecond;
        quality = std::clamp(jpegQuality, 1, 100);
        index.clear();
        maxFrame = 0;
        const uint32_t scale = 1000;
        const uint32_t rate = static_cast<uint32_t>(std::lround(fps * 1000.0));
        PutTag("RIFF");
        riffSizePos = std::ftell(f);
        Put32(0);
        PutTag("AVI ");
        PutTag("LIST");
        Put32(4 + 8 + 56 + 8 + 4 + 8 + 56 + 8 + 40);
        PutTag("hdrl");
        PutTag("avih");
        Put32(56);
        Put32(static_cast<uint32_t>(std::lround(1e6 / fps)));
        Put32(0);
        Put32(0);
        Put32(0x10);
        avihFramesPos = std::ftell(f);
        Put32(0);
        Put32(0);
        Put32(1);
        strhBufPos = std::ftell(f);
        Put32(0);
        Put32(static_cast<uint32_t>(w));
        Put32(static_cast<uint32_t>(h));
        for (int i = 0; i < 4; i++)
            Put32(0);
        PutTag("LIST");
        Put32(4 + 8 + 56 + 8 + 40);
        PutTag("strl");
        PutTag("strh");
        Put32(56);
        PutTag("vids");
        PutTag("MJPG");
        Put32(0);
        Put16(0);
        Put16(0);
        Put32(0);
        Put32(scale);
        Put32(rate);
        Put32(0);
        strhLengthPos = std::ftell(f);
        Put32(0);
        Put32(0);
        Put32(0xFFFFFFFFu);
        Put32(0);
        Put16(0);
        Put16(0);
        Put16(static_cast<uint16_t>(w));
        Put16(static_cast<uint16_t>(h));
        PutTag("strf");
        Put32(40);
        Put32(40);
        Put32(static_cast<uint32_t>(w));
        Put32(static_cast<uint32_t>(h));
        Put16(1);
        Put16(24);
        PutTag("MJPG");
        Put32(static_cast<uint32_t>(w * h * 3));
        Put32(0);
        Put32(0);
        Put32(0);
        Put32(0);
        PutTag("LIST");
        moviSizePos = std::ftell(f);
        Put32(0);
        PutTag("movi");
        moviStart = std::ftell(f) - 4;
        return std::ferror(f) == 0;
    }

    bool AviMjpegWriter::AddFrameBgra(const uint8_t* bgra) {
        if (!f)
            return false;
        rgb.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
        const size_t pixels = static_cast<size_t>(w) * static_cast<size_t>(h);
        for (size_t i = 0; i < pixels; i++) {
            rgb[i * 3] = bgra[i * 4 + 2];
            rgb[i * 3 + 1] = bgra[i * 4 + 1];
            rgb[i * 3 + 2] = bgra[i * 4];
        }
        jpeg.clear();
        stbi_write_jpg_to_func(
            [](void* ctx, void* data, int size) {
                auto* v = static_cast<std::vector<uint8_t>*>(ctx);
                v->insert(v->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
            },
            &jpeg, w, h, 3, rgb.data(), quality);
        if (jpeg.empty())
            return false;
        const long chunkPos = std::ftell(f);
        if (static_cast<unsigned long>(chunkPos) + jpeg.size() + 4096 + index.size() * 16 > 0xFFFFFFF0ul)
            return false;
        PutTag("00dc");
        Put32(static_cast<uint32_t>(jpeg.size()));
        std::fwrite(jpeg.data(), 1, jpeg.size(), f);
        if (jpeg.size() & 1)
            std::fputc(0, f);
        index.push_back({static_cast<uint32_t>(chunkPos - moviStart), static_cast<uint32_t>(jpeg.size())});
        maxFrame = std::max(maxFrame, static_cast<uint32_t>(jpeg.size()));
        return std::ferror(f) == 0;
    }

    bool AviMjpegWriter::Close(std::string& error) {
        if (!f)
            return false;
        const long moviEnd = std::ftell(f);
        PutTag("idx1");
        Put32(static_cast<uint32_t>(index.size() * 16));
        for (const Index& e : index) {
            PutTag("00dc");
            Put32(0x10);
            Put32(e.offset);
            Put32(e.size);
        }
        const long end = std::ftell(f);
        Patch32(moviSizePos, static_cast<uint32_t>(moviEnd - moviSizePos - 4));
        Patch32(riffSizePos, static_cast<uint32_t>(end - 8));
        Patch32(avihFramesPos, static_cast<uint32_t>(index.size()));
        Patch32(strhLengthPos, static_cast<uint32_t>(index.size()));
        Patch32(strhBufPos, maxFrame);
        const bool ok = std::ferror(f) == 0;
        std::fclose(f);
        f = nullptr;
        if (!ok)
            error = "write error";
        return ok;
    }

    void AviMjpegWriter::Abort() {
        if (f) {
            std::fclose(f);
            f = nullptr;
        }
    }
}
