#include "client/systems/camera/BuiltinCameraPresets.hpp"
#include <cassert>
#include <iostream>

int main() {
    const auto& p = perlin::camera::kBuiltinPresets;
    assert(p.size() == 4);
    assert(p[0] == "minecraft:first_person");
    assert(p[1] == "minecraft:third_person");
    assert(p[2] == "minecraft:third_person_front");
    assert(p[3] == "minecraft:free");
    std::cout << "built-in camera names passed\n";
}
