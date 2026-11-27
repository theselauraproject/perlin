#include "world/level/chunk/ChunkCodec.hpp"
#include "world/level/Level.hpp"
#include "world/level/chunk/ChunkLoadOrder.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {
    using namespace perlin;
    int checks = 0;
    int failures = 0;

    void Check(bool okay, const char* text) {
        ++checks;
        if (!okay) {
            ++failures;
            std::printf("FAIL %s\n", text);
        }
    }

    // The old algorithm is retained only in this differential correctness probe.
    bool Reference(const PaletteStorage& storage, ChunkCodec::CellIndices& out) {
        if (storage.palette.empty())
            return false;
        if (storage.bitsPerBlock == 0) {
            out.fill(0);
            return true;
        }
        const auto bits = static_cast<unsigned>(storage.bitsPerBlock);
        const unsigned perWord = 32 / bits;
        const uint32_t mask = (1u << bits) - 1;
        if (storage.words.size() < (ChunkCodec::kCells + perWord - 1) / perWord)
            return false;
        for (unsigned cell = 0; cell < ChunkCodec::kCells; ++cell) {
            const uint32_t index = (storage.words[cell / perWord] >> ((cell % perWord) * bits)) & mask;
            if (index >= storage.palette.size())
                return false;
            out[cell] = static_cast<uint16_t>(index);
        }
        return true;
    }

    void Compare(const PaletteStorage& storage, const char* description) {
        ChunkCodec::CellIndices expected, actual;
        expected.fill(0xabcd);
        actual = expected;
        const bool expectedResult = Reference(storage, expected);
        const bool actualResult = ChunkCodec::ExpandIndices(storage, actual);
        Check(actualResult == expectedResult && actual == expected, description);
    }

    void CheckPreparedSections() {
        using Cells = std::array<uint16_t, ChunkCodec::kCells>;
        Cells cells{}, liquid{};
        std::array<uint8_t, ChunkCodec::kCells> biomes{};
        Level level;
        level.EnsureChunk(-2, 3);
        for (int cell = 0; cell < ChunkCodec::kCells; ++cell) {
            cells[static_cast<size_t>(cell)] = static_cast<uint16_t>(1 + (cell * 17) % 301);
            liquid[static_cast<size_t>(cell)] = cell % 13 == 0 ? 901 : 0;
            biomes[static_cast<size_t>(cell)] = static_cast<uint8_t>(cell % 251);
        }
        level.SetBiomeSubChunk(-2, 3, -4, biomes.data());
        auto prepared = Chunk::PrepareSection(cells.data(), liquid.data());
        Check(level.GetBlock(-32, -64, 48) == 0, "preparation does not mutate the live world");
        Check(level.SetPreparedSubChunk(-2, 3, -4, std::move(prepared), level.CaptureChunkWriteTarget(-2, 3)),
              "prepared nonuniform block and liquid storage commits");
        bool same = true;
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                for (int y = 0; y < 16; ++y) {
                    const size_t cell = static_cast<size_t>(x * 256 + z * 16 + y);
                    same &= level.GetBlock(-32 + x, -64 + y, 48 + z) == cells[cell];
                    same &= level.GetLiquid(-32 + x, -64 + y, 48 + z) == liquid[cell];
                    same &= level.GetBiome(-32 + x, -64 + y, 48 + z) == biomes[cell];
                }
        Check(same, "all 4096 network-order cells transpose correctly and preserve existing biomes");

        std::vector<DirtyChunk> dirty;
        level.DrainDirty(dirty);
        Check(dirty.size() == 1, "biome and block updates deduplicate the dirty column");
        const uint64_t changes = level.changeCounter;
        const uint64_t version = dirty.empty() ? 0 : dirty.front().version;
        const uint64_t border = dirty.empty() ? 0 : dirty.front().borderVersion;
        prepared = Chunk::PrepareSection(cells.data(), liquid.data());
        Check(level.SetPreparedSubChunk(-2, 3, -4, std::move(prepared), level.CaptureChunkWriteTarget(-2, 3)),
              "an identical prepared section is accepted");
        dirty.clear();
        level.DrainDirty(dirty);
        Check(dirty.empty() && level.changeCounter == changes,
              "identical prepared data does not bump counters or dirty the column");

        cells.fill(37);
        prepared = Chunk::PrepareSection(cells.data());
        Check(level.SetPreparedSubChunk(-2, 3, -4, std::move(prepared), level.CaptureChunkWriteTarget(-2, 3)),
              "uniform replacement removes the former extra liquid layer");
        level.DrainDirty(dirty);
        Check(dirty.size() == 1 && dirty.front().version == version + 1 &&
                  dirty.front().borderVersion == border + 1 && level.changeCounter == changes + 1,
              "one changed section advances version, border version, and change counter exactly once");
        Check(level.GetBlock(-29, -57, 57) == 37 && level.GetLiquid(-32, -64, 48) == 0 &&
                  level.GetBiome(-29, -57, 57) == biomes[3 * 256 + 9 * 16 + 7],
              "uniform block replacement retains biomes while clearing absent waterlogging");

        const auto beforeClear = level.CaptureChunkWriteTarget(-2, 3);
        cells.fill(90);
        prepared = Chunk::PrepareSection(cells.data());
        level.Clear();
        level.EnsureChunk(-2, 3);
        level.SetBlock(-32, -64, 48, 123);
        const uint64_t afterClearChanges = level.changeCounter;
        Check(!level.SetPreparedSubChunk(-2, 3, -4, std::move(prepared), beforeClear),
              "a section prepared before world reset is rejected under the final commit lock");
        Check(level.GetBlock(-32, -64, 48) == 123 && level.changeCounter == afterClearChanges,
              "stale prepared work cannot repopulate or dirty the replacement world");

        const auto beforeReload = level.CaptureChunkWriteTarget(-2, 3);
        prepared = Chunk::PrepareSection(cells.data());
        level.RemoveChunk(-2, 3);
        level.EnsureChunk(-2, 3);
        level.SetBlock(-32, -64, 48, 124);
        Check(!level.SetPreparedSubChunk(-2, 3, -4, std::move(prepared), beforeReload) &&
                  level.GetBlock(-32, -64, 48) == 124,
              "remove and recreate at the same coordinate rejects an old chunk incarnation");

        const auto beforeTransfer = level.CaptureChunkWriteTarget(-2, 3);
        Level adopted;
        adopted.EnsureChunk(-2, 3);
        const auto adoptedBefore = adopted.CaptureChunkWriteTarget(-2, 3);
        auto oldDestination = Chunk::PrepareSection(cells.data());
        adopted.TakeFrom(level);
        Check(!adopted.SetPreparedSubChunk(-2, 3, -4, std::move(oldDestination), adoptedBefore),
              "adopting a world invalidates work from the destination's previous contents");
        level.EnsureChunk(-2, 3);
        prepared = Chunk::PrepareSection(cells.data());
        Check(!level.SetPreparedSubChunk(-2, 3, -4, std::move(prepared), beforeTransfer),
              "transferring a world also invalidates prepared work on the source");

        Level undo;
        undo.EnableUndo(64u << 20);
        undo.SetEpoch(0);
        undo.EnsureChunk(0, 0);
        cells.fill(41);
        undo.SetSubChunk(0, 0, 0, cells.data(), liquid.data());
        undo.SetEpoch(1);
        cells.fill(42);
        undo.SetSubChunk(0, 0, 0, cells.data());
        const auto beforeRewind = undo.CaptureChunkWriteTarget(0, 0);
        prepared = Chunk::PrepareSection(cells.data());
        Check(undo.RewindTo(1) && undo.GetBlock(0, 0, 0) == 41 && undo.GetLiquid(0, 0, 0) == 901,
              "undo restores the pre-commit blocks and liquid layer");
        Check(!undo.SetPreparedSubChunk(0, 0, 0, std::move(prepared), beforeRewind) &&
                  undo.GetBlock(0, 0, 0) == 41,
              "prepared future data cannot leak across a rewind");

        Cells16 narrow, wide;
        cells.fill(17);
        narrow.Load(cells.data());
        for (int cell = 0; cell < ChunkCodec::kCells; ++cell)
            cells[static_cast<size_t>(cell)] = static_cast<uint16_t>(cell % 301);
        wide.Load(cells.data());
        for (int cell = 0; cell < ChunkCodec::kCells; ++cell)
            wide.Set(cell, 17);
        Check(narrow.Equals(wide) && wide.Equals(narrow),
              "prepared equality compares values across narrow and wide storage representations");
        narrow.Set(13, 19);
        narrow.Set(13, 17);
        Check(narrow.Equals(wide), "unused palette entries do not make equal cell values different");
        wide.Set(4095, 18);
        Check(!narrow.Equals(wide), "the final cell participates in prepared equality");
    }

    void CheckReceiveOrder() {
        ChunkLoadOrder order;
        order.Synchronize(7);
        const auto column = order.BeginColumn(3, -4);
        const auto oldBlob = order.BeginSlot(3, -2, -4);
        const auto newerDirect = order.BeginSlot(3, -2, -4);
        Check(!order.CurrentSlot(oldBlob) && order.CurrentSlot(newerDirect),
              "a late blob completion cannot overwrite a newer direct reply for the same section");
        Check(order.CurrentColumn(column) && !order.MayWriteSection(column, -2) &&
                  order.MayWriteSection(column, -1),
              "an older cached column may fill untouched sections without erasing newer section data");
        const auto otherSlot = order.BeginSlot(3, -1, -4);
        Check(order.CurrentSlot(newerDirect) && order.CurrentSlot(otherSlot),
              "independent sections do not cancel one another");
        order.RecordSectionWrite(newerDirect, 4);
        Check(!order.MayWriteSection(column, 4),
              "a payload Y override stamps its actual destination against an older cached column");
        const auto replacement = order.BeginColumn(3, -4);
        Check(!order.CurrentColumn(column) && !order.CurrentSlot(otherSlot) &&
                  order.CurrentColumn(replacement),
              "whole-column replacement invalidates all pending old section and column blobs");
        const auto beforeUnload = order.BeginSlot(3, 0, -4);
        order.ForgetColumn(3, -4);
        order.BeginColumn(3, -4);
        Check(!order.CurrentSlot(beforeUnload), "unload and reload cannot revive a pending old section");
        const auto beforeReset = order.BeginSlot(3, 0, -4);
        order.Synchronize(8);
        order.BeginColumn(3, -4);
        order.BeginSlot(3, 0, -4);
        Check(!order.CurrentSlot(beforeReset), "world-generation changes invalidate every deferred ticket");
    }
}

