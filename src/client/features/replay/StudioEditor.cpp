#include "client/ui/GuiViewport.hpp"
#include "client/features/replay/Studio.hpp"
#include "client/systems/bindings/Keys.hpp"
#include "client/systems/replay/Timeline.hpp"
#include "client/systems/replay/TimelineEditor.hpp"
#include "client/ui/ReplayControls.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include "util/Log.hpp"

namespace perlin::replay {
    using replay::kCameraTrack;
    using replay::kSpeedTrack;
    using replay::kTimeOfDayTrack;

    namespace {
        using Clock = std::chrono::steady_clock;
        using Row = ui::EditorRow;

        std::string Fmt(double v, int decimals = 0) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
            return buf;
        }

        constexpr double kSpeedSteps[] = {0.1, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0};
        constexpr int kSpeedStepCount = static_cast<int>(std::size(kSpeedSteps));
        constexpr const char* kTrackTitle[replay::kTrackCount] = {"Camera key", "Speed key", "Time key"};
        constexpr double kSeekDragMinMs = 40.0;
        constexpr auto kSeekDragMinInterval = std::chrono::milliseconds(70);
        constexpr auto kClickGrace = std::chrono::milliseconds(150);
        constexpr double kJumpMs = 5000.0, kFineJumpMs = 1000.0;
        constexpr double kRemoveKeyToleranceMs = 400.0;
        constexpr double kKeySnapMs = 10.0;

        class SideRows {
        public:
            explicit SideRows(std::vector<Row>& rows) : rows(rows) {}

            void Header(const std::string& label) { Add(Row::Kind::Header, "", label); }
            void Info(const std::string& label) { Add(Row::Kind::Info, "", label); }
            void Cycle(const std::string& id, const std::string& label, const std::string& value) {
                Row& r = Add(Row::Kind::Cycle, id, label);
                r.value = value;
            }
            void Toggle(const std::string& id, const std::string& label, bool on) {
                Row& r = Add(Row::Kind::Toggle, id, label);
                r.on = on;
            }
            void Button(const std::string& id, const std::string& label, bool enabled = true) {
                Row& r = Add(Row::Kind::Button, id, label);
                r.enabled = enabled;
            }

        private:
            Row& Add(Row::Kind kind, const std::string& id, const std::string& label) {
                Row& r = rows.emplace_back();
                r.kind = kind;
                r.id = id;
                r.label = label;
                return r;
            }
            std::vector<Row>& rows;
        };

        struct SideVisuals {
            bool hideHud, players, entities, nametags;
            int speedIdx;
        };

        void AddKeyframeRows(SideRows& rows, const replay::Timeline& tl, const replay::TimelineEditor& ed) {
            rows.Header("Keyframe");
            if (!ed.Valid()) {
                rows.Info("Click a key to edit it.");
                rows.Info("K adds a camera key at");
                rows.Info("the playhead.");
                rows.Button("s_addspeed", "Add speed key here");
                rows.Button("s_addtime", "Add time-of-day key here");
                return;
            }
            const auto [track, index] = ed.Selection();
            rows.Info(std::string(kTrackTitle[track]) + " " + std::to_string(index + 1) + " of " +
                      std::to_string(ed.Count(track)));
            rows.Cycle("s_time", "Time", ui::FormatReplayTimeMs(ed.KeyTime(track, index)));
            rows.Cycle("s_interp", "Easing", replay::InterpName(ed.InterpOf(track, index)));
            if (track == kCameraTrack) {
                const replay::CamKey& ck = tl.camera[static_cast<size_t>(index)];
                rows.Cycle("s_fov", "FOV", Fmt(ck.fov));
                rows.Cycle("s_roll", "Roll", Fmt(ck.roll) + " deg");
                rows.Button("s_setview", "Set from current view");
                rows.Button("s_goview", "Go to this view");
            } else if (track == kSpeedTrack) {
                rows.Cycle("s_value", "Speed", Fmt(tl.speed[static_cast<size_t>(index)].value, 2) + "x");
            } else {
                rows.Cycle("s_value", "Ticks", Fmt(tl.timeOfDay[static_cast<size_t>(index)].value));
            }
            rows.Button("s_delete", "Delete key");
        }

