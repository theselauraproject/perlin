#include "client/screen/screens/PauseScreen.hpp"
#include "util/Resources.hpp"
#include <cmath>
#include <algorithm>

#include "client/systems/text/FormatCodes.hpp"
#include "client/ui/SceneKit.hpp"
#include "client/ui/UiTemplate.hpp"
#include "client/ui/TextEdit.hpp"

namespace perlin::ui {
    std::string DefaultPauseScreenTemplate() {
        return ui::ReadMarkup("ui/screens/pause_screen.rml");
    }

    OptionsLayout ComputeOptionsLayout(const std::vector<OptionsRow>& rows, int guiW, int guiH,
                                       float scroll, int columns) {
        columns = std::clamp(columns, 1, 6);
        OptionsLayout l;
        l.count = static_cast<int>(rows.size());
        l.x.assign(rows.size(), 0.0f);
        l.y.assign(rows.size(), 0.0f);
        l.rowW.assign(rows.size(), l.w);
        l.visible.assign(rows.size(), 1);
        const float start = std::max(36.0f, static_cast<float>(guiH) / 6.0f - 12.0f);
        const float cx = static_cast<float>(guiW) / 2.0f;
        float y = start;
        int col = 0;
        float x0 = cx - 155.0f, colW = l.w, stride = 160.0f;
        if (columns == 1) {
            colW = 310.0f;
        } else if (columns > 2) {
            const float total = std::min(static_cast<float>(guiW) - 16.0f, 400.0f);
            colW = std::floor((total - static_cast<float>(columns - 1) * 4.0f) / static_cast<float>(columns));
            stride = colW + 4.0f;
            x0 = cx - total / 2.0f;
        }
        for (size_t i = 0; i < rows.size(); i++) {
            if (rows[i].header) {
                if (col > 0) {
                    y += 24.0f;
                    col = 0;
                }
                l.x[i] = cx - 155.0f;
                l.y[i] = y + 4.0f;
                y += 22.0f;
                continue;
            }
            if (rows[i].tab > 0) {
                if (col > 0) {
                    y += 24.0f;
                    col = 0;
                }
                l.x[i] = cx - 155.0f + static_cast<float>(rows[i].tab - 1) * 105.0f;
                l.rowW[i] = 100.0f;
                l.y[i] = y;
                if (rows[i].tab == 3)
                    y += 28.0f;
                continue;
            }
            if (rows[i].wide) {
                if (col > 0) {
                    y += 24.0f;
                    col = 0;
                }
                l.x[i] = cx - 155.0f;
                l.rowW[i] = 310.0f;
                l.y[i] = y;
                y += 24.0f;
                continue;
            }
            l.x[i] = x0 + static_cast<float>(col) * stride;
            l.rowW[i] = colW;
            l.y[i] = y;
            if (++col >= columns) {
                col = 0;
                y += 24.0f;
            }
        }
        if (col > 0)
            y += 24.0f;
        l.listTop = start;
        l.listBottom = static_cast<float>(guiH) - 32.0f;
        l.contentH = y - start;
        l.scrolling = y + 12.0f + 20.0f > static_cast<float>(guiH) - 6.0f;
        l.maxScroll = l.scrolling ? std::max(0.0f, l.contentH - (l.listBottom - l.listTop)) : 0.0f;
        const float offset = l.scrolling ? std::clamp(scroll, 0.0f, l.maxScroll) : 0.0f;
        for (size_t i = 0; i < rows.size(); i++) {
            l.y[i] -= offset;
            const float height = rows[i].header ? 9.0f : 20.0f;
            if (l.scrolling && (l.y[i] < l.listTop - 1.0f || l.y[i] + height > l.listBottom + 1.0f))
                l.visible[i] = 0;
        }
        l.doneW = 200.0f;
        l.doneX = cx - 100.0f;
        l.doneY = l.scrolling ? static_cast<float>(guiH) - 27.0f
                              : std::min(y + 12.0f, static_cast<float>(guiH) - 27.0f);
        return l;
    }

    namespace {
        const ScreenTemplateDefault kOptionsHeadDefault("ui/screens/options_screen.rml");
    }