int main() {
    PaletteStorage uniform;
    ChunkCodec::CellIndices out;
    out.fill(0xabcd);
    Check(!ChunkCodec::ExpandIndices(uniform, out) && out.front() == 0xabcd,
          "empty palette is rejected without output writes");
    uniform.palette = {73};
    Compare(uniform, "zero-bit storage expands to index zero");

    for (const int bits : {1, 2, 3, 4, 5, 6, 8, 16}) {
        const unsigned perWord = 32u / static_cast<unsigned>(bits);
        const uint32_t mask = (1u << bits) - 1;
        for (unsigned seed = 0; seed < 16; ++seed) {
            PaletteStorage storage;
            storage.bitsPerBlock = bits;
            const unsigned maxPalette = std::min(4096u, mask + 1);
            const unsigned paletteSize = seed == 0 ? maxPalette : 1 + (seed * 101) % maxPalette;
            storage.palette.resize(paletteSize);
            storage.words.assign((ChunkCodec::kCells + perWord - 1) / perWord, UINT32_MAX);
            ChunkCodec::CellIndices direct;
            uint32_t random = UINT32_C(0x9e3779b9) ^ seed;
            for (unsigned cell = 0; cell < ChunkCodec::kCells; ++cell) {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                const uint32_t index = random % paletteSize;
                direct[cell] = static_cast<uint16_t>(index);
                const unsigned shift = (cell % perWord) * static_cast<unsigned>(bits);
                auto& word = storage.words[cell / perWord];
                word = (word & ~(mask << shift)) | (index << shift);
            }
            Check(ChunkCodec::ExpandIndices(storage, out) && out == direct,
                  "every supported packing width preserves literal indices and ignores word padding");
            Compare(storage, "valid random storage matches old decoder exactly");
            storage.words.push_back(UINT32_MAX);
            Compare(storage, "extra words are ignored");
            storage.words.pop_back();
            if (paletteSize <= mask) {
                for (const unsigned cell : {0u, 2048u, 4095u}) {
                    const unsigned wordIndex = cell / perWord;
                    const unsigned shift = (cell % perWord) * static_cast<unsigned>(bits);
                    const uint32_t saved = storage.words[wordIndex];
                    storage.words[wordIndex] = (saved & ~(mask << shift)) | (mask << shift);
                    Compare(storage, "invalid palette index rejects at the same cell with the same output prefix");
                    storage.words[wordIndex] = saved;
                }
            }
            storage.words.pop_back();
            Compare(storage, "truncated packed words reject before output writes");
        }
    }
    for (const int bits : {-1, 7, 9, 15, 17, 31, 32, 33,
                           std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
        PaletteStorage storage;
        storage.bitsPerBlock = bits;
        storage.palette = {1};
        storage.words.resize(4096);
        out.fill(0xabcd);
        Check(!ChunkCodec::ExpandIndices(storage, out) && out.front() == 0xabcd,
              "unsupported packing widths reject without shifts or divisions");
    }
    CheckPreparedSections();
    CheckReceiveOrder();
    std::printf("chunk_ingest_selftest: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
