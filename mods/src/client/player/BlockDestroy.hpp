#pragma once
#include <string>

namespace perlin::destroy {
    struct BlockInfo {
        bool known = false;
        float hardness = 1.5f;
        int tool = 0;
        int harvestLevel = 0;
    };
    BlockInfo Lookup(const std::string& blockName);

    bool NeedsSupportBelow(const std::string& blockName);
    bool BreaksWholeStack(const std::string& blockName);

    struct Conditions {
        std::string heldItem;
        bool onGround = true;
        bool inWater = false;
        bool creative = false;
        // effect amplifier + 1, 0 when absent; haste is the larger of haste and conduit power
        int haste = 0, miningFatigue = 0;
        int efficiency = 0;
    };
    float ProgressPerTick(const BlockInfo& block, const Conditions& c);
}
