#pragma once
#include "world/level/Level.hpp"
#include "world/level/lighting/ChunkLighting.hpp"
#include "world/level/lighting/LightRegistry.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace perlin {
    // Sky and block light kept per chunk and changed only where the world changed.
    //
    // ChunkLighting::Compute lights a 48x48 window (the chunk and its eight neighbours) for every mesh, so each
    // chunk's light is worked out nine times over and again whenever a neighbour arrives. Here each chunk is lit
    // once when it arrives or changes: its own sources are flooded, the light already in its neighbours flows in,
    // light that leaves it flows out (at most 15 blocks, so into the eight neighbours only), and when it changes or
    // goes away the light that came from it is taken back out first. The values are the same fixed point the
    // window gives (tests/light_store_selftest.cpp checks them against it), so meshes do not change.
    //
    // Every call needs level.mutex held by the caller. The store lives in Level::lightCache, which Clear and
    // TakeFrom drop.
    class LightStore {
    public:
        // The store of this level, made on first use; registry gives every block's light properties.
        static LightStore& For(Level& level, const LightRegistry& registry);

        // Brings the chunks around pos up to date, then copies out the 18x18 padded light of rows yLo..yHi in
        // the layout of ChunkLighting::Result.
        void Extract(const Level& level, const ChunkPos& pos, int yLo, int yHi, ChunkLighting::Result& out);
        // Every chunk of the level up to date (tests).
        void SyncAll(const Level& level);

        struct Stats {
            uint64_t relit = 0, removed = 0, extracts = 0;
            uint64_t increaseSteps = 0, decreaseSteps = 0;
        };
        const Stats& Counters() const { return stats; }
        size_t Bytes() const;
        size_t Chunks() const { return columns.size(); }

        explicit LightStore(const LightRegistry& registry);
        ~LightStore();

    private:
        static constexpr int kSections = Chunk::SECTION_COUNT;

        struct Column {
            uint64_t version = 0;
            std::array<std::unique_ptr<uint8_t[]>, kSections> cells;
            std::array<uint8_t, kSections> uniform{};
            uint8_t Get(int section, int cell) const {
                return cells[static_cast<size_t>(section)] ? cells[static_cast<size_t>(section)][static_cast<size_t>(cell)]
                                                           : uniform[static_cast<size_t>(section)];
            }
            void Set(int section, int cell, uint8_t value);
            void Compact();
        };

        struct Work;
        void CheckGeneration();
        void Sync(const Level& level, const ChunkPos& pos);
        // Lights the chunk at pos afresh from `now`, or takes it away when now is null.
        void Update(const Level& level, const ChunkPos& pos, const Chunk* now);
        void Prepare(const Level& level, const ChunkPos& pos, bool selfPresent, const Chunk* self);

        std::unordered_map<ChunkPos, Column, ChunkPosHash> columns;
        std::vector<uint16_t> props;
        std::vector<uint16_t> seenIds;
        const LightRegistry& registry;
        uint64_t registryGeneration = 0;
        std::unique_ptr<Work> work;
        Stats stats;
    };
}
