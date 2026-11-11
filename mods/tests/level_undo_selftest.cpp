#include <cassert>
#include <cstdio>
#include <vector>
#include "world/level/Level.hpp"
using namespace perlin;
int main() {
    Level lv;
    lv.EnableUndo(64u << 20);
    lv.SetEpoch(0);
    lv.EnsureChunk(0, 0);
    lv.EnsureChunk(1, 0);
    lv.SetBlock(1, 70, 1, 5);
    lv.SetEpoch(1);
    lv.SetBlock(1, 70, 1, 6);
    lv.SetBlock(17, 70, 1, 9);
    lv.SetEpoch(2);
    lv.RemoveChunk(1, 0);
    lv.EnsureChunk(2, 0);
    lv.SetBlock(33, 10, 3, 4);
    lv.SetEpoch(3);
    lv.SetBlock(1, 70, 1, 7);
    assert(lv.GetBlock(1, 70, 1) == 7 && !lv.HasChunk(1, 0) && lv.HasChunk(2, 0));
    assert(lv.RewindTo(3));
    assert(lv.GetBlock(1, 70, 1) == 6);
    assert(lv.RewindTo(2));
    assert(lv.HasChunk(1, 0) && lv.HasChunk(2, 0) && lv.GetBlock(17, 70, 1) == 9 &&
           lv.GetBlock(33, 10, 3) == 4);
    assert(lv.RewindTo(1));
    assert(lv.GetBlock(1, 70, 1) == 5 && lv.GetBlock(17, 70, 1) == 0);
    {
        Level l2;
        l2.EnableUndo(64u << 20);
        l2.SetEpoch(0);
        l2.EnsureChunk(0, 0);
        std::vector<uint16_t> cells(4096, 3);
        l2.SetSubChunk(0, 0, 4, cells.data());
        std::vector<DirtyChunk> d1;
        l2.DrainDirty(d1);
        l2.SetEpoch(1);
        l2.SetSubChunk(0, 0, 4, cells.data());
        std::vector<DirtyChunk> d2;
        l2.DrainDirty(d2);
        assert(d2.empty());
    }
    lv.SetBlock(1, 70, 1, 8);
    lv.SetEpoch(2);
    lv.SetBlock(1, 70, 1, 3);
    assert(lv.RewindTo(2) && lv.GetBlock(1, 70, 1) == 8);
    Level other;
    other.TakeFrom(lv);
    assert(other.GetBlock(1, 70, 1) == 8 && !lv.HasChunk(0, 0));
    assert(other.RewindTo(1) && other.GetBlock(1, 70, 1) == 5);
    Level small;
    small.EnableUndo(1);
    small.SetEpoch(0);
    small.EnsureChunk(0, 0);
    small.SetBlock(1, 5, 1, 1);
    small.SetEpoch(1);
    small.SetBlock(1, 5, 1, 2);
    small.SetEpoch(2);
    small.SetBlock(1, 5, 1, 3);
    assert(!small.CanRewindTo(0));
    std::puts("level_undo_selftest ok");
}
