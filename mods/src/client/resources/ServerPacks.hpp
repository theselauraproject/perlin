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
        std::vector<std::string> Directories() const;
        // server-sent packs; excludes pinned test folders (Clear keeps them)
        bool HasServerPacks() const {
            std::lock_guard lock(mutex);
            return dirs.size() > pinned.size();
        }
        // dev/test: extracted pack folder as if server-sent
        void AddDirectory(const std::string& directory);
        uint32_t Generation() const { return generation; }
        // a set of packs is mounting: each Mount bumps the generation, consumers reload once it ends
        bool Mounting() const { return batches.load() > 0; }
        class Batch {
        public:
            Batch() { Instance().batches++; }
            ~Batch() { Instance().batches--; }
            Batch(const Batch&) = delete;
            Batch& operator=(const Batch&) = delete;
        };

    private:
        mutable std::mutex mutex;
        std::string root;
        std::vector<std::string> dirs;
        std::vector<std::string> pinned;
        std::atomic<uint32_t> generation{0};
        std::atomic<int> batches{0};
    };
}
