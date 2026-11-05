// density function values from the 26.3 jar's own classes (tools/javaref/reference_seed12345.txt), seed 12345;
#include "world/vanilla/DensityFunction.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace perlin::server::vanilla;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: vanilla_density_test <reference file>\n");
        return 2;
    }
    auto pack = DataPack::Embedded();
    DensityGraph graph(*pack, 12345);
    const char* names[] = {"temp", "veg", "cont", "ero", "depth", "ridges", "surf", "final"};
    const char* ids[] = {"overworld/temperature",
                         "overworld/vegetation",
                         "overworld/continents",
                         "overworld/erosion",
                         "overworld/depth",
                         "overworld/ridges",
                         "overworld/chunk_surface_level",
                         "overworld/final_density"};
    std::ifstream in(argv[1]);
    std::string line;
    int bad = 0, checked = 0;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        int x, y, z;
        ls >> x >> y >> z;
        std::string tok;
        while (ls >> tok) {
            const size_t eq = tok.find('=');
            if (eq == std::string::npos)
                continue;
            const std::string name = tok.substr(0, eq);
            const float want = std::strtof(tok.c_str() + eq + 1, nullptr);
            for (int k = 0; k < 8; k++)
                if (name == names[k]) {
                    DfPtr node = graph.Load(ids[k]);
                    if (!node) {
                        std::printf("missing %s\n", ids[k]);
                        bad++;
                        continue;
                    }
                    const float got = node->Eval(x, y, z);
                    checked++;
                    if (got != want) {
                        std::printf("%d %d %d %s: got %.9g want %.9g\n", x, y, z, name.c_str(), got, want);
                        bad++;
                    }
                }
        }
    }
    for (const std::string& u : graph.Unsupported())
        std::printf("unsupported node type: %s\n", u.c_str());
    std::printf("%d value(s) checked, %d differ\n", checked, bad);
    // a missing or empty reference file must not pass
    if (checked < 30) {
        std::printf("too few values checked: is the reference file readable?\n");
        return 1;
    }
    return bad ? 1 : 0;
}
