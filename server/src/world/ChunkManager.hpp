#pragma once
#include "world/Chunk.hpp"
#include "world/Generator.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace perlin::server {

    struct ChunkEntry {
        ChunkColumn column;

        std::vector<uint8_t> payload;
        uint64_t payloadVersion = ~0ull;
    };

    class ChunkManager {
    public:

        explicit ChunkManager(std::unique_ptr<TerrainBackend> backend, size_t maxThreads = 0, int dimension = kOverworld);
        int DimensionId() const { return dimension; }
        ~ChunkManager();

        const char* BackendName() const { return backend->Name(); }

        TerrainBackend& Backend() { return *backend; }

        std::shared_ptr<ChunkEntry> Get(ChunkPos pos);
        std::shared_ptr<ChunkEntry> Peek(ChunkPos pos) const;
        std::shared_ptr<ChunkEntry> Load(ChunkPos pos);

        void Pump();
        std::vector<ChunkPos> TakeReady() { return std::exchange(ready, {}); }

        const std::vector<uint8_t>& Payload(ChunkEntry& entry) const;

        uint16_t GetBlock(int x, int y, int z) const;

        bool SetBlock(int x, int y, int z, uint16_t block);

        size_t Loaded() const { return chunks.size(); }
        size_t Pending() const;

    private:
        void Worker();
        std::vector<std::shared_ptr<const ChunkColumn>> Raws(const std::vector<ChunkPos>& wanted);
        void Finish(const std::vector<ChunkPos>& batch, std::vector<ChunkColumn>& columns);

        std::unique_ptr<TerrainBackend> backend;
        int dimension = kOverworld;
        std::unordered_map<ChunkPos, std::shared_ptr<ChunkEntry>, ChunkPosHash> chunks;
        std::unordered_set<ChunkPos, ChunkPosHash> requested;
        std::vector<ChunkPos> ready;

        mutable std::mutex mutex;
        std::condition_variable wake;
        std::deque<ChunkPos> queue;
        std::vector<std::pair<ChunkPos, std::shared_ptr<ChunkEntry>>> done;
        bool stopping = false;
        std::unordered_map<ChunkPos, std::shared_ptr<const ChunkColumn>, ChunkPosHash> raw;
        std::unordered_set<ChunkPos, ChunkPosHash> rawPending;
        std::deque<ChunkPos> rawOrder;
        std::condition_variable rawReady;
        size_t rawCap = 512;

        std::vector<std::thread> workers;
    };
}
