#include "world/level/chunk/ChunkCodec.hpp"

#include "network/VarInt.hpp"

#include <algorithm>
#include <map>
#include <limits>

namespace perlin {
    namespace {

        bool ValidBits(unsigned b) {
            return b == 0 || b == 1 || b == 2 || b == 3 || b == 4 || b == 5 || b == 6 || b == 8 || b == 16;
        }

        size_t WordCount(unsigned bpb) {
            if (bpb == 0)
                return 0;
            unsigned perWord = 32 / bpb;
            return (4096 + perWord - 1) / perWord;
        }

        PersistentResolver gResolver = nullptr;

        bool NbtString(wire::Reader& r, std::string& out) {
            return r.ReadString(out);
        }

        bool SkipPayload(wire::Reader& r, uint8_t type, int depth);

        bool SkipCompound(wire::Reader& r, int depth) {
            for (;;) {
                uint8_t t = 0;
                if (!r.ReadByte(t))
                    return false;
                if (t == 0)
                    return true;
                std::string k;
                if (!NbtString(r, k) || !SkipPayload(r, t, depth + 1))
                    return false;
            }
        }

        bool SkipPayload(wire::Reader& r, uint8_t type, int depth) {
            if (depth > 32)
                return false;
            switch (type) {
            case 1: {
                uint8_t v;
                return r.ReadByte(v);
            }
            case 2: {
                uint16_t v;
                return r.ReadLE16(v);
            }
            case 3: {
                int32_t v;
                return r.ReadZigzag32(v);
            }
            case 4: {
                int64_t v;
                return r.ReadZigzag64(v);
            }
            case 5: {
                float v;
                return r.ReadLEFloat(v);
            }
            case 6: {
                for (int i = 0; i < 8; i++) {
                    uint8_t b;
                    if (!r.ReadByte(b))
                        return false;
                }
                return true;
            }
            case 7: {
                int32_t n;
                if (!r.ReadZigzag32(n) || n < 0)
                    return false;
                for (int32_t i = 0; i < n; i++) {
                    uint8_t b;
                    if (!r.ReadByte(b))
                        return false;
                }
                return true;
            }
            case 8: {
                std::string s;
                return NbtString(r, s);
            }
            case 9: {
                uint8_t e;
                int32_t n;
                if (!r.ReadByte(e) || !r.ReadZigzag32(n) || n < 0 || (n > 0 && e == 0))
                    return false;
                for (int32_t i = 0; i < n; i++)
                    if (!SkipPayload(r, e, depth + 1))
                        return false;
                return true;
            }
            case 10:
                return SkipCompound(r, depth);
            case 11: {
                int32_t n;
                if (!r.ReadZigzag32(n) || n < 0)
                    return false;
                for (int32_t i = 0; i < n; i++) {
                    int32_t v;
                    if (!r.ReadZigzag32(v))
                        return false;
                }
                return true;
            }
            case 12: {
                int32_t n;
                if (!r.ReadZigzag32(n) || n < 0)
                    return false;
                for (int32_t i = 0; i < n; i++) {
                    int64_t v;
                    if (!r.ReadZigzag64(v))
                        return false;
                }
                return true;
            }
            default:
                return false;
            }
        }

        bool ReadStates(wire::Reader& r, PersistentBlockState& out) {
            for (;;) {
                uint8_t t = 0;
                if (!r.ReadByte(t))
                    return false;
                if (t == 0)
                    return true;
                PersistentBlockState::Entry e;
                if (!NbtString(r, e.key))
                    return false;
                if (t == 1) {
                    uint8_t v;
                    if (!r.ReadByte(v))
                        return false;
                    e.kind = 0;
                    e.number = v;
                } else if (t == 3) {
                    int32_t v;
                    if (!r.ReadZigzag32(v))
                        return false;
                    e.kind = 1;
                    e.number = v;
                } else if (t == 8) {
                    e.kind = 2;
                    if (!NbtString(r, e.text))
                        return false;
                } else if (!SkipPayload(r, t, 1)) {
                    return false;
                } else {
                    continue;
                }
                out.states.push_back(std::move(e));
            }
        }

        bool ReadPersistentEntry(wire::Reader& r, PersistentBlockState& out) {
            uint8_t root = 0;
            if (!r.ReadByte(root) || root != 10)
                return false;
            std::string rootName;
            if (!NbtString(r, rootName))
                return false;
            for (;;) {
                uint8_t t = 0;
                if (!r.ReadByte(t))
                    return false;
                if (t == 0)
                    return true;
                std::string k;
                if (!NbtString(r, k))
                    return false;
                if (k == "name" && t == 8) {
                    if (!NbtString(r, out.name))
                        return false;
                } else if (k == "states" && t == 10) {
                    if (!ReadStates(r, out))
                        return false;
                } else if (!SkipPayload(r, t, 1)) {
                    return false;
                }
            }
        }

