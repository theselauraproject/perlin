#include "world/level/lighting/LightStore.hpp"

#include "world/level/lighting/LightRegistry.hpp"

#include <algorithm>
#include <cstring>

namespace perlin {
    namespace {
        constexpr int kMinY = Chunk::MIN_Y;
        constexpr int kHeight = Chunk::HEIGHT;
        constexpr int kTopY = kMinY + kHeight - 1;
        constexpr int kSpan = 48;  // the chunk and one neighbour on each side
        constexpr int kColumns = kSpan * kSpan;
        constexpr int kSectionsPer = Chunk::SECTION_COUNT;

        constexpr uint16_t kResolved = 0x8000, kShaped = 0x1000;

        // A cell of the 3x3 neighbourhood: x and z from -16 to 31 relative to the centre chunk.
        inline uint32_t Node(int lx, int y, int lz) {
            return static_cast<uint32_t>(((y - kMinY) * kSpan + (lz + 16)) * kSpan + (lx + 16));
        }
        inline void Unpack(uint32_t node, int& lx, int& y, int& lz) {
            lx = static_cast<int>(node % kSpan) - 16;
            lz = static_cast<int>((node / kSpan) % kSpan) - 16;
            y = static_cast<int>(node / (kSpan * kSpan)) + kMinY;
        }
        inline int ChunkSlot(int lx, int lz) {
            return ((lz >> 4) + 1) * 3 + ((lx >> 4) + 1);
        }
        inline int CellOf(int lx, int y, int lz) {
            return (((y & 15) << 4) + (lz & 15)) * 16 + (lx & 15);
        }
        inline int SectionOf(int y) {
            return (y - kMinY) >> 4;
        }
        inline int ColumnOf(int lx, int lz) {
            return (lz + 16) * kSpan + (lx + 16);
        }
        inline bool InSpan(int lx, int y, int lz) {
            return lx >= -16 && lx < 32 && lz >= -16 && lz < 32 && y >= kMinY && y <= kTopY;
        }
        inline bool InCentre(int lx, int lz) {
            return lx >= 0 && lx < 16 && lz >= 0 && lz < 16;
        }

        // The six steps with ChunkLighting's face codes (0 down, 1 up, 2 -z, 3 +z, 4 -x, 5 +x).
        struct Step {
            int dx, dy, dz, dir;
        };
        constexpr Step kSteps[6] = {{-1, 0, 0, 4}, {1, 0, 0, 5}, {0, 0, -1, 2},
                                    {0, 0, 1, 3},  {0, -1, 0, 0}, {0, 1, 0, 1}};

        // Light kept as one byte per cell: sky in the high nibble, block light in the low one.
        inline int Channel(uint8_t packed, int channel) {
            return channel == 0 ? packed >> 4 : packed & 15;
        }
        inline uint8_t WithChannel(uint8_t packed, int channel, int value) {
            return channel == 0 ? static_cast<uint8_t>((packed & 0x0F) | (value << 4))
                                : static_cast<uint8_t>((packed & 0xF0) | value);
        }
    }

    void LightStore::Column::Set(int section, int cell, uint8_t value) {
        auto& slot = cells[static_cast<size_t>(section)];
        if (!slot) {
            if (value == uniform[static_cast<size_t>(section)])
                return;
            slot = std::make_unique<uint8_t[]>(4096);
            std::memset(slot.get(), uniform[static_cast<size_t>(section)], 4096);
        }
        slot[static_cast<size_t>(cell)] = value;
    }

    void LightStore::Column::Compact() {
        for (int s = 0; s < kSections; s++) {
            auto& slot = cells[static_cast<size_t>(s)];
            if (!slot)
                continue;
            const uint8_t first = slot[0];
            bool same = true;
            for (int i = 1; i < 4096 && same; i++)
                same = slot[static_cast<size_t>(i)] == first;
            if (same) {
                uniform[static_cast<size_t>(s)] = first;
                slot.reset();
            }
        }
    }

