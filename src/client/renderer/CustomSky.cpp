#include "client/renderer/CustomSky.hpp"

#include "util/Log.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>

namespace perlin::sky {
    namespace {
        std::string Trim(const std::string& s) {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
                a++;
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
                b--;
            return s.substr(a, b - a);
        }

        std::string Lower(std::string s) {
            for (char& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        // java.util.Properties: '#' and '!' comments, '=' or ':' separators, backslash continuations
        std::map<std::string, std::string> ReadProperties(const std::string& text) {
            std::map<std::string, std::string> out;
            std::istringstream in(text);
            std::string line, pending;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                line = Trim(pending.empty() ? line : pending + Trim(line));
                pending.clear();
                if (!line.empty() && line.back() == '\\') {
                    pending = line.substr(0, line.size() - 1);
                    continue;
                }
                if (line.empty() || line[0] == '#' || line[0] == '!')
                    continue;
                const size_t sep = line.find_first_of("=:");
                if (sep == std::string::npos)
                    continue;
                out[Trim(line.substr(0, sep))] = Trim(line.substr(sep + 1));
            }
            return out;
        }

        std::vector<std::string> Tokens(const std::string& s, const char* separators = " \t,") {
            std::vector<std::string> out;
            std::string cur;
            for (char c : s) {
                if (std::strchr(separators, c)) {
                    if (!cur.empty())
                        out.push_back(cur);
                    cur.clear();
                } else {
                    cur += c;
                }
            }
            if (!cur.empty())
                out.push_back(cur);
            return out;
        }

        bool ParseInt(std::string s, int& out) {
            s = Trim(s);
            if (s.size() >= 2 && s.front() == '(' && s.back() == ')')
                s = s.substr(1, s.size() - 2);
            if (s.empty())
                return false;
            char* end = nullptr;
            const long v = std::strtol(s.c_str(), &end, 10);
            if (!end || *end)
                return false;
            out = static_cast<int>(v);
            return true;
        }

        // ConnectedParser.parseRangeListInt: "0 2-4 6", negatives in brackets "(-64)-0"
        std::vector<std::pair<int, int>> ParseRanges(const std::string& s) {
            std::vector<std::pair<int, int>> out;
            for (const std::string& token : Tokens(s, " \t")) {
                size_t dash = std::string::npos;
                int depth = 0;
                for (size_t i = 0; i < token.size(); i++) {
                    if (token[i] == '(')
                        depth++;
                    else if (token[i] == ')')
                        depth--;
                    else if (token[i] == '-' && depth == 0 && i > 0) {
                        dash = i;
                        break;
                    }
                }
                int a = 0, b = 0;
                if (dash == std::string::npos) {
                    if (ParseInt(token, a))
                        out.emplace_back(a, a);
                } else if (ParseInt(token.substr(0, dash), a) && ParseInt(token.substr(dash + 1), b)) {
                    out.emplace_back(std::min(a, b), std::max(a, b));
                }
            }
            return out;
        }

        bool InRanges(const std::vector<std::pair<int, int>>& ranges, int v) {
            for (const auto& [a, b] : ranges)
                if (v >= a && v <= b)
                    return true;
            return false;
        }

        int NormalizeTime(int t) {
            while (t >= 24000)
                t -= 24000;
            while (t < 0)
                t += 24000;
            return t;
        }

        bool TimeBetween(int t, int a, int b) {
            return a <= b ? (t >= a && t <= b) : (t >= a || t <= b);
        }

        // CustomSkyLayer.parseTime: hh:mm, 6:00 is tick 0
        int ParseTime(const std::string& s) {
            if (s.empty())
                return -1;
            const size_t colon = s.find(':');
            int h = 0, m = 0;
            if (colon == std::string::npos || !ParseInt(s.substr(0, colon), h) || !ParseInt(s.substr(colon + 1), m) ||
                h < 0 || h > 23 || m < 0 || m > 59) {
                Log("sky", "invalid time: {}", s);
                return -1;
            }
            h -= 6;
            if (h < 0)
                h += 24;
            return h * 1000 + static_cast<int>(static_cast<double>(m) / 60.0 * 1000.0);
        }

        Blend ParseBlend(const std::string& s) {
            static const std::pair<const char*, Blend> kNames[] = {
                {"alpha", Blend::Alpha},       {"add", Blend::Add},         {"subtract", Blend::Subtract},
                {"multiply", Blend::Multiply}, {"dodge", Blend::Dodge},     {"burn", Blend::Burn},
                {"screen", Blend::Screen},     {"overlay", Blend::Overlay}, {"replace", Blend::Replace}};
            const std::string l = Lower(Trim(s));
            for (const auto& [name, blend] : kNames)
                if (l == name)
                    return blend;
            if (!l.empty())
                Log("sky", "unknown blend: {}", s);
            return Blend::Add;
        }

        std::string BiomeKey(std::string s) {
            s = Lower(s);
            if (s.starts_with("minecraft:"))
                s = s.substr(10);
            std::string out;
            for (char c : s)
                if (c != '_' && c != ' ')
                    out += c;
            return out;
        }

        // TextureUtils.fixResourcePath, relative to assets/minecraft
        std::string ResolvePath(std::string path, const std::string& directory) {
            path = Trim(path);
            if (path.starts_with("./"))
                return directory + "/" + path.substr(2);
            if (path.starts_with("~/"))
                return (directory.starts_with("mcpatcher/") ? "mcpatcher/" : "optifine/") + path.substr(2);
            if (path.starts_with("minecraft:"))
                return path.substr(10);
            if (path.starts_with("/"))
                return path.substr(1);
            return path;
        }
    }

