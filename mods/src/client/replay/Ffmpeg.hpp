#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "client/replay/AviWriter.hpp"
#include "client/replay/Process.hpp"
#include <utility>

namespace perlin::replay::ffmpeg {
    struct Codec {
        std::string id;
        std::string label;
        std::string encoder;
        std::vector<std::string> fallbackEncoders;
        bool lossless = false;
        bool hasAlpha = false;
        bool usesCrf = true;
    };
    struct Container {
        std::string id;
        std::string label;
        std::string ext;
        std::string muxer;
        bool imageSequence = false;
        std::vector<std::string> codecs;
    };
    struct Catalog {
        bool available = false;
        std::string path;
        std::string version;
        bool builtin = false;
        std::vector<std::string> encoders;
        std::vector<std::string> muxers;
        bool HasEncoder(const std::string& e) const;
        bool HasMuxer(const std::string& m) const;
    };

    const std::vector<Codec>& Codecs();
    const std::vector<Container>& Containers();
    const Codec* FindCodec(const std::string& id);
    const Container* FindContainer(const std::string& id);

    std::string FindExecutable();
    Catalog Query(const std::string& path);

    struct Settings {
        std::string ffmpegPath;
        std::string container = "mp4";
        std::string codec = "h264";
        std::string encoderOverride;
        std::string muxerOverride;
        std::string extensionOverride;
        int width = 1920, height = 1080;
        int inputWidth = 0, inputHeight = 0;
        double fps = 60.0;
        int crf = 20;
        int bitrateKbps = 0;
        std::string preset;
        std::string pixelFormat;
        bool transparent = false;
        int gifColors = 256;
        std::string extraArgs;
        std::string outputPath;
        std::string audioPath;
    };

    bool BuildArgs(const Settings& s, const Catalog& cat, std::vector<std::string>& argv, std::string& error);
    std::vector<std::string> SplitArgs(const std::string& line);

    struct EncodePlan {
        std::string encoder, muxer, pixFmt, codecTag;
        std::vector<std::pair<std::string, std::string>> options;
        int qscale = -1;
        bool imageSequence = false;
        bool tagColors = false;
        bool gif = false;
    };
    bool PlanEncode(const Settings& s, const Catalog& cat, EncodePlan& plan, std::string& error);

    bool WritePngBgra(const std::string& path, int w, int h, const uint8_t* bgra, size_t pitch, bool flipY,
                      bool keepAlpha);
    std::vector<uint8_t> DownscaleBgra(const uint8_t* bgra, int w, int h, size_t pitch, bool flipY, int outW,
                                       int outH);

    class Encoder {
    public:
        Encoder();
        ~Encoder();
        bool Start(const Settings& s, const Catalog& cat, std::string& error);
        bool WriteFrame(const uint8_t* bgra, size_t bytes);
        bool Finish(std::string& error);
        void Abort();
        std::string Log();
        bool Alive();

    private:
        std::unique_ptr<Process> proc;
        std::unique_ptr<AviMjpegWriter> avi;
        std::unique_ptr<class LibavEncoder> lib;
        int inW = 0, inH = 0, outW = 0, outH = 0;
        size_t frameBytes = 0;
    };
}
