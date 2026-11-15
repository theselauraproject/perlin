#include "client/ui/PluginLayer.hpp"
#include "client/ui/UiTemplate.hpp"

#include "client/systems/text/FormatCodes.hpp"
#include "client/ui/SceneKit.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace perlin::ui {
    namespace {
        std::string Color(uint32_t packed) {
            const int r = static_cast<int>(packed >> 24) & 255, g = static_cast<int>(packed >> 16) & 255,
                      b = static_cast<int>(packed >> 8) & 255, a = static_cast<int>(packed) & 255;
            return "rgba(" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + "," +
                   std::to_string(a) + ")";
        }

        bool Valid(float v) {
            return std::isfinite(v) && std::fabs(v) < 100000.0f;
        }

        void Emit(const mods::UiLayer& layer, SceneBuilder& out) {
            abi::Reader r(layer.commands.data(), layer.commands.size());
            int budget = 4000;
            while (r.Remaining() > 0 && r.Ok() && budget-- > 0) {
                const auto kind = static_cast<abi::Draw>(r.get<uint8_t>());
                if (kind == abi::Draw::Rect || kind == abi::Draw::Gradient || kind == abi::Draw::Border ||
                    kind == abi::Draw::Image) {
                    const float x = r.get<float>(), y = r.get<float>(), w = r.get<float>(),
                                h = r.get<float>();
                    if (kind == abi::Draw::Rect) {
                        const uint32_t c = r.get<uint32_t>();
                        if (r.Ok() && Valid(x) && Valid(y) && Valid(w) && Valid(h) && (c & 255))
                            kit::Rect(out, x, y, w, h, Color(c));
                    } else if (kind == abi::Draw::Gradient) {
                        const uint32_t top = r.get<uint32_t>(), bottom = r.get<uint32_t>();
                        if (r.Ok() && Valid(x) && Valid(y) && Valid(w) && Valid(h))
                            kit::Gradient(out, x, y, w, h,
                                          "vertical-gradient(" + Color(top) + " " + Color(bottom) + ")");
                    } else if (kind == abi::Draw::Border) {
                        const uint32_t c = r.get<uint32_t>();
                        const float t = std::max(1.0f, r.get<float>());
                        if (r.Ok() && Valid(x) && Valid(y) && Valid(w) && Valid(h) && (c & 255)) {
                            const std::string bg = Color(c);
                            kit::Rect(out, x, y, w, t, bg);
                            kit::Rect(out, x, y + h - t, w, t, bg);
                            kit::Rect(out, x, y + t, t, std::max(0.0f, h - 2 * t), bg);
                            kit::Rect(out, x + w - t, y + t, t, std::max(0.0f, h - 2 * t), bg);
                        }
                    } else {
                        const uint32_t tint = r.get<uint32_t>();
                        const std::string path = r.str();
                        (void)tint;
                        if (r.Ok() && Valid(x) && Valid(y) && Valid(w) && Valid(h) && !path.empty() &&
                            path.size() < 256 && !path.contains("..") && !path.contains("://"))
                            kit::Image(out, x, y, w, h, path);
                    }
                } else if (kind == abi::Draw::Line) {
                    const float x0 = r.get<float>(), y0 = r.get<float>(), x1 = r.get<float>(),
                                y1 = r.get<float>();
                    const uint32_t c = r.get<uint32_t>();
                    const float t = std::max(1.0f, r.get<float>());
                    if (!r.Ok() || !Valid(x0) || !Valid(y0) || !Valid(x1) || !Valid(y1) || !(c & 255))
                        continue;
                    const std::string bg = Color(c);
                    if (std::fabs(y1 - y0) < 0.5f) {
                        kit::Rect(out, std::min(x0, x1), y0 - t / 2, std::fabs(x1 - x0), t, bg);
                    } else if (std::fabs(x1 - x0) < 0.5f) {
                        kit::Rect(out, x0 - t / 2, std::min(y0, y1), t, std::fabs(y1 - y0), bg);
                    } else {
                        const float len = std::hypot(x1 - x0, y1 - y0);
                        const int steps = std::clamp(static_cast<int>(len / t), 1, 200);
                        for (int i = 0; i <= steps; i++) {
                            const float u = static_cast<float>(i) / static_cast<float>(steps);
                            kit::Rect(out, x0 + (x1 - x0) * u - t / 2, y0 + (y1 - y0) * u - t / 2, t, t, bg);
                        }
                    }
                } else if (kind == abi::Draw::Text) {
                    const float x = r.get<float>(), y = r.get<float>();
                    const uint32_t c = r.get<uint32_t>();
                    const float scale = std::clamp(r.get<float>(), 0.25f, 16.0f);
                    const bool shadow = r.get<uint8_t>() != 0;
                    const uint8_t align = r.get<uint8_t>();
                    const std::string text = r.str();
                    if (!r.Ok() || !Valid(x) || !Valid(y) || text.empty() || text.size() > 2000)
                        continue;
                    const float boxW = 2000.0f;
                    const float left = align == 1 ? x - boxW / 2 : align == 2 ? x - boxW : x;
                    const char* alignName = align == 1 ? "center" : align == 2 ? "right" : "left";
                    SceneNode n;
                    n.kind = SceneNode::Text;
                    n.x = left;
                    n.y = y;
                    n.w = boxW;
                    n.h = 9 * scale;
                    n.family = shadow ? "minecraft-shadow" : "minecraft";
                    n.size = 8 * scale;
                    n.lineHeight = 9 * scale;
                    n.align = alignName;
                    n.color = Color(c);
                    n.runs = kit::Formatted(text);
                    out.Add(std::move(n));
                } else if (kind == abi::Draw::Item) {
                    const float x = r.get<float>(), y = r.get<float>(),
                                size = std::clamp(r.get<float>(), 1.0f, 512.0f);
                    const std::string name = r.str();
                    if (!r.Ok() || !Valid(x) || !Valid(y) || name.empty() || name.size() > 128)
                        continue;
                    kit::Item(out, x, y, size, size, name + ";0");
                } else {
                    break;
                }
            }
        }
    }

    namespace {
        const ScreenTemplateDefault kPluginLayerDefault("ui/overlays/plugin_layer.rml");
    }

    SceneDoc BuildPluginLayer(const std::vector<mods::UiLayer>& layers, int guiW, int guiH) {
        SceneDoc doc;
        doc.rml = ScreenTemplate("ui/overlays/plugin_layer.rml");
        doc.scene.vars["w"] = std::to_string(guiW);
        doc.scene.vars["h"] = std::to_string(guiH);
        SceneBuilder out(doc.scene.lists["body"]);
        for (const mods::UiLayer& layer : layers)
            Emit(layer, out);
        return doc;
    }
}
