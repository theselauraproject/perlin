#pragma once
#include <string>

namespace perlin {
    int RunMeshBench(int radius, int rounds);
    int RunUiBench(const std::string& resourcePack, int rounds);
}