    bool ParseLayer(const std::string& text, const std::string& directory, int index, Layer& out,
                    std::string& error) {
        const auto props = ReadProperties(text);
        const auto get = [&](const char* key, const char* fallback = "") {
            const auto it = props.find(key);
            return it == props.end() ? std::string(fallback) : it->second;
        };
        Layer l;
        l.source = ResolvePath(get("source", ("./sky" + std::to_string(index) + ".png").c_str()), directory);
        if (!l.source.ends_with(".png"))
            l.source += ".png";
        l.startFadeIn = ParseTime(get("startFadeIn"));
        l.endFadeIn = ParseTime(get("endFadeIn"));
        l.startFadeOut = ParseTime(get("startFadeOut"));
        l.endFadeOut = ParseTime(get("endFadeOut"));
        l.blend = ParseBlend(get("blend", "add"));
        l.rotate = Lower(get("rotate", "true")) != "false";
        if (const std::string speed = get("speed"); !speed.empty())
            l.speed = std::strtof(speed.c_str(), nullptr);
        if (const std::string axis = get("axis"); !axis.empty()) {
            const auto parts = Tokens(axis, " \t");
            if (parts.size() == 3) {
                const float x = std::strtof(parts[0].c_str(), nullptr), y = std::strtof(parts[1].c_str(), nullptr),
                            z = std::strtof(parts[2].c_str(), nullptr);
                // CustomSkyLayer.parseAxis keeps (z, y, -x): the axis in the sky's turned frame
                if (x * x + y * y + z * z >= 1e-5f)
                    l.axis = {z, y, -x};
            }
        }
        l.days = ParseRanges(get("days"));
        if (const std::string loop = get("daysLoop"); !loop.empty())
            ParseInt(loop, l.daysLoop);
        if (l.daysLoop <= 0)
            l.daysLoop = 8;
        const auto weather = Tokens(Lower(get("weather", "clear")), " \t");
        l.clear = std::find(weather.begin(), weather.end(), "clear") != weather.end();
        l.rain = std::find(weather.begin(), weather.end(), "rain") != weather.end();
        l.thunder = std::find(weather.begin(), weather.end(), "thunder") != weather.end();
        for (const std::string& b : Tokens(get("biomes"), " \t"))
            l.biomes.push_back(BiomeKey(b));
        l.heights = ParseRanges(get("heights"));
        if (const std::string t = get("transition"); !t.empty())
            l.transition = std::max(std::strtof(t.c_str(), nullptr), 0.0f);

        // CustomSkyLayer.isValid
        if (l.startFadeIn < 0 && l.endFadeIn < 0 && l.startFadeOut < 0 && l.endFadeOut < 0) {
            l.startFadeIn = l.endFadeIn = 0;
            l.startFadeOut = l.endFadeOut = 24000;
            l.timed = false;
        } else if (l.startFadeIn < 0 || l.endFadeIn < 0 || l.endFadeOut < 0) {
            error = "invalid times, required are startFadeIn, endFadeIn and endFadeOut";
            return false;
        } else {
            l.timed = true;
            const int fadeIn = NormalizeTime(l.endFadeIn - l.startFadeIn);
            if (l.startFadeOut < 0) {
                l.startFadeOut = NormalizeTime(l.endFadeOut - fadeIn);
                if (TimeBetween(l.startFadeOut, l.startFadeIn, l.endFadeIn))
                    l.startFadeOut = l.endFadeIn;
            }
        }
        if (l.speed < 0.0f) {
            error = "invalid speed";
            return false;
        }
        out = std::move(l);
        return true;
    }