        bool ParseStorage(wire::Reader& r, PaletteStorage& st, const char** err) {
            auto fail = [&](const char* m) {
                if (err)
                    *err = m;
                return false;
            };
            uint8_t hdr = 0;
            if (!r.ReadByte(hdr))
                return fail("storage header truncated");
            const bool runtimeFlag = (hdr & 1) != 0;
            unsigned bpb = hdr >> 1;
            if (!ValidBits(bpb))
                return fail("invalid bits-per-block");
            st.bitsPerBlock = (int)bpb;
            size_t nwords = WordCount(bpb);
            st.words.resize(nwords);
            for (size_t i = 0; i < nwords; i++)
                if (!r.ReadLE32(st.words[i]))
                    return fail("storage words truncated");
            const auto nbtAhead = [&]() {
                if (runtimeFlag)
                    return false;
                wire::Reader peek = r;
                int32_t n = 0;
                uint8_t first = 0;
                if (bpb != 0 && !peek.ReadZigzag32(n))
                    return false;
                return peek.ReadByte(first) && first == 10;
            };
            const bool persistent = nbtAhead();
            st.persistent = persistent;
            const auto readOne = [&](int32_t& value) {
                if (!persistent)
                    return r.ReadZigzag32(value);
                PersistentBlockState block;
                if (!ReadPersistentEntry(r, block) || !gResolver)
                    return false;
                value = gResolver(block);
                return true;
            };
            if (bpb == 0) {
                int32_t v = 0;
                if (!readOne(v))
                    return fail("uniform palette entry truncated");
                st.palette.assign(1, v);
                return true;
            }
            int32_t count = 0;
            if (!r.ReadZigzag32(count) || count <= 0 || count > 4096)
                return fail("bad palette size");
            st.palette.resize((size_t)count);
            for (int32_t i = 0; i < count; i++)
                if (!readOne(st.palette[(size_t)i]))
                    return fail("palette truncated");
            return true;
        }

    }

    void SetPersistentResolver(PersistentResolver resolver) {
        gResolver = resolver;
    }

    namespace {
        DimensionInfo gDimensions[3] = {{0, -64, 24}, {1, 0, 8}, {2, 0, 16}};
    }

    void ChunkCodec::SetDimension(int id, int minY, int heightRange) {
        if (id < 0 || id > 2 || heightRange <= 0)
            return;
        gDimensions[id].minY = minY;
        gDimensions[id].slots = (heightRange + 15) / 16;
    }

    const DimensionInfo& ChunkCodec::Dimension(int id) {
        return gDimensions[id == 1 ? 1 : id == 2 ? 2 : 0];
    }

    bool ChunkCodec::ParseSubChunk(const uint8_t* data, size_t size, SubChunkData& out, size_t& consumed,
                                   const char** err) {
        auto fail = [&](const char* m) {
            if (err)
                *err = m;
            return false;
        };
        wire::Reader r(data, size);
        uint8_t version = 0;
        if (!r.ReadByte(version))
            return fail("sub-chunk empty");
        out = SubChunkData{};
        out.version = version;
        if (version == 1) {
            PaletteStorage st;
            if (!ParseStorage(r, st, err))
                return false;
            out.storages.push_back(std::move(st));
        } else if (version == 8 || version == 9) {
            uint8_t n = 0;
            if (!r.ReadByte(n) || n > 8)
                return fail("bad storage count");
            if (version == 9) {
                int8_t y = 0;
                if (!r.ReadInt8(y))
                    return fail("y index truncated");
                out.hasYIndex = true;
                out.yIndex = y;
            }
            for (uint8_t i = 0; i < n; i++) {
                PaletteStorage st;
                if (!ParseStorage(r, st, err))
                    return false;
                out.storages.push_back(std::move(st));
            }
        } else {
            return fail("unknown sub-chunk version");
        }
        consumed = size - r.Remaining();
        return true;
    }

    bool ChunkCodec::ExpandIndices(const PaletteStorage& st, CellIndices& out) {
        if (st.palette.empty())
            return false;
        if (st.bitsPerBlock == 0) {
            out.fill(0);
            return true;
        }
        unsigned bpb = (unsigned)st.bitsPerBlock;
        unsigned perWord = 32 / bpb;
        uint32_t mask = (1u << bpb) - 1;
        if (st.words.size() < WordCount(bpb))
            return false;
        for (unsigned pos = 0; pos < (unsigned)kCells; pos++) {
            uint32_t w = st.words[pos / perWord];
            uint32_t idx = (w >> ((pos % perWord) * bpb)) & mask;
            if (idx >= st.palette.size())
                return false;
            out[pos] = (uint16_t)idx;
        }
        return true;
    }

