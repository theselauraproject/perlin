#include "client/systems/replay/ExportForm.hpp"

#include "client/systems/replay/Timeline.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace perlin;
using namespace perlin::replay;

namespace {
    int failures = 0;

    std::string Num(double v, int decimals = 0) {
        char buf[48];
        std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
        return buf;
    }
    std::string Trim(std::string v, bool zeros) {
        if (zeros && v.contains('.')) {
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

    std::string RefFieldValue(const ExportConfig& c, const std::string& id) {
        if (id == "ex_w")
            return std::to_string(c.width);
        if (id == "ex_h")
            return std::to_string(c.height);
        if (id == "ex_fps")
            return FpsLabel(c.fps);
        if (id == "ex_start")
            return Trim(Num(c.customStartSec, 3), true);
        if (id == "ex_end")
            return Trim(Num(c.customEndSec, 3), true);
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

    void RefCommit(ExportConfig& c, const std::string& id, const std::string& text) {
        const AspectRatio& asp = AspectRatios()[static_cast<size_t>(
            std::clamp(c.aspectIdx, 0, static_cast<int>(AspectRatios().size()) - 1))];
        const auto fromHeight = [&] {
            if (asp.w > 0)
                c.width = std::clamp(
                    static_cast<int>(std::lround(static_cast<double>(c.height) * asp.w / asp.h)), 16, 16384);
        };
        const auto fromWidth = [&] {
            if (asp.w > 0)
                c.height = std::clamp(
                    static_cast<int>(std::lround(static_cast<double>(c.width) * asp.h / asp.w)), 16, 16384);
        };
        const auto asInt = [&](int lo, int hi, int& out) {
            if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
                return false;
            const long long v = std::stoll(text.substr(0, 9));
            if (v < lo)
                return false;
            out = static_cast<int>(std::min<long long>(v, hi));
            return true;
        };
        int v = 0;
        if (id == "ex_w") {
            if (asInt(16, 16384, v)) {
                c.width = v;
                fromWidth();
            }
        } else if (id == "ex_h") {
            if (asInt(16, 16384, v)) {
                c.height = v;
                fromHeight();
            }
        } else if (id == "ex_fps") {
            const double f = ParseFps(text);
            if (f > 0.0 && f <= 1000.0)
                c.fps = f;
        } else if (id == "ex_start") {
            if (const auto p = str::ParseDouble(text, false))
                c.customStartSec = std::max(0.0, *p);
        } else if (id == "ex_end") {
            if (const auto p = str::ParseDouble(text, false))
                c.customEndSec = std::max(0.0, *p);
        } else if (id == "ex_name")
            c.nameTemplate = text;
        else if (id == "ex_dir")
            c.outputDir = text;
        else if (id == "ex_br") {
            if (text.empty())
                c.bitrateKbps = 0;
            else if (asInt(0, 2000000, v))
                c.bitrateKbps = v;
        } else if (id == "ex_preset")
            c.preset = text;
        else if (id == "ex_pix")
            c.pixelFormat = text;
        else if (id == "ex_enc")
            c.encoderOverride = text;
        else if (id == "ex_mux")
            c.muxerOverride = text;
        else if (id == "ex_ext")
            c.extensionOverride = text;
        else if (id == "ex_extra")
            c.extraArgs = text;
    }

    struct RefFrame {
        const ffmpeg::Container* cont;
        double rs, re;
        int presetIdx, fpsIdx;
        const AspectRatio* asp;
    };

    RefFrame RefDerive(ExportConfig& c, double dur, double inMs, double outMs) {
        RefFrame f;
        f.cont = ffmpeg::FindContainer(c.container);
        if (!f.cont) {
            c.container = "mp4";
            f.cont = ffmpeg::FindContainer("mp4");
        }
        const bool custom = c.container == "custom";
        const bool seq = f.cont->imageSequence;
        if (!seq && !custom && !f.cont->codecs.empty() &&
            std::ranges::find(f.cont->codecs, c.codec) == f.cont->codecs.end())
            c.codec = f.cont->codecs.front();
        f.rs = 0;
        f.re = dur;
        if (c.range == RangeMode::InOut) {
            if (inMs >= 0)
                f.rs = inMs;
            if (outMs >= 0)
                f.re = outMs;
        } else if (c.range == RangeMode::Custom) {
            f.rs = c.customStartSec * 1000.0;
            f.re = c.customEndSec * 1000.0;
        }
        f.rs = std::clamp(f.rs, 0.0, dur);
        f.re = std::clamp(f.re, 0.0, dur);
        f.asp = &AspectRatios()[static_cast<size_t>(
            std::clamp(c.aspectIdx, 0, static_cast<int>(AspectRatios().size()) - 1))];
        f.presetIdx = static_cast<int>(ResolutionPresets().size()) - 1;
        for (size_t i = 0; i + 1 < ResolutionPresets().size(); i++) {
            const Resolution& r = ResolutionPresets()[i];
            if (c.height == r.h &&
                (f.asp->w == 0 ? c.width == r.w
                               : std::abs(c.width - static_cast<int>(std::lround(static_cast<double>(r.h) *
                                                                                 f.asp->w / f.asp->h))) <= 1))
                f.presetIdx = static_cast<int>(i);
        }
        f.fpsIdx = -1;
        for (size_t i = 0; i < FpsPresets().size(); i++)
            if (std::fabs(FpsPresets()[i] - c.fps) < 1e-3)
                f.fpsIdx = static_cast<int>(i);
        return f;
    }

    void RefStep(ExportConfig& c, const std::string& base, int dir, bool shift, const RefFrame& f) {
        const AspectRatio& asp = *f.asp;
        const auto fromHeight = [&] {
            if (asp.w > 0)
                c.width = std::clamp(
                    static_cast<int>(std::lround(static_cast<double>(c.height) * asp.w / asp.h)), 16, 16384);
        };
        if (base == "ex_container") {
            const auto& all = ffmpeg::Containers();
            int idx = 0;
            for (size_t i = 0; i < all.size(); i++)
                if (all[i].id == c.container)
                    idx = static_cast<int>(i);
            c.container = all[static_cast<size_t>(Wrap(idx, dir, all))].id;
        } else if (base == "ex_codec" && f.cont && !f.cont->codecs.empty()) {
            int idx = 0;
            for (size_t i = 0; i < f.cont->codecs.size(); i++)
                if (f.cont->codecs[i] == c.codec)
                    idx = static_cast<int>(i);
            c.codec = f.cont->codecs[static_cast<size_t>(Wrap(idx, dir, f.cont->codecs))];
        } else if (base == "ex_res") {
            const int n = Wrap(f.presetIdx, dir, ResolutionPresets());
            const Resolution& r = ResolutionPresets()[static_cast<size_t>(n)];
            if (r.h > 0) {
                c.height = r.h;
                if (asp.w > 0)
                    fromHeight();
                else
                    c.width = r.w;
            }
        } else if (base == "ex_aspect") {
            c.aspectIdx = Wrap(c.aspectIdx, dir, AspectRatios());
            const AspectRatio& na = AspectRatios()[static_cast<size_t>(c.aspectIdx)];
            if (na.w > 0)
                c.width = std::clamp(
                    static_cast<int>(std::lround(static_cast<double>(c.height) * na.w / na.h)), 16, 16384);
        } else if (base == "ex_fpsp") {
            int idx = f.fpsIdx;
            if (idx < 0) {
                idx = 0;
                while (idx + 1 < static_cast<int>(FpsPresets().size()) &&
                       FpsPresets()[static_cast<size_t>(idx)] < c.fps)
                    idx++;
                if (dir > 0)
                    idx--;
            }
            c.fps = FpsPresets()[static_cast<size_t>(Wrap(idx, dir, FpsPresets()))];
        } else if (base == "ex_ssaa") {
            c.ssaa = (c.ssaa - 1 + dir + 4) % 4 + 1;
        } else if (base == "ex_range") {
            c.range = static_cast<RangeMode>((static_cast<int>(c.range) + dir + 3) % 3);
        } else if (base == "ex_crf") {
            c.crf = std::clamp(c.crf + dir * (shift ? 5 : 1), 0, 51);
        }
    }

    std::string RandomText(std::mt19937& rng) {
        static const char* const samples[] = {
            "",     "0",          "7",       "15",      "16",           "17",
            "640",  "1920",       "99999",   "16385",   "123456789012", "12a",
            "-5",   "2000000",    "2000001", "59.94",   "24",           "30000/1001",
            "1001", "0.5",        "1e3",     "abc",     "  ",           "3.14159",
            "9",    "mp4_{date}", "C:\\out", "libx264", "yuv420p"};
        return samples[rng() % (sizeof samples / sizeof *samples)];
    }

    void Compare(const ExportConfig& a, const ExportConfig& b, int step, const char* what) {
        if (a.ToJson() != b.ToJson()) {
            std::printf("FAIL %s differs at step %d\n", what, step);
            failures++;
            std::exit(1);
        }
    }
}

int main() {
    std::mt19937 rng(42);
    const char* const textIds[] = {"ex_w",    "ex_h",   "ex_fps", "ex_start",  "ex_end",
                                   "ex_name", "ex_dir", "ex_br",  "ex_preset", "ex_pix",
                                   "ex_enc",  "ex_mux", "ex_ext", "ex_extra",  "ex_nope"};
    const char* const stepIds[] = {"ex_container", "ex_codec", "ex_res", "ex_aspect", "ex_fpsp",
                                   "ex_ssaa",      "ex_range", "ex_crf", "ex_nope"};
    ExportConfig ref, got;
    for (int step = 0; step < 60000; step++) {
        const int kind = rng() % 4;
        if (kind == 0) {
            const std::string id = textIds[rng() % 15];
            const std::string text = RandomText(rng);
            RefCommit(ref, id, text);
            CommitExportField(got, id, text);
            if (RefFieldValue(ref, id) != ExportFieldText(got, id)) {
                std::printf("FAIL field text %s at %d\n", id.c_str(), step);
                failures++;
            }
        } else if (kind == 1) {
            const double dur = 1000.0 * (rng() % 600);
            const double in = static_cast<int>(rng() % 3) - 1 == 0 ? -1 : 1000.0 * (rng() % 700);
            const double out = static_cast<int>(rng() % 3) - 1 == 0 ? -1 : 1000.0 * (rng() % 700);
            if (rng() % 7 == 0) {
                static const char* const containers[] = {"mp4", "nonsense", "custom", "gif",
                                                         "png", "webm",     "avi"};
                ref.container = got.container = containers[rng() % 7];
                static const char* const codecs[] = {"h264", "vp9", "zzz", "png"};
                ref.codec = got.codec = codecs[rng() % 4];
            }
            const RefFrame rf = RefDerive(ref, dur, in, out);
            const ExportDerived d = DeriveExportForm(got, dur, in, out);
            const bool same = rf.cont == d.container && rf.rs == d.rangeStartMs && rf.re == d.rangeEndMs &&
                              rf.presetIdx == d.resolutionPreset && rf.fpsIdx == d.fpsPreset &&
                              rf.asp == d.aspect && ffmpeg::FindCodec(ref.codec) == d.codec;
            if (!same) {
                std::printf("FAIL derive at %d\n", step);
                failures++;
                return 1;
            }
            const std::string base = stepIds[rng() % 9];
            const int dir = rng() % 2 ? 1 : -1;
            const bool shift = rng() % 2;
            RefStep(ref, base, dir, shift, rf);
            StepExportOption(got, base, dir, shift, d);
        } else if (kind == 2) {
            ref.aspectIdx = got.aspectIdx = static_cast<int>(rng() % 12) - 2;
            ref.width = got.width = 16 + static_cast<int>(rng() % 4000);
            ref.height = got.height = 16 + static_cast<int>(rng() % 4000);
            if (rng() % 2) {
                const Resolution& r = ResolutionPresets()[rng() % (ResolutionPresets().size() - 1)];
                const AspectRatio& a = AspectRatios()[static_cast<size_t>(
                    std::clamp(ref.aspectIdx, 0, static_cast<int>(AspectRatios().size()) - 1))];
                const int expected =
                    a.w == 0 ? r.w : static_cast<int>(std::lround(static_cast<double>(r.h) * a.w / a.h));
                ref.height = got.height = r.h;
                ref.width = got.width = std::max(16, expected + static_cast<int>(rng() % 5) - 2);
            }
        } else {
            ref.range = got.range = static_cast<RangeMode>(rng() % 3);
            ref.customStartSec = got.customStartSec = (rng() % 5000) / 100.0;
            ref.customEndSec = got.customEndSec = (rng() % 5000) / 100.0;
            ref.fps = got.fps =
                rng() % 3 == 0 ? FpsPresets()[rng() % FpsPresets().size()] : 1.0 + (rng() % 20000) / 100.0;
        }
        Compare(ref, got, step, "config");
    }
    for (const char* id : {"ex_w", "ex_extra", "ex_gui", "ex_dl", "x"})
        if (IsExportTextField(id) != (std::string(id) == "ex_w" || std::string(id) == "ex_extra")) {
            std::printf("FAIL IsExportTextField %s\n", id);
            failures++;
        }
    std::string base;
    int dir = 0;
    if (!SplitCycleId("ex_res:+", base, dir) || base != "ex_res" || dir != 1 ||
        !SplitCycleId("ex_crf:-", base, dir) || dir != -1 || !SplitCycleId("ex_x:n", base, dir) || dir != 1 ||
        SplitCycleId("ex_res", base, dir) || SplitCycleId("a:q", base, dir) ||
        SplitCycleId(":+", base, dir)) {
        std::printf("FAIL SplitCycleId\n");
        failures++;
    }
    std::printf(failures ? "export_form_selftest FAILED\n" : "export_form_selftest passed\n");
    return failures ? 1 : 0;
}
