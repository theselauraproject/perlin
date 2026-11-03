#include "world/Chunk.hpp"

#include "world/Blocks.hpp"

#include <algorithm>
#include <map>

namespace perlin::server {
    namespace {
        void PutVarint(std::vector<uint8_t>& o, uint32_t v) {
            while (v >= 0x80) {
                o.push_back(static_cast<uint8_t>((v & 0x7F) | 0x80));
                v >>= 7;
            }
            o.push_back(static_cast<uint8_t>(v));
        }

        void PutZigzag(std::vector<uint8_t>& o, int32_t v) {
            PutVarint(o, (static_cast<uint32_t>(v) << 1) ^ static_cast<uint32_t>(v >> 31));
        }

        // paletted storage as the wire carries it: header (bits << 1 | 1 for runtime ids), packed words, palette
        void WriteStorage(std::vector<uint8_t>& out, const std::array<int32_t, kCells>& values) {
            std::vector<int32_t> palette;
            std::map<int32_t, uint32_t> lookup;
            std::array<uint16_t, kCells> idx;
            for (int i = 0; i < kCells; i++) {
                auto it = lookup.find(values[static_cast<size_t>(i)]);
                if (it == lookup.end()) {
                    it = lookup.emplace(values[static_cast<size_t>(i)], static_cast<uint32_t>(palette.size()))
                             .first;
                    palette.push_back(values[static_cast<size_t>(i)]);
                }
                idx[static_cast<size_t>(i)] = static_cast<uint16_t>(it->second);
            }
            unsigned need = 0;
            while ((1u << need) < palette.size())
                need++;
            // the wire only allows these widths
            const unsigned bits = need == 0   ? 0
                                  : need <= 1 ? 1
                                  : need <= 2 ? 2
                                  : need <= 3 ? 3
                                  : need <= 4 ? 4
                                  : need <= 5 ? 5
                                  : need <= 6 ? 6
                                  : need <= 8 ? 8
                                              : 16;
            out.push_back(static_cast<uint8_t>((bits << 1) | 1));
            if (bits == 0) {
                PutZigzag(out, palette[0]);
                return;
            }
            const unsigned perWord = 32 / bits;
            const unsigned words = (kCells + perWord - 1) / perWord;
            std::vector<uint32_t> packed(words, 0);
            for (unsigned i = 0; i < static_cast<unsigned>(kCells); i++)
                packed[i / perWord] |= static_cast<uint32_t>(idx[i]) << ((i % perWord) * bits);
            for (const uint32_t w : packed)
                for (int b = 0; b < 4; b++)
                    out.push_back(static_cast<uint8_t>((w >> (8 * b)) & 0xFF));
            PutZigzag(out, static_cast<int32_t>(palette.size()));
            for (const int32_t v : palette)
                PutZigzag(out, v);
        }
    }

    uint16_t ChunkColumn::Get(int x, int y, int z) const {
        if (x < 0 || x > 15 || z < 0 || z > 15 || y < kMinY || y >= kMinY + kHeight)
            return 0;
        const Section& s = sections[static_cast<size_t>((y - kMinY) >> 4)];
        return s.cells ? (*s.cells)[static_cast<size_t>(CellIndex(x, y - kMinY, z))] : s.uniform;
    }

    void ChunkColumn::Set(int x, int y, int z, uint16_t block) {
        if (x < 0 || x > 15 || z < 0 || z > 15 || y < kMinY || y >= kMinY + kHeight)
            return;
        Section& s = sections[static_cast<size_t>((y - kMinY) >> 4)];
        if (!s.cells) {
            if (s.uniform == block)
                return;
            s.cells = std::make_unique<std::array<uint16_t, kCells>>();
            s.cells->fill(s.uniform);
        }
        (*s.cells)[static_cast<size_t>(CellIndex(x, y - kMinY, z))] = block;
        version++;
    }

    ChunkColumn ChunkColumn::Clone() const {
        ChunkColumn c;
        for (size_t i = 0; i < sections.size(); i++) {
            c.sections[i].uniform = sections[i].uniform;
            if (sections[i].cells)
                c.sections[i].cells = std::make_unique<std::array<uint16_t, kCells>>(*sections[i].cells);
        }
        c.genBiomes = genBiomes;
        c.netBiomes = netBiomes;
        c.version = version;
        return c;
    }

    void ChunkColumn::FillSection(int section, uint16_t block) {
        Section& s = sections[static_cast<size_t>(section)];
        s.cells.reset();
        s.uniform = block;
    }

    void ChunkColumn::SetSection(int section, const uint16_t* cells) {
        Section& s = sections[static_cast<size_t>(section)];
        if (std::all_of(cells, cells + kCells, [&](uint16_t v) { return v == cells[0]; })) {
            FillSection(section, cells[0]);
            return;
        }
        if (!s.cells)
            s.cells = std::make_unique<std::array<uint16_t, kCells>>();
        std::copy(cells, cells + kCells, s.cells->begin());
    }

    void ChunkColumn::EncodeNetwork(std::vector<uint8_t>& out) const {
        const BlockRegistry& blocks = BlockRegistry::Instance();
        const int minSection = kMinY >> 4;
        std::array<int32_t, kCells> values;
        for (int sec = 0; sec < kSections; sec++) {
            const Section& s = sections[static_cast<size_t>(sec)];
            // version 9: storage count, absolute sub chunk index, then the storages
            out.push_back(9);
            out.push_back(1);
            out.push_back(static_cast<uint8_t>(static_cast<int8_t>(minSection + sec)));
            if (s.cells) {
                for (int i = 0; i < kCells; i++)
                    values[static_cast<size_t>(i)] = blocks.Hash((*s.cells)[static_cast<size_t>(i)]);
            } else {
                values.fill(blocks.Hash(s.uniform));
            }
            WriteStorage(out, values);
        }
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = 0; y < 16; y++)
                    values[static_cast<size_t>(CellIndex(x, y, z))] = netBiomes[static_cast<size_t>(x * 16 + z)];
        for (int sec = 0; sec < kSections; sec++)
            WriteStorage(out, values);
        // border blocks (education edition), none
        out.push_back(0);
    }
}
