#include "client/ui/ReplayScreen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "client/ui/RmlEmit.hpp"
#include "client/ui/TextEdit.hpp"
#include "client/ui/UiTemplate.hpp"

namespace perlin::ui {
    namespace {
        std::string N(int v) {
            return std::to_string(v);
        }
        std::string F(float v) {
            return std::to_string(static_cast<int>(std::lround(v)));
        }

        std::string Hex(unsigned rgb) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "#%06x", rgb & 0xFFFFFFu);
            return buf;
        }

        int TextWidth(const std::string& text) {
            int n = 0;
            for (unsigned char c : text) {
                if ((c & 0xC0) == 0x80)
                    continue;
                if (c == 'i' || c == '!' || c == '.' || c == ',' || c == ':' || c == ';' || c == '|' ||
                    c == '\'')
                    n += 2;
                else if (c == 'l' || c == '`')
                    n += 3;
                else if (c == 'I' || c == 't' || c == '[' || c == ']' || c == ' ')
                    n += 4;
                else if (c == 'f' || c == 'k' || c == '<' || c == '>' || c == '{' || c == '}' || c == '(' ||
                         c == ')' || c == '*' || c == '"')
                    n += 5;
                else
                    n += 6;
            }
            return n;
        }

        const char* kStyle = R"(<rml>
