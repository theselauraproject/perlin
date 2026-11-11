#include "client/PerlinInternal.hpp"
#include "client/Perlin.hpp"

#include "client/input/Keys.hpp"
#include "client/replay/LibavEncoder.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/ui/ReplayScreen.hpp"
#include "client/ui/TextEdit.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include "util/Log.hpp"

namespace perlin {
    namespace fs = std::filesystem;

    namespace {
        using Clock = std::chrono::steady_clock;
        using ui::PanelRow;

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

        PanelRow Header(const std::string& label) {
            PanelRow r;
            r.kind = PanelRow::Kind::Header;
            r.label = label;
            return r;
        }
        PanelRow Info(const std::string& label, const std::string& value = {}) {
            PanelRow r;
            r.kind = PanelRow::Kind::Info;
            r.label = label;
            r.value = value;
            return r;
        }
        PanelRow Cycle(const std::string& id, const std::string& label, const std::string& value,
                       bool enabled = true) {
            PanelRow r;
            r.kind = PanelRow::Kind::Cycle;
            r.id = id;
            r.label = label;
            r.value = value;
            r.enabled = enabled;
            return r;
        }
        PanelRow Field(const std::string& id, const std::string& label, const std::string& value,
                       bool focused, bool enabled = true) {
            PanelRow r;
            r.kind = PanelRow::Kind::Field;
            r.id = id;
            r.label = label;
            r.value = value;
            r.focused = focused;
            r.enabled = enabled;
            return r;
        }
        PanelRow Toggle(const std::string& id, const std::string& label, bool on, bool enabled = true) {
            PanelRow r;
            r.kind = PanelRow::Kind::Toggle;
            r.id = id;
            r.label = label;
            r.on = on;
            r.enabled = enabled;
            return r;
        }
        PanelRow Btn(const std::string& id, const std::string& label, bool enabled = true) {
            PanelRow r;
            r.kind = PanelRow::Kind::Button;
            r.id = id;
            r.label = label;
            r.enabled = enabled;
            return r;
        }

        bool SplitCycle(const std::string& hover, std::string& base, int& dir) {
            if (hover.size() < 3 || hover[hover.size() - 2] != ':')
                return false;
            const char c = hover.back();
            if (c != '-' && c != '+' && c != 'n')
                return false;
            base = hover.substr(0, hover.size() - 2);
            dir = c == '-' ? -1 : 1;
            return true;
        }

        template <class T> int Wrap(int i, int dir, const std::vector<T>& v) {
            const int n = static_cast<int>(v.size());
            return n ? ((i + dir) % n + n) % n : 0;
        }
    }

    void Perlin::ExportOpenMenu() {
        LoadExportConfig();
        if (!exportUi.catQueried) {
            exportUi.catQueried = true;
            exportUi.cat = replay::ffmpeg::LibavCompiledIn()
                               ? replay::ffmpeg::QueryLibav()
                               : replay::ffmpeg::Query(replay::ffmpeg::FindExecutable());
        }
        exportUi.open = true;
        exportUi.tab = 0;
        exportUi.scroll = 0;
        exportUi.focus.clear();
        exportUi.signature.clear();
        exportUi.leftDown = true;
        exportUi.openedAt = Clock::now();
        exportUi.status.clear();
        if (hud)
            hud->Overlay().ClearOverlay();
    }