    // Scratch for one update: the 3x3 chunks around the one being lit, their block ids and opacities by
    // section (read on first use), and the sky that falls straight down each column (worked out on first use).
    struct LightStore::Work {
        const Chunk* chunk[9] = {};
        Column* col[9] = {};
        bool present[9] = {};

        struct Section {
            uint16_t ids[4096];
            uint8_t opacity[4096];
            bool clear = false;  // nothing in it stops or bends light
            bool emits = false;
        };
        std::vector<Section> sections = std::vector<Section>(9 * kSectionsPer);
        std::array<uint8_t, 9 * kSectionsPer> built{};

        std::vector<uint8_t> columnSky = std::vector<uint8_t>(static_cast<size_t>(kColumns) * kHeight);
        std::array<int16_t, kColumns> columnTop{};  // highest row where the falling sky is below 15
        std::array<int16_t, kColumns> columnLit{};  // lowest row where it is above 1
        std::array<uint8_t, kColumns> columnKnown{};

        std::vector<uint32_t> increase, decrease;
    };

    LightStore::LightStore(const LightRegistry& registry)
        : props(65536, 0), registry(registry), work(std::make_unique<Work>()) {}
    LightStore::~LightStore() = default;

    LightStore& LightStore::For(Level& level, const LightRegistry& registry) {
        if (!level.lightCache)
            level.lightCache = std::make_shared<LightStore>(registry);
        return *static_cast<LightStore*>(level.lightCache.get());
    }

    size_t LightStore::Bytes() const {
        size_t bytes = columns.size() * sizeof(Column);
        for (const auto& [pos, c] : columns)
            for (const auto& s : c.cells)
                if (s)
                    bytes += 4096;
        return bytes;
    }

    void LightStore::Prepare(const Level& level, const ChunkPos& pos, bool selfPresent, const Chunk* self) {
        Work& w = *work;
        for (int dz = 0; dz < 3; dz++)
            for (int dx = 0; dx < 3; dx++) {
                const int i = dz * 3 + dx;
                const ChunkPos p{pos.x + dx - 1, pos.z + dz - 1};
                w.chunk[i] = nullptr;
                w.col[i] = nullptr;
                w.present[i] = false;
                if (i == 4) {
                    if (selfPresent) {
                        w.chunk[i] = self;
                        w.col[i] = &columns[p];
                        w.present[i] = true;
                    }
                    continue;
                }
                // A neighbour takes part once it is lit itself. Until then it is as if it were not there (as a
                // missing chunk is in the window), and it pulls this chunk's light in when it is lit.
                const auto lit = columns.find(p);
                const auto it = level.chunks.find(p);
                if (lit == columns.end() || it == level.chunks.end() || lit->second.version != it->second.version)
                    continue;
                w.chunk[i] = &it->second;
                w.col[i] = &lit->second;
                w.present[i] = true;
            }
        w.built.fill(0);
        w.columnKnown.fill(0);
        w.increase.clear();
        w.decrease.clear();
    }

    namespace {
        // Everything the propagation reads and writes, over the prepared scratch.
        template <class Work, class Column>
        class Access {
        public:
            Access(Work& w, std::vector<uint16_t>& props, const LightRegistry& registry,
                   uint16_t (*resolve)(std::vector<uint16_t>&, uint16_t))
                : w(w), props(props), registry(registry), resolve(resolve) {}

            typename Work::Section& Section(int slot, int s) {
                const size_t si = static_cast<size_t>(slot) * kSectionsPer + static_cast<size_t>(s);
                typename Work::Section& sec = w.sections[si];
                if (!w.built[si])
                    Build(sec, slot, s, si);
                return sec;
            }

