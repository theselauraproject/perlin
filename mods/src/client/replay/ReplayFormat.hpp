#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "client/ui/Json.hpp"

namespace perlin::replay {
    inline constexpr int kFormatVersion = 1;
    inline constexpr const char* kExtension = ".perlinreplay";

    enum class Dir : uint8_t { ToClient = 0, ToServer = 1, Snapshot = 2 };

    struct Record {
        Dir dir = Dir::ToClient;
        uint32_t timeMs = 0;
        uint32_t id = 0;
        std::vector<uint8_t> body;
    };

    struct Marker {
        uint32_t timeMs = 0;
        std::string name;
        uint32_t color = 0xFFFFFF;
    };

    struct ChunkInfo {
        std::string file;
        uint32_t startMs = 0, endMs = 0, records = 0;
        bool snapshot = false;
    };

    struct Pose {
        float x = 0, y = 0, z = 0, yaw = 0, pitch = 0;
        bool known = false;
    };

    struct Metadata {
        int version = kFormatVersion;
        std::string name, server, gameVersion, playerName, perlinVersion;
        int protocol = 0;
        int64_t createdUnix = 0;
        uint32_t durationMs = 0;
        uint64_t localRuntimeId = 0;
        Pose startPose;
        std::vector<ChunkInfo> chunks;
        std::vector<Marker> markers;
        ui::Json ToJson() const;
        static bool FromJson(const ui::Json& j, Metadata& out, std::string& error);
    };

    void EncodeRecord(const Record& r, std::vector<uint8_t>& out);
    size_t DecodeRecord(const uint8_t* data, size_t size, Record& out);

    bool IsHandshakePacket(uint32_t id);

    class StateCache {
    public:
        void Reset();
        void OnToClient(uint32_t id, const std::vector<uint8_t>& body);
        std::vector<Record> Snapshot() const;
        size_t Bytes() const;

    private:
        struct Entry {
            uint64_t seq = 0;
            uint32_t id = 0;
            std::vector<uint8_t> body;
            uint64_t unique = 0, runtime = 0;
        };
        mutable std::mutex mutex;
        uint64_t seq = 0;
        bool joined = false;
        size_t chunkBytes = 0, joinBytes = 0, listBytes = 0;
        std::vector<Entry> join;
        std::map<uint64_t, Entry> chunks;
        std::vector<Entry> blockUpdates;
        std::vector<Entry> playerLists;
        std::map<uint64_t, Entry> entities;
        std::map<std::pair<uint32_t, uint64_t>, Entry> latest;
        std::map<uint32_t, Entry> singletons;
        void DropEntity(uint64_t id);
    };

    class Recorder {
    public:
        ~Recorder();
        using SnapshotProvider = std::function<std::vector<Record>()>;
        bool Start(const std::string& workDir, Metadata meta, const std::vector<Record>& snapshot,
                   std::string& error, SnapshotProvider provider = nullptr, uint32_t periodicMs = 0,
                   size_t maxSnapshotBytes = 0);
        void Push(Dir dir, uint32_t id, const std::vector<uint8_t>& body);
        void AddMarker(const std::string& name);
        bool Stop(const std::string& outPath, const std::vector<uint8_t>& thumbnailPng, std::string& error);
        void Abort();
        bool Active() const { return active; }
        uint32_t ElapsedMs() const;
        uint64_t BytesWritten() const { return bytesTotal; }

    private:
        void FlushLocked();
        mutable std::mutex mutex;
        bool active = false;
        std::string dir;
        Metadata meta;
        std::vector<uint8_t> buffer;
        uint32_t bufferStartMs = 0, lastMs = 0, bufferRecords = 0;
        int64_t startTick = 0;
        uint64_t bytesTotal = 0;
        std::vector<ChunkInfo> chunkInfos;
        SnapshotProvider provider;
        uint32_t periodicMs = 0, nextSnapshotMs = 0;
        size_t maxSnapshotBytes = 0;
        bool nextChunkSnapshot = false;
    };

    class Reader {
    public:
        bool Open(const std::string& path, std::string& error);
        static bool ReadMetadata(const std::string& path, Metadata& out, std::vector<uint8_t>* thumbnail,
                                 std::string& error);
        const Metadata& Meta() const { return meta; }
        bool LoadChunk(size_t index, std::vector<Record>& out, std::string& error);
        std::string ReadFile(const std::string& name);
        const std::string& Path() const { return path; }

    private:
        std::string path;
        Metadata meta;
        std::shared_ptr<void> zip;
    };

    bool PackArchive(const std::string& dir, const Metadata& meta, const std::vector<uint8_t>& thumbnail,
                     const std::string& outPath, std::string& error);
    bool UpdateArchiveFile(const std::string& path, const std::string& name, const std::string& content,
                           std::string& error);
    struct ArchiveAdd {
        std::string name;
        std::string diskPath;
        std::string content;
    };
    bool AddArchiveFiles(const std::string& path, const std::vector<ArchiveAdd>& files, std::string& error);
    std::string SafeFileName(const std::string& name);
    std::string ReplayDirectory();
}
