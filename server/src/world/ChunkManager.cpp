#include "world/ChunkManager.hpp"

#include <algorithm>

#if defined(_WIN32)
extern "C" {
__declspec(dllimport) void* __stdcall GetCurrentThread(void);
__declspec(dllimport) int __stdcall SetThreadPriority(void*, int);
}
#elif defined(__linux__) || defined(__ANDROID__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace perlin::server {
    namespace {
        void YieldToTick() {
#if defined(_WIN32)
            SetThreadPriority(GetCurrentThread(), -1);
#elif defined(__linux__) || defined(__ANDROID__)
            setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 4);
#endif
        }
    }

    ChunkManager::ChunkManager(std::unique_ptr<TerrainBackend> b, size_t maxThreads) : backend(std::move(b)) {
        size_t threads = 1;
        if (backend->ThreadSafe()) {
            const unsigned hw = std::thread::hardware_concurrency();
            // leave a core for the tick and network threads
            threads = std::clamp<size_t>(hw > 2 ? hw - 1 : 1, 1, 16);
            if (maxThreads > 0)
                threads = std::min(threads, maxThreads);
        }
        for (size_t i = 0; i < threads; i++)
            workers.emplace_back([this] { Worker(); });
    }

    ChunkManager::~ChunkManager() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        for (std::thread& w : workers)
            if (w.joinable())
                w.join();
    }

    void ChunkManager::Worker() {
        YieldToTick();
        for (;;) {
            std::vector<ChunkPos> batch;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stopping || !queue.empty(); });
                if (stopping)
                    return;
                const size_t take = std::min(queue.size(), std::max<size_t>(1, backend->PreferredBatch()));
                batch.assign(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(take));
                queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(take));
            }
            std::vector<std::shared_ptr<ChunkEntry>> entries(batch.size());
            std::vector<ChunkColumn> columns(batch.size());
            backend->Generate(batch, columns);
            std::lock_guard lock(mutex);
            for (size_t i = 0; i < batch.size(); i++) {
                auto entry = std::make_shared<ChunkEntry>();
                entry->column = std::move(columns[i]);
                done.emplace_back(batch[i], std::move(entry));
            }
        }
    }

    std::shared_ptr<ChunkEntry> ChunkManager::Get(ChunkPos pos) {
        if (const auto it = chunks.find(pos); it != chunks.end())
            return it->second;
        if (requested.insert(pos).second) {
            {
                std::lock_guard lock(mutex);
                queue.push_back(pos);
            }
            wake.notify_one();
        }
        return nullptr;
    }

    std::shared_ptr<ChunkEntry> ChunkManager::Peek(ChunkPos pos) const {
        const auto it = chunks.find(pos);
        return it == chunks.end() ? nullptr : it->second;
    }

    void ChunkManager::Pump() {
        std::vector<std::pair<ChunkPos, std::shared_ptr<ChunkEntry>>> ready;
        {
            std::lock_guard lock(mutex);
            ready.swap(done);
        }
        for (auto& [pos, entry] : ready) {
            requested.erase(pos);
            chunks[pos] = std::move(entry);
        }
    }

    size_t ChunkManager::Pending() const {
        std::lock_guard lock(mutex);
        return queue.size() + done.size();
    }

    const std::vector<uint8_t>& ChunkManager::Payload(ChunkEntry& entry) const {
        if (entry.payloadVersion != entry.column.version) {
            entry.payload.clear();
            entry.column.EncodeNetwork(entry.payload);
            entry.payloadVersion = entry.column.version;
        }
        return entry.payload;
    }

    uint16_t ChunkManager::GetBlock(int x, int y, int z) const {
        const auto it = chunks.find({x >> 4, z >> 4});
        return it == chunks.end() ? 0 : it->second->column.Get(x & 15, y, z & 15);
    }

    bool ChunkManager::SetBlock(int x, int y, int z, uint16_t block) {
        const auto it = chunks.find({x >> 4, z >> 4});
        if (it == chunks.end())
            return false;
        it->second->column.Set(x & 15, y, z & 15, block);
        return true;
    }
}
