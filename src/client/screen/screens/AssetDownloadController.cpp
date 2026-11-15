#include "client/screen/screens/AssetDownloadController.hpp"

#include "client/screen/screens/LoadingScreen.hpp"
#include "client/screen/screens/ScreenSupport.hpp"
#include "client/systems/i18n/Lang.hpp"
#include "client/ui/GuiViewport.hpp"
#include "util/Env.hpp"
#include "util/Log.hpp"
#include "util/Version.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace perlin::screen {
    AssetDownloadController::AssetDownloadController(Deps d)
        : asset(d.asset), titleScreen(d.titleScreen), screens(d.screens), input(d.input), options(d.options),
          ports(d.ports) {}

    bool AssetDownloadController::Update(ui::Hud& hud, int fbW, int fbH) {
        const std::array<assets::Job*, 2> jobs = ports.AssetJobs();
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_DOWNLOAD") != nullptr;
        bool busy = false, failed = false;
        for (assets::Job* j : jobs) {
            busy = busy || assets::Busy(*j);
            failed = failed || j->stage.load() == assets::Stage::Failed;
        }
        if (failed && PERLIN_ENV("PERLIN_PREVIEW_TITLE"))
            asset.skip = true;
        if (asset.skip || (!busy && !failed && !preview)) {
            if (asset.IsOpen()) {
                screens.Close(asset);
                asset.signature.clear();
                hud.Overlay().ClearOverlay();
                screens.Close(titleScreen);
            }
            return false;
        }
        const auto now = Clock::now();
        if (!asset.IsOpen()) {
            screens.Open(asset);
            asset.openedAt = now;
            asset.sampleAt = now;
            asset.leftDown = true;
            asset.signature.clear();
            for (int i = 0; i < 2; i++) {
                asset.lastBytes[i] = jobs[i]->bytes.done.load();
                asset.speed[i] = 0.0;
            }
            input.SetTextMode(true);
        }
        const double dt = std::chrono::duration<double>(now - asset.sampleAt).count();
        if (dt >= 0.25) {
            for (int i = 0; i < 2; i++) {
                const uint64_t b = jobs[i]->bytes.done.load();
                const double inst =
                    b >= asset.lastBytes[i] ? static_cast<double>(b - asset.lastBytes[i]) / dt : 0.0;
                asset.speed[i] = asset.speed[i] * 0.6 + inst * 0.4;
                asset.lastBytes[i] = b;
            }
            asset.sampleAt = now;
        }

        const auto mb = [](uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); };
        ui::DownloadScreenText t;
        t.title = "Downloading game assets";
        t.subtitle = "This only happens the first time Perlin starts.";
        const char* labels[2] = {"Minecraft resources", "Fonts and menu art"};
        int indeterminate = 0;
        for (int i = 0; i < 2; i++) {
            assets::Job& j = *jobs[i];
            ui::DownloadRow row;
            row.label = labels[i];
            char buf[128];
            const uint64_t total = j.bytes.total.load(), done = j.bytes.done.load();
            switch (preview ? assets::Stage::Downloading : j.stage.load()) {
            case assets::Stage::Idle:
            case assets::Stage::Checking:
                row.status = "Connecting...";
                indeterminate++;
                break;
            case assets::Stage::Downloading:
                if (preview) {
                    std::snprintf(buf, sizeof(buf), "%.1f / %.1f MB  -  %.1f MB/s", 61.3, 147.0, 24.8);
                    row.status = buf;
                    row.progress = 61.3f / 147.0f;
                } else if (total > 0) {
                    std::snprintf(buf, sizeof(buf), "%.1f / %.1f MB  -  %.1f MB/s", mb(done), mb(total),
                                  mb(static_cast<uint64_t>(asset.speed[i])));
                    row.status = buf;
                    row.progress = static_cast<float>(static_cast<double>(done) / static_cast<double>(total));
                } else if (j.filesTotal.load() > 0) {
                    std::snprintf(buf, sizeof(buf), "%llu / %llu files",
                                  static_cast<unsigned long long>(j.filesDone.load()),
                                  static_cast<unsigned long long>(j.filesTotal.load()));
                    row.status = buf;
                    row.progress =
                        static_cast<float>(j.filesDone.load()) / static_cast<float>(j.filesTotal.load());
                } else {
                    row.status = "Downloading...";
                    indeterminate++;
                }
                break;
            case assets::Stage::Extracting:
                if (j.filesTotal.load() > 0) {
                    std::snprintf(buf, sizeof(buf), "Unpacking %llu / %llu files",
                                  static_cast<unsigned long long>(j.filesDone.load()),
                                  static_cast<unsigned long long>(j.filesTotal.load()));
                    row.status = buf;
                    row.progress =
                        static_cast<float>(j.filesDone.load()) / static_cast<float>(j.filesTotal.load());
                } else {
                    row.status = "Unpacking...";
                    indeterminate++;
                }
                break;
            case assets::Stage::Done:
                row.status = "Done";
                row.progress = 1.0f;
                break;
            case assets::Stage::Failed:
                row.status = "Failed";
                row.progress = 1.0f;
                row.failed = true;
                break;
            }
            t.rows.push_back(row);
        }
        if (failed && !busy) {
            t.subtitle = "Some downloads failed. Check your internet connection.";
            t.retry = "Retry";
            t.skip = "Continue anyway";
        }

        const auto [scale, guiW, guiH] = ui::OverlayViewport(hud, fbW, fbH);
        const double time = std::chrono::duration<double>(now - asset.openedAt).count();
        std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "|" + t.subtitle;
        for (const ui::DownloadRow& r : t.rows)
            signature += "|" + r.status + "|" + std::to_string(static_cast<int>(r.progress * 1000.0f));
        if (indeterminate)
            signature += "|" + std::to_string(static_cast<int>(time * 30.0));
        if (signature != asset.signature || !hud.Overlay().HasOverlay()) {
            asset.signature = signature;
            std::string error;
            if (const ui::SceneDoc doc = ui::BuildDownloadScreen(t, guiW, guiH, time);
                !hud.Overlay().SetOverlay(doc, error))
                Log("ui", "download screen: {}", error);
        }
        const glm::vec2 pointer = screen::PointerOrPreview(input);
        float mx = pointer.x, my = pointer.y;
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (!down && asset.leftDown && now - asset.openedAt > std::chrono::milliseconds(300)) {
            const std::string hover = hud.Overlay().OverlayHover();
            if (hover == ui::kDownloadRetryId) {
                Log("ui", "download screen: retry");
                ports.StartAssetDownloads();
            } else if (hover == ui::kDownloadSkipId) {
                asset.skip = true;
            }
        }
        asset.leftDown = down;
        return true;
    }
}
