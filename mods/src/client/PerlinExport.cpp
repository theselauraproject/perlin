#include "client/PerlinInternal.hpp"
#include "client/Perlin.hpp"

#include "client/input/Keys.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/ui/Json.hpp"
#include "client/ui/ReplayScreen.hpp"
#include "client/replay/LibavEncoder.hpp"
#include "util/Paths.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include "util/Log.hpp"

namespace perlin {
    static constexpr bool kExportProgressWindow = false;

    namespace fs = std::filesystem;

    namespace {
        using Clock = std::chrono::steady_clock;

        std::string ConfigPath() {
            return replay::ReplayDirectory() + "/export_settings.json";
        }

        int Even(int v) {
            return std::max(2, v & ~1);
        }

        std::string Num(double v, int decimals = 0) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
            return buf;
        }
    }

    void Perlin::LoadTimeline() {
        timeline = replay::Timeline{};
        timelineDirty = false;
        const std::string text = replayCtl.ReadArchiveFile("editor.json");
        if (!text.empty() && !replay::Timeline::FromJson(text, timeline))
            Log("replay", "editor.json could not be read; starting with an empty timeline");
    }

    void Perlin::SaveTimeline() {
        if (!timelineDirty)
            return;
        std::string error;
        const std::string json = timeline.Empty() ? std::string() : timeline.ToJson();
        if (!replayCtl.SaveArchiveFile("editor.json", json, error))
            Log("replay", "could not save the timeline: {}", error);
        timelineDirty = false;
    }

    void Perlin::LoadExportConfig() {
        if (exportUi.cfgLoaded)
            return;
        exportUi.cfgLoaded = true;
        std::ifstream in(ConfigPath(), std::ios::binary);
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            exportUi.cfg = replay::ExportConfig::FromJson(ss.str());
        }
    }

    void Perlin::SaveExportConfig() {
        std::error_code ec;
        fs::create_directories(replay::ReplayDirectory(), ec);
        std::ofstream out(ConfigPath(), std::ios::binary | std::ios::trunc);
        if (out)
            out << exportUi.cfg.ToJson();
    }

    void Perlin::FramebufferSizeEffective(int& w, int& h) {
        if (exportJob.active) {
            w = exportJob.renderW;
            h = exportJob.renderH;
            return;
        }
        platform->GetFramebufferSize(w, h);
    }

    bool Perlin::ExportStart(const ExportRequest& req) {
        ExportJob& j = exportJob;
        if (j.active)
            return false;
        if (!replayCtl.Playing() || !session || !renderer) {
            exportUi.status = "Open a replay first";
            return false;
        }
        replay::ExportConfig cfg = req.cfg;
        if (!req.screenshot && !exportUi.cat.available && cfg.container != "png" &&
            cfg.container != "builtin" && replay::ffmpeg::FindContainer(cfg.container) &&
            !replay::ffmpeg::FindContainer(cfg.container)->imageSequence) {
            cfg.container = "builtin";
            Log("export", "ffmpeg not found, using the built-in AVI encoder");
        }
        const replay::ffmpeg::Container* cont = replay::ffmpeg::FindContainer(cfg.container);
        if (!cont) {
            exportUi.status = "Unknown format";
            return false;
        }
        const std::string replayName = replayCtl.Meta().name.empty() ? "replay" : replayCtl.Meta().name;
        j.cfg = cfg;
        j.screenshot = req.screenshot;
        j.error.clear();
        j.encoder.reset();
        j.useFfmpeg = false;
        j.imageSeq = false;
        const int ssaa = std::clamp(cfg.ssaa, 1, 4);
        int outW = std::clamp(cfg.width, 16, 16384), outH = std::clamp(cfg.height, 16, 16384);
        if (!req.screenshot && !cont->imageSequence) {
            outW = Even(outW);
            outH = Even(outH);
        }
        int renderW = outW * ssaa, renderH = outH * ssaa;
        while ((renderW > 16384 || renderH > 16384) && ssaa > 1)
            return ExportStart([&] {
                ExportRequest r = req;
                r.cfg.ssaa = ssaa - 1;
                return r;
            }());
        if (renderW > 16384 || renderH > 16384) {
            exportUi.status = "Resolution too large (max 16384)";
            return false;
        }
        j.outW = outW;
        j.outH = outH;
        j.renderW = renderW;
        j.renderH = renderH;
        j.name = replay::ExpandNameTemplate(cfg.nameTemplate, replayName, outW, outH, cfg.fps, cfg.container);
        const std::string dir = replay::ExportDirectory(cfg);
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec) {
            exportUi.status = "Cannot create " + dir;
            return false;
        }

        const double dur = replayCtl.DurationMs();
        j.startMs = 0;
        j.endMs = dur;
        if (cfg.range == replay::RangeMode::InOut) {
            if (replayUi.inMs >= 0)
                j.startMs = replayUi.inMs;
            if (replayUi.outMs >= 0)
                j.endMs = replayUi.outMs;
        } else if (cfg.range == replay::RangeMode::Custom) {
            j.startMs = cfg.customStartSec * 1000.0;
            j.endMs = cfg.customEndSec * 1000.0;
        }
        j.startMs = std::clamp(j.startMs, 0.0, dur);
        j.endMs = std::clamp(j.endMs, 0.0, dur);
        if (req.screenshot) {
            j.startMs = j.endMs = replayCtl.TimeMs();
            j.total = 1;
        } else {
            if (j.endMs <= j.startMs + 1) {
                exportUi.status = "The selected range is empty";
                return false;
            }
            j.total = replay::CountExportFrames(timeline, j.startMs, j.endMs, cfg.fps);
            if (j.total < 1) {
                exportUi.status = "Nothing to export";
                return false;
            }
        }

        if (req.screenshot) {
            j.outPath = replay::UniquePath(dir + "/" + j.name + ".png", false);
        } else {
            replay::ffmpeg::Settings& fs2 = j.fs;
            fs2 = replay::ffmpeg::Settings{};
            fs2.ffmpegPath = exportUi.cat.available ? exportUi.cat.path : std::string();
            fs2.container = cfg.container;
            fs2.codec = cfg.codec;
            fs2.encoderOverride = cfg.encoderOverride;
            fs2.muxerOverride = cfg.muxerOverride;
            fs2.width = outW;
            fs2.height = outH;
            fs2.inputWidth = renderW;
            fs2.inputHeight = renderH;
            fs2.fps = cfg.fps;
            fs2.crf = cfg.crf;
            fs2.bitrateKbps = cfg.bitrateKbps;
            fs2.preset = cfg.preset;
            fs2.pixelFormat = cfg.pixelFormat;
            fs2.extraArgs = cfg.extraArgs;
            fs2.gifColors = cfg.gifColors;
            const std::string ext = !cfg.extensionOverride.empty() ? cfg.extensionOverride : cont->ext;
            j.imageSeq = cont->imageSequence;
            if (j.imageSeq) {
                const std::string seqDir = replay::UniquePath(dir + "/" + j.name, true);
                fs::create_directories(seqDir, ec);
                j.outPath = seqDir;
                fs2.outputPath = seqDir + "/" + j.name + "_%05d." + ext;
            } else {
                j.outPath = replay::UniquePath(dir + "/" + j.name + "." + ext, false);
                fs2.outputPath = j.outPath;
            }
            const bool builtin = cfg.container == "builtin";
            const bool pngFallback = j.imageSeq && cfg.container == "png" && !exportUi.cat.available;
            if (pngFallback) {
                j.useFfmpeg = false;
            } else {
                if (!exportUi.cat.available && !builtin) {
                    exportUi.status = "ffmpeg is needed for this format (Advanced tab), or pick PNG sequence";
                    return false;
                }
                j.encoder = std::make_unique<replay::ffmpeg::Encoder>();
                std::string error;
                if (!j.encoder->Start(fs2, exportUi.cat, error)) {
                    j.encoder.reset();
                    exportUi.status = error;
                    return false;
                }
                j.useFfmpeg = true;
            }
        }

        j.savedHideHud = replayUi.hideHud;
        j.savedPaused = replayCtl.Paused();
        j.savedMenu = replayUi.menuOpen;
        j.savedSpeed = replayCtl.Speed();
        j.savedTimeMs = replayCtl.TimeMs();
        j.savedMode = replayCtl.cameraMode;
        replayUi.hideHud = cfg.hideGui;
        replayUi.menuOpen = false;
        replayUi.scrubbing = false;
        exportUi.open = false;
        kfUi.open = false;
        pause.shown = false;
        if (hud)
            hud->Overlay().ClearOverlay();
        input.SetTextMode(false);
        replayCtl.SetPaused(false);
        replayCtl.SetSpeed(1.0);
        renderer->SetForcedSize(renderW, renderH);
        j.offscreen = renderer->SetExportTarget(true);
        j.stepMs = 1000.0 / cfg.fps;
        j.targetMs = j.startMs;
        j.frame = 0;
        j.virtualUs = 0;
        j.virtualBase = Clock::now();
        j.realStart = Clock::now();
        j.phaseStart = j.realStart;
        j.lastOverlay = {};
        j.settleIters = j.quietIters = j.waitIters = 0;
        {
            std::lock_guard<std::mutex> lk(j.capMutex);
            j.ready.clear();
            j.stopWriter = false;
            j.sizeMismatch = false;
            j.writerError.clear();
        }
        j.written = 0;
        j.lastWritten = 0;
        j.lastTitle = {};
        j.lastProgress = Clock::now();
        j.overlayRml.clear();
        j.maxInFlight =
            std::clamp<long long>((static_cast<long long>(256) << 20) /
                                      std::max<long long>(1, static_cast<long long>(renderW) * renderH * 4),
                                  2, 8);
        j.advanceNow = false;
        j.finishing = false;
        j.phase = req.screenshot ? ExportJob::Phase::Settle : ExportJob::Phase::Seeking;
        if (!req.screenshot)
            replayCtl.Seek(j.startMs);
        j.active = true;
        j.writer = std::thread([this] { ExportWriterLoop(); });
        Log("export", "{} {}x{} (render {}x{}) {} fps, {} frames -> {}",
            (req.screenshot ? "screenshot" : "video"), outW, outH, renderW, renderH, cfg.fps, j.total,
            j.outPath);
        return true;
    }

    void Perlin::ExportFinish(bool cancelled, const std::string& error) {
        ExportJob& j = exportJob;
        if (!j.active)
            return;
        std::string message;
        std::string err = error;
        {
            std::lock_guard<std::mutex> lk(j.capMutex);
            if (cancelled || !err.empty())
                j.ready.clear();
            j.stopWriter = true;
        }
        j.capCv.notify_all();
        if (j.writer.joinable())
            j.writer.join();
        {
            std::lock_guard<std::mutex> lk(j.capMutex);
            if (err.empty() && !j.writerError.empty())
                err = j.writerError;
        }
        j.frame = j.written.load();
        if (j.encoder) {
            std::string finishError;
            if (cancelled || !err.empty()) {
                if (!j.encoder->Finish(finishError) && err.empty())
                    err = finishError;
            } else if (!j.encoder->Finish(finishError)) {
                err = finishError;
            }
            j.encoder.reset();
        }
        if (renderer)
            renderer->SetForcedSize(0, 0);
        renderer->SetExportTarget(false);
        j.offscreen = false;
        j.active = false;
        SetStatusHint("");
        platform->SetProgressWindow(false, "", "", "", 0.0f);
        replayUi.hideHud = j.savedHideHud;
        replayCtl.SetSpeed(j.savedSpeed);
        replayCtl.SetPaused(j.savedPaused);
        replayCtl.cameraMode = j.savedMode;
        replayUi.menuOpen = true;
        replayUi.openedAt = Clock::now();
        replayUi.signature.clear();
        if (hud)
            hud->Overlay().ClearOverlay();
        const double secs = std::chrono::duration<double>(Clock::now() - j.realStart).count();
        if (!err.empty()) {
            message = "Export failed: " + err;
        } else if (cancelled) {
            message = "Export cancelled (" + std::to_string(j.frame) + " frames kept)";
        } else {
            message = (j.screenshot ? "Saved screenshot: " : "Export finished: ") + j.outPath + " (" +
                      Num(secs, 1) + " s)";
        }
        Log("export", "{}", message);
        exportUi.status = message;
        ReplayStatus(message);
        replayUi.statusUntil = Clock::now() + std::chrono::seconds(12);
        if (!cancelled && err.empty() && !exportUi.queue.empty())
            ExportStartNext();
        else if (cancelled)
            exportUi.queue.clear();
    }

    void Perlin::ExportStartNext() {
        while (!exportUi.queue.empty()) {
            ExportRequest r = exportUi.queue.front();
            exportUi.queue.erase(exportUi.queue.begin());
            if (ExportStart(r))
                return;
            Log("export", "queued job skipped: {}", exportUi.status);
        }
    }

    void Perlin::ExportWriterLoop() {
        ExportJob& j = exportJob;
        for (;;) {
            ExportJob::CapturedFrame frame;
            {
                std::unique_lock<std::mutex> lk(j.capMutex);
                j.capCv.wait(lk, [&] { return j.stopWriter || !j.ready.empty(); });
                if (j.ready.empty())
                    return;
                frame = std::move(j.ready.front());
                j.ready.pop_front();
            }
            j.capCv.notify_all();
            bool ok = true;
            std::string err;
            const long long index = j.written.load() + 1;
            if (j.screenshot || (!j.useFfmpeg)) {
                std::vector<uint8_t> scaled;
                const uint8_t* data = frame.data.data();
                if (j.renderW != j.outW) {
                    scaled = replay::ffmpeg::DownscaleBgra(frame.data.data(), j.renderW, j.renderH,
                                                           static_cast<size_t>(j.renderW) * 4, false, j.outW,
                                                           j.outH);
                    data = scaled.data();
                }
                std::string target = j.outPath;
                if (!j.screenshot) {
                    char name[64];
                    std::snprintf(name, sizeof name, "_%05lld.png", index);
                    target = j.outPath + "/" + j.name + name;
                }
                ok = replay::ffmpeg::WritePngBgra(target, j.outW, j.outH, data,
                                                  static_cast<size_t>(j.outW) * 4, false, false);
                if (!ok)
                    err = "could not write " + target;
            } else {
                ok = j.encoder->WriteFrame(frame.data.data(), frame.data.size());
                if (!ok)
                    err = "the encoder stopped: " + j.encoder->Log().substr(0, 400);
            }
            if (!ok) {
                std::lock_guard<std::mutex> lk(j.capMutex);
                j.writerError = err;
                j.ready.clear();
                return;
            }
            j.written++;
        }
    }

    void Perlin::ExportPreFrame(Clock::time_point& frameStart, float& dt) {
        ExportJob& j = exportJob;
        const auto realNow = Clock::now();
        if (!platform->isRunning) {
            ExportFinish(true, "");
            return;
        }
        if (inputManager.Pressed(Action::Pause) || platform->TakeProgressCancel()) {
            ExportFinish(true, "");
            return;
        }
        if (!replayCtl.Playing() || !session || !session->IsReplay()) {
            ExportFinish(true, "the replay was closed");
            return;
        }
        {
            std::string werr;
            {
                std::lock_guard<std::mutex> lk(j.capMutex);
                werr = j.writerError;
            }
            if (!werr.empty()) {
                ExportFinish(false, werr);
                return;
            }
        }
        if (realNow - j.lastTitle > std::chrono::milliseconds(250)) {
            j.lastTitle = realNow;
            const long long done = j.written.load();
            const double elapsed = std::chrono::duration<double>(realNow - j.realStart).count();
            std::string t =
                "Exporting " +
                Num(100.0 * static_cast<double>(done) / static_cast<double>(std::max<long long>(1, j.total)),
                    1) +
                "% (" + std::to_string(done) + "/" + std::to_string(j.total) + ")";
            if (done > 2 && elapsed > 0.5) {
                t += ", " + Num(static_cast<double>(done) / elapsed, 1) + " fps, " +
                     ui::FormatReplayTime(elapsed / static_cast<double>(done) *
                                          static_cast<double>(j.total - done) * 1000.0) +
                     " left";
            }
            SetStatusHint(t);
            if (kExportProgressWindow && platform->HasProgressWindow()) {
                std::string l2 = Num(100.0 * static_cast<double>(done) /
                                         static_cast<double>(std::max<long long>(1, j.total)),
                                     1) +
                                 "%";
                if (done > 2 && elapsed > 0.5)
                    l2 += "   " + Num(static_cast<double>(done) / elapsed, 1) + " frames/s   " +
                          ui::FormatReplayTime(elapsed / static_cast<double>(done) *
                                               static_cast<double>(j.total - done) * 1000.0) +
                          " left";
                platform->SetProgressWindow(
                    true, "Exporting " + j.name,
                    "Frame " + std::to_string(std::min(done + 1, j.total)) + "/" + std::to_string(j.total),
                    l2, static_cast<float>(done) / static_cast<float>(std::max<long long>(1, j.total)));
            }
        }
        j.advanceNow = false;
        switch (j.phase) {
        case ExportJob::Phase::Seeking:
            if (!replayCtl.SeekPending() && loading.done && sessionReady) {
                j.phase = ExportJob::Phase::Settle;
                j.settleIters = j.quietIters = 0;
                j.phaseStart = realNow;
            } else if (realNow - j.phaseStart > std::chrono::seconds(120)) {
                ExportFinish(false, "timed out while seeking to the start");
                return;
            }
            break;
        case ExportJob::Phase::Settle: {
            j.settleIters++;
            const bool quiet = levelRenderer && levelRenderer->PendingMeshes() == 0 &&
                               levelRenderer->MeshesInFlight() == 0 && session->ChunkWorkIdle();
            j.quietIters = quiet ? j.quietIters + 1 : 0;
            const int need = j.frame == 0 ? 3 : 1;
            if ((j.settleIters >= need && j.quietIters >= need) ||
                realNow - j.phaseStart > std::chrono::seconds(30))
                j.phase = ExportJob::Phase::Capture;
            break;
        }
        case ExportJob::Phase::Capture:
            break;
        case ExportJob::Phase::Waiting: {
            bool good = false, mismatch = false;
            {
                std::lock_guard<std::mutex> lk(j.capMutex);
                good = !j.ready.empty() || j.written.load() > 0;
                mismatch = j.sizeMismatch;
                j.sizeMismatch = false;
            }
            if (good) {
                j.phase = ExportJob::Phase::Captured;
            } else if (mismatch) {
                if (++j.waitIters > 120) {
                    ExportFinish(false, "the renderer would not produce " + std::to_string(j.renderW) + "x" +
                                            std::to_string(j.renderH) + " frames");
                    return;
                }
                j.frame = 0;
                j.phase = ExportJob::Phase::Capture;
            } else if (++j.waitIters > 600) {
                ExportFinish(false, "the renderer did not deliver a frame");
                return;
            }
            break;
        }
        case ExportJob::Phase::Captured: {
            bool mismatch = false;
            long long queued = 0;
            {
                std::lock_guard<std::mutex> lk(j.capMutex);
                mismatch = j.sizeMismatch;
                queued = static_cast<long long>(j.ready.size());
            }
            if (mismatch) {
                ExportFinish(false, "the window size changed during the export");
                return;
            }
            if (j.frame >= j.total) {
                j.phase = ExportJob::Phase::Draining;
                j.lastProgress = realNow;
                j.lastWritten = j.written.load();
                break;
            }
            if (j.frame - j.written.load() >= j.maxInFlight || queued >= j.maxInFlight) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                break;
            }
            j.targetMs += j.stepMs * timeline.SpeedAt(j.targetMs);
            j.phase = ExportJob::Phase::Advance;
            break;
        }
        case ExportJob::Phase::Draining: {
            const long long done = j.written.load();
            if (done >= j.total) {
                ExportFinish(false, "");
                return;
            }
            if (done != j.lastWritten) {
                j.lastWritten = done;
                j.lastProgress = realNow;
            } else if (realNow - j.lastProgress > std::chrono::seconds(120)) {
                ExportFinish(false, "the encoder stopped responding");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            break;
        }
        case ExportJob::Phase::Advance:
            break;
        }
        if (j.phase == ExportJob::Phase::Advance) {
            j.advanceNow = true;
            j.virtualUs += static_cast<long long>(1000000.0 / j.cfg.fps);
            j.phase = ExportJob::Phase::Settle;
            j.settleIters = j.quietIters = 0;
            j.phaseStart = realNow;
        }
        frameStart = j.virtualBase + std::chrono::microseconds(j.virtualUs);
        dt = j.advanceNow ? static_cast<float>(1.0 / j.cfg.fps) : 1e-5f;
    }

    void Perlin::ExportPostRender() {
        ExportJob& j = exportJob;
        if (!j.active || j.phase != ExportJob::Phase::Capture)
            return;
        ExportJob* job = &j;
        renderer->RequestFrameCapture([job](uint32_t w, uint32_t h, uint32_t pitch, const uint8_t* bgra,
                                            bool flipY) {
            ExportJob::CapturedFrame f;
            bool match = w == static_cast<uint32_t>(job->renderW) && h == static_cast<uint32_t>(job->renderH);
            if (match) {
                f.data.resize(static_cast<size_t>(w) * h * 4);
                for (uint32_t y = 0; y < h; y++) {
                    const uint8_t* src = bgra + static_cast<size_t>(flipY ? h - 1 - y : y) * pitch;
                    std::copy(src, src + static_cast<size_t>(w) * 4,
                              f.data.data() + static_cast<size_t>(y) * w * 4);
                }
            }
            {
                std::lock_guard<std::mutex> lk(job->capMutex);
                if (match)
                    job->ready.push_back(std::move(f));
                else
                    job->sizeMismatch = true;
            }
            job->capCv.notify_all();
        });
        j.frame++;
        j.phase = j.frame == 1 ? ExportJob::Phase::Waiting : ExportJob::Phase::Captured;
        j.waitIters = 0;
    }

    void Perlin::ExportOverlay(ui::Hud& h, int fbW, int fbH) {
        ExportJob& j = exportJob;
        const auto now = Clock::now();
        if (kExportProgressWindow && platform->HasProgressWindow()) {
            if (h.Overlay().HasOverlay())
                h.Overlay().ClearOverlay();
            return;
        }
        const bool visible =
            j.offscreen ||
            !j.screenshot && (j.phase == ExportJob::Phase::Seeking || j.phase == ExportJob::Phase::Draining ||
                              (j.phase == ExportJob::Phase::Settle && j.settleIters <= 1));
        if (!visible) {
            if (h.Overlay().HasOverlay())
                h.Overlay().ClearOverlay();
            return;
        }
        const bool rebuild = j.overlayRml.empty() || now - j.lastOverlay > std::chrono::milliseconds(250);
        if (!rebuild) {
            std::string error;
            if (!h.Overlay().HasOverlay() && !h.Overlay().SetOverlay(j.overlayRml, error))
                Log("ui", "export overlay: {}", error);
            h.UpdateOverlay(fbW, fbH, options.guiScale);
            return;
        }
        j.lastOverlay = now;
        const long long done = j.written.load();
        const double elapsed = std::chrono::duration<double>(now - j.realStart).count();
        const double perFrame = done > 0 ? elapsed / static_cast<double>(done) : 0.0;
        const double eta = perFrame * static_cast<double>(j.total - done);
        ui::PanelModel m;
        m.title = "Exporting " + j.name;
        m.status =
            Num(100.0 * static_cast<double>(done) / static_cast<double>(std::max<long long>(1, j.total)), 1) +
            "%";
        m.progress = static_cast<float>(done) / static_cast<float>(std::max<long long>(1, j.total));
        const auto info = [&](const std::string& a, const std::string& b) {
            ui::PanelRow r;
            r.kind = ui::PanelRow::Kind::Info;
            r.label = a;
            r.value = b;
            m.rows.push_back(r);
        };
        m.exportLayer = j.offscreen;
        m.title = "Exporting";
        m.status =
            "Frame " + std::to_string(std::min(done + 1, j.total)) + "/" + std::to_string(j.total) + "   " +
            Num(100.0 * static_cast<double>(done) / static_cast<double>(std::max<long long>(1, j.total)), 0) +
            "%";
        info("Time left",
             done > 2 && elapsed > 0.5 ? ui::FormatReplayTime(eta * 1000.0) : std::string("estimating..."));
        ui::PanelRow esc;
        esc.kind = ui::PanelRow::Kind::Info;
        esc.label = "Press Esc to cancel";
        m.rows.push_back(esc);
        const auto [scale, guiW, guiH] = detail::OverlayViewport(h, fbW, fbH);
        std::string error;
        m.scroll = 0;
        j.overlayRml = ui::BuildPanelRml(m, guiW, guiH);
        if (!h.Overlay().SetOverlay(j.overlayRml, error))
            Log("ui", "export overlay: {}", error);
        h.UpdateOverlay(fbW, fbH, options.guiScale);
    }
}
