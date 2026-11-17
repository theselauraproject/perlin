#include "client/screen/screens/LoadingController.hpp"

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
    LoadingController::LoadingController(Deps d)
        : loading(d.loading), titleScreen(d.titleScreen), screens(d.screens), input(d.input),
          options(d.options), chat(d.chat), panorama(d.panorama), ports(d.ports) {}

    namespace {
        std::string LoadingTr(const char* key, const char* fallback, const std::vector<std::string>& args = {}) {
            std::string s = args.empty() ? i18n::Tr(key) : i18n::Tr(key, args);
            if (s == key || s.empty()) {
                s = fallback;
                for (size_t i = 0; i < args.size(); i++) {
                    const std::string token = "%" + std::to_string(i + 1);
                    for (size_t at = s.find(token); at != std::string::npos; at = s.find(token, at))
                        s.replace(at, token.size(), args[i]);
                }
            }
            return s;
        }
    }

    bool LoadingController::PollLeaseJoin(ui::Hud& hud) {
        if (ports.TakeLeaseJoinFailed()) {
            hud.Overlay().ClearOverlay();
            screens.Close(loading);
            loading.textMode = loading.leftDown = false;
            loading.signature.clear();
            titleScreen.active = true;
            return true;
        }
        ports.PollLeaseJoin();
        return false;
    }

    void LoadingController::ComposePackProgressText(ui::LoadingScreenText& t, const PackDownloader::Progress& progress,
                                         TimePoint now) {
        const auto tr = [](const char* key, const char* fallback, const std::vector<std::string>& args = {}) {
            return LoadingTr(key, fallback, args);
        };
        if (progress.total > 0 && progress.done < progress.total) {
            const int pct = progress.bytesTotal > 0
                                ? static_cast<int>(100.0 * static_cast<double>(progress.bytes) /
                                                   static_cast<double>(progress.bytesTotal))
                                : 0;
            t.message =
                tr("progressScreen.title.downloading", "Downloading packs %1",
                   {std::to_string(progress.done + 1) + "/" + std::to_string(progress.total)});
            t.progress =
                progress.bytesTotal > 0
                    ? static_cast<float>(progress.bytes) / static_cast<float>(progress.bytesTotal)
                    : static_cast<float>(progress.done) / static_cast<float>(progress.total);
            if (!rateInit) {
                rateInit = true;
                sampleAt = now;
            }
            if (progress.bytes < rateBytes) {
                rateBytes = progress.bytes;
                sampleAt = now;
                rate = 0.0;
            }
            if (const double dt = std::chrono::duration<double>(now - sampleAt).count(); dt >= 2.0) {
                rate = static_cast<double>(progress.bytes - rateBytes) / dt;
                rateBytes = progress.bytes;
                sampleAt = now;
            }
            const auto mb = [](uint64_t b) {
                char s[32];
                std::snprintf(s, sizeof(s), "%.1f", static_cast<double>(b) / (1024.0 * 1024.0));
                return std::string(s);
            };
            t.detail = std::to_string(pct) + "%  -  " + mb(progress.bytes) + " / " +
                       mb(progress.bytesTotal) + " MB";
            if (rate > 0.0)
                t.detail += "  -  " + std::to_string(static_cast<int>(rate / 1024.0)) + " KB/s";
        } else {
            t.message = tr("progressScreen.title.applyingPacks", "Loading resource packs");
        }
    }

    bool LoadingController::EvaluateWorldLoad(ui::LoadingScreenText& t, TimePoint now) {
        const auto tr = [](const char* key, const char* fallback, const std::vector<std::string>& args = {}) {
            return LoadingTr(key, fallback, args);
        };
        BedrockSession* session = ports.Session();
        LevelRenderer* levelRenderer = ports.Level();
        bool show = true;
        if (!loading.startAt.time_since_epoch().count())
            loading.startAt = now;
        const int chunks = session->ChunksReceived();
        const int freshChunks = chunks - loading.chunkBase;

        if (levelRenderer)
            levelRenderer->SetFastBuild(true);
        const bool assets =
            levelRenderer && levelRenderer->Blocks().Complete() && levelRenderer->TexturesLoaded() &&
            !levelRenderer->AssetsBusy() &&
            (levelRenderer->ServerBlocksSettled() || now - loading.startAt > std::chrono::seconds(90));
        const bool placed = session->Initialized();
        const size_t meshed =
            levelRenderer ? levelRenderer->MeshedChunks() -
                                std::min(levelRenderer->MeshedChunks(), levelRenderer->MeshesInFlight())
                          : 0;

        const bool enough = freshChunks >= 9 && assets && placed && meshed >= 9 && levelRenderer &&
                            levelRenderer->MeshesInFlight() == 0;
        if (enough) {
            if (!loading.readyArmed) {
                loading.readyArmed = true;
                loading.readyAt = now;
            }
        } else {
            loading.readyArmed = false;
        }

        const auto waited = now - loading.startAt;
        const bool settled = assets && placed && freshChunks > 0 && levelRenderer &&
                             levelRenderer->PendingMeshes() == 0 &&
                             levelRenderer->MeshesInFlight() == 0 && waited > std::chrono::seconds(8);
        if ((loading.readyArmed && now - loading.readyAt >= std::chrono::milliseconds(600)) || settled) {
            loading.done = true;
            show = false;
        } else if (!assets) {
            t.message = tr("perlin.loading.textures", "Loading block textures");
            if (levelRenderer) {
                const LevelRenderer::LoadStatus status = levelRenderer->AssetStatus();
                if (!status.message.empty())
                    t.message = status.message;
                t.detail = status.detail;
                t.progress = status.progress;
            }
        } else if (!placed || freshChunks == 0) {
            t.message = tr("perlin.loading.world", "Waiting for the world");
            if (waited > std::chrono::seconds(20))
                t.detail =
                    tr("perlin.loading.worldSlow", "The server has not sent any terrain yet (%1 s)",
                       {std::to_string(static_cast<int>(
                           std::chrono::duration_cast<std::chrono::seconds>(waited).count()))});
        } else {
            t.message = tr("progressScreen.message.building", "Building terrain");
            const float fraction =
                std::min(1.0f, static_cast<float>(std::min<size_t>(meshed, 49)) / 49.0f);
            t.progress = std::min(0.98f, fraction);
        }
        return show;
    }

    void LoadingController::Hide(ui::Hud& hud) {
        if (LevelRenderer* levelRenderer = ports.Level())
            levelRenderer->SetFastBuild(false);
        if (loading.IsOpen()) {
            hud.Overlay().ClearOverlay();
            screens.Close(loading);
            loading.signature.clear();
            loading.startAt = {};
            loading.readyArmed = false;
        }
        if (loading.textMode) {
            loading.textMode = false;
            if (!chat.IsOpen())
                input.SetTextMode(false);
        }
        loading.leftDown = false;
    }

    bool LoadingController::Present(ui::Hud& hud, int fbW, int fbH, ui::LoadingScreenText& t, TimePoint now,
                                      bool preview) {
        const auto tr = [](const char* key, const char* fallback, const std::vector<std::string>& args = {}) {
            return LoadingTr(key, fallback, args);
        };
        t.cancel = tr("gui.cancel", "Cancel");
        if (t.progress >= 0.0f && t.detail.empty())
            t.detail = std::to_string(static_cast<int>(std::clamp(t.progress, 0.0f, 1.0f) * 100.0f)) + "%";
        if (!input.TextMode())
            input.SetTextMode(true);
        loading.textMode = true;
        if (!loading.IsOpen()) {
            loading.openedAt = now;
            loading.leftDown = true;
            screens.Open(loading);
            ports.CloseGameplayScreens();
        }
        const auto [scale, guiW, guiH] = ui::OverlayViewport(hud, fbW, fbH);
        const bool modHidden = ports.ModHidesLoading();
        loading.modHidden = modHidden;
        t.panorama = !modHidden && panorama.Draw(fbW, fbH, true);
        const std::string signature = std::to_string(guiW) + "x" + std::to_string(guiH) + "|" + t.title +
                                      "|" + t.message + "|" + t.detail + "|" + t.cancel +
                                      (t.panorama ? "|p" : "|d");

        if (modHidden) {
            if (hud.Overlay().HasOverlay())
                hud.Overlay().ClearOverlay();
            loading.signature.clear();
        } else if (signature != loading.signature || !hud.Overlay().HasOverlay()) {
            loading.signature = signature;
            std::string error;
            hud.Overlay().SetOverlay(ui::BuildLoadingScreen(t, guiW, guiH), error);
        }
        const glm::vec2 pointer = screen::PointerOrPreview(input);
        float mx = pointer.x, my = pointer.y;
        hud.Overlay().MouseMove(mx / static_cast<float>(scale), my / static_cast<float>(scale));
        hud.UpdateOverlay(fbW, fbH, options.guiScale);

        const bool down = input.IsMouseDown(0);
        if (!down && loading.leftDown && !preview &&
            now - loading.openedAt > std::chrono::milliseconds(300) &&
            hud.Overlay().OverlayHover() == ui::kLoadingCancelId) {
            Log("ui", "loading screen: cancel");
            ports.LeaveToServerList(hud);
            return true;
        }
        loading.leftDown = down;
        return true;
    }

    bool LoadingController::Update(ui::Hud& hud, int fbW, int fbH) {
        if (PollLeaseJoin(hud))
            return false;
        if (titleScreen.active || titleScreen.IsOpen())
            return false;
        const auto now = Clock::now();
        const bool preview = PERLIN_ENV("PERLIN_PREVIEW_LOADING") != nullptr;
        const auto tr = [](const char* key, const char* fallback, const std::vector<std::string>& args = {}) {
            return LoadingTr(key, fallback, args);
        };
        ui::LoadingScreenText t;
        t.title = tr("progressScreen.title.connectingExternal", "Connecting to external server");
        if (!ports.ReplayPlaying())
            t.warning = "Perlin is not yet stable, and may get you kicked/banned by server anticheats, due "
                        "to movement and other inaccuracies";
        bool show = false;
        BedrockSession* session = ports.Session();
        const bool sessionReady = ports.SessionReady();
        const bool transferring = session && session->Transferring();
        if (preview) {
            show = true;
            t.message = tr("progressScreen.title.downloading", "Downloading packs %1", {"3/25"});
            t.progress = 0.4f;
            t.detail = "40%  -  8.7 / 21.7 MB  -  262 KB/s";
        } else if (PERLIN_ENV("PERLIN_PREVIEW") && !session) {
            show = false;
        } else if (!session || (!sessionReady && session->GetState() != BedrockSession::State::Failed) ||
                   transferring) {
            show = true;
            if (transferring)
                loading.done = false;
            const BedrockSession::State st =
                session ? session->GetState() : BedrockSession::State::Disconnected;
            const auto progress = session ? session->PackProgress() : PackDownloader::Progress{};
            const std::string hint = ports.StatusHint();
            if (!hint.empty()) {
                t.message = tr("connect.authorizing", "Logging in...");
                t.detail = hint;
            } else if (!session || st == BedrockSession::State::Disconnected) {
                t.message = tr("progressScreen.message.resourceLoading", "Loading Resources");
            } else if (st == BedrockSession::State::Connecting) {
                t.message = ports.OnlineThisRun() ? tr("connect.authorizing", "Logging in...")
                                          : tr("progressScreen.message.locating", "Locating server");
            } else if (st == BedrockSession::State::ResourcePacks) {
                ComposePackProgressText(t, progress, now);
            } else {
                t.message = tr("connect.connecting", "Connecting to the server...");
            }
        } else if (sessionReady && !loading.done) {
            show = EvaluateWorldLoad(t, now);
        }
        if (!show) {
            Hide(hud);
            return false;
        }
        return Present(hud, fbW, fbH, t, now, preview);
    }
}
