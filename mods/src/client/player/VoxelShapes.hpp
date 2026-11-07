#pragma once
#include "client/player/PlayerPhysics.hpp"
#include "protocol/Packets.hpp"

#include <string>
#include <atomic>
#include <vector>

namespace perlin {
    class VoxelShapeRegistry {
    public:
        static VoxelShapeRegistry& Instance();
        void Set(const proto::VoxelShapesPacket& packet);
        void Clear();
        bool Find(const std::string& blockName, std::vector<physics::Box>& out) const;
        size_t Size() const;
        uint64_t Revision() const { return revision.load(std::memory_order_acquire); }

    private:
        std::atomic<uint64_t> revision{0};
    };
}
