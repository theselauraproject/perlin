#pragma once

namespace perlin {
    // times the chunk mesher on generated terrain (Perlin --meshbench [radius] [rounds]); returns an exit code
    int RunMeshBench(int radius, int rounds);
}