    SceneDoc BuildOptionsScreen(const std::vector<OptionsRow>& rows, const std::string& title,
                                const std::string& done, float scroll, int guiW, int guiH, int hover,
                                int columns) {
        const OptionsLayout l = ComputeOptionsLayout(rows, guiW, guiH, scroll, columns);
        SceneDoc doc;
        doc.rml = ScreenTemplate("ui/screens/options_screen.rml");
        doc.scene.vars["title"] = title;
        SceneBuilder b(doc.scene.lists["body"]);
        for (int i = 0; i < l.count; i++) {
            const size_t k = static_cast<size_t>(i);
            const OptionsRow& row = rows[k];
            if (!l.visible[k])
                continue;
            if (row.header) {
                kit::Label(b, 0, static_cast<float>(static_cast<int>(l.y[k])), static_cast<float>(guiW), "hdr",
                           kit::Plain(row.label));
                continue;
            }
            if (row.label.empty() && row.value.empty())
                continue;
            const std::string label = row.value.empty() ? row.label : row.label + ": " + row.value;
            const std::string id = "row" + std::to_string(i);
            if (row.fraction >= 0.0f)
                kit::Slider(b, id, l.x[k], l.y[k], l.rowW[k], kit::Auto(label, row.formatted), row.fraction);
            else
                kit::Button(b, id, l.x[k], l.y[k], l.rowW[k], kit::Auto(label, row.formatted), row.locked);
            if (!row.icon.empty()) {
                const float ix = static_cast<float>(static_cast<int>(l.x[k]) + 3);
                const float iy = static_cast<float>(static_cast<int>(l.y[k]) + 2);
                if (row.icon.starts_with("item:"))
                    kit::Item(b, ix, iy, 16, 16, row.icon.substr(5) + ";0", "9");
                else
                    kit::Image(b, ix, iy, 16, 16, row.icon, "9");
            }
        }
        if (hover >= 0 && hover < l.count && l.visible[static_cast<size_t>(hover)]) {
            const OptionsRow& row = rows[static_cast<size_t>(hover)];
            if (!row.tip.empty() || row.impact > 0) {
                const float tw = 190.0f;
                const int perLine = static_cast<int>((tw - 8.0f) / 5.0f);
                int lines = 0;
                {
                    int col = 0;
                    lines = 1;
                    size_t p = 0;
                    while (p < row.tip.size()) {
                        size_t e = row.tip.find(' ', p);
                        if (e == std::string::npos)
                            e = row.tip.size();
                        const int len = static_cast<int>(e - p);
                        if (col > 0 && col + 1 + len > perLine) {
                            lines++;
                            col = len;
                        } else {
                            col += (col > 0 ? 1 : 0) + len;
                        }
                        p = e + 1;
                    }
                    if (row.tip.empty())
                        lines = 0;
                }
                const float th = static_cast<float>(lines) * 10.0f + (row.impact > 0 ? 12.0f : 0.0f) + 8.0f;
                float tx = std::min(l.x[static_cast<size_t>(hover)] +
                                        l.rowW[static_cast<size_t>(hover)] / 2.0f - tw / 2.0f,
                                    static_cast<float>(guiW) - tw - 4.0f);
                tx = std::max(tx, 4.0f);
                float ty = l.y[static_cast<size_t>(hover)] + l.h + 2.0f;
                if (ty + th > static_cast<float>(guiH) - 2.0f)
                    ty = l.y[static_cast<size_t>(hover)] - th - 2.0f;
                kit::Box(b, tx, ty, tw, th, "tip0", {}, "20");
                kit::Box(b, tx + 1, ty + 1, tw - 2, th - 2, "tip1", {}, "21");
                kit::Box(b, tx + 2, ty + 2, tw - 4, th - 4, "tip0", {}, "22");
                if (lines > 0)
                    kit::Label(b, tx + 4, ty + 4, tw - 8, "tiptx", kit::Plain(row.tip), "23");
                if (row.impact > 0) {
                    static const char* kNames[] = {"", "Low", "Medium", "High"};
                    static const char* kColors[] = {"#ffffff", "#55ff55", "#ffff55", "#ff5555"};
                    const int im = std::clamp(row.impact, 1, 3);
                    std::vector<SceneRun> runs = kit::Plain("Performance Impact: ");
                    for (SceneRun& run : kit::Plain(kNames[im], kColors[im]))
                        runs.push_back(std::move(run));
                    kit::Label(b, tx + 4, ty + 4 + static_cast<float>(lines) * 10.0f + (lines > 0 ? 2.0f : 0.0f),
                               tw - 8, "tipimp", std::move(runs), "23");
                }
            }
        }
        if (l.scrolling && l.maxScroll > 0.0f) {
            const float listH = l.listBottom - l.listTop;
            const float th = std::max(16.0f, listH * listH / l.contentH);
            const float ty = l.listTop + (listH - th) * (std::clamp(scroll, 0.0f, l.maxScroll) / l.maxScroll);
            const float sx = static_cast<float>(guiW / 2 + 160);
            kit::Box(b, sx, std::floor(l.listTop), 6, std::floor(listH), "sbtrack");
            kit::Box(b, sx, std::floor(ty), 6, std::floor(th), "sbthumb");
        }
        kit::Button(b, kOptionsDoneId, l.doneX, l.doneY, l.doneW, kit::Plain(done));
        return doc;
    }

