#include "client/replay/Ffmpeg.hpp"

#include <algorithm>
#include <filesystem>
#include <cstdio>
#include <sstream>

#include "client/replay/LibavEncoder.hpp"
#include "util/Paths.hpp"
#include <miniz.h>

namespace perlin::replay::ffmpeg {
    namespace fs = std::filesystem;

    bool Catalog::HasEncoder(const std::string& e) const {
        return std::find(encoders.begin(), encoders.end(), e) != encoders.end();
    }
    bool Catalog::HasMuxer(const std::string& m) const {
        return std::find(muxers.begin(), muxers.end(), m) != muxers.end();
    }

    const std::vector<Codec>& Codecs() {
        static const std::vector<Codec> v = {
            {"h264",
             "H.264 / AVC",
             "libx264",
             {"h264_nvenc", "h264_qsv", "h264_amf", "h264_mf", "libopenh264"},
             false,
             false,
             true},
            {"h265",
             "H.265 / HEVC",
             "libx265",
             {"hevc_nvenc", "hevc_qsv", "hevc_amf", "hevc_mf"},
             false,
             false,
             true},
            {"av1",
             "AV1",
             "libsvtav1",
             {"libaom-av1", "av1_nvenc", "av1_qsv", "av1_amf", "librav1e"},
             false,
             false,
             true},
            {"vp9", "VP9", "libvpx-vp9", {"vp9_qsv"}, false, true, true},
            {"vp8", "VP8", "libvpx", {}, false, true, true},
            {"prores", "Apple ProRes", "prores_ks", {"prores"}, false, true, false},
            {"dnxhd", "Avid DNxHD / DNxHR", "dnxhd", {}, false, false, false},
            {"ffv1", "FFV1 (lossless)", "ffv1", {}, true, true, false},
            {"huffyuv", "HuffYUV (lossless)", "huffyuv", {"ffvhuff"}, true, false, false},
            {"utvideo", "Ut Video (lossless)", "utvideo", {}, true, true, false},
            {"qtrle", "QuickTime Animation", "qtrle", {}, true, true, false},
            {"mpeg4", "MPEG-4 Part 2", "mpeg4", {}, false, false, true},
            {"mpeg2", "MPEG-2", "mpeg2video", {}, false, false, true},
            {"mjpeg", "Motion JPEG", "mjpeg", {}, false, false, true},
            {"gif", "GIF", "gif", {}, false, true, false},
            {"webp", "Animated WebP", "libwebp_anim", {"libwebp"}, false, true, true},
            {"apng", "Animated PNG", "apng", {}, true, true, false},
            {"rawvideo", "Uncompressed", "rawvideo", {}, true, false, false},
        };
        return v;
    }

    const std::vector<Container>& Containers() {
        static const std::vector<Container> v = {
            {"mp4",
             "MP4 (.mp4)",
             "mp4",
             "mp4",
             false,
             {"h264", "h265", "av1", "mpeg4", "vp9", "mpeg2", "mjpeg"}},
            {"mkv",
             "Matroska (.mkv)",
             "mkv",
             "matroska",
             false,
             {"h264", "h265", "av1", "vp9", "vp8", "ffv1", "huffyuv", "utvideo", "mpeg4", "mpeg2", "mjpeg",
              "prores", "dnxhd", "qtrle"}},
            {"webm", "WebM (.webm)", "webm", "webm", false, {"vp9", "vp8", "av1"}},
            {"mov",
             "QuickTime (.mov)",
             "mov",
             "mov",
             false,
             {"prores", "h264", "h265", "dnxhd", "qtrle", "mjpeg", "mpeg4", "rawvideo"}},
            {"avi",
             "AVI (.avi)",
             "avi",
             "avi",
             false,
             {"mpeg4", "mjpeg", "huffyuv", "utvideo", "ffv1", "h264", "mpeg2", "rawvideo"}},
            {"mxf", "MXF (.mxf)", "mxf", "mxf", false, {"dnxhd", "mpeg2"}},
            {"ts", "MPEG-TS (.ts)", "ts", "mpegts", false, {"h264", "h265", "mpeg2"}},
            {"flv", "Flash Video (.flv)", "flv", "flv", false, {"h264"}},
            {"ogv", "Ogg Video (.ogv)", "ogv", "ogg", false, {"vp8"}},
            {"gif", "Animated GIF (.gif)", "gif", "gif", false, {"gif"}},
            {"webp", "Animated WebP (.webp)", "webp", "webp", false, {"webp"}},
            {"apng", "Animated PNG (.apng)", "apng", "apng", false, {"apng"}},
            {"builtin", "Built-in AVI / MJPEG (no ffmpeg needed)", "avi", "", false, {}},
            {"png", "PNG sequence", "png", "", true, {}},
            {"jpg", "JPEG sequence", "jpg", "", true, {}},
            {"bmp", "BMP sequence", "bmp", "", true, {}},
            {"tiff", "TIFF sequence", "tiff", "", true, {}},
            {"exr", "OpenEXR sequence", "exr", "", true, {}},
            {"webpseq", "WebP sequence", "webp", "", true, {}},
            {"custom", "Custom (any ffmpeg muxer/encoder)", "mkv", "", false, {}},
        };
        return v;
    }

