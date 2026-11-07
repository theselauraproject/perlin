#pragma once
#include <waila/api.hpp>

#include <algorithm>
#include <cmath>

namespace waila {
    struct Theme {
        Rgba outline, frame_top, frame_bottom, fill;

        static Theme dark() {
            return {Rgba(0x13, 0x13, 0x13), Rgba(0x38, 0x38, 0x38), Rgba(0x2e, 0x2e, 0x2e),
                    Rgba(0x13, 0x13, 0x13)};
        }
        static Theme waila() {
            return {Rgba(0x10, 0x00, 0x10), Rgba(0x50, 0x00, 0xff), Rgba(0x28, 0x00, 0x7f),
                    Rgba(0x10, 0x00, 0x10)};
        }
        static Theme create() {
            return {Rgba(0x00, 0x00, 0x00), Rgba(0x40, 0x3c, 0x37), Rgba(0x30, 0x2d, 0x2a),
                    Rgba(0x00, 0x00, 0x00)};
        }
    };

    struct Prim {
        enum class Kind : uint8_t { Rect, Gradient, Text, Item, Image, Line, Count } kind = Kind::Rect;
        float x = 0, y = 0, w = 0, h = 0;
        Rgba a = Rgba::white, b = Rgba::white;
        std::string text;
        float scale = 1.0f;
    };

    struct Laid {
        float width = 0, height = 0;
        std::vector<Prim> prims;
        float bar_x = 0, bar_width = 0;
    };

    namespace metrics {
        inline constexpr float kBorder = 2.0f;
        inline constexpr float kPadding = 3.0f;
        inline constexpr float kIcon = 18.0f;
        inline constexpr float kIconGap = 6.0f;
        inline constexpr float kRow = 9.0f;
        inline constexpr float kPin = 6.0f;
        inline constexpr float kHeart = 8.0f;
    }

    namespace detail_layout {
        inline float text_width(const std::string& s) {
            return ui::text_width(s);
        }

        struct Size {
            float w = 0, h = 0;
        };

        inline int heart_count(const Hearts& e) {
            return std::clamp(static_cast<int>(std::ceil(e.max / 2.0f)), 1, 200);
        }

        inline Size measure(const Element& element) {
            return std::visit(
                [](const auto& e) -> Size {
                    using T = std::decay_t<decltype(e)>;
                    if constexpr (std::is_same_v<T, Text>) {
                        return {text_width(e.text), metrics::kRow};
                    } else if constexpr (std::is_same_v<T, ItemIcon>) {
                        return {e.size, std::max(e.size >= 12.0f ? e.size + 2.0f : e.size, metrics::kRow)};
                    } else if constexpr (std::is_same_v<T, Texture>) {
                        return {e.width, std::max(e.height, metrics::kRow)};
                    } else if constexpr (std::is_same_v<T, Bar>) {
                        return {e.width, e.height};
                    } else if constexpr (std::is_same_v<T, Hearts>) {
                        const int n = heart_count(e), per = std::max(1, e.per_line);
                        const int columns = std::min(n, per), rows = (n + per - 1) / per;
                        return {static_cast<float>(columns - 1) * metrics::kHeart + 9.0f,
                                static_cast<float>(rows) * 10.0f - 1.0f};
                    } else if constexpr (std::is_same_v<T, Armor>) {
                        return {9.0f * 8.0f + 9.0f, 9.0f};
                    } else if constexpr (std::is_same_v<T, Mark>) {
                        return {e.size + 1.0f, metrics::kRow};
                    } else {
                        return {e.width, 0.0f};
                    }
                },
                element);
        }

        inline Size measure(const std::vector<Element>& group) {
            Size s;
            for (const Element& e : group) {
                const Size m = measure(e);
                s.w += m.w;
                s.h = std::max(s.h, m.h);
            }
            return s;
        }