    bool Perlin::UpdateExportMenu(ui::Hud& h, int fbW, int fbH) {
        using namespace replay;
        ExportUi& u = exportUi;
        ExportConfig& c = u.cfg;
        const auto now = Clock::now();
        const auto [scale, guiW, guiH] = detail::OverlayViewport(h, fbW, fbH);

        const auto closeMenu = [&] {
            u.open = false;
            u.focus.clear();
            SaveExportConfig();
            h.Overlay().ClearOverlay();
            replayUi.signature.clear();
            replayUi.openedAt = now;
            replayUi.leftDown = true;
        };

        if (u.downloader && u.downloader->Finished() && !u.downloadHandled) {
            u.downloadHandled = true;
            u.downloader->Join();
            if (u.downloader->Succeeded()) {
                u.cat = ffmpeg::LibavCompiledIn() ? ffmpeg::QueryLibav()
                                                  : ffmpeg::Query(ffmpeg::FindExecutable());
                u.status =
                    u.cat.available ? "ffmpeg is ready" : "ffmpeg was installed but could not be started";
            } else {
                u.status = u.downloader->Status();
            }
        }
        if (now - u.caretAt > std::chrono::milliseconds(500)) {
            u.caretAt = now;
            u.caret = !u.caret;
        }

        const auto fieldValue = [&](const std::string& id) -> std::string {
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
        };
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
        const auto commitField = [&](const std::string& id, const std::string& text) {
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
                try {
                    c.customStartSec = std::max(0.0, std::stod(text));
                } catch (...) {}
            } else if (id == "ex_end") {
                try {
                    c.customEndSec = std::max(0.0, std::stod(text));
                } catch (...) {}
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
        };

        for (const auto& t : inputManager.TakeTyped()) {
            if (!t.character && t.key == Key::Escape) {
                if (!u.focus.empty())
                    u.focus.clear();
                else if (now - u.openedAt > std::chrono::milliseconds(120)) {
                    closeMenu();
                    return true;
                }
            } else if (!u.focus.empty()) {
                std::string& buf = u.text[u.focus];
                if (!t.character && (t.key == Key::Enter || t.key == Key::Tab))
                    u.focus.clear();
                else
                    ui::TextEdit::Focused().Apply(t, buf);
                if (!u.focus.empty())
                    commitField(u.focus, buf);
            }
        }
        {
            const float wheel = inputManager.Scroll();
            if (wheel != 0.0f)
                u.scroll = std::max(0, u.scroll - static_cast<int>(std::lround(wheel)));
        }

        const ffmpeg::Container* cont = ffmpeg::FindContainer(c.container);
        if (!cont) {
            c.container = "mp4";
            cont = ffmpeg::FindContainer("mp4");
        }
        const bool custom = c.container == "custom";
        const bool seq = cont->imageSequence;
        if (!seq && !custom && !cont->codecs.empty() &&
            std::find(cont->codecs.begin(), cont->codecs.end(), c.codec) == cont->codecs.end())
            c.codec = cont->codecs.front();
        const ffmpeg::Codec* codec = ffmpeg::FindCodec(c.codec);
        const double dur = replayCtl.DurationMs();
        double rs = 0, re = dur;
        if (c.range == RangeMode::InOut) {
            if (replayUi.inMs >= 0)
                rs = replayUi.inMs;
            if (replayUi.outMs >= 0)
                re = replayUi.outMs;
        } else if (c.range == RangeMode::Custom) {
            rs = c.customStartSec * 1000.0;
            re = c.customEndSec * 1000.0;
        }
        rs = std::clamp(rs, 0.0, dur);
        re = std::clamp(re, 0.0, dur);
        const long long frames = re > rs ? CountExportFrames(timeline, rs, re, c.fps) : 0;
        int presetIdx = static_cast<int>(ResolutionPresets().size()) - 1;
        for (size_t i = 0; i + 1 < ResolutionPresets().size(); i++) {
            const Resolution& r = ResolutionPresets()[i];
            if (c.height == r.h &&
                (asp.w == 0 ? c.width == r.w
                            : std::abs(c.width - static_cast<int>(std::lround(static_cast<double>(r.h) *
                                                                              asp.w / asp.h))) <= 1))
                presetIdx = static_cast<int>(i);
        }
        int fpsIdx = -1;
        for (size_t i = 0; i < FpsPresets().size(); i++)
            if (std::fabs(FpsPresets()[i] - c.fps) < 1e-3)
                fpsIdx = static_cast<int>(i);

        ui::PanelModel m;
        m.title = "Export Replay";
        m.status = u.status.size() > 52 ? u.status.substr(0, 52) + "..." : u.status;
        m.tabs = {"Video", "Output", "Advanced"};
        m.tab = u.tab;
        m.caret = u.caret;
        const auto fieldRow = [&](const std::string& id, const std::string& label, bool enabled = true) {
            const bool f = u.focus == id;
            return Field(id, label, f ? u.text[id] : fieldValue(id), f, enabled);
        };
        const auto containerLabel = [&](const ffmpeg::Container& k) {
            std::string l = k.label;
            if (u.cat.available && !k.imageSequence && !k.muxer.empty() && !u.cat.HasMuxer(k.muxer))
                l += " (unavailable)";
            return l;
        };
        if (u.tab == 0) {
            m.rows.push_back(
                u.cat.available
                    ? Info(u.cat.builtin ? "Encoder (built in)" : "ffmpeg", u.cat.version.substr(0, 40))
                    : Info("ffmpeg not found", "using built-in AVI (Advanced: get ffmpeg)"));
            m.rows.push_back(Header("Format"));
            m.rows.push_back(Cycle("ex_container", "Container", containerLabel(*cont)));
            if (!seq && !custom) {
                std::string label = codec ? codec->label : c.codec;
                if (codec && u.cat.available && !u.cat.HasEncoder(codec->encoder)) {
                    bool alt = false;
                    for (const std::string& f : codec->fallbackEncoders)
                        alt = alt || u.cat.HasEncoder(f);
                    if (!alt)
                        label += " (missing)";
                }
                m.rows.push_back(Cycle("ex_codec", "Codec", label));
            } else if (custom) {
                m.rows.push_back(Info("Set encoder and muxer on the Advanced tab"));
            }
            m.rows.push_back(Header("Picture"));
            m.rows.push_back(
                Cycle("ex_res", "Resolution", ResolutionPresets()[static_cast<size_t>(presetIdx)].label));
            m.rows.push_back(Cycle("ex_aspect", "Aspect ratio", asp.label));
            m.rows.push_back(fieldRow("ex_w", "Width"));
            m.rows.push_back(fieldRow("ex_h", "Height"));
            m.rows.push_back(
                Cycle("ex_fpsp", "Frame rate", fpsIdx >= 0 ? FpsLabel(c.fps) + " fps" : "Custom"));
            m.rows.push_back(fieldRow("ex_fps", "FPS (any value)"));
            m.rows.push_back(
                Cycle("ex_ssaa", "Supersampling", c.ssaa == 1 ? "Off" : std::to_string(c.ssaa) + "x"));
            m.rows.push_back(Info("Rendered at", std::to_string(c.width * c.ssaa) + "x" +
                                                     std::to_string(c.height * c.ssaa)));
        } else if (u.tab == 1) {
            static const char* const kRange[] = {"Whole replay", "Between in / out points",
                                                 "Custom (seconds)"};
            m.rows.push_back(Header("Range"));
            m.rows.push_back(Cycle("ex_range", "Range", kRange[static_cast<int>(c.range)]));
            if (c.range == RangeMode::Custom) {
                m.rows.push_back(fieldRow("ex_start", "Start (s)"));
                m.rows.push_back(fieldRow("ex_end", "End (s)"));
            }
            m.rows.push_back(
                Info("Replay span", ui::FormatReplayTime(rs) + " - " + ui::FormatReplayTime(re)));
            m.rows.push_back(Info("Output", std::to_string(frames) + " frames, " +
                                                Num(static_cast<double>(frames) / c.fps, 1) + " s"));
            m.rows.push_back(Header("Rendering"));
            m.rows.push_back(Toggle("ex_gui", "Hide GUI", c.hideGui));
            m.rows.push_back(
                Info("Camera", std::string(replayCtl.cameraMode == CameraMode::Keyframes ? "keyframe path"
                                           : replayCtl.cameraMode == CameraMode::Follow  ? "recorded player"
                                                                                         : "free camera")));
            m.rows.push_back(Header("File"));
            m.rows.push_back(fieldRow("ex_name", "File name"));
            m.rows.push_back(fieldRow("ex_dir", "Folder"));
            m.rows.push_back(Info("Saves to", ExportDirectory(c)));
            m.rows.push_back(Info("Name tags", "{replay} {date} {time} {res} {fps}"));
            m.rows.push_back(Btn("ex_openfolder", "Open export folder"));
        } else {
            m.rows.push_back(Header("Quality"));
            m.rows.push_back(Cycle(
                "ex_crf", "Quality (CRF)",
                std::to_string(c.crf) + (c.bitrateKbps > 0 ? " (ignored)" : "") + "  lower = better", !seq));
            m.rows.push_back(fieldRow("ex_br", "Bitrate kbps (0=CRF)", !seq));
            m.rows.push_back(Header("Encoder options"));
            m.rows.push_back(fieldRow("ex_preset", "Encoder preset", !seq));
            m.rows.push_back(fieldRow("ex_pix", "Pixel format", !seq));
            m.rows.push_back(fieldRow("ex_enc", custom ? "Encoder (required)" : "Encoder override"));
            m.rows.push_back(fieldRow("ex_mux", "Muxer override"));
            m.rows.push_back(fieldRow("ex_ext", "File extension"));
            m.rows.push_back(fieldRow("ex_extra", "Extra ffmpeg args"));
            m.rows.push_back(Header(u.cat.builtin ? "Built-in encoders" : "ffmpeg"));
            if (u.cat.available) {
                m.rows.push_back(Info("Found", u.cat.path));
                m.rows.push_back(Info("Encoders / muxers", std::to_string(u.cat.encoders.size()) + " / " +
                                                               std::to_string(u.cat.muxers.size())));
            } else {
                m.rows.push_back(Info("ffmpeg was not found"));
                m.rows.push_back(
                    Info("Put ffmpeg(.exe) in",
                         ExportDirectory(c).substr(0, ExportDirectory(c).rfind('/')).substr(0, 60)));
            }
            const bool dl = u.downloader && u.downloader->Running();
            if (ffmpeg::Downloader::Supported() && !u.cat.builtin)
                m.rows.push_back(Btn(
                    "ex_dl",
                    dl ? "Downloading ffmpeg " + Num(std::max(0.0f, u.downloader->Progress()) * 100.0f) + "%"
                       : (u.cat.available ? "Re-download ffmpeg" : "Download ffmpeg (about 150 MB)"),
                    !dl));
            if (!u.cat.builtin)
                m.rows.push_back(Btn("ex_recheck", "Check for ffmpeg again"));
            if (!u.queue.empty()) {
                m.rows.push_back(Header("Queue"));
                m.rows.push_back(Info("Waiting jobs", std::to_string(u.queue.size())));
                m.rows.push_back(Btn("ex_clearq", "Clear queue"));
            }
        }
        const bool havePlayback = replayCtl.Playing();
        m.buttons = {{"ex_export", "Export", havePlayback},
                     {"ex_queue", "Queue", havePlayback},
                     {"ex_shot", "Screenshot", havePlayback},
                     {"ex_close", "Close", true}};
        const ui::PanelLayout layout = ui::ComputePanelLayout(m, guiW, guiH);
        u.scroll = std::clamp(u.scroll, 0, std::max(0, static_cast<int>(m.rows.size()) - layout.visibleRows));
        m.scroll = u.scroll;
        const std::string rml = ui::BuildPanelRml(m, guiW, guiH);
        if (rml != u.signature || !h.Overlay().HasOverlay()) {
            u.signature = rml;
            std::string error;
            if (!h.Overlay().SetOverlay(rml, error))
                Log("ui", "export panel: {}", error);
        }
        const float mx = input.MouseX() / static_cast<float>(scale),
                    my = input.MouseY() / static_cast<float>(scale);
        h.Overlay().MouseMove(mx, my);
        h.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (!down && u.leftDown && now - u.openedAt > std::chrono::milliseconds(150)) {
            const std::string hover = h.Overlay().OverlayHover();
            std::string base;
            int dir = 0;
            const bool shift = input.IsKeyDown(Key::Shift);
            bool refocus = false;
            if (hover.starts_with(ui::kPanelTabPrefix)) {
                u.tab = std::clamp(std::atoi(hover.c_str() + 6), 0, 2);
                u.scroll = 0;
            } else if (hover.starts_with("ex_") &&
                       (hover == "ex_w" || hover == "ex_h" || hover == "ex_fps" || hover == "ex_start" ||
                        hover == "ex_end" || hover == "ex_name" || hover == "ex_dir" || hover == "ex_br" ||
                        hover == "ex_preset" || hover == "ex_pix" || hover == "ex_enc" || hover == "ex_mux" ||
                        hover == "ex_ext" || hover == "ex_extra")) {
                u.focus = hover;
                u.text[hover] = fieldValue(hover);
                refocus = true;
            } else if (SplitCycle(hover, base, dir)) {
                if (base == "ex_container") {
                    const auto& all = ffmpeg::Containers();
                    int idx = 0;
                    for (size_t i = 0; i < all.size(); i++)
                        if (all[i].id == c.container)
                            idx = static_cast<int>(i);
                    c.container = all[static_cast<size_t>(Wrap(idx, dir, all))].id;
                } else if (base == "ex_codec" && cont && !cont->codecs.empty()) {
                    int idx = 0;
                    for (size_t i = 0; i < cont->codecs.size(); i++)
                        if (cont->codecs[i] == c.codec)
                            idx = static_cast<int>(i);
                    c.codec = cont->codecs[static_cast<size_t>(Wrap(idx, dir, cont->codecs))];
                } else if (base == "ex_res") {
                    const int n = Wrap(presetIdx, dir, ResolutionPresets());
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
                            static_cast<int>(std::lround(static_cast<double>(c.height) * na.w / na.h)), 16,
                            16384);
                } else if (base == "ex_fpsp") {
                    int idx = fpsIdx;
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
            } else if (hover == "ex_gui") {
                c.hideGui = !c.hideGui;
            } else if (hover == "ex_openfolder") {
                std::error_code ec;
                std::string p = ExportDirectory(c);
                fs::create_directories(p, ec);
                std::replace(p.begin(), p.end(), '\\', '/');
                platform->OpenUrl("file:///" + (!p.empty() && p[0] == '/' ? p.substr(1) : p));
            } else if (hover == "ex_dl") {
                if (!u.downloader)
                    u.downloader = std::make_unique<ffmpeg::Downloader>();
                u.downloadHandled = false;
                u.downloader->Start();
                u.status = "Downloading ffmpeg...";
            } else if (hover == "ex_recheck") {
                u.cat = ffmpeg::LibavCompiledIn() ? ffmpeg::QueryLibav()
                                                  : ffmpeg::Query(ffmpeg::FindExecutable());
                u.status = u.cat.available ? "ffmpeg found" : "ffmpeg still not found";
            } else if (hover == "ex_clearq") {
                u.queue.clear();
            } else if (hover == "ex_close") {
                closeMenu();
                return true;
            } else if (hover == "ex_export" || hover == "ex_shot") {
                if (havePlayback) {
                    SaveExportConfig();
                    ExportRequest req;
                    req.cfg = c;
                    req.screenshot = hover == "ex_shot";
                    if (ExportStart(req))
                        return true;
                }
            } else if (hover == "ex_queue") {
                if (havePlayback) {
                    ExportRequest req;
                    req.cfg = c;
                    u.queue.push_back(req);
                    u.status = "Added to queue (" + std::to_string(u.queue.size()) + ")";
                }
            }
            if (!refocus && !hover.starts_with("ex_") && !hover.starts_with(ui::kPanelTabPrefix))
                u.focus.clear();
            else if (!refocus)
                u.focus.clear();
            u.signature.clear();
        }
        u.leftDown = down;
        return true;
    }

