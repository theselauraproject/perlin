#include "client/systems/blocks/BlockModels.hpp"

#include <cmath>
#include <cstdio>

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "resources/data/blocks/block_models.bin";
    perlin::BlockModels m;
    if (!m.Loaded() && !m.Load(path)) {
        std::printf("FAIL: cannot load %s\n", path);
        return 1;
    }
    if (m.BlockCount() < 10000 || m.Textures().size() < 500) {
        std::printf("FAIL: too few models (%zu blocks, %zu textures)\n", m.BlockCount(), m.Textures().size());
        return 1;
    }
    // Inventory meshes must be separate from placed-block states. A lone
    // fence post is not a valid Java fence inventory icon.
    if (m.ItemModelCount() < 700) {
        std::printf("FAIL: missing Java inventory models (%zu)\n", m.ItemModelCount());
        return 1;
    }
    for (const char* name : {"oak_fence", "oak_stairs", "oak_fence_gate", "mossy_cobblestone_wall", "spruce_planks"}) {
        const auto* model = m.FindItem(name);
        if (!model || model->groups.empty()) {
            std::printf("FAIL: missing Java inventory mesh for %s\n", name);
            return 1;
        }
        size_t faces = 0;
        for (const auto& group : model->groups)
            for (const auto& choice : group)
                faces += m.Variant(choice.variant).size();
        if (faces < 4) {
            std::printf("FAIL: incomplete Java inventory mesh for %s\n", name);
            return 1;
        }
    }
    size_t quads = 0;
    // The previous random negative-hash scan could visit no entries at all.
    // Verify the actual inventory meshes, including their texture references.
    for (const char* name : {"oak_fence", "oak_stairs", "oak_fence_gate", "mossy_cobblestone_wall", "spruce_planks"}) {
        const auto* model = m.FindItem(name);
        for (const auto& group : model->groups)
            for (const auto& choice : group)
                for (const auto& q : m.Variant(choice.variant)) {
                    ++quads;
                    if (q.tex != 0xFFFF && q.tex >= m.Textures().size()) {
                        std::printf("FAIL: item %s has invalid texture index\n", name);
                        return 1;
                    }
                    for (const auto& v : q.pos)
                        for (const float coordinate : v)
                            if (!std::isfinite(coordinate) || coordinate < -1.5f || coordinate > 2.5f) {
                                std::printf("FAIL: item %s has invalid vertex\n", name);
                                return 1;
                            }
                }
    }
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
