#include "client/ui/GuiViewport.hpp"
#include "client/features/replay/Studio.hpp"

#include "client/systems/bindings/Keys.hpp"
#include "client/systems/replay/ExportForm.hpp"
#include "client/systems/replay/LibavEncoder.hpp"
#include "client/ui/ReplayControls.hpp"
#include "client/ui/TextEdit.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include "util/Log.hpp"
#include "util/Strings.hpp"

namespace perlin::replay {
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

    }

    void Studio::ExportOpenMenu() {
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

    namespace {
        constexpr auto kClickGrace = std::chrono::milliseconds(150);
        constexpr auto kEscapeGrace = std::chrono::milliseconds(120);
        constexpr auto kCaretBlink = std::chrono::milliseconds(500);
        constexpr size_t kStatusChars = 52;
        constexpr int kTabCount = 3;

        struct ExportPanelView {
            const replay::ExportConfig& cfg;
            const replay::ExportDerived& derived;
            const replay::ffmpeg::Catalog& catalog;
            const std::string& focus;
            std::map<std::string, std::string>& text;
            const replay::ffmpeg::Downloader* downloader;
            size_t queued;
            replay::CameraMode camera;
            long long frames;
        };

        PanelRow FieldRow(const ExportPanelView& v, const std::string& id, const std::string& label,
                          bool enabled = true) {
            const bool focused = v.focus == id;
            return Field(id, label, focused ? v.text[id] : replay::ExportFieldText(v.cfg, id), focused,
                         enabled);
        }

        std::string ContainerLabel(const ExportPanelView& v, const replay::ffmpeg::Container& k) {
            std::string l = k.label;
            if (v.catalog.available && !k.imageSequence && !k.muxer.empty() && !v.catalog.HasMuxer(k.muxer))
                l += " (unavailable)";
            return l;
        }

        std::string CodecLabel(const ExportPanelView& v) {
            const replay::ffmpeg::Codec* codec = v.derived.codec;
            std::string label = codec ? codec->label : v.cfg.codec;
            if (codec && v.catalog.available && !v.catalog.HasEncoder(codec->encoder)) {
                const bool alternative = std::ranges::any_of(
                    codec->fallbackEncoders, [&](const std::string& f) { return v.catalog.HasEncoder(f); });
                if (!alternative)
                    label += " (missing)";
            }
            return label;
        }

        void AddVideoTab(ui::PanelModel& m, const ExportPanelView& v) {
            const replay::ExportConfig& c = v.cfg;
            const replay::ExportDerived& d = v.derived;
            m.rows.push_back(v.catalog.available
                                 ? Info(v.catalog.builtin ? "Encoder (built in)" : "ffmpeg",
                                        v.catalog.version.substr(0, 40))
                                 : Info("ffmpeg not found", "using built-in AVI (Advanced: get ffmpeg)"));
            m.rows.push_back(Header("Format"));
            m.rows.push_back(Cycle("ex_container", "Container", ContainerLabel(v, *d.container)));
            if (!d.imageSequence && !d.custom)
                m.rows.push_back(Cycle("ex_codec", "Codec", CodecLabel(v)));
            else if (d.custom)
                m.rows.push_back(Info("Set encoder and muxer on the Advanced tab"));
            m.rows.push_back(Header("Picture"));
            m.rows.push_back(
                Cycle("ex_res", "Resolution",
                      replay::ResolutionPresets()[static_cast<size_t>(d.resolutionPreset)].label));
            m.rows.push_back(Cycle("ex_aspect", "Aspect ratio", d.aspect->label));
            m.rows.push_back(FieldRow(v, "ex_w", "Width"));
            m.rows.push_back(FieldRow(v, "ex_h", "Height"));
            m.rows.push_back(Cycle("ex_fpsp", "Frame rate",
                                   d.fpsPreset >= 0 ? replay::FpsLabel(c.fps) + " fps" : "Custom"));
            m.rows.push_back(FieldRow(v, "ex_fps", "FPS (any value)"));
            m.rows.push_back(
                Cycle("ex_ssaa", "Supersampling", c.ssaa == 1 ? "Off" : std::to_string(c.ssaa) + "x"));
            m.rows.push_back(Info("Rendered at", std::to_string(c.width * c.ssaa) + "x" +
                                                     std::to_string(c.height * c.ssaa)));
        }

        void AddOutputTab(ui::PanelModel& m, const ExportPanelView& v) {
            using replay::CameraMode;
            const replay::ExportConfig& c = v.cfg;
            static const char* const kRange[] = {"Whole replay", "Between in / out points",
                                                 "Custom (seconds)"};
            m.rows.push_back(Header("Range"));
            m.rows.push_back(Cycle("ex_range", "Range", kRange[static_cast<int>(c.range)]));
            if (c.range == replay::RangeMode::Custom) {
                m.rows.push_back(FieldRow(v, "ex_start", "Start (s)"));
                m.rows.push_back(FieldRow(v, "ex_end", "End (s)"));
            }
            m.rows.push_back(Info("Replay span", ui::FormatReplayTime(v.derived.rangeStartMs) + " - " +
                                                     ui::FormatReplayTime(v.derived.rangeEndMs)));
            m.rows.push_back(Info("Output", std::to_string(v.frames) + " frames, " +
                                                Num(static_cast<double>(v.frames) / c.fps, 1) + " s"));
            m.rows.push_back(Header("Rendering"));
            m.rows.push_back(Toggle("ex_gui", "Hide GUI", c.hideGui));
            m.rows.push_back(Info("Camera", std::string(v.camera == CameraMode::Keyframes ? "keyframe path"
                                                        : v.camera == CameraMode::Follow  ? "recorded player"
                                                                                          : "free camera")));
            m.rows.push_back(Header("File"));
            m.rows.push_back(FieldRow(v, "ex_name", "File name"));
            m.rows.push_back(FieldRow(v, "ex_dir", "Folder"));
            m.rows.push_back(Info("Saves to", replay::ExportDirectory(c)));
            m.rows.push_back(Info("Name tags", "{replay} {date} {time} {res} {fps}"));
            m.rows.push_back(Btn("ex_openfolder", "Open export folder"));
        }

        void AddAdvancedTab(ui::PanelModel& m, const ExportPanelView& v) {
            const replay::ExportConfig& c = v.cfg;
            const replay::ExportDerived& d = v.derived;
            const replay::ffmpeg::Catalog& cat = v.catalog;
            const bool seq = d.imageSequence;
            m.rows.push_back(Header("Quality"));
            m.rows.push_back(Cycle(
                "ex_crf", "Quality (CRF)",
                std::to_string(c.crf) + (c.bitrateKbps > 0 ? " (ignored)" : "") + "  lower = better", !seq));
            m.rows.push_back(FieldRow(v, "ex_br", "Bitrate kbps (0=CRF)", !seq));
            m.rows.push_back(Header("Encoder options"));
            m.rows.push_back(FieldRow(v, "ex_preset", "Encoder preset", !seq));
            m.rows.push_back(FieldRow(v, "ex_pix", "Pixel format", !seq));
            m.rows.push_back(FieldRow(v, "ex_enc", d.custom ? "Encoder (required)" : "Encoder override"));
            m.rows.push_back(FieldRow(v, "ex_mux", "Muxer override"));
            m.rows.push_back(FieldRow(v, "ex_ext", "File extension"));
            m.rows.push_back(FieldRow(v, "ex_extra", "Extra ffmpeg args"));
            m.rows.push_back(Header(cat.builtin ? "Built-in encoders" : "ffmpeg"));
            if (cat.available) {
                m.rows.push_back(Info("Found", cat.path));
                m.rows.push_back(Info("Encoders / muxers", std::to_string(cat.encoders.size()) + " / " +
                                                               std::to_string(cat.muxers.size())));
            } else {
                const std::string dir = replay::ExportDirectory(c);
                m.rows.push_back(Info("ffmpeg was not found"));
                m.rows.push_back(Info("Put ffmpeg(.exe) in", dir.substr(0, dir.rfind('/')).substr(0, 60)));
            }
            const bool downloading = v.downloader && v.downloader->Running();
            if (replay::ffmpeg::Downloader::Supported() && !cat.builtin)
                m.rows.push_back(Btn(
                    "ex_dl",
                    downloading
                        ? "Downloading ffmpeg " + Num(std::max(0.0f, v.downloader->Progress()) * 100.0f) + "%"
                        : (cat.available ? "Re-download ffmpeg" : "Download ffmpeg (about 150 MB)"),
                    !downloading));
            if (!cat.builtin)
                m.rows.push_back(Btn("ex_recheck", "Check for ffmpeg again"));
            if (v.queued > 0) {
                m.rows.push_back(Header("Queue"));
                m.rows.push_back(Info("Waiting jobs", std::to_string(v.queued)));
                m.rows.push_back(Btn("ex_clearq", "Clear queue"));
            }
        }

        replay::ffmpeg::Catalog QueryEncoders() {
            return replay::ffmpeg::LibavCompiledIn()
                       ? replay::ffmpeg::QueryLibav()
                       : replay::ffmpeg::Query(replay::ffmpeg::FindExecutable());
        }
    }

    void Studio::ExportCloseMenu(ui::Hud& h, TimePoint now) {
        ExportUi& u = exportUi;
        u.open = false;
        u.focus.clear();
        SaveExportConfig();
        h.Overlay().ClearOverlay();
        replayUi.signature.clear();
        replayUi.openedAt = now;
        replayUi.leftDown = true;
    }

    void Studio::ExportPollDownload() {
        ExportUi& u = exportUi;
        if (!u.downloader || !u.downloader->Finished() || u.downloadHandled)
            return;
        u.downloadHandled = true;
        u.downloader->Join();
        if (!u.downloader->Succeeded()) {
            u.status = u.downloader->Status();
            return;
        }
        u.cat = QueryEncoders();
        u.status = u.cat.available ? "ffmpeg is ready" : "ffmpeg was installed but could not be started";
    }

    bool Studio::ExportTyped(ui::Hud& h, TimePoint now) {
        ExportUi& u = exportUi;
        for (const auto& t : inputManager.TakeTyped()) {
            if (!t.character && t.key == Key::Escape) {
                if (!u.focus.empty()) {
                    u.focus.clear();
                } else if (now - u.openedAt > kEscapeGrace) {
                    ExportCloseMenu(h, now);
                    return true;
                }
            } else if (!u.focus.empty()) {
                std::string& buf = u.text[u.focus];
                if (!t.character && (t.key == Key::Enter || t.key == Key::Tab))
                    u.focus.clear();
                else
                    ui::TextEdit::Focused().Apply(t, buf);
                if (!u.focus.empty())
                    replay::CommitExportField(u.cfg, u.focus, buf);
            }
        }
        return false;
    }

    void Studio::ExportOpenFolder() {
        std::error_code ec;
        std::string p = replay::ExportDirectory(exportUi.cfg);
        fs::create_directories(p, ec);
        std::ranges::replace(p, '\\', '/');
        host.OpenUrl("file:///" + (!p.empty() && p[0] == '/' ? p.substr(1) : p));
    }

    bool Studio::ExportClick(ui::Hud& h, const std::string& hover, const replay::ExportDerived& d,
                             TimePoint now) {
        ExportUi& u = exportUi;
        const bool havePlayback = replayCtl.Playing();
        bool refocus = false;
        std::string base;
        int dir = 0;
        if (hover.starts_with(ui::kPanelTabPrefix)) {
            u.tab = std::clamp(std::atoi(hover.c_str() + 6), 0, kTabCount - 1);
            u.scroll = 0;
        } else if (replay::IsExportTextField(hover)) {
            u.focus = hover;
            u.text[hover] = replay::ExportFieldText(u.cfg, hover);
            refocus = true;
        } else if (replay::SplitCycleId(hover, base, dir)) {
            replay::StepExportOption(u.cfg, base, dir, input.IsKeyDown(Key::Shift), d);
        } else if (hover == "ex_gui") {
            u.cfg.hideGui = !u.cfg.hideGui;
        } else if (hover == "ex_openfolder") {
            ExportOpenFolder();
        } else if (hover == "ex_dl") {
            if (!u.downloader)
                u.downloader = std::make_unique<replay::ffmpeg::Downloader>();
            u.downloadHandled = false;
            u.downloader->Start();
            u.status = "Downloading ffmpeg...";
        } else if (hover == "ex_recheck") {
            u.cat = QueryEncoders();
            u.status = u.cat.available ? "ffmpeg found" : "ffmpeg still not found";
        } else if (hover == "ex_clearq") {
            u.queue.clear();
        } else if (hover == "ex_close") {
            ExportCloseMenu(h, now);
            return true;
        } else if ((hover == "ex_export" || hover == "ex_shot") && havePlayback) {
            SaveExportConfig();
            ExportRequest req;
            req.cfg = u.cfg;
            req.screenshot = hover == "ex_shot";
            if (ExportStart(req))
                return true;
        } else if (hover == "ex_queue" && havePlayback) {
            ExportRequest req;
            req.cfg = u.cfg;
            u.queue.push_back(req);
            u.status = "Added to queue (" + std::to_string(u.queue.size()) + ")";
        }
        if (!refocus)
            u.focus.clear();
        u.signature.clear();
        return false;
    }

    bool Studio::UpdateExportMenu(ui::Hud& h, int fbW, int fbH) {
        ExportUi& u = exportUi;
        replay::ExportConfig& c = u.cfg;
        const auto now = Clock::now();
        const auto [scale, guiW, guiH] = ui::OverlayViewport(h, fbW, fbH);

        ExportPollDownload();
        if (now - u.caretAt > kCaretBlink) {
            u.caretAt = now;
            u.caret = !u.caret;
        }
        if (ExportTyped(h, now))
            return true;
        if (const float wheel = inputManager.Scroll(); wheel != 0.0f)
            u.scroll = std::max(0, u.scroll - static_cast<int>(std::lround(wheel)));

        const double dur = replayCtl.DurationMs();
        const replay::ExportDerived d = replay::DeriveExportForm(c, dur, replayUi.inMs, replayUi.outMs);
        const long long frames =
            d.rangeEndMs > d.rangeStartMs
                ? replay::CountExportFrames(timeline, d.rangeStartMs, d.rangeEndMs, c.fps)
                : 0;

        ui::PanelModel m;
        m.title = "Export Replay";
        m.status = u.status.size() > kStatusChars ? u.status.substr(0, kStatusChars) + "..." : u.status;
        m.tabs = {"Video", "Output", "Advanced"};
        m.tab = u.tab;
        m.caret = u.caret;
        const ExportPanelView view{
            c, d, u.cat, u.focus, u.text, u.downloader.get(), u.queue.size(), replayCtl.cameraMode, frames};
        if (u.tab == 0)
            AddVideoTab(m, view);
        else if (u.tab == 1)
            AddOutputTab(m, view);
        else
            AddAdvancedTab(m, view);
        const bool havePlayback = replayCtl.Playing();
        m.buttons = {{"ex_export", "Export", havePlayback},
                     {"ex_queue", "Queue", havePlayback},
                     {"ex_shot", "Screenshot", havePlayback},
                     {"ex_close", "Close", true}};
        const ui::PanelLayout layout = ui::ComputePanelLayout(m, guiW, guiH);
        u.scroll = std::clamp(u.scroll, 0, std::max(0, static_cast<int>(m.rows.size()) - layout.visibleRows));
        m.scroll = u.scroll;
        const ui::SceneDoc doc = ui::BuildPanel(m, guiW, guiH);
        if (const std::string dump = ui::DumpScene(doc); dump != u.signature || !h.Overlay().HasOverlay()) {
            u.signature = dump;
            std::string error;
            if (!h.Overlay().SetOverlay(doc, error))
                Log("ui", "export panel: {}", error);
        }
        h.Overlay().MouseMove(input.MouseX() / static_cast<float>(scale),
                              input.MouseY() / static_cast<float>(scale));
        h.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (!down && u.leftDown && now - u.openedAt > kClickGrace &&
            ExportClick(h, h.Overlay().OverlayHover(), d, now))
            return true;
        u.leftDown = down;
        return true;
    }

    bool Studio::UpdateKeyframePanel(ui::Hud& h, int fbW, int fbH) {
        using namespace replay;
        KeyframeUi& k = kfUi;
        const auto now = Clock::now();
        const auto [scale, guiW, guiH] = ui::OverlayViewport(h, fbW, fbH);
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
        const ui::SceneDoc doc = ui::BuildPanel(m, guiW, guiH);
        if (const std::string dump = ui::DumpScene(doc); dump != k.signature || !h.Overlay().HasOverlay()) {
            k.signature = dump;
            std::string error;
            if (!h.Overlay().SetOverlay(doc, error))
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
            } else if (replay::SplitCycleId(hover, base, dir)) {
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
                    SetStatus("Camera mode: keyframe path");
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