    bool ChunkCodec::Expand(const PaletteStorage& st, CellIds& out) {
        CellIndices idx;
        if (!ExpandIndices(st, idx))
            return false;
        for (int i = 0; i < kCells; i++)
            out[(size_t)i] = st.palette[idx[(size_t)i]];
        return true;
    }

    void ChunkCodec::WriteStorage(std::vector<uint8_t>& out, const CellIds& cells) {
        std::vector<int32_t> palette;
        std::map<int32_t, uint32_t> lookup;
        std::vector<uint32_t> idx(kCells);
        for (int i = 0; i < kCells; i++) {
            auto it = lookup.find(cells[(size_t)i]);
            if (it == lookup.end()) {
                it = lookup.emplace(cells[(size_t)i], (uint32_t)palette.size()).first;
                palette.push_back(cells[(size_t)i]);
            }
            idx[(size_t)i] = it->second;
        }
        unsigned need = 0;
        while ((1u << need) < palette.size())
            need++;
        unsigned bpb = need == 0 ? 0
                                 : (need <= 1   ? 1
                                    : need <= 2 ? 2
                                    : need <= 3 ? 3
                                    : need <= 4 ? 4
                                    : need <= 5 ? 5
                                    : need <= 6 ? 6
                                    : need <= 8 ? 8
                                                : 16);
        out.push_back((uint8_t)((bpb << 1) | 1));
        if (bpb == 0) {
            wire::WriteZigzag32(out, palette[0]);
            return;
        }
        unsigned perWord = 32 / bpb;
        std::vector<uint32_t> words(WordCount(bpb), 0);
        for (unsigned pos = 0; pos < (unsigned)kCells; pos++)
            words[pos / perWord] |= idx[pos] << ((pos % perWord) * bpb);
        for (uint32_t w : words)
            wire::WriteLE32(out, w);
        wire::WriteZigzag32(out, (int32_t)palette.size());
        for (int32_t v : palette)
            wire::WriteZigzag32(out, v);
    }

    void ChunkCodec::WriteSubChunk(std::vector<uint8_t>& out, int yIndex, const CellIds& cells) {
        out.push_back(9);
        out.push_back(1);
        out.push_back((uint8_t)(int8_t)yIndex);
        WriteStorage(out, cells);
    }

    bool ChunkCodec::ParseColumn(const uint8_t* data, size_t size, uint32_t count, int firstSubY, int slots,
                                 std::vector<std::pair<int, SubChunkData>>& out, size_t& consumed,
                                 const char** err) {
        out.clear();
        consumed = 0;
        // some servers send fewer in the count than the payload holds: read the payload, not the count
        if (slots < 0 || (count != 0 && (!data || size == 0))) {
            if (err)
                *err = "inline subchunk count exceeds dimension height";
            return false;
        }
        std::vector<std::pair<int, SubChunkData>> parsed;
        size_t offset = 0;
        int nextY = firstSubY;
        // some proxies declare a raised minY yet still ship the full vanilla span: accept indexes
        // across the union of both so terrain below the declared floor still applies
        const int loY = std::min(firstSubY, -4);
        const int hiY = std::max(firstSubY + slots, 20);
        uint32_t read = 0;
        while (offset < size && static_cast<int>(parsed.size()) < (hiY - loY)) {
            // trailing block-entity nbt starts with a compound tag, never a sub-chunk version
            const uint8_t version = data[offset];
            if (version != 1 && version != 8 && version != 9)
                break;
            // past the count come the biome palettes, whose header (bpb << 1 | 1) reads as version 1 or 9:
            // only a well-formed v8/v9 sub-chunk continuing the heights counts, anything else ends the column
            const bool counted = read < count;
            if (!counted && version == 1)
                break;
            SubChunkData sc;
            size_t used = 0;
            if (!ParseSubChunk(data + offset, size - offset, sc, used, err)) {
                if (counted)
                    return false;
                break;
            }
            if (!counted && (sc.storages.empty() || (sc.hasYIndex && sc.yIndex != nextY)))
                break;
            ++read;
            int64_t y;
            if (sc.hasYIndex) {
                y = sc.yIndex;
                nextY = static_cast<int>(y) + 1;
            } else {
                y = nextY++;
            }
            offset += used;
            // some proxies repeat or overshoot heights: skip the entry, keep the column
            if (y > std::numeric_limits<int>::max() || y < loY || y >= static_cast<int64_t>(hiY))
                continue;
            if (std::any_of(parsed.begin(), parsed.end(),
                            [y](const auto& entry) { return entry.first == y; }))
                continue;
            parsed.emplace_back(static_cast<int>(y), std::move(sc));
        }
        if (parsed.empty() && count > 0) {
            if (err)
                *err = "inline subchunk count exceeds dimension height";
            return false;
        }
        out = std::move(parsed);
        consumed = offset;
        return true;
    }

}
