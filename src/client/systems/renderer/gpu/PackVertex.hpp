#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace perlin::gpu {

    constexpr uint32_t kPackVertexBytes = 72;

    enum PackOffset : uint32_t {
        kPos = 0,
        kColor = 12,
        kUv = 16,
        kLight = 24,
        kNormal = 32,
        kEntity = 40,
        kMid = 48,
        kMidBlock = 56,
        kTangent = 64
    };

    struct PackMesh {
        std::vector<uint8_t> bytes;
        uint32_t count = 0;

        void Vertex(float x, float y, float z, const uint8_t rgba[4], float u, float v);
        void Quad(const float centre[3], const float right[3], const float up[3], const uint8_t rgba[4],
                  float u0, float v0, float u1, float v1);
        bool Empty() const { return count == 0; }
        size_t SizeBytes() const { return bytes.size(); }
    };

}
