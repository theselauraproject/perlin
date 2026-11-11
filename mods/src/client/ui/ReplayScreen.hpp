#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace perlin::ui {
    struct ReplayBarModel {
        bool open = false;
        bool paused = false;
        bool ended = false;
        bool catching = false;
        bool hideHud = true;
        bool hudShown = false;
        int cameraMode = 0;
        double timeMs = 0, durationMs = 0;
        double scrubMs = -1;
        double speed = 1.0;
        double inMs = -1, outMs = -1;
        std::string title, status;
        struct Mark {
            double timeMs = 0;
            unsigned color = 0xFFFFFF;
            std::string name;
        };
        std::vector<Mark> markers;
        struct Key {
            double timeMs = 0;
            int track = 0;
        };
        std::vector<Key> keys;
        bool previewKeys = false;
    };
    struct ReplayBarLayout {
        float x = 0, y = 0, w = 0, h = 0;
        float trackX = 0, trackY = 0, trackW = 0, trackH = 0;
    };
    ReplayBarLayout ComputeReplayBarLayout(int guiW, int guiH);
    std::string BuildReplayBarRml(const ReplayBarModel& m, int guiW, int guiH);
    std::string FormatReplayTime(double ms);

    inline constexpr const char* kRbTrack = "rb_track";
    inline constexpr const char* kRbPlay = "rb_play";
    inline constexpr const char* kRbBack = "rb_back";
    inline constexpr const char* kRbFwd = "rb_fwd";
    inline constexpr const char* kRbSlower = "rb_slower";
    inline constexpr const char* kRbFaster = "rb_faster";
    inline constexpr const char* kRbSpeed = "rb_speed";
    inline constexpr const char* kRbCamera = "rb_camera";
    inline constexpr const char* kRbMarker = "rb_marker";
    inline constexpr const char* kRbHud = "rb_hud";
    inline constexpr const char* kRbOptions = "rb_options";
    inline constexpr const char* kRbExport = "rb_export";
    inline constexpr const char* kRbKeyframes = "rb_keyframes";
    inline constexpr const char* kRbExit = "rb_exit";

    struct PanelRow {
        enum class Kind { Header, Cycle, Field, Toggle, Button, Info } kind = Kind::Info;
        std::string id, label, value;
        bool enabled = true, focused = false, on = false;
    };
    struct PanelButton {
        std::string id, label;
        bool enabled = true;
    };
    struct PanelModel {
        std::string title, status;
        std::vector<std::string> tabs;
        int tab = 0;
        std::vector<PanelRow> rows;
        std::vector<PanelButton> buttons;
        int scroll = 0;
        bool caret = false;
        float progress = -1;
        bool exportLayer = false;
    };
    struct PanelLayout {
        float x = 0, y = 0, w = 0, h = 0;
        float rowsTop = 0, rowH = 22;
        int visibleRows = 1;
    };
    PanelLayout ComputePanelLayout(const PanelModel& m, int guiW, int guiH);
    std::string BuildPanelRml(const PanelModel& m, int guiW, int guiH);
    inline constexpr const char* kPanelTabPrefix = "pn_tab";

    struct EditorRow {
        enum class Kind { Header, Info, Cycle, Toggle, Button } kind = Kind::Info;
        std::string id, label, value;
        bool on = false, enabled = true;
    };
    struct EditorModel {
        double timeMs = 0, durationMs = 1, viewStartMs = 0, viewSpanMs = 1, inMs = -1, outMs = -1;
        bool paused = false, ended = false, catching = false;
        std::string cameraLabel, status, title;
        bool hideHud = true;
        std::vector<double> keyMs[3];
        std::vector<double> markerMs;
        int selTrack = -1, selIndex = -1;
        std::vector<EditorRow> rows;
        int sideScroll = 0;
        int dragKind = 0;
    };
    struct EditorLayout {
        float guiW = 0, guiH = 0;
        float menuH = 16, dockY = 0, dockH = 126;
        float sideX = 0, sideW = 0, sideTop = 0, sideBottom = 0, rowH = 16;
        float labelW = 64, tlX0 = 0, tlX1 = 0;
        float transportY = 0, rulerY = 0, rulerH = 14, tracksY = 0, trackH = 18, scrollY = 0, scrollH = 6;
        int sideVisibleRows = 1;
    };
    struct EditorHit {
        std::string id;
        float x = 0, y = 0, w = 0, h = 0;
    };
    struct EditorBuilt {
        std::string rml;
        std::vector<EditorHit> hits;
    };
    EditorLayout ComputeEditorLayout(int guiW, int guiH);
    EditorBuilt BuildEditorRml(const EditorModel& m, int guiW, int guiH);
    std::string FormatReplayTimeMs(double ms);
    inline double EditorTimeAtX(const EditorLayout& l, const EditorModel& m, float x) {
        return m.viewStartMs +
               static_cast<double>((x - l.tlX0) / std::max(1.0f, l.tlX1 - l.tlX0)) * m.viewSpanMs;
    }
    inline float EditorXAtTime(const EditorLayout& l, const EditorModel& m, double t) {
        return l.tlX0 +
               static_cast<float>((t - m.viewStartMs) / std::max(1.0, m.viewSpanMs)) * (l.tlX1 - l.tlX0);
    }
}
