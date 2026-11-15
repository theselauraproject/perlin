#include "client/ui/GuiViewport.hpp"
#include "util/Version.hpp"
#include "client/features/replay/Studio.hpp"

#include "client/systems/i18n/Lang.hpp"
#include "client/systems/bindings/Keys.hpp"
#include "client/systems/replay/ReplayController.hpp"
#include "client/systems/resources/ServerPacks.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "client/systems/renderer/chunk/LodStore.hpp"
#include "client/ui/ReplayControls.hpp"
#include "client/ui/TextEdit.hpp"
#include "util/Paths.hpp"
#include "util/SimClock.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include "util/Log.hpp"

namespace perlin::replay {
    namespace fs = std::filesystem;

    namespace {
        using Clock = std::chrono::steady_clock;

        const double kSpeeds[] = {0.1, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0};
        constexpr int kSpeedCount = 8;
    }


    Studio::Studio(Deps d)
        : options(d.options), uiAssets(d.uiAssets), serverPacks(d.serverPacks), input(d.input), inputManager(d.inputManager), camera(d.camera), player(d.player),
          chat(d.chat), hudTicks(d.hudTicks), entityRenderer(d.entityRenderer), sounds(d.sounds),
          levelRenderer(d.levelRenderer), renderer(d.renderer), hud(d.hud), particleLevel(d.particleLevel),
          session(d.session), retiredSessions(d.retiredSessions), sessionReady(d.sessionReady),
          connectGeneration(d.connectGeneration), replayCtl(d.controller), host(d.host) {}

    void Studio::Finalize() {
        if (exportJob.active)
            ExportFinish(true, "");
        if (replayCtl.Playing()) {
            SaveTimeline();
            if (replayUi.dirty) {
                std::string saveError;
                replayCtl.SaveMarkers(saveError);
            }
        }
    }

    void Studio::SetStatus(const std::string& text) {
        replayUi.status = text;
        replayUi.statusUntil = Clock::now() + std::chrono::seconds(3);
        replayUi.signature.clear();
    }

    void Studio::MakeSession() {
        const replay::Metadata& meta = replayCtl.Meta();
        session = std::make_unique<BedrockSession>();
        session->BeginReplayMode(meta.playerName.empty() ? options.playerName : meta.playerName,
                                 meta.localRuntimeId, meta.protocol);
        session->SetChunkRadius(options.renderDistance);
        session->SetMaxViewDistance(std::max(32, options.renderDistance));
        session->SetNearRadius(options.enableLods && options.renderDistance > LodStore::kRealChunks
                                   ? LodStore::kRealChunks
                                   : 1 << 20);
        session->SetPackDirectory(paths::DataDir() + "/server_packs");
        sessionReady = true;
    }

    void Studio::MountPacks() {
        const auto stored = replayCtl.StoredPacks();
        const bool had = !serverPacks.Directories().empty();
        if (stored.empty() && !had)
            return;
        serverPacks.Clear();
        serverPacks.SetRoot(paths::DataDir() + "/server_packs_extracted");
        namespace fs = std::filesystem;
        const fs::path dir = fs::path(paths::DataDir()) / "server_packs" / "replay";
        std::error_code ec;
        fs::create_directories(dir, ec);
        size_t mounted = 0;
        for (const auto& sp : stored) {
            const std::string bytes = replayCtl.ReadArchiveFile(sp.file);
            if (bytes.empty())
                continue;
            const fs::path out = dir / replay::SafeFileName(fs::path(sp.file).filename().string());
            if (!fs::exists(out, ec) || fs::file_size(out, ec) != bytes.size()) {
                std::ofstream f(out, std::ios::binary | std::ios::trunc);
                f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                if (!f)
                    continue;
            }
            if (serverPacks.Mount(out.string(), sp.uuid, sp.key))
                mounted++;
        }
        Log("replay", "mounted {} of {} stored resource packs", mounted, stored.size());
        if (levelRenderer)
            levelRenderer->ReloadResources();
        entityRenderer.ClearCaches();
        uiAssets.ClearItemCache();
        i18n::Lang::Get().Invalidate();
        sounds.Reload();
        if (hud)
            hud->Reload();
    }