    const Codec* FindCodec(const std::string& id) {
        for (const Codec& c : Codecs())
            if (c.id == id)
                return &c;
        return nullptr;
    }
    const Container* FindContainer(const std::string& id) {
        for (const Container& c : Containers())
            if (c.id == id)
                return &c;
        return nullptr;
    }

    std::string FindExecutable() {
#ifdef _WIN32
        const char* exe = "ffmpeg.exe";
#else
        const char* exe = "ffmpeg";
#endif
        const std::string base = paths::DataDir();
        std::vector<fs::path> candidates;
        {
            const std::string app = paths::ExeDir();
            if (!app.empty()) {
                candidates.push_back(fs::path(app) / exe);
                candidates.push_back(fs::path(app) / "ffmpeg" / exe);
            }
        }
        for (const fs::path& extra :
             {fs::path(base) / "ffmpeg" / exe, fs::path(base) / exe, fs::path(base) / "ffmpeg" / "bin" / exe})
            candidates.push_back(extra);
        std::error_code ec;
        for (const fs::path& c : candidates)
            if (fs::exists(c, ec)) {
                int code = -1;
                RunCapture({c.string(), "-hide_banner", "-version"}, &code);
                if (code == 0)
                    return c.string();
            }
        int code = -1;
        RunCapture({exe, "-hide_banner", "-version"}, &code);
        return code == 0 ? std::string(exe) : std::string();
    }

    Catalog Query(const std::string& path) {
        Catalog cat;
        if (path.empty())
            return cat;
        int code = -1;
        const std::string ver = RunCapture({path, "-hide_banner", "-version"}, &code);
        if (code != 0)
            return cat;
        cat.available = true;
        cat.path = path;
        cat.version = ver.substr(0, ver.find('\n'));
        {
            std::istringstream in(RunCapture({path, "-hide_banner", "-encoders"}));
            std::string line;
            while (std::getline(in, line)) {
                if (line.size() > 8 && line[0] == ' ' && line[1] == 'V') {
                    std::istringstream ls(line.substr(8));
                    std::string name;
                    ls >> name;
                    if (!name.empty() && name != "=")
                        cat.encoders.push_back(name);
                }
            }
        }
        {
            std::istringstream in(RunCapture({path, "-hide_banner", "-muxers"}));
            std::string line;
            while (std::getline(in, line)) {
                if (line.size() > 4 && line[0] == ' ' && (line[1] == 'E' || line[2] == 'E') &&
                    line[3] == ' ') {
                    std::istringstream ls(line.substr(3));
                    std::string name;
                    ls >> name;
                    if (!name.empty() && name != "=")
                        cat.muxers.push_back(name);
                }
            }
        }
        std::sort(cat.encoders.begin(), cat.encoders.end());
        std::sort(cat.muxers.begin(), cat.muxers.end());
        return cat;
    }