        void AddVisualRows(SideRows& rows, const SideVisuals& v) {
            rows.Header("Visuals");
            rows.Toggle("v_hud", "Hide HUD", v.hideHud);
            rows.Toggle("v_players", "Render players", v.players);
            rows.Toggle("v_entities", "Render entities", v.entities);
            rows.Toggle("v_nametags", "Render nametags", v.nametags);
            rows.Header("Playback");
            rows.Cycle("p_speed", "Speed",
                       Fmt(kSpeedSteps[std::clamp(v.speedIdx, 0, kSpeedStepCount - 1)], 2) + "x");
        }

        void AddShortcutRows(SideRows& rows) {
            rows.Header("Shortcuts");
            for (const char* line : {"Space   play / pause", "K       camera key", "Del     delete key",
                                     "I / O   in / out point", "M       marker", "J / L   seek -/+ 5s",
                                     "F       fit timeline", "Wheel   zoom timeline", "Tab/RMB mouse look"})
                rows.Info(line);
        }

        struct SideAction {
            std::string base;
            int direction = 0;

            static SideAction Parse(const std::string& id) {
                SideAction a{id, 0};
                if (id.size() > 2 && id[id.size() - 2] == ':') {
                    a.direction = id.back() == '+' ? 1 : -1;
                    a.base = id.substr(0, id.size() - 2);
                }
                return a;
            }
        };

