#include "client/ui/SceneKit.hpp"

#include <algorithm>
#include <cstdio>

#include <algorithm>
#include <cmath>

namespace perlin::ui::kit {
    namespace {
        float Round(float v) {
            return std::round(v);
        }
    }

    std::string Px(float v) {
        return std::to_string(static_cast<int>(std::lround(v)));
    }

    std::vector<SceneRun> Plain(const std::string& text, const std::string& color) {
        SceneRun run;
        run.text = text;
        if (!color.empty()) {
            run.colored = "1";
            run.color = color;
        }
        return {std::move(run)};
    }

    std::vector<SceneRun> Formatted(const std::string& text, const std::string& color) {
        return ParseFormatRuns(text, color);
    }

    std::vector<SceneRun> Auto(const std::string& text, bool formatted, const std::string& color) {
        return formatted ? Formatted(text, color) : Plain(text, color);
    }

    SceneNode& Box(SceneBuilder& b, float x, float y, float w, float h, const std::string& cls,
                   const std::string& id, const std::string& z) {
        SceneNode n;
        n.kind = SceneNode::Box;
        n.x = Round(x);
        n.y = Round(y);
        n.w = Round(w);
        n.h = Round(h);
        n.cls = cls;
        n.id = id;
        n.z = z;
        return b.Add(std::move(n));
    }

    SceneNode& Begin(SceneBuilder& b, float x, float y, float w, float h, const std::string& cls,
                     const std::string& id, const std::string& z) {
        SceneNode n;
        n.kind = SceneNode::Box;
        n.x = Round(x);
        n.y = Round(y);
        n.w = Round(w);
        n.h = Round(h);
        n.cls = cls;
        n.id = id;
        n.z = z;
        return b.Begin(std::move(n));
    }

    SceneNode& Rect(SceneBuilder& b, float x, float y, float w, float h, const std::string& color,
                    const std::string& z, const std::string& cls, const std::string& id) {
        SceneNode n;
        n.kind = SceneNode::Rect;
        n.x = Round(x);
        n.y = Round(y);
        n.w = Round(w);
        n.h = Round(h);
        n.color = color;
        n.cls = cls;
        n.id = id;
        n.z = z;
        return b.Add(std::move(n));
    }

    SceneNode& Gradient(SceneBuilder& b, float x, float y, float w, float h, const std::string& decorator,
                        const std::string& z) {
        SceneNode n;
        n.kind = SceneNode::Gradient;
        n.x = Round(x);
        n.y = Round(y);
        n.w = Round(w);
        n.h = Round(h);
        n.decorator = decorator;
        n.z = z;
        return b.Add(std::move(n));
    }

    SceneNode& Label(SceneBuilder& b, float x, float y, float w, const std::string& cls,
                     std::vector<SceneRun> runs, const std::string& z, const std::string& id) {
        SceneNode n;
        n.kind = SceneNode::Label;
        n.x = Round(x);
        n.y = Round(y);
        n.w = Round(w);
        n.cls = w > 0 ? cls : cls + " auto";
        n.id = id;
        n.z = z;
        n.runs = std::move(runs);
        return b.Add(std::move(n));
    }

    SceneNode& Image(SceneBuilder& b, float x, float y, float w, float h, const std::string& src,
                     const std::string& z, const std::string& id) {
        SceneNode n;
        n.kind = SceneNode::Image;
        n.x = Round(x);
        n.y = Round(y);
        n.w = Round(w);
        n.h = Round(h);
        n.src = src;
        n.id = id;
        n.z = z;
        return b.Add(std::move(n));
    }

    SceneNode& Item(SceneBuilder& b, float x, float y, float w, float h, const std::string& key,
                    const std::string& z, const std::string& id) {
        SceneNode n;
        n.kind = SceneNode::Item;
        n.x = Round(x);
        n.y = Round(y);
        n.w = Round(w);
        n.h = Round(h);
        n.key = key;
        n.id = id;
        n.z = z;
        return b.Add(std::move(n));
    }

    SceneNode& Button(SceneBuilder& b, const std::string& id, float x, float y, float w,
                      std::vector<SceneRun> label, bool off) {
        SceneNode n;
        n.kind = SceneNode::Button;
        n.x = Round(x);
        n.y = Round(y);
        n.w = Round(w);
        n.id = id;
        n.h = 20;
        n.cls = off ? "btn off" : "btn";
        n.z = "5";
        n.runs = std::move(label);
        return b.Add(std::move(n));
    }

    SceneNode& Slider(SceneBuilder& b, const std::string& id, float x, float y, float w,
                      std::vector<SceneRun> label, float fraction) {
        SceneNode n;
        n.kind = SceneNode::Slider;
        n.x = Round(x);
        n.y = Round(y);
        n.w = Round(w);
        n.id = id;
        n.h = 20;
        n.cls = "btn track";
        n.z = "5";
        n.knob = Round(1.0f + std::clamp(fraction, 0.0f, 1.0f) * (w - 10.0f));
        n.runs = std::move(label);
        return b.Add(std::move(n));
    }

    SceneNode& Dim(SceneBuilder& b, float w, float h, int alpha) {
        char color[16];
        std::snprintf(color, sizeof color, "#000000%02x", std::clamp(alpha, 0, 255));
        return Rect(b, 0, 0, w, h, color, "auto", {}, "dim");
    }

    SceneNode& Heading(SceneBuilder& b, float guiW, float y, const std::string& title) {
        SceneNode& n = Label(b, 0, y, guiW, "ctr", Plain(title), "3", "heading");
        return n;
    }

    void Append(std::vector<SceneNode>& dst, std::vector<SceneNode>& src) {
        for (SceneNode& n : src)
            dst.push_back(std::move(n));
        src.clear();
    }

    SceneNode& Container(std::vector<SceneNode>& dst, const std::string& id, float x, float y, float w, float h,
                         const std::string& z, std::vector<SceneNode> children) {
        SceneNode n;
        n.id = id;
        n.x = x;
        n.y = y;
        n.w = w;
        n.h = h;
        n.z = z;
        n.children = std::move(children);
        dst.push_back(std::move(n));
        return dst.back();
    }

    void TopLayer(std::vector<SceneNode>& dst, float w, float h, const std::string& z, std::vector<SceneNode>& cursor,
                  std::vector<SceneNode>& tooltip) {
        std::vector<SceneNode> kids;
        SceneNode top;
        top.z = "-999";
        top.layer = "top";
        kids.push_back(std::move(top));
        Append(kids, cursor);
        Append(kids, tooltip);
        SceneNode gui;
        gui.z = "999";
        gui.layer = "gui";
        kids.push_back(std::move(gui));
        Container(dst, "top", 0, 0, w, h, z, std::move(kids));
    }

    void Backdrop(SceneBuilder& b, float w, float h, bool panorama) {
        if (!panorama) {
            SceneNode n;
            n.kind = SceneNode::Image;
            n.w = w;
            n.h = h;
            n.z = "1";
            n.src = "textures/blocks/dirt";
            n.tiled = "1";
            n.tileScale = "2";
            n.tint = "0.25 0.25 0.25";
            b.Add(std::move(n));
        }
        Rect(b, 0, 0, w, h, "#00000040", "1");
    }
}
