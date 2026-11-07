#include "client/PerlinInternal.hpp"
#include "client/Perlin.hpp"
#include "client/input/Keys.hpp"
#include "client/replay/Timeline.hpp"
#include "client/ui/ReplayScreen.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include "util/Log.hpp"

namespace perlin {
    namespace {
        using Clock = std::chrono::steady_clock;
        std::string Fmt(double v, int decimals = 0) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
            return buf;
        }
        const double kSpeedSteps[] = {0.1, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0};
        const float kValueLadder[] = {0.05f, 0.1f, 0.25f, 0.5f,  0.75f, 1.0f,  1.5f,
                                      2.0f,  4.0f, 8.0f,  16.0f, 32.0f, 64.0f, 100.0f};
    }

    bool Perlin::UpdateEditor(ui::Hud& h, int fbW, int fbH) {
        using namespace replay;
        EditorUi& e = editorUi;
        const auto now = Clock::now();
        const auto [scale, guiW, guiH] = detail::OverlayViewport(h, fbW, fbH);
        const ui::EditorLayout lay = ui::ComputeEditorLayout(guiW, guiH);
        const double dur = std::max(1.0, static_cast<double>(replayCtl.DurationMs()));
        const bool shift = input.IsKeyDown(Key::Shift);
        const double t = replayCtl.TimeMs();

        if (e.viewSpan <= 0)
            e.viewSpan = dur;
        e.viewSpan = std::clamp(e.viewSpan, std::min(200.0, dur), dur);
        e.viewStart = std::clamp(e.viewStart, 0.0, std::max(0.0, dur - e.viewSpan));

        const auto count = [&](int tr) -> int {
            return tr == 0   ? static_cast<int>(timeline.camera.size())
                   : tr == 1 ? static_cast<int>(timeline.speed.size())
                             : static_cast<int>(timeline.timeOfDay.size());
        };
        const auto keyT = [&](int tr, int i) -> double {
            return tr == 0   ? timeline.camera[static_cast<size_t>(i)].tMs
                   : tr == 1 ? timeline.speed[static_cast<size_t>(i)].tMs
                             : timeline.timeOfDay[static_cast<size_t>(i)].tMs;
        };
        const auto setKeyT = [&](int tr, int i, double v) {
            (tr == 0   ? timeline.camera[static_cast<size_t>(i)].tMs
             : tr == 1 ? timeline.speed[static_cast<size_t>(i)].tMs
                       : timeline.timeOfDay[static_cast<size_t>(i)].tMs) = v;
        };
        const auto interpOf = [&](int tr, int i) -> Interp {
            return tr == 0   ? timeline.camera[static_cast<size_t>(i)].interp
                   : tr == 1 ? timeline.speed[static_cast<size_t>(i)].interp
                             : timeline.timeOfDay[static_cast<size_t>(i)].interp;
        };
        const auto setInterp = [&](int tr, int i, Interp v) {
            (tr == 0   ? timeline.camera[static_cast<size_t>(i)].interp
             : tr == 1 ? timeline.speed[static_cast<size_t>(i)].interp
                       : timeline.timeOfDay[static_cast<size_t>(i)].interp) = v;
        };
        const auto selValid = [&] {
            return e.selTrack >= 0 && e.selTrack < 3 && e.selIndex >= 0 && e.selIndex < count(e.selTrack);
        };
        const auto resort = [&](double selectedT) {
            const auto byT = [](const auto& a, const auto& b) { return a.tMs < b.tMs; };
            std::stable_sort(timeline.camera.begin(), timeline.camera.end(), byT);
            std::stable_sort(timeline.speed.begin(), timeline.speed.end(), byT);
            std::stable_sort(timeline.timeOfDay.begin(), timeline.timeOfDay.end(), byT);
            if (e.selTrack >= 0 && e.selTrack < 3)
                for (int i = 0; i < count(e.selTrack); i++)
                    if (std::fabs(keyT(e.selTrack, i) - selectedT) < 0.5) {
                        e.selIndex = i;
                        break;
                    }
            timelineDirty = true;
        };
        const auto seekTo = [&](double ms, bool force) {
            ms = std::clamp(ms, 0.0, dur);
            if (force ||
                (std::fabs(ms - e.lastSeekMs) > 40.0 && now - e.lastSeekAt > std::chrono::milliseconds(70))) {
                replayCtl.Seek(ms);
                e.lastSeekMs = ms;
                e.lastSeekAt = now;
            }
        };
        const auto addCameraKey = [&] {
            CamKey ck;
            ck.tMs = replayCtl.TimeMs();
            ck.x = camera.x;
            ck.y = camera.y;
            ck.z = camera.z;
            ck.yaw = camera.yaw;
            ck.pitch = camera.pitch;
            ck.roll = kfUi.roll;
            ck.fov = kfUi.fov;
            ck.interp = static_cast<Interp>(kfUi.interp);
            timeline.SetCamera(ck);
            timelineDirty = true;
            e.selTrack = 0;
            for (int i = 0; i < count(0); i++)
                if (std::fabs(keyT(0, i) - ck.tMs) < 51.0)
                    e.selIndex = i;
            if (timeline.camera.size() >= 2 && replayCtl.cameraMode != CameraMode::Keyframes)
                replayCtl.cameraMode = CameraMode::Keyframes;
            ReplayStatus("Camera key " + std::to_string(timeline.camera.size()) + " at " +
                         ui::FormatReplayTimeMs(ck.tMs));
        };
        const auto deleteSelected = [&] {
            if (!selValid())
                return;
            if (e.selTrack == 0)
                timeline.camera.erase(timeline.camera.begin() + e.selIndex);
            else if (e.selTrack == 1)
                timeline.speed.erase(timeline.speed.begin() + e.selIndex);
            else
                timeline.timeOfDay.erase(timeline.timeOfDay.begin() + e.selIndex);
            e.selIndex = -1;
            timelineDirty = true;
        };
        const auto goToView = [&](const CamKey& ck) {
            camera.x = static_cast<float>(ck.x);
            camera.y = static_cast<float>(ck.y);
            camera.z = static_cast<float>(ck.z);
            camera.yaw = ck.yaw;
            camera.pitch = ck.pitch;
        };
        const auto closeEditor = [&] {
            e.open = false;
            e.drag = 0;
            replayUi.editorOverUi = false;
            replayUi.editorViewX0 = replayUi.editorViewY0 = 0;
            replayUi.editorViewX1 = replayUi.editorViewY1 = 1e9f;
            h.Overlay().ClearOverlay();
            replayUi.signature.clear();
            replayUi.openedAt = now;
            replayUi.leftDown = true;
        };
        const auto togglePlay = [&] {
            if (replayCtl.AtEnd() || replayCtl.TimeMs() >= replayCtl.DurationMs() - 1) {
                replayCtl.Seek(0);
                replayCtl.SetPaused(false);
            } else {
                replayCtl.SetPaused(!replayCtl.Paused());
            }
        };

        const auto keyEdge = [&](int code) {
            const bool down = input.IsKeyDown(code);
            const bool edge = down && !e.keyPrev[code & 255];
            e.keyPrev[code & 255] = down;
            return edge;
        };
        if (inputManager.Pressed(Action::Pause)) {
            if (replayUi.grab)
                replayUi.grab = false;
            else {
                closeEditor();
                return true;
            }
        }
        if (keyEdge(Key::Tab)) {
            replayUi.grab = !replayUi.grab;
            ReplayStatus(replayUi.grab ? "Mouse captured - Tab or Esc to release" : "Mouse released");
        }
        if (keyEdge(Key::Space))
            togglePlay();
        if (keyEdge('J'))
            seekTo(replayCtl.TimeMs() - (shift ? 1000.0 : 5000.0), true);
        if (keyEdge('L'))
            seekTo(replayCtl.TimeMs() + (shift ? 1000.0 : 5000.0), true);
        if (keyEdge(Key::Home))
            seekTo(0, true);
        if (keyEdge(Key::End))
            seekTo(dur, true);
        if (keyEdge('K')) {
            if (shift)
                timeline.RemoveNear(0, replayCtl.TimeMs(), 400), timelineDirty = true;
            else
                addCameraKey();
        }
        if (keyEdge(Key::Delete) || keyEdge(Key::Backspace))
            deleteSelected();
        if (keyEdge('I')) {
            replayUi.inMs = replayCtl.TimeMs();
            ReplayStatus("In point set");
        }
        if (keyEdge('O')) {
            replayUi.outMs = replayCtl.TimeMs();
            ReplayStatus("Out point set");
        }
        if (keyEdge('M')) {
            replayCtl.AddPlaybackMarker("Marker " + std::to_string(replayCtl.Markers().size() + 1),
                                        static_cast<uint32_t>(replayCtl.TimeMs()));
            replayUi.dirty = true;
            ReplayStatus("Marker added at " + ui::FormatReplayTimeMs(replayCtl.TimeMs()));
        }
        if (keyEdge('F'))
            e.viewSpan = 0;

        if (e.drag == 0 && !replayCtl.Paused() && (t > e.viewStart + e.viewSpan || t < e.viewStart))
            e.viewStart = std::clamp(t - e.viewSpan * 0.1, 0.0, std::max(0.0, dur - e.viewSpan));

        if (!replayUi.status.empty() && now > replayUi.statusUntil)
            replayUi.status.clear();
        ui::EditorModel m;
        m.timeMs = t;
        m.durationMs = dur;
        m.viewStartMs = e.viewStart;
        m.viewSpanMs = e.viewSpan;
        m.inMs = replayUi.inMs;
        m.outMs = replayUi.outMs;
        m.paused = replayCtl.Paused();
        m.ended = replayCtl.AtEnd();
        m.catching = replayCtl.CatchingUp();
        m.hideHud = replayUi.hideHud;
        m.cameraLabel = replayCtl.cameraMode == CameraMode::Free     ? "Cam: Free"
                        : replayCtl.cameraMode == CameraMode::Follow ? "Cam: Player"
                                                                     : "Cam: Path";
        m.status = replayUi.status;
        m.title = replayCtl.Meta().name;
        for (const CamKey& k : timeline.camera)
            m.keyMs[0].push_back(k.tMs);
        for (const ValueKey& k : timeline.speed)
            m.keyMs[1].push_back(k.tMs);
        for (const ValueKey& k : timeline.timeOfDay)
            m.keyMs[2].push_back(k.tMs);
        for (const Marker& mk : replayCtl.Markers())
            m.markerMs.push_back(static_cast<double>(mk.timeMs));
        if (!selValid()) {
            e.selTrack = -1;
            e.selIndex = -1;
        }
        m.selTrack = e.selTrack;
        m.selIndex = e.selIndex;
        m.dragKind = e.drag;
        m.sideScroll = e.sideScroll;

        using Row = ui::EditorRow;
        const auto header = [&](const std::string& s) {
            Row r;
            r.kind = Row::Kind::Header;
            r.label = s;
            m.rows.push_back(r);
        };
        const auto info = [&](const std::string& s) {
            Row r;
            r.kind = Row::Kind::Info;
            r.label = s;
            m.rows.push_back(r);
        };
        const auto cycle = [&](const std::string& id, const std::string& label, const std::string& value) {
            Row r;
            r.kind = Row::Kind::Cycle;
            r.id = id;
            r.label = label;
            r.value = value;
            m.rows.push_back(r);
        };
        const auto toggle = [&](const std::string& id, const std::string& label, bool on) {
            Row r;
            r.kind = Row::Kind::Toggle;
            r.id = id;
            r.label = label;
            r.on = on;
            m.rows.push_back(r);
        };
        const auto button = [&](const std::string& id, const std::string& label, bool enabled = true) {
            Row r;
            r.kind = Row::Kind::Button;
            r.id = id;
            r.label = label;
            r.enabled = enabled;
            m.rows.push_back(r);
        };
        static const char* const kTrackTitle[3] = {"Camera key", "Speed key", "Time key"};
        header("Keyframe");
        if (selValid()) {
            info(std::string(kTrackTitle[e.selTrack]) + " " + std::to_string(e.selIndex + 1) + " of " +
                 std::to_string(count(e.selTrack)));
            cycle("s_time", "Time", ui::FormatReplayTimeMs(keyT(e.selTrack, e.selIndex)));
            cycle("s_interp", "Easing", InterpName(interpOf(e.selTrack, e.selIndex)));
            if (e.selTrack == 0) {
                const CamKey& ck = timeline.camera[static_cast<size_t>(e.selIndex)];
                cycle("s_fov", "FOV", Fmt(ck.fov));
                cycle("s_roll", "Roll", Fmt(ck.roll) + " deg");
                button("s_setview", "Set from current view");
                button("s_goview", "Go to this view");
            } else if (e.selTrack == 1) {
                cycle("s_value", "Speed",
                      Fmt(timeline.speed[static_cast<size_t>(e.selIndex)].value, 2) + "x");
            } else {
                cycle("s_value", "Ticks", Fmt(timeline.timeOfDay[static_cast<size_t>(e.selIndex)].value));
            }
            button("s_delete", "Delete key");
        } else {
            info("Click a key to edit it.");
            info("K adds a camera key at");
            info("the playhead.");
            button("s_addspeed", "Add speed key here");
            button("s_addtime", "Add time-of-day key here");
        }
        header("Visuals");
        toggle("v_hud", "Hide HUD", replayUi.hideHud);
        toggle("v_players", "Render players", replayUi.showPlayers);
        toggle("v_entities", "Render entities", replayUi.showEntities);
        toggle("v_nametags", "Render nametags", replayUi.showNametags);
        header("Playback");
        cycle("p_speed", "Speed", Fmt(kSpeedSteps[std::clamp(replayUi.speedIdx, 0, 7)], 2) + "x");
        header("Shortcuts");
        info("Space   play / pause");
        info("K       camera key");
        info("Del     delete key");
        info("I / O   in / out point");
        info("M       marker");
        info("J / L   seek -/+ 5s");
        info("F       fit timeline");
        info("Wheel   zoom timeline");
        info("Tab/RMB mouse look");
        e.sideScroll =
            std::clamp(e.sideScroll, 0, std::max(0, static_cast<int>(m.rows.size()) - lay.sideVisibleRows));
        m.sideScroll = e.sideScroll;

        const ui::EditorBuilt built = ui::BuildEditorRml(m, guiW, guiH);
        if (built.rml != e.signature || !h.Overlay().HasOverlay()) {
            e.signature = built.rml;
            std::string error;
            if (!h.Overlay().SetOverlay(built.rml, error))
                Log("ui", "editor: {}", error);
        }

        float mx = e.drag ? -1.0f : -1.0f;
        float my = -1.0f;
        const bool captured = input.Captured();
        if (!captured) {
            mx = input.MouseX() / static_cast<float>(scale);
            my = input.MouseY() / static_cast<float>(scale);
            h.Overlay().MouseMove(mx, my);
        }
        h.UpdateOverlay(fbW, fbH, options.guiScale);
        const bool inMenu = my >= 0 && my < lay.menuH + 5;
        const bool inDock = my >= lay.dockY;
        const bool inSide = mx >= lay.sideX && my >= lay.sideTop && my < lay.sideBottom;
        if (!captured)
            replayUi.editorOverUi = inMenu || inDock || inSide;
        replayUi.editorViewX0 = 0;
        replayUi.editorViewY0 = (lay.menuH + 5) * static_cast<float>(scale);
        replayUi.editorViewX1 = lay.sideX * static_cast<float>(scale);
        replayUi.editorViewY1 = lay.dockY * static_cast<float>(scale);

        const auto hitAt = [&](float x, float y) -> const ui::EditorHit* {
            for (auto it = built.hits.rbegin(); it != built.hits.rend(); ++it)
                if (x >= it->x && x < it->x + it->w && y >= it->y && y < it->y + it->h)
                    return &*it;
            return nullptr;
        };
        const auto sideAction = [&](const std::string& id) -> bool {
            std::string base = id;
            int dir = 0;
            if (id.size() > 2 && id[id.size() - 2] == ':') {
                dir = id.back() == '+' ? 1 : -1;
                base = id.substr(0, id.size() - 2);
            }
            const bool fine = shift;
            if (base == "s_time" && selValid()) {
                const double nt =
                    std::clamp(keyT(e.selTrack, e.selIndex) + dir * (fine ? 10.0 : 100.0), 0.0, dur);
                setKeyT(e.selTrack, e.selIndex, nt);
                resort(nt);
            } else if (base == "s_interp" && selValid()) {
                setInterp(
                    e.selTrack, e.selIndex,
                    static_cast<Interp>((static_cast<int>(interpOf(e.selTrack, e.selIndex)) + dir + 6) % 6));
                timelineDirty = true;
            } else if (base == "s_fov" && selValid() && e.selTrack == 0) {
                CamKey& ck = timeline.camera[static_cast<size_t>(e.selIndex)];
                ck.fov = std::clamp(ck.fov + static_cast<float>(dir) * (fine ? 1.0f : 5.0f), 5.0f, 170.0f);
                timelineDirty = true;
            } else if (base == "s_roll" && selValid() && e.selTrack == 0) {
                CamKey& ck = timeline.camera[static_cast<size_t>(e.selIndex)];
                ck.roll =
                    std::clamp(ck.roll + static_cast<float>(dir) * (fine ? 1.0f : 5.0f), -180.0f, 180.0f);
                timelineDirty = true;
            } else if (base == "s_value" && selValid() && e.selTrack == 1) {
                float& v = timeline.speed[static_cast<size_t>(e.selIndex)].value;
                int best = 0;
                for (int i = 1; i < 14; i++)
                    if (std::fabs(kValueLadder[i] - v) < std::fabs(kValueLadder[best] - v))
                        best = i;
                v = kValueLadder[std::clamp(best + dir, 0, 13)];
                timelineDirty = true;
            } else if (base == "s_value" && selValid() && e.selTrack == 2) {
                float& v = timeline.timeOfDay[static_cast<size_t>(e.selIndex)].value;
                v = std::fmod(v + static_cast<float>(dir) * (fine ? 100.0f : 1000.0f) + 24000.0f, 24000.0f);
                timelineDirty = true;
            } else if (base == "s_setview" && selValid() && e.selTrack == 0) {
                CamKey& ck = timeline.camera[static_cast<size_t>(e.selIndex)];
                ck.x = camera.x;
                ck.y = camera.y;
                ck.z = camera.z;
                ck.yaw = camera.yaw;
                ck.pitch = camera.pitch;
                timelineDirty = true;
                ReplayStatus("Key updated from the current view");
            } else if (base == "s_goview" && selValid() && e.selTrack == 0) {
                goToView(timeline.camera[static_cast<size_t>(e.selIndex)]);
                if (replayCtl.cameraMode == CameraMode::Keyframes)
                    replayCtl.cameraMode = CameraMode::Free;
            } else if (base == "s_delete") {
                deleteSelected();
            } else if (base == "s_addspeed") {
                timeline.SetSpeed({t, 1.0f, Interp::Linear});
                e.selTrack = 1;
                for (int i = 0; i < count(1); i++)
                    if (std::fabs(keyT(1, i) - t) < 51.0)
                        e.selIndex = i;
                timelineDirty = true;
            } else if (base == "s_addtime") {
                timeline.SetTimeOfDay({t, kfUi.timeTicks, Interp::Linear});
                e.selTrack = 2;
                for (int i = 0; i < count(2); i++)
                    if (std::fabs(keyT(2, i) - t) < 51.0)
                        e.selIndex = i;
                timelineDirty = true;
            } else if (base == "v_hud") {
                replayUi.hideHud = !replayUi.hideHud;
            } else if (base == "v_players") {
                replayUi.showPlayers = !replayUi.showPlayers;
            } else if (base == "v_entities") {
                replayUi.showEntities = !replayUi.showEntities;
            } else if (base == "v_nametags") {
                replayUi.showNametags = !replayUi.showNametags;
            } else if (base == "p_speed") {
                replayUi.speedIdx = std::clamp(replayUi.speedIdx + dir, 0, 7);
                replayCtl.SetSpeed(kSpeedSteps[replayUi.speedIdx]);
            } else {
                return false;
            }
            return true;
        };

        const bool down = input.IsMouseDown(0);
        const bool pressed = down && !e.leftDown && now - e.openedAt > std::chrono::milliseconds(150);
        if (!captured) {
            const float wheel = inputManager.Scroll();
            if (wheel != 0.0f) {
                if (inDock && mx >= lay.tlX0) {
                    if (shift) {
                        e.viewStart = std::clamp(e.viewStart - static_cast<double>(wheel) * e.viewSpan * 0.1,
                                                 0.0, std::max(0.0, dur - e.viewSpan));
                    } else {
                        const double anchor = ui::EditorTimeAtX(lay, m, mx);
                        const double ns = std::clamp(e.viewSpan * std::pow(0.8, static_cast<double>(wheel)),
                                                     std::min(200.0, dur), dur);
                        const double frac = (mx - lay.tlX0) / std::max(1.0f, lay.tlX1 - lay.tlX0);
                        e.viewSpan = ns;
                        e.viewStart = std::clamp(anchor - frac * ns, 0.0, std::max(0.0, dur - ns));
                    }
                } else if (inSide) {
                    e.sideScroll = std::max(0, e.sideScroll - static_cast<int>(std::lround(wheel)));
                }
            }
            if (pressed) {
                const ui::EditorHit* hit = hitAt(mx, my);
                if (hit) {
                    const std::string& id = hit->id;
                    if (id == "m_exit") {
                        closeEditor();
                        e.leftDown = down;
                        return true;
                    } else if (id == "m_export") {
                        ExportOpenMenu();
                        e.leftDown = down;
                        return true;
                    } else if (id == "m_options") {
                        pause.shown = true;
                        pause.view = 1;
                        optionsUi.page = 0;
                        optionsUi.sub = 0;
                        pause.openedAt = now;
                        pause.signature.clear();
                        optionsUi.signature.clear();
                        pause.leftDown = true;
                        h.Overlay().ClearOverlay();
                        e.leftDown = down;
                        return true;
                    } else if (id == "m_camera") {
                        replayCtl.cameraMode =
                            static_cast<CameraMode>((static_cast<int>(replayCtl.cameraMode) + 1) % 3);
                    } else if (id == "m_hud") {
                        replayUi.hideHud = !replayUi.hideHud;
                    } else if (id == "m_marker") {
                        replayCtl.AddPlaybackMarker("Marker " +
                                                        std::to_string(replayCtl.Markers().size() + 1),
                                                    static_cast<uint32_t>(t));
                        replayUi.dirty = true;
                    } else if (id == "t_start")
                        seekTo(0, true);
                    else if (id == "t_back")
                        seekTo(t - 5000.0, true);
                    else if (id == "t_play")
                        togglePlay();
                    else if (id == "t_fwd")
                        seekTo(t + 5000.0, true);
                    else if (id == "t_end")
                        seekTo(dur, true);
                    else if (id == "t_zoomin" || id == "t_zoomout") {
                        const double ns = std::clamp(e.viewSpan * (id == "t_zoomin" ? 0.6 : 1.6),
                                                     std::min(200.0, dur), dur);
                        const double center = e.viewStart + e.viewSpan * 0.5;
                        e.viewSpan = ns;
                        e.viewStart = std::clamp(center - ns * 0.5, 0.0, std::max(0.0, dur - ns));
                    } else if (id == "t_fit")
                        e.viewSpan = 0;
                    else if (id == "t_addkey")
                        addCameraKey();
                    else if (id == "t_delkey")
                        deleteSelected();
                    else if (id == "ruler" || id.starts_with("track")) {
                        e.drag = 1;
                        if (id.starts_with("track")) {
                            e.selTrack = -1;
                            e.selIndex = -1;
                        }
                        seekTo(ui::EditorTimeAtX(lay, m, mx), true);
                    } else if (id.starts_with("key:")) {
                        int tr = 0, idx = 0;
                        std::sscanf(id.c_str(), "key:%d:%d", &tr, &idx);
                        e.selTrack = tr;
                        e.selIndex = idx;
                        if (selValid()) {
                            e.drag = 2;
                            e.dragTrack = tr;
                            e.dragOffset = ui::EditorTimeAtX(lay, m, mx) - keyT(tr, idx);
                            seekTo(keyT(tr, idx), true);
                            if (tr == 0) {
                                const CamKey& ck = timeline.camera[static_cast<size_t>(idx)];
                                kfUi.fov = ck.fov;
                                kfUi.roll = ck.roll;
                                kfUi.interp = static_cast<int>(ck.interp);
                                if (replayCtl.cameraMode != CameraMode::Keyframes)
                                    goToView(ck);
                            }
                        }
                    } else if (id == "thumb") {
                        e.drag = 3;
                        e.dragOffset = ui::EditorTimeAtX(lay, m, mx) - e.viewStart;
                    } else if (id == "scroll") {
                        e.viewStart = std::clamp(ui::EditorTimeAtX(lay, m, mx) - e.viewSpan * 0.5, 0.0,
                                                 std::max(0.0, dur - e.viewSpan));
                    } else
                        sideAction(id);
                    e.signature.clear();
                }
            }
            if (down && e.drag) {
                if (e.drag == 1) {
                    seekTo(ui::EditorTimeAtX(lay, m, mx), false);
                } else if (e.drag == 2 && selValid() && e.selTrack == e.dragTrack) {
                    double nt = ui::EditorTimeAtX(lay, m, mx) - e.dragOffset;
                    nt = std::clamp(shift ? nt : std::round(nt / 10.0) * 10.0, 0.0, dur);
                    if (std::fabs(nt - keyT(e.selTrack, e.selIndex)) > 0.01) {
                        setKeyT(e.selTrack, e.selIndex, nt);
                        resort(nt);
                    }
                } else if (e.drag == 3) {
                    const double target = ui::EditorTimeAtX(lay, m, mx) - e.dragOffset;
                    const double px =
                        (target - e.viewStart) / std::max(1.0, e.viewSpan) * (lay.tlX1 - lay.tlX0);
                    e.viewStart = std::clamp(e.viewStart + px / (lay.tlX1 - lay.tlX0) * dur, 0.0,
                                             std::max(0.0, dur - e.viewSpan));
                }
            }
            if (!down && e.drag) {
                if (e.drag == 1)
                    seekTo(ui::EditorTimeAtX(lay, m, mx), true);
                e.drag = 0;
                e.lastSeekMs = -1e9;
                e.signature.clear();
            }
        } else if (!down) {
            e.drag = 0;
        }
        e.leftDown = down;
        return true;
    }
}
