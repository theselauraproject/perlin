#include "client/systems/renderer/gpu/ShadowMath.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace perlin::gpu::shadow {

    Basis BasisOf(const glm::vec3& light) {
        Basis b;
        b.fwd = -light;
        const glm::vec3 upRef = std::abs(b.fwd.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        b.right = glm::normalize(glm::cross(upRef, b.fwd));
        b.up = glm::cross(b.fwd, b.right);
        return b;
    }

    std::optional<Light> LightFromSunAngle(float sunAngle) {
        glm::vec3 direction(-std::sin(sunAngle), std::cos(sunAngle), 0.0f);
        if (direction.y < 0.0f)
            direction = -direction;
        if (direction.y < kMinLightHeight)
            return std::nullopt;
        const std::array<int64_t, 2> key = {static_cast<int64_t>(std::llround(direction.x * 300.0f)),
                                            static_cast<int64_t>(std::llround(direction.y * 300.0f))};
        const glm::vec3 quantized =
            glm::normalize(glm::vec3(static_cast<float>(key[0]), static_cast<float>(key[1]),
                                     std::round(direction.z * 300.0f)) /
                           300.0f);
        return Light{direction, quantized, key};
    }

    Volume VolumeFor(float distance, int size, bool tinted) {
        return {distance, distance + 80.0f, size, tinted};
    }

    float TexelSize(const Volume& volume) {
        return 2.0f * volume.radius / static_cast<float>(volume.size);
    }

    float SnapStep(const Volume& volume) {
        return TexelSize(volume) * 32.0f;
    }

    Matrices BuildMatrices(const Basis& b, const glm::vec3& centre, const Volume& volume) {
        const float R = volume.radius, D = volume.depth;
        Matrices out;
        float S[16] = {};
        const auto col = [&](int c, int r) -> float& { return S[c * 4 + r]; };
        for (int j = 0; j < 3; j++) {
            col(j, 0) = b.right[j] / (2.0f * R);
            col(j, 1) = b.up[j] / (2.0f * R);
            col(j, 2) = b.fwd[j] / (2.0f * D);
        }
        col(3, 0) = 0.5f - centre.x / (2.0f * R);
        col(3, 1) = 0.5f - centre.y / (2.0f * R);
        col(3, 2) = 0.5f - centre.z / (2.0f * D);
        col(3, 3) = 1.0f;
        std::memcpy(out.sample, S, sizeof(S));

        for (int c = 0; c < 4; c++) {
            const float u = S[c * 4 + 0], v = S[c * 4 + 1], d = S[c * 4 + 2], w = S[c * 4 + 3];
            out.raster[c * 4 + 0] = 2.0f * u - (c == 3 ? 1.0f : 0.0f);
            out.raster[c * 4 + 1] = -(2.0f * v - (c == 3 ? 1.0f : 0.0f));
            out.raster[c * 4 + 2] = d;
            out.raster[c * 4 + 3] = w;
        }
        out.info[0] = 1.0f / static_cast<float>(volume.size);
        out.info[1] = 2.0f * R;
        out.info[2] = 2.0f * D;
        out.info[3] = volume.tinted ? 2.0f : 1.0f;
        return out;
    }

    glm::vec3 SnappedCentre(const Basis& b, const glm::vec3& camera, float step) {
        return {std::floor(glm::dot(camera, b.right) / step) * step,
                std::floor(glm::dot(camera, b.up) / step) * step,
                std::floor(glm::dot(camera, b.fwd) / step) * step};
    }

    std::array<int64_t, 6> RebuildKey(const Basis& b, const glm::vec3& camera, float step,
                                      const std::array<int64_t, 2>& lightKey, uint64_t meshGeneration) {
        return {static_cast<int64_t>(std::llround(std::floor(glm::dot(camera, b.right) / step))),
                static_cast<int64_t>(std::llround(std::floor(glm::dot(camera, b.up) / step))),
                static_cast<int64_t>(std::llround(std::floor(glm::dot(camera, b.fwd) / step))),
                lightKey[0],
                lightKey[1],
                static_cast<int64_t>(meshGeneration)};
    }

    bool ChunkInVolume(const glm::vec3& chunkCentre, float chunkRadius, const Basis& b,
                       const glm::vec3& centre, const Volume& volume) {
        return std::abs(glm::dot(chunkCentre, b.right) - centre.x) <= volume.radius + chunkRadius &&
               std::abs(glm::dot(chunkCentre, b.up) - centre.y) <= volume.radius + chunkRadius &&
               std::abs(glm::dot(chunkCentre, b.fwd) - centre.z) <= volume.depth + chunkRadius;
    }

    void CasterPlanes(const Basis& b, const glm::vec3& centre, const Volume& volume, float planes[6][4]) {
        const glm::vec3 axes[3] = {b.right, b.up, b.fwd};
        const float c[3] = {centre.x, centre.y, centre.z},
                    half[3] = {volume.radius, volume.radius, volume.depth};
        for (int a = 0; a < 3; a++) {
            float* lo = planes[a * 2];
            float* hi = planes[a * 2 + 1];
            lo[0] = axes[a].x, lo[1] = axes[a].y, lo[2] = axes[a].z, lo[3] = -(c[a] - half[a]);
            hi[0] = -axes[a].x, hi[1] = -axes[a].y, hi[2] = -axes[a].z, hi[3] = c[a] + half[a];
        }
    }

    Fade AdvanceFade(float t, float dt) {
        Fade fade;
        fade.t = t + dt / kFadeSeconds;
        if (fade.t >= 1.0f) {
            fade.t = 0.0f;
            fade.finished = true;
            return fade;
        }
        fade.weight = fade.t * fade.t * (3.0f - 2.0f * fade.t);
        return fade;
    }

}
