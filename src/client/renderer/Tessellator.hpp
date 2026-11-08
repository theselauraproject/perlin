#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace perlin {
    enum class RenderLayer : uint8_t { Solid, Cutout, Translucent, CutoutCull, TranslucentCull };
    constexpr int kRenderLayerCount = 5;

    struct Vertex {
        float x, y, z;
        uint32_t rgba;
        float u, v;
        uint32_t light;

        uint32_t normal;
        float entity;
        float midU, midV;
        float tangent;
    };
    static_assert(sizeof(Vertex) == 48, "Vertex layout must match the GPU vertex layout");

    struct MeshSection {
        int sy;
        std::array<uint32_t, kRenderLayerCount> start;
        std::array<uint32_t, kRenderLayerCount> count;
    };

    struct SectionVis {
        int sy;
        uint16_t conn;
    };

    struct SpecialBlock {
        int x, y, z;
        uint16_t id;
    };

    inline uint32_t PackColor(float r, float g, float b, float a = 1.0f) {
        auto q = [](float c) -> uint32_t {
            c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
            return static_cast<uint32_t>(c * 255.0f + 0.5f);
        };
        return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
    }

    inline uint32_t PackLight(float block, float sky) {
        auto q = [](float level) -> uint32_t {
            level = level < 0.0f ? 0.0f : (level > 15.0f ? 15.0f : level);
            return static_cast<uint32_t>(level * 16.0f + 0.5f);
        };
        return q(block) | (q(sky) << 8);
    }

    struct QuadInfo {
        float bx, by, bz;
        uint16_t block;
        uint8_t layer;
    };

    class Tessellator {
    public:
        static void SetCaptureQuadInfo(bool on) { capture.store(on, std::memory_order_relaxed); }
        static bool CaptureQuadInfo() { return capture.load(std::memory_order_relaxed); }
        void SetBlock(uint16_t id, float x, float y, float z) {
            curBlock = id;
            curX = x;
            curY = y;
            curZ = z;
        }
        const std::vector<QuadInfo>& Quads() const { return quads; }

        void Begin();
        void End();
        void Reserve(size_t quads);

        void SetLayer(RenderLayer value) { layer = value; }
        void EnterSection(int sy);
        void AddVisibility(int sy, uint16_t conn) { visibility.push_back({sy, conn}); }
        const std::vector<SectionVis>& Visibility() const { return visibility; }
        void AddSpecial(int x, int y, int z, uint16_t id) { specials.push_back({x, y, z, id}); }
        std::vector<SpecialBlock>& Specials() { return specials; }

        void AddQuad(const float v[4][3], const float c[4][3], const float uv[4][2], bool flip,
                     const float light[4][2], const float* ao = nullptr, float shade = 1.0f);
        void AddQuad(const float v[4][3], const float c[4][3], const float uv[4][2], bool flip, float block,
                     float sky);

        const std::vector<Vertex>& Vertices() const { return vertices; }

        std::vector<uint8_t> packVertices;
        uint64_t packStamp = 0;
        const std::vector<uint32_t>& Indices(RenderLayer value) const {
            return indices[static_cast<size_t>(value)];
        }
        const std::vector<MeshSection>& Sections() const { return sections; }
        bool Empty() const {
            for (const auto& list : indices)
                if (!list.empty())
                    return false;
            return true;
        }

    private:
        std::vector<Vertex> vertices;
        std::array<std::vector<uint32_t>, kRenderLayerCount> indices;
        std::vector<MeshSection> sections;
        std::vector<SectionVis> visibility;
        std::vector<SpecialBlock> specials;
        std::vector<QuadInfo> quads;
        uint16_t curBlock = 0;
        float curX = 0, curY = 0, curZ = 0;
        static inline std::atomic<bool> capture{false};
        RenderLayer layer = RenderLayer::Solid;
        bool building = false;
    };
}
