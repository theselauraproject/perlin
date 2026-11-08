#include "world/ChunkManager.hpp"

#include <algorithm>
#include <chrono>
#include <span>

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

    ChunkManager::ChunkManager(std::unique_ptr<TerrainBackend> b, size_t maxThreads, int dimensionId)
        : backend(std::move(b)), dimension(dimensionId) {
        size_t threads = 1;
        if (backend->ThreadSafe()) {
            const unsigned hw = std::thread::hardware_concurrency();

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
        rawReady.notify_all();
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
            std::vector<ChunkColumn> columns(batch.size());
            if (backend->Decorates())
                Finish(batch, columns);
            else
                backend->Generate(batch, columns);
            std::lock_guard lock(mutex);
            for (size_t i = 0; i < batch.size(); i++) {
                auto entry = std::make_shared<ChunkEntry>();
                entry->column = std::move(columns[i]);
                done.emplace_back(batch[i], std::move(entry));
            }
        }
    }

    std::vector<std::shared_ptr<const ChunkColumn>> ChunkManager::Raws(const std::vector<ChunkPos>& wanted) {
        std::vector<std::shared_ptr<const ChunkColumn>> out(wanted.size());
        std::vector<ChunkPos> mine;
        {
            std::lock_guard lock(mutex);
            for (const ChunkPos& p : wanted)
                if (!raw.contains(p) && !rawPending.contains(p)) {
                    rawPending.insert(p);
                    mine.push_back(p);
                }
        }
        const size_t step = std::max<size_t>(1, backend->PreferredBatch());
        for (size_t i = 0; i < mine.size(); i += step) {
            const size_t n = std::min(step, mine.size() - i);
            std::vector<ChunkColumn> columns(n);
            backend->Generate(std::span<const ChunkPos>(mine.data() + i, n), columns);
            std::lock_guard lock(mutex);
            for (size_t k = 0; k < n; k++) {
                const ChunkPos p = mine[i + k];
                raw[p] = std::make_shared<const ChunkColumn>(std::move(columns[k]));
                rawOrder.push_back(p);
                rawPending.erase(p);
            }
            while (raw.size() > rawCap && !rawOrder.empty()) {
                raw.erase(rawOrder.front());
                rawOrder.pop_front();
            }
            rawReady.notify_all();
        }
        std::unique_lock lock(mutex);
        for (size_t i = 0; i < wanted.size(); i++) {
            rawReady.wait(lock, [&] { return stopping || raw.contains(wanted[i]) || !rawPending.contains(wanted[i]); });
            if (const auto it = raw.find(wanted[i]); it != raw.end()) {
                out[i] = it->second;
            } else if (!stopping) {
                rawPending.insert(wanted[i]);
                lock.unlock();
                std::vector<ChunkColumn> one(1);
                backend->Generate(std::span<const ChunkPos>(&wanted[i], 1), one);
                auto made = std::make_shared<const ChunkColumn>(std::move(one[0]));
                lock.lock();
                raw[wanted[i]] = made;
                rawOrder.push_back(wanted[i]);
                rawPending.erase(wanted[i]);
                rawReady.notify_all();
                out[i] = made;
            }
        }
        return out;
    }

    void ChunkManager::Finish(const std::vector<ChunkPos>& batch, std::vector<ChunkColumn>& columns) {
        std::vector<ChunkPos> wanted;
        for (const ChunkPos& p : batch)
            for (int dz = -1; dz <= 1; dz++)
                for (int dx = -1; dx <= 1; dx++) {
                    const ChunkPos q{p.x + dx, p.z + dz};
                    if (std::find(wanted.begin(), wanted.end(), q) == wanted.end())
                        wanted.push_back(q);
                }
        std::sort(wanted.begin(), wanted.end(), [&](const ChunkPos& a, const ChunkPos& b) {
            const bool ia = std::find(batch.begin(), batch.end(), a) != batch.end();
            const bool ib = std::find(batch.begin(), batch.end(), b) != batch.end();
            return ia > ib;
        });
        const auto raws = Raws(wanted);
        for (size_t i = 0; i < batch.size(); i++) {
            const ChunkColumn* region[9] = {};
            for (int dz = -1; dz <= 1; dz++)
                for (int dx = -1; dx <= 1; dx++) {
                    const ChunkPos q{batch[i].x + dx, batch[i].z + dz};
                    const size_t at = static_cast<size_t>(std::find(wanted.begin(), wanted.end(), q) - wanted.begin());
                    region[(dz + 1) * 3 + dx + 1] = raws[at].get();
                }
            if (!region[4])
                continue;
            columns[i] = region[4]->Clone();
            backend->Decorate(batch[i], region, columns[i]);
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
            if (chunks.contains(pos))
                continue;
            chunks[pos] = std::move(entry);
            this->ready.push_back(pos);
        }
    }

    std::shared_ptr<ChunkEntry> ChunkManager::Load(ChunkPos pos) {
        if (const auto it = chunks.find(pos); it != chunks.end())
            return it->second;
        if (!backend->ThreadSafe()) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (std::chrono::steady_clock::now() < until) {
                Get(pos);
                Pump();
                if (auto entry = Peek(pos))
                    return entry;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return nullptr;
        }
        std::vector<ChunkPos> batch{pos};
        std::vector<ChunkColumn> columns(1);
        if (backend->Decorates())
            Finish(batch, columns);
        else
            backend->Generate(batch, columns);
        auto entry = std::make_shared<ChunkEntry>();
        entry->column = std::move(columns[0]);
        chunks[pos] = entry;
        ready.push_back(pos);
        return entry;
    }

    size_t ChunkManager::Pending() const {
        std::lock_guard lock(mutex);
        return queue.size() + done.size();
    }

    const std::vector<uint8_t>& ChunkManager::Payload(ChunkEntry& entry) const {
        if (entry.payloadVersion != entry.column.version) {
            entry.payload.clear();
            entry.column.EncodeNetwork(entry.payload, dimension);
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
