// Java special item models and display transforms: the shield must sit in the lower corner of the
// screen in first person (not cover the view), mirror for the off hand, and the conduit must be the
// centred 6x6x6 shell. Cube polygons must reproduce ModelPart.Cube's texture rectangles.
#include "client/systems/sprites/SpecialItemModels.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
    using perlin::itemdisplay::Entry;
    using perlin::itemdisplay::Transform;

    Transform Make(glm::vec3 r, glm::vec3 t, float s) {
        Transform x;
        x.present = true;
        x.rotation = r;
        x.translation = t;
        x.scale = glm::vec3(s);
        return x;
    }

    struct Bounds {
        glm::vec3 lo{1e9f}, hi{-1e9f};
    };

    Bounds Measure(const std::vector<perlin::ui::special::Part>& parts, const glm::mat4& m) {
        Bounds b;
        for (const auto& part : parts)
            for (const auto& q : part.quads)
                for (const glm::vec3& p : q.p) {
                    const glm::vec3 w = glm::vec3(m * glm::vec4(p, 1.0f));
                    b.lo = glm::min(b.lo, w);
                    b.hi = glm::max(b.hi, w);
                }
        return b;
    }

    bool Near(float a, float b) {
        return std::fabs(a - b) < 1e-4f;
    }
}

int main() {
    Entry shield;
    shield.found = true;
    shield.kind = Entry::Kind::Special;
    shield.special = "shield";
    shield.firstRight = Make({0, 180, 5}, {-10, 1.75f, -10}, 1.25f);
    shield.firstLeft = Make({0, 180, 5}, {10, 0, -10}, 1.25f);
    shield.local = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, -1.0f));
    shield.hasLocal = true;
    std::vector<perlin::ui::special::Part> parts;
    assert(perlin::ui::special::Build(shield, "minecraft:shield", parts));
    assert(parts.size() == 1 && parts[0].quads.size() == 12);

    // ItemInHandRenderer.applyItemArmTransform at rest, then ItemTransform.apply.
    for (const bool left : {false, true}) {
        const float side = left ? -1.0f : 1.0f;
        const glm::mat4 arm = glm::translate(glm::mat4(1.0f), glm::vec3(side * 0.56f, -0.52f, -0.72f));
        const Bounds b =
            Measure(parts, arm * perlin::itemdisplay::Pose(left ? shield.firstLeft : shield.firstRight, left));
        std::printf("%s shield x %.3f..%.3f y %.3f..%.3f z %.3f..%.3f\n", left ? "left" : "right", b.lo.x, b.hi.x,
                    b.lo.y, b.hi.y, b.lo.z, b.hi.z);
        // Held to the side and below the crosshair, in front of the camera.
        assert(side * b.lo.x > 0.0f || side * b.hi.x > 0.0f);
        assert(side * (b.lo.x + b.hi.x) * 0.5f > 0.3f);
        assert(b.hi.y < 0.0f);
        assert(b.hi.z < -0.3f);
    }

    Entry conduit;
    conduit.found = true;
    conduit.kind = Entry::Kind::Special;
    conduit.special = "conduit";
    conduit.local = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f));
    conduit.hasLocal = true;
    assert(perlin::ui::special::Build(conduit, "minecraft:conduit", parts));
    const Bounds c = Measure(parts, glm::mat4(1.0f));
    assert(Near(c.lo.x, 5.0f / 16.0f) && Near(c.hi.x, 11.0f / 16.0f));
    assert(Near(c.lo.y, 5.0f / 16.0f) && Near(c.hi.y, 11.0f / 16.0f));

    Entry chest;
    chest.found = true;
    chest.kind = Entry::Kind::Special;
    chest.special = "chest";
    chest.args["texture"] = "normal";
    assert(perlin::ui::special::Build(chest, "minecraft:chest", parts));
    const Bounds k = Measure(parts, glm::mat4(1.0f));
    assert(Near(k.lo.x, 1.0f / 16.0f) && Near(k.hi.x, 15.0f / 16.0f));
    assert(Near(k.lo.y, 0.0f) && Near(k.hi.y, 14.0f / 16.0f));
    assert(Near(k.hi.z, 1.0f));  // the latch faces south

    // ModelPart.Cube: a 2x3x4 box at texOffs(10, 20) on a 64x64 texture.
    std::vector<perlin::attachable::Quad> quads;
    perlin::ui::special::AddJavaCube(quads, glm::mat4(1.0f), {0, 0, 0}, {2, 3, 4}, {10, 20}, {64, 64});
    assert(quads.size() == 6);
    const auto uvBounds = [](const perlin::attachable::Quad& q) {
        glm::vec2 lo(1e9f), hi(-1e9f);
        for (const glm::vec2& t : q.uv) {
            lo.x = std::min(lo.x, t.x * 64.0f);
            lo.y = std::min(lo.y, t.y * 64.0f);
            hi.x = std::max(hi.x, t.x * 64.0f);
            hi.y = std::max(hi.y, t.y * 64.0f);
        }
        return std::pair(lo, hi);
    };
    // down: (u+d, v)-(u+d+w, v+d); north: (u+d, v+d)-(u+d+w, v+d+h); south: (u+2d+w, v+d)-(u+2d+2w, v+d+h)
    auto [dl, dh] = uvBounds(quads[0]);
    assert(Near(dl.x, 14) && Near(dl.y, 20) && Near(dh.x, 16) && Near(dh.y, 24));
    auto [nl, nh] = uvBounds(quads[3]);
    assert(Near(nl.x, 14) && Near(nl.y, 24) && Near(nh.x, 16) && Near(nh.y, 27));
    auto [sl, sh] = uvBounds(quads[5]);
    assert(Near(sl.x, 20) && Near(sl.y, 24) && Near(sh.x, 22) && Near(sh.y, 27));
    assert(quads[3].n.z < -0.99f && quads[5].n.z > 0.99f);
    std::puts("special item models passed");
}