    bool Perlin::UpdateKeyframePanel(ui::Hud& h, int fbW, int fbH) {
        using namespace replay;
        KeyframeUi& k = kfUi;
        const auto now = Clock::now();
        const auto [scale, guiW, guiH] = detail::OverlayViewport(h, fbW, fbH);
        const auto closePanel = [&] {
            k.open = false;
            h.Overlay().ClearOverlay();
            replayUi.signature.clear();
            replayUi.openedAt = now;
            replayUi.leftDown = true;
        };
        for (const auto& t : inputManager.TakeTyped())
            if (!t.character && t.key == Key::Escape && now - k.openedAt > std::chrono::milliseconds(120)) {
                closePanel();
                return true;
            }
        {
            const float wheel = inputManager.Scroll();
            if (wheel != 0.0f)
                k.scroll = std::max(0, k.scroll - static_cast<int>(std::lround(wheel)));
        }
        static const char* const kModeName[] = {"Free camera", "Follow player", "Keyframe path"};
        static const char* const kTabs[] = {"Camera", "Speed", "Time of day"};
        const double t = replayCtl.TimeMs();
        ui::PanelModel m;
        m.title = "Keyframes  " + ui::FormatReplayTime(t);
        m.tabs = {kTabs[0], kTabs[1], kTabs[2]};
        m.tab = k.tab;
        m.rows.push_back(Cycle("kf_mode", "Camera mode", kModeName[static_cast<int>(replayCtl.cameraMode)]));
        m.rows.push_back(Cycle("kf_interp", "Interpolation", InterpName(static_cast<Interp>(k.interp))));
        int& sel = k.sel[k.tab];
        const auto listRow = [&](const std::string& id, bool selected, const std::string& text) {
            PanelRow r = Btn(id, (selected ? "> " : "  ") + text);
            return r;
        };
        if (k.tab == 0) {
            m.rows.push_back(
                Cycle("kf_fov", "Field of view", Num(k.fov) + (k.fov == 70 ? " (default)" : "")));
            m.rows.push_back(Cycle("kf_roll", "Roll", Num(k.roll) + " deg"));
            m.rows.push_back(Btn("kf_add", "Add keyframe here (K)"));
            m.rows.push_back(Btn("kf_upd", "Update selected from current view",
                                 sel >= 0 && sel < static_cast<int>(timeline.camera.size())));
            m.rows.push_back(
                Btn("kf_del", "Delete selected", sel >= 0 && sel < static_cast<int>(timeline.camera.size())));
            m.rows.push_back(Header("Camera keyframes (" + std::to_string(timeline.camera.size()) + ")"));
            for (size_t i = 0; i < timeline.camera.size(); i++) {
                const CamKey& ck = timeline.camera[i];
                m.rows.push_back(listRow("kf_k" + std::to_string(i), static_cast<int>(i) == sel,
                                         ui::FormatReplayTime(ck.tMs) + "  " + Num(ck.x) + " " + Num(ck.y) +
                                             " " + Num(ck.z) + "  fov " + Num(ck.fov) + "  " +
                                             InterpName(ck.interp)));
            }
        } else if (k.tab == 1) {
            m.rows.push_back(Cycle("kf_speed", "Speed", Trim(Num(k.speed, 2), true) + "x"));
            m.rows.push_back(Btn("kf_add", "Add speed keyframe here"));
            m.rows.push_back(
                Btn("kf_del", "Delete selected", sel >= 0 && sel < static_cast<int>(timeline.speed.size())));
            m.rows.push_back(Info("Use a high speed for timelapses, low for slow motion"));
            m.rows.push_back(Header("Speed keyframes (" + std::to_string(timeline.speed.size()) + ")"));
            for (size_t i = 0; i < timeline.speed.size(); i++) {
                const ValueKey& vk = timeline.speed[i];
                m.rows.push_back(listRow("kf_k" + std::to_string(i), static_cast<int>(i) == sel,
                                         ui::FormatReplayTime(vk.tMs) + "  " + Trim(Num(vk.value, 2), true) +
                                             "x  " + InterpName(vk.interp)));
            }
        } else {
            m.rows.push_back(Cycle("kf_tod", "Time of day", Num(k.timeTicks) + " ticks"));
            m.rows.push_back(Toggle("kf_freeze", "Freeze time (hold first key)", timeline.freezeTimeOfDay));
            m.rows.push_back(Btn("kf_add", "Add time keyframe here"));
            m.rows.push_back(Btn("kf_del", "Delete selected",
                                 sel >= 0 && sel < static_cast<int>(timeline.timeOfDay.size())));
            m.rows.push_back(Header("Time keyframes (" + std::to_string(timeline.timeOfDay.size()) + ")"));
            for (size_t i = 0; i < timeline.timeOfDay.size(); i++) {
                const ValueKey& vk = timeline.timeOfDay[i];
                m.rows.push_back(listRow("kf_k" + std::to_string(i), static_cast<int>(i) == sel,
                                         ui::FormatReplayTime(vk.tMs) + "  " + Num(vk.value) + " ticks  " +
                                             InterpName(vk.interp)));
            }
        }
        m.buttons = {{"kf_clear", "Clear all", !timeline.Empty()}, {"kf_close", "Close", true}};
        const ui::PanelLayout layout = ui::ComputePanelLayout(m, guiW, guiH);
        k.scroll = std::clamp(k.scroll, 0, std::max(0, static_cast<int>(m.rows.size()) - layout.visibleRows));
        m.scroll = k.scroll;
        const std::string rml = ui::BuildPanelRml(m, guiW, guiH);
        if (rml != k.signature || !h.Overlay().HasOverlay()) {
            k.signature = rml;
            std::string error;
            if (!h.Overlay().SetOverlay(rml, error))
                Log("ui", "keyframe panel: {}", error);
        }
        const float mx = input.MouseX() / static_cast<float>(scale),
                    my = input.MouseY() / static_cast<float>(scale);
        h.Overlay().MouseMove(mx, my);
        h.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (!down && k.leftDown && now - k.openedAt > std::chrono::milliseconds(150)) {
            const std::string hover = h.Overlay().OverlayHover();
            std::string base;
            int dir = 0;
            const bool shift = input.IsKeyDown(Key::Shift);
            const auto currentCamKey = [&](double at) {
                CamKey ck;
                ck.tMs = at;
                ck.x = camera.x;
                ck.y = camera.y;
                ck.z = camera.z;
                ck.yaw = camera.yaw;
                ck.pitch = camera.pitch;
                ck.roll = k.roll;
                ck.fov = k.fov;
                ck.interp = static_cast<Interp>(k.interp);
                return ck;
            };
            if (hover.starts_with(ui::kPanelTabPrefix)) {
                k.tab = std::clamp(std::atoi(hover.c_str() + 6), 0, 2);
                k.scroll = 0;
            } else if (SplitCycle(hover, base, dir)) {
                if (base == "kf_mode") {
                    replayCtl.cameraMode =
                        static_cast<CameraMode>((static_cast<int>(replayCtl.cameraMode) + dir + 3) % 3);
                } else if (base == "kf_interp") {
                    k.interp = (k.interp + dir + 6) % 6;
                } else if (base == "kf_fov") {
                    k.fov = std::clamp(k.fov + static_cast<float>(dir) * (shift ? 1.0f : 5.0f), 5.0f, 170.0f);
                } else if (base == "kf_roll") {
                    k.roll =
                        std::clamp(k.roll + static_cast<float>(dir) * (shift ? 1.0f : 5.0f), -180.0f, 180.0f);
                } else if (base == "kf_speed") {
                    static const float ladder[] = {0.05f, 0.1f, 0.25f, 0.5f,  0.75f, 1.0f,  1.5f,
                                                   2.0f,  4.0f, 8.0f,  16.0f, 32.0f, 64.0f, 100.0f};
                    int best = 0;
                    for (int i = 1; i < 14; i++)
                        if (std::fabs(ladder[i] - k.speed) < std::fabs(ladder[best] - k.speed))
                            best = i;
                    k.speed = ladder[std::clamp(best + dir, 0, 13)];
                } else if (base == "kf_tod") {
                    k.timeTicks = std::fmod(
                        k.timeTicks + static_cast<float>(dir) * (shift ? 100.0f : 1000.0f) + 24000.0f,
                        24000.0f);
                }
            } else if (hover == "kf_add") {
                if (k.tab == 0) {
                    timeline.SetCamera(currentCamKey(t));
                } else if (k.tab == 1) {
                    timeline.SetSpeed({t, k.speed, static_cast<Interp>(k.interp == 0 ? 1 : k.interp)});
                } else {
                    timeline.SetTimeOfDay(
                        {t, k.timeTicks, static_cast<Interp>(k.interp == 0 ? 1 : k.interp)});
                }
                timelineDirty = true;
                replayUi.signature.clear();
                if (k.tab == 0 && timeline.camera.size() >= 2 &&
                    replayCtl.cameraMode != CameraMode::Keyframes) {
                    replayCtl.cameraMode = CameraMode::Keyframes;
                    ReplayStatus("Camera mode: keyframe path");
                }
            } else if (hover == "kf_upd") {
                if (k.tab == 0 && sel >= 0 && sel < static_cast<int>(timeline.camera.size())) {
                    CamKey nk = currentCamKey(timeline.camera[static_cast<size_t>(sel)].tMs);
                    timeline.camera[static_cast<size_t>(sel)] = nk;
                    timelineDirty = true;
                }
            } else if (hover == "kf_del") {
                if (sel >= 0) {
                    auto erase = [&](auto& v) {
                        if (sel < static_cast<int>(v.size()))
                            v.erase(v.begin() + sel);
                    };
                    if (k.tab == 0)
                        erase(timeline.camera);
                    else if (k.tab == 1)
                        erase(timeline.speed);
                    else
                        erase(timeline.timeOfDay);
                    sel = -1;
                    timelineDirty = true;
                }
            } else if (hover == "kf_freeze") {
                timeline.freezeTimeOfDay = !timeline.freezeTimeOfDay;
                timelineDirty = true;
            } else if (hover == "kf_clear") {
                timeline.Clear();
                k.sel[0] = k.sel[1] = k.sel[2] = -1;
                timelineDirty = true;
            } else if (hover == "kf_close") {
                closePanel();
                return true;
            } else if (hover.starts_with("kf_k")) {
                const int idx = std::atoi(hover.c_str() + 4);
                sel = idx;
                double at = 0;
                if (k.tab == 0 && idx < static_cast<int>(timeline.camera.size())) {
                    const CamKey& ck = timeline.camera[static_cast<size_t>(idx)];
                    at = ck.tMs;
                    k.fov = ck.fov;
                    k.roll = ck.roll;
                    k.interp = static_cast<int>(ck.interp);
                } else if (k.tab == 1 && idx < static_cast<int>(timeline.speed.size())) {
                    at = timeline.speed[static_cast<size_t>(idx)].tMs;
                    k.speed = timeline.speed[static_cast<size_t>(idx)].value;
                } else if (k.tab == 2 && idx < static_cast<int>(timeline.timeOfDay.size())) {
                    at = timeline.timeOfDay[static_cast<size_t>(idx)].tMs;
                    k.timeTicks = timeline.timeOfDay[static_cast<size_t>(idx)].value;
                } else {
                    at = t;
                }
                replayCtl.Seek(at);
                if (k.tab == 0 && idx < static_cast<int>(timeline.camera.size()) &&
                    replayCtl.cameraMode != CameraMode::Keyframes) {
                    const CamKey& ck = timeline.camera[static_cast<size_t>(idx)];
                    camera.x = static_cast<float>(ck.x);
                    camera.y = static_cast<float>(ck.y);
                    camera.z = static_cast<float>(ck.z);
                    camera.yaw = ck.yaw;
                    camera.pitch = ck.pitch;
                }
            }
            k.signature.clear();
        }
        k.leftDown = down;
        return true;
    }
}