            void Build(typename Work::Section& sec, int slot, int s, size_t si) {
                const Chunk* c = w.chunk[slot];
                const int y0 = kMinY + s * 16;
                uint16_t same = 0;
                if (c->SectionUniform(s, same)) {
                    // One block all through (most often air): no per-cell work.
                    const uint16_t p = resolve(props, same);
                    std::fill_n(sec.ids, 4096, same);
                    std::memset(sec.opacity, static_cast<int>(p & 0xFF), sizeof sec.opacity);
                    sec.clear = (p & (0xFF | kShaped)) == 0;
                    sec.emits = ((p >> 8) & 15) != 0;
                    w.built[si] = 1;
                    return;
                }
                for (int layer = 0; layer < 16; layer++)
                    c->CopyRows(y0 + layer, 0, 16, 0, 16, sec.ids + layer * 256, 16);
                uint16_t lastId = sec.ids[0], lastProps = resolve(props, lastId), all = lastProps;
                for (int i = 0; i < 4096; i++) {
                    const uint16_t id = sec.ids[i];
                    if (id != lastId) {
                        lastId = id;
                        lastProps = resolve(props, id);
                        all |= lastProps;
                    }
                    sec.opacity[i] = static_cast<uint8_t>(lastProps);
                }
                sec.clear = (all & (0xFF | kShaped)) == 0;
                sec.emits = ((all >> 8) & 15) != 0;
                w.built[si] = 1;
            }

            int Opacity(int lx, int y, int lz) {
                const int slot = ChunkSlot(lx, lz);
                if (!w.present[slot])
                    return kMaxLight;
                return Section(slot, SectionOf(y)).opacity[CellOf(lx, y, lz)];
            }

            uint16_t Id(int lx, int y, int lz) {
                return Section(ChunkSlot(lx, lz), SectionOf(y)).ids[CellOf(lx, y, lz)];
            }

            bool Shaped(int lx, int y, int lz) {
                return (resolve(props, Id(lx, y, lz)) & kShaped) != 0;
            }

            int Emission(int lx, int y, int lz) {
                return (resolve(props, Id(lx, y, lz)) >> 8) & 15;
            }

            // As ChunkLighting's Occluded: light passing from one cell into the next through a face both shapes cover.
            bool Occluded(int fx, int fy, int fz, int tx, int ty, int tz, int dir) {
                const bool a = Shaped(fx, fy, fz), b = Shaped(tx, ty, tz);
                if (!a && !b)
                    return false;
                const FaceOcclusion* fa = a ? registry.Occlusion(Id(fx, fy, fz)) : nullptr;
                const FaceOcclusion* fb = b ? registry.Occlusion(Id(tx, ty, tz)) : nullptr;
                return FaceOcclusion::Occludes(fa, dir, fb);
            }

            uint8_t Get(int lx, int y, int lz) {
                return w.col[ChunkSlot(lx, lz)]->Get(SectionOf(y), CellOf(lx, y, lz));
            }
            void Set(int lx, int y, int lz, uint8_t value) {
                w.col[ChunkSlot(lx, lz)]->Set(SectionOf(y), CellOf(lx, y, lz), value);
            }

            // The sky falling straight down a column, as ChunkLighting's first pass: 15 from the top, less each
            // cell's opacity, gone at an opaque cell or a shape that closes the face below.
            void EnsureColumn(int lx, int lz) {
                const int c = ColumnOf(lx, lz);
                if (w.columnKnown[c])
                    return;
                w.columnKnown[c] = 1;
                uint8_t* sky = &w.columnSky[static_cast<size_t>(c) * kHeight];
                std::memset(sky, 0, kHeight);
                int top = kMinY - 1, lit = kTopY + 1;
                int level = kMaxLight;
                const int slot = ChunkSlot(lx, lz);
                const int cellXZ = (lz & 15) * 16 + (lx & 15);
                for (int s = kSectionsPer - 1; s >= 0 && level > 0; s--) {
                    typename Work::Section& sec = Section(slot, s);
                    const int y0 = kMinY + s * 16;
                    if (sec.clear && level == kMaxLight) {
                        // A section that changes nothing: the sky passes through at full strength.
                        std::memset(sky + (y0 - kMinY), kMaxLight, 16);
                        lit = y0;
                        continue;
                    }
                    for (int yy = 15; yy >= 0; yy--) {
                        const int y = y0 + yy;
                        const int cost = sec.opacity[yy * 256 + cellXZ];
                        if (cost >= kMaxLight)
                            level = 0;
                        else if (y < kTopY && Occluded(lx, y + 1, lz, lx, y, lz, 0))
                            level = 0;
                        else
                            level = std::max(0, level - cost);
                        if (level < kMaxLight && top < y)
                            top = y;
                        if (level == 0)
                            break;
                        sky[y - kMinY] = static_cast<uint8_t>(level);
                        if (level > 1)
                            lit = y;
                    }
                }
                w.columnTop[c] = static_cast<int16_t>(top);
                w.columnLit[c] = static_cast<int16_t>(lit);
            }

