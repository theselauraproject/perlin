#include "client/ui/SceneKit.hpp"

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
}
