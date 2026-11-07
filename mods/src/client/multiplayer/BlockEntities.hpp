#pragma once
#include "client/ui/Json.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace perlin {
    struct SignFace {
        std::string text;
        uint32_t color = 0xFF000000u;
        bool glow = false;
    };

    struct BlockEntity {
        std::string id;
        int color = -1;
        SignFace front, back;
        bool waxed = false;
        float rotation = 0.0f;
        int base = -1, bannerType = 0;
        std::vector<std::pair<std::string, int>> patterns;
        bool paired = false;
        int pairX = 0, pairZ = 0;
        std::string nbt;
    };

    class BlockEntityStore {
    public:
        void Set(int x, int y, int z, const ui::Json& nbt);
        bool Get(int x, int y, int z, BlockEntity& out) const;
        void Remove(int x, int y, int z);
        void RemoveChunk(int cx, int cz);
        void Clear();
        void TakeFrom(BlockEntityStore& other);
        size_t Size() const;
        void SetLidOpen(int x, int y, int z, bool open);
        float LidOpenness(int x, int y, int z) const;

    private:
        static uint64_t Key(int x, int y, int z) {
            return (static_cast<uint64_t>(static_cast<uint32_t>(x) & 0x3FFFFFFu) << 38) |
                   (static_cast<uint64_t>(static_cast<uint32_t>(z) & 0x3FFFFFFu) << 12) |
                   (static_cast<uint64_t>(y + 2048) & 0xFFFu);
        }
        mutable std::mutex mutex;
        std::unordered_map<uint64_t, BlockEntity> entities;
        struct Lid {
            bool open = false;
            float from = 0.0f;
            std::chrono::steady_clock::time_point at;
        };
        std::unordered_map<uint64_t, Lid> lids;
        static float LidAt(const Lid& lid, std::chrono::steady_clock::time_point now);
    };
}