    std::vector<std::string> SplitArgs(const std::string& line) {
        std::vector<std::string> out;
        std::string cur;
        bool inQuote = false, have = false;
        char quote = 0;
        for (char c : line) {
            if (inQuote) {
                if (c == quote)
                    inQuote = false;
                else
                    cur += c;
            } else if (c == '"' || c == '\'') {
                inQuote = true;
                quote = c;
                have = true;
            } else if (c == ' ' || c == '\t' || c == '\n') {
                if (have || !cur.empty())
                    out.push_back(cur);
                cur.clear();
                have = false;
            } else
                cur += c;
        }
        if (have || !cur.empty())
            out.push_back(cur);
        return out;
    }

    namespace {
        std::string Num(double v) {
            std::ostringstream o;
            o << v;
            return o.str();
        }
        std::string StripFfmpegFlagsPrefix(const std::string& s) {
            return s;
        }

        bool StartsWith(const std::string& s, const char* p) {
            return s.starts_with(p);
        }
        bool Contains(const std::string& s, const char* p) {
            return s.contains(p);
        }

        std::string PickEncoder(const Settings& s, const Codec& codec, const Catalog& cat) {
            if (!s.encoderOverride.empty())
                return s.encoderOverride;
            if (!cat.available || cat.HasEncoder(codec.encoder))
                return codec.encoder;
            for (const std::string& f : codec.fallbackEncoders)
                if (cat.HasEncoder(f))
                    return f;
            return codec.encoder;
        }

        void QualityArgs(const Settings& s, const std::string& enc, std::vector<std::string>& a) {
            const int crf = std::clamp(s.crf, 0, 63);
            const bool br = s.bitrateKbps > 0;
            const std::string b = std::to_string(s.bitrateKbps) + "k";
            if (enc == "libx264" || enc == "libx265") {
                if (br) {
                    a.insert(a.end(), {"-b:v", b});
                } else {
                    a.insert(a.end(), {"-crf", std::to_string(std::min(crf, 51))});
                }
                a.insert(a.end(), {"-preset", s.preset.empty() ? "veryfast" : s.preset});
                if (enc == "libx265")
                    a.insert(a.end(), {"-tag:v", "hvc1"});
            } else if (Contains(enc, "nvenc")) {
                if (br)
                    a.insert(a.end(), {"-rc", "vbr", "-b:v", b});
                else
                    a.insert(a.end(),
                             {"-rc", "vbr", "-cq", std::to_string(std::min(crf + 4, 51)), "-b:v", "0"});
                a.insert(a.end(), {"-preset", s.preset.empty() ? "p5" : s.preset});
                if (StartsWith(enc, "hevc"))
                    a.insert(a.end(), {"-tag:v", "hvc1"});
            } else if (Contains(enc, "qsv")) {
                if (br)
                    a.insert(a.end(), {"-b:v", b});
                else
                    a.insert(a.end(), {"-global_quality", std::to_string(std::min(crf + 4, 51))});
                if (!s.preset.empty())
                    a.insert(a.end(), {"-preset", s.preset});
            } else if (Contains(enc, "amf")) {
                if (br)
                    a.insert(a.end(), {"-rc", "vbr_peak", "-b:v", b});
                else
                    a.insert(a.end(), {"-rc", "cqp", "-qp_i", std::to_string(crf), "-qp_p",
                                       std::to_string(crf + 2), "-qp_b", std::to_string(crf + 4)});
            } else if (enc == "libvpx-vp9" || enc == "libvpx") {
                if (br)
                    a.insert(a.end(), {"-b:v", b});
                else
                    a.insert(a.end(), {"-crf", std::to_string(std::min(crf + 10, 63)), "-b:v", "0"});
                a.insert(a.end(), {"-deadline", "good", "-cpu-used", "2", "-row-mt", "1"});
            } else if (enc == "libsvtav1") {
                if (br)
                    a.insert(a.end(), {"-b:v", b});
                else
                    a.insert(a.end(), {"-crf", std::to_string(std::min(crf + 8, 63))});
                a.insert(a.end(), {"-preset", s.preset.empty() ? "8" : s.preset});
            } else if (enc == "libaom-av1") {
                if (br)
                    a.insert(a.end(), {"-b:v", b});
                else
                    a.insert(a.end(), {"-crf", std::to_string(std::min(crf + 8, 63)), "-b:v", "0"});
                a.insert(a.end(), {"-cpu-used", "6", "-row-mt", "1"});
            } else if (enc == "librav1e") {
                if (br)
                    a.insert(a.end(), {"-b:v", b});
                else
                    a.insert(a.end(), {"-qp", std::to_string(std::min(crf * 4, 255))});
            } else if (enc == "prores_ks" || enc == "prores") {
                const int profile = crf <= 8 ? 4 : crf <= 16 ? 3 : crf <= 24 ? 2 : crf <= 32 ? 1 : 0;
                a.insert(a.end(), {"-profile:v", std::to_string(profile)});
            } else if (enc == "dnxhd") {
                a.insert(a.end(), {"-profile:v", "dnxhr_hq"});
            } else if (enc == "mpeg4" || enc == "mpeg2video" || enc == "mjpeg") {
                if (br)
                    a.insert(a.end(), {"-b:v", b});
                else
                    a.insert(a.end(), {"-q:v", std::to_string(std::clamp(2 + crf / 4, 1, 31))});
            } else if (enc == "libopenh264" || Contains(enc, "_mf")) {
                a.insert(a.end(), {"-b:v", br ? b : "20M"});
            } else if (enc == "libwebp_anim" || enc == "libwebp") {
                a.insert(a.end(),
                         {"-quality", std::to_string(std::clamp(100 - crf * 2, 0, 100)), "-loop", "0"});
            } else if (enc == "ffv1") {
                a.insert(a.end(), {"-level", "3", "-slicecrc", "1"});
            } else if (br) {
                a.insert(a.end(), {"-b:v", b});
            }
        }

