#pragma once
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>

namespace perlin::gpu::shadow {

    struct Basis {
        glm::vec3 right, up, fwd;
    };

    struct Volume {
        float radius;
        float depth;
        int size;
        bool tinted;
    };

    struct Light {
        glm::vec3 direction;
        glm::vec3 quantized;
        std::array<int64_t, 2> key;
    };

    struct Matrices {
        float sample[16] = {};
        float raster[16] = {};
        float info[4] = {};
    };

    struct Fade {
        float t = 0.0f;
        float weight = 0.0f;
        bool finished = false;
    };

    constexpr float kMinLightHeight = 0.03f;
    constexpr float kFadeSeconds = 0.45f;

    Basis BasisOf(const glm::vec3& light);
    std::optional<Light> LightFromSunAngle(float sunAngle);
    Volume VolumeFor(float distance, int size, bool tinted);
    float TexelSize(const Volume& volume);
    float SnapStep(const Volume& volume);
    Matrices BuildMatrices(const Basis& basis, const glm::vec3& centre, const Volume& volume);
    glm::vec3 SnappedCentre(const Basis& basis, const glm::vec3& camera, float step);
    std::array<int64_t, 6> RebuildKey(const Basis& basis, const glm::vec3& camera, float step,
                                      const std::array<int64_t, 2>& lightKey, uint64_t meshGeneration);
    bool ChunkInVolume(const glm::vec3& chunkCentre, float chunkRadius, const Basis& basis,
                       const glm::vec3& centre, const Volume& volume);
    void CasterPlanes(const Basis& basis, const glm::vec3& centre, const Volume& volume, float planes[6][4]);
    Fade AdvanceFade(float t, float dt);

}
