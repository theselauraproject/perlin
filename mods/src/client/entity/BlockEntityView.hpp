#pragma once
#include "client/entity/EntityRenderer.hpp"
#include "client/multiplayer/BlockEntities.hpp"
#include "client/renderer/Tessellator.hpp"

#include <vector>

namespace perlin::entity {
    void BuildBlockEntityDraws(const std::vector<SpecialBlock>& specials, const BlockEntityStore& store,
                               std::vector<BlockEntityDraw>& out);
}