    void Studio::StartPlayback(const std::string& path) {
        std::string error;
        if (!replayCtl.Open(path, error)) {
            host.ReportOpenFailure("Could not open the replay: " + error);
            return;
        }
        FinishRecording("a replay was opened");
        Log("replay", "opening {} ({} chunks, {}s)", path, replayCtl.Meta().chunks.size(),
            replayCtl.DurationMs() / 1000);
        connectGeneration++;
        particleLevel = nullptr;
        sessionReady = false;
        if (session) {
            if (!session->Transferring())
                session->Disconnect();
            retiredSessions.push_back(std::move(session));
        }
        replayUi = ReplayUi{};
        MountPacks();
        SimClock::EnterVirtual();
        replayPrevMs = 0;
        replayWorldFromMs = 0;
        MakeSession();
        host.DeactivateTitle();
        host.ResetLoading();
        replayCtl.cameraMode = replay::CameraMode::Free;
        exportUi.open = false;
        kfUi = KeyframeUi{};
        editorUi = EditorUi{};
        LoadTimeline();
    }

    void Studio::ExitPlayback(ui::Hud& hud) {
        input.SetFreeCursor(false);
        input.SetTextMode(false);
        if (input.Captured())
            host.SetCursorCaptured(false);
        SaveTimeline();
        timeline = replay::Timeline{};
        exportUi.open = false;
        kfUi = KeyframeUi{};
        if (replayUi.dirty) {
            std::string error;
            if (!replayCtl.SaveMarkers(error))
                Log("replay", "could not save changes: {}", error);
            replayUi.dirty = false;
        }
        replayCtl.Close();
        SimClock::ExitVirtual();
        replayUi = ReplayUi{};
        editorUi = EditorUi{};
        host.ClosePause();
        host.ReturnToReplayList(hud);
    }

    void Studio::UpdateCursor() {
        const bool panel = exportUi.open || kfUi.open;
        if (!host.PauseOpen())
            input.SetTextMode(panel);
        input.SetFreeCursor(true);
        const float mxPx = input.MouseX(), myPx = input.MouseY();
        const bool inView = !editorUi.open || input.Captured() ||
                            (mxPx >= replayUi.editorViewX0 && mxPx < replayUi.editorViewX1 &&
                             myPx >= replayUi.editorViewY0 && myPx < replayUi.editorViewY1);
        const bool wantGrab = !panel && !host.PauseOpen() && !exportJob.active &&
                              (replayUi.grab || (input.IsMouseDown(Mouse::Right) && inView));
        if (wantGrab != input.Captured())
            host.SetCursorCaptured(wantGrab);
        if (panel || host.PauseOpen() || exportJob.active)
            replayUi.grab = false;
    }

