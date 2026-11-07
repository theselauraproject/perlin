#include <cstdio>
#include <filesystem>
#include <vector>
#include "client/replay/Ffmpeg.hpp"
using namespace perlin::replay::ffmpeg;
int main(int argc, char** argv) {
    std::string path = argc > 1 ? argv[1] : FindExecutable();
    Catalog cat = Query(path);
    if (!cat.available) {
        std::puts("no ffmpeg; skipped");
        return 0;
    }
    std::printf("%s | %zu encoders %zu muxers\n", cat.version.c_str(), cat.encoders.size(),
                cat.muxers.size());
    std::string dir = argc > 2 ? argv[2] : "/tmp/ffout";
    std::filesystem::create_directories(dir);
    int ok = 0, fail = 0, skip = 0;
    {
        Settings s;
        s.ffmpegPath = path;
        s.container = "mp4";
        s.codec = "h264";
        s.width = 64;
        s.height = 48;
        s.inputWidth = 128;
        s.inputHeight = 96;
        s.fps = 23.976;
        s.outputPath = dir + "/ssaa.mp4";
        std::string err;
        Encoder e;
        if (!e.Start(s, cat, err)) {
            std::printf("FAIL ssaa start: %s\n", err.c_str());
            return 1;
        }
        std::vector<uint8_t> f(128 * 96 * 4, 200);
        for (int i = 0; i < 10; i++)
            e.WriteFrame(f.data(), f.size());
        if (!e.Finish(err)) {
            std::printf("FAIL ssaa: %s\n", err.c_str());
            return 1;
        }
        std::printf("ssaa ok\n");
        if (!WritePngBgra(dir + "/p.png", 128, 96, f.data(), 128 * 4, false, false)) {
            std::puts("FAIL png");
            return 1;
        }
        std::printf("png ok\n");
        {
            Settings b;
            b.container = "builtin";
            b.width = 64;
            b.height = 48;
            b.inputWidth = 128;
            b.inputHeight = 96;
            b.fps = 30;
            b.outputPath = dir + "/builtin.avi";
            Encoder be;
            std::string e2;
            if (!be.Start(b, cat, e2)) {
                std::printf("FAIL builtin: %s\n", e2.c_str());
                return 1;
            }
            for (int i = 0; i < 5; i++)
                if (!be.WriteFrame(f.data(), f.size())) {
                    std::puts("FAIL builtin write");
                    return 1;
                }
            if (!be.Finish(e2)) {
                std::printf("FAIL builtin finish: %s\n", e2.c_str());
                return 1;
            }
            std::printf("builtin ok\n");
        }
        std::vector<uint8_t> d = DownscaleBgra(f.data(), 128, 96, 128 * 4, false, 64, 48);
        if (d.size() != 64 * 48 * 4 || d[0] != 200) {
            std::puts("FAIL downscale");
            return 1;
        }
        if (argc > 3)
            return 0;
    }
    const int W = 130, H = 74;
    for (const Container& c : Containers()) {
        if (c.id == "custom")
            continue;
        std::vector<std::string> codecs = c.codecs;
        if (c.imageSequence)
            codecs = {""};
        for (const std::string& cid : codecs) {
            Settings s;
            s.ffmpegPath = path;
            s.container = c.id;
            s.codec = cid;
            s.width = W;
            s.height = H;
            s.fps = 30;
            s.crf = 23;
            std::string name = dir + "/t_" + c.id + "_" + cid;
            if (c.imageSequence) {
                std::filesystem::create_directories(name);
                s.outputPath = name + "/f_%05d." + c.ext;
            } else
                s.outputPath = name + "." + c.ext;
            std::string err;
            std::vector<std::string> a;
            if (!BuildArgs(s, cat, a, err)) {
                std::printf("SKIP %s/%s: %s\n", c.id.c_str(), cid.c_str(), err.c_str());
                skip++;
                continue;
            }
            Encoder e;
            if (!e.Start(s, cat, err)) {
                std::printf("FAIL start %s/%s: %s\n", c.id.c_str(), cid.c_str(), err.c_str());
                fail++;
                continue;
            }
            std::vector<uint8_t> f(W * H * 4);
            for (int i = 0; i < 20; i++) {
                for (int p = 0; p < W * H; p++) {
                    f[p * 4] = i * 12;
                    f[p * 4 + 1] = p % W * 2;
                    f[p * 4 + 2] = p / W * 3;
                    f[p * 4 + 3] = 255;
                }
                if (!e.WriteFrame(f.data(), f.size()))
                    break;
            }
            if (!e.Finish(err)) {
                std::printf("FAIL %s/%s: %s\n", c.id.c_str(), cid.c_str(), err.c_str());
                fail++;
            } else {
                ok++;
            }
        }
    }
    std::printf("ok=%d fail=%d skip=%d\n", ok, fail, skip);
    return 0;
}