        std::string DefaultPixFmt(const std::string& enc, bool alpha) {
            if (enc == "libx264" || enc == "libx265" || Contains(enc, "nvenc") || Contains(enc, "qsv") ||
                Contains(enc, "amf") || enc == "libsvtav1" || enc == "libaom-av1" || enc == "librav1e" ||
                enc == "mpeg4" || enc == "mpeg2video" || enc == "libopenh264" || Contains(enc, "_mf") ||
                enc == "libvpx")
                return alpha && enc == "libvpx" ? "yuva420p" : "yuv420p";
            if (enc == "libvpx-vp9")
                return alpha ? "yuva420p" : "yuv420p";
            if (enc == "mjpeg")
                return "yuvj420p";
            if (enc == "prores_ks" || enc == "prores")
                return alpha ? "yuva444p10le" : "yuv422p10le";
            if (enc == "dnxhd")
                return "yuv422p";
            if (enc == "ffv1" || enc == "utvideo")
                return alpha ? "bgra" : "bgr0";
            if (enc == "huffyuv")
                return "yuv422p";
            if (enc == "qtrle")
                return alpha ? "argb" : "rgb24";
            if (enc == "apng" || enc == "libwebp_anim" || enc == "libwebp")
                return alpha ? "rgba" : "rgb24";
            if (enc == "rawvideo")
                return "bgr24";
            if (enc == "gif")
                return "pal8";
            return "yuv420p";
        }
    }

