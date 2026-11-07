#include "client/PerlinInternal.hpp"
#include "client/Perlin.hpp"

#include "client/i18n/Lang.hpp"
#include "client/input/Keys.hpp"
#include "client/replay/ReplayController.hpp"
#include "client/resources/ServerPacks.hpp"
#include "client/ui/UiAssets.hpp"
#include "client/platform/AppPlatform.hpp"
#include "client/renderer/chunk/LodStore.hpp"
#include "client/ui/ReplayScreen.hpp"
#include "client/ui/TextEdit.hpp"
#include "client/ui/TitleScreen.hpp"
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

namespace perlin {
    namespace fs = std::filesystem;

    namespace {
        using Clock = std::chrono::steady_clock;

        struct CachedMeta {
            fs::file_time_type mtime;
            uintmax_t size = 0;
            replay::Metadata meta;
            bool ok = false;
            std::string error;
        };
        std::map<std::string, CachedMeta>& MetaCache() {
            static std::map<std::string, CachedMeta> cache;
            return cache;
        }

        std::string Duration(double ms) {
            return ui::FormatReplayTime(ms);
        }

        std::string SizeText(uintmax_t bytes) {
            char buf[48];
            if (bytes >= (1u << 30))
                std::snprintf(buf, sizeof buf, "%.1f GB", static_cast<double>(bytes) / (1u << 30));
            else if (bytes >= (1u << 20))
                std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(bytes) / (1u << 20));
            else
                std::snprintf(buf, sizeof buf, "%.0f KB", static_cast<double>(bytes) / 1024.0);
            return buf;
        }

