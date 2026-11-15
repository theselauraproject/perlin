#include "client/systems/multiplayer/HudItemConvert.hpp"

#include "client/systems/sprites/ItemData.hpp"

#include <algorithm>

namespace perlin {
    ui::HudItem ToHudItem(const BedrockSession::ItemStackInfo& info) {
        ui::HudItem item;
        if (info.Empty())
            return item;
        item.name = info.name;
        item.count = info.count;
        item.blockRuntimeId = info.blockRuntimeId;
        item.enchanted = info.enchanted;
        item.customName = info.customName;
        item.hasColor = info.hasColor;
        item.color = info.color;
        item.nbtTags = info.nbtTags;
        item.enchants = info.enchants;
        item.lore = info.lore;
        item.canDestroy = info.canDestroy;
        item.maxDamage = ui::MaxDurability(info.name);
        item.aux = item.maxDamage > 0 ? std::clamp(info.damage, 0, item.maxDamage) : info.aux;
        return item;
    }
}