    bool PlanEncode(const Settings& s, const Catalog& cat, EncodePlan& p, std::string& error) {
        p = EncodePlan{};
        if (s.width < 16 || s.height < 16 || s.width > 16384 || s.height > 16384) {
            error = "resolution out of range (16..16384)";
            return false;
        }
        if (!(s.fps > 0.0) || s.fps > 1000.0) {
            error = "frame rate out of range";
            return false;
        }
        const Container* cont = FindContainer(s.container);
        if (!cont) {
            error = "unknown container " + s.container;
            return false;
        }
        const auto addExtra = [&] {
            const std::vector<std::string> extra = SplitArgs(s.extraArgs);
            for (size_t i = 0; i + 1 < extra.size(); i++) {
                if (extra[i].size() > 1 && extra[i][0] == '-') {
                    std::string key = extra[i].substr(1);
                    if (const size_t colon = key.find(':'); colon != std::string::npos)
                        key.resize(colon);
                    if (key == "b") {
                        p.options.emplace_back("b", extra[i + 1]);
                    } else if (key == "q") {
                        p.qscale = std::atoi(extra[i + 1].c_str());
                    } else
                        p.options.emplace_back(key, extra[i + 1]);
                    i++;
                }
            }
        };
        if (cont->imageSequence) {
            p.imageSequence = true;
            p.muxer = "image2";
            if (s.container == "png") {
                p.encoder = "png";
                p.pixFmt = s.transparent ? "rgba" : "rgb24";
            } else if (s.container == "jpg") {
                p.encoder = "mjpeg";
                p.pixFmt = "yuvj444p";
                p.qscale = std::clamp(2 + s.crf / 6, 1, 31);
            } else if (s.container == "bmp") {
                p.encoder = "bmp";
                p.pixFmt = "bgr24";
            } else if (s.container == "tiff") {
                p.encoder = "tiff";
                p.pixFmt = s.transparent ? "rgba" : "rgb24";
            } else if (s.container == "exr") {
                p.encoder = "exr";
                p.pixFmt = s.transparent ? "gbrapf32le" : "gbrpf32le";
            } else {
                p.encoder = "libwebp";
                p.pixFmt = "yuv420p";
                p.options.emplace_back("quality", std::to_string(std::clamp(100 - s.crf * 2, 0, 100)));
            }
            addExtra();
            return true;
        }
        const Codec* codec = nullptr;
        std::string enc;
        if (s.container == "custom") {
            enc = s.encoderOverride;
            if (enc.empty()) {
                error = "custom export needs an encoder name";
                return false;
            }
        } else {
            codec = FindCodec(s.codec);
            if (!codec && s.encoderOverride.empty()) {
                error = "unknown codec " + s.codec;
                return false;
            }
            if (codec && s.encoderOverride.empty() &&
                std::find(cont->codecs.begin(), cont->codecs.end(), codec->id) == cont->codecs.end()) {
                error = codec->label + " can't be stored in " + cont->label +
                        " (choose another codec or container)";
                return false;
            }
            enc = codec ? PickEncoder(s, *codec, cat) : s.encoderOverride;
            if (!s.encoderOverride.empty())
                enc = s.encoderOverride;
        }
        if (cat.available && !cat.HasEncoder(enc)) {
            error = "no encoder \"" + enc + "\" in this build";
            return false;
        }
        if (enc == "dnxhd" && (s.width < 256 || s.height < 120)) {
            error = "DNxHD needs at least 256x120 pixels";
            return false;
        }
        p.encoder = enc;
        p.muxer = !s.muxerOverride.empty() ? s.muxerOverride : cont->muxer;
        if (cat.available && !p.muxer.empty() && !cat.HasMuxer(p.muxer)) {
            error = "no muxer \"" + p.muxer + "\" in this build";
            return false;
        }
        const bool alpha = s.transparent && ((codec && codec->hasAlpha) || !s.pixelFormat.empty());
        p.pixFmt = s.pixelFormat.empty() ? DefaultPixFmt(enc, alpha) : s.pixelFormat;
        if (enc == "gif") {
            p.gif = true;
            if (s.pixelFormat.empty())
                p.pixFmt = "bgr8";
        } else {
            std::vector<std::string> q;
            QualityArgs(s, enc, q);
            for (size_t i = 0; i + 1 < q.size(); i += 2) {
                std::string key = q[i].substr(1);
                if (key == "b:v")
                    key = "b";
                else if (key == "profile:v")
                    key = "profile";
                if (key == "tag:v")
                    p.codecTag = q[i + 1];
                else if (key == "q:v")
                    p.qscale = std::atoi(q[i + 1].c_str());
                else
                    p.options.emplace_back(key, q[i + 1]);
            }
            if (enc == "libx264" || enc == "libx265" || enc == "libsvtav1" || enc == "libvpx-vp9")
                p.tagColors = true;
        }
        addExtra();
        return true;
    }

