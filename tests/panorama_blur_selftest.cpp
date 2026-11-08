#include "client/ui/TitlePanorama.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace perlin::ui;

namespace {
    struct V3 {
        float x, y, z;
    };

    constexpr V3 kFace[6][4] = {
        {{-1, 1, -1}, {1, 1, -1}, {1, -1, -1}, {-1, -1, -1}},
        {{1, 1, -1}, {1, 1, 1}, {1, -1, 1}, {1, -1, -1}},
        {{1, 1, 1}, {-1, 1, 1}, {-1, -1, 1}, {1, -1, 1}},
        {{-1, 1, 1}, {-1, 1, -1}, {-1, -1, -1}, {-1, -1, 1}},
        {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},
        {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
    };

    V3 Dir(int f, float u, float v) {
        const V3* c = kFace[f];
        return {c[0].x + (c[1].x - c[0].x) * u + (c[3].x - c[0].x) * v,
                c[0].y + (c[1].y - c[0].y) * u + (c[3].y - c[0].y) * v,
                c[0].z + (c[1].z - c[0].z) * u + (c[3].z - c[0].z) * v};
    }

    uint8_t Field(const V3& d) {
        const float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        const float s =
            std::sin(d.x / l * 9.0f) * std::sin(d.y / l * 7.0f + 1.0f) * std::sin(d.z / l * 11.0f + 2.0f);
        return s > 0.0f ? 230 : 20;
    }

    int Value(const PanoramaImage& img, int x, int y) {
        return img.rgba[(static_cast<size_t>(y) * img.width + x) * 4];
    }

    int failures = 0;
    void Check(bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL: %s\n", what);
            failures++;
        }
    }
}

int main() {
    constexpr int kSrc = 128, kOut = 64;
    PanoramaImage in[6], out[6];
    for (int f = 0; f < 6; f++) {
        in[f].width = in[f].height = kSrc;
        in[f].rgba.resize(kSrc * kSrc * 4);
        for (int y = 0; y < kSrc; y++)
            for (int x = 0; x < kSrc; x++) {
                const uint8_t v = Field(Dir(f, (x + 0.5f) / kSrc, (y + 0.5f) / kSrc));
                uint8_t* p = &in[f].rgba[(static_cast<size_t>(y) * kSrc + x) * 4];
                p[0] = p[1] = p[2] = v;
                p[3] = 255;
            }
    }
    Check(BlurPanorama(in, kOut, 3, out), "blur ran");
    Check(out[0].width == kOut && out[5].height == kOut, "output size");

    float seam = 0.0f, inner = 0.0f;
    int seamN = 0, innerN = 0;
    for (int f = 0; f < 6; f++)
        for (int k = 0; k < kOut; k++)
            for (int side = 0; side < 4; side++) {
                const float t = (k + 0.5f) / kOut;
                const float eu = side == 0 ? 0.0f : side == 1 ? 1.0f : t;
                const float ev = side == 2 ? 0.0f : side == 3 ? 1.0f : t;
                const int x = side == 0 ? 0 : side == 1 ? kOut - 1 : k;
                const int y = side == 2 ? 0 : side == 3 ? kOut - 1 : k;
                const V3 d = Dir(f, eu, ev);

                for (int g = 0; g < 6; g++) {
                    if (g == f)
                        continue;
                    const V3* c = kFace[g];
                    const V3 n{(c[0].x + c[2].x) * 0.5f, (c[0].y + c[2].y) * 0.5f, (c[0].z + c[2].z) * 0.5f};
                    if (std::fabs(d.x * n.x + d.y * n.y + d.z * n.z - 1.0f) > 1e-4f)
                        continue;
                    const float gu =
                        ((d.x - c[0].x) * (c[1].x - c[0].x) + (d.y - c[0].y) * (c[1].y - c[0].y) +
                         (d.z - c[0].z) * (c[1].z - c[0].z)) *
                        0.25f;
                    const float gv =
                        ((d.x - c[0].x) * (c[3].x - c[0].x) + (d.y - c[0].y) * (c[3].y - c[0].y) +
                         (d.z - c[0].z) * (c[3].z - c[0].z)) *
                        0.25f;
                    const int gx = std::clamp(static_cast<int>(gu * kOut), 0, kOut - 1);
                    const int gy = std::clamp(static_cast<int>(gv * kOut), 0, kOut - 1);
                    seam += std::abs(Value(out[f], x, y) - Value(out[g], gx, gy));
                    seamN++;
                }
                const int ix = side == 0 ? 1 : side == 1 ? kOut - 2 : k;
                const int iy = side == 2 ? 1 : side == 3 ? kOut - 2 : k;
                inner += std::abs(Value(out[f], x, y) - Value(out[f], ix, iy));
                innerN++;
            }
    seam /= static_cast<float>(seamN ? seamN : 1);
    inner /= static_cast<float>(innerN ? innerN : 1);
    std::printf("mean edge step %.2f, mean inner step %.2f\n", seam, inner);
    Check(seamN > 0, "found neighbour faces");
    Check(seam <= inner * 1.5f + 1.0f, "no seam at face edges");

    Check(PanoramaBlurRadius(0, 512, 1080) == 0, "blur off");
    Check(PanoramaBlurRadius(5, 512, 1080) == 2, "default blur at 1080p");
    Check(PanoramaBlurRadius(10, 512, 720) > PanoramaBlurRadius(5, 512, 720), "radius grows with option");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