    KeybindLayout ComputeKeybindLayout(int rows, int guiW, int guiH) {
        KeybindLayout l;
        l.listBottom = static_cast<float>(guiH) - 32.0f;
        const float cx = static_cast<float>(guiW) / 2.0f;
        l.labelX = cx - 160.0f;
        l.keyX = cx + 20.0f;
        l.resetX = cx + 100.0f;
        l.doneW = 200.0f;
        l.doneX = cx - 100.0f;
        l.doneY = static_cast<float>(guiH) - 26.0f;
        l.contentH = static_cast<float>(rows) * l.pitch + 12.0f;
        l.maxScroll = std::max(0.0f, l.contentH - (l.listBottom - l.listTop));
        return l;
    }

    namespace {
        const ScreenTemplateDefault kKeybindsHeadDefault("ui/screens/keybinds_screen.rml");
    }

    SceneDoc BuildKeybindsScreen(const std::vector<KeybindRow>& rows, const std::string& title,
                                 const std::string& done, const std::string& reset, float scroll, int guiW,
                                 int guiH) {
        const KeybindLayout l = ComputeKeybindLayout(static_cast<int>(rows.size()), guiW, guiH);
        const float listH = l.listBottom - l.listTop;
        SceneDoc doc;
        doc.rml = ScreenTemplate("ui/screens/keybinds_screen.rml");
        doc.scene.vars["title"] = title;
        SceneBuilder b(doc.scene.lists["body"]);
        SceneNode list;
        list.id = "list";
        list.x = std::round(0.0f);
        list.y = std::round(l.listTop);
        list.w = static_cast<float>(guiW);
        list.h = std::round(listH);
        b.Begin(std::move(list));
        const auto key = [&](int i, int which, float x, float w, const std::string& label, bool off,
                             const char* color) {
            const std::string id = "kb" + std::to_string(which) + "_" + std::to_string(i);
            const float y = l.RowY(i, scroll) - l.listTop;
            kit::Button(b, id, x, y, w, kit::Plain(label, color ? color : ""), off);
        };
        for (int i = 0; i < static_cast<int>(rows.size()); i++) {
            const KeybindRow& r = rows[static_cast<size_t>(i)];
            const float y = l.RowY(i, scroll) - l.listTop;
            if (y < 0.0f || y + 20.0f > listH)
                continue;
            if (r.header) {
                kit::Label(b, 0, y + 6.0f, static_cast<float>(guiW), "row head", kit::Plain(r.label));
                continue;
            }
            kit::Label(b, l.labelX, y + 6.0f, 175, "row", kit::Plain(r.label));
            key(i, 0, l.keyX, l.keyW, r.value, false,
                r.color == 1 ? "#ff5555" : r.color == 2 ? "#ffff55" : nullptr);
            key(i, 1, l.resetX, l.resetW, reset, !r.canReset, nullptr);
        }
        b.End();
        const float gw = static_cast<float>(guiW);
        kit::Box(b, 0, l.listTop - 1.0f, gw, 1, "sepb");
        kit::Box(b, 0, l.listTop - 2.0f, gw, 1, "sep");
        kit::Box(b, 0, l.listBottom, gw, 1, "sepb");
        kit::Box(b, 0, l.listBottom + 1.0f, gw, 1, "sep");
        if (l.maxScroll > 0.0f) {
            const float th = std::max(16.0f, listH * listH / l.contentH);
            const float ty = l.listTop + (listH - th) * (scroll / l.maxScroll);
            const float sx = static_cast<float>(guiW / 2 + 160);
            kit::Box(b, sx, l.listTop, 6, listH, "track");
            kit::Box(b, sx, ty, 6, th, "thumb");
        }
        kit::Button(b, kKeybindDoneId, l.doneX, l.doneY, l.doneW, kit::Plain(done));
        return doc;
    }