        inline void push_rect(Laid& out, float x, float y, float w, float h, Rgba c) {
            if (w <= 0.0f || h <= 0.0f || c.a == 0)
                return;
            Prim p;
            p.kind = Prim::Kind::Rect;
            p.x = x, p.y = y, p.w = w, p.h = h, p.a = c;
            out.prims.push_back(std::move(p));
        }
        inline void push_gradient(Laid& out, float x, float y, float w, float h, Rgba top, Rgba bottom) {
            if (w <= 0.0f || h <= 0.0f)
                return;
            Prim p;
            p.kind = Prim::Kind::Gradient;
            p.x = x, p.y = y, p.w = w, p.h = h, p.a = top, p.b = bottom;
            out.prims.push_back(std::move(p));
        }
        inline void push_line(Laid& out, float x0, float y0, float x1, float y1, Rgba c,
                              float thickness = 1.0f) {
            Prim p;
            p.kind = Prim::Kind::Line;
            p.x = x0, p.y = y0, p.w = x1, p.h = y1, p.a = c, p.scale = thickness;
            out.prims.push_back(std::move(p));
        }

        inline float place(Laid& out, const Element& element, float x, float y, float height) {
            const Size m = measure(element);
            const float top = y + (height - m.h) * 0.5f;
            std::visit(
                [&](const auto& e) {
                    using T = std::decay_t<decltype(e)>;
                    if constexpr (std::is_same_v<T, Text>) {
                        Prim p;
                        p.kind = Prim::Kind::Text;
                        p.x = x, p.y = y + (height - metrics::kRow) * 0.5f, p.a = e.color, p.text = e.text;
                        out.prims.push_back(std::move(p));
                    } else if constexpr (std::is_same_v<T, ItemIcon>) {
                        Prim p;
                        p.kind = Prim::Kind::Item;
                        p.x = x, p.y = top, p.w = e.size, p.h = e.size, p.text = e.id;
                        out.prims.push_back(std::move(p));
                        if (e.count > 1) {
                            Prim n;
                            n.kind = Prim::Kind::Text;
                            const std::string count = std::to_string(e.count);
                            n.scale = e.size >= 16.0f ? 1.0f : 0.5f;
                            n.x = x + e.size - text_width(count) * n.scale + 1.0f;
                            n.y = top + e.size - metrics::kRow * n.scale + 1.0f;
                            n.text = count;
                            out.prims.push_back(std::move(n));
                        }
                    } else if constexpr (std::is_same_v<T, Texture>) {
                        Prim p;
                        p.kind = Prim::Kind::Image;
                        p.x = x, p.y = top, p.w = e.width, p.h = e.height, p.text = e.path;
                        out.prims.push_back(std::move(p));
                    } else if constexpr (std::is_same_v<T, Bar>) {
                        push_rect(out, x, top, e.width, e.height, Rgba(0x1c, 0x1c, 0x1c, 255));
                        push_rect(out, x + 1, top + 1,
                                  std::max(0.0f, (e.width - 2.0f) * std::clamp(e.fraction, 0.0f, 1.0f)),
                                  e.height - 2.0f, e.fill);
                        push_rect(out, x, top, e.width, 1, Rgba(0x55, 0x55, 0x55));
                        push_rect(out, x, top + e.height - 1, e.width, 1, Rgba(0x55, 0x55, 0x55));
                        push_rect(out, x, top + 1, 1, e.height - 2, Rgba(0x55, 0x55, 0x55));
                        push_rect(out, x + e.width - 1, top + 1, 1, e.height - 2, Rgba(0x55, 0x55, 0x55));
                        if (!e.label.empty()) {
                            Prim t;
                            t.kind = Prim::Kind::Text;
                            t.text = e.label;
                            t.x = x + (e.width - text_width(e.label)) * 0.5f;
                            t.y = top + (e.height - metrics::kRow) * 0.5f + 0.5f;
                            out.prims.push_back(std::move(t));
                        }
                    } else if constexpr (std::is_same_v<T, Hearts>) {
                        const int n = heart_count(e), per = std::max(1, e.per_line);
                        for (int i = 0; i < n; i++) {
                            const float hx = x + static_cast<float>(i % per) * metrics::kHeart,
                                        hy = top + static_cast<float>(i / per) * 10.0f;
                            Prim back;
                            back.kind = Prim::Kind::Image;
                            back.x = hx, back.y = hy, back.w = 9, back.h = 9,
                            back.text = "textures/ui/heart_background";
                            out.prims.push_back(back);
                            const float twice = static_cast<float>(i) * 2.0f;
                            if (e.health >= twice + 2.0f - 0.01f || e.health >= twice + 1.0f) {
                                back.text = e.health >= twice + 2.0f - 0.01f ? "textures/ui/heart"
                                                                             : "textures/ui/heart_half";
                                out.prims.push_back(back);
                            }
                        }
                    } else if constexpr (std::is_same_v<T, Armor>) {
                        for (int i = 0; i < 10; i++) {
                            const float twice = static_cast<float>(i) * 2.0f;
                            Prim p;
                            p.kind = Prim::Kind::Image;
                            p.x = x + static_cast<float>(i) * 8.0f, p.y = top, p.w = 9, p.h = 9;
                            p.text = e.points >= twice + 2.0f   ? "textures/ui/armor_full"
                                     : e.points >= twice + 1.0f ? "textures/ui/armor_half"
                                                                : "textures/ui/armor_empty";
                            out.prims.push_back(std::move(p));
                        }
                    } else if constexpr (std::is_same_v<T, Mark>) {
                        const float s = e.size;
                        const float mx = x + 0.5f, my = y + (height - s) * 0.5f + e.drop;
                        if (e.ok) {
                            const Rgba green(0x55, 0xff, 0x55);
                            push_line(out, mx, my + s * 0.55f, mx + s * 0.35f, my + s, green);
                            push_line(out, mx + s * 0.35f, my + s, mx + s, my, green);
                        } else {
                            const Rgba red(0xff, 0x55, 0x55);
                            push_line(out, mx, my, mx + s, my + s, red);
                            push_line(out, mx + s, my, mx, my + s, red);
                        }
                    }
                },
                element);
            return m.w;
        }
    }