    void Studio::Pump(float dt) {
        if (!replayCtl.Playing() || !session || !session->IsReplay())
            return;
        if (replayCtl.NeedsRestart()) {
            replayCtl.ClearRestart();
            const int64_t epoch = static_cast<int64_t>(replayCtl.SeekTargetMs()) / 1000;
            bool keepWorld =
                host.LoadingDone() && session->ReplayCanRewind(epoch) && session->ReplayRewindWorld(epoch);
            std::unique_ptr<BedrockSession> old = std::move(session);
            particleLevel = nullptr;
            MakeSession();
            if (keepWorld) {
                session->AdoptWorld(*old);
                if (levelRenderer)
                    levelRenderer->RebindLevel(&session->NetLevel());
                replayWorldFromMs = static_cast<double>(epoch) * 1000.0;
            } else {
                replayWorldFromMs = 0;
                host.ResetLoading();
            }
            if (!old->Transferring())
                old->Disconnect();
            retiredSessions.push_back(std::move(old));
            replayPrevMs = replayCtl.TimeMs();
        }
        BedrockSession* s = session.get();
        double dtMs = host.LoadingDone() ? static_cast<double>(dt) * 1000.0 : 0.0;
        if (exportJob.active)
            dtMs = host.LoadingDone() && exportJob.advanceNow
                       ? std::max(0.0, exportJob.targetMs - replayCtl.TimeMs())
                       : 0.0;
        else if (!timeline.speed.empty())
            replayCtl.SetSpeed(kSpeeds[replayUi.speedIdx] * timeline.SpeedAt(replayCtl.TimeMs()));
        const double worldFrom = replayWorldFromMs;
        replayCtl.SetClientSink([s](const replay::Record& r) {
            if (r.id == 144 || r.id == 36 || r.id == 44 || r.id == 31)
                s->InjectClientPacket(r.id, r.body);
        });
        replayCtl.Advance(
            dtMs,
            [s, worldFrom](const replay::Record& r) {
                const bool world = r.id == 21 || r.id == 58 || r.id == 110 || r.id == 172 || r.id == 174;
                if (world) {
                    if (static_cast<double>(r.timeMs) < worldFrom)
                        return;
                    s->ReplayEpoch(
                        std::max<int64_t>(r.timeMs / 1000, static_cast<int64_t>(worldFrom / 1000.0)));
                }
                s->InjectIncoming(r.id, r.body);
            },
            exportJob.active ? 0.0 : (replayCtl.CatchingUp() ? 20.0 : 6.0));
        SimClock::Advance(replayCtl.TimeMs() - replayPrevMs);
        replayPrevMs = replayCtl.TimeMs();

        const replay::Pose rec = replayCtl.InterpolatedPose();
        const bool follow = replayCtl.cameraMode == replay::CameraMode::Follow;
        if ((!replayUi.camInit || follow) && rec.known) {
            camera.x = rec.x;
            camera.y = rec.y;
            camera.z = rec.z;
            camera.yaw = rec.yaw + 90.0f;
            camera.pitch = std::clamp(-rec.pitch, -89.0f, 89.0f);
            replayUi.camInit = true;
        } else if (!replayUi.camInit && !rec.known) {
            const auto pose = s->GetPose();
            if (pose.known) {
                camera.x = pose.x;
                camera.y = pose.y + 1.62f;
                camera.z = pose.z;
                replayUi.camInit = true;
            }
        }
        UpdateCursor();
        replayUi.roll = 0.0f;
        replayUi.fovOverride = -1.0f;
        if (replayCtl.cameraMode == replay::CameraMode::Keyframes) {
            replay::CamPose kp;
            if (timeline.EvaluateCamera(replayCtl.TimeMs(), kp)) {
                camera.x = static_cast<float>(kp.x);
                camera.y = static_cast<float>(kp.y);
                camera.z = static_cast<float>(kp.z);
                camera.yaw = kp.yaw;
                camera.pitch = std::clamp(kp.pitch, -89.9f, 89.9f);
                replayUi.roll = kp.roll;
                replayUi.fovOverride = kp.fov;
                replayUi.camInit = true;
            }
        }
        const bool flyEnabled =
            replayCtl.cameraMode == replay::CameraMode::Free ||
            (replayCtl.cameraMode == replay::CameraMode::Keyframes && timeline.camera.empty());
        if (flyEnabled && replayUi.camInit && !host.PauseOpen() && !exportJob.active) {
            const float wheel = inputManager.Scroll();
            if (wheel != 0.0f && !inputManager.IsDown(Action::Zoom) &&
                !(editorUi.open && replayUi.editorOverUi))
                replayUi.flySpeed =
                    std::clamp(replayUi.flySpeed * std::pow(1.15, static_cast<double>(wheel)), 1.0, 200.0);
            const glm::vec3 dir = camera.Direction();
            const glm::vec3 right = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
            glm::vec3 move(0.0f);
            if (inputManager.IsDown(Action::Forward))
                move += dir;
            if (inputManager.IsDown(Action::Back))
                move -= dir;
            if (inputManager.IsDown(Action::Right))
                move += right;
            if (inputManager.IsDown(Action::Left))
                move -= right;
            if (inputManager.IsDown(Action::Jump))
                move.y += 1.0f;
            if (inputManager.IsDown(Action::Sneak))
                move.y -= 1.0f;
            glm::vec3 wanted(0.0f);
            if (glm::length(move) > 0.001f)
                wanted =
                    glm::normalize(move) *
                    static_cast<float>(replayUi.flySpeed * (inputManager.IsDown(Action::Sprint) ? 3.0 : 1.0));
            const float k = 1.0f - std::exp(-12.0f * dt);
            replayUi.flyVel += (wanted - replayUi.flyVel) * k;
            camera.x += replayUi.flyVel.x * dt;
            camera.y += replayUi.flyVel.y * dt;
            camera.z += replayUi.flyVel.z * dt;
            if (!input.TextMode()) {
                const float turn = 110.0f * dt;
                if (input.IsKeyDown(Key::Left))
                    camera.yaw -= turn;
                if (input.IsKeyDown(Key::Right))
                    camera.yaw += turn;
                if (input.IsKeyDown(Key::Up))
                    camera.pitch = std::clamp(camera.pitch + turn, -89.0f, 89.0f);
                if (input.IsKeyDown(Key::Down))
                    camera.pitch = std::clamp(camera.pitch - turn, -89.0f, 89.0f);
            }
        } else {
            replayUi.flyVel = glm::vec3(0.0f);
        }
        player.ReplayPlace(camera.x, camera.y - 1.62, camera.z);
    }