            int StoredSkyTop(int lx, int lz) {
                const Column* col = w.col[ChunkSlot(lx, lz)];
                for (int s = kSectionsPer - 1; s >= 0; s--) {
                    const auto& cells = col->cells[static_cast<size_t>(s)];
                    if (!cells) {
                        if ((col->uniform[static_cast<size_t>(s)] >> 4) < kMaxLight)
                            return kMinY + s * 16 + 15;
                        continue;
                    }
                    const int xz = (lz & 15) * 16 + (lx & 15);
                    for (int yy = 15; yy >= 0; yy--)
                        if ((cells[static_cast<size_t>(yy * 256 + xz)] >> 4) < kMaxLight)
                            return kMinY + s * 16 + yy;
                }
                return kMinY - 1;
            }

            int ColumnSky(int lx, int y, int lz) {
                EnsureColumn(lx, lz);
                return w.columnSky[static_cast<size_t>(ColumnOf(lx, lz)) * kHeight + static_cast<size_t>(y - kMinY)];
            }

            int Source(int lx, int y, int lz, int channel) {
                return channel == 0 ? ColumnSky(lx, y, lz) : Emission(lx, y, lz);
            }

            Work& w;
            std::vector<uint16_t>& props;
            const LightRegistry& registry;
            uint16_t (*resolve)(std::vector<uint16_t>&, uint16_t);
        };
    }

    void LightStore::SyncAll(const Level& level) {
        std::vector<ChunkPos> gone;
        for (const auto& [pos, c] : columns)
            if (!level.chunks.contains(pos))
                gone.push_back(pos);
        for (const ChunkPos& pos : gone)
            Update(level, pos, nullptr);
        for (const auto& [pos, chunk] : level.chunks)
            Sync(level, pos);
    }

    void LightStore::Sync(const Level& level, const ChunkPos& pos) {
        const auto it = level.chunks.find(pos);
        const auto lit = columns.find(pos);
        if (it == level.chunks.end()) {
            if (lit != columns.end())
                Update(level, pos, nullptr);
            return;
        }
        if (lit != columns.end() && lit->second.version == it->second.version)
            return;
        Update(level, pos, &it->second);
    }

    void LightStore::CheckGeneration() {
        const uint64_t generation = registry.Generation();
        if (generation != registryGeneration) {
            // Opacity, emission or shapes changed (rules loaded, a pack's blocks): every chunk is lit again.
            columns.clear();
            std::fill(props.begin(), props.end(), uint16_t(0));
            registryGeneration = generation;
        }
    }

