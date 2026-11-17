#pragma once
#include <glm/glm.hpp>

#include <cmath>

namespace perlin {
    struct Frustum {
        enum class Result { Outside, Intersecting, Inside };

        float planes[6][4];

        static Frustum FromViewProjection(const glm::mat4& vp) {
            auto row = [&](int i) { return glm::vec4(vp[0][i], vp[1][i], vp[2][i], vp[3][i]); };
            const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
            const glm::vec4 raw[6] = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2};
            Frustum f{};
            for (int i = 0; i < 6; i++) {
                const float len = std::sqrt(raw[i].x * raw[i].x + raw[i].y * raw[i].y + raw[i].z * raw[i].z);
                const float inv = len > 1e-6f ? 1.0f / len : 1.0f;
                f.planes[i][0] = raw[i].x * inv;
                f.planes[i][1] = raw[i].y * inv;
                f.planes[i][2] = raw[i].z * inv;
                f.planes[i][3] = raw[i].w * inv;
            }
            return f;
        }

        Result Test(float x0, float y0, float z0, float x1, float y1, float z1) const {
            Result result = Result::Inside;
            for (const auto& p : planes) {
                const float far = p[0] * (p[0] >= 0 ? x1 : x0) + p[1] * (p[1] >= 0 ? y1 : y0) +
                                  p[2] * (p[2] >= 0 ? z1 : z0) + p[3];
                if (far < 0.0f)
                    return Result::Outside;
                const float near = p[0] * (p[0] >= 0 ? x0 : x1) + p[1] * (p[1] >= 0 ? y0 : y1) +
                                   p[2] * (p[2] >= 0 ? z0 : z1) + p[3];
                if (near < 0.0f)
                    result = Result::Intersecting;
            }
            return result;
        }

        bool Visible(float x0, float y0, float z0, float x1, float y1, float z1) const {
            return Test(x0, y0, z0, x1, y1, z1) != Result::Outside;
        }
    };
}
