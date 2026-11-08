#include "client/resources/ServerBlocks.hpp"

namespace perlin {
    ServerBlocks& ServerBlocks::Instance() {
        static ServerBlocks instance;
        return instance;
    }

    void ServerBlocks::Add(CustomBlockDef def) {
        std::lock_guard lock(mutex);
        index[def.name] = all.size();
        built.erase(def.name);

        wanted.insert(def.name);
        pending.push_back(def);
        all.push_back(std::move(def));
    }

    void ServerBlocks::Want(const std::string& name, int chunkX, int chunkZ) {
        std::lock_guard lock(mutex);
        const auto it = index.find(name);
        if (it == index.end())
            return;
        if (wanted.insert(name).second)
            pending.push_back(all[it->second]);
        else if (built.contains(name))
            return;
        if (chunkX != kNoChunk &&
            waitingSet.insert((static_cast<int64_t>(chunkX) << 32) | static_cast<uint32_t>(chunkZ)).second)
            waiting.emplace_back(chunkX, chunkZ);
    }

    void ServerBlocks::MarkBuilt(const std::string& name) {
        std::lock_guard lock(mutex);
        built.insert(name);
    }

    std::vector<CustomBlockDef> ServerBlocks::Take() {
        std::lock_guard lock(mutex);
        std::vector<CustomBlockDef> out;
        out.swap(pending);
        return out;
    }

    void ServerBlocks::Return(std::vector<CustomBlockDef> rest) {
        std::lock_guard lock(mutex);
        pending.insert(pending.begin(), std::make_move_iterator(rest.begin()), std::make_move_iterator(rest.end()));
    }

    std::vector<std::pair<int, int>> ServerBlocks::TakeWaitingChunks() {
        std::lock_guard lock(mutex);
        std::vector<std::pair<int, int>> out;
        if (!pending.empty())
            return out;
        out.swap(waiting);
        waitingSet.clear();
        return out;
    }

    bool ServerBlocks::Pending() const {
        std::lock_guard lock(mutex);
        return !pending.empty();
    }

    bool ServerBlocks::HasWaiting() const {
        std::lock_guard lock(mutex);
        return !waiting.empty();
    }

    void ServerBlocks::Requeue() {
        std::lock_guard lock(mutex);
        pending.clear();
        built.clear();
        for (const std::string& name : wanted)
            if (const auto it = index.find(name); it != index.end())
                pending.push_back(all[it->second]);
        for (const auto& [name, at] : index)
            if (!wanted.contains(name)) {
                wanted.insert(name);
                pending.push_back(all[at]);
            }
    }

    void ServerBlocks::Reset() {
        std::lock_guard lock(mutex);
        pending.clear();
        all.clear();
        index.clear();
        wanted.clear();
        built.clear();
        waiting.clear();
        waitingSet.clear();
        collisions.clear();
        collisionRevision.fetch_add(1, std::memory_order_release);
    }

    void ServerBlocks::SetCollision(const std::string& name, CustomBlockCollision collision) {
        std::lock_guard lock(mutex);
        collisions[name] = std::move(collision);
        collisionRevision.fetch_add(1, std::memory_order_release);
    }

    bool ServerBlocks::Collision(const std::string& name, CustomBlockCollision& out) const {
        std::lock_guard lock(mutex);
        const auto it = collisions.find(name);
        if (it == collisions.end())
            return false;
        out = it->second;
        return true;
    }

    void ServerBlocks::SetItem(CustomItemDef item) {
        std::lock_guard lock(mutex);
        const std::string key = item.name;
        items[key] = std::move(item);
    }

    bool ServerBlocks::Item(const std::string& name, CustomItemDef& out) const {
        std::lock_guard lock(mutex);
        const auto it = items.find(name);
        if (it == items.end())
            return false;
        out = it->second;
        return true;
    }

    void ServerBlocks::ClearItems() {
        std::lock_guard lock(mutex);
        items.clear();
    }
}