    void LightStore::Update(const Level& level, const ChunkPos& pos, const Chunk* now) {
        Column old;
        if (const auto lit = columns.find(pos); lit != columns.end()) {
            old = std::move(lit->second);
            columns.erase(lit);
        }
        Prepare(level, pos, now != nullptr, now);
        Work& w = *work;
        // Opacity and emission are asked of the registry afresh for each update, as ChunkLighting does
        // for each window (its answers can change while names and packs load).
        for (const uint16_t id : seenIds)
            props[id] = 0;
        seenIds.clear();
        static thread_local std::vector<uint16_t>* seen = nullptr;
        static thread_local const LightRegistry* lights = nullptr;
        seen = &seenIds;
        lights = &registry;
        Access<Work, Column> a(w, props, registry, [](std::vector<uint16_t>& table, uint16_t id) -> uint16_t {
            uint16_t p = table[id];
            if (p & kResolved)
                return p;
            seen->push_back(id);
            p = kResolved;
            if (id != 0) {
                const BlockLightProps lp = lights->Resolve(id);
                p |= static_cast<uint16_t>(lp.opacity | (lp.emission << 8));
                if (lp.opacity < kMaxLight && lights->Occlusion(id))
                    p |= kShaped;
            }
            table[id] = p;
            return p;
        });
        if (now)
            stats.relit++;
        else
            stats.removed++;

        for (int channel = 0; channel < 2; channel++) {
            std::vector<uint32_t>& inc = w.increase;
            std::vector<uint32_t>& dec = w.decrease;
            inc.clear();
            dec.clear();

            // 1. What this chunk lit before leaves its neighbours: each border cell's old light is taken out
            //    of the cells next to it, and of everything those lit in turn.
            for (int y = kMinY; y <= kTopY; y++) {
                const int s = SectionOf(y);
                if (!old.cells[static_cast<size_t>(s)] && Channel(old.uniform[static_cast<size_t>(s)], channel) == 0) {
                    y = kMinY + s * 16 + 15;
                    continue;
                }
                for (int i = 0; i < 16; i++) {
                    const int edge[4][2] = {{0, i}, {15, i}, {i, 0}, {i, 15}};
                    for (int e = 0; e < 4; e++) {
                        const int v = Channel(old.Get(s, CellOf(edge[e][0], y, edge[e][1])), channel);
                        if (v > 0)
                            dec.push_back(Node(edge[e][0], y, edge[e][1]) | (static_cast<uint32_t>(v) << 20));
                    }
                }
            }
            for (size_t head = 0; head < dec.size(); head++) {
                const uint32_t entry = dec[head];
                const int from = static_cast<int>(entry >> 20);
                int lx, y, lz;
                Unpack(entry & 0xFFFFF, lx, y, lz);
                stats.decreaseSteps++;
                for (const Step& st : kSteps) {
                    const int nx = lx + st.dx, ny = y + st.dy, nz = lz + st.dz;
                    if (!InSpan(nx, ny, nz) || InCentre(nx, nz) || !w.present[ChunkSlot(nx, nz)])
                        continue;
                    const uint8_t packed = a.Get(nx, ny, nz);
                    const int level = Channel(packed, channel);
                    if (level == 0)
                        continue;
                    if (level < from) {
                        // It may have been lit from here: back to its own source, and on outwards.
                        const int source = a.Source(nx, ny, nz, channel);
                        if (source < level) {
                            a.Set(nx, ny, nz, WithChannel(packed, channel, source));
                            dec.push_back(Node(nx, ny, nz) | (static_cast<uint32_t>(level) << 20));
                        }
                        if (source > 1)
                            inc.push_back(Node(nx, ny, nz));
                    } else if (!InCentre(lx, lz)) {
                        // Lit from elsewhere: it lights the darkened cells again. (Next to the chunk itself
                        // this is not needed: the chunk is lit afresh and takes in its border below.)
                        inc.push_back(Node(nx, ny, nz));
                    }
                }
            }

            // 2. The chunk's own light, from its sources, and what flows in from the neighbours' borders.
            if (now) {
                Column& self = *w.col[4];
                if (channel == 0) {
                    // Sky: the falling column, stored section by section (uniform where it can be).
                    for (int s = 0; s < kSections; s++) {
                        self.uniform[static_cast<size_t>(s)] = 0;
                        self.cells[static_cast<size_t>(s)].reset();
                    }
                    for (int lz = 0; lz < 16; lz++)
                        for (int lx = 0; lx < 16; lx++)
                            a.EnsureColumn(lx, lz);
                    for (int s = 0; s < kSections; s++) {
                        const int y0 = kMinY + s * 16;
                        bool allFull = true, allDark = true;
                        for (int c = 0; c < 256 && (allFull || allDark); c++) {
                            const int lx = c & 15, lz = c >> 4;
                            const uint8_t* sky = &w.columnSky[static_cast<size_t>(ColumnOf(lx, lz)) * kHeight];
                            for (int yy = 0; yy < 16; yy++) {
                                const uint8_t v = sky[y0 - kMinY + yy];
                                allFull = allFull && v == kMaxLight;
                                allDark = allDark && v == 0;
                            }
                        }
                        if (allFull || allDark) {
                            self.uniform[static_cast<size_t>(s)] = allFull ? static_cast<uint8_t>(kMaxLight << 4) : 0;
                            continue;
                        }
                        for (int yy = 0; yy < 16; yy++)
                            for (int c = 0; c < 256; c++) {
                                const int lx = c & 15, lz = c >> 4;
                                const uint8_t v =
                                    w.columnSky[static_cast<size_t>(ColumnOf(lx, lz)) * kHeight + static_cast<size_t>(y0 - kMinY + yy)];
                                if (v)
                                    self.Set(s, CellOf(lx, y0 + yy, lz), static_cast<uint8_t>(v << 4));
                            }
                    }
                    // Only cells next to a darker one can light anything: as ChunkLighting, look from each
                    // column's lowest lit row up to just above the highest attenuation around it.
                    for (int lz = 0; lz < 16; lz++)
                        for (int lx = 0; lx < 16; lx++) {
                            const int c = ColumnOf(lx, lz);
                            int top = w.columnTop[c];
                            for (const Step& st : kSteps) {
                                if (st.dy)
                                    continue;
                                const int nx = lx + st.dx, nz = lz + st.dz;
                                const int slot = ChunkSlot(nx, nz);
                                if (!w.present[slot])
                                    continue;
                                if (slot == 4) {
                                    top = std::max<int>(top, w.columnTop[ColumnOf(nx, nz)]);
                                    continue;
                                }
                                // A neighbour chunk's column: the highest cell it holds below full sky, from
                                // the light it already has (no need to read its blocks).
                                top = std::max(top, a.StoredSkyTop(nx, nz));
                            }
                            const int scanTop = std::min(kTopY, top + 1);
                            for (int y = w.columnLit[c]; y <= scanTop; y++) {
                                const int level = w.columnSky[static_cast<size_t>(c) * kHeight + static_cast<size_t>(y - kMinY)];
                                if (level <= 1)
                                    continue;
                                inc.push_back(Node(lx, y, lz));
                            }
                        }
                } else {
                    for (int s = 0; s < kSections; s++) {
                        typename Work::Section& sec = a.Section(4, s);
                        if (!sec.emits)
                            continue;
                        const int y0 = kMinY + s * 16;
                        for (int i = 0; i < 4096; i++) {
                            const int e = (props[sec.ids[i]] >> 8) & 15;
                            if (!e)
                                continue;
                            const int lx = i & 15, lz = (i >> 4) & 15, y = y0 + (i >> 8);
                            self.Set(s, i, WithChannel(self.Get(s, i), 1, e));
                            inc.push_back(Node(lx, y, lz));
                        }
                    }
                }
                // Light already in the neighbours flows in across the border.
                for (int y = kMinY; y <= kTopY; y++)
                    for (int i = 0; i < 16; i++) {
                        const int outside[4][2] = {{-1, i}, {16, i}, {i, -1}, {i, 16}};
                        const int inside[4][2] = {{0, i}, {15, i}, {i, 0}, {i, 15}};
                        for (int e = 0; e < 4; e++) {
                            const int ox = outside[e][0], oz = outside[e][1];
                            if (!w.present[ChunkSlot(ox, oz)])
                                continue;
                            const int level = Channel(a.Get(ox, y, oz), channel);
                            if (level <= 1)
                                continue;
                            const int ix = inside[e][0], iz = inside[e][1];
                            const int cost = a.Opacity(ix, y, iz);
                            if (cost >= kMaxLight)
                                continue;
                            if (level - std::max(1, cost) > Channel(a.Get(ix, y, iz), channel))
                                inc.push_back(Node(ox, y, oz));
                        }
                    }
            }

            // 3. Flood: every queued cell lights its neighbours, as ChunkLighting's Flood.
            for (size_t head = 0; head < inc.size(); head++) {
                int lx, y, lz;
                Unpack(inc[head], lx, y, lz);
                stats.increaseSteps++;
                const int level = Channel(a.Get(lx, y, lz), channel);
                if (level <= 1)
                    continue;
                for (const Step& st : kSteps) {
                    const int nx = lx + st.dx, ny = y + st.dy, nz = lz + st.dz;
                    if (!InSpan(nx, ny, nz) || !w.present[ChunkSlot(nx, nz)])
                        continue;
                    const int cost = a.Opacity(nx, ny, nz);
                    if (cost >= kMaxLight)
                        continue;
                    const int next = level - std::max(1, cost);
                    const uint8_t packed = a.Get(nx, ny, nz);
                    if (next <= Channel(packed, channel))
                        continue;
                    if (a.Occluded(lx, y, lz, nx, ny, nz, st.dir))
                        continue;
                    a.Set(nx, ny, nz, WithChannel(packed, channel, next));
                    inc.push_back(Node(nx, ny, nz));
                }
            }
        }

        if (now) {
            Column& self = *w.col[4];
            self.Compact();
            self.version = now->version;
        }
    }

