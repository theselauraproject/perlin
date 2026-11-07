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
        int armorSlot = -1; // minecraft:wearable slot: 0 head, 1 chest, 2 legs, 3 feet
        int useTicks = 0;   // minecraft:use_modifiers / use_duration; 0: used in one click
    };

    class ServerBlocks {
    public:
        static ServerBlocks& Instance();
        // definitions only register: textures are built for the ones a mesh or a ui asks for (Want), the atlas
        // cannot hold every block a server defines
        void Add(CustomBlockDef def);
        // any thread; (chunkX, chunkZ) is the chunk that met the block, kNoChunk when nothing needs remeshing
        void Want(const std::string& name, int chunkX, int chunkZ);
        static constexpr int kNoChunk = 0x7fffffff;
        std::vector<CustomBlockDef> Take();
        // unbuilt remainder of a Take: a time-boxed build puts back what it did not reach
        void Return(std::vector<CustomBlockDef> rest);
        void MarkBuilt(const std::string& name);
        // chunks that met a block while it had no textures; empty while anything is still queued, since a chunk
        // meshed before its blocks are built would ask again for nothing
        std::vector<std::pair<int, int>> TakeWaitingChunks();
        bool Pending() const;
        // a fresh library: everything wanted so far is built again
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
