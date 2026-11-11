#include "client/renderer/block/BlockModels.hpp"

#include <cmath>
#include <cstdio>

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "resources/block_models.bin";
    perlin::BlockModels m;
    if (!m.Loaded() && !m.Load(path)) {
        std::printf("FAIL: cannot load %s\n", path);
        return 1;
    }
    if (m.BlockCount() < 10000 || m.Textures().size() < 500) {
        std::printf("FAIL: too few models (%zu blocks, %zu textures)\n", m.BlockCount(), m.Textures().size());
        return 1;
    }
    size_t quads = 0;
    for (int32_t h = -2147483647; h < 0; h += 9973) {
        const auto* e = m.Find(h);
        if (!e)
            continue;
        for (const auto& g : e->groups)
            for (const auto& c : g)
                for (const auto& q : m.Variant(c.variant)) {
                    quads++;
                    for (auto& v : q.pos)
                        for (float f : v)
                            if (!std::isfinite(f) || f < -1.5f || f > 2.5f) {
                                std::printf("FAIL: bad vertex %f\n", f);
                                return 1;
                            }
                    if (q.dir > 5 || q.shadeDir > 5 || q.cull > 5) {
                        std::printf("FAIL: bad direction\n");
                        return 1;
                    }
                    if (q.tex != 0xFFFF && q.tex >= m.Textures().size()) {
                        std::printf("FAIL: bad texture index\n");
                        return 1;
                    }
                }
    }
    std::printf("models_selftest ok (%zu sampled quads)\n", quads);
    return 0;
}
