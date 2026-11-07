#include "client/ui/PluginLayer.hpp"
#include "client/ui/UiTemplate.hpp"

#include "client/ui/FormatCodes.hpp"
#include "client/ui/RmlEmit.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace perlin::ui {
    namespace {
        std::string F(float v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
            return buf;
        }

        std::string Color(uint32_t packed) {
            const int r = static_cast<int>(packed >> 24) & 255, g = static_cast<int>(packed >> 16) & 255,
                      b = static_cast<int>(packed >> 8) & 255, a = static_cast<int>(packed) & 255;
            return "rgba(" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + "," +
                   std::to_string(a) + ")";
        }

        std::string Box(float x, float y, float w, float h, const std::string& style) {
            return "<div style=\"left:" + F(x) + "px;top:" + F(y) + "px;width:" + F(w) + "px;height:" + F(h) +
                   "px;" + style + "\"></div>\n";
        }

        bool Valid(float v) {
            return std::isfinite(v) && std::fabs(v) < 100000.0f;
        }

        void Emit(const mods::UiLayer& layer, std::string& out) {
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
                            out += Box(x, y, w, h, "background-color:" + Color(c) + ";");
                    } else if (kind == abi::Draw::Gradient) {
                        const uint32_t top = r.get<uint32_t>(), bottom = r.get<uint32_t>();
                        if (r.Ok() && Valid(x) && Valid(y) && Valid(w) && Valid(h))
                            out +=
                                Box(x, y, w, h,
                                    "decorator:vertical-gradient(" + Color(top) + " " + Color(bottom) + ");");
                    } else if (kind == abi::Draw::Border) {
                        const uint32_t c = r.get<uint32_t>();
                        const float t = std::max(1.0f, r.get<float>());
                        if (r.Ok() && Valid(x) && Valid(y) && Valid(w) && Valid(h) && (c & 255)) {
                            const std::string bg = "background-color:" + Color(c) + ";";
                            out += Box(x, y, w, t, bg);
                            out += Box(x, y + h - t, w, t, bg);
                            out += Box(x, y + t, t, std::max(0.0f, h - 2 * t), bg);
                            out += Box(x + w - t, y + t, t, std::max(0.0f, h - 2 * t), bg);
                        }
                    } else {
                        const uint32_t tint = r.get<uint32_t>();
                        const std::string path = r.str();
                        (void)tint;
                        if (r.Ok() && Valid(x) && Valid(y) && Valid(w) && Valid(h) && !path.empty() &&
                            path.size() < 256 && !path.contains("..") && !path.contains("://"))
                            out += "<mcimg src=\"" + EscapeRml(path) + "\" style=\"left:" + F(x) +
                                   "px;top:" + F(y) + "px;width:" + F(w) + "px;height:" + F(h) +
                                   "px;\"></mcimg>\n";
                    }
                } else if (kind == abi::Draw::Line) {
                    const float x0 = r.get<float>(), y0 = r.get<float>(), x1 = r.get<float>(),
                                y1 = r.get<float>();
                    const uint32_t c = r.get<uint32_t>();
                    const float t = std::max(1.0f, r.get<float>());
                    if (!r.Ok() || !Valid(x0) || !Valid(y0) || !Valid(x1) || !Valid(y1) || !(c & 255))
                        continue;
                    const std::string bg = "background-color:" + Color(c) + ";";
                    if (std::fabs(y1 - y0) < 0.5f) {
                        out += Box(std::min(x0, x1), y0 - t / 2, std::fabs(x1 - x0), t, bg);
                    } else if (std::fabs(x1 - x0) < 0.5f) {
                        out += Box(x0 - t / 2, std::min(y0, y1), t, std::fabs(y1 - y0), bg);
                    } else {
                        const float len = std::hypot(x1 - x0, y1 - y0);
                        const int steps = std::clamp(static_cast<int>(len / t), 1, 200);
                        for (int i = 0; i <= steps; i++) {
                            const float u = static_cast<float>(i) / static_cast<float>(steps);
                            out += Box(x0 + (x1 - x0) * u - t / 2, y0 + (y1 - y0) * u - t / 2, t, t, bg);
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
                    out += "<div style=\"left:" + F(left) + "px;top:" + F(y) + "px;width:" + F(boxW) +
                           "px;height:" + F(9 * scale) +
                           "px;font-family:" + (shadow ? "minecraft-shadow" : "minecraft") +
                           ";font-size:" + F(8 * scale) + "px;line-height:" + F(9 * scale) +
                           "px;text-align:" + alignName + ";white-space:nowrap;color:" + Color(c) + ";\">" +
                           FormatCodesToRml(text) + "</div>\n";
                } else if (kind == abi::Draw::Item) {
                    const float x = r.get<float>(), y = r.get<float>(),
                                size = std::clamp(r.get<float>(), 1.0f, 512.0f);
                    const std::string name = r.str();
                    if (!r.Ok() || !Valid(x) || !Valid(y) || name.empty() || name.size() > 128)
                        continue;
                    out += "<mcitem key=\"" + EscapeRml(name + ";0") + "\" style=\"left:" + F(x) +
                           "px;top:" + F(y) + "px;width:" + F(size) + "px;height:" + F(size) +
                           "px;\"></mcitem>\n";
                } else {
                    break;
                }
            }
        }
    }

    namespace {
        const char* kPluginLayerHead =
            "<rml>\n<head>\n<title>plugins</title>\n<style>\n"
            "body { position: absolute; left: 0px; top: 0px; width: {{w}}px; height: {{h}}px; margin: 0; "
            "padding: 0; background: transparent; overflow: hidden; }\n"
            "div, mcimg, mcitem { display: block; position: absolute; }\n</style>\n</head>\n<body>\n";
        const ScreenTemplateDefault kPluginLayerDefault("plugin_layer.rml", kPluginLayerHead);
    }

    std::string BuildPluginLayerRml(const std::vector<mods::UiLayer>& layers, int guiW, int guiH) {
        std::string out = ScreenTemplate("plugin_layer.rml", kPluginLayerHead);
        ReplaceAll(out, "w", std::to_string(guiW));
        ReplaceAll(out, "h", std::to_string(guiH));
        for (const mods::UiLayer& layer : layers)
            Emit(layer, out);
        out += "</body>\n</rml>\n";
        return out;
    }
}