    // CustomSkyLayer.getFadeBrightness
    float FadeBrightness(const Layer& l, int t) {
        if (!l.timed)
            return 1.0f;
        if (TimeBetween(t, l.startFadeIn, l.endFadeIn)) {
            const int span = NormalizeTime(l.endFadeIn - l.startFadeIn);
            return span == 0 ? 1.0f : static_cast<float>(NormalizeTime(t - l.startFadeIn)) / static_cast<float>(span);
        }
        if (TimeBetween(t, l.endFadeIn, l.startFadeOut))
            return 1.0f;
        if (TimeBetween(t, l.startFadeOut, l.endFadeOut)) {
            const int span = NormalizeTime(l.endFadeOut - l.startFadeOut);
            return span == 0 ? 0.0f
                             : 1.0f - static_cast<float>(NormalizeTime(t - l.startFadeOut)) / static_cast<float>(span);
        }
        return 0.0f;
    }

    std::vector<Draw> Evaluate(std::vector<Layer>& layers, const Inputs& in) {
        std::vector<Draw> out;
        const int timeOfDay = NormalizeTime(static_cast<int>(in.worldTime % 24000));
        const std::string biome = BiomeKey(in.biome);
        for (Layer& l : layers) {
            Draw d;
            d.texture = l.texture;
            d.cubemap = l.cubemap;
            d.blend = l.blend;
            if (l.cubemap) {
                out.push_back(d);
                continue;
            }
            // CustomSkyLayer.isActive
            if (l.timed && TimeBetween(timeOfDay, l.endFadeOut, l.startFadeIn))
                continue;
            if (!l.days.empty()) {
                int64_t t = in.worldTime - l.startFadeIn;
                while (t < 0)
                    t += static_cast<int64_t>(24000) * l.daysLoop;
                if (!InRanges(l.days, static_cast<int>((t / 24000) % l.daysLoop)))
                    continue;
            }
            // getPositionBrightness, smoothed over `transition` seconds
            float position = 1.0f;
            if (!l.biomes.empty() || !l.heights.empty()) {
                bool inside = true;
                if (!l.biomes.empty())
                    inside = std::find(l.biomes.begin(), l.biomes.end(), biome) != l.biomes.end();
                if (inside && !l.heights.empty())
                    inside = InRanges(l.heights, static_cast<int>(std::floor(in.eyeY)));
                const float target = inside ? 1.0f : 0.0f;
                if (l.positionBrightness < 0.0f || l.transition <= 0.0f)
                    l.positionBrightness = target;
                else {
                    const float step = in.dt / l.transition;
                    l.positionBrightness += std::clamp(target - l.positionBrightness, -step, step);
                }
                position = l.positionBrightness;
            }
            // getWeatherBrightness
            float weather = 0.0f;
            if (l.clear)
                weather += 1.0f - in.rain;
            if (l.rain)
                weather += in.rain - in.thunder;
            if (l.thunder)
                weather += in.thunder;
            weather = std::clamp(weather, 0.0f, 1.0f);
            d.brightness = std::clamp(position * weather * FadeBrightness(l, timeOfDay), 0.0f, 1.0f);
            if (d.brightness < 1e-4f)
                continue;
            if (l.rotate) {
                float offset = 0.0f;
                if (l.speed != std::round(l.speed)) {
                    const int64_t days = (in.worldTime + 18000) / 24000;
                    offset = static_cast<float>(
                        std::fmod(static_cast<double>(std::fmod(l.speed, 1.0f)) * static_cast<double>(days), 1.0));
                }
                d.rotation = glm::rotate(glm::mat4(1.0f), glm::radians(360.0f * (offset + in.celestialAngle * l.speed)),
                                         l.axis);
            }
            out.push_back(d);
        }
        return out;
    }
}