    namespace {
        const ScreenTemplateDefault kListHeadDefault("ui/screens/list_screen.rml");
    }

    namespace {
        SceneDoc ListDoc(const std::string& title, const std::string& dimClass) {
            SceneDoc doc;
            doc.rml = ScreenTemplate("ui/screens/list_screen.rml");
            doc.scene.vars["title"] = title;
            doc.scene.vars["dim_cls"] = dimClass;
            return doc;
        }
    }

    float ListLayout::ButtonX(const ListRow& row, int index) const {
        float x = rightX;
        for (int i = static_cast<int>(row.buttons.size()) - 1; i >= index; i--)
            x -= row.buttons[static_cast<size_t>(i)].w +
                 (i == static_cast<int>(row.buttons.size()) - 1 ? 0.0f : 4.0f);
        return x;
    }

    ListLayout ComputeListLayout(int rows, int guiW, int guiH) {
        ListLayout l;
        l.listBottom = static_cast<float>(guiH) - 32.0f;
        const float cx = static_cast<float>(guiW) / 2.0f;
        l.labelX = cx - 160.0f;
        l.rightX = cx + 160.0f;
        l.doneW = 200.0f;
        l.doneX = cx - 100.0f;
        l.doneY = static_cast<float>(guiH) - 26.0f;
        l.contentH = static_cast<float>(rows) * l.pitch + 12.0f;
        l.maxScroll = std::max(0.0f, l.contentH - (l.listBottom - l.listTop));
        return l;
    }

    SceneDoc BuildListScreen(const std::vector<ListRow>& rows, const std::string& title,
                             const std::string& done, float scroll, int guiW, int guiH) {
        const ListLayout l = ComputeListLayout(static_cast<int>(rows.size()), guiW, guiH);
        const float listH = l.listBottom - l.listTop;
        const float gw = static_cast<float>(guiW);
        SceneDoc doc = ListDoc(title, "");
        SceneBuilder b(doc.scene.lists["body"]);
        kit::Box(b, 0, l.listTop, gw, listH, "lpanel");
        for (int i = 0; i < static_cast<int>(rows.size()); i++) {
            const ListRow& r = rows[static_cast<size_t>(i)];
            const float y = l.RowY(i, scroll);
            if (y < l.listTop || y + 20.0f > l.listBottom)
                continue;
            if (r.header) {
                kit::Label(b, 0, y + 6.0f, gw, "row head", kit::Plain(r.label));
                continue;
            }
            const char* color = r.labelColor == 1   ? "#ff5555"
                                : r.labelColor == 2 ? "#55ff55"
                                : r.labelColor == 3 ? "#aaaaaa"
                                                    : nullptr;
            kit::Label(b, l.labelX, r.sub.empty() ? y + 6.0f : y + 1.0f, 190, "row",
                       kit::Plain(r.label, color ? color : ""));
            if (!r.sub.empty())
                kit::Label(b, l.labelX, y + 11.0f, 190, "sub", kit::Plain(r.sub));
            for (int k = 0; k < static_cast<int>(r.buttons.size()); k++) {
                const ListButton& btn = r.buttons[static_cast<size_t>(k)];
                const char* bc = btn.color == 1   ? "#ff5555"
                                 : btn.color == 2 ? "#ffff55"
                                 : btn.color == 3 ? "#55ff55"
                                                  : nullptr;
                kit::Button(b, "lb" + std::to_string(k) + "_" + std::to_string(i), l.ButtonX(r, k), y, btn.w,
                            kit::Plain(btn.text, bc ? bc : ""), !btn.enabled);
            }
        }
        kit::Box(b, 0, l.listTop - 1.0f, gw, 1, "sepb");
        kit::Box(b, 0, l.listTop - 2.0f, gw, 1, "sep");
        kit::Box(b, 0, l.listBottom, gw, 1, "sepb");
        kit::Box(b, 0, l.listBottom + 1.0f, gw, 1, "sep");
        if (l.maxScroll > 0.0f) {
            const float th = std::max(16.0f, listH * listH / l.contentH);
            const float ty = l.listTop + (listH - th) * (scroll / l.maxScroll);
            const float sx = static_cast<float>(guiW / 2 + 164);
            kit::Box(b, sx, l.listTop, 6, listH, "sbtrack");
            kit::Box(b, sx, ty, 6, th, "sbthumb");
        }
        kit::Button(b, "listdone", l.doneX, l.doneY, l.doneW, kit::Plain(done));
        return doc;
    }

