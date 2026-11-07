#pragma once
#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "perlin/rpc.hpp"
#include "perlin/types.hpp"

namespace perlin::ui {
    enum class Align : uint8_t { Left = 0, Center = 1, Right = 2 };

    struct TextStyle {
        Rgba color = Rgba::white;
        float scale = 1.0f;
        bool shadow = true;
        Align align = Align::Left;
    };

    class Canvas {
    public:
        Canvas(int width, int height) : w(width), h(height) {}

        int width() const { return w; }
        int height() const { return h; }

        Canvas& rect(float x, float y, float width, float height, Rgba color) {
            begin(abi::Draw::Rect, x, y, width, height).put(color.packed());
            return *this;
        }

        Canvas& gradient(float x, float y, float width, float height, Rgba top, Rgba bottom) {
            begin(abi::Draw::Gradient, x, y, width, height).put(top.packed()).put(bottom.packed());
            return *this;
        }

        Canvas& border(float x, float y, float width, float height, Rgba color, float thickness = 1.0f) {
            begin(abi::Draw::Border, x, y, width, height).put(color.packed()).put(thickness);
            return *this;
        }

        Canvas& line(float x0, float y0, float x1, float y1, Rgba color, float thickness = 1.0f) {
            buffer.put<uint8_t>(static_cast<uint8_t>(abi::Draw::Line))
                .put(x0)
                .put(y0)
                .put(x1)
                .put(y1)
                .put(color.packed())
                .put(thickness);
            return *this;
        }

        Canvas& text(float x, float y, std::string_view value, const TextStyle& style = {}) {
            buffer.put<uint8_t>(static_cast<uint8_t>(abi::Draw::Text))
                .put(x)
                .put(y)
                .put(style.color.packed())
                .put(style.scale)
                .put<uint8_t>(style.shadow ? 1 : 0)
                .put<uint8_t>(static_cast<uint8_t>(style.align))
                .str(value);
            return *this;
        }

        template <class... Args>
        Canvas& text(float x, float y, const TextStyle& style, std::format_string<Args...> fmt,
                     Args&&... args) {
            return text(x, y, std::format(fmt, std::forward<Args>(args)...), style);
        }

        Canvas& item(float x, float y, std::string_view itemName, float size = 16.0f) {
            buffer.put<uint8_t>(static_cast<uint8_t>(abi::Draw::Item)).put(x).put(y).put(size).str(itemName);
            return *this;
        }

        Canvas& image(float x, float y, float width, float height, std::string_view path,
                      Rgba tint = Rgba::white) {
            begin(abi::Draw::Image, x, y, width, height).put(tint.packed()).str(path);
            return *this;
        }

        const abi::Writer& commands() const { return buffer; }
        bool empty() const { return buffer.Size() == 0; }

    private:
        abi::Writer& begin(abi::Draw kind, float x, float y, float width, float height) {
            buffer.put<uint8_t>(static_cast<uint8_t>(kind)).put(x).put(y).put(width).put(height);
            return buffer;
        }

        int w, h;
        abi::Writer buffer;
    };

    inline float text_width(std::string_view value, float scale = 1.0f) {
        abi::Writer w;
        w.str(value).put(scale);
        auto bytes = detail::call(Op::UiMeasure, w);
        if (!bytes || bytes->size() < sizeof(float))
            return static_cast<float>(value.size()) * 6.0f * scale;
        abi::Reader r(bytes->data(), bytes->size());
        return r.get<float>();
    }

    inline Result<void> submit(std::string_view layer, int z, const Canvas& canvas) {
        abi::Writer w;
        w.str(layer).put<int32_t>(z).bytes(canvas.commands().Data(), canvas.commands().Size());
        return detail::call_void(Op::UiSubmit, w);
    }

    inline Result<void> clear(std::string_view layer) {
        abi::Writer w;
        w.str(layer);
        return detail::call_void(Op::UiClear, w);
    }
}
