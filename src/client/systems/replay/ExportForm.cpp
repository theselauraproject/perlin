#include "client/systems/replay/ExportForm.hpp"

#include "util/Strings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace perlin::replay {
    namespace {
        std::string Num(double v, int decimals) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
            return buf;
        }

        std::string TrimZeros(std::string v) {
            if (v.contains('.')) {
                while (!v.empty() && v.back() == '0')
                    v.pop_back();
                if (!v.empty() && v.back() == '.')
                    v.pop_back();
            }
            return v;
        }

        template <class T> int Wrap(int i, int dir, const std::vector<T>& v) {
            const int n = static_cast<int>(v.size());
            return n ? ((i + dir) % n + n) % n : 0;
        }

        int ClampSide(double side) {
            return std::clamp(static_cast<int>(std::lround(side)), kMinExportSide, kMaxExportSide);
        }

        const AspectRatio& AspectOf(const ExportConfig& c) {
            const auto& all = AspectRatios();
            return all[static_cast<size_t>(std::clamp(c.aspectIdx, 0, static_cast<int>(all.size()) - 1))];
        }

        bool ParseBoundedInt(const std::string& text, int lo, int hi, int& out) {
            if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
                return false;
            const long long v = std::stoll(text.substr(0, 9));
            if (v < lo)
                return false;
            out = static_cast<int>(std::min<long long>(v, hi));
            return true;
        }

        constexpr std::array<std::string_view, 14> kTextFields = {
            "ex_w",  "ex_h",      "ex_fps", "ex_start", "ex_end", "ex_name", "ex_dir",
            "ex_br", "ex_preset", "ex_pix", "ex_enc",   "ex_mux", "ex_ext",  "ex_extra"};
    }

    bool IsExportTextField(std::string_view id) {
        return std::ranges::find(kTextFields, id) != kTextFields.end();
    }

    std::string ExportFieldText(const ExportConfig& c, std::string_view id) {
        if (id == "ex_w")
            return std::to_string(c.width);
        if (id == "ex_h")
            return std::to_string(c.height);
        if (id == "ex_fps")
            return FpsLabel(c.fps);
        if (id == "ex_start")
            return TrimZeros(Num(c.customStartSec, 3));
        if (id == "ex_end")
            return TrimZeros(Num(c.customEndSec, 3));
        if (id == "ex_name")
            return c.nameTemplate;
        if (id == "ex_dir")
            return c.outputDir;
        if (id == "ex_br")
            return std::to_string(c.bitrateKbps);
        if (id == "ex_preset")
            return c.preset;
        if (id == "ex_pix")
            return c.pixelFormat;
        if (id == "ex_enc")
            return c.encoderOverride;
        if (id == "ex_mux")
            return c.muxerOverride;
        if (id == "ex_ext")
            return c.extensionOverride;
        if (id == "ex_extra")
            return c.extraArgs;
        return {};
    }

    void ApplyAspectToHeight(ExportConfig& c, const AspectRatio& aspect) {
        if (aspect.w > 0)
            c.width = ClampSide(static_cast<double>(c.height) * aspect.w / aspect.h);
    }

    void ApplyAspectToWidth(ExportConfig& c, const AspectRatio& aspect) {
        if (aspect.w > 0)
            c.height = ClampSide(static_cast<double>(c.width) * aspect.h / aspect.w);
    }

    void CommitExportField(ExportConfig& c, std::string_view id, const std::string& text) {
        int v = 0;
        if (id == "ex_w") {
            if (ParseBoundedInt(text, kMinExportSide, kMaxExportSide, v)) {
                c.width = v;
                ApplyAspectToWidth(c, AspectOf(c));
            }
        } else if (id == "ex_h") {
            if (ParseBoundedInt(text, kMinExportSide, kMaxExportSide, v)) {
                c.height = v;
                ApplyAspectToHeight(c, AspectOf(c));
            }
        } else if (id == "ex_fps") {
            const double f = ParseFps(text);
            if (f > 0.0 && f <= 1000.0)
                c.fps = f;
        } else if (id == "ex_start") {
            if (const auto parsed = str::ParseDouble(text, false))
                c.customStartSec = std::max(0.0, *parsed);
        } else if (id == "ex_end") {
            if (const auto parsed = str::ParseDouble(text, false))
                c.customEndSec = std::max(0.0, *parsed);
        } else if (id == "ex_name") {
            c.nameTemplate = text;
        } else if (id == "ex_dir") {
            c.outputDir = text;
        } else if (id == "ex_br") {
            if (text.empty())
                c.bitrateKbps = 0;
            else if (ParseBoundedInt(text, 0, 2000000, v))
                c.bitrateKbps = v;
        } else if (id == "ex_preset") {
            c.preset = text;
        } else if (id == "ex_pix") {
            c.pixelFormat = text;
        } else if (id == "ex_enc") {
            c.encoderOverride = text;
        } else if (id == "ex_mux") {
            c.muxerOverride = text;
        } else if (id == "ex_ext") {
            c.extensionOverride = text;
        } else if (id == "ex_extra") {
            c.extraArgs = text;
        }
    }

    ExportDerived DeriveExportForm(ExportConfig& c, double durationMs, double inMs, double outMs) {
        ExportDerived d;
        d.container = ffmpeg::FindContainer(c.container);
        if (!d.container) {
            c.container = "mp4";
            d.container = ffmpeg::FindContainer("mp4");
        }
        d.custom = c.container == "custom";
        d.imageSequence = d.container->imageSequence;
        if (!d.imageSequence && !d.custom && !d.container->codecs.empty() &&
            std::ranges::find(d.container->codecs, c.codec) == d.container->codecs.end())
            c.codec = d.container->codecs.front();
        d.codec = ffmpeg::FindCodec(c.codec);

        double start = 0, end = durationMs;
        if (c.range == RangeMode::InOut) {
            if (inMs >= 0)
                start = inMs;
            if (outMs >= 0)
                end = outMs;
        } else if (c.range == RangeMode::Custom) {
            start = c.customStartSec * 1000.0;
            end = c.customEndSec * 1000.0;
        }
        d.rangeStartMs = std::clamp(start, 0.0, durationMs);
        d.rangeEndMs = std::clamp(end, 0.0, durationMs);

        d.aspect = &AspectOf(c);
        const auto& resolutions = ResolutionPresets();
        d.resolutionPreset = static_cast<int>(resolutions.size()) - 1;
        for (size_t i = 0; i + 1 < resolutions.size(); i++) {
            const Resolution& r = resolutions[i];
            const bool widthMatches =
                d.aspect->w == 0
                    ? c.width == r.w
                    : std::abs(c.width - static_cast<int>(std::lround(static_cast<double>(r.h) * d.aspect->w /
                                                                      d.aspect->h))) <= 1;
            if (c.height == r.h && widthMatches)
                d.resolutionPreset = static_cast<int>(i);
        }
        const auto& rates = FpsPresets();
        for (size_t i = 0; i < rates.size(); i++)
            if (std::fabs(rates[i] - c.fps) < 1e-3)
                d.fpsPreset = static_cast<int>(i);
        return d;
    }

    bool SplitCycleId(std::string_view id, std::string& base, int& direction) {
        if (id.size() < 3 || id[id.size() - 2] != ':')
            return false;
        const char c = id.back();
        if (c != '-' && c != '+' && c != 'n')
            return false;
        base = std::string(id.substr(0, id.size() - 2));
        direction = c == '-' ? -1 : 1;
        return true;
    }

    bool StepExportOption(ExportConfig& c, std::string_view option, int dir, bool coarse,
                          const ExportDerived& d) {
        if (option == "ex_container") {
            const auto& all = ffmpeg::Containers();
            int idx = 0;
            for (size_t i = 0; i < all.size(); i++)
                if (all[i].id == c.container)
                    idx = static_cast<int>(i);
            c.container = all[static_cast<size_t>(Wrap(idx, dir, all))].id;
        } else if (option == "ex_codec") {
            if (!d.container || d.container->codecs.empty())
                return false;
            const auto& codecs = d.container->codecs;
            int idx = 0;
            for (size_t i = 0; i < codecs.size(); i++)
                if (codecs[i] == c.codec)
                    idx = static_cast<int>(i);
            c.codec = codecs[static_cast<size_t>(Wrap(idx, dir, codecs))];
        } else if (option == "ex_res") {
            const Resolution& r =
                ResolutionPresets()[static_cast<size_t>(Wrap(d.resolutionPreset, dir, ResolutionPresets()))];
            if (r.h > 0) {
                c.height = r.h;
                if (d.aspect->w > 0)
                    ApplyAspectToHeight(c, *d.aspect);
                else
                    c.width = r.w;
            }
        } else if (option == "ex_aspect") {
            c.aspectIdx = Wrap(c.aspectIdx, dir, AspectRatios());
            const AspectRatio& next = AspectRatios()[static_cast<size_t>(c.aspectIdx)];
            ApplyAspectToHeight(c, next);
        } else if (option == "ex_fpsp") {
            const auto& rates = FpsPresets();
            int idx = d.fpsPreset;
            if (idx < 0) {
                idx = 0;
                while (idx + 1 < static_cast<int>(rates.size()) && rates[static_cast<size_t>(idx)] < c.fps)
                    idx++;
                if (dir > 0)
                    idx--;
            }
            c.fps = rates[static_cast<size_t>(Wrap(idx, dir, rates))];
        } else if (option == "ex_ssaa") {
            c.ssaa = (c.ssaa - 1 + dir + 4) % 4 + 1;
        } else if (option == "ex_range") {
            c.range = static_cast<RangeMode>((static_cast<int>(c.range) + dir + 3) % 3);
        } else if (option == "ex_crf") {
            c.crf = std::clamp(c.crf + dir * (coarse ? 5 : 1), 0, 51);
        } else {
            return false;
        }
        return true;
    }
}