    WorldLayout ComputeWorldLayout(const std::vector<WorldRow>& rows, int guiW, int guiH, float scroll) {
        WorldLayout l;
        l.panelW = std::min(300.0f, static_cast<float>(guiW) - 20.0f);
        l.panelX = (static_cast<float>(guiW) - l.panelW) / 2.0f;
        l.listTop = 28.0f;
        l.listBottom = static_cast<float>(guiH) - 34.0f;
        l.doneW = 200.0f;
        l.doneX = static_cast<float>(guiW) / 2.0f - 100.0f;
        l.doneY = static_cast<float>(guiH) - 27.0f;
        float y = 0.0f;
        for (const WorldRow& r : rows) {
            float h = 32.0f;
            switch (r.kind) {
            case WorldRow::Kind::Section:
                h = 26.0f;
                break;
            case WorldRow::Kind::Toggle:
                h = 32.0f;
                break;
            case WorldRow::Kind::Segmented:
                h = r.desc.empty() ? 42.0f : 54.0f;
                break;
            case WorldRow::Kind::Slider:
                h = 46.0f;
                break;
            case WorldRow::Kind::Number:
                h = 58.0f;
                break;
            case WorldRow::Kind::Button:
                h = 30.0f;
                break;
            }
            l.y.push_back(y);
            l.h.push_back(h);
            y += h;
        }
        l.contentH = y;
        l.maxScroll = std::max(0.0f, y - (l.listBottom - l.listTop));
        const float offset = std::clamp(scroll, 0.0f, l.maxScroll);
        for (float& v : l.y)
            v = l.listTop + v - offset;
        return l;
    }

