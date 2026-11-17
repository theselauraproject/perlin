#pragma once
#include "protocol/Packets.hpp"

#include <vector>

namespace perlin {
    void InstallPersistentChunkResolver();
    size_t RegisterCustomItems(const std::vector<proto::ItemstatesEntry>& items);
    size_t RegisterCustomBlocks(const std::vector<proto::BlockPropertiesEntry>& blocks);
}
