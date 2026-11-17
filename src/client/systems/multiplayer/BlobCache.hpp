#pragma once
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin {

    class BlobCache {
    public:
        static BlobCache& Instance();
        explicit BlobCache(std::string path);

        bool Usable();
        bool Get(uint64_t hash, std::vector<uint8_t>& out);
        void Put(uint64_t hash, const uint8_t* data, size_t size);
        void Flush();

    private:
        struct Entry {
            uint64_t offset = 0;
            uint32_t size = 0;
            uint32_t check = 0;
        };
        void OpenLocked();
        void ResetLocked();

        std::mutex mutex;
        bool opened = false;
        bool usable = false;
        std::string path;
        std::fstream file;
        uint64_t end = 0;
        std::unordered_map<uint64_t, Entry> index;
    };
}
