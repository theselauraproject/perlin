#pragma once
#include "client/player/PerlinBlockOffset.hpp"
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace perlin {
    using CollisionBox = std::array<float, 6>;

    struct CollisionSet {
        bool defined = false;
        std::vector<CollisionBox> boxes;
    };

    struct CustomBlockCollision {
        CollisionSet base;
        std::vector<std::pair<std::string, CollisionSet>> permutations;
        CollisionSet selection;
        std::vector<std::pair<std::string, CollisionSet>> selectionPermutations;
        float friction = -1.0f;
        std::optional<physics::bds::BlockRandomOffset> randomOffset;
        std::vector<std::pair<std::string, physics::bds::BlockRandomOffset>> randomOffsetPermutations;
    };

    struct CustomBlockDef {
        std::string name;
        std::map<std::string, std::string> faceTexture;
        bool cutout = false;
        std::string json;
        std::string geometry;
    };

    struct CustomItemDef {
        std::string name;
        std::string icon;
        std::string displayName;
        int armorSlot = -1;
        int useTicks = 0;
    };

    class ServerBlocks {
    public:
        static ServerBlocks& Instance();

        void Add(CustomBlockDef def);

        void Want(const std::string& name, int chunkX, int chunkZ);
        static constexpr int kNoChunk = 0x7fffffff;
        std::vector<CustomBlockDef> Take();

        void Return(std::vector<CustomBlockDef> rest);
        void MarkBuilt(const std::string& name);

        std::vector<std::pair<int, int>> TakeWaitingChunks();
        bool Pending() const;
        bool HasWaiting() const;

        void Requeue();
        void Reset();

        void SetCollision(const std::string& name, CustomBlockCollision collision);
        bool Collision(const std::string& name, CustomBlockCollision& out) const;
        uint64_t CollisionRevision() const { return collisionRevision.load(std::memory_order_acquire); }

        void SetItem(CustomItemDef item);
        bool Item(const std::string& name, CustomItemDef& out) const;
        void ClearItems();

    private:
        mutable std::mutex mutex;
        std::vector<CustomBlockDef> pending;
        std::vector<CustomBlockDef> all;
        std::unordered_map<std::string, size_t> index;
        std::unordered_set<std::string> wanted, built;
        std::unordered_set<int64_t> waitingSet;
        std::vector<std::pair<int, int>> waiting;
        std::unordered_map<std::string, CustomBlockCollision> collisions;
        std::atomic<uint64_t> collisionRevision{0};
        std::unordered_map<std::string, CustomItemDef> items;
    };
}