    SceneDoc BuildWorldScreen(const std::vector<WorldRow>& rows, const std::string& title,
                              const std::string& done, float scroll, int guiW, int guiH) {
        const WorldLayout l = ComputeWorldLayout(rows, guiW, guiH, scroll);
        SceneDoc doc = ListDoc(title, "heavy");
        SceneBuilder b(doc.scene.lists["body"]);
        kit::Box(b, l.panelX, l.listTop, l.panelW, l.listBottom - l.listTop, "wpanel");
        const float px = l.panelX;
        for (size_t i = 0; i < rows.size(); i++) {
            const WorldRow& r = rows[i];
            const float y = l.y[i], h = l.h[i];
            if (y < l.listTop - 0.5f || y + h > l.listBottom + 0.5f)
                continue;
            const std::string labelColor = r.enabled ? "#ffffff" : "#8a8a8a";
            if (r.kind != WorldRow::Kind::Section)
                kit::Box(b, px, y + h - 1.0f, l.panelW, 1, "wline");
            switch (r.kind) {
            case WorldRow::Kind::Section:
                kit::Label(b, px + 8.0f, y + 4.0f, 0, "wl", kit::Plain(r.label, "#ffffff"));
                if (!r.desc.empty())
                    kit::Label(b, px + 8.0f, y + 14.0f, 0, "wd", kit::Plain(r.desc));
                break;
            case WorldRow::Kind::Toggle: {
                kit::Label(b, px + 8.0f, y + 6.0f, 0, "wl", kit::Plain(r.label, labelColor));
                kit::Label(b, px + 8.0f, y + 17.0f, l.panelW - 60.0f, "wd clip", kit::Plain(r.desc));
                const float sx = px + l.panelW - 8.0f - 36.0f, sy = y + 8.0f;
                kit::Box(b, sx, sy, 36, 16, "wedge");
                kit::Box(b, sx + 1.0f, sy + 1.0f, 34, 14, r.on ? "wtrack on" : "wtrack");
                const float tx = r.on ? sx + 18.0f : sx + 1.0f;
                kit::Box(b, tx, sy + 1.0f, 17, 14, "wknob0");
                kit::Box(b, tx + 1.0f, sy + 1.0f, 15, 12, "wknob1");
                kit::Label(b, r.on ? sx + 1.0f : sx + 18.0f, sy + 4.0f, 17, "seg", kit::Plain(r.on ? "I" : "O", "#ffffff"));
                break;
            }
            case WorldRow::Kind::Segmented: {
                kit::Label(b, px + 8.0f, y + 5.0f, 0, "wl", kit::Plain(r.label, labelColor));
                const float segW =
                    (l.panelW - 16.0f) / static_cast<float>(std::max<size_t>(1, r.options.size()));
                for (size_t k = 0; k < r.options.size(); k++) {
                    const bool sel = static_cast<int>(k) == r.selected;
                    const float bx = px + 8.0f + static_cast<float>(k) * segW;
                    kit::Box(b, bx, y + 16.0f, segW, 20, "wedge");
                    kit::Box(b, bx + 1.0f, y + 17.0f, segW - 2.0f, 18, sel ? "wseg on" : "wseg");
                    kit::Label(b, bx, y + 22.0f, segW, "seg", kit::Plain(r.options[k], sel ? "#ffffff" : "#303030"));
                }
                if (!r.desc.empty())
                    kit::Label(b, px + 8.0f, y + 40.0f, l.panelW - 16.0f, "wd clip", kit::Plain(r.desc));
                break;
            }
            case WorldRow::Kind::Slider: {
                kit::Label(b, px + 8.0f, y + 5.0f, 0, "wl", kit::Plain(r.label + " " + r.value, labelColor));
                kit::Label(b, px + 8.0f, y + 15.0f, l.panelW - 16.0f, "wd clip", kit::Plain(r.desc));
                const float tw = l.panelW - 16.0f;
                const float fraction = std::clamp(r.fraction, 0.0f, 1.0f);
                kit::Box(b, px + 8.0f, y + 28.0f, tw, 8, "wedge");
                kit::Box(b, px + 9.0f, y + 29.0f, (tw - 2.0f) * fraction, 6, "wfill");
                const float hx = px + 8.0f + (tw - 8.0f) * fraction;
                kit::Box(b, hx, y + 25.0f, 8, 14, "wedge");
                kit::Box(b, hx + 1.0f, y + 26.0f, 6, 12, "wknob1");
                break;
            }
            case WorldRow::Kind::Number: {
                kit::Label(b, px + 8.0f, y + 5.0f, 0, "wl", kit::Plain(r.label, labelColor));
                kit::Box(b, px + 8.0f, y + 16.0f, l.panelW - 16.0f, 20, r.editing ? "wedge editing" : "wedge");
                kit::Box(b, px + 9.0f, y + 17.0f, l.panelW - 18.0f, 18, "wfield");
                kit::Label(b, px + 14.0f, y + 22.0f, 0, "wl",
                           r.editing ? TextFieldRuns(r.value, true) : kit::Plain(r.value));
                kit::Label(b, px + 8.0f, y + 41.0f, l.panelW - 16.0f, "wd clip", kit::Plain(r.desc));
                break;
            }
            case WorldRow::Kind::Button:
                kit::Button(b, "wb" + std::to_string(i), px + 8.0f, y + 4.0f, 150.0f, kit::Plain(r.label), !r.enabled);
                break;
            }
        }
        if (l.maxScroll > 0.0f) {
            const float listH = l.listBottom - l.listTop;
            const float th = std::max(16.0f, listH * listH / l.contentH);
            const float ty = l.listTop + (listH - th) * (std::clamp(scroll, 0.0f, l.maxScroll) / l.maxScroll);
            kit::Box(b, l.panelX + l.panelW + 2.0f, ty, 4, th, "wthumb");
        }
        kit::Button(b, "worlddone", l.doneX, l.doneY, l.doneW, kit::Plain(done));
        return doc;
    }

