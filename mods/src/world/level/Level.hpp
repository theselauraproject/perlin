#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace perlin {
    struct ChunkPos {
        int x, z;
        bool operator==(const ChunkPos& o) const { return x == o.x && z == o.z; }
    };

    struct ChunkPosHash {
        size_t operator()(const ChunkPos& p) const {
            return std::hash<int64_t>()((static_cast<int64_t>(p.x) << 32) | static_cast<uint32_t>(p.z));
        }
    };

    class Chunk {
    public:
        static constexpr int SIZE = 16;
        static constexpr int MIN_Y = -64;
        static constexpr int HEIGHT = 384;
        static constexpr int SECTION_COUNT = HEIGHT / 16;
        static constexpr uint8_t DEFAULT_BIOME = 1;

        bool HasSection(int y) const {
            return y >= MIN_Y && y < MIN_Y + HEIGHT && sections[(y - MIN_Y) >> 4] != nullptr;
        }
        uint16_t Get(int x, int y, int z) const;
        void Set(int x, int y, int z, uint16_t id);
        uint16_t GetLiquid(int x, int y, int z) const;
        void SetLiquid(int x, int y, int z, uint16_t id);
        void ReadLiquid(int x, int z, int yLo, int yHi, uint16_t* out, size_t stride) const;
        uint8_t GetBiome(int x, int y, int z) const;

        bool YRange(int& lo, int& hi) const;
        void ReadColumn(int x, int z, int yLo, int yHi, uint16_t* blocks, uint8_t* biomes,
                        size_t stride) const;

        void ReadBlocks(int x, int z, int yLo, int yHi, uint16_t* blocks, size_t stride) const;
        void CopyRun(int y, int z, int x0, int n, uint16_t* blocks, uint8_t* biomes, uint16_t* liquid) const;

        void SetSection(int section, const uint16_t* cells, const uint16_t* liquidCells = nullptr);
        void SetBiomeSection(int section, const uint8_t* cells);

        Chunk Clone() const;
        bool SectionEquals(int section, const uint16_t* cells, const uint16_t* liquidCells) const;
        bool BiomeSectionEquals(int section, const uint8_t* cells) const;
        size_t ApproxBytes() const;

        uint64_t version = 0;
        uint64_t borderVersion = 0;
        bool streaming = false;
        uint64_t readyAt = 0;
        uint64_t firstSeen = 0;
        bool queued = false;
        bool lodOnly = false;

    private:
        struct Section {
            std::unique_ptr<std::array<uint16_t, 4096>> blocks;
            std::unique_ptr<std::array<uint16_t, 4096>> liquid;
            std::unique_ptr<std::array<uint8_t, 4096>> biomes;
            uint16_t uniform = 0;
            uint8_t uniformBiome = DEFAULT_BIOME;
            uint16_t Block(int cell) const { return blocks ? (*blocks)[static_cast<size_t>(cell)] : uniform; }
            uint8_t Biome(int cell) const {
                return biomes ? (*biomes)[static_cast<size_t>(cell)] : uniformBiome;
            }
            void SetBlock(int cell, uint16_t id) {
                if (!blocks) {
                    if (id == uniform)
                        return;
                    blocks = std::make_unique<std::array<uint16_t, 4096>>();
                    blocks->fill(uniform);
                }
                (*blocks)[static_cast<size_t>(cell)] = id;
            }
        };

        static int Local(int x, int y, int z) { return (((y & 15) << 4) + z) * 16 + x; }
        static bool InBounds(int x, int y, int z) {
            return x >= 0 && x < SIZE && z >= 0 && z < SIZE && y >= MIN_Y && y < MIN_Y + HEIGHT;
        }
        Section& Ensure(int section);

        std::array<std::unique_ptr<Section>, SECTION_COUNT> sections;
    };

    struct DirtyChunk {
        ChunkPos pos;
        bool exists = false;
        uint64_t version = 0;
        uint64_t borderVersion = 0;
        bool streaming = false;
        uint64_t firstSeen = 0;
        uint64_t readyAt = 0;
        bool lodOnly = false;
    };

    class Level {
    public:
        void DrainDirty(std::vector<DirtyChunk>& out);
        void MarkAllDirty();
        static uint64_t NowMs();

        uint16_t GetBlock(int x, int y, int z) const;
        uint16_t GetBlock(int x, int y, int z, bool& loaded) const;
        uint16_t GetLiquid(int x, int y, int z) const;
        void SetLiquid(int x, int y, int z, uint16_t id);
        uint8_t GetBiome(int x, int y, int z) const;
        void SetBlock(int x, int y, int z, uint16_t id);
        void EnsureChunk(int cx, int cz, bool streaming = false, bool lodOnly = false);
        bool PromoteLodOnly(int cx, int cz);
        bool RemoveLodOnly(int cx, int cz);
        void MarkComplete(int cx, int cz);
        bool HasChunk(int cx, int cz) const;
        bool HasSectionAt(int x, int y, int z) const;
        bool RemoveChunk(int cx, int cz);
        void SetSubChunk(int cx, int cz, int absSubY, const uint16_t* cells,
                         const uint16_t* liquidCells = nullptr);
        void SetBiomeSubChunk(int cx, int cz, int absSubY, const uint8_t* cells);
        void Clear();

        void EnableUndo(size_t capBytes);
        void SetEpoch(int64_t epoch) { currentEpoch.store(epoch); }
        int64_t Epoch() const { return currentEpoch.load(); }
        bool RewindTo(int64_t epoch);
        bool CanRewindTo(int64_t epoch) const;
        void TakeFrom(Level& other);

    private:
        struct UndoEntry {
            int64_t epoch = 0;
            ChunkPos pos{0, 0};
            bool existed = false;
            Chunk data;
            size_t bytes = 0;
        };
        void LogBefore(const ChunkPos& pos);
        std::deque<UndoEntry> undo;
        std::unordered_map<ChunkPos, int64_t, ChunkPosHash> loggedEpoch;
        size_t undoBytes = 0, undoCap = 0;
        bool undoOn = false;
        int64_t undoFloor = 0;
        std::atomic<int64_t> currentEpoch{0};

        void Touch(const ChunkPos& pos, Chunk& chunk) {
            if (!chunk.queued) {
                chunk.queued = true;
                dirty.push_back(pos);
            }
        }
        void TouchNeighbors(int cx, int cz);

    public:
        int FindBlockBelow(int x, int z, int fromY, const std::function<bool(uint16_t)>& accept) const;

        std::unordered_map<ChunkPos, Chunk, ChunkPosHash> chunks;
        std::vector<ChunkPos> dirty;
        mutable std::mutex mutex;
        std::atomic<int> lodOnlyCount{0};
        std::atomic<uint64_t> changeCounter{0};
        std::atomic<uint64_t> clearCounter{0};
    };
}
