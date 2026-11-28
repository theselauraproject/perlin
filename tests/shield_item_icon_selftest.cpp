#include "client/systems/sprites/ShieldItemIcon.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    constexpr int size = 64;
    std::vector<uint8_t> atlas(size * size * 4, 255);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            atlas[(y * size + x) * 4] = static_cast<uint8_t>(x * 4);
    const auto img = perlin::ui::BuildShieldItemIcon(atlas, size, size);
    assert(img.size() == 32 * 32 * 4);
    size_t coverage = 0;
    for (size_t i = 3; i < img.size(); i += 4)
        coverage += img[i] != 0;
    assert(coverage > 90 && coverage < 300); // a narrow item, not a full atlas square
    assert(img[3] == 0); // transparent border
    int xmin = 32, xmax = -1, ymin = 32, ymax = -1;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if (img[(y * 32 + x) * 4 + 3] != 0) {
                xmin = std::min(xmin, x);
                xmax = std::max(xmax, x);
                ymin = std::min(ymin, y);
                ymax = std::max(ymax, y);
            }
    assert(xmax > xmin + 6 && xmax < xmin + 19); // recognizable angled plate
    assert(ymax > ymin + 17); // not collapsed into a line
    // The plate's visible rear face uses the UV rectangle (14,1)-(26,23)
    // in Java's box-unfolding scheme. It must not sample the handle/edge area.
    std::vector<uint8_t> uvAtlas(64 * 64 * 4, 255);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const size_t k = static_cast<size_t>(y * 64 + x) * 4;
            uvAtlas[k] = (x >= 14 && x < 26 && y >= 1 && y < 23) ? 255 : 0;
            uvAtlas[k + 1] = 0;
            uvAtlas[k + 2] = (x >= 14 && x < 26 && y >= 1 && y < 23) ? 0 : 255;
        }
    const auto uvImg = perlin::ui::BuildShieldItemIcon(uvAtlas, 64, 64);
    int platePixels = 0;
    for (size_t i = 0; i < uvImg.size(); i += 4)
        if (uvImg[i + 3] && uvImg[i] > uvImg[i + 2])
            ++platePixels;
    assert(platePixels > 60); // Java's plate UVs occupy the main visible face
    const auto missing = perlin::ui::BuildShieldItemIcon({}, 64, 64);
    assert(missing.size() == img.size() && missing[3] == 0);
    std::cout << "shield preview shape and bounds passed\n";
}
