#include "client/ui/TitlePanorama.hpp"

#include <cmath>
#include <cstdio>

using namespace perlin::ui;

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                                         \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

static bool Inside(const PanoramaVertex& a, const PanoramaVertex& b, const PanoramaVertex& c, float x,
                   float y) {
    const float d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::fabs(d) < 1e-9f)
        return false;
    const float l1 = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / d;
    const float l2 = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / d;
    return l1 >= -1e-4f && l2 >= -1e-4f && 1.0f - l1 - l2 >= -1e-4f;
}

int main() {
    const int W = 640, H = 360;
    for (float yaw = 0; yaw < 360; yaw += 17.5f)
        for (float pitch : {-30.0f, 0.0f, 8.0f, 11.0f, 45.0f}) {
            PanoramaView v;
            v.yawDegrees = yaw;
            v.pitchDegrees = pitch;
            PanoramaFace faces[6];
            BuildPanorama(v, W, H, faces);
            int holes = 0;
            for (int sy = 0; sy <= 8; sy++)
                for (int sx = 0; sx <= 12; sx++) {
                    const float x = std::fmin(W - 0.5f, sx * W / 12.0f + 0.5f),
                                y = std::fmin(H - 0.5f, sy * H / 8.0f + 0.5f);
                    bool covered = false;
                    for (int f = 0; f < 6 && !covered; f++)
                        for (size_t k = 0; k + 2 < faces[f].indices.size() && !covered; k += 3)
                            covered = Inside(faces[f].vertices[faces[f].indices[k]],
                                             faces[f].vertices[faces[f].indices[k + 1]],
                                             faces[f].vertices[faces[f].indices[k + 2]], x, y);
                    holes += !covered;
                }
            if (holes)
                std::printf("yaw %.1f pitch %.1f: %d uncovered samples\n", yaw, pitch, holes);
            CHECK(holes == 0);
        }

    const PanoramaView a = PanoramaViewAt(10.0), b = PanoramaViewAt(10.0 + 1.0 / 240.0);
    CHECK(std::fabs(b.yawDegrees - a.yawDegrees - 1.5f / 240.0f) < 1e-3f);
    CHECK(std::fabs(PanoramaViewAt(240.0).yawDegrees) < 1e-3f ||
          std::fabs(PanoramaViewAt(240.0).yawDegrees - 360.0f) < 1e-3f);

    if (failures == 0)
        std::printf("panorama_selftest: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
