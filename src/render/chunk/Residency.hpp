#pragma once
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace perlin::render {
    struct ColumnPos {
        int x = 0, z = 0;
        bool operator==(const ColumnPos& o) const { return x == o.x && z == o.z; }
    };

    struct UnitKey {
        int level = 0;
        int x = 0, z = 0;
        bool operator==(const UnitKey& o) const { return level == o.level && x == o.x && z == o.z; }
        int Width() const { return 1 << level; }
    };
    struct UnitKeyHash {
        size_t operator()(const UnitKey& k) const {
            uint64_t h =
                (static_cast<uint64_t>(static_cast<uint32_t>(k.x)) << 32) | static_cast<uint32_t>(k.z);
            h ^= static_cast<uint64_t>(k.level) * 0x9E3779B97F4A7C15ull;
            h *= 0xFF51AFD7ED558CCDull;
            return static_cast<size_t>(h ^ (h >> 32));
        }
    };
    struct ColumnPosHash {
        size_t operator()(const ColumnPos& p) const { return UnitKeyHash()(UnitKey{0, p.x, p.z}); }
    };

    struct ResidencyConfig {

        int fullRadius = 12;

        int lodStep = 4;
        int maxLevel = 3;

        int lodRadius = 64;

        int hysteresis = 1;

        uint64_t retainMs = 20000;
        uint64_t retainBudgetBytes = 256ull << 20;

        float lookaheadChunks = 3.0f;
        uint32_t maxBuildsPerTick = 8;
        uint32_t maxInFlight = 16;
    };

    struct BuildRequest {
        UnitKey unit;
        uint64_t version = 0;

        uint8_t neighbourMask = 0;
    };

    struct Plan {
        std::vector<BuildRequest> build;
        std::vector<UnitKey> evict;
        void Clear() {
            build.clear();
            evict.clear();
        }
    };

    struct ResidencyStats {
        uint32_t wanted = 0;
        uint32_t resident = 0;
        uint32_t retained = 0;
        uint64_t residentBytes = 0;
        uint64_t retainedBytes = 0;
        uint32_t inFlight = 0;
        uint64_t retainedRevived = 0;
        uint64_t retainedExpired = 0;
        uint64_t retainedOverBudget = 0;
    };

    class ChunkResidency {
    public:
        void Configure(const ResidencyConfig& c) { cfg = c; }
        const ResidencyConfig& Config() const { return cfg; }

        void SetData(ColumnPos pos, bool present);

        void MarkDirty(ColumnPos pos);

        void Tick(uint64_t nowMs, ColumnPos camera, float velX, float velZ, Plan& plan);

        void OnBuilt(const UnitKey& unit, uint64_t version, uint8_t neighbourMask, uint64_t bytes);
        void OnBuildFailed(const UnitKey& unit);

        void OnLost(const UnitKey& unit);
        void Reset();

        struct DrawItem {
            UnitKey unit;
        };

        void CollectDraw(std::vector<DrawItem>& out) const;

        ResidencyStats Stats() const;
        bool HasMesh(const UnitKey& unit) const { return units.contains(unit) && units.at(unit).hasMesh; }
        bool IsRetained(const UnitKey& unit) const {
            const auto it = units.find(unit);
            return it != units.end() && it->second.hasMesh && !it->second.wanted;
        }

        int LevelForDistance(int d) const;

    private:
        struct Column {
            bool present = false;
            uint64_t version = 0;
        };
        struct Unit {
            bool hasMesh = false;
            bool wanted = false;
            uint64_t meshVersion = 0;
            uint8_t meshMask = 0;
            uint64_t bytes = 0;
            uint64_t pendingVersion = 0;
            uint8_t pendingMask = 0;
            bool inFlight = false;
            uint32_t failures = 0;
            uint64_t retryAt = 0;
            uint64_t wantedVersion = 0;
            uint8_t wantedMask = 0;
            uint64_t unusedSince = 0;

            bool covering = false;
        };

        int ColumnLevel(ColumnPos c, ColumnPos cam) const;
        static UnitKey UnitFor(ColumnPos c, int level);
        uint8_t MaskFor(ColumnPos c) const;

        ResidencyConfig cfg;
        std::unordered_map<ColumnPos, Column, ColumnPosHash> columns;
        std::unordered_map<UnitKey, Unit, UnitKeyHash> units;

        std::unordered_set<ColumnPos, ColumnPosHash> holes;
        uint64_t nextVersion = 1;
        uint64_t lastNow = 0;
        mutable ResidencyStats stats;
        ColumnPos effectiveCamera{0, 0};
        bool haveCamera = false;
    };
}
