// Standalone chunk-mesh benchmark and exactness check (the same code as `Perlin --meshbench`), built
// without the renderer so it runs in CI and under valgrind. Usage: perlin_meshbench [radius] [rounds]
// PERLIN_SAMPLES=<folder with resource_pack/ and metadata/> meshes with textures, tints and models.
#include "client/systems/renderer/chunk/MeshBench.hpp"

#include <cstdlib>

int main(int argc, char** argv) {
    const int radius = argc > 1 ? std::atoi(argv[1]) : 4;
    const int rounds = argc > 2 ? std::atoi(argv[2]) : 3;
    return perlin::RunMeshBench(radius, rounds);
}
