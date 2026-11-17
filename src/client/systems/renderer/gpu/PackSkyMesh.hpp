#pragma once
#include "client/systems/renderer/gpu/PackVertex.hpp"

namespace perlin::gpu::sky {

    struct StarField {
        PackMesh mesh;
        uint32_t starVertices = 0;
    };

    PackMesh EndBox();
    StarField Stars();
    PackMesh Dome(const float skyColor[3]);
    PackMesh SunriseFan(const float sunrise[4], float sunAngle);
    PackMesh MoonQuad(float moonPhase);
    PackMesh DarkDisc();
}
