#include "client/systems/replay/ExportConfig.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>

#include "client/systems/replay/ReplayFormat.hpp"
#include "util/JsonValue.hpp"
#include "util/Paths.hpp"
#include "util/Strings.hpp"

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
        const size_t slash = text.find('/');
        if (slash != std::string::npos) {
            const auto n = str::ParseDouble(text.substr(0, slash), false);
            const auto d = str::ParseDouble(text.substr(slash + 1), false);
            return n && d && *d > 0 ? *n / *d : -1;
        }
        return str::ParseDouble(text).value_or(-1);
    }

    std::string ExportConfig::ToJson() const {
        json::Value j = json::Value::MakeObject();
        j.Set("container", json::Value(container));
        j.Set("codec", json::Value(codec));
        j.Set("width", json::Value(width));
        j.Set("height", json::Value(height));
        j.Set("aspect", json::Value(aspectIdx));
        j.Set("fps", json::Value(fps));
        j.Set("crf", json::Value(crf));
        j.Set("bitrate", json::Value(bitrateKbps));
        j.Set("ssaa", json::Value(ssaa));
        j.Set("hideGui", json::Value(hideGui));
        j.Set("transparent", json::Value(transparent));
        j.Set("range", json::Value(static_cast<int>(range)));
        j.Set("customStart", json::Value(customStartSec));
        j.Set("customEnd", json::Value(customEndSec));
        j.Set("outputDir", json::Value(outputDir));
        j.Set("nameTemplate", json::Value(nameTemplate));
        j.Set("preset", json::Value(preset));
        j.Set("pixelFormat", json::Value(pixelFormat));
        j.Set("extraArgs", json::Value(extraArgs));
        j.Set("encoder", json::Value(encoderOverride));
        j.Set("muxer", json::Value(muxerOverride));
        j.Set("extension", json::Value(extensionOverride));
        j.Set("gifColors", json::Value(gifColors));
        return j.Dump(2);
    }

    ExportConfig ExportConfig::FromJson(const std::string& text) {
        ExportConfig c;
        json::Value j;
        std::string err;
        if (!json::Value::Parse(text, j, err) || !j.IsObject())
            return c;
        const auto str = [&](const char* k, std::string& out) {
            if (const json::Value* f = j.Find(k))
                out = f->StringOr(out);
        };
        const auto num = [&](const char* k, double d) {
            const json::Value* f = j.Find(k);
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
        if (const json::Value* f = j.Find("hideGui"))
            c.hideGui = f->BoolOr(true);
        if (const json::Value* f = j.Find("transparent"))
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