    bool BuildArgs(const Settings& s, const Catalog& cat, std::vector<std::string>& argv,
                   std::string& error) {
        argv.clear();
        if (s.ffmpegPath.empty()) {
            error = "ffmpeg was not found";
            return false;
        }
        if (s.width < 16 || s.height < 16 || s.width > 16384 || s.height > 16384) {
            error = "resolution out of range (16..16384)";
            return false;
        }
        if (!(s.fps > 0.0) || s.fps > 1000.0) {
            error = "frame rate out of range";
            return false;
        }
        const Container* cont = FindContainer(s.container);
        if (!cont) {
            error = "unknown container " + s.container;
            return false;
        }
        argv = {s.ffmpegPath,
                "-hide_banner",
                "-loglevel",
                "warning",
                "-y",
                "-f",
                "rawvideo",
                "-pix_fmt",
                "bgra",
                "-s",
                std::to_string(s.inputWidth > 0 ? s.inputWidth : s.width) + "x" +
                    std::to_string(s.inputHeight > 0 ? s.inputHeight : s.height),
                "-framerate",
                Num(s.fps),
                "-i",
                "-"};
        if (!s.audioPath.empty()) {
            argv.insert(argv.end(), {"-i", s.audioPath});
        }
        std::vector<std::string> vf;
        if ((s.inputWidth > 0 && s.inputWidth != s.width) || (s.inputHeight > 0 && s.inputHeight != s.height))
            vf.push_back("scale=" + std::to_string(s.width) + ":" + std::to_string(s.height) + ":flags=area");
        vf.push_back("setsar=1");

        if (cont->imageSequence) {
            if (s.container == "png")
                argv.insert(argv.end(), {"-c:v", "png", "-pix_fmt", s.transparent ? "rgba" : "rgb24"});
            else if (s.container == "jpg")
                argv.insert(argv.end(), {"-c:v", "mjpeg", "-pix_fmt", "yuvj444p", "-q:v",
                                         std::to_string(std::clamp(2 + s.crf / 6, 1, 31))});
            else if (s.container == "bmp")
                argv.insert(argv.end(), {"-c:v", "bmp", "-pix_fmt", "bgr24"});
            else if (s.container == "tiff")
                argv.insert(argv.end(), {"-c:v", "tiff", "-pix_fmt", s.transparent ? "rgba" : "rgb24"});
            else if (s.container == "exr")
                argv.insert(argv.end(),
                            {"-c:v", "exr", "-pix_fmt", s.transparent ? "gbrapf32le" : "gbrpf32le"});
            else if (s.container == "webpseq")
                argv.insert(argv.end(), {"-c:v", "libwebp", "-quality",
                                         std::to_string(std::clamp(100 - s.crf * 2, 0, 100))});
            {
                std::string seqVf;
                for (size_t i = 0; i < vf.size(); i++)
                    seqVf += (i ? "," : "") + vf[i];
                argv.insert(argv.end(), {"-vf", seqVf});
            }
            if (!s.extraArgs.empty())
                for (const std::string& e : SplitArgs(s.extraArgs))
                    argv.push_back(e);
            argv.push_back(s.outputPath);
            return true;
        }

        std::string enc;
        const Codec* codec = nullptr;
        if (s.container == "custom") {
            enc = s.encoderOverride;
            if (enc.empty()) {
                error = "custom export needs an encoder name";
                return false;
            }
            codec = nullptr;
        } else {
            codec = FindCodec(s.codec);
            if (!codec && s.encoderOverride.empty()) {
                error = "unknown codec " + s.codec;
                return false;
            }
            if (codec && s.encoderOverride.empty() &&
                std::find(cont->codecs.begin(), cont->codecs.end(), codec->id) == cont->codecs.end()) {
                error = codec->label + " can't be stored in " + cont->label +
                        " (choose another codec or container)";
                return false;
            }
            enc = codec ? PickEncoder(s, *codec, cat) : s.encoderOverride;
            if (s.encoderOverride.empty() == false)
                enc = s.encoderOverride;
        }
        if (cat.available && !cat.HasEncoder(enc)) {
            error = "this ffmpeg build has no encoder \"" + enc + "\"";
            return false;
        }
        if (enc == "dnxhd" && (s.width < 256 || s.height < 120)) {
            error = "DNxHD needs at least 256x120 pixels";
            return false;
        }
        const std::string muxer = !s.muxerOverride.empty() ? s.muxerOverride : cont->muxer;
        if (cat.available && !muxer.empty() && !cat.HasMuxer(muxer)) {
            error = "this ffmpeg build has no muxer \"" + muxer + "\"";
            return false;
        }

        const bool alpha = s.transparent && ((codec && codec->hasAlpha) || !s.pixelFormat.empty());
        std::string pix = s.pixelFormat.empty() ? DefaultPixFmt(enc, alpha) : s.pixelFormat;

        if (enc == "gif") {
            vf.push_back(
                "split[a][b];[a]palettegen=max_colors=" + std::to_string(std::clamp(s.gifColors, 2, 256)) +
                "[p];[b][p]paletteuse=dither=bayer:bayer_scale=3");
            argv.insert(argv.end(), {"-c:v", "gif", "-loop", "0"});
        } else {
            vf.push_back("format=" + pix);
            argv.insert(argv.end(), {"-c:v", enc});
            QualityArgs(s, enc, argv);
            if (enc == "libx264" || enc == "libx265" || enc == "libsvtav1" || enc == "libvpx-vp9") {
                vf.back() = "scale=in_range=full:out_range=tv:out_color_matrix=bt709,format=" + pix;
                argv.insert(argv.end(), {"-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc",
                                         "bt709", "-color_range", "tv"});
            }
        }
        std::string vfs;
        for (size_t i = 0; i < vf.size(); i++) {
            if (i)
                vfs += (vf[i].starts_with("split") ? "," : ",");
            vfs += vf[i];
        }
        argv.insert(argv.end(), {"-vf", vfs});
        if (!s.audioPath.empty())
            argv.insert(argv.end(), {"-map", "0:v:0", "-map", "1:a:0", "-c:a",
                                     cont->id == "webm" ? "libopus" : "aac", "-shortest"});
        if (cont->id == "mp4" || cont->id == "mov")
            argv.insert(argv.end(), {"-movflags", "+faststart"});
        if (!muxer.empty())
            argv.insert(argv.end(), {"-f", muxer});
        if (!s.extraArgs.empty())
            for (const std::string& e : SplitArgs(s.extraArgs))
                argv.push_back(e);
        argv.push_back(s.outputPath);
        (void)StripFfmpegFlagsPrefix;
        return true;
    }

