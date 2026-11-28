// Leather armour colouring. Vanilla leather textures (worn layers and item icons alike) are grey where the
// dye goes, at full alpha, and carry the undyed brown details at a near-zero alpha (1 on the armour
// layers, 3 on the icons, about 30 on the horse armour icon). The dye must colour the grey and leave the
// details as they are.
#include "client/systems/entity/EntityColor.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace perlin;

namespace {
    int failures = 0;

    void Check(bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            failures++;
        }
    }

    bool Pixel(const std::vector<uint8_t>& rgba, size_t i, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        return rgba[i * 4] == r && rgba[i * 4 + 1] == g && rgba[i * 4 + 2] == b && rgba[i * 4 + 3] == a;
    }
}

int main() {
    // A grey cap pixel, the leather helmet icon's brown stripe (alpha 3), an armour layer's knee patch
    // (alpha 1), the horse armour icon's saddle edge (alpha 30), and a transparent pixel.
    const std::vector<uint8_t> texture = {
        150, 150, 150, 255, //
        149, 94,  59,  3,   //
        82,  52,  32,  1,   //
        52,  34,  22,  30,  //
        255, 255, 255, 0,   //
    };

    std::vector<uint8_t> undyed = texture;
    entity::BakeColorMask(undyed, entity::kLeatherColor);
    Check(Pixel(undyed, 0, 150 * 0xA0 / 255, 150 * 0x65 / 255, 150 * 0x40 / 255, 255),
          "undyed grey is not the default leather brown");
    Check(Pixel(undyed, 1, 149, 94, 59, 255), "icon overlay was recoloured or left see-through");
    Check(Pixel(undyed, 2, 82, 52, 32, 255), "armour overlay was recoloured or left see-through");
    Check(Pixel(undyed, 3, 52, 34, 22, 255), "horse armour overlay was recoloured or left see-through");
    Check(Pixel(undyed, 4, 255, 255, 255, 0), "transparent pixel became visible");

    std::vector<uint8_t> blue = texture;
    entity::BakeColorMask(blue, 0x3C44AA);
    Check(Pixel(blue, 0, 150 * 0x3C / 255, 150 * 0x44 / 255, 150 * 0xAA / 255, 255),
          "dye not applied to grey");
    Check(Pixel(blue, 1, 149, 94, 59, 255), "dye leaked into the undyed overlay");

    std::vector<uint8_t> white = texture;
    entity::BakeColorMask(white, 0xFFFFFF);
    Check(Pixel(white, 0, 150, 150, 150, 255), "white dye changed the grey");

    if (failures)
        return EXIT_FAILURE;
    std::puts("leather_color_selftest: ok");
    return EXIT_SUCCESS;
}
