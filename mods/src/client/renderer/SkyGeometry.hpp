#pragma once
#include <cstdint>
#include <vector>

namespace perlin {
    struct PositionVertex {
        float x, y, z;
    };

    struct ColorVertex {
        float x, y, z;
        uint32_t abgr;
    };

    struct TextureVertex {
        float x, y, z;
        float u, v;
    };

    enum class CloudCameraPos { Above, Inside, Below };

    struct CloudTexture {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> solid;
    };

    struct CloudFrame {
        int cellX = 0;
        int cellZ = 0;
        float xInCell = 0.0f;
        float zInCell = 0.0f;
        float relativeBottomY = 0.0f;
        CloudCameraPos position = CloudCameraPos::Inside;
    };

    struct CloudMesh {
        std::vector<TextureVertex> vertices;
        std::vector<uint32_t> indices;
    };

    constexpr float kCloudCellSize = 12.0f;
    constexpr float kCloudCellHeight = 4.0f;
    constexpr int kCloudTicksPerCell = 400;

    std::vector<PositionVertex> BuildSkyDisc(float y);
    std::vector<ColorVertex> BuildSunriseFan();
    std::vector<PositionVertex> BuildStars();
    std::vector<TextureVertex> BuildSunQuad();
    std::vector<TextureVertex> BuildMoonQuads();
    std::vector<uint16_t> FanIndices(int vertexCount);
    std::vector<uint16_t> QuadIndices(int quadCount);

    CloudFrame ComputeCloudFrame(double cameraX, double cameraY, double cameraZ, float bottomY,
                                 double gameTicks, const CloudTexture& texture);
    CloudMesh BuildCloudMesh(const CloudTexture& texture, const CloudFrame& frame, bool fancy,
                             int rangeChunks);
}