    inline std::string signature(const Tooltip& tip) {
        std::string out;
        const auto add = [&out](const auto&... parts) { ((out += parts, out += '\x1f'), ...); };
        if (tip.icon)
            add(tip.icon->id, std::to_string(tip.icon->count));
        for (const auto& stage : tip.rows)
            for (const Row& r : stage)
                for (const auto* group : {&r.left, &r.right}) {
                    out += '|';
                    for (const Element& e : *group)
                        std::visit(
                            [&](const auto& v) {
                                using T = std::decay_t<decltype(v)>;
                                if constexpr (std::is_same_v<T, Text>)
                                    add('t', v.text, std::to_string(v.color.packed()));
                                else if constexpr (std::is_same_v<T, ItemIcon>)
                                    add('i', v.id, std::to_string(v.count), std::to_string(v.size));
                                else if constexpr (std::is_same_v<T, Texture>)
                                    add('x', v.path);
                                else if constexpr (std::is_same_v<T, Bar>)
                                    add('b', std::to_string(v.fraction), v.label,
                                        std::to_string(v.fill.packed()));
                                else if constexpr (std::is_same_v<T, Hearts>)
                                    add('h', std::to_string(v.health), std::to_string(v.max));
                                else if constexpr (std::is_same_v<T, Armor>)
                                    add('a', std::to_string(v.points));
                                else if constexpr (std::is_same_v<T, Mark>)
                                    add('m', v.ok ? "1" : "0");
                                else
                                    add('g', std::to_string(v.width));
                            },
                            e);
                }
        return out;
    }

