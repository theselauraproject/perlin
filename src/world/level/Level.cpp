#include "world/level/Level.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>

namespace perlin {
    void Cells16::Reset() {
        narrow.reset();
        wide.reset();
        palette.clear();
        palette.shrink_to_fit();
    }

    void Cells16::Fill(uint16_t value) {
        wide.reset();
        narrow = std::make_unique<uint8_t[]>(4096);
        std::memset(narrow.get(), 0, 4096);
        palette.assign(1, value);
    }

    void Cells16::Widen() {
        auto out = std::make_unique<uint16_t[]>(4096);
        for (size_t i = 0; i < 4096; i++)
            out[i] = palette[narrow[i]];
        wide = std::move(out);
        narrow.reset();
        palette.clear();
        palette.shrink_to_fit();
    }

    void Cells16::Set(int cell, uint16_t value) {
        if (Empty())
            Fill(0);
        if (wide) {
            wide[static_cast<size_t>(cell)] = value;
            return;
        }
        const auto it = std::find(palette.begin(), palette.end(), value);
        if (it != palette.end()) {
            narrow[static_cast<size_t>(cell)] = static_cast<uint8_t>(it - palette.begin());
            return;
        }
        if (palette.size() < 256) {
            palette.push_back(value);
            narrow[static_cast<size_t>(cell)] = static_cast<uint8_t>(palette.size() - 1);
            return;
        }
        Widen();
        wide[static_cast<size_t>(cell)] = value;
    }

    void Cells16::Load(const uint16_t* cells) {
        // ids map to palette slots through a 64k table; only the touched entries are cleared afterwards
        static thread_local std::vector<int16_t> slotOf(65536, -1);
        std::vector<uint16_t> pal;
        pal.reserve(16);
        auto idx = std::make_unique<uint8_t[]>(4096);
        bool fits = true;
        for (size_t i = 0; i < 4096 && fits; i++) {
            int16_t& slot = slotOf[cells[i]];
            if (slot < 0) {
                if (pal.size() == 256) {
                    fits = false;
                    break;
                }
                slot = static_cast<int16_t>(pal.size());
                pal.push_back(cells[i]);
            }
            idx[i] = static_cast<uint8_t>(slot);
        }
        for (uint16_t v : pal)
            slotOf[v] = -1;
        if (fits) {
            wide.reset();
            narrow = std::move(idx);
            pal.shrink_to_fit();
            palette = std::move(pal);
            return;
        }
        narrow.reset();
        palette.clear();
        wide = std::make_unique<uint16_t[]>(4096);
        std::memcpy(wide.get(), cells, 4096 * sizeof(uint16_t));
    }

    void Cells16::CopyOut(int cell, int n, uint16_t* out) const {
        if (wide) {
            std::memcpy(out, wide.get() + cell, static_cast<size_t>(n) * sizeof(uint16_t));
            return;
        }
        const uint8_t* src = narrow.get() + cell;
        const uint16_t* pal = palette.data();
        for (int i = 0; i < n; i++)
            out[i] = pal[src[i]];
    }

    bool Cells16::AllZero(int cell, int n) const {
        for (int i = 0; i < n; i++)
            if (Get(cell + i) != 0)
                return false;
        return true;
    }

    size_t Cells16::Bytes() const {
        return (narrow ? 4096 + palette.capacity() * sizeof(uint16_t) : 0) + (wide ? 8192 : 0);
    }

    Cells16 Cells16::Clone() const {
        Cells16 c;
        if (narrow) {
            c.narrow = std::make_unique<uint8_t[]>(4096);
            std::memcpy(c.narrow.get(), narrow.get(), 4096);
            c.palette = palette;
        }
        if (wide) {
            c.wide = std::make_unique<uint16_t[]>(4096);
            std::memcpy(c.wide.get(), wide.get(), 4096 * sizeof(uint16_t));
        }
        return c;
    }

    Chunk::Section& Chunk::Ensure(int section) {
        auto& slot = sections[section];
        if (!slot)
            slot = std::make_unique<Section>();
        return *slot;
    }