    bool Studio::UpdateScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        (void)live;
        const auto now = Clock::now();
        if (host.PauseOpen()) {
            host.UpdatePause(hud, live, fbW, fbH);
            if (host.PauseOpen())
                return true;
            hud.Overlay().ClearOverlay();
            replayUi.signature.clear();
            replayUi.openedAt = now;
            replayUi.leftDown = true;
        }
        if (exportUi.open)
            return UpdateExportMenu(hud, fbW, fbH);
        if (kfUi.open)
            return UpdateKeyframePanel(hud, fbW, fbH);
        if (editorUi.open)
            return UpdateEditor(hud, fbW, fbH);
        const auto [scale, guiW, guiH] = ui::OverlayViewport(hud, fbW, fbH);
        const ui::ReplayBarLayout layout = ui::ComputeReplayBarLayout(guiW, guiH);

        bool escape = false;
        if (inputManager.Pressed(Action::Pause)) {
            if (replayUi.grab)
                replayUi.grab = false;
            else
                escape = true;
        }
        const auto keyEdge = [&](int code) {
            const bool down = input.IsKeyDown(code);
            const bool edge = down && !replayUi.keyPrev[code & 255];
            replayUi.keyPrev[code & 255] = down;
            return edge;
        };
        const bool kSpace = keyEdge(Key::Space), kLeft = keyEdge('J'), kRight = keyEdge('L'),
                   kM = keyEdge('M'), kC = keyEdge('C'), kK = keyEdge('K'), kI = keyEdge('I'),
                   kO = keyEdge('O'), kTab = keyEdge(Key::Tab), kComma = keyEdge(0xBC),
                   kPeriod = keyEdge(0xBE);
        const auto openMenu = [&](bool open) {
            replayUi.menuOpen = open;
            replayUi.openedAt = now;
            replayUi.signature.clear();
            replayUi.leftDown = true;
            if (!open)
                hud.Overlay().ClearOverlay();
        };
        if (escape)
            openMenu(!replayUi.menuOpen);
        if (kTab) {
            replayUi.grab = !replayUi.grab;
            SetStatus(replayUi.grab ? "Mouse captured - Tab or Esc to release" : "Mouse released");
        }
        const auto togglePlay = [&] {
            if (replayCtl.AtEnd() || replayCtl.TimeMs() >= replayCtl.DurationMs() - 1) {
                replayCtl.Seek(0);
                replayCtl.SetPaused(false);
            } else {
                replayCtl.SetPaused(!replayCtl.Paused());
            }
            replayUi.signature.clear();
        };
        const auto setSpeed = [&](int idx) {
            replayUi.speedIdx = std::clamp(idx, 0, kSpeedCount - 1);
            replayCtl.SetSpeed(kSpeeds[replayUi.speedIdx]);
            char speedText[32];
            std::snprintf(speedText, sizeof speedText, "Speed %gx", kSpeeds[replayUi.speedIdx]);
            SetStatus(speedText);
        };
        if (kSpace)
            togglePlay();
        if (kLeft)
            replayCtl.Seek(replayCtl.TimeMs() - (input.IsKeyDown(Key::Shift) ? 1000.0 : 5000.0));
        if (kRight)
            replayCtl.Seek(replayCtl.TimeMs() + (input.IsKeyDown(Key::Shift) ? 1000.0 : 5000.0));
        if (kComma)
            setSpeed(replayUi.speedIdx - 1);
        if (kPeriod)
            setSpeed(replayUi.speedIdx + 1);
        const auto cycleCamera = [&] {
            replayCtl.cameraMode =
                static_cast<replay::CameraMode>((static_cast<int>(replayCtl.cameraMode) + 1) % 3);
            replayUi.signature.clear();
            static const char* const names[] = {"Camera: free", "Camera: recorded player",
                                                "Camera: keyframe path"};
            SetStatus(names[static_cast<int>(replayCtl.cameraMode)]);
        };
        if (kC)
            cycleCamera();
        if (kK) {
            if (input.IsKeyDown(Key::Shift)) {
                if (timeline.RemoveNear(0, replayCtl.TimeMs(), 400)) {
                    timelineDirty = true;
                    SetStatus("Camera keyframe removed");
                }
            } else {
                replay::CamKey ck;
                ck.tMs = replayCtl.TimeMs();
                ck.x = camera.x;
                ck.y = camera.y;
                ck.z = camera.z;
                ck.yaw = camera.yaw;
                ck.pitch = camera.pitch;
                ck.roll = kfUi.roll;
                ck.fov = kfUi.fov;
                ck.interp = static_cast<replay::Interp>(kfUi.interp);
                timeline.SetCamera(ck);
                timelineDirty = true;
                if (timeline.camera.size() >= 2 && replayCtl.cameraMode != replay::CameraMode::Keyframes)
                    replayCtl.cameraMode = replay::CameraMode::Keyframes;
                SetStatus("Camera keyframe " + std::to_string(timeline.camera.size()) + " set at " +
                             ui::FormatReplayTime(ck.tMs));
            }
        }
        if (kI) {
            replayUi.inMs = replayCtl.TimeMs();
            SetStatus("In point set");
        }
        if (kO) {
            replayUi.outMs = replayCtl.TimeMs();
            SetStatus("Out point set");
        }
        const auto addMarker = [&] {
            replayCtl.AddPlaybackMarker("Marker " + std::to_string(replayCtl.Markers().size() + 1),
                                        static_cast<uint32_t>(replayCtl.TimeMs()));
            replayUi.dirty = true;
            SetStatus("Marker added at " + ui::FormatReplayTime(replayCtl.TimeMs()));
        };
        if (kM)
            addMarker();

