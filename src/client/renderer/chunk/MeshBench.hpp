#pragma once
#include <string>

namespace perlin {
    // times the chunk mesher on generated terrain (Perlin --meshbench [radius] [rounds]); returns an exit code
    int RunMeshBench(int radius, int rounds);
    // times the hud's json ui scan and build (Perlin --uibench <vanilla resource pack> [rounds])
    int RunUiBench(const std::string& resourcePack, int rounds);
}
