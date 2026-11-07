#pragma once
#include <cstdint>
#include <vector>

namespace perlin::ui {
    struct PanoramaVertex {
        float x, y;
        float u, v;
    };

    struct PanoramaFace {
        std::vector<PanoramaVertex> vertices;
        std::vector<uint32_t> indices;
    };

    struct PanoramaView {
        float yawDegrees = 0;
        float pitchDegrees = 0;
        float fovYDegrees = 85;
    };

    PanoramaView PanoramaViewAt(double seconds);

    void BuildPanorama(const PanoramaView& view, int width, int height, PanoramaFace (&faces)[6],
                       int grid = 16);

    struct PanoramaImage {
        int width = 0, height = 0;
        std::vector<uint8_t> rgba;
    };

    // menu backdrop: each face resampled to size x size and box blurred 3 times (radius in output texels);
    // blurred across face edges through the cube, no seams
    bool BlurPanorama(const PanoramaImage (&in)[6], int size, int radius, PanoramaImage (&out)[6]);

    // box radius in face texels matching the game's screen-space menu blur (radius in framebuffer pixels)
    int PanoramaBlurRadius(int menuBlur, int faceSize, int fbHeight, float fovYDegrees = 85.0f);
}