    void LightStore::Extract(const Level& level, const ChunkPos& pos, int yLo, int yHi, ChunkLighting::Result& out) {
        CheckGeneration();
        // Chunks unloaded far from anything still meshed are never next to an extract again, so sweep them out
        // once the store holds noticeably more than the level does; otherwise a moving player leaves a trail.
        if (columns.size() > level.chunks.size() + 32) {
            std::vector<ChunkPos> gone;
            for (const auto& [p, c] : columns)
                if (!level.chunks.contains(p))
                    gone.push_back(p);
            for (const ChunkPos& p : gone)
                Update(level, p, nullptr);
        }
        // Gone chunks first (their light leaves the neighbours), then the centre and its neighbours. Light near
        // this chunk can only come from within 15 blocks, so these nine are all that matter.
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++) {
                const ChunkPos p{pos.x + dx, pos.z + dz};
                if (!level.chunks.contains(p) && columns.contains(p))
                    Update(level, p, nullptr);
            }
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++)
                Sync(level, {pos.x + dx, pos.z + dz});
        stats.extracts++;

        constexpr int kPadded = ChunkLighting::kPadded;
        out.yBase = yLo;
        out.rows = yHi - yLo + 1;
        out.packed.assign(static_cast<size_t>(std::max(0, out.rows)) * kPadded * kPadded, 0);
        const Column* around[3][3];
        for (int dz = 0; dz < 3; dz++)
            for (int dx = 0; dx < 3; dx++) {
                const auto c = columns.find({pos.x + dx - 1, pos.z + dz - 1});
                around[dz][dx] = c == columns.end() ? nullptr : &c->second;
            }
        for (int y = yLo; y <= yHi; y++) {
            uint8_t* row = out.packed.data() + static_cast<size_t>(y - yLo) * kPadded * kPadded;
            if (y > kTopY) {
                std::memset(row, ChunkLighting::Result::kSkyOnly, kPadded * kPadded);
                continue;
            }
            if (y < kMinY)
                continue;
            const int s = SectionOf(y);
            for (int z = -1; z <= 16; z++) {
                const Column* const* line = around[z < 0 ? 0 : (z > 15 ? 2 : 1)];
                uint8_t* dst = row + static_cast<size_t>(z + 1) * kPadded;
                if (const Column* c = line[0])
                    dst[0] = c->Get(s, CellOf(15, y, z));
                if (const Column* c = line[1]) {
                    if (c->cells[static_cast<size_t>(s)])
                        std::memcpy(dst + 1, c->cells[static_cast<size_t>(s)].get() + CellOf(0, y, z), 16);
                    else
                        std::memset(dst + 1, c->uniform[static_cast<size_t>(s)], 16);
                }
                if (const Column* c = line[2])
                    dst[17] = c->Get(s, CellOf(0, y, z));
            }
        }
    }
}
