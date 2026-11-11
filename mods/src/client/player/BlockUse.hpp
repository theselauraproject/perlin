#pragma once
#include <cstdint>
#include <string>

namespace perlin::blockuse {
    enum class Use { Nothing, Interact, Place };

    bool IsReplaceable(const std::string& blockName);
    bool IsReplaceableCell(bool loaded, uint16_t blockId, const std::string& blockName);
    bool IsUpperSlabPlacement(int face, float clickY);
    // StairBlock::getPlacementBlock 0x148F9C270; current: the held state's upside_down_bit
    bool IsUpsideDownStairPlacement(int face, float clickY, bool current);
    // weirdo_direction from the actor yaw in degrees (StairBlock::getPlacementBlock 0x148F9C270)
    int StairDirection(float yaw);
    // pillar_axis index (0 y, 1 x, 2 z) for the clicked face (RotatedPillarBlock::getPlacementBlock 0x14B24B7F0)
    int PillarAxis(int face);
    bool IsInteractive(const std::string& blockName);
    // ticks a vanilla item stays in use while held (StartUsingItem, then a release); 0 for one-click items
    int VanillaUseTicks(const std::string& itemName, bool food);

    struct Result {
        Use use = Use::Nothing;
        int dest[3] = {0, 0, 0};
    };

    Result Classify(const std::string& clickedName, const std::string& neighbourName, int clicked[3],
                    int face, bool holdsItem, bool holdsBlock, bool sneaking);
}