    Encoder::Encoder() = default;
    Encoder::~Encoder() = default;

    bool Encoder::Start(const Settings& s, const Catalog& cat, std::string& error) {
        if (cat.builtin && s.container != "builtin") {
            inW = s.inputWidth > 0 ? s.inputWidth : s.width;
            inH = s.inputHeight > 0 ? s.inputHeight : s.height;
            frameBytes = static_cast<size_t>(inW) * static_cast<size_t>(inH) * 4;
            lib = std::make_unique<LibavEncoder>();
            if (!lib->Start(s, cat, error)) {
                lib.reset();
                return false;
            }
            return true;
        }
        if (s.container == "builtin") {
            inW = s.inputWidth > 0 ? s.inputWidth : s.width;
            inH = s.inputHeight > 0 ? s.inputHeight : s.height;
            frameBytes = static_cast<size_t>(inW) * static_cast<size_t>(inH) * 4;
            avi = std::make_unique<AviMjpegWriter>();
            outW = s.width;
            outH = s.height;
            const int quality = std::clamp(100 - s.crf, 40, 100);
            if (!avi->Open(s.outputPath, s.width, s.height, s.fps, quality, error)) {
                avi.reset();
                return false;
            }
            return true;
        }
        std::vector<std::string> argv;
        if (!BuildArgs(s, cat, argv, error))
            return false;
        frameBytes = static_cast<size_t>(s.inputWidth > 0 ? s.inputWidth : s.width) *
                     static_cast<size_t>(s.inputHeight > 0 ? s.inputHeight : s.height) * 4;
        proc = std::make_unique<Process>();
        if (!proc->Start(argv, true, error)) {
            proc.reset();
            return false;
        }
        return true;
    }