        const char* CameraLabel(replay::CameraMode mode) {
            return mode == replay::CameraMode::Free     ? "Cam: Free"
                   : mode == replay::CameraMode::Follow ? "Cam: Player"
                                                        : "Cam: Path";
        }
    }

    struct Studio::EditorFrame {
        ui::Hud& hud;
        int fbW, fbH;
        Clock::time_point now;
        int scale;
        int guiW, guiH;
        ui::EditorLayout lay;
        double dur;
        double t;
        bool shift;
        ui::EditorModel model;
        ui::EditorBuilt built;
        float mx = -1.0f, my = -1.0f;
        bool captured = false, down = false;
        bool inMenu = false, inDock = false, inSide = false;
    };

    bool Studio::EditorKeyEdge(int code) {
        EditorUi& e = editorUi;
        const bool down = input.IsKeyDown(code);
        const bool edge = down && !e.keyPrev[code & 255];
        e.keyPrev[code & 255] = down;
        return edge;
    }

    void Studio::EditorSeek(const EditorFrame& f, double ms, bool force) {
        EditorUi& e = editorUi;
        ms = std::clamp(ms, 0.0, f.dur);
        if (force ||
            (std::fabs(ms - e.lastSeekMs) > kSeekDragMinMs && f.now - e.lastSeekAt > kSeekDragMinInterval)) {
            replayCtl.Seek(ms);
            e.lastSeekMs = ms;
            e.lastSeekAt = f.now;
        }
    }

    void Studio::EditorTogglePlay() {
        if (replayCtl.AtEnd() || replayCtl.TimeMs() >= replayCtl.DurationMs() - 1) {
            replayCtl.Seek(0);
            replayCtl.SetPaused(false);
        } else {
            replayCtl.SetPaused(!replayCtl.Paused());
        }
    }

    void Studio::EditorGoToView(const replay::CamKey& ck) {
        camera.x = static_cast<float>(ck.x);
        camera.y = static_cast<float>(ck.y);
        camera.z = static_cast<float>(ck.z);
        camera.yaw = ck.yaw;
        camera.pitch = ck.pitch;
    }

    void Studio::EditorClose(const EditorFrame& f) {
        EditorUi& e = editorUi;
        e.open = false;
        e.drag = 0;
        replayUi.editorOverUi = false;
        replayUi.editorViewX0 = replayUi.editorViewY0 = 0;
        replayUi.editorViewX1 = replayUi.editorViewY1 = 1e9f;
        f.hud.Overlay().ClearOverlay();
        replayUi.signature.clear();
        replayUi.openedAt = f.now;
        replayUi.leftDown = true;
    }

    void Studio::EditorAddCameraKey() {
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
        replay::TimelineEditor(timeline, editorUi.sel).AddCamera(ck);
        timelineDirty = true;
        if (timeline.camera.size() >= 2 && replayCtl.cameraMode != replay::CameraMode::Keyframes)
            replayCtl.cameraMode = replay::CameraMode::Keyframes;
        SetStatus("Camera key " + std::to_string(timeline.camera.size()) + " at " +
                     ui::FormatReplayTimeMs(ck.tMs));
    }

    void Studio::EditorAddMarker(double tMs, bool announce) {
        replayCtl.AddPlaybackMarker("Marker " + std::to_string(replayCtl.Markers().size() + 1),
                                    static_cast<uint32_t>(tMs));
        replayUi.dirty = true;
        if (announce)
            SetStatus("Marker added at " + ui::FormatReplayTimeMs(tMs));
    }

    bool Studio::EditorShortcuts(const EditorFrame& f) {
        EditorUi& e = editorUi;
        replay::TimelineEditor keys(timeline, e.sel);
        if (inputManager.Pressed(Action::Pause)) {
            if (!replayUi.grab) {
                EditorClose(f);
                return true;
            }
            replayUi.grab = false;
        }
        if (EditorKeyEdge(Key::Tab)) {
            replayUi.grab = !replayUi.grab;
            SetStatus(replayUi.grab ? "Mouse captured - Tab or Esc to release" : "Mouse released");
        }
        if (EditorKeyEdge(Key::Space))
            EditorTogglePlay();
        const double jump = f.shift ? kFineJumpMs : kJumpMs;
        if (EditorKeyEdge('J'))
            EditorSeek(f, replayCtl.TimeMs() - jump, true);
        if (EditorKeyEdge('L'))
            EditorSeek(f, replayCtl.TimeMs() + jump, true);
        if (EditorKeyEdge(Key::Home))
            EditorSeek(f, 0, true);
        if (EditorKeyEdge(Key::End))
            EditorSeek(f, f.dur, true);
        if (EditorKeyEdge('K')) {
            if (f.shift) {
                timeline.RemoveNear(kCameraTrack, replayCtl.TimeMs(), kRemoveKeyToleranceMs);
                timelineDirty = true;
            } else {
                EditorAddCameraKey();
            }
        }
        if (EditorKeyEdge(Key::Delete) || EditorKeyEdge(Key::Backspace))
            timelineDirty |= keys.DeleteSelected();
        if (EditorKeyEdge('I')) {
            replayUi.inMs = replayCtl.TimeMs();
            SetStatus("In point set");
        }
        if (EditorKeyEdge('O')) {
            replayUi.outMs = replayCtl.TimeMs();
            SetStatus("Out point set");
        }
        if (EditorKeyEdge('M'))
            EditorAddMarker(replayCtl.TimeMs(), true);
        if (EditorKeyEdge('F'))
            e.view.Fit();
        return false;
    }

    void Studio::EditorFillModel(EditorFrame& f) {
        EditorUi& e = editorUi;
        ui::EditorModel& m = f.model;
        const replay::TimelineEditor keys(timeline, e.sel);
        m.timeMs = f.t;
        m.durationMs = f.dur;
        m.viewStartMs = e.view.start;
        m.viewSpanMs = e.view.span;
        m.inMs = replayUi.inMs;
        m.outMs = replayUi.outMs;
        m.paused = replayCtl.Paused();
        m.ended = replayCtl.AtEnd();
        m.catching = replayCtl.CatchingUp();
        m.hideHud = replayUi.hideHud;
        m.cameraLabel = CameraLabel(replayCtl.cameraMode);
        m.status = replayUi.status;
        m.title = replayCtl.Meta().name;
        for (const replay::CamKey& k : timeline.camera)
            m.keyMs[kCameraTrack].push_back(k.tMs);
        for (const replay::ValueKey& k : timeline.speed)
            m.keyMs[kSpeedTrack].push_back(k.tMs);
        for (const replay::ValueKey& k : timeline.timeOfDay)
            m.keyMs[kTimeOfDayTrack].push_back(k.tMs);
        for (const replay::Marker& mk : replayCtl.Markers())
            m.markerMs.push_back(static_cast<double>(mk.timeMs));
        m.selTrack = e.sel.track;
        m.selIndex = e.sel.index;
        m.dragKind = e.drag;

        SideRows rows(m.rows);
        AddKeyframeRows(rows, timeline, keys);
        AddVisualRows(rows, {replayUi.hideHud, replayUi.showPlayers, replayUi.showEntities,
                             replayUi.showNametags, replayUi.speedIdx});
        AddShortcutRows(rows);
        e.sideScroll =
            std::clamp(e.sideScroll, 0, std::max(0, static_cast<int>(m.rows.size()) - f.lay.sideVisibleRows));
        m.sideScroll = e.sideScroll;
    }

    void Studio::EditorSyncPointer(EditorFrame& f) {
        const bool captured = input.Captured();
        f.captured = captured;
        if (!captured) {
            f.mx = input.MouseX() / static_cast<float>(f.scale);
            f.my = input.MouseY() / static_cast<float>(f.scale);
            f.hud.Overlay().MouseMove(f.mx, f.my);
        }
        f.hud.UpdateOverlay(f.fbW, f.fbH, options.guiScale);
        const ui::EditorLayout& lay = f.lay;
        f.inMenu = f.my >= 0 && f.my < lay.menuH + 5;
        f.inDock = f.my >= lay.dockY;
        f.inSide = f.mx >= lay.sideX && f.my >= lay.sideTop && f.my < lay.sideBottom;
        if (!captured)
            replayUi.editorOverUi = f.inMenu || f.inDock || f.inSide;
        const float scale = static_cast<float>(f.scale);
        replayUi.editorViewX0 = 0;
        replayUi.editorViewY0 = (lay.menuH + 5) * scale;
        replayUi.editorViewX1 = lay.sideX * scale;
        replayUi.editorViewY1 = lay.dockY * scale;
    }

    void Studio::EditorSideAction(const EditorFrame& f, const std::string& id) {
        EditorUi& e = editorUi;
        replay::TimelineEditor keys(timeline, e.sel);
        const auto [base, dir] = SideAction::Parse(id);
        const bool fine = f.shift;
        const bool edited = [&] {
            if (base == "s_time")
                return keys.NudgeTime(dir, fine, f.dur);
            if (base == "s_interp")
                return keys.CycleInterp(dir);
            if (base == "s_fov")
                return keys.NudgeFov(dir, fine);
            if (base == "s_roll")
                return keys.NudgeRoll(dir, fine);
            if (base == "s_value")
                return keys.StepSpeed(dir) || keys.NudgeTimeOfDay(dir, fine);
            if (base == "s_setview") {
                const bool done = keys.SetPose(camera.x, camera.y, camera.z, camera.yaw, camera.pitch);
                if (done)
                    SetStatus("Key updated from the current view");
                return done;
            }
            if (base == "s_delete")
                return keys.DeleteSelected();
            if (base == "s_addspeed") {
                keys.AddSpeed(f.t, 1.0f);
                return true;
            }
            if (base == "s_addtime") {
                keys.AddTimeOfDay(f.t, kfUi.timeTicks);
                return true;
            }
            return false;
        }();
        timelineDirty |= edited;
        if (base == "s_goview") {
            if (const replay::CamKey* ck = keys.SelectedCamera()) {
                EditorGoToView(*ck);
                if (replayCtl.cameraMode == replay::CameraMode::Keyframes)
                    replayCtl.cameraMode = replay::CameraMode::Free;
            }
        } else if (base == "v_hud") {
            replayUi.hideHud = !replayUi.hideHud;
        } else if (base == "v_players") {
            replayUi.showPlayers = !replayUi.showPlayers;
        } else if (base == "v_entities") {
            replayUi.showEntities = !replayUi.showEntities;
        } else if (base == "v_nametags") {
            replayUi.showNametags = !replayUi.showNametags;
        } else if (base == "p_speed") {
            replayUi.speedIdx = std::clamp(replayUi.speedIdx + dir, 0, kSpeedStepCount - 1);
            replayCtl.SetSpeed(kSpeedSteps[replayUi.speedIdx]);
        }
    }

    void Studio::EditorOpenOptions(const EditorFrame& f) {
        host.OpenPauseOptions(f.now);
        f.hud.Overlay().ClearOverlay();
    }

    bool Studio::EditorMenuHit(const EditorFrame& f, const std::string& id) {
        if (id == "m_exit") {
            EditorClose(f);
            return true;
        }
        if (id == "m_export") {
            ExportOpenMenu();
            return true;
        }
        if (id == "m_options") {
            EditorOpenOptions(f);
            return true;
        }
        if (id == "m_camera")
            replayCtl.cameraMode =
                static_cast<replay::CameraMode>((static_cast<int>(replayCtl.cameraMode) + 1) % 3);
        else if (id == "m_hud")
            replayUi.hideHud = !replayUi.hideHud;
        else if (id == "m_marker")
            EditorAddMarker(f.t, false);
        return false;
    }

    bool Studio::EditorTransportHit(const EditorFrame& f, const std::string& id) {
        EditorUi& e = editorUi;
        if (id == "t_start")
            EditorSeek(f, 0, true);
        else if (id == "t_back")
            EditorSeek(f, f.t - kJumpMs, true);
        else if (id == "t_play")
            EditorTogglePlay();
        else if (id == "t_fwd")
            EditorSeek(f, f.t + kJumpMs, true);
        else if (id == "t_end")
            EditorSeek(f, f.dur, true);
        else if (id == "t_zoomin" || id == "t_zoomout")
            e.view.ZoomAroundCentre(id == "t_zoomin" ? 0.6 : 1.6, f.dur);
        else if (id == "t_fit")
            e.view.Fit();
        else if (id == "t_addkey")
            EditorAddCameraKey();
        else if (id == "t_delkey")
            timelineDirty |= replay::TimelineEditor(timeline, e.sel).DeleteSelected();
        else
            return false;
        return true;
    }

    bool Studio::EditorTimelineHit(const EditorFrame& f, const std::string& id) {
        EditorUi& e = editorUi;
        replay::TimelineEditor keys(timeline, e.sel);
        if (id == "ruler" || id.starts_with("track")) {
            e.drag = 1;
            if (id.starts_with("track"))
                keys.Deselect();
            EditorSeek(f, ui::EditorTimeAtX(f.lay, f.model, f.mx), true);
        } else if (id.starts_with("key:")) {
            int track = 0, index = 0;
            std::sscanf(id.c_str(), "key:%d:%d", &track, &index);
            keys.Select(track, index);
            if (keys.Valid()) {
                e.drag = 2;
                e.dragTrack = track;
                e.dragOffset = ui::EditorTimeAtX(f.lay, f.model, f.mx) - keys.KeyTime(track, index);
                EditorSeek(f, keys.KeyTime(track, index), true);
                if (track == kCameraTrack)
                    AdoptCameraKey(timeline.camera[static_cast<size_t>(index)]);
            }
        } else if (id == "thumb") {
            e.drag = 3;
            e.dragOffset = ui::EditorTimeAtX(f.lay, f.model, f.mx) - e.view.start;
        } else if (id == "scroll") {
            e.view.CentreOn(ui::EditorTimeAtX(f.lay, f.model, f.mx), f.dur);
        } else {
            return false;
        }
        return true;
    }

    void Studio::AdoptCameraKey(const replay::CamKey& ck) {
        kfUi.fov = ck.fov;
        kfUi.roll = ck.roll;
        kfUi.interp = static_cast<int>(ck.interp);
        if (replayCtl.cameraMode != replay::CameraMode::Keyframes)
            EditorGoToView(ck);
    }

    bool Studio::EditorPress(const EditorFrame& f, const std::string& id) {
        bool leave = false;
        if (id.starts_with("m_"))
            leave = EditorMenuHit(f, id);
        else if (!EditorTransportHit(f, id) && !EditorTimelineHit(f, id))
            EditorSideAction(f, id);
        if (!leave)
            editorUi.signature.clear();
        return leave;
    }

    void Studio::EditorWheel(const EditorFrame& f) {
        EditorUi& e = editorUi;
        const float wheel = inputManager.Scroll();
        if (wheel == 0.0f)
            return;
        if (f.inDock && f.mx >= f.lay.tlX0) {
            if (f.shift) {
                e.view.Pan(static_cast<double>(wheel), f.dur);
            } else {
                const double anchor = ui::EditorTimeAtX(f.lay, f.model, f.mx);
                const double fraction = (f.mx - f.lay.tlX0) / std::max(1.0f, f.lay.tlX1 - f.lay.tlX0);
                e.view.ZoomAt(anchor, fraction, static_cast<double>(wheel), f.dur);
            }
        } else if (f.inSide) {
            e.sideScroll = std::max(0, e.sideScroll - static_cast<int>(std::lround(wheel)));
        }
    }

    void Studio::EditorDrag(const EditorFrame& f) {
        EditorUi& e = editorUi;
        replay::TimelineEditor keys(timeline, e.sel);
        if (f.down) {
            if (e.drag == 1) {
                EditorSeek(f, ui::EditorTimeAtX(f.lay, f.model, f.mx), false);
            } else if (e.drag == 2 && keys.Valid() && e.sel.track == e.dragTrack) {
                double nt = ui::EditorTimeAtX(f.lay, f.model, f.mx) - e.dragOffset;
                nt = std::clamp(f.shift ? nt : std::round(nt / kKeySnapMs) * kKeySnapMs, 0.0, f.dur);
                if (std::fabs(nt - keys.KeyTime(e.sel.track, e.sel.index)) > 0.01)
                    timelineDirty |= keys.MoveSelectedTo(nt);
            } else if (e.drag == 3) {
                const double target = ui::EditorTimeAtX(f.lay, f.model, f.mx) - e.dragOffset;
                e.view.DragThumb(target, f.lay.tlX1 - f.lay.tlX0, f.dur);
            }
            return;
        }
        if (e.drag == 1)
            EditorSeek(f, ui::EditorTimeAtX(f.lay, f.model, f.mx), true);
        e.drag = 0;
        e.lastSeekMs = -1e9;
        e.signature.clear();
    }

    bool Studio::UpdateEditor(ui::Hud& h, int fbW, int fbH) {
        EditorUi& e = editorUi;
        const auto now = Clock::now();
        const auto [scale, guiW, guiH] = ui::OverlayViewport(h, fbW, fbH);
        EditorFrame f{h,
                      fbW,
                      fbH,
                      now,
                      scale,
                      guiW,
                      guiH,
                      ui::ComputeEditorLayout(guiW, guiH),
                      std::max(1.0, static_cast<double>(replayCtl.DurationMs())),
                      replayCtl.TimeMs(),
                      input.IsKeyDown(Key::Shift),
                      {},
                      {}};

        e.view.Normalize(f.dur);
        if (EditorShortcuts(f))
            return true;
        if (e.drag == 0 && !replayCtl.Paused())
            e.view.Follow(f.t, f.dur);
        if (!replayUi.status.empty() && now > replayUi.statusUntil)
            replayUi.status.clear();

        replay::TimelineEditor(timeline, e.sel).DropInvalidSelection();
        EditorFillModel(f);
        f.built = ui::BuildEditor(f.model, guiW, guiH);
        if (const std::string dump = ui::DumpScene(f.built.doc); dump != e.signature || !h.Overlay().HasOverlay()) {
            e.signature = dump;
            std::string error;
            if (!h.Overlay().SetOverlay(f.built.doc, error))
                Log("ui", "editor: {}", error);
        }

        EditorSyncPointer(f);
        f.down = input.IsMouseDown(0);
        if (f.captured) {
            if (!f.down)
                e.drag = 0;
            e.leftDown = f.down;
            return true;
        }
        EditorWheel(f);
        const bool pressed = f.down && !e.leftDown && now - e.openedAt > kClickGrace;
        if (pressed) {
            const auto hit = std::ranges::find_if(
                f.built.hits.rbegin(), f.built.hits.rend(), [&](const ui::EditorHit& it) {
                    return f.mx >= it.x && f.mx < it.x + it.w && f.my >= it.y && f.my < it.y + it.h;
                });
            if (hit != f.built.hits.rend() && EditorPress(f, hit->id)) {
                e.leftDown = f.down;
                return true;
            }
        }
        if (e.drag)
            EditorDrag(f);
        e.leftDown = f.down;
        return true;
    }
}
