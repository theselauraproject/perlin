#pragma once

#include <array>
#include <string_view>

namespace perlin::camera {
    // Built-in Bedrock preset names, in the documented order.  Server-supplied
    // preset lists still take precedence because packet runtime IDs are indices.
    inline constexpr std::array<std::string_view, 4> kBuiltinPresets{
        "minecraft:first_person", "minecraft:third_person",
        "minecraft:third_person_front", "minecraft:free"};
}
