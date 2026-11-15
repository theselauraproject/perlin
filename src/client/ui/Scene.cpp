#include "client/ui/Scene.hpp"

#include "client/systems/text/FormatCodes.hpp"

#include <utility>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace perlin::ui {
    SceneNode& SceneBuilder::Add(SceneNode node) {
        node.x += shiftX;
        node.y += shiftY;
        Current().push_back(std::move(node));
        return Current().back();
    }

    SceneNode& SceneBuilder::Begin(SceneNode node) {
        if (Depth() >= kSceneMaxDepth) {
            const float bx = node.x, by = node.y;
            SceneNode& added = Add(std::move(node));
            stack.push_back(stack.back());
            flat.push_back(1);
            shifts.emplace_back(bx, by);
            shiftX += bx;
            shiftY += by;
            return added;
        }
        SceneNode& added = Add(std::move(node));
        stack.push_back(&added.children);
        flat.push_back(0);
        return added;
    }

    void SceneBuilder::End() {
        if (stack.size() <= 1)
            return;
        if (flat.back()) {
            shiftX -= shifts.back().first;
            shiftY -= shifts.back().second;
            shifts.pop_back();
        }
        stack.pop_back();
        flat.pop_back();
    }

    std::string SceneNumber(float v) {
        char buffer[32];
        if (std::fabs(v - std::round(v)) < 0.0005f)
            std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(v)));
        else
            std::snprintf(buffer, sizeof(buffer), "%.3f", v);
        return buffer;
    }

    std::vector<SceneRun> ParseFormatRuns(const std::string& text, const std::string& baseColor, float alpha,
                                       bool breaks) {
        std::vector<SceneRun> out;
        const int alphaByte = std::clamp(static_cast<int>(std::lround(alpha * 255.0f)), 0, 255);
        for (const FormatRun& run : ParseFormatCodes(text, breaks)) {
            SceneRun r;
            if (run.lineBreak) {
                r.text = "\n";
                r.color = baseColor;
                r.colored = baseColor.empty() ? "" : "1";
                out.push_back(std::move(r));
                continue;
            }
            r.text = run.text;
            r.color = baseColor;
            if (!baseColor.empty())
                r.colored = "1";
            if (run.hasColor) {
                r.colored = "1";
                char buf[16];
                std::snprintf(buf, sizeof buf, "#%06x%02x", run.rgb & 0xFFFFFFu, alphaByte);
                r.color = buf;
            }
            if (run.bold)
                r.weight = "bold";
            if (run.italic)
                r.style = "italic";
            out.push_back(std::move(r));
        }
        return out;
    }

    std::vector<SceneRun> ParseFormatLines(const std::string& text, const std::string& baseColor) {
        std::vector<SceneRun> out;
        size_t start = 0;
        while (start <= text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            if (start > 0) {
                SceneRun br;
                br.text = "\n";
                br.color = baseColor;
                br.colored = baseColor.empty() ? "" : "1";
                out.push_back(std::move(br));
            }
            for (SceneRun& run : ParseFormatRuns(text.substr(start, end - start), baseColor, 1.0f))
                out.push_back(std::move(run));
            start = end + 1;
        }
        return out;
    }

    void EnsureSceneLists(SceneDoc& doc) {
        constexpr std::string_view kToken = "{{scene:";
        for (size_t at = doc.rml.find(kToken); at != std::string::npos; at = doc.rml.find(kToken, at)) {
            const size_t end = doc.rml.find("}}", at);
            if (end == std::string::npos)
                break;
            doc.scene.lists.try_emplace(doc.rml.substr(at + kToken.size(), end - at - kToken.size()));
            at = end;
        }
    }

    namespace {
        template <class Fn>
        bool AnyNode(const std::vector<SceneNode>& nodes, const Fn& fn) {
            for (const SceneNode& n : nodes)
                if (fn(n) || AnyNode(n.children, fn))
                    return true;
            return false;
        }

        template <class Fn>
        bool AnyScene(const SceneDoc& doc, const Fn& fn) {
            if (AnyNode(doc.scene.nodes, fn))
                return true;
            for (const auto& [name, list] : doc.scene.lists)
                if (AnyNode(list, fn))
                    return true;
            return false;
        }

        std::string Runs(const std::vector<SceneRun>& runs) {
            std::string out;
            for (const SceneRun& r : runs)
                out += r.text;
            return out;
        }

        void DumpNodes(const std::vector<SceneNode>& nodes, int depth, std::string& out) {
            for (const SceneNode& n : nodes) {
                out += std::string(static_cast<size_t>(depth) * 2, ' ');
                out += "kind=" + std::to_string(n.kind);
                if (!n.id.empty())
                    out += " id=" + n.id;
                if (!n.cls.empty())
                    out += " cls=" + n.cls;
                out += " x=" + SceneNumber(n.x) + " y=" + SceneNumber(n.y) + " w=" + SceneNumber(n.w) +
                       " h=" + SceneNumber(n.h) + " z=" + n.z;
                if (!n.src.empty())
                    out += " src=" + n.src;
                if (!n.key.empty())
                    out += " key=" + n.key;
                const std::pair<const char*, const std::string*> attributes[] = {
                    {"uv", &n.uv},         {"nine", &n.nine},   {"tiled", &n.tiled}, {"tilescale", &n.tileScale},
                    {"clip", &n.clipDirection}, {"tint", &n.tint}, {"alpha", &n.alpha}, {"keep", &n.keep},
                    {"glint", &n.glint},   {"snd", &n.snd},     {"vol", &n.vol},     {"pit", &n.pit},
                    {"decorator", &n.decorator}};
                for (const auto& [name, value] : attributes)
                    if (!value->empty())
                        out += std::string(" ") + name + "=" + *value;
                if (n.kind == SceneNode::Text)
                    out += " font=" + n.family + " size=" + SceneNumber(n.size) + " line=" + SceneNumber(n.lineHeight) +
                           " align=" + n.align + " color=" + n.color;
                if (n.kind == SceneNode::Rect)
                    out += " color=" + n.color;
                if (n.kind == SceneNode::Slider)
                    out += " knob=" + SceneNumber(n.knob);
                if (!n.runs.empty())
                    out += " text=" + Runs(n.runs);
                out += "\n";
                DumpNodes(n.children, depth + 1, out);
            }
        }
    }

    bool SceneHasId(const SceneDoc& doc, const std::string& id, bool prefix) {
        return AnyScene(doc, [&](const SceneNode& n) {
            return prefix ? n.id.starts_with(id) : n.id == id;
        });
    }

    bool SceneHasText(const SceneDoc& doc, const std::string& needle) {
        return AnyScene(doc, [&](const SceneNode& n) { return Runs(n.runs).find(needle) != std::string::npos; });
    }

    std::string DumpScene(const SceneDoc& doc) {
        std::string out = doc.rml;
        out += "\n<!-- scene -->\n";
        DumpNodes(doc.scene.nodes, 0, out);
        for (const auto& [name, list] : doc.scene.lists) {
            out += "list " + name + "\n";
            DumpNodes(list, 1, out);
        }
        for (const auto& [name, value] : doc.scene.vars)
            out += "var " + name + "=" + value + "\n";
        return out;
    }
}
