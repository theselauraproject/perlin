#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace perlin {
    class ServerPacks {
    public:
        static ServerPacks& Instance();
        void SetRoot(const std::string& directory);
        void Clear();
        bool Mount(const std::string& zipPath, const std::string& id, const std::string& contentKey);
        struct PackRef {
            std::string zipPath, id, contentKey;
        };
        // mounts exactly these packs; the same set as now keeps the generation, so a server transfer
        // offering the packs already loaded does not reload every texture, model and sound
        void Replace(const std::vector<PackRef>& packs);
        std::vector<std::string> Directories() const;

        bool HasServerPacks() const {
            std::lock_guard lock(mutex);
            return dirs.size() > pinned.size();
        }

        void AddDirectory(const std::string& directory);
        uint32_t Generation() const { return generation; }

        bool Mounting() const { return batches.load() > 0; }
        class Batch {
        public:
            Batch() { Instance().batches++; }
            ~Batch() { Instance().batches--; }
            Batch(const Batch&) = delete;
            Batch& operator=(const Batch&) = delete;
        };

    private:
        std::string Extract(const std::string& zipPath, const std::string& id, const std::string& contentKey);
        mutable std::mutex mutex;
        std::string root;
        std::vector<std::string> dirs;
        std::vector<std::string> pinned;
        std::atomic<uint32_t> generation{0};
        std::atomic<int> batches{0};
    };
}
