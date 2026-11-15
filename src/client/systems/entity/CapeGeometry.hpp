#pragma once
#include "client/systems/attachable/Geometry.hpp"

#include <string>

namespace perlin::entity {
    inline constexpr const char* kCapeGeometryPlainId = "geometry.cape_physics_plain";
    inline constexpr const char* kCapeGeometryShoulderId = "geometry.cape_physics_shoulder";

    bool LoadCapeGeometries(attachable::GeometryLibrary& library, std::string& error);
    bool LoadCapeGeometryText(attachable::GeometryLibrary& library, const std::string& text,
                              const char* expectedId, std::string& error);
}
