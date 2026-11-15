#pragma once
#include <cstdint>
#include <iterator>
#include <map>

namespace perlin {
    class RangeAllocator {
    public:
        RangeAllocator() = default;
        explicit RangeAllocator(uint32_t capacity) { Reset(capacity); }

        void Reset(uint32_t capacity) {
            cap = capacity;
            blocks.clear();
            if (cap)
                blocks[0] = cap;
        }

        bool Alloc(uint32_t n, uint32_t& off) {
            if (n == 0) {
                off = 0;
                return true;
            }
            for (auto it = blocks.begin(); it != blocks.end(); ++it) {
                if (it->second < n)
                    continue;
                off = it->first;
                uint32_t start = it->first + n, rem = it->second - n;
                blocks.erase(it);
                if (rem)
                    blocks[start] = rem;
                return true;
            }
            return false;
        }

        void Free(uint32_t off, uint32_t n) {
            if (n == 0)
                return;
            auto next = blocks.lower_bound(off);
            if (next != blocks.begin()) {
                auto prev = std::prev(next);
                if (prev->first + prev->second == off) {
                    off = prev->first;
                    n += prev->second;
                    blocks.erase(prev);
                }
            }
            if (next != blocks.end() && off + n == next->first) {
                n += next->second;
                blocks.erase(next);
            }
            blocks[off] = n;
        }

        uint32_t Capacity() const { return cap; }
        uint32_t FreeTotal() const {
            uint32_t t = 0;
            for (auto& [o, l] : blocks)
                t += l;
            return t;
        }
        size_t FreeBlocks() const { return blocks.size(); }
        bool Unused() const {
            return cap != 0 && blocks.size() == 1 && blocks.begin()->first == 0 &&
                   blocks.begin()->second == cap;
        }

    private:
        uint32_t cap = 0;
        std::map<uint32_t, uint32_t> blocks;
    };
}
