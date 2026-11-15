#include "client/systems/renderer/gpu/ShadowMath.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

using namespace perlin::gpu::shadow;

namespace {
    int failures = 0;

    void Check(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            failures++;
        }
    }

    struct RefBasis {
        glm::vec3 right, up, fwd;
    };

    RefBasis RefBasisOf(const glm::vec3& light) {
        RefBasis b;
        b.fwd = -light;
        const glm::vec3 upRef = std::abs(b.fwd.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        b.right = glm::normalize(glm::cross(upRef, b.fwd));
        b.up = glm::cross(b.fwd, b.right);
        return b;
    }

    void RefFill(const RefBasis& b, float cx, float cy, float cz, float R, float D, int size, bool tint,
                 float* sample, float* raster, float* info) {
        float S[16] = {};
        auto col = [&](int c, int r) -> float& { return S[c * 4 + r]; };
        for (int j = 0; j < 3; j++) {
            col(j, 0) = b.right[j] / (2.0f * R);
            col(j, 1) = b.up[j] / (2.0f * R);
            col(j, 2) = b.fwd[j] / (2.0f * D);
        }
        col(3, 0) = 0.5f - cx / (2.0f * R);
        col(3, 1) = 0.5f - cy / (2.0f * R);
        col(3, 2) = 0.5f - cz / (2.0f * D);
        col(3, 3) = 1.0f;
        std::memcpy(sample, S, sizeof(S));
        for (int c = 0; c < 4; c++) {
            const float u = S[c * 4 + 0], v = S[c * 4 + 1], d = S[c * 4 + 2], w = S[c * 4 + 3];
            raster[c * 4 + 0] = 2.0f * u - (c == 3 ? 1.0f : 0.0f);
            raster[c * 4 + 1] = -(2.0f * v - (c == 3 ? 1.0f : 0.0f));
            raster[c * 4 + 2] = d;
            raster[c * 4 + 3] = w;
        }
        info[0] = 1.0f / static_cast<float>(size);
        info[1] = 2.0f * R;
        info[2] = 2.0f * D;
        info[3] = tint ? 2.0f : 1.0f;
    }
}