        if (!replayUi.status.empty() && now > replayUi.statusUntil) {
            replayUi.status.clear();
            replayUi.signature.clear();
        }
        ui::ReplayBarModel model;
        model.open = replayUi.menuOpen;
        model.paused = replayCtl.Paused();
        model.ended = replayCtl.AtEnd();
        model.catching = replayCtl.CatchingUp();
        model.hideHud = replayUi.hideHud;
        model.hudShown = !options.showHud;
        model.cameraMode = static_cast<int>(replayCtl.cameraMode);
        model.timeMs = replayCtl.TimeMs();
        model.durationMs = replayCtl.DurationMs();
        model.scrubMs = replayUi.scrubbing ? replayUi.scrubMs : -1.0;
        model.speed = replayCtl.Speed();
        model.inMs = replayUi.inMs;
        model.outMs = replayUi.outMs;
        model.title = replayCtl.Meta().name;
        model.status = replayUi.status;
        for (const replay::CamKey& ck : timeline.camera)
            model.keys.push_back({ck.tMs, 0});
        for (const replay::ValueKey& vk : timeline.speed)
            model.keys.push_back({vk.tMs, 1});
        for (const replay::ValueKey& vk : timeline.timeOfDay)
            model.keys.push_back({vk.tMs, 2});
        for (const replay::Marker& m : replayCtl.Markers())
            model.markers.push_back({static_cast<double>(m.timeMs), m.color, m.name});
        std::string signature =
            std::to_string(guiW) + "x" + std::to_string(guiH) + (model.open ? "O" : "c") +
            (model.paused ? "P" : "p") + (model.ended ? "E" : "e") + (model.catching ? "S" : "s") +
            (model.hideHud ? "H" : "h") + (model.hudShown ? "U" : "u") + std::to_string(model.cameraMode) +
            "|" + std::to_string(static_cast<long long>(model.timeMs / (model.open ? 100.0 : 1000.0))) + "|" +
            std::to_string(static_cast<long long>(model.scrubMs)) + "|" + std::to_string(model.speed) + "|" +
            std::to_string(model.inMs) + "|" + std::to_string(model.outMs) + "|" + model.status + "|" +
            std::to_string(model.markers.size()) + "k" + std::to_string(model.keys.size());
        if (signature != replayUi.signature || !hud.Overlay().HasOverlay()) {
            replayUi.signature = signature;
            std::string error;
            if (!hud.Overlay().SetOverlay(ui::BuildReplayBar(model, guiW, guiH), error))
                Log("ui", "replay bar: {}", error);
        }
        const float mx = input.MouseX() / static_cast<float>(scale),
                    my = input.MouseY() / static_cast<float>(scale);
        hud.Overlay().MouseMove(mx, my);
        hud.UpdateOverlay(fbW, fbH, options.guiScale);
        if (!replayUi.menuOpen) {
            replayUi.leftDown = false;
            return true;
        }

