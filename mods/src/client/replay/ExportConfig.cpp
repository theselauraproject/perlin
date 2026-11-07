#include "client/replay/ExportConfig.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>

#include "client/replay/ReplayFormat.hpp"
#include "client/ui/Json.hpp"
#include "util/Paths.hpp"

namespace perlin::replay {
    const std::vector<Resolution>& ResolutionPresets() {
        static const std::vector<Resolution> v = {
            {"360p", 640, 360},         {"480p", 854, 480},
            {"720p (HD)", 1280, 720},   {"1080p (Full HD)", 1920, 1080},
            {"1440p (2K)", 2560, 1440}, {"2160p (4K)", 3840, 2160},
            {"4320p (8K)", 7680, 4320}, {"Custom", 0, 0},
        };
        return v;
    }
    const std::vector<AspectRatio>& AspectRatios() {
        static const std::vector<AspectRatio> v = {
            {"16:9", 16, 9},
            {"21:9", 21, 9},
            {"32:9", 32, 9},
            {"4:3", 4, 3},
            {"3:2", 3, 2},
            {"1:1", 1, 1},
            {"9:16 (vertical)", 9, 16},
            {"4:5", 4, 5},
            {"Free", 0, 0},
        };
        return v;
    }
    const std::vector<double>& FpsPresets() {
        static const std::vector<double> v = {15, 23.976, 24,  25,  29.97, 30,  48,  50,  59.94, 60,
                                              72, 90,     120, 144, 165,   240, 360, 480, 960};
        return v;
    }

    std::string FpsLabel(double fps) {
        char buf[32];
        if (std::fabs(fps - std::round(fps)) < 1e-6)
            std::snprintf(buf, sizeof buf, "%d", static_cast<int>(std::lround(fps)));
        else
            std::snprintf(buf, sizeof buf, "%.3f", fps);
        std::string s = buf;
        if (s.contains('.')) {
            while (s.back() == '0')
                s.pop_back();
            if (s.back() == '.')
                s.pop_back();
        }
        return s;
    }

    double ParseFps(const std::string& text) {
        if (text.empty())
            return -1;
        try {
            const size_t slash = text.find('/');
            if (slash != std::string::npos) {
                const double n = std::stod(text.substr(0, slash)), d = std::stod(text.substr(slash + 1));
                return d > 0 ? n / d : -1;
            }
            size_t used = 0;
            const double v = std::stod(text, &used);
            return used == text.size() ? v : -1;
        } catch (...) {
            return -1;
        }
    }

    std::string ExportConfig::ToJson() const {
        ui::Json j = ui::Json::MakeObject();
        j.Set("container", ui::Json(container));
        j.Set("codec", ui::Json(codec));
        j.Set("width", ui::Json(width));
        j.Set("height", ui::Json(height));
        j.Set("aspect", ui::Json(aspectIdx));
        j.Set("fps", ui::Json(fps));
        j.Set("crf", ui::Json(crf));
        j.Set("bitrate", ui::Json(bitrateKbps));
        j.Set("ssaa", ui::Json(ssaa));
        j.Set("hideGui", ui::Json(hideGui));
        j.Set("transparent", ui::Json(transparent));
        j.Set("range", ui::Json(static_cast<int>(range)));
        j.Set("customStart", ui::Json(customStartSec));
        j.Set("customEnd", ui::Json(customEndSec));
        j.Set("outputDir", ui::Json(outputDir));
        j.Set("nameTemplate", ui::Json(nameTemplate));
        j.Set("preset", ui::Json(preset));
        j.Set("pixelFormat", ui::Json(pixelFormat));
        j.Set("extraArgs", ui::Json(extraArgs));
        j.Set("encoder", ui::Json(encoderOverride));
        j.Set("muxer", ui::Json(muxerOverride));
        j.Set("extension", ui::Json(extensionOverride));
        j.Set("gifColors", ui::Json(gifColors));
        return j.Dump(2);
    }