    bool Encoder::WriteFrame(const uint8_t* bgra, size_t bytes) {
        if (lib)
            return lib->WriteFrame(bgra, bytes);
        if (avi) {
            if (bytes != frameBytes)
                return false;
            if (inW == outW && inH == outH)
                return avi->AddFrameBgra(bgra);
            const std::vector<uint8_t> scaled =
                DownscaleBgra(bgra, inW, inH, static_cast<size_t>(inW) * 4, false, outW, outH);
            return avi->AddFrameBgra(scaled.data());
        }
        if (!proc || bytes != frameBytes)
            return false;
        return proc->WriteStdin(bgra, bytes);
    }

    bool Encoder::Alive() {
        return lib || avi || (proc && proc->Running());
    }
    std::string Encoder::Log() {
        return proc ? proc->Output() : std::string();
    }

    bool Encoder::Finish(std::string& error) {
        if (lib) {
            const bool ok = lib->Finish(error);
            lib.reset();
            return ok;
        }
        if (avi) {
            const bool ok = avi->Close(error);
            avi.reset();
            return ok;
        }
        if (!proc)
            return false;
        proc->CloseStdin();
        const int code = proc->Wait();
        if (code != 0) {
            std::string log = proc->Output();
            if (log.size() > 600)
                log = log.substr(log.size() - 600);
            error = "ffmpeg exited with code " + std::to_string(code) + (log.empty() ? "" : ": " + log);
            proc.reset();
            return false;
        }
        proc.reset();
        return true;
    }

    void Encoder::Abort() {
        lib.reset();
        avi.reset();
        if (proc) {
            proc->Kill();
            proc->Wait();
            proc.reset();
        }
    }

    bool WritePngBgra(const std::string& path, int w, int h, const uint8_t* bgra, size_t pitch, bool flipY,
                      bool keepAlpha) {
        if (w <= 0 || h <= 0 || !bgra)
            return false;
        const int channels = keepAlpha ? 4 : 3;
        std::vector<uint8_t> rgb(static_cast<size_t>(w) * static_cast<size_t>(h) *
                                 static_cast<size_t>(channels));
        for (int y = 0; y < h; y++) {
            const uint8_t* src = bgra + static_cast<size_t>(flipY ? h - 1 - y : y) * pitch;
            uint8_t* dst =
                rgb.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * static_cast<size_t>(channels);
            for (int x = 0; x < w; x++, src += 4, dst += channels) {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                if (keepAlpha)
                    dst[3] = src[3];
            }
        }
        size_t size = 0;
        void* png =
            tdefl_write_image_to_png_file_in_memory_ex(rgb.data(), w, h, channels, &size, 6, MZ_FALSE);
        if (!png)
            return false;
        FILE* f = std::fopen(path.c_str(), "wb");
        bool ok = false;
        if (f) {
            ok = std::fwrite(png, 1, size, f) == size;
            std::fclose(f);
        }
        mz_free(png);
        return ok;
    }

    std::vector<uint8_t> DownscaleBgra(const uint8_t* bgra, int w, int h, size_t pitch, bool flipY, int outW,
                                       int outH) {
        std::vector<uint8_t> out(static_cast<size_t>(outW) * static_cast<size_t>(outH) * 4);
        for (int oy = 0; oy < outH; oy++) {
            const int y0 = static_cast<int>(static_cast<long long>(oy) * h / outH);
            const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<long long>(oy + 1) * h / outH));
            for (int ox = 0; ox < outW; ox++) {
                const int x0 = static_cast<int>(static_cast<long long>(ox) * w / outW);
                const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<long long>(ox + 1) * w / outW));
                unsigned acc[4] = {0, 0, 0, 0};
                for (int y = y0; y < y1; y++) {
                    const uint8_t* row = bgra + static_cast<size_t>(flipY ? h - 1 - y : y) * pitch;
                    for (int x = x0; x < x1; x++)
                        for (int c = 0; c < 4; c++)
                            acc[c] += row[x * 4 + c];
                }
                const unsigned n = static_cast<unsigned>((y1 - y0) * (x1 - x0));
                uint8_t* d =
                    out.data() +
                    (static_cast<size_t>(oy) * static_cast<size_t>(outW) + static_cast<size_t>(ox)) * 4;
                for (int c = 0; c < 4; c++)
                    d[c] = static_cast<uint8_t>(acc[c] / n);
            }
        }
        return out;
    }
}