    Chunk Chunk::Clone() const {
        Chunk c;
        c.version = version;
        c.borderVersion = borderVersion;
        c.streaming = streaming;
        c.readyAt = readyAt;
        c.firstSeen = firstSeen;
        c.queued = false;
        c.lodOnly = lodOnly;
        for (int i = 0; i < SECTION_COUNT; i++) {
            const auto& src = sections[static_cast<size_t>(i)];
            if (!src)
                continue;
            auto dst = std::make_unique<Section>();
            dst->uniform = src->uniform;
            dst->uniformBiome = src->uniformBiome;
            dst->blocks = src->blocks.Clone();
            dst->liquid = src->liquid.Clone();
            if (src->biomes)
                dst->biomes = std::make_unique<std::array<uint8_t, 4096>>(*src->biomes);
            c.sections[static_cast<size_t>(i)] = std::move(dst);
        }
        return c;
    }

    bool Chunk::SectionEquals(int section, const uint16_t* cells, const uint16_t* liquidCells) const {
        const auto& s = sections[static_cast<size_t>(section)];
        if (!s)
            return false;
        bool anyLiquid = false;
        if (liquidCells)
            for (int i = 0; i < 4096 && !anyLiquid; i++)
                anyLiquid = liquidCells[i] != 0;
        if (anyLiquid != !s->liquid.Empty())
            return false;
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = 0; y < 16; y++) {
                    const int src = x * 256 + z * 16 + y;
                    const int dst = (y * 16 + z) * 16 + x;
                    if (s->Block(dst) != cells[src])
                        return false;
                    if (anyLiquid && s->liquid.Get(dst) != liquidCells[src])
                        return false;
                }
        return true;
    }

    bool Chunk::BiomeSectionEquals(int section, const uint8_t* cells) const {
        const auto& s = sections[static_cast<size_t>(section)];
        if (!s)
            return false;
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = 0; y < 16; y++)
                    if (s->Biome((y * 16 + z) * 16 + x) != cells[x * 256 + z * 16 + y])
                        return false;
        return true;
    }

    size_t Chunk::ApproxBytes() const {
        size_t n = sizeof(Chunk);
        for (const auto& s : sections)
            if (s)
                n += sizeof(Section) + s->blocks.Bytes() + s->liquid.Bytes() + (s->biomes ? 4096 : 0);
        return n;
    }

    uint16_t Chunk::Get(int x, int y, int z) const {
        if (!InBounds(x, y, z))
            return 0;
        const auto& s = sections[(y - MIN_Y) >> 4];
        return s ? s->Block(Local(x, y, z)) : 0;
    }

    void Chunk::Set(int x, int y, int z, uint16_t id) {
        if (!InBounds(x, y, z))
            return;
        const int index = (y - MIN_Y) >> 4;
        if (!sections[index] && id == 0)
            return;
        Ensure(index).SetBlock(Local(x, y, z), id);
    }

    uint16_t Chunk::GetLiquid(int x, int y, int z) const {
        if (!InBounds(x, y, z))
            return 0;
        const auto& s = sections[(y - MIN_Y) >> 4];
        return s && !s->liquid.Empty() ? s->liquid.Get(Local(x, y, z)) : 0;
    }

    void Chunk::SetLiquid(int x, int y, int z, uint16_t id) {
        if (!InBounds(x, y, z))
            return;
        const int index = (y - MIN_Y) >> 4;
        if (!sections[index] && id == 0)
            return;
        Section& s = Ensure(index);
        if (s.liquid.Empty()) {
            if (id == 0)
                return;
            s.liquid.Fill(0);
        }
        s.liquid.Set(Local(x, y, z), id);
    }

    void Chunk::ReadLiquid(int x, int z, int yLo, int yHi, uint16_t* out, size_t stride) const {
        for (int y = yLo; y <= yHi; y++) {
            const auto& s = sections[(y - MIN_Y) >> 4];
            if (s && !s->liquid.Empty())
                out[static_cast<size_t>(y - yLo) * stride] = s->liquid.Get(Local(x, y, z));
        }
    }

    uint8_t Chunk::GetBiome(int x, int y, int z) const {
        if (!InBounds(x, y, z))
            return DEFAULT_BIOME;
        const auto& s = sections[(y - MIN_Y) >> 4];
        return s ? s->Biome(Local(x, y, z)) : DEFAULT_BIOME;
    }

    bool Chunk::YRange(int& lo, int& hi) const {
        bool any = false;
        for (int i = 0; i < SECTION_COUNT; i++) {
            const auto& s = sections[i];
            if (!s)
                continue;
            if (s->blocks.Empty() && s->uniform == 0)
                continue;
            for (int layer = 0; layer < 16; layer++) {
                if (!s->blocks.Empty() && s->blocks.AllZero(layer * 256, 256))
                    continue;
                const int y = MIN_Y + i * 16 + layer;
                if (!any)
                    lo = y;
                hi = y;
                any = true;
            }
        }
        return any;
    }

    void Chunk::ReadColumn(int x, int z, int yLo, int yHi, uint16_t* blocks, uint8_t* biomes,
                           size_t stride) const {
        for (int y = yLo; y <= yHi; y++) {
            const auto& s = sections[(y - MIN_Y) >> 4];
            if (!s)
                continue;
            const int local = Local(x, y, z);
            const size_t out = static_cast<size_t>(y - yLo) * stride;
            blocks[out] = s->Block(local);
            biomes[out] = s->Biome(local);
        }
    }

    void Chunk::CopyRun(int y, int z, int x0, int n, uint16_t* blocks, uint8_t* biomes,
                        uint16_t* liquid) const {
        if (y < MIN_Y || y >= MIN_Y + HEIGHT)
            return;
        const auto& s = sections[(y - MIN_Y) >> 4];
        if (!s)
            return;
        const int local = Local(x0, y, z);
        if (blocks) {
            if (!s->blocks.Empty())
                s->blocks.CopyOut(local, n, blocks);
            else
                std::fill_n(blocks, n, s->uniform);
        }
        if (biomes) {
            if (s->biomes)
                std::memcpy(biomes, s->biomes->data() + local, static_cast<size_t>(n));
            else
                std::memset(biomes, s->uniformBiome, static_cast<size_t>(n));
        }
        if (liquid && !s->liquid.Empty())
            s->liquid.CopyOut(local, n, liquid);
    }

    void Chunk::ReadBlocks(int x, int z, int yLo, int yHi, uint16_t* blocks, size_t stride) const {
        for (int y = yLo; y <= yHi; y++) {
            const auto& s = sections[(y - MIN_Y) >> 4];
            if (s)
                blocks[static_cast<size_t>(y - yLo) * stride] = s->Block(Local(x, y, z));
        }
    }

    void Chunk::SetSection(int section, const uint16_t* cells, const uint16_t* liquidCells) {
        Section& s = Ensure(section);
        const uint16_t first = cells[0];
        uint16_t local[4096];
        if (std::all_of(cells, cells + 4096, [first](uint16_t v) { return v == first; })) {
            s.blocks.Reset();
            s.uniform = first;
        } else {
            for (int x = 0; x < 16; x++)
                for (int z = 0; z < 16; z++)
                    for (int y = 0; y < 16; y++)
                        local[(y * 16 + z) * 16 + x] = cells[x * 256 + z * 16 + y];
            s.blocks.Load(local);
        }
        bool any = false;
        if (liquidCells)
            for (int i = 0; i < 4096 && !any; i++)
                any = liquidCells[i] != 0;
        if (!any) {
            s.liquid.Reset();
            return;
        }
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = 0; y < 16; y++)
                    local[(y * 16 + z) * 16 + x] = liquidCells[x * 256 + z * 16 + y];
        s.liquid.Load(local);
    }

    void Chunk::SetBiomeSection(int section, const uint8_t* cells) {
        Section& s = Ensure(section);
        const uint8_t first = cells[0];
        if (std::all_of(cells, cells + 4096, [first](uint8_t v) { return v == first; })) {
            s.biomes.reset();
            s.uniformBiome = first;
            return;
        }
        if (!s.biomes)
            s.biomes = std::make_unique<std::array<uint8_t, 4096>>();
        for (int x = 0; x < 16; x++)
            for (int z = 0; z < 16; z++)
                for (int y = 0; y < 16; y++)
                    (*s.biomes)[static_cast<size_t>((y * 16 + z) * 16 + x)] = cells[x * 256 + z * 16 + y];
    }

    void Level::Clear() {
        std::lock_guard lock(mutex);
        changeCounter++;
        clearCounter++;
        chunks.clear();
        lodOnlyCount = 0;
        dirty.clear();
        undo.clear();
        loggedEpoch.clear();
        undoBytes = 0;
        undoFloor = currentEpoch.load();
    }

    void Level::EnableUndo(size_t capBytes) {
        std::lock_guard lock(mutex);
        undoOn = capBytes > 0;
        undoCap = capBytes;
        undo.clear();
        loggedEpoch.clear();
        undoBytes = 0;
        undoFloor = currentEpoch.load();
    }

    void Level::LogBefore(const ChunkPos& pos) {
        if (!undoOn)
            return;
        const int64_t epoch = currentEpoch.load();
        auto [slot, fresh] = loggedEpoch.try_emplace(pos, epoch);
        if (!fresh) {
            if (slot->second == epoch)
                return;
            slot->second = epoch;
        }
        UndoEntry entry;
        entry.epoch = epoch;
        entry.pos = pos;
        const auto it = chunks.find(pos);
        entry.existed = it != chunks.end();
        if (entry.existed) {
            entry.data = it->second.Clone();
            entry.bytes = entry.data.ApproxBytes();
        } else {
            entry.bytes = sizeof(UndoEntry);
        }
        undoBytes += entry.bytes;
        undo.push_back(std::move(entry));
        while (undoBytes > undoCap && undo.size() > 1) {
            const int64_t oldest = undo.front().epoch;
            while (!undo.empty() && undo.front().epoch == oldest) {
                undoBytes -= std::min(undoBytes, undo.front().bytes);
                undo.pop_front();
            }
            undoFloor = oldest + 1;
        }
    }

    bool Level::CanRewindTo(int64_t epoch) const {
        std::lock_guard lock(mutex);
        return undoOn && epoch >= undoFloor;
    }

    bool Level::RewindTo(int64_t epoch) {
        std::lock_guard lock(mutex);
        if (!undoOn || epoch < undoFloor)
            return false;
        while (!undo.empty() && undo.back().epoch >= epoch) {
            UndoEntry& e = undo.back();
            const auto cur = chunks.find(e.pos);
            if (e.existed) {
                uint64_t version = e.data.version, border = e.data.borderVersion;
                if (cur != chunks.end()) {
                    version = std::max(version, cur->second.version);
                    border = std::max(border, cur->second.borderVersion);
                    if (cur->second.lodOnly)
                        lodOnlyCount--;
                    chunks.erase(cur);
                }
                auto [it, inserted] = chunks.emplace(e.pos, std::move(e.data));
                (void)inserted;
                it->second.version = version + 1;
                it->second.borderVersion = border + 1;
                it->second.queued = false;
                if (it->second.lodOnly)
                    lodOnlyCount++;
                Touch(it->first, it->second);
            }
            undoBytes -= std::min(undoBytes, e.bytes);
            undo.pop_back();
        }
        loggedEpoch.clear();
        changeCounter++;
        currentEpoch.store(epoch);
        return true;
    }

    void Level::TakeFrom(Level& other) {
        if (&other == this)
            return;
        std::scoped_lock lock(mutex, other.mutex);
        chunks = std::move(other.chunks);
        other.chunks.clear();
        dirty = std::move(other.dirty);
        other.dirty.clear();
        undo = std::move(other.undo);
        other.undo.clear();
        loggedEpoch = std::move(other.loggedEpoch);
        other.loggedEpoch.clear();
        undoBytes = other.undoBytes;
        other.undoBytes = 0;
        undoCap = other.undoCap;
        undoOn = other.undoOn;
        undoFloor = other.undoFloor;
        currentEpoch.store(other.currentEpoch.load());
        lodOnlyCount = other.lodOnlyCount.load();
        other.lodOnlyCount = 0;
        changeCounter++;
        for (auto& [pos, chunk] : chunks)
            chunk.queued = false;
        dirty.clear();
    }

    uint16_t Level::GetBlock(int x, int y, int z) const {
        std::lock_guard lock(mutex);
        const auto it = chunks.find({x >> 4, z >> 4});
        return it == chunks.end() ? 0 : it->second.Get(x & 15, y, z & 15);
    }

    uint16_t Level::GetBlock(int x, int y, int z, bool& loaded) const {
        std::lock_guard lock(mutex);
        const auto it = chunks.find({x >> 4, z >> 4});
        loaded = it != chunks.end();
        return loaded ? it->second.Get(x & 15, y, z & 15) : 0;
    }

    uint16_t Level::GetLiquid(int x, int y, int z) const {
        std::lock_guard lock(mutex);
        const auto it = chunks.find({x >> 4, z >> 4});
        return it == chunks.end() ? 0 : it->second.GetLiquid(x & 15, y, z & 15);
    }

    void Level::SetLiquid(int x, int y, int z, uint16_t id) {
        std::lock_guard lock(mutex);
        const auto it = chunks.find({x >> 4, z >> 4});
        if (it == chunks.end())
            return;
        if (it->second.GetLiquid(x & 15, y, z & 15) == id)
            return;
        LogBefore(it->first);
        changeCounter++;
        it->second.SetLiquid(x & 15, y, z & 15, id);
        it->second.version++;
        if ((x & 15) == 0 || (x & 15) == 15 || (z & 15) == 0 || (z & 15) == 15)
            it->second.borderVersion++;
        Touch(it->first, it->second);
    }

    uint8_t Level::GetBiome(int x, int y, int z) const {
        std::lock_guard lock(mutex);
        const auto it = chunks.find({x >> 4, z >> 4});
        return it == chunks.end() ? Chunk::DEFAULT_BIOME : it->second.GetBiome(x & 15, y, z & 15);
    }

    int Level::FindBlockBelow(int x, int z, int fromY, const std::function<bool(uint16_t)>& accept) const {
        std::lock_guard lock(mutex);
        const auto it = chunks.find({x >> 4, z >> 4});
        if (it == chunks.end())
            return Chunk::MIN_Y - 1;
        const Chunk& chunk = it->second;
        const int top = std::min(fromY, Chunk::MIN_Y + Chunk::HEIGHT - 1);
        for (int y = top; y >= Chunk::MIN_Y; y--)
            if (accept(chunk.Get(x & 15, y, z & 15)))
                return y;
        return Chunk::MIN_Y - 1;
    }

    void Level::SetBlock(int x, int y, int z, uint16_t id) {
        std::lock_guard lock(mutex);
        const auto it = chunks.find({x >> 4, z >> 4});
        if (it == chunks.end())
            return;
        if (it->second.Get(x & 15, y, z & 15) == id)
            return;
        LogBefore(it->first);
        changeCounter++;
        it->second.Set(x & 15, y, z & 15, id);
        it->second.version++;
        if ((x & 15) == 0 || (x & 15) == 15 || (z & 15) == 0 || (z & 15) == 15)
            it->second.borderVersion++;
        Touch(it->first, it->second);
    }

    bool Level::RemoveChunk(int cx, int cz) {
        std::lock_guard lock(mutex);
        const auto found = chunks.find({cx, cz});
        if (found == chunks.end())
            return false;
        LogBefore(found->first);
        if (found->second.lodOnly)
            lodOnlyCount--;
        chunks.erase(found);
        dirty.push_back({cx, cz});
        changeCounter++;
        return true;
    }

    uint64_t Level::NowMs() {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now().time_since_epoch())
                                         .count());
    }

    void Level::TouchNeighbors(int cx, int cz) {
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++) {
                const auto it = chunks.find({cx + dx, cz + dz});
                if (it != chunks.end())
                    Touch(it->first, it->second);
            }
    }

    void Level::MarkAllDirty() {
        std::lock_guard lock(mutex);
        for (auto& [pos, chunk] : chunks)
            Touch(pos, chunk);
    }

    void Level::DrainDirty(std::vector<DirtyChunk>& out) {
        std::lock_guard lock(mutex);
        for (const ChunkPos& pos : dirty) {
            const auto it = chunks.find(pos);
            DirtyChunk d;
            d.pos = pos;
            if (it != chunks.end()) {
                it->second.queued = false;
                d.exists = true;
                d.version = it->second.version;
                d.borderVersion = it->second.borderVersion;
                d.streaming = it->second.streaming;
                d.firstSeen = it->second.firstSeen;
                d.readyAt = it->second.readyAt;
                d.lodOnly = it->second.lodOnly;
            }
            out.push_back(d);
        }
        dirty.clear();
    }

    bool Level::PromoteLodOnly(int cx, int cz) {
        std::lock_guard lock(mutex);
        const auto it = chunks.find({cx, cz});
        if (it == chunks.end())
            return false;
        if (it->second.lodOnly) {
            LogBefore(it->first);
            it->second.lodOnly = false;
            lodOnlyCount--;
            it->second.version++;
            it->second.borderVersion++;
            Touch(it->first, it->second);
            changeCounter++;
        }
        return true;
    }

    bool Level::RemoveLodOnly(int cx, int cz) {
        std::lock_guard lock(mutex);
        const auto it = chunks.find({cx, cz});
        if (it == chunks.end() || !it->second.lodOnly)
            return false;
        LogBefore(it->first);
        chunks.erase(it);
        lodOnlyCount--;
        dirty.push_back({cx, cz});
        changeCounter++;
        return true;
    }

    void Level::EnsureChunk(int cx, int cz, bool streaming, bool lodOnly) {
        std::lock_guard lock(mutex);
        changeCounter++;
        if (chunks.find({cx, cz}) == chunks.end())
            LogBefore({cx, cz});
        const auto [it, inserted] = chunks.try_emplace({cx, cz});
        if (inserted) {
            it->second.lodOnly = lodOnly;
            if (lodOnly)
                lodOnlyCount++;
            static std::atomic<uint64_t> loads{0};
            it->second.version = (loads.fetch_add(1, std::memory_order_relaxed) + 1) << 24;
            it->second.streaming = streaming;
            it->second.firstSeen = NowMs();
            it->second.readyAt = streaming ? 0 : it->second.firstSeen;
            Touch(it->first, it->second);
        }
    }

    void Level::MarkComplete(int cx, int cz) {
        std::lock_guard lock(mutex);
        const auto it = chunks.find({cx, cz});
        if (it == chunks.end() || !it->second.streaming)
            return;
        it->second.streaming = false;
        it->second.readyAt = NowMs();
        Touch(it->first, it->second);
        changeCounter++;
    }

    bool Level::HasSectionAt(int x, int y, int z) const {
        std::lock_guard lock(mutex);
        const auto it = chunks.find({x >> 4, z >> 4});
        return it != chunks.end() && !it->second.lodOnly && it->second.HasSection(y);
    }

    bool Level::HasChunk(int cx, int cz) const {
        std::lock_guard lock(mutex);
        return chunks.contains({cx, cz});
    }

    void Level::SetSubChunk(int cx, int cz, int absSubY, const uint16_t* cells, const uint16_t* liquidCells) {
        const int baseY = absSubY * 16;
        if (baseY < Chunk::MIN_Y || baseY + 16 > Chunk::MIN_Y + Chunk::HEIGHT)
            return;

        std::lock_guard lock(mutex);
        const auto found = chunks.find({cx, cz});
        if (found == chunks.end())
            return;
        if (found->second.SectionEquals((baseY - Chunk::MIN_Y) >> 4, cells, liquidCells))
            return;
        LogBefore(found->first);
        changeCounter++;
        Chunk& chunk = found->second;
        chunk.SetSection((baseY - Chunk::MIN_Y) >> 4, cells, liquidCells);
        chunk.version++;
        chunk.borderVersion++;
        Touch(found->first, chunk);
    }

    void Level::SetBiomeSubChunk(int cx, int cz, int absSubY, const uint8_t* cells) {
        const int baseY = absSubY * 16;
        if (baseY < Chunk::MIN_Y || baseY + 16 > Chunk::MIN_Y + Chunk::HEIGHT)
            return;

        std::lock_guard lock(mutex);
        const auto found = chunks.find({cx, cz});
        if (found == chunks.end())
            return;
        if (found->second.BiomeSectionEquals((baseY - Chunk::MIN_Y) >> 4, cells))
            return;
        LogBefore(found->first);
        changeCounter++;
        Chunk& chunk = found->second;
        chunk.SetBiomeSection((baseY - Chunk::MIN_Y) >> 4, cells);
        chunk.version++;
        chunk.borderVersion++;
        Touch(found->first, chunk);
    }
}
