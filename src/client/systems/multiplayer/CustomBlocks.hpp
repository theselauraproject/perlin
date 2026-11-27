#pragma once
#include "client/systems/multiplayer/ItemCooldowns.hpp"
#include "protocol/Packets.hpp"

#include <vector>

namespace perlin {
    void InstallPersistentChunkResolver();
    size_t RegisterCustomItems(const std::vector<proto::ItemstatesEntry>& items,
                              std::map<std::string, itemcooldown::Definition, std::less<>>& cooldowns);
    size_t RegisterCustomBlocks(const std::vector<proto::BlockPropertiesEntry>& blocks);
}
