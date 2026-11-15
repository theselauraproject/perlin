#pragma once
#include <cstdint>
#include <string>

namespace perlin::blockuse {
    enum class Use { Nothing, Interact, Place };

    bool IsReplaceable(const std::string& blockName);
    bool IsReplaceableCell(bool loaded, uint16_t blockId, const std::string& blockName);
    bool IsUpperSlabPlacement(int face, float clickY);

    bool IsUpsideDownStairPlacement(int face, float clickY, bool current);

    int StairDirection(float yaw);

    int PillarAxis(int face);
    bool IsInteractive(const std::string& blockName);

    int VanillaUseTicks(const std::string& itemName, bool food);

    struct Result {
        Use use = Use::Nothing;
        int dest[3] = {0, 0, 0};
    };

    Result Classify(const std::string& clickedName, const std::string& neighbourName, int clicked[3],
                    int face, bool holdsItem, bool holdsBlock, bool sneaking);
}