int main() {
    std::mt19937 rng(4242);
    std::uniform_real_distribution<float> angle(-7.0f, 7.0f), pos(-2000.0f, 2000.0f), dist(24.0f, 256.0f);
    int usable = 0;
    for (int trial = 0; trial < 5000; trial++) {
        const float sunA = angle(rng);
        glm::vec3 L(-std::sin(sunA), std::cos(sunA), 0.0f);
        if (L.y < 0.0f)
            L = -L;
        const auto light = LightFromSunAngle(sunA);
        Check(light.has_value() == (L.y >= 0.03f), "light usability matches original");
        if (!light)
            continue;
        usable++;
        const int64_t k0 = static_cast<int64_t>(std::llround(L.x * 300.0f)),
                      k1 = static_cast<int64_t>(std::llround(L.y * 300.0f));
        const glm::vec3 Lq = glm::normalize(
            glm::vec3(static_cast<float>(k0), static_cast<float>(k1), std::round(L.z * 300.0f)) / 300.0f);
        Check(light->direction == L && light->quantized == Lq && light->key[0] == k0 && light->key[1] == k1,
              "light quantisation matches original");

        const float R = dist(rng), D = R + 80.0f;
        const int size = 1024 * (1 + static_cast<int>(rng() % 4));
        const bool tint = rng() % 2;
        const Volume volume = VolumeFor(R, size, tint);
        const float texel = 2.0f * R / static_cast<float>(size), step = texel * 32.0f;
        Check(TexelSize(volume) == texel && SnapStep(volume) == step, "texel and step");
        const glm::vec3 cam(pos(rng), std::fabs(pos(rng)) * 0.1f, pos(rng));

        const RefBasis rb = RefBasisOf(L);
        const Basis b = BasisOf(L);
        Check(rb.right == b.right && rb.up == b.up && rb.fwd == b.fwd, "basis matches original");
        const float cx = std::floor(glm::dot(cam, rb.right) / step) * step,
                    cy = std::floor(glm::dot(cam, rb.up) / step) * step,
                    cz = std::floor(glm::dot(cam, rb.fwd) / step) * step;
        const glm::vec3 centre = SnappedCentre(b, cam, step);
        Check(centre.x == cx && centre.y == cy && centre.z == cz, "snapped centre matches original");
        float s[16], r[16], i[4];
        RefFill(rb, cx, cy, cz, R, D, size, tint, s, r, i);
        const Matrices m = BuildMatrices(b, centre, volume);
        Check(std::memcmp(s, m.sample, sizeof(s)) == 0 && std::memcmp(r, m.raster, sizeof(r)) == 0 &&
                  std::memcmp(i, m.info, sizeof(i)) == 0,
              "matrices match original");

        const RefBasis kb = RefBasisOf(Lq);
        const int64_t key[6] = {
            static_cast<int64_t>(std::llround(std::floor(glm::dot(cam, kb.right) / step))),
            static_cast<int64_t>(std::llround(std::floor(glm::dot(cam, kb.up) / step))),
            static_cast<int64_t>(std::llround(std::floor(glm::dot(cam, kb.fwd) / step))),
            k0,
            k1,
            77};
        const auto mine = RebuildKey(BasisOf(Lq), cam, step, light->key, 77);
        Check(std::memcmp(key, mine.data(), sizeof(key)) == 0, "rebuild key matches original");

        for (int c = 0; c < 8; c++) {
            const glm::vec3 chunk(pos(rng), pos(rng) * 0.05f, pos(rng));
            const float radius = 11.3f + std::fabs(pos(rng)) * 0.02f;
            const bool reference = !(std::abs(glm::dot(chunk, rb.right) - cx) > R + radius ||
                                     std::abs(glm::dot(chunk, rb.up) - cy) > R + radius ||
                                     std::abs(glm::dot(chunk, rb.fwd) - cz) > D + radius);
            Check(ChunkInVolume(chunk, radius, b, centre, volume) == reference,
                  "chunk culling matches original");
        }

        float planes[6][4] = {};
        CasterPlanes(b, centre, volume, planes);
        const glm::vec3 axes[3] = {rb.right, rb.up, rb.fwd};
        const float cen[3] = {cx, cy, cz}, half[3] = {R, R, D};
        bool same = true;
        for (int a = 0; a < 3; a++) {
            const float lo[4] = {axes[a].x, axes[a].y, axes[a].z, -(cen[a] - half[a])};
            const float hi[4] = {-axes[a].x, -axes[a].y, -axes[a].z, cen[a] + half[a]};
            same = same && std::memcmp(lo, planes[a * 2], sizeof(lo)) == 0 &&
                   std::memcmp(hi, planes[a * 2 + 1], sizeof(hi)) == 0;
        }
        Check(same, "caster planes match original");
    }
    Check(usable > 1000, "enough usable sun angles exercised");
    Check(!LightFromSunAngle(1.5707963f).has_value(), "no shadows with the sun at the horizon");

    float t = 0.0f;
    float last = 0.0f;
    int frames = 0;
    for (;;) {
        const Fade f = AdvanceFade(t, 0.05f);
        frames++;
        if (f.finished)
            break;
        Check(f.weight >= last, "fade weight is monotonic");
        last = f.weight;
        t = f.t;
        if (frames > 100)
            break;
    }
    Check(frames == 9, "fade lasts ceil(0.45 / 0.05) frames");
    Check(AdvanceFade(0.0f, 0.0f).weight == 0.0f, "fade starts at zero");

    if (failures == 0)
        std::printf("shadow_math_selftest OK\n");
    return failures == 0 ? 0 : 1;
}