    inline Laid layout(const Tooltip& tip, const Theme& theme, bool show_icon) {
        using namespace detail_layout;
        Laid out;

        std::vector<const Row*> rows;
        for (const auto& stage : tip.rows)
            for (const Row& r : stage)
                if (!r.empty())
                    rows.push_back(&r);

        const bool icon = show_icon && tip.icon.has_value();
        float content_w = 0, content_h = 0;
        std::vector<float> heights(rows.size());
        for (size_t i = 0; i < rows.size(); i++) {
            const Size l = measure(rows[i]->left), r = measure(rows[i]->right);
            heights[i] = std::max({l.h, r.h, metrics::kRow});
            content_w = std::max(content_w, l.w + r.w + (r.w > 0.0f && l.w > 0.0f ? metrics::kPin : 0.0f));
            content_h += heights[i];
        }
        const float lead = icon ? metrics::kIcon + metrics::kIconGap : 0.0f;
        const float inner_h = std::max(content_h, icon ? metrics::kIcon : 0.0f);
        const float edge = metrics::kBorder + metrics::kPadding;
        out.width = std::ceil(edge * 2.0f + lead + content_w);
        out.height = std::ceil(edge * 2.0f + inner_h);

        const float W = out.width, H = out.height;
        push_rect(out, 2, 2, W - 4, H - 4, theme.fill);
        push_rect(out, 1, 0, W - 2, 1, theme.outline);
        push_rect(out, 1, H - 1, W - 2, 1, theme.outline);
        push_rect(out, 0, 1, 1, H - 2, theme.outline);
        push_rect(out, W - 1, 1, 1, H - 2, theme.outline);
        push_rect(out, 1, 1, W - 2, 1, theme.frame_top);
        push_rect(out, 1, H - 2, W - 2, 1, theme.frame_bottom);
        push_gradient(out, 1, 2, 1, H - 4, theme.frame_top, theme.frame_bottom);
        push_gradient(out, W - 2, 2, 1, H - 4, theme.frame_top, theme.frame_bottom);

        const float x0 = edge, y0 = edge;
        if (icon) {
            Prim p;
            p.kind = Prim::Kind::Item;
            p.x = x0 + 1.0f, p.y = y0 + (inner_h - 16.0f) * 0.5f, p.w = 16, p.h = 16, p.text = tip.icon->id;
            const float icon_x = p.x, icon_y = p.y;
            out.prims.push_back(std::move(p));
            if (tip.icon->count > 1) {
                Prim n;
                n.kind = Prim::Kind::Text;
                n.text = std::to_string(tip.icon->count);
                n.x = icon_x + 17.0f - text_width(n.text);
                n.y = icon_y + 8.0f;
                out.prims.push_back(std::move(n));
            }
        }

        float y = y0 + (inner_h - content_h) * 0.5f;
        const float tx = x0 + lead;
        for (size_t i = 0; i < rows.size(); i++) {
            float x = tx;
            for (const Element& e : rows[i]->left)
                x += place(out, e, x, y, heights[i]);
            const float right_w = measure(rows[i]->right).w;
            x = tx + content_w - right_w;
            for (const Element& e : rows[i]->right)
                x += place(out, e, x, y, heights[i]);
            y += heights[i];
        }

        out.bar_x = 3.0f;
        out.bar_width = W - 6.0f;
        return out;
    }

    inline void paint(ui::Canvas& g, const Laid& laid, float x, float y, float scale, float alpha,
                      float opacity, float breaking) {
        const auto fade = [&](Rgba c, float extra = 1.0f) {
            c.a = static_cast<uint8_t>(std::clamp(static_cast<float>(c.a) * alpha * extra, 0.0f, 255.0f));
            return c;
        };
        size_t frame_prims = 0;
        for (const Prim& p : laid.prims) {
            const float extra = frame_prims < 9 ? opacity : 1.0f;
            frame_prims++;
            switch (p.kind) {
            case Prim::Kind::Rect:
                g.rect(x + p.x * scale, y + p.y * scale, p.w * scale, p.h * scale, fade(p.a, extra));
                break;
            case Prim::Kind::Gradient:
                g.gradient(x + p.x * scale, y + p.y * scale, p.w * scale, p.h * scale, fade(p.a, extra),
                           fade(p.b, extra));
                break;
            case Prim::Kind::Line:
                g.line(x + p.x * scale, y + p.y * scale, x + p.w * scale, y + p.h * scale, fade(p.a),
                       std::max(1.0f, p.scale * scale));
                break;
            case Prim::Kind::Text:
                if (alpha > 0.04f)
                    g.text(x + p.x * scale, y + p.y * scale, p.text,
                           {.color = fade(p.a), .scale = p.scale * scale});
                break;
            case Prim::Kind::Item:
                if (alpha > 0.04f)
                    g.item(x + p.x * scale, y + p.y * scale, p.text, p.w * scale);
                break;
            case Prim::Kind::Image:
                if (alpha > 0.04f)
                    g.image(x + p.x * scale, y + p.y * scale, p.w * scale, p.h * scale, p.text,
                            fade(Rgba::white));
                break;
            default:
                break;
            }
        }
        if (breaking > 0.0f)
            g.rect(x + laid.bar_x * scale, y + (laid.height - 3.0f) * scale,
                   laid.bar_width * std::clamp(breaking, 0.0f, 1.0f) * scale, 1.0f * scale,
                   fade(Rgba(255, 255, 255, 190)));
    }
}
