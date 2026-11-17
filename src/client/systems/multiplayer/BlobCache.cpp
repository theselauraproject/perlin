#include "client/systems/multiplayer/BlobCache.hpp"
#include "util/Env.hpp"
#include "util/Log.hpp"
#include "util/Paths.hpp"

#include <cstring>
#include <filesystem>
#include <utility>

namespace perlin {
    namespace {
        constexpr char kMagic[8] = {'P', 'B', 'L', 'O', 'B', 'S', '0', '1'};
        constexpr size_t kRecordHeader = 16;
        constexpr uint64_t kCapBytes = 256ull << 20;
        constexpr uint32_t kMaxBlob = 1u << 20;

        uint32_t Check(const uint8_t* data, size_t size) {
            uint32_t h = 2166136261u;
            for (size_t i = 0; i < size; i++)
                h = (h ^ data[i]) * 16777619u;
            return h;
        }

        void PutU64(uint8_t* p, uint64_t v) {
            for (int i = 0; i < 8; i++)
                p[i] = static_cast<uint8_t>(v >> (i * 8));
        }
        void PutU32(uint8_t* p, uint32_t v) {
            for (int i = 0; i < 4; i++)
                p[i] = static_cast<uint8_t>(v >> (i * 8));
        }
        uint64_t GetU64(const uint8_t* p) {
            uint64_t v = 0;
            for (int i = 0; i < 8; i++)
                v |= static_cast<uint64_t>(p[i]) << (i * 8);
            return v;
        }
        uint32_t GetU32(const uint8_t* p) {
            uint32_t v = 0;
            for (int i = 0; i < 4; i++)
                v |= static_cast<uint32_t>(p[i]) << (i * 8);
            return v;
        }
    }

    BlobCache& BlobCache::Instance() {
        static BlobCache cache(PERLIN_ENV("PERLIN_NO_BLOB_CACHE")
                                   ? std::string()
                                   : (std::filesystem::path(paths::DataDir()) / "cache" / "chunk_blobs.bin").string());
        return cache;
    }

    BlobCache::BlobCache(std::string p) : path(std::move(p)) {}

    bool BlobCache::Usable() {
        std::lock_guard lock(mutex);
        OpenLocked();
        return usable;
    }

    void BlobCache::OpenLocked() {
        if (opened)
            return;
        opened = true;
        if (path.empty())
            return;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        if (!std::filesystem::exists(path, ec))
            std::ofstream(path, std::ios::binary).write(kMagic, sizeof(kMagic));
        file.open(path, std::ios::in | std::ios::out | std::ios::binary);
        if (!file) {
            Log("chunk", "blob cache unavailable at {}", path);
            return;
        }
        usable = true;
        char magic[sizeof(kMagic)] = {};
        file.read(magic, sizeof(magic));
        if (!file || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
            ResetLocked();
            return;
        }
        end = sizeof(kMagic);
        uint8_t head[kRecordHeader];
        std::vector<uint8_t> body;
        for (;;) {
            file.seekg(static_cast<std::streamoff>(end));
            if (!file.read(reinterpret_cast<char*>(head), kRecordHeader))
                break;
            const uint64_t hash = GetU64(head);
            const uint32_t size = GetU32(head + 8), check = GetU32(head + 12);
            if (size == 0 || size > kMaxBlob)
                break;
            body.resize(size);
            if (!file.read(reinterpret_cast<char*>(body.data()), size) || Check(body.data(), size) != check)
                break;
            index[hash] = {end + kRecordHeader, size, check};
            end += kRecordHeader + size;
        }
        file.clear();
        if (std::filesystem::file_size(path, ec) != end && !ec)
            std::filesystem::resize_file(path, end, ec);
        Log("chunk", "blob cache: {} blobs, {} KB", index.size(), end / 1024);
    }

    void BlobCache::ResetLocked() {
        file.close();
        std::ofstream(path, std::ios::binary | std::ios::trunc).write(kMagic, sizeof(kMagic));
        file.open(path, std::ios::in | std::ios::out | std::ios::binary);
        usable = static_cast<bool>(file);
        index.clear();
        end = sizeof(kMagic);
    }

    bool BlobCache::Get(uint64_t hash, std::vector<uint8_t>& out) {
        std::lock_guard lock(mutex);
        OpenLocked();
        if (!usable)
            return false;
        const auto it = index.find(hash);
        if (it == index.end())
            return false;
        out.resize(it->second.size);
        file.clear();
        file.seekg(static_cast<std::streamoff>(it->second.offset));
        if (!file.read(reinterpret_cast<char*>(out.data()), it->second.size) ||
            Check(out.data(), out.size()) != it->second.check) {
            file.clear();
            index.erase(it);
            return false;
        }
        return true;
    }

    void BlobCache::Put(uint64_t hash, const uint8_t* data, size_t size) {
        if (size == 0 || size > kMaxBlob)
            return;
        std::lock_guard lock(mutex);
        OpenLocked();
        if (!usable || index.contains(hash))
            return;
        if (end + kRecordHeader + size > kCapBytes)
            ResetLocked();
        uint8_t head[kRecordHeader];
        const uint32_t check = Check(data, size);
        PutU64(head, hash);
        PutU32(head + 8, static_cast<uint32_t>(size));
        PutU32(head + 12, check);
        file.clear();
        file.seekp(static_cast<std::streamoff>(end));
        file.write(reinterpret_cast<const char*>(head), kRecordHeader);
        file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!file) {
            file.clear();
            return;
        }
        index[hash] = {end + kRecordHeader, static_cast<uint32_t>(size), check};
        end += kRecordHeader + size;
    }

    void BlobCache::Flush() {
        std::lock_guard lock(mutex);
        if (usable)
            file.flush();
    }
}
