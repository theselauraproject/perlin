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
    const auto missing = perlin::ui::BuildShieldItemIcon({}, 64, 64);
    assert(missing.size() == img.size() && missing[3] == 0);
    std::cout << "shield preview shape and bounds passed\n";
}
