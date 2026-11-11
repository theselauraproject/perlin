#include "client/renderer/SkyGeometry.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace perlin {
    namespace {
        constexpr float kPi = 3.14159265358979323846f;

        class SingleThreadedRandom {
        public:
            explicit SingleThreadedRandom(int64_t seed)
                : state((static_cast<uint64_t>(seed) ^ 0x5DEECE66DULL) & kMask) {}

            float NextFloat() { return static_cast<float>(Next(24)) * 5.9604645E-8f; }

            double NextDouble() {
                const int64_t upper = Next(26);
                const int64_t lower = Next(27);
                return static_cast<double>((upper << 27) + lower) * static_cast<double>(1.110223E-16f);
            }

        private:
            static constexpr uint64_t kMask = (1ULL << 48) - 1;

            int32_t Next(int bits) {
                state = (state * 0x5DEECE66DULL + 0xBULL) & kMask;
                return static_cast<int32_t>(static_cast<int64_t>(state) >> (48 - bits));
            }

            uint64_t state;
        };

        uint32_t PackWhite(float alpha) {
            const uint32_t a = static_cast<uint32_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
            return 0x00FFFFFFu | (a << 24);
        }

        int FloorMod(int a, int b) {
            const int m = a % b;
            return m < 0 ? m + b : m;
        }
    }

    std::vector<PositionVertex> BuildSkyDisc(float y) {
        const float x = (y > 0.0f ? 1.0f : y < 0.0f ? -1.0f : 0.0f) * 512.0f;
        std::vector<PositionVertex> out;
        out.push_back({0.0f, y, 0.0f});
        for (int i = -180; i <= 180; i += 45) {
            const float angle = static_cast<float>(i) * (kPi / 180.0f);
            out.push_back({x * std::cos(angle), y, 512.0f * std::sin(angle)});
        }
        return out;
    }

    std::vector<ColorVertex> BuildSunriseFan() {
        std::vector<ColorVertex> out;
        out.push_back({0.0f, 100.0f, 0.0f, PackWhite(1.0f)});
        for (int i = 0; i <= 16; i++) {
            const float angle = static_cast<float>(i) * (kPi * 2.0f) / 16.0f;
            const float s = std::sin(angle);
            const float c = std::cos(angle);
            out.push_back({s * 120.0f, c * 120.0f, -c * 40.0f, PackWhite(0.0f)});
        }
        return out;
    }

    std::vector<PositionVertex> BuildStars() {
        std::vector<PositionVertex> out;
        SingleThreadedRandom random(10842);
        for (int i = 0; i < 1500; i++) {
            const float x = random.NextFloat() * 2.0f - 1.0f;
            const float y = random.NextFloat() * 2.0f - 1.0f;
            const float z = random.NextFloat() * 2.0f - 1.0f;
            const float size = 0.15f + random.NextFloat() * 0.1f;
            const float lengthSq = x * x + y * y + z * z;
            if (lengthSq <= 0.010000001f || lengthSq >= 1.0f)
                continue;

            const glm::vec3 center = glm::normalize(glm::vec3(x, y, z)) * 100.0f;
            const float zRot = static_cast<float>(random.NextDouble() * static_cast<double>(kPi) * 2.0);

            const glm::vec3 direction = glm::normalize(-center);
            const glm::vec3 up(0.0f, 1.0f, 0.0f);
            const glm::vec3 left = glm::normalize(glm::cross(up, direction));
            const glm::vec3 upward = glm::cross(direction, left);
            const float cs = std::cos(-zRot);
            const float sn = std::sin(-zRot);

            const auto corner = [&](float cx, float cy) {
                const float rx = cs * cx - sn * cy;
                const float ry = sn * cx + cs * cy;
                const glm::vec3 p = left * rx + upward * ry + center;
                return PositionVertex{p.x, p.y, p.z};
            };
            out.push_back(corner(size, -size));
            out.push_back(corner(size, size));
            out.push_back(corner(-size, size));
            out.push_back(corner(-size, -size));
        }
        return out;
    }

    std::vector<TextureVertex> BuildSunQuad() {
        return {{-1.0f, 0.0f, -1.0f, 0.0f, 0.0f},
                {1.0f, 0.0f, -1.0f, 1.0f, 0.0f},
                {1.0f, 0.0f, 1.0f, 1.0f, 1.0f},
                {-1.0f, 0.0f, 1.0f, 0.0f, 1.0f}};
    }

    std::vector<TextureVertex> BuildMoonQuads() {
        std::vector<TextureVertex> out;
        for (int phase = 0; phase < 8; phase++) {
            const float u0 = static_cast<float>(phase % 4) / 4.0f;
            const float v0 = static_cast<float>(phase / 4) / 2.0f;
            const float u1 = u0 + 0.25f;
            const float v1 = v0 + 0.5f;
            out.push_back({-1.0f, 0.0f, -1.0f, u1, v1});
            out.push_back({1.0f, 0.0f, -1.0f, u0, v1});
            out.push_back({1.0f, 0.0f, 1.0f, u0, v0});
            out.push_back({-1.0f, 0.0f, 1.0f, u1, v0});
        }
        return out;
    }

    std::vector<uint16_t> FanIndices(int vertexCount) {
        std::vector<uint16_t> out;
        for (int i = 1; i + 1 < vertexCount; i++) {
            out.push_back(0);
            out.push_back(static_cast<uint16_t>(i));
            out.push_back(static_cast<uint16_t>(i + 1));
        }
        return out;
    }

    std::vector<uint16_t> QuadIndices(int quadCount) {
        std::vector<uint16_t> out;
        out.reserve(static_cast<size_t>(quadCount) * 6);
        for (int i = 0; i < quadCount; i++) {
            const uint16_t b = static_cast<uint16_t>(i * 4);
            for (uint16_t k : {0, 1, 2, 2, 3, 0})
                out.push_back(static_cast<uint16_t>(b + k));
        }
        return out;
    }

    CloudFrame ComputeCloudFrame(double cameraX, double cameraY, double cameraZ, float bottomY,
                                 double gameTicks, const CloudTexture& texture) {
        CloudFrame frame;
        if (texture.width <= 0 || texture.height <= 0)
            return frame;

        const float relativeBottom = static_cast<float>(bottomY - cameraY);
        const float relativeTop = relativeBottom + kCloudCellHeight;
        if (relativeTop < 0.0f)
            frame.position = CloudCameraPos::Above;
        else if (relativeBottom > 0.0f)
            frame.position = CloudCameraPos::Below;
        else
            frame.position = CloudCameraPos::Inside;
        frame.relativeBottomY = relativeBottom;

        const double whole = std::floor(gameTicks);
        const float partial = static_cast<float>(gameTicks - whole);
        const int64_t period = static_cast<int64_t>(texture.width) * kCloudTicksPerCell;
        const float cloudOffset = static_cast<float>(static_cast<int64_t>(whole) % period) + partial;

        double cloudX = cameraX + static_cast<double>(cloudOffset * 0.030000001f);
        double cloudZ = cameraZ + static_cast<double>(3.96f);
        const double widthBlocks = texture.width * 12.0;
        const double heightBlocks = texture.height * 12.0;
        cloudX -= std::floor(cloudX / widthBlocks) * widthBlocks;
        cloudZ -= std::floor(cloudZ / heightBlocks) * heightBlocks;
        frame.cellX = static_cast<int>(std::floor(cloudX / 12.0));
        frame.cellZ = static_cast<int>(std::floor(cloudZ / 12.0));
        frame.xInCell = static_cast<float>(cloudX - static_cast<double>(frame.cellX * 12.0f));
        frame.zInCell = static_cast<float>(cloudZ - static_cast<double>(frame.cellZ * 12.0f));
        return frame;
    }

    namespace {
        constexpr int kPackedX = 0xF03CC3;
        constexpr int kPackedY = 0x6666F0;
        constexpr int kPackedZ = 0xC3F066;
        constexpr float kFaceShade[6] = {0.7f, 1.0f, 0.8f, 0.8f, 0.9f, 0.9f};
        enum Direction { Down, Up, North, South, West, East };

        struct CloudBuilder {
            const CloudTexture& texture;
            CloudMesh& mesh;

            bool Empty(int x, int y) const {
                return !texture.solid[static_cast<size_t>(FloorMod(x, texture.width)) +
                                      static_cast<size_t>(FloorMod(y, texture.height)) * texture.width];
            }

            void Face(int cellX, int cellZ, Direction direction, bool inside, bool topColor) {
                const float shade = topColor ? 1.0f : kFaceShade[direction];
                const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
                for (int q = 0; q < 4; q++) {
                    const int index = direction * 4 + (inside ? 3 - q : q);
                    const float fx = static_cast<float>((kPackedX >> index) & 1);
                    const float fy = static_cast<float>((kPackedY >> index) & 1);
                    const float fz = static_cast<float>((kPackedZ >> index) & 1);
                    mesh.vertices.push_back({fx * kCloudCellSize + static_cast<float>(cellX) * kCloudCellSize,
                                             fy * kCloudCellHeight,
                                             fz * kCloudCellSize + static_cast<float>(cellZ) * kCloudCellSize,
                                             shade, 0.0f});
                }
                for (uint32_t k : {0u, 1u, 2u, 2u, 3u, 0u})
                    mesh.indices.push_back(base + k);
            }
        };
    }

    CloudMesh BuildCloudMesh(const CloudTexture& texture, const CloudFrame& frame, bool fancy,
                             int rangeChunks) {
        CloudMesh mesh;
        if (texture.width <= 0 || texture.height <= 0 ||
            texture.solid.size() < static_cast<size_t>(texture.width) * texture.height)
            return mesh;

        CloudBuilder builder{texture, mesh};
        const int radiusCells = static_cast<int>(std::ceil(static_cast<float>(rangeChunks * 16) / 12.0f));

        const auto tryBuild = [&](int rx, int rz) {
            const int ix = FloorMod(frame.cellX + rx, texture.width);
            const int iy = FloorMod(frame.cellZ + rz, texture.height);
            if (!texture.solid[static_cast<size_t>(ix) + static_cast<size_t>(iy) * texture.width])
                return;
            if (!fancy) {
                builder.Face(rx, rz, Down, false, true);
                return;
            }
            const int cx = frame.cellX + rx;
            const int cz = frame.cellZ + rz;
            if (frame.position != CloudCameraPos::Below)
                builder.Face(rx, rz, Up, false, false);
            if (frame.position != CloudCameraPos::Above)
                builder.Face(rx, rz, Down, false, false);
            if (builder.Empty(cx, cz - 1) && rz > 0)
                builder.Face(rx, rz, North, false, false);
            if (builder.Empty(cx, cz + 1) && rz < 0)
                builder.Face(rx, rz, South, false, false);
            if (builder.Empty(cx - 1, cz) && rx > 0)
                builder.Face(rx, rz, West, false, false);
            if (builder.Empty(cx + 1, cz) && rx < 0)
                builder.Face(rx, rz, East, false, false);
            if (std::abs(rx) <= 1 && std::abs(rz) <= 1)
                for (int d = 0; d < 6; d++)
                    builder.Face(rx, rz, static_cast<Direction>(d), true, false);
        };

        for (int ring = 0; ring <= 2 * radiusCells; ring++)
            for (int rx = -ring; rx <= ring; rx++) {
                const int rz = ring - std::abs(rx);
                if (rz < 0 || rz > radiusCells || rx * rx + rz * rz > radiusCells * radiusCells)
                    continue;
                if (rz != 0)
                    tryBuild(rx, -rz);
                tryBuild(rx, rz);
            }
        return mesh;
    }
}