        std::string DateText(long long unixTime) {
            if (unixTime <= 0)
                return "unknown date";
            const std::time_t t = static_cast<std::time_t>(unixTime);
            std::tm tmv{};
#ifdef _WIN32
            localtime_s(&tmv, &t);
#else
            localtime_r(&t, &tmv);
#endif
            char buf[64];
            std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tmv);
            return buf;
        }

        const double kSpeeds[] = {0.1, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0};
        constexpr int kSpeedCount = 8;
    }

    void Perlin::RefreshReplayList() {
        std::error_code ec;
        fs::create_directories(replay::ReplayDirectory(), ec);
        if (!replayRecoveryChecked) {
            replayRecoveryChecked = true;
            std::string recovered;
            if (replay::ReplayController::RecoverInterrupted(recovered))
                title.replayMessage = "Recovered an interrupted recording";
        }
        std::vector<ui::ReplayEntry> list;
        for (const auto& e : fs::directory_iterator(replay::ReplayDirectory(), ec)) {
            if (!e.is_regular_file(ec) || e.path().extension() != replay::kExtension)
                continue;
            const std::string key = e.path().string();
            CachedMeta& cm = MetaCache()[key];
            const auto mtime = e.last_write_time(ec);
            const uintmax_t size = e.file_size(ec);
            if (cm.mtime != mtime || cm.size != size || (!cm.ok && cm.error.empty())) {
                cm = CachedMeta{};
                cm.mtime = mtime;
                cm.size = size;
                cm.ok = replay::Reader::ReadMetadata(key, cm.meta, nullptr, cm.error);
            }
            ui::ReplayEntry r;
            r.file = key;
            r.sizeBytes = static_cast<long long>(size);
            if (cm.ok) {
                r.name = cm.meta.name.empty() ? e.path().stem().string() : cm.meta.name;
                r.created = cm.meta.createdUnix;
                r.durationMs = cm.meta.durationMs;
                r.line1 = Duration(cm.meta.durationMs) + "  -  " + DateText(cm.meta.createdUnix) + "  -  " +
                          SizeText(size);
                r.line2 = (cm.meta.server.empty() ? std::string("unknown server") : cm.meta.server) +
                          "  -  " + cm.meta.playerName +
                          (cm.meta.gameVersion.empty() ? std::string() : "  -  " + cm.meta.gameVersion);
            } else {
                r.name = e.path().stem().string();
                r.broken = true;
                r.line1 = "Can't open: " + cm.error;
                r.line2 = e.path().filename().string();
            }
            list.push_back(std::move(r));
        }
        const int sort = title.replaySort;
        const bool desc = title.replaySortDesc;
        const auto key = [&](const ui::ReplayEntry& a, const ui::ReplayEntry& b) {
            const auto cmp = [](auto x, auto y) { return x < y ? -1 : (y < x ? 1 : 0); };
            switch (sort) {
            case 1:
                return cmp(a.name, b.name);
            case 2:
                return cmp(a.line2, b.line2);
            case 3:
                return cmp(a.durationMs, b.durationMs);
            case 4:
                return cmp(a.sizeBytes, b.sizeBytes);
            default:
                return cmp(a.created, b.created);
            }
        };
        std::stable_sort(list.begin(), list.end(), [&](const ui::ReplayEntry& a, const ui::ReplayEntry& b) {
            const int c = key(a, b);
            return desc ? c > 0 : c < 0;
        });
        std::string selected;
        if (title.replaySelected >= 0 && title.replaySelected < static_cast<int>(title.replays.size()))
            selected = title.replays[static_cast<size_t>(title.replaySelected)].file;
        title.replays = std::move(list);
        title.replaySelected = -1;
        for (size_t i = 0; i < title.replays.size(); i++)
            if (title.replays[i].file == selected)
                title.replaySelected = static_cast<int>(i);
        titleScreen.signature.clear();
    }

    bool Perlin::TitleReplayTyped(const InputManager::Typed& t) {
        const int key = t.key;
        const bool character = t.character;
        if (title.page == ui::TitlePage::ReplayRename) {
            if (!character && key == Key::Enter)
                TitleReplayClick(ui::kReplayRenameOk);
            else
                ui::TextEdit::Focused().Apply(t, title.replayName, ui::TextLimits{60});
            return true;
        }
        if (title.page == ui::TitlePage::Replays) {
            const int count = static_cast<int>(title.replays.size());
            if (!character && key == Key::Enter) {
                TitleReplayClick(ui::kReplayPlay);
            } else if (!character && key == Key::Down && count) {
                title.replaySelected = std::min(count - 1, title.replaySelected + 1);
            } else if (!character && key == Key::Up && count) {
                title.replaySelected = std::max(0, title.replaySelected - 1);
            } else if (!character && key == Key::Delete && title.replaySelected >= 0) {
                title.page = ui::TitlePage::ReplayDelete;
            }
            return true;
        }
        return title.page == ui::TitlePage::ReplayDelete;
    }

    bool Perlin::TitleReplayClick(const std::string& hover) {
        const auto now = Clock::now();
        const auto selectedEntry = [&]() -> const ui::ReplayEntry* {
            if (title.replaySelected < 0 || title.replaySelected >= static_cast<int>(title.replays.size()))
                return nullptr;
            return &title.replays[static_cast<size_t>(title.replaySelected)];
        };
        if (hover == ui::kTitleReplays) {
            title.page = ui::TitlePage::Replays;
            title.replayMessage.clear();
            title.replaySelected = -1;
            title.replayScroll = 0;
            RefreshReplayList();
            return true;
        }
        if (title.page != ui::TitlePage::Replays && title.page != ui::TitlePage::ReplayRename &&
            title.page != ui::TitlePage::ReplayDelete)
            return false;
        const std::string rowPrefix = ui::kReplayRowPrefix;
        if (hover.compare(0, rowPrefix.size(), rowPrefix) == 0) {
            const int row = std::atoi(hover.c_str() + rowPrefix.size());
            title.replaySelected = row;
            if (row == replayClickRow && now - replayClickAt < std::chrono::milliseconds(400))
                TitleReplayClick(ui::kReplayPlay);
            replayClickRow = row;
            replayClickAt = now;
            return true;
        }
        if (hover == ui::kReplayPlay) {
            if (const ui::ReplayEntry* e = selectedEntry(); e && !e->broken)
                StartReplayPlayback(e->file);
            return true;
        }
        if (hover == ui::kReplayDone) {
            title.page = ui::TitlePage::Main;
            return true;
        }
        if (hover == ui::kReplayFolder) {
            std::error_code ec;
            fs::create_directories(replay::ReplayDirectory(), ec);
            std::string p = replay::ReplayDirectory();
            std::replace(p.begin(), p.end(), '\\', '/');
            platform->OpenUrl("file:///" + (!p.empty() && p[0] == '/' ? p.substr(1) : p));
            return true;
        }
        if (hover == ui::kReplaySort) {
            title.replaySort = (title.replaySort + 1) % 5;
            RefreshReplayList();
            return true;
        }
        if (hover == ui::kReplayDirection) {
            title.replaySortDesc = !title.replaySortDesc;
            RefreshReplayList();
            return true;
        }
        if (hover == ui::kReplayRename) {
            if (const ui::ReplayEntry* e = selectedEntry()) {
                title.replayName = e->name;
                title.page = ui::TitlePage::ReplayRename;
            }
            return true;
        }
        if (hover == ui::kReplayDelete) {
            if (selectedEntry())
                title.page = ui::TitlePage::ReplayDelete;
            return true;
        }
        if (hover == ui::kReplayDialogCancel) {
            title.page = ui::TitlePage::Replays;
            return true;
        }
        if (hover == ui::kReplayRenameOk) {
            if (const ui::ReplayEntry* e = selectedEntry()) {
                replay::Metadata meta;
                std::string error;
                const std::string file = e->file;
                if (replay::Reader::ReadMetadata(file, meta, nullptr, error)) {
                    meta.name = title.replayName.empty() ? meta.name : title.replayName;
                    if (replay::UpdateArchiveFile(file, "metadata.json", meta.ToJson().Dump(2), error))
                        title.replayMessage = "Saved";
                    else
                        title.replayMessage = "Could not save: " + error;
                } else {
                    title.replayMessage = "Could not read the replay: " + error;
                }
                MetaCache().erase(file);
            }
            title.page = ui::TitlePage::Replays;
            RefreshReplayList();
            return true;
        }
        if (hover == ui::kReplayDeleteOk) {
            if (const ui::ReplayEntry* e = selectedEntry()) {
                std::error_code ec;
                fs::remove(e->file, ec);
                MetaCache().erase(e->file);
                title.replayMessage = ec ? "Could not delete: " + ec.message() : "Deleted";
            }
            title.replaySelected = -1;
            title.page = ui::TitlePage::Replays;
            RefreshReplayList();
            return true;
        }
        return false;
    }

    bool Perlin::ImportReplayFile(const std::string& source) {
        std::error_code ec;
        replay::Metadata meta;
        std::string error;
        if (!replay::Reader::ReadMetadata(source, meta, nullptr, error)) {
            title.replayMessage = "Not a valid replay: " + error;
            chat.AddMessage("\xC2\xA7"
                            "cNot a valid replay: " +
                                error,
                            hudAnim.ticks);
            return false;
        }
        fs::create_directories(replay::ReplayDirectory(), ec);
        fs::path dest =
            fs::path(replay::ReplayDirectory()) / replay::SafeFileName(fs::path(source).stem().string());
        dest += replay::kExtension;
        for (int n = 2; fs::exists(dest, ec); n++) {
            dest = fs::path(replay::ReplayDirectory()) /
                   (replay::SafeFileName(fs::path(source).stem().string()) + "_" + std::to_string(n) +
                    replay::kExtension);
        }
        fs::copy_file(source, dest, fs::copy_options::none, ec);
        if (ec) {
            title.replayMessage = "Could not import: " + ec.message();
            return false;
        }
        title.replayMessage = "Imported " + dest.filename().string();
        chat.AddMessage("\xC2\xA7"
                        "aImported replay " +
                            dest.filename().string(),
                        hudAnim.ticks);
        if (title.page == ui::TitlePage::Replays)
            RefreshReplayList();
        return true;
    }

    void Perlin::ReplayStatus(const std::string& text) {
        replayUi.status = text;
        replayUi.statusUntil = Clock::now() + std::chrono::seconds(3);
        replayUi.signature.clear();
    }

    void Perlin::MakeReplaySession() {
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

    void Perlin::MountReplayPacks() {
        const auto stored = replayCtl.StoredPacks();
        const bool had = !ServerPacks::Instance().Directories().empty();
        if (stored.empty() && !had)
            return;
        ServerPacks::Instance().Clear();
        ServerPacks::Instance().SetRoot(paths::DataDir() + "/server_packs_extracted");
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
            if (ServerPacks::Instance().Mount(out.string(), sp.uuid, sp.key))
                mounted++;
        }
        Log("replay", "mounted {} of {} stored resource packs", mounted, stored.size());
        if (levelRenderer)
            levelRenderer->ReloadResources();
        entityRenderer.ClearCaches();
        ui::UiAssets::Instance().ClearItemCache();
        i18n::Lang::Get().Invalidate();
        sounds.Reload();
        if (hud)
            hud->Reload();
    }

    void Perlin::StartReplayPlayback(const std::string& path) {
        std::string error;
        if (!replayCtl.Open(path, error)) {
            title.replayMessage = "Could not open the replay: " + error;
            titleScreen.signature.clear();
            return;
        }
        FinishReplayRecording("a replay was opened");
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
        MountReplayPacks();
        SimClock::EnterVirtual();
        replayPrevMs = 0;
        replayWorldFromMs = 0;
        MakeReplaySession();
        titleScreen.active = false;
        loading.done = false;
        loading.startAt = {};
        loading.readyArmed = false;
        replayCtl.cameraMode = replay::CameraMode::Free;
        exportUi.open = false;
        kfUi = KeyframeUi{};
        editorUi = EditorUi{};
        LoadTimeline();
    }

    void Perlin::ExitReplayPlayback(ui::Hud& hud) {
        input.SetFreeCursor(false);
        input.SetTextMode(false);
        if (input.Captured())
            platform->SetCursorCaptured(false);
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
        pause.shown = false;
        titleScreen.returnToServers = false;
        titleScreen.returnToReplays = true;
        LeaveToServerList(hud);
        titleScreen.returnToServers = false;
        titleScreen.returnToReplays = true;
    }

    void Perlin::UpdateReplayCursor() {
        const bool panel = exportUi.open || kfUi.open;
        if (!pause.shown)
            input.SetTextMode(panel);
        input.SetFreeCursor(true);
        const float mxPx = input.MouseX(), myPx = input.MouseY();
        const bool inView = !editorUi.open || input.Captured() ||
                            (mxPx >= replayUi.editorViewX0 && mxPx < replayUi.editorViewX1 &&
                             myPx >= replayUi.editorViewY0 && myPx < replayUi.editorViewY1);
        const bool wantGrab = !panel && !pause.shown && !exportJob.active &&
                              (replayUi.grab || (input.IsMouseDown(Mouse::Right) && inView));
        if (wantGrab != input.Captured())
            platform->SetCursorCaptured(wantGrab);
        if (panel || pause.shown || exportJob.active)
            replayUi.grab = false;
    }

    void Perlin::PumpReplay(float dt) {
        if (!replayCtl.Playing() || !session || !session->IsReplay())
            return;
        if (replayCtl.NeedsRestart()) {
            replayCtl.ClearRestart();
            const int64_t epoch = static_cast<int64_t>(replayCtl.SeekTargetMs()) / 1000;
            bool keepWorld =
                loading.done && session->ReplayCanRewind(epoch) && session->ReplayRewindWorld(epoch);
            std::unique_ptr<BedrockSession> old = std::move(session);
            particleLevel = nullptr;
            MakeReplaySession();
            if (keepWorld) {
                session->AdoptWorld(*old);
                if (levelRenderer)
                    levelRenderer->RebindLevel(&session->NetLevel());
                replayWorldFromMs = static_cast<double>(epoch) * 1000.0;
            } else {
                replayWorldFromMs = 0;
                loading.done = false;
                loading.startAt = {};
                loading.readyArmed = false;
            }
            if (!old->Transferring())
                old->Disconnect();
            retiredSessions.push_back(std::move(old));
            replayPrevMs = replayCtl.TimeMs();
        }
        BedrockSession* s = session.get();
        double dtMs = loading.done ? static_cast<double>(dt) * 1000.0 : 0.0;
        if (exportJob.active)
            dtMs = loading.done && exportJob.advanceNow
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
        UpdateReplayCursor();
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
        if (flyEnabled && replayUi.camInit && !pause.shown && !exportJob.active) {
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

    bool Perlin::UpdateReplayScreen(ui::Hud& hud, BedrockSession* live, int fbW, int fbH) {
        (void)live;
        const auto now = Clock::now();
        if (pause.shown) {
            UpdatePauseScreen(hud, live, fbW, fbH);
            if (pause.shown)
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
        const auto [scale, guiW, guiH] = detail::OverlayViewport(hud, fbW, fbH);
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
            ReplayStatus(replayUi.grab ? "Mouse captured - Tab or Esc to release" : "Mouse released");
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
            ReplayStatus(speedText);
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
            ReplayStatus(names[static_cast<int>(replayCtl.cameraMode)]);
        };
        if (kC)
            cycleCamera();
        if (kK) {
            if (input.IsKeyDown(Key::Shift)) {
                if (timeline.RemoveNear(0, replayCtl.TimeMs(), 400)) {
                    timelineDirty = true;
                    ReplayStatus("Camera keyframe removed");
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
                ReplayStatus("Camera keyframe " + std::to_string(timeline.camera.size()) + " set at " +
                             ui::FormatReplayTime(ck.tMs));
            }
        }
        if (kI) {
            replayUi.inMs = replayCtl.TimeMs();
            ReplayStatus("In point set");
        }
        if (kO) {
            replayUi.outMs = replayCtl.TimeMs();
            ReplayStatus("Out point set");
        }
        const auto addMarker = [&] {
            replayCtl.AddPlaybackMarker("Marker " + std::to_string(replayCtl.Markers().size() + 1),
                                        static_cast<uint32_t>(replayCtl.TimeMs()));
            replayUi.dirty = true;
            ReplayStatus("Marker added at " + ui::FormatReplayTime(replayCtl.TimeMs()));
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
            if (!hud.Overlay().SetOverlay(ui::BuildReplayBarRml(model, guiW, guiH), error))
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
                pause.shown = true;
                pause.view = 1;
                optionsUi.page = 0;
                optionsUi.sub = 0;
                pause.openedAt = now;
                pause.signature.clear();
                optionsUi.signature.clear();
                pause.leftDown = true;
                hud.Overlay().ClearOverlay();
            } else if (hover == ui::kRbExit) {
                ExitReplayPlayback(hud);
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
}

namespace perlin {
    void Perlin::ToggleReplayRecording(bool start) {
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
            if (replayCtl.StartRecording(info, PERLIN_VERSION, error)) {
                chat.AddMessage("\xC2\xA7"
                                "aReplay recording started",
                                hudAnim.ticks);
                Log("replay", "recording started");
            } else {
                chat.AddMessage("\xC2\xA7"
                                "cCould not start the replay: " +
                                    error,
                                hudAnim.ticks);
                Log("replay", "start failed: {}", error);
            }
        } else {
            FinishReplayRecording("you stopped it");
        }
    }

    void Perlin::FinishReplayRecording(const char* why) {
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
                            hudAnim.ticks);
        } else {
            Log("replay", "save failed: {}", error);
            chat.AddMessage("\xC2\xA7"
                            "cReplay could not be saved: " +
                                error,
                            hudAnim.ticks);
        }
    }
}
