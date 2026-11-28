#pragma once
#include "client/systems/attachable/Geometry.hpp"
#include "client/systems/sprites/ItemDisplay.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace perlin::ui::special {
    // One textured piece of a Java special item model (shield, chest, banner, head, conduit, shulker box,
    // copper golem statue).
    struct Part {
        std::string texture;  // resource path without extension, e.g. "textures/entity/shield"
        std::vector<attachable::Quad> quads;
        glm::vec3 tint{1.0f};
    };

    // Builds the model in the space ItemTransform.apply works in: the quads already include the special
    // renderer's own pose and the item definition's "transformation", so a display context only has to
    // apply itemdisplay::Pose(transform, leftHand). Returns false for a special type with no model here
    // (decorated pot, trident), so the caller keeps its own fallback.
    bool Build(const itemdisplay::Entry& entry, const std::string& itemName, std::vector<Part>& out);

    // Adds a Java ModelPart cube exactly as ModelPart.Cube builds it: the same six polygons, vertex order
    // and texture rectangles (including mirror), with positions in model pixels transformed by `pose`
    // (which should include the 1/16 scale) and UVs divided by the texture size.
    void AddJavaCube(std::vector<attachable::Quad>& out, const glm::mat4& pose, glm::vec3 origin, glm::vec3 size,
                     glm::vec2 uv, glm::vec2 textureSize, float grow = 0.0f, bool mirror = false);
}