    SceneDoc BuildPauseScreen(const std::string& templateText, const PauseScreenText& t, int guiW, int guiH) {
        SceneDoc doc;
        doc.rml = templateText;
        const int top = (guiH - (t.showReplay ? 178 : 154)) / 4;
        const int quitY = t.showReplay ? top + 174 : top + 150;
        const int x1 = guiW / 2 - 102;
        auto& v = doc.scene.vars;
        v["x1"] = std::to_string(x1);
        v["x2"] = std::to_string(x1 + 104);
        v["y1"] = std::to_string(top + 54);
        v["y2"] = std::to_string(top + 78);
        v["y3"] = std::to_string(top + 102);
        v["y4"] = std::to_string(top + 126);
        v["y5"] = std::to_string(top + 150);
        v["y6"] = std::to_string(quitY);
        v["xw"] = std::to_string(x1 + 208);
        v["xr"] = std::to_string(x1 + 104);
        v["mods"] = t.mods;
        v["title"] = t.title;
        v["back"] = t.back;
        v["feedback"] = t.feedback;
        v["players"] = t.players;
        v["options"] = t.options;
        v["world"] = t.world;
        v["quit"] = t.quit;
        SceneBuilder b(doc.scene.lists["extra"]);
        if (t.showReplay) {
            kit::Button(b, kPauseReplayStartId, static_cast<float>(x1), static_cast<float>(top + 150), 100.0f,
                        kit::Plain(t.replayStart), t.replayRecording);
            kit::Button(b, kPauseReplayStopId, static_cast<float>(x1 + 104), static_cast<float>(top + 150),
                        100.0f, kit::Plain(t.replayStop), !t.replayRecording);
        }
        if (t.showPanel) {
            const int pw = 150;
            const int rows = std::max<int>(1, static_cast<int>(t.panel.size()));
            const int shown = std::min(rows, (guiH - 40) / 14);
            const int ph = 22 + shown * 14 + 4;
            const int px = guiW - pw - 6;
            const int py = std::max(4, (guiH - ph) / 2);
            SceneNode panel;
            panel.id = "pp";
            panel.x = static_cast<float>(px);
            panel.y = static_cast<float>(py);
            panel.w = static_cast<float>(pw);
            panel.h = static_cast<float>(ph);
            panel.z = "6";
            b.Begin(std::move(panel));
            kit::Box(b, 1, 1, static_cast<float>(pw - 2), static_cast<float>(ph - 2), "ppa");
            kit::Box(b, 1, 1, static_cast<float>(pw - 3), static_cast<float>(ph - 3), "ppb");
            kit::Box(b, 2, 2, static_cast<float>(pw - 4), static_cast<float>(ph - 4), "ppc");
            kit::Label(b, 0, 8, static_cast<float>(pw), "pptitle", kit::Plain(t.panelTitle));
            for (int i = 0; i < shown && i < static_cast<int>(t.panel.size()); i++) {
                const PauseScreenText::PanelRow& r = t.panel[static_cast<size_t>(i)];
                const char* color = r.perm >= 2 ? "#ffaa00" : (r.perm == 0 ? "#aaaaaa" : "#55ff55");
                const std::string& role =
                    r.perm >= 2 ? t.roleOperator : (r.perm == 0 ? t.roleVisitor : t.roleMember);
                const float ry = static_cast<float>(22 + i * 14);
                kit::Label(b, 8, ry, 80, "ppname", kit::Formatted(r.name));
                kit::Label(b, 88, ry, static_cast<float>(pw - 96), "pprole", kit::Plain(role, color));
            }
            b.End();
        }
        return doc;
    }
}