        const bool down = input.IsMouseDown(0);
        const std::string hover = hud.Overlay().OverlayHover();
        if (down && !replayUi.leftDown && hover == ui::kRbTrack) {
            replayUi.scrubbing = true;
        }
        if (replayUi.scrubbing) {
            const double frac = std::clamp(
                (static_cast<double>(mx) - layout.trackX) / std::max(1.0f, layout.trackW), 0.0, 1.0);
            replayUi.scrubMs = frac * replayCtl.DurationMs();
            if (!down) {
                replayUi.scrubbing = false;
                replayCtl.Seek(replayUi.scrubMs);
                replayUi.lastScrubSeekMs = -1e9;
                replayUi.signature.clear();
            } else {
                if (std::fabs(replayUi.scrubMs - replayUi.lastScrubSeekMs) > 40.0 &&
                    now - replayUi.lastScrubAt > std::chrono::milliseconds(70)) {
                    replayCtl.Seek(replayUi.scrubMs);
                    replayUi.lastScrubSeekMs = replayUi.scrubMs;
                    replayUi.lastScrubAt = now;
                }
                if (static_cast<long long>(replayUi.scrubMs) / 100 !=
                    static_cast<long long>(model.scrubMs) / 100)
                    replayUi.signature.clear();
            }
        } else if (!down && replayUi.leftDown && now - replayUi.openedAt > std::chrono::milliseconds(150)) {
            if (hover == ui::kRbPlay) {
                togglePlay();
            } else if (hover == ui::kRbBack) {
                replayCtl.Seek(replayCtl.TimeMs() - 5000.0);
            } else if (hover == ui::kRbFwd) {
                replayCtl.Seek(replayCtl.TimeMs() + 5000.0);
            } else if (hover == ui::kRbSlower) {
                setSpeed(replayUi.speedIdx - 1);
            } else if (hover == ui::kRbFaster) {
                setSpeed(replayUi.speedIdx + 1);
            } else if (hover == ui::kRbSpeed) {
                setSpeed(3);
            } else if (hover == ui::kRbCamera) {
                cycleCamera();
            } else if (hover == ui::kRbMarker) {
                addMarker();
            } else if (hover == ui::kRbHud) {
                replayUi.hideHud = !replayUi.hideHud;
                replayUi.signature.clear();
            } else if (hover == ui::kRbOptions) {
                host.OpenPauseOptions(now);
                hud.Overlay().ClearOverlay();
            } else if (hover == ui::kRbExit) {
                ExitPlayback(hud);
                return true;
            } else if (hover == ui::kRbExport) {
                ExportOpenMenu();
                return true;
            } else if (hover == ui::kRbKeyframes) {
                editorUi.open = true;
                editorUi.openedAt = now;
                editorUi.leftDown = true;
                editorUi.signature.clear();
                hud.Overlay().ClearOverlay();
                return true;
            }
        }
        replayUi.leftDown = down;
        return true;
    }

    void Studio::ToggleRecording(bool start) {
        if (start) {
            if (!session || replayCtl.Recording())
                return;
            replay::ReplayController::LiveInfo info;
            info.server = session->ServerAddress();
            info.gameVersion = session->GameVersion();
            info.playerName = session->PlayerName();
            info.protocol = session->ServerProtocol();
            info.runtimeId = session->GetRuntimeId();
            const auto pose = session->GetPose();
            info.pose = {pose.x, pose.y, pose.z, pose.yaw, pose.pitch, pose.known};
            for (const auto& pf : session->ServerPackFiles())
                info.packs.push_back({pf.path, pf.fileName, pf.uuid, pf.key});
            std::string error;
            if (replayCtl.StartRecording(info, kVersion, error)) {
                chat.AddMessage("\xC2\xA7"
                                "aReplay recording started",
                                hudTicks);
                Log("replay", "recording started");
            } else {
                chat.AddMessage("\xC2\xA7"
                                "cCould not start the replay: " +
                                    error,
                                hudTicks);
                Log("replay", "start failed: {}", error);
            }
        } else {
            FinishRecording("you stopped it");
        }
    }

    void Studio::FinishRecording(const char* why) {
        if (!replayCtl.Recording())
            return;
        const uint32_t ms = replayCtl.RecordingMs();
        std::string path, error;
        if (replayCtl.StopRecording(path, error)) {
            Log("replay", "saved {} ({}s) because {}", path, ms / 1000, why);
            chat.AddMessage("\xC2\xA7"
                            "aReplay saved: " +
                                std::filesystem::path(path).filename().string() + " (" +
                                std::to_string(ms / 1000) + "s)",
                            hudTicks);
        } else {
            Log("replay", "save failed: {}", error);
            chat.AddMessage("\xC2\xA7"
                            "cReplay could not be saved: " +
                                error,
                            hudTicks);
        }
    }
}
