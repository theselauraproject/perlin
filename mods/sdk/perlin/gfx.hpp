#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "perlin/rpc.hpp"

namespace perlin::gfx {
    using Vertex = abi::GfxVertex;
    using Command = abi::GfxCmd;
    using Display = abi::GfxDisplayInfo;

    constexpr uint32_t rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) |
               (static_cast<uint32_t>(a) << 24);
    }

    enum class Filter : bool { Nearest = false, Linear = true };

    inline Result<Display> display() {
        return detail::call_as<Display>(Op::GfxDisplay);
    }

    struct Image {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba;
    };

    inline Result<Image> load_image(std::string_view path) {
        abi::Writer a;
        a.str(path);
        auto bytes = detail::call(Op::GfxImageLoad, a);
        if (!bytes)
            return unexpected(bytes.error());
        abi::Reader r(bytes->data(), bytes->size());
        Image image;
        image.width = r.get<int32_t>();
        image.height = r.get<int32_t>();
        image.rgba = r.bytes();
        if (!r.Ok() || image.width <= 0 || image.height <= 0 ||
            image.rgba.size() != static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4)
            return unexpected(Error{Status::Failed});
        return image;
    }

    class Texture {
    public:
        Texture() = default;
        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;
        Texture(Texture&& other) noexcept : tid(std::exchange(other.tid, 0)), w(other.w), h(other.h) {}
        Texture& operator=(Texture&& other) noexcept {
            if (this != &other) {
                reset();
                tid = std::exchange(other.tid, 0);
                w = other.w;
                h = other.h;
            }
            return *this;
        }
        ~Texture() { reset(); }

        static Result<Texture> create(int width, int height, std::span<const uint8_t> rgba,
                                      Filter filter = Filter::Linear) {
            abi::Writer a;
            a.put<int32_t>(width)
                .put<int32_t>(height)
                .boolean(filter == Filter::Linear)
                .bytes(rgba.data(), rgba.size());
            auto bytes = detail::call(Op::GfxTextureCreate, a);
            if (!bytes)
                return unexpected(bytes.error());
            abi::Reader r(bytes->data(), bytes->size());
            Texture t;
            t.tid = r.get<uint32_t>();
            t.w = width;
            t.h = height;
            if (!r.Ok() || t.tid == 0)
                return unexpected(Error{Status::Failed});
            return t;
        }

        Result<void> update(int x, int y, int width, int height, std::span<const uint8_t> rgba) const {
            abi::Writer a;
            a.put<uint32_t>(tid)
                .put<int32_t>(x)
                .put<int32_t>(y)
                .put<int32_t>(width)
                .put<int32_t>(height)
                .bytes(rgba.data(), rgba.size());
            return detail::call_void(Op::GfxTextureUpdate, a);
        }

        void reset() {
            if (!tid)
                return;
            abi::Writer a;
            a.put<uint32_t>(tid);
            (void)detail::call_void(Op::GfxTextureDestroy, a);
            tid = 0;
        }

        uint32_t id() const { return tid; }
        int width() const { return w; }
        int height() const { return h; }
        explicit operator bool() const { return tid != 0; }

    private:
        uint32_t tid = 0;
        int w = 0, h = 0;
    };

    class DrawList {
    public:
        void clear() {
            vertices_.clear();
            indices_.clear();
            commands_.clear();
        }

        DrawList& batch(uint32_t texture, float x0, float y0, float x1, float y1) {
            Command c;
            c.texture = texture;
            c.clip[0] = x0;
            c.clip[1] = y0;
            c.clip[2] = x1;
            c.clip[3] = y1;
            c.indexOffset = static_cast<uint32_t>(indices_.size());
            c.vertexOffset = static_cast<uint32_t>(vertices_.size());
            commands_.push_back(c);
            return *this;
        }
        uint32_t vertex(float x, float y, float u, float v, uint32_t color) {
            vertices_.push_back({x, y, u, v, color});
            return static_cast<uint32_t>(vertices_.size() - 1 -
                                         (commands_.empty() ? 0 : commands_.back().vertexOffset));
        }
        DrawList& triangle(uint32_t a, uint32_t b, uint32_t c) {
            indices_.push_back(a + base());
            indices_.push_back(b + base());
            indices_.push_back(c + base());
            if (!commands_.empty())
                commands_.back().indexCount += 3;
            return *this;
        }
        DrawList& rect(float x0, float y0, float x1, float y1, uint32_t color, float u0 = 0, float v0 = 0,
                       float u1 = 1, float v1 = 1) {
            const uint32_t a = vertex(x0, y0, u0, v0, color), b = vertex(x1, y0, u1, v0, color),
                           c = vertex(x1, y1, u1, v1, color), d = vertex(x0, y1, u0, v1, color);
            return triangle(a, b, c).triangle(a, c, d);
        }

        std::span<const Vertex> vertices() const { return vertices_; }
        std::span<const uint32_t> indices() const { return indices_; }
        std::span<const Command> commands() const { return commands_; }
        bool empty() const { return commands_.empty(); }

    private:
        uint32_t base() const { return 0; }
        std::vector<Vertex> vertices_;
        std::vector<uint32_t> indices_;
        std::vector<Command> commands_;
    };

    template <class Index>
        requires(std::same_as<Index, uint16_t> || std::same_as<Index, uint32_t>)
    Result<void> submit(std::string_view layer, int z, std::span<const Vertex> vertices,
                        std::span<const Index> indices, std::span<const Command> commands) {
        abi::GfxHeader h;
        h.vertexCount = static_cast<uint32_t>(vertices.size());
        h.indexCount = static_cast<uint32_t>(indices.size());
        h.commandCount = static_cast<uint32_t>(commands.size());
        h.flags = sizeof(Index) == 4 ? abi::kGfxIndex32 : 0;
        abi::Writer blob(sizeof(h) + vertices.size_bytes() + indices.size_bytes() + commands.size_bytes());
        blob.raw(&h, sizeof(h))
            .raw(vertices.data(), vertices.size_bytes())
            .raw(indices.data(), indices.size_bytes())
            .raw(commands.data(), commands.size_bytes());
        abi::Writer a;
        a.str(layer).put<int32_t>(z).bytes(blob.Data(), blob.Size());
        return detail::call_void(Op::GfxSubmit, a);
    }

    inline Result<void> submit(std::string_view layer, int z, const DrawList& list) {
        return submit<uint32_t>(layer, z, list.vertices(), list.indices(), list.commands());
    }

    inline Result<void> clear(std::string_view layer) {
        abi::Writer a;
        a.str(layer);
        return detail::call_void(Op::GfxClear, a);
    }

    inline Result<abi::NativeHandles> native_handles() {
        return detail::call_as<abi::NativeHandles>(Op::NativeInfo);
    }
}