    ExportConfig ExportConfig::FromJson(const std::string& text) {
        ExportConfig c;
        ui::Json j;
        std::string err;
        if (!ui::Json::Parse(text, j, err) || !j.IsObject())
            return c;
        const auto str = [&](const char* k, std::string& out) {
            if (const ui::Json* f = j.Find(k))
                out = f->StringOr(out);
        };
        const auto num = [&](const char* k, double d) {
            const ui::Json* f = j.Find(k);
            return f ? f->NumberOr(d) : d;
        };
        str("container", c.container);
        str("codec", c.codec);
        c.width = std::clamp(static_cast<int>(num("width", c.width)), 16, 16384);
        c.height = std::clamp(static_cast<int>(num("height", c.height)), 16, 16384);
        c.aspectIdx =
            std::clamp(static_cast<int>(num("aspect", 0)), 0, static_cast<int>(AspectRatios().size()) - 1);
        c.fps = std::clamp(num("fps", c.fps), 0.1, 1000.0);
        c.crf = std::clamp(static_cast<int>(num("crf", c.crf)), 0, 51);
        c.bitrateKbps = std::clamp(static_cast<int>(num("bitrate", 0)), 0, 2000000);
        c.ssaa = std::clamp(static_cast<int>(num("ssaa", 1)), 1, 4);
        if (const ui::Json* f = j.Find("hideGui"))
            c.hideGui = f->BoolOr(true);
        if (const ui::Json* f = j.Find("transparent"))
            c.transparent = f->BoolOr(false);
        c.range = static_cast<RangeMode>(std::clamp(static_cast<int>(num("range", 0)), 0, 2));
        c.customStartSec = num("customStart", 0);
        c.customEndSec = num("customEnd", 10);
        str("outputDir", c.outputDir);
        str("nameTemplate", c.nameTemplate);
        str("preset", c.preset);
        str("pixelFormat", c.pixelFormat);
        str("extraArgs", c.extraArgs);
        str("encoder", c.encoderOverride);
        str("muxer", c.muxerOverride);
        str("extension", c.extensionOverride);
        c.gifColors = std::clamp(static_cast<int>(num("gifColors", 256)), 2, 256);
        if (!ffmpeg::FindContainer(c.container))
            c.container = "mp4";
        return c;
    }

    std::string ExpandNameTemplate(const std::string& tpl, const std::string& replayName, int w, int h,
                                   double fps, const std::string& container) {
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        char date[16], time[16];
        std::strftime(date, sizeof date, "%Y-%m-%d", &tm);
        std::strftime(time, sizeof time, "%H-%M-%S", &tm);
        std::string out;
        for (size_t i = 0; i < tpl.size();) {
            if (tpl[i] == '{') {
                const size_t end = tpl.find('}', i);
                if (end != std::string::npos) {
                    const std::string key = tpl.substr(i + 1, end - i - 1);
                    if (key == "replay")
                        out += replayName;
                    else if (key == "date")
                        out += date;
                    else if (key == "time")
                        out += time;
                    else if (key == "res")
                        out += std::to_string(w) + "x" + std::to_string(h);
                    else if (key == "fps")
                        out += FpsLabel(fps);
                    else if (key == "container")
                        out += container;
                    else
                        out += tpl.substr(i, end - i + 1);
                    i = end + 1;
                    continue;
                }
            }
            out += tpl[i++];
        }
        out = SafeFileName(out);
        return out.empty() ? "export" : out;
    }

    std::string ExportDirectory(const ExportConfig& c) {
        if (!c.outputDir.empty())
            return c.outputDir;
        return ReplayDirectory() + "/exports";
    }

    std::string UniquePath(const std::string& path, bool isDirectory) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(path, ec))
            return path;
        const fs::path p(path);
        for (int i = 2; i < 10000; i++) {
            fs::path cand = p.parent_path() / (p.stem().string() + " (" + std::to_string(i) + ")" +
                                               (isDirectory ? "" : p.extension().string()));
            if (!fs::exists(cand, ec))
                return cand.string();
        }
        return path;
    }
}
