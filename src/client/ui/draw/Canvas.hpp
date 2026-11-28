#pragma once
#include "client/systems/gfx/RenderContext.hpp"
#include "client/ui/draw/Color.hpp"
#include "client/ui/draw/Text.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace perlin::ui::draw {
    struct TexInfo {
        uint32_t handle = 0;
        int w = 0, h = 0;
    };

    class TextureSource {
    public:
        virtual ~TextureSource() = default;
        virtual TexInfo Get(const std::string& key) = 0;
        virtual int Device() const = 0;
    };

    struct Batch {
        uint32_t texture = 0;
        bool glint = false;
        UiLayer layer = UiLayer::Gui;
        bool scissor = false;
        int rect[4] = {0, 0, 0, 0};
        int shift = -1;
        std::vector<UiVertex> vertices;
        std::vector<uint32_t> indices;
        uint32_t mesh = 0;
    };

    struct ImageSpec {
        std::string src;
        float x = 0, y = 0, w = 0, h = 0;
        float uv[4] = {0, 0, -1, -1};
        bool hasUv = false;
        bool nine = false;
        float slice[4] = {0, 0, 0, 0};
        int tiled = 0;
        float tileScale = 1;
        std::string clip;
        Color tint;
        float alpha = 1;
        bool keep = false;
    };

    class Canvas final : public TextSink {
    public:
        Canvas(TextureSource& textures, std::vector<Batch>& out);

        int Device() const override { return device; }
        uint32_t Texture(const std::string& key) override { return tex.Get(key).handle; }
        void Quad(uint32_t texture, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                  uint32_t color) override;
        void Skewed(uint32_t texture, const float xy[8], const float uv[8], uint32_t color) override;

        float Snap(float v) const { return std::round(v * static_cast<float>(device)) / static_cast<float>(device); }
        void Solid(float x, float y, float w, float h, Color c);
        void Gradient(float x, float y, float w, float h, bool vertical, Color a, Color b);
        void Image(const ImageSpec& spec);
        void Item(const std::string& key, float x, float y, float w, float h, float alpha, bool glint);
        float Text(const Face& face, const std::string& text, float x, float baseline, Color color);

        void PushClip(float x0, float y0, float x1, float y1);
        void PopClip();
        bool ClipEmpty() const;
        void SetLayer(UiLayer l) { layer = l; }
        UiLayer Layer() const { return layer; }
        void SetShift(int s) { shift = s; }
        int Shift() const { return shift; }
        bool Starved() const { return starved; }

    private:
        struct Scissor {
            bool on;
            int r[4];
        };
        Batch& Current(uint32_t texture, bool glint);
        static void Append(Batch& b, const std::array<float, 8>& xy, const std::array<float, 8>& uv, uint32_t color);

        TextureSource& tex;
        std::vector<Batch>& out;
        int device;
        UiLayer layer = UiLayer::Gui;
        int shift = -1;
        bool starved = false;
        std::vector<Scissor> scissors;
    };
}
