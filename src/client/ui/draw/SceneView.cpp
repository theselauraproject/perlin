#include "client/ui/draw/SceneView.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace perlin::ui::draw {
    namespace {
        std::vector<float> Floats(const std::string& text) {
            std::vector<float> out;
            std::istringstream in(text);
            float v;
            while (in >> v)
                out.push_back(v);
            return out;
        }

        Color ParseOr(const std::string& text, Color fallback) {
            Color c = fallback;
            if (!text.empty() && ParseColor(text, c))
                return c;
            return fallback;
        }

        int ZOf(const SceneNode& n) {
            if (n.z.empty() || n.z == "auto")
                return INT_MIN;
            return std::atoi(n.z.c_str());
        }

        struct Frag {
            std::string text;
            float x = 0;
            const SceneRun* run = nullptr;
            float w = 0;
            float boxW = -1;
        };

        struct Line {
            std::vector<Frag> frags;
            float width = 0;
        };

        struct Atom {
            std::string text;
            const SceneRun* run = nullptr;
            bool space = false;
            bool newline = false;
            bool block = false;
            float blockW = 0;
        };

        Face RunFace(const Style& s, const SceneRun* run) {
            return FaceFromFamily(s.family, s.size, run && run->weight == "bold", run && run->style == "italic");
        }

        void Atomize(const std::vector<SceneRun>& runs, const Style& s, std::vector<Atom>& atoms) {
            for (const SceneRun& run : runs) {
                if (run.display == "inline-block") {
                    Atom a;
                    a.text = run.text;
                    a.run = &run;
                    a.block = true;
                    a.blockW = run.width == "auto" || run.width.empty() ? -1.0f : std::strtof(run.width.c_str(), nullptr);
                    atoms.push_back(std::move(a));
                    continue;
                }
                const std::string& t = run.text;
                size_t i = 0;
                while (i < t.size()) {
                    if (t[i] == '\n') {
                        Atom a;
                        a.newline = true;
                        a.run = &run;
                        atoms.push_back(a);
                        i++;
                        continue;
                    }
                    if (t[i] == ' ' || t[i] == '\t') {
                        size_t j = i;
                        while (j < t.size() && (t[j] == ' ' || t[j] == '\t'))
                            j++;
                        Atom a;
                        a.text = s.pre ? t.substr(i, j - i) : std::string(" ");
                        a.run = &run;
                        a.space = true;
                        atoms.push_back(std::move(a));
                        i = j;
                        continue;
                    }
                    size_t j = i;
                    while (j < t.size() && t[j] != ' ' && t[j] != '\t' && t[j] != '\n')
                        j++;
                    Atom a;
                    a.text = t.substr(i, j - i);
                    a.run = &run;
                    atoms.push_back(std::move(a));
                    i = j;
                }
            }
        }

        std::vector<Line> Flow(const std::vector<SceneRun>& runs, const Style& s, float width) {
            std::vector<Atom> atoms;
            Atomize(runs, s, atoms);
            std::vector<Line> lines(1);
            const auto push = [&](Line& line, const std::string& text, const SceneRun* run, bool block, float boxW) {
                const Face face = RunFace(s, run);
                float w = static_cast<float>(StringWidth(face, text));
                if (block && boxW >= 0)
                    w = boxW;
                Frag f;
                f.text = text;
                f.x = line.width;
                f.run = run;
                f.w = w;
                f.boxW = block ? boxW : -1;
                line.frags.push_back(std::move(f));
                line.width += w;
            };
            const bool wrap = !s.nowrap && width > 0;
            for (size_t i = 0; i < atoms.size(); i++) {
                Atom& a = atoms[i];
                Line& line = lines.back();
                if (a.newline) {
                    lines.emplace_back();
                    continue;
                }
                if (a.block) {
                    push(line, a.text, a.run, true, a.blockW);
                    continue;
                }
                if (a.space) {
                    if (!s.pre && (line.frags.empty() || line.frags.back().text == " "))
                        continue;
                    push(line, a.text, a.run, false, -1);
                    continue;
                }
                const float w = static_cast<float>(StringWidth(RunFace(s, a.run), a.text));
                if (wrap && !line.frags.empty() && line.width + w > width) {
                    while (!line.frags.empty() && line.frags.back().text == " " && !s.pre) {
                        line.width -= line.frags.back().w;
                        line.frags.pop_back();
                    }
                    lines.emplace_back();
                }
                push(lines.back(), a.text, a.run, false, -1);
            }
            if (!s.pre)
                for (Line& line : lines)
                    while (!line.frags.empty() && line.frags.back().text == " ") {
                        line.width -= line.frags.back().w;
                        line.frags.pop_back();
                    }
            return lines;
        }

        class Drawer {
        public:
            Drawer(const Scene& scene, const HoverSet& hover, TextureSource& tex, int guiW, int guiH, Rendered& out)
                : scene(scene), hover(hover), tex(tex), canvas(tex, out.batches), out(out), guiW(guiW), guiH(guiH),
                  device(std::max(1, tex.Device())) {}

            void Run() {
                out.batches.clear();
                out.hits.clear();
                out.shifts.clear();
                out.usesHover = false;
                out.panoSpan = scene.panoSpan;
                out.fontStamp = FontStamp();
                Style root;
                root.pointer = scene.theme != Theme::Hud;
                std::vector<Entry> list;
                rootStyle = root;
                Collect(-1, scene.nodes, 0, 0, list);
                Paint(list);
                out.starved = canvas.Starved();
            }

        private:
            struct Ctx {
                const SceneNode* n = nullptr;
                int parent = -1;
                Style style;
                float ax = 0, ay = 0, w = 0, h = 0;
            };
            struct Entry {
                int idx;
                int z;
            };

            const Scene& scene;
            const HoverSet& hover;
            TextureSource& tex;
            Canvas canvas;
            Rendered& out;
            int guiW, guiH, device;
            Style rootStyle;
            std::vector<Ctx> arena;

            std::vector<const SceneNode*> PathOf(int idx) const {
                std::vector<const SceneNode*> path;
                for (int i = idx; i >= 0; i = arena[static_cast<size_t>(i)].parent)
                    path.push_back(arena[static_cast<size_t>(i)].n);
                std::reverse(path.begin(), path.end());
                return path;
            }

            bool Hot(const SceneNode* n) const { return hover.contains(n); }

            static bool CreatesContext(const Ctx& c) { return ZOf(*c.n) != INT_MIN || c.style.clip; }

            std::vector<SceneRun> NoRuns;

            void Measure(Ctx& c) {
                const SceneNode& n = *c.n;
                c.w = n.w;
                c.h = n.h;
                if (n.kind == SceneNode::Label || n.kind == SceneNode::Text) {
                    if (c.w <= 0 || c.h <= 0) {
                        Style s = TextStyle(c);
                        const std::vector<Line> lines = Flow(n.runs, s, c.w > 0 ? c.w : -1);
                        float width = 0;
                        for (const Line& l : lines)
                            width = std::max(width, l.width);
                        if (c.w <= 0)
                            c.w = width;
                        if (c.h <= 0)
                            c.h = s.line * static_cast<float>(lines.size());
                    }
                }
            }

            Style TextStyle(const Ctx& c) const {
                Style s = c.style;
                const SceneNode& n = *c.n;
                if (n.kind == SceneNode::Text) {
                    s.family = n.family;
                    s.size = n.size;
                    s.line = n.lineHeight;
                    s.align = n.align == "center" ? 1 : n.align == "right" ? 2 : 0;
                    s.color = ParseOr(n.color.empty() ? "#ffffff" : n.color, Rgb(0xffffff));
                    s.nowrap = true;
                    s.pre = true;
                }
                return s;
            }

            void Collect(int parent, const std::vector<SceneNode>& nodes, float ox, float oy, std::vector<Entry>& list) {
                for (const SceneNode& n : nodes) {
                    Ctx c;
                    c.n = &n;
                    c.parent = parent;
                    const std::vector<const SceneNode*> path = PathOf(parent);
                    const Style& ps = parent >= 0 ? arena[static_cast<size_t>(parent)].style : rootStyle;
                    c.style = ResolveStyle(scene.theme, n, ps, path, hover);
                    if (c.style.hoverDependent)
                        out.usesHover = true;
                    if (c.style.hidden)
                        continue;
                    c.ax = ox + n.x;
                    c.ay = oy + n.y;
                    Measure(c);
                    arena.push_back(c);
                    const int idx = static_cast<int>(arena.size()) - 1;
                    list.push_back({idx, ZOf(n)});
                    if (!CreatesContext(arena[static_cast<size_t>(idx)]))
                        Collect(idx, n.children, c.ax, c.ay, list);
                }
            }

            void Paint(std::vector<Entry>& list) {
                std::vector<Entry> sorted = list;
                std::stable_sort(sorted.begin(), sorted.end(), [](const Entry& a, const Entry& b) {
                    return (a.z == INT_MIN ? 0 : a.z) < (b.z == INT_MIN ? 0 : b.z);
                });
                for (const Entry& e : sorted)
                    PaintEntry(e.idx);
            }

            void RecordHit(int idx) {
                const Ctx& c = arena[static_cast<size_t>(idx)];
                Hit h;
                h.node = c.n;
                h.parent = c.parent >= 0 ? hitIndex[static_cast<size_t>(c.parent)] : -1;
                h.x0 = c.ax;
                h.y0 = c.ay;
                h.x1 = c.ax + c.w;
                h.y1 = c.ay + c.h;
                h.pointer = c.style.pointer;
                if (clipDepth > 0) {
                    h.cx0 = clipRect[0];
                    h.cy0 = clipRect[1];
                    h.cx1 = clipRect[2];
                    h.cy1 = clipRect[3];
                }
                if (hitIndex.size() <= static_cast<size_t>(idx))
                    hitIndex.resize(static_cast<size_t>(idx) + 1, -1);
                hitIndex[static_cast<size_t>(idx)] = static_cast<int>(out.hits.size());
                out.hits.push_back(h);
            }

            std::vector<int> hitIndex;
            int clipDepth = 0;
            float clipRect[4] = {0, 0, 0, 0};
            std::vector<std::array<float, 4>> clipStack;

            void PaintEntry(int idx) {
                Ctx& c = arena[static_cast<size_t>(idx)];
                const SceneNode& n = *c.n;
                if (!n.layer.empty())
                    canvas.SetLayer(n.layer == "top" ? UiLayer::Top : n.layer == "export" ? UiLayer::Export : UiLayer::Gui);
                const int previousShift = canvas.Shift();
                const bool movable = n.id == "cursor" || n.id == "tip";
                if ((scene.theme == Theme::Title && n.id == "pano") || movable) {
                    out.shifts.push_back({n.id, c.ax, c.ay});
                    canvas.SetShift(static_cast<int>(out.shifts.size()) - 1);
                }
                RecordHit(idx);
                PaintBox(c);
                PaintContent(c);
                if (CreatesContext(c)) {
                    bool visible = true;
                    if (c.style.clip) {
                        canvas.PushClip(c.ax, c.ay, c.ax + c.w, c.ay + c.h);
                        clipStack.push_back({clipDepth > 0 ? clipRect[0] : -1e9f, clipDepth > 0 ? clipRect[1] : -1e9f,
                                             clipDepth > 0 ? clipRect[2] : 1e9f, clipDepth > 0 ? clipRect[3] : 1e9f});
                        clipRect[0] = clipDepth > 0 ? std::max(clipRect[0], c.ax) : c.ax;
                        clipRect[1] = clipDepth > 0 ? std::max(clipRect[1], c.ay) : c.ay;
                        clipRect[2] = clipDepth > 0 ? std::min(clipRect[2], c.ax + c.w) : c.ax + c.w;
                        clipRect[3] = clipDepth > 0 ? std::min(clipRect[3], c.ay + c.h) : c.ay + c.h;
                        clipDepth++;
                        visible = !canvas.ClipEmpty();
                    }
                    if (visible) {
                        std::vector<Entry> inner;
                        Collect(idx, n.children, c.ax, c.ay, inner);
                        Paint(inner);
                    }
                    if (c.style.clip) {
                        clipDepth--;
                        const auto& prev = clipStack.back();
                        for (int i = 0; i < 4; i++)
                            clipRect[i] = prev[static_cast<size_t>(i)];
                        clipStack.pop_back();
                        canvas.PopClip();
                    }
                }
                canvas.SetShift(previousShift);
            }

            void PaintBox(const Ctx& c) {
                const Style& s = c.style;
                if (s.hasBg)
                    canvas.Solid(c.ax, c.ay, c.w, c.h, s.bg);
                if (s.border > 0) {
                    canvas.Solid(c.ax, c.ay, c.w, s.border, s.borderColor);
                    canvas.Solid(c.ax, c.ay + c.h - s.border, c.w, s.border, s.borderColor);
                    canvas.Solid(c.ax, c.ay + s.border, s.border, c.h - 2 * s.border, s.borderColor);
                    canvas.Solid(c.ax + c.w - s.border, c.ay + s.border, s.border, c.h - 2 * s.border, s.borderColor);
                }
            }

            void PaintContent(Ctx& c) {
                const SceneNode& n = *c.n;
                switch (n.kind) {
                case SceneNode::Image:
                    PaintImage(c);
                    break;
                case SceneNode::Item:
                    canvas.Item(n.key, c.ax, c.ay, c.w, c.h, n.alpha.empty() ? 1.0f : std::strtof(n.alpha.c_str(), nullptr),
                                !n.glint.empty());
                    break;
                case SceneNode::Text:
                case SceneNode::Label:
                    PaintText(c, TextStyle(c), 0, 0, c.w, n.runs);
                    break;
                case SceneNode::Rect:
                    canvas.Solid(c.ax, c.ay, c.w, c.h, ParseOr(n.color, Rgb(0xffffff)));
                    break;
                case SceneNode::Gradient:
                    PaintGradient(c);
                    break;
                case SceneNode::Link:
                    if (Hot(c.n)) {
                        out.usesHover = true;
                        canvas.Solid(c.ax, c.ay + c.h - 1, c.w, 1, Rgb(0xffff55));
                    }
                    break;
                case SceneNode::Button:
                case SceneNode::Slider:
                    PaintButton(c);
                    break;
                default:
                    break;
                }
            }

            void PaintGradient(const Ctx& c) {
                const std::string& d = c.n->decorator;
                const bool vertical = d.starts_with("vertical");
                const size_t open = d.find('(');
                const size_t close = d.rfind(')');
                if (open == std::string::npos || close == std::string::npos)
                    return;
                std::istringstream in(d.substr(open + 1, close - open - 1));
                std::string a, b;
                in >> a >> b;
                Color ca, cb;
                if (!ParseColor(a, ca) || !ParseColor(b, cb))
                    return;
                canvas.Gradient(c.ax, c.ay, c.w, c.h, vertical, ca, cb);
            }

            void PaintImage(const Ctx& c) {
                const SceneNode& n = *c.n;
                ImageSpec spec;
                spec.src = n.src;
                spec.x = c.ax;
                spec.y = c.ay;
                spec.w = c.w;
                spec.h = c.h;
                if (!n.uv.empty()) {
                    const auto v = Floats(n.uv);
                    if (v.size() >= 2) {
                        spec.hasUv = true;
                        spec.uv[0] = v[0];
                        spec.uv[1] = v[1];
                        if (v.size() >= 4) {
                            spec.uv[2] = v[2];
                            spec.uv[3] = v[3];
                        }
                    }
                }
                if (!n.nine.empty()) {
                    const auto v = Floats(n.nine);
                    if (v.size() >= 4) {
                        spec.nine = true;
                        for (int i = 0; i < 4; i++)
                            spec.slice[i] = v[static_cast<size_t>(i)];
                    }
                }
                spec.tiled = n.tiled.empty() ? 0 : std::atoi(n.tiled.c_str());
                spec.tileScale = n.tileScale.empty() ? 1.0f : std::strtof(n.tileScale.c_str(), nullptr);
                spec.clip = n.clipDirection;
                if (!n.tint.empty()) {
                    const auto v = Floats(n.tint);
                    if (v.size() >= 3) {
                        const auto byte = [](float f) {
                            return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(f * 255.0f)), 0, 255));
                        };
                        spec.tint = {byte(v[0]), byte(v[1]), byte(v[2]), 255};
                    }
                }
                spec.alpha = n.alpha.empty() ? 1.0f : std::strtof(n.alpha.c_str(), nullptr);
                spec.keep = !n.keep.empty() && std::atoi(n.keep.c_str()) != 0;
                canvas.Image(spec);
            }

            void PaintText(const Ctx& c, const Style& s, float offX, float offY, float width,
                           const std::vector<SceneRun>& runs) {
                if (runs.empty())
                    return;
                const std::vector<Line> lines = Flow(runs, s, width > 0 ? width : -1);
                const Face base = RunFace(s, nullptr);
                const FontMetrics m = MetricsFor(base);
                float blockW = width;
                if (blockW <= 0)
                    for (const Line& l : lines)
                        blockW = std::max(blockW, l.width);
                float top = c.ay + offY;
                for (const Line& line : lines) {
                    float x = c.ax + offX;
                    if (s.align == 1)
                        x += (blockW - line.width) * 0.5f;
                    else if (s.align == 2)
                        x += blockW - line.width;
                    const float baseline = top + (s.line - (m.ascent + m.descent)) * 0.5f + m.ascent;
                    for (const Frag& f : line.frags) {
                        const SceneRun* run = f.run;
                        const Face face = RunFace(s, run);
                        Color color = s.color;
                        if (run && run->colored == "1" && !run->color.empty())
                            color = ParseOr(run->color, s.color);
                        const float fx = x + f.x;
                        if (run && !run->background.empty() && run->background != "transparent") {
                            Color bg;
                            if (ParseColor(run->background, bg))
                                canvas.Solid(fx, baseline - m.ascent, f.w, m.ascent + m.descent, bg);
                        }
                        const bool clipBox = f.boxW >= 0 && run && run->overflow == "hidden";
                        if (clipBox)
                            canvas.PushClip(fx, top, fx + f.w, top + s.line);
                        canvas.Text(face, f.text, fx, baseline, color);
                        if (clipBox)
                            canvas.PopClip();
                    }
                    top += s.line;
                }
            }

            void PaintButton(const Ctx& c) {
                const SceneNode& n = *c.n;
                const bool slider = n.kind == SceneNode::Slider;
                const bool off = HasClass(n, "off");
                const bool on = HasClass(n, "on");
                const bool hot = Hot(c.n);
                out.usesHover = true;
                const float w = c.w, h = n.h > 0 ? n.h : 20.0f;
                const float x = c.ax, y = c.ay;
                Style label = c.style;
                label.family = "minecraft-shadow";
                label.size = 8;
                label.line = 9;
                label.align = 1;
                label.nowrap = true;
                label.color = off ? Rgb(0xa0a0a0) : Rgb(0xffffff);
                const bool useSprites = scene.sprites || tex.Get("mcres:ui_menu_btn.png").handle != 0;
                if (useSprites) {
                    ImageSpec spec;
                    spec.src = off || slider ? "mcres:ui_menu_btn_off.png" : hot ? "mcres:ui_menu_btn_hi.png" : "mcres:ui_menu_btn.png";
                    spec.x = x;
                    spec.y = y;
                    spec.w = w;
                    spec.h = h;
                    spec.nine = true;
                    for (float& f : spec.slice)
                        f = 3;
                    canvas.Image(spec);
                    if (slider) {
                        ImageSpec knob;
                        knob.src = hot ? "mcres:ui_menu_btn_hi.png" : "mcres:ui_menu_btn.png";
                        knob.x = x + n.knob;
                        knob.y = y;
                        knob.w = 8;
                        knob.h = h;
                        knob.nine = true;
                        for (float& f : knob.slice)
                            f = 3;
                        canvas.Image(knob);
                    }
                } else {
                    const bool facing = scene.theme == Theme::Options || scene.theme == Theme::Replay;
                    canvas.Solid(x, y, w, h, hot && !off ? Rgb(0xffffff) : Rgb(0x000000));
                    uint32_t rim = off ? 0x2c2c2c : hot ? 0xafafaf : 0xaaaaaa;
                    uint32_t face = off ? 0x2c2c2c : hot ? 0x757575 : 0x6f6f6f;
                    if (on && !off) {
                        rim = 0x7fb85a;
                        face = 0x4c7a2f;
                    }
                    if (slider) {
                        rim = 0x2c2c2c;
                        face = 0x1f1f1f;
                    }
                    canvas.Solid(x + 1, y + 1, w - 3, 16, Rgb(rim));
                    canvas.Solid(x + 2, y + 2, w - 4, 15, Rgb(face));
                    if (!slider) {
                        const uint32_t shade = off ? 0x2c2c2c : hot && facing ? 0x5c5c5c : 0x565656;
                        const uint32_t edge = off ? 0x2c2c2c : hot ? 0x5c5c5c : 0x565656;
                        canvas.Solid(x + 1, y + 17, w - 2, 2, Rgb(shade));
                        canvas.Solid(x + w - 2, y + 1, 1, 16, Rgb(edge));
                    }
                }
                Ctx lc = c;
                lc.ay = y + 6;
                PaintText(lc, label, 0, 0, w, n.runs);
                if (slider && !useSprites) {
                    const float kx = x + n.knob;
                    canvas.Solid(kx, y, 8, 20, Rgb(0x000000));
                    canvas.Solid(kx + 1, y + 1, 6, 16, Rgb(hot ? 0xbdbdbd : 0xaaaaaa));
                    canvas.Solid(kx + 1, y + 17, 6, 2, Rgb(0x565656));
                }
            }
        };
    }

    void RenderScene(const Scene& scene, const HoverSet& hover, TextureSource& textures, int guiW, int guiH,
                     Rendered& out) {
        Drawer drawer(scene, hover, textures, guiW, guiH, out);
        drawer.Run();
    }

    int HitTest(const Rendered& r, float x, float y) {
        for (int i = static_cast<int>(r.hits.size()) - 1; i >= 0; i--) {
            const Hit& h = r.hits[static_cast<size_t>(i)];
            if (!h.pointer)
                continue;
            if (x < h.x0 || y < h.y0 || x >= h.x1 || y >= h.y1)
                continue;
            if (x < h.cx0 || y < h.cy0 || x >= h.cx1 || y >= h.cy1)
                continue;
            return i;
        }
        return -1;
    }

    void HoverChain(const Rendered& r, int index, HoverSet& out) {
        out.clear();
        for (int i = index; i >= 0; i = r.hits[static_cast<size_t>(i)].parent)
            out.insert(r.hits[static_cast<size_t>(i)].node);
    }

    float PanoOffset(float span, double seconds) {
        const double t = std::fmod(seconds, 240.0) / 240.0;
        return static_cast<float>(-t * span);
    }
}