<head>
<title>replay</title>
<style>
body { position: absolute; left: 0px; top: 0px; margin: 0; padding: 0; background: transparent; }
div { display: block; position: absolute; }
.btn { height: 20px; background-color: #000000; z-index: 5; }
.btn div { position: absolute; }
.btn .rim { left: 1px; top: 1px; height: 16px; background-color: #aaaaaa; }
.btn .face { left: 2px; top: 2px; height: 15px; background-color: #6f6f6f; }
.btn .shade { left: 1px; top: 17px; height: 2px; background-color: #565656; }
.btn .edge { top: 1px; width: 1px; height: 16px; background-color: #565656; }
.btn .lbl { font-family: minecraft-shadow; left: 0px; top: 6px; height: 9px; font-size: 8px; line-height: 9px; text-align: center; color: #ffffff; white-space: nowrap; }
.btn:hover { background-color: #ffffff; }
.btn:hover .rim { background-color: #afafaf; }
.btn:hover .face { background-color: #757575; }
.btn:hover .shade, .btn:hover .edge { background-color: #5c5c5c; }
.btn.on .face { background-color: #4c7a2f; }
.btn.on .rim { background-color: #7fb85a; }
.btn.off, .btn.off:hover { background-color: #000000; }
.btn.off .rim, .btn.off .face, .btn.off .shade, .btn.off .edge { background-color: #2c2c2c; }
.btn.off .lbl { color: #a0a0a0; }
.txt { font-family: minecraft-shadow; font-size: 8px; line-height: 9px; color: #ffffff; white-space: nowrap; z-index: 6; }
.gray { color: #aaaaaa; }
.yellow { color: #ffff55; }
.green { color: #55ff55; }
</style>
</head>
<body>
)";
        const ScreenTemplateDefault kReplayDefault("replay_screen.rml", kStyle);

        std::string Button(const std::string& id, float x, float y, float w, const std::string& label,
                           bool on = false, bool off = false) {
            std::string d = "<div id=\"" + id + "\" class=\"btn" + (on ? " on" : "") + (off ? " off" : "") +
                            "\" style=\"left: " + F(x) + "px; top: " + F(y) + "px; width: " + F(w) + "px;\">";
            d += "<div class=\"rim\" style=\"width: " + F(w - 3) +
                 "px;\"></div><div class=\"face\" style=\"width: " + F(w - 4) + "px;\"></div>";
            d += "<div class=\"shade\" style=\"width: " + F(w - 2) +
                 "px;\"></div><div class=\"edge\" style=\"left: " + F(w - 2) + "px;\"></div>";
            d += "<div class=\"lbl\" style=\"width: " + F(w) + "px;\">" + EscapeRml(label) + "</div></div>\n";
            return d;
        }

        std::string Rect(float x, float y, float w, float h, const std::string& color, int z = 6) {
            return "<div style=\"left: " + F(x) + "px; top: " + F(y) + "px; width: " + F(std::max(w, 0.0f)) +
                   "px; height: " + F(std::max(h, 0.0f)) + "px; background-color: " + color +
                   "; z-index: " + N(z) + ";\"></div>\n";
        }

        std::string Text(float x, float y, float w, const std::string& text, const std::string& cls = "",
                         const std::string& align = "left") {
            return "<div class=\"txt " + cls + "\" style=\"left: " + F(x) + "px; top: " + F(y) +
                   "px; width: " + F(w) + "px; height: 9px; text-align: " + align + ";\">" + EscapeRml(text) +
                   "</div>\n";
        }
    }

    std::string FormatReplayTime(double ms) {
        if (ms < 0)
            ms = 0;
        const long long total = static_cast<long long>(ms / 1000.0);
        char buf[48];
        if (total >= 3600)
            std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", total / 3600, (total / 60) % 60, total % 60);
        else
            std::snprintf(buf, sizeof buf, "%lld:%02lld", total / 60, total % 60);
        return buf;
    }

    ReplayBarLayout ComputeReplayBarLayout(int guiW, int guiH) {
        ReplayBarLayout l;
        l.h = 62;
        l.w = static_cast<float>(guiW);
        l.x = 0;
        l.y = static_cast<float>(guiH) - l.h;
        l.trackX = 40;
        l.trackW = static_cast<float>(guiW) - 80;
        l.trackY = l.y + 17;
        l.trackH = 10;
        return l;
    }

    std::string BuildReplayBarRml(const ReplayBarModel& m, int guiW, int guiH) {
        std::string d = ScreenTemplate("replay_screen.rml", kStyle);
        const size_t body = d.find("<body>");
        d.insert(body + 6, "");
        d.replace(body, 6, "<body style=\"width: " + N(guiW) + "px; height: " + N(guiH) + "px;\">");
        const ReplayBarLayout l = ComputeReplayBarLayout(guiW, guiH);
        const std::string shown =
            FormatReplayTime(m.scrubMs >= 0 ? m.scrubMs : m.timeMs) + " / " + FormatReplayTime(m.durationMs);
        if (m.hudShown) {
            d += "</body>\n</rml>\n";
            return d;
        }
        if (!m.open) {
            std::string line = std::string(m.paused ? "Paused  " : "") + shown +
                               (m.catching ? "  (seeking...)" : "") + "   Esc: replay controls";
            if (!m.status.empty())
                line = m.status + "   " + line;
            d += Text(4, static_cast<float>(guiH) - 11, static_cast<float>(guiW) - 8, line, "gray");
            d += "</body>\n</rml>\n";
            return d;
        }
        d += Rect(l.x, l.y, l.w, l.h, "#000000b4", 3);
        d += Rect(l.x, l.y, l.w, 1, "#ffffff55", 4);
        d += Text(6, l.y + 5, l.w / 2, m.title.empty() ? "Replay" : m.title, "yellow");
        d += Text(l.w / 2, l.y + 5, l.w / 2 - 6, m.status.empty() ? shown : m.status + "   " + shown, "",
                  "right");
        d += Rect(l.trackX - 1, l.trackY - 1, l.trackW + 2, l.trackH + 2, "#000000", 5);
        d += Rect(l.trackX, l.trackY, l.trackW, l.trackH, "#3a3a3a", 5);
        const double dur = std::max(1.0, m.durationMs);
        const auto xAt = [&](double t) {
            return l.trackX + static_cast<float>(std::clamp(t / dur, 0.0, 1.0)) * l.trackW;
        };
        if (m.inMs >= 0 || m.outMs >= 0) {
            const float a = xAt(m.inMs >= 0 ? m.inMs : 0.0), b = xAt(m.outMs >= 0 ? m.outMs : dur);
            d += Rect(a, l.trackY, b - a, l.trackH, "#3d5a7a", 5);
        }
        const double cur = m.scrubMs >= 0 ? m.scrubMs : m.timeMs;
        d += Rect(l.trackX, l.trackY, xAt(cur) - l.trackX, l.trackH, "#6b8f4e", 5);
        for (const auto& k : m.keys)
            d += Rect(xAt(k.timeMs) - 2, l.trackY + 3, 5, 5,
                      k.track == 0 ? "#ffd84d" : (k.track == 1 ? "#4dc3ff" : "#ff7a4d"), 7);
        for (const auto& mk : m.markers)
            d += Rect(xAt(mk.timeMs) - 1, l.trackY - 3, 3, l.trackH + 6, Hex(mk.color), 7);
        d += Rect(xAt(cur) - 1, l.trackY - 3, 3, l.trackH + 6, "#ffffff", 8);
        d += Text(4, l.trackY + 1, 34, FormatReplayTime(cur), "gray");
        d += Text(l.trackX + l.trackW + 4, l.trackY + 1, 34, FormatReplayTime(m.durationMs), "gray");
        d += "<div id=\"" + std::string(kRbTrack) + "\" style=\"left: " + F(l.trackX - 3) +
             "px; top: " + F(l.trackY - 5) + "px; width: " + F(l.trackW + 6) +
             "px; height: " + F(l.trackH + 10) + "px; z-index: 9;\"></div>\n";

        struct B {
            const char* id;
            std::string label;
            bool on = false;
        };
        char speedBuf[32];
        if (m.speed >= 1.0 && std::fabs(m.speed - std::round(m.speed)) < 0.001)
            std::snprintf(speedBuf, sizeof speedBuf, "%.0fx", m.speed);
        else
            std::snprintf(speedBuf, sizeof speedBuf, "%gx", m.speed);
        const char* camLabel =
            m.cameraMode == 0 ? "Cam: Free" : (m.cameraMode == 1 ? "Cam: Player" : "Cam: Path");
        const std::vector<B> row = {
            {kRbBack, "-5s"},         {kRbPlay, m.paused || m.ended ? "Play" : "Pause"},
            {kRbFwd, "+5s"},          {kRbSlower, "-"},
            {kRbSpeed, speedBuf},     {kRbFaster, "+"},
            {kRbCamera, camLabel},    {kRbMarker, "Marker"},
            {kRbKeyframes, "Editor"}, {kRbHud, m.hideHud ? "HUD: Off" : "HUD: On", !m.hideHud},
            {kRbExport, "Export"},    {kRbOptions, "Options"},
            {kRbExit, "Exit"},
        };
        std::vector<float> widths;
        float total = 0;
        for (const B& b : row) {
            const float w = static_cast<float>(std::max(20, TextWidth(b.label) + 12));
            widths.push_back(w);
            total += w;
        }
        const float gap = 3;
        total += gap * static_cast<float>(row.size() - 1);
        const float avail = static_cast<float>(guiW) - 8;
        const float scale = total > avail ? (avail - gap * static_cast<float>(row.size() - 1)) /
                                                (total - gap * static_cast<float>(row.size() - 1))
                                          : 1.0f;
        float x = std::max(4.0f, (static_cast<float>(guiW) - std::min(total, avail)) / 2.0f);
        const float y = l.y + 36;
        for (size_t i = 0; i < row.size(); i++) {
            const float w = std::floor(widths[i] * scale);
            d += Button(row[i].id, x, y, w, row[i].label, row[i].on);
            x += w + gap;
        }
        d += "</body>\n</rml>\n";
        return d;
    }

    PanelLayout ComputePanelLayout(const PanelModel& m, int guiW, int guiH) {
        PanelLayout l;
        l.w = static_cast<float>(std::min(guiW - 12, 340));
        l.x = (static_cast<float>(guiW) - l.w) / 2;
        l.y = 4;
        l.h = static_cast<float>(guiH) - 8;
        l.rowsTop = l.y + 14 + (m.tabs.empty() ? 0 : 24);
        const float footer = l.y + l.h - 24 - (m.progress >= 0 ? 12 : 0);
        l.visibleRows = std::max(1, static_cast<int>((footer - 4 - l.rowsTop) / l.rowH));
        return l;
    }

    std::string BuildPanelRml(const PanelModel& m, int guiW, int guiH) {
        std::string d = ScreenTemplate("replay_screen.rml", kStyle);
        const size_t body = d.find("<body>");
        d.replace(body, 6, "<body style=\"width: " + N(guiW) + "px; height: " + N(guiH) + "px;\">");
        const PanelLayout l = ComputePanelLayout(m, guiW, guiH);
        if (m.exportLayer)
            d += "<mclayer layer=\"export\" style=\"display: block; position: absolute; width: 0px; height: "
                 "0px; z-index: -999;\"></mclayer>\n";
        d += Rect(l.x, l.y, l.w, l.h, "#000000d0", 1);
        d += Rect(l.x, l.y, l.w, 1, "#ffffff66", 2);
        d += Rect(l.x, l.y + l.h - 1, l.w, 1, "#ffffff66", 2);
        d += Text(l.x + 6, l.y + 4, l.w - 12, m.title, "yellow");
        if (!m.status.empty())
            d += Text(l.x + 6, l.y + 4, l.w - 12, m.status, "green", "right");
        if (!m.tabs.empty()) {
            const float tw = (l.w - 8) / static_cast<float>(m.tabs.size());
            for (size_t i = 0; i < m.tabs.size(); i++)
                d += Button(std::string(kPanelTabPrefix) + N(static_cast<int>(i)),
                            l.x + 4 + tw * static_cast<float>(i), l.y + 16, tw - 2, m.tabs[i],
                            static_cast<int>(i) == m.tab);
        }
        const float labelW = l.w * 0.38f;
        const float wx = l.x + 6 + labelW;
        const float ww = l.w - 12 - labelW;
        float y = l.rowsTop;
        const int total = static_cast<int>(m.rows.size());
        for (int i = m.scroll; i < total && i < m.scroll + l.visibleRows; i++, y += l.rowH) {
            const PanelRow& r = m.rows[static_cast<size_t>(i)];
            const std::string cls = r.enabled ? "" : "gray";
            switch (r.kind) {
            case PanelRow::Kind::Header:
                d += Text(l.x + 6, y + 8, l.w - 12, r.label, "yellow");
                d += Rect(l.x + 6, y + 19, l.w - 12, 1, "#ffffff33", 2);
                break;
            case PanelRow::Kind::Info:
                d +=
                    Text(l.x + 6, y + 6, l.w - 12, r.label + (r.value.empty() ? "" : ": " + r.value), "gray");
                break;
            case PanelRow::Kind::Cycle:
                d += Text(l.x + 6, y + 6, labelW, r.label, cls);
                d += Button(r.id + ":-", wx, y + 1, 16, "<", false, !r.enabled);
                d += Button(r.id + ":+", wx + 18, y + 1, ww - 36, r.value, false, !r.enabled);
                d += Button(r.id + ":n", wx + ww - 16, y + 1, 16, ">", false, !r.enabled);
                break;
            case PanelRow::Kind::Toggle:
                d += Text(l.x + 6, y + 6, labelW, r.label, cls);
                d += Button(r.id, wx, y + 1, ww, r.on ? "On" : "Off", r.on, !r.enabled);
                break;
            case PanelRow::Kind::Button:
                d += Button(r.id, l.x + 6, y + 1, l.w - 12, r.label, false, !r.enabled);
                break;
            case PanelRow::Kind::Field: {
                d += Text(l.x + 6, y + 6, labelW, r.label, cls);
                d += "<div id=\"" + r.id + "\" style=\"left: " + F(wx) + "px; top: " + F(y + 1) +
                     "px; width: " + F(ww) +
                     "px; height: 20px; background-color: " + (r.focused ? "#ffffff" : "#666666") +
                     "; z-index: 5;\"></div>\n";
                d += Rect(wx + 1, y + 2, ww - 2, 18, "#000000", 6);
                std::string shown = r.value;
                const int maxPx = static_cast<int>(ww) - 8;
                while (!shown.empty() && TextWidth(shown) > maxPx) {
                    size_t cut = 1;
                    while (cut < shown.size() && (static_cast<unsigned char>(shown[cut]) & 0xC0) == 0x80)
                        cut++;
                    shown.erase(0, cut);
                }
                if (r.focused && shown == r.value)
                    d += "<div class=\"txt" + std::string(r.enabled ? "" : " gray") +
                         "\" style=\"left: " + F(wx + 4) + "px; top: " + F(y + 7) +
                         "px; width: " + F(ww - 6) + "px; height: 9px; text-align: left;\">" +
                         TextFieldRml(r.value, m.caret) + "</div>\n";
                else
                    d += Text(wx + 4, y + 7, ww - 6, shown + (r.focused && m.caret ? "_" : ""),
                              r.enabled ? "" : "gray");
                break;
            }
            }
        }
        if (total > l.visibleRows) {
            const float trackH = l.visibleRows * l.rowH;
            const float thumbH =
                std::max(8.0f, trackH * static_cast<float>(l.visibleRows) / static_cast<float>(total));
            const float maxScroll = static_cast<float>(total - l.visibleRows);
            const float thumbY = l.rowsTop + (trackH - thumbH) * (static_cast<float>(m.scroll) / maxScroll);
            d += Rect(l.x + l.w - 4, l.rowsTop, 2, trackH, "#333333", 3);
            d += Rect(l.x + l.w - 4, thumbY, 2, thumbH, "#aaaaaa", 4);
        }
        float by = l.y + l.h - 24;
        if (m.progress >= 0) {
            d += Rect(l.x + 6, by - 10, l.w - 12, 6, "#333333", 3);
            d += Rect(l.x + 6, by - 10, (l.w - 12) * std::min(1.0f, m.progress), 6, "#55ff55", 4);
        }
        if (!m.buttons.empty()) {
            const float gap = 4;
            const float bw = (l.w - 12 - gap * static_cast<float>(m.buttons.size() - 1)) /
                             static_cast<float>(m.buttons.size());
            for (size_t i = 0; i < m.buttons.size(); i++)
                d += Button(m.buttons[i].id, l.x + 6 + (bw + gap) * static_cast<float>(i), by, bw,
                            m.buttons[i].label, false, !m.buttons[i].enabled);
        }
        if (m.exportLayer)
            d += "<mclayer layer=\"gui\" style=\"display: block; position: absolute; width: 0px; height: "
                 "0px; z-index: 999;\"></mclayer>\n";
        d += "</body>\n</rml>\n";
        return d;
    }

    std::string FormatReplayTimeMs(double ms) {
        if (ms < 0)
            ms = 0;
        const long long total = static_cast<long long>(ms);
        char buf[48];
        std::snprintf(buf, sizeof buf, "%lld:%02lld.%03lld", total / 60000, (total / 1000) % 60,
                      total % 1000);
        return buf;
    }

    EditorLayout ComputeEditorLayout(int guiW, int guiH) {
        EditorLayout l;
        l.guiW = static_cast<float>(guiW);
        l.guiH = static_cast<float>(guiH);
        l.dockH = 126;
        l.dockY = std::max(l.menuH + 40.0f, l.guiH - l.dockH);
        l.dockH = l.guiH - l.dockY;
        l.sideW = std::clamp(l.guiW * 0.27f, 120.0f, 156.0f);
        l.sideX = l.guiW - l.sideW;
        l.sideTop = l.menuH;
        l.sideBottom = l.dockY;
        l.tlX0 = l.labelW;
        l.tlX1 = l.guiW - 6;
        l.transportY = l.dockY + 3;
        l.rulerY = l.dockY + 27;
        l.tracksY = l.rulerY + l.rulerH + 1;
        l.scrollY = l.tracksY + 4 * l.trackH + 3;
        l.sideVisibleRows = std::max(1, static_cast<int>((l.sideBottom - l.sideTop - 18) / l.rowH));
        return l;
    }

    EditorBuilt BuildEditorRml(const EditorModel& m, int guiW, int guiH) {
        EditorBuilt out;
        std::string& d = out.rml;
        d = ScreenTemplate("replay_screen.rml", kStyle);
        const size_t body = d.find("<body>");
        d.replace(body, 6, "<body style=\"width: " + N(guiW) + "px; height: " + N(guiH) + "px;\">");
        const EditorLayout l = ComputeEditorLayout(guiW, guiH);
        const auto hit = [&](const std::string& id, float x, float y, float w, float h) {
            out.hits.push_back({id, x, y, w, h});
        };
        const auto button = [&](const std::string& id, float x, float y, float w, const std::string& label,
                                bool on = false, bool off = false) {
            d += Button(id, x, y, w, label, on, off);
            if (!off)
                hit(id, x, y, w, 20);
        };
        const float panelBg = 0;
        (void)panelBg;

        d += Rect(0, 0, l.guiW, l.menuH + 4, "#0d0f14ee", 2);
        d += Rect(0, l.menuH + 4, l.guiW, 1, "#ffffff22", 3);
        {
            struct B {
                const char* id;
                std::string label;
            };
            const std::vector<B> bar = {{"m_exit", "Close editor"},
                                        {"m_export", "Export"},
                                        {"m_options", "Options"},
                                        {"m_camera", m.cameraLabel},
                                        {"m_hud", m.hideHud ? "HUD: Hidden" : "HUD: Shown"},
                                        {"m_marker", "Add marker"}};
            float x = 3;
            for (const B& b : bar) {
                const float w = static_cast<float>(std::max(24, TextWidth(b.label) + 12));
                if (x + w > l.guiW - 80)
                    break;
                button(b.id, x, -3, w, b.label);
                x += w + 3;
            }
            d += Text(x + 4, 5, l.guiW - x - 8, m.status.empty() ? m.title : m.status,
                      m.status.empty() ? "gray" : "yellow", "right");
        }

        d += Rect(l.sideX, l.sideTop + 4, l.sideW, l.sideBottom - l.sideTop - 4, "#101218ee", 2);
        d += Rect(l.sideX, l.sideTop + 4, 1, l.sideBottom - l.sideTop - 4, "#ffffff22", 3);
        {
            float y = l.sideTop + 8;
            const float lx = l.sideX + 6, rw = l.sideW - 12;
            const int total = static_cast<int>(m.rows.size());
            for (int i = m.sideScroll; i < total && i < m.sideScroll + l.sideVisibleRows; i++, y += l.rowH) {
                const EditorRow& r = m.rows[static_cast<size_t>(i)];
                const std::string cls = r.enabled ? "" : "gray";
                switch (r.kind) {
                case EditorRow::Kind::Header:
                    d += Text(lx, y + 4, rw, r.label, "yellow");
                    d += Rect(lx, y + 14, rw, 1, "#ffffff33", 3);
                    break;
                case EditorRow::Kind::Info:
                    d += Text(lx, y + 4, rw, r.label, "gray");
                    break;
                case EditorRow::Kind::Cycle: {
                    const float labelW = rw * 0.36f;
                    d += Text(lx, y + 4, labelW, r.label, cls);
                    const float bx = lx + labelW;
                    const float vw = rw - labelW - 36;
                    d += Button(r.id + ":-", bx, y - 2, 16, "<", false, !r.enabled);
                    d += Text(bx + 18, y + 4, vw, r.value, cls, "center");
                    d += Button(r.id + ":+", bx + 18 + vw + 2, y - 2, 16, ">", false, !r.enabled);
                    if (r.enabled) {
                        hit(r.id + ":-", bx, y - 2, 16, 20);
                        hit(r.id + ":+", bx + 18 + vw + 2, y - 2, 16, 20);
                    }
                    break;
                }
                case EditorRow::Kind::Toggle: {
                    d += Rect(lx, y + 2, 11, 11, "#000000", 4);
                    d += Rect(lx + 1, y + 3, 9, 9, "#3a3d48", 5);
                    if (r.on)
                        d += Rect(lx + 3, y + 5, 5, 5, "#ffffff", 6);
                    d += Text(lx + 16, y + 4, rw - 16, r.label, cls);
                    if (r.enabled)
                        hit(r.id, lx, y, rw, l.rowH);
                    break;
                }
                case EditorRow::Kind::Button:
                    d += Button(r.id, lx, y - 2, rw, r.label, false, !r.enabled);
                    if (r.enabled)
                        hit(r.id, lx, y - 2, rw, 20);
                    break;
                }
            }
            if (total > l.sideVisibleRows) {
                const float trackH = l.sideVisibleRows * l.rowH;
                const float thumbH = std::max(8.0f, trackH * static_cast<float>(l.sideVisibleRows) /
                                                        static_cast<float>(total));
                const float maxScroll = static_cast<float>(total - l.sideVisibleRows);
                const float thumbY =
                    l.sideTop + 8 + (trackH - thumbH) * (static_cast<float>(m.sideScroll) / maxScroll);
                d += Rect(l.guiW - 3, l.sideTop + 8, 2, trackH, "#333333", 3);
                d += Rect(l.guiW - 3, thumbY, 2, thumbH, "#aaaaaa", 4);
            }
        }

        d += Rect(0, l.dockY, l.guiW, l.dockH, "#0d0f14f2", 2);
        d += Rect(0, l.dockY, l.guiW, 1, "#ffffff33", 3);
        {
            float x = 4;
            const float limit = l.guiW - 6 -
                                static_cast<float>(TextWidth("Fit") + 12 + TextWidth("Zoom +") + 12 +
                                                   TextWidth("Zoom -") + 12 + 6);
            const auto tb = [&](const char* id, const std::string& label, float w) {
                if (x + w > limit)
                    return;
                button(id, x, l.transportY - 2, w, label);
                x += w + 2;
            };
            tb("t_start", "|<", 22);
            tb("t_back", "<<", 22);
            tb("t_play", m.paused || m.ended ? "Play" : "Pause", 38);
            tb("t_fwd", ">>", 22);
            tb("t_end", ">|", 22);
            const std::string shown = FormatReplayTimeMs(m.timeMs) + " / " + FormatReplayTime(m.durationMs) +
                                      (m.catching ? "  (seeking...)" : "");
            if (x + 6 + static_cast<float>(TextWidth(shown)) < limit) {
                d += Text(x + 6, l.transportY + 4, 170, shown);
                x += 6 + static_cast<float>(TextWidth(shown)) + 12;
            }
            tb("t_addkey", "+ Camera key", static_cast<float>(TextWidth("+ Camera key") + 12));
            tb("t_delkey", "Delete key", static_cast<float>(TextWidth("Delete key") + 12));
            float rx = l.guiW - 6;
            const auto rb = [&](const char* id, const std::string& label) {
                const float w = static_cast<float>(std::max(20, TextWidth(label) + 12));
                rx -= w;
                button(id, rx, l.transportY - 2, w, label);
                rx -= 2;
            };
            rb("t_fit", "Fit");
            rb("t_zoomin", "Zoom +");
            rb("t_zoomout", "Zoom -");
        }

        const float tlW = l.tlX1 - l.tlX0;
        const auto xAt = [&](double t) { return EditorXAtTime(l, m, t); };
        const auto visible = [&](float x) { return x >= l.tlX0 - 1 && x <= l.tlX1 + 1; };
        d += Rect(l.tlX0, l.rulerY, tlW, l.rulerH, "#181b24", 3);
        {
            static const double steps[] = {50,    100,   200,   500,    1000,   2000,   5000,    10000,
                                           15000, 30000, 60000, 120000, 300000, 600000, 1800000, 3600000};
            const double pxPerMs = tlW / std::max(1.0, m.viewSpanMs);
            double step = steps[15];
            for (double s : steps)
                if (s * pxPerMs >= 52) {
                    step = s;
                    break;
                }
            const double first = std::floor(m.viewStartMs / step) * step;
            for (double t = first; t <= m.viewStartMs + m.viewSpanMs + step; t += step) {
                const float x = xAt(t);
                if (visible(x)) {
                    d += Rect(x, l.rulerY + 6, 1, l.rulerH - 6, "#9a9ca6", 4);
                    d += Text(x + 3, l.rulerY + 1, 60,
                              step >= 1000 ? FormatReplayTime(t) : FormatReplayTimeMs(t), "gray");
                }
                for (int i = 1; i < 5; i++) {
                    const float mx = xAt(t + step * i / 5.0);
                    if (step * pxPerMs / 5.0 >= 5.0 && visible(mx))
                        d += Rect(mx, l.rulerY + 10, 1, l.rulerH - 10, "#55575f", 4);
                }
            }
        }
        static const char* const kTrackName[4] = {"Camera", "Speed", "Time of day", "Markers"};
        static const char* const kTrackColor[4] = {"#ffd84d", "#4dc3ff", "#ff7a4d", "#7dff7d"};
        static const char* const kClipColor[3] = {"#5a4a1a", "#1a4a5a", "#5a2a1a"};
        for (int tr = 0; tr < 4; tr++) {
            const float y = l.tracksY + static_cast<float>(tr) * l.trackH;
            d += Rect(0, y, l.guiW, l.trackH - 1, tr % 2 ? "#14161d" : "#191c25", 3);
            d += Text(6, y + 5, l.labelW - 8, kTrackName[tr], "gray");
            d += Rect(l.tlX0 - 1, y, 1, l.trackH - 1, "#ffffff22", 4);
            hit("track" + N(tr), l.tlX0, y, tlW, l.trackH - 1);
        }
        hit("ruler", l.tlX0, l.rulerY, tlW, l.rulerH);
        if (m.inMs >= 0 || m.outMs >= 0) {
            const float a = std::clamp(xAt(m.inMs >= 0 ? m.inMs : 0.0), l.tlX0, l.tlX1);
            const float b = std::clamp(xAt(m.outMs >= 0 ? m.outMs : m.durationMs), l.tlX0, l.tlX1);
            if (b > a)
                d += Rect(a, l.rulerY, b - a, l.rulerH + 4 * l.trackH, "#3d5a7a40", 4);
        }
        for (int tr = 0; tr < 3; tr++) {
            const auto& keys = m.keyMs[tr];
            const float y = l.tracksY + static_cast<float>(tr) * l.trackH;
            for (size_t i = 0; i + 1 < keys.size(); i++) {
                const float a = std::clamp(xAt(keys[i]), l.tlX0, l.tlX1),
                            b = std::clamp(xAt(keys[i + 1]), l.tlX0, l.tlX1);
                if (b > a)
                    d += Rect(a, y + 6, b - a, 5, kClipColor[tr], 5);
            }
            for (size_t i = 0; i < keys.size(); i++) {
                const float x = xAt(keys[i]);
                if (!visible(x))
                    continue;
                const bool sel = m.selTrack == tr && m.selIndex == static_cast<int>(i);
                if (sel)
                    d += Rect(x - 4, y + 3, 9, 11, "#ffffff", 6);
                d += Rect(x - 3, y + 4, 7, 9, "#000000", 7);
                d += Rect(x - 2, y + 5, 5, 7, kTrackColor[tr], 8);
                hit("key:" + N(tr) + ":" + N(static_cast<int>(i)), x - 5, y, 11, l.trackH - 1);
            }
        }
        {
            const float y = l.tracksY + 3 * l.trackH;
            for (double t : m.markerMs) {
                const float x = xAt(t);
                if (!visible(x))
                    continue;
                d += Rect(x - 1, y + 2, 3, 13, kTrackColor[3], 7);
            }
        }
        {
            const float x = xAt(m.timeMs);
            if (visible(x)) {
                d += Rect(x, l.rulerY, 1, l.rulerH + 4 * l.trackH, "#ffffff", 9);
                d += Rect(x - 3, l.rulerY, 7, 5, "#ffffff", 9);
            }
        }
        {
            const double dur = std::max(1.0, m.durationMs);
            d += Rect(l.tlX0, l.scrollY, tlW, l.scrollH, "#1d2029", 3);
            const float tw = std::max(14.0f, tlW * static_cast<float>(std::min(1.0, m.viewSpanMs / dur)));
            const float tx =
                l.tlX0 + (tlW - tw) * static_cast<float>(std::clamp(
                                          m.viewStartMs / std::max(1.0, dur - m.viewSpanMs), 0.0, 1.0));
            d += Rect(tx, l.scrollY, tw, l.scrollH, m.dragKind == 3 ? "#cfd2dc" : "#8a8d99", 4);
            hit("scroll", l.tlX0, l.scrollY - 2, tlW, l.scrollH + 4);
            hit("thumb", tx, l.scrollY - 2, tw, l.scrollH + 4);
        }
        d += "</body>\n</rml>\n";
        return out;
    }
}
