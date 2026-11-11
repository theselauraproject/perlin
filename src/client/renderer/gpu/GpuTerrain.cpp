#include "client/renderer/gpu/GpuTerrain.hpp"

#include "util/Env.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>
#include <unordered_set>

namespace perlin::gpu {
    // lib_6_8 dxil of terrain_cull_graph.hlsl, empty when the build had no dxc for d3d12
    extern const unsigned char kTerrainGraphDxil[];
    extern const size_t kTerrainGraphDxilSize;

    namespace {
        struct Params {
            float planes[6][4];
            float camera[4];
            float clipRow[4][4];
            uint32_t recordCount;
            int32_t cullRadius;
            int32_t cameraChunkX;
            int32_t cameraChunkZ;
            uint32_t hizLevels;
            uint32_t depthWidth;
            uint32_t depthHeight;
            uint32_t options;
            uint32_t hizOffsets[16];
        };
        static_assert(sizeof(Params) == 272, "Params must match CullParams in terrain_cull.slang");

        struct CullPush {
            // the work graph reads chunkCount; mode 3 (shadow casters) reads the slice and the planes
            uint32_t mode, bucketCount, hizValid, chunkCount;
            uint32_t sliceFirst = 0, sliceEnd = 0, pad0 = 0, pad1 = 0;
            float planes[6][4] = {};
        };
        static_assert(sizeof(CullPush) == 128, "CullPush must match PushData in terrain_cull.slang");
        struct ChunkRow {
            uint32_t start, count;
            int32_t chunkX, chunkZ;
            float minX, minY, minZ;
            uint32_t flags;
            float maxX, maxY, maxZ;
            uint32_t pad;
        };
        static_assert(sizeof(ChunkRow) == 48, "ChunkRow must match terrain_cull_graph.hlsl");
        struct HizPush {
            uint32_t srcWidth, srcHeight, dstWidth, dstHeight, srcOffset, dstOffset, fromDepth, pad;
        };

        constexpr uint32_t kFlagLod = 1u;
        constexpr uint32_t kOptFaces = 1u, kOptCave = 2u, kOptHiz = 4u;
        constexpr uint32_t kCommandBytes = 5 * sizeof(uint32_t);
        constexpr uint32_t kOtherGroup = 3;
        // a camera on a face plane sees it edge-on; the slack also covers shaders that nudge vertices (waving)
        constexpr float kFaceMargin = 0.5f;

        conduit::shader_handle MakeCompute(Gpu& g, const char* name) {
            const conduit::shader_library& library = g.Library();
            const conduit::program_info* info = library.find(name);
            if (!info)
                return {};
            const conduit::shader_target target = conduit::shader_library::target_for(g.Api());
            const auto code = library.code(*info, conduit::shader_stage::compute, target);
            if (code.empty())
                return {};
            conduit::shader_desc sd;
            sd.stage = conduit::shader_stage::compute;
            sd.format = conduit::shader_library::format_for(target);
            sd.code = code;
            sd.entry = conduit::shader_library::entry_for(conduit::shader_stage::compute, target);
            sd.name = name;
            return g.Dev().create_shader(sd);
        }

        uint32_t MaxRuns(const GpuTerrain::Record& r) {
            uint32_t groups = 0;
            for (uint32_t n : r.faces)
                groups += n != 0;
            return std::min(groups, 4u);
        }
    }

    namespace {
        conduit::buffer_handle MakeBuffer(Gpu& g, uint64_t bytes, conduit::buffer_usage usage, uint32_t stride,
                                          const char* name) {
            conduit::buffer_desc b;
            b.size = std::max<uint64_t>(bytes, 16);
            b.usage = usage;
            b.memory = conduit::memory_class::gpu_only;
            b.stride = stride;
            b.name = name;
            return g.Dev().create_buffer(b);
        }

        void Drop(Gpu& g, conduit::buffer_handle& b) {
            if (!b)
                return;
            g.ForgetBuffer(b);
            // deferred by conduit until the gpu is done with it: no stall while chunks stream in
            g.Dev().destroy(b);
            b = {};
        }
    }

    int GpuTerrain::LayerSlot(RenderLayer layer) {
        for (size_t i = 0; i < kLayers.size(); i++)
            if (kLayers[i] == layer)
                return static_cast<int>(i);
        return -1;
    }

    uint32_t GpuTerrain::VisibleFaces(const Record& r, const float c[3], float m) {
        uint32_t mask = 1u << kOtherGroup;
        if (c[0] > r.minX - m)
            mask |= 1u << 0;
        if (c[1] > r.minY - m)
            mask |= 1u << 1;
        if (c[2] > r.minZ - m)
            mask |= 1u << 2;
        if (c[2] < r.maxZ + m)
            mask |= 1u << 4;
        if (c[1] < r.maxY + m)
            mask |= 1u << 5;
        if (c[0] < r.maxX + m)
            mask |= 1u << 6;
        return mask;
    }

    uint32_t GpuTerrain::Runs(const Record& r, uint32_t mask, uint32_t first[4], uint32_t count[4]) {
        uint32_t runs = 0, offset = r.first;
        bool open = false;
        for (uint32_t g = 0; g < kFaceGroups; g++) {
            const uint32_t n = r.faces[g];
            if (n && (mask & (1u << g))) {
                if (open) {
                    count[runs - 1] += n;
                } else if (runs < 4) {
                    first[runs] = offset;
                    count[runs] = n;
                    runs++;
                    open = true;
                }
            } else if (n) {
                open = false;
            }
            offset += n;
        }
        return runs;
    }

    bool GpuTerrain::CreatePipelines() {
        conduit::device& dev = gpu->Dev();
        cullShader = MakeCompute(*gpu, "terrain_cull");
        hizShader = MakeCompute(*gpu, "hiz_build");
        if (!cullShader || !hizShader)
            return false;
        const auto cs = conduit::shader_stage::compute;
        const conduit::bind_layout_entry cullEntries[] = {
            {0, conduit::binding_type::storage_buffer, cs},    {1, conduit::binding_type::storage_buffer, cs},
            {2, conduit::binding_type::storage_buffer, cs},    {3, conduit::binding_type::storage_buffer_rw, cs},
            {4, conduit::binding_type::storage_buffer_rw, cs}, {5, conduit::binding_type::storage_buffer_rw, cs},
            {6, conduit::binding_type::storage_buffer_rw, cs}, {7, conduit::binding_type::storage_buffer_rw, cs},
            {8, conduit::binding_type::uniform_buffer, cs},    {9, conduit::binding_type::storage_buffer, cs},
            {10, conduit::binding_type::storage_buffer_rw, cs},
        };
        conduit::bind_layout_desc bl;
        bl.entries = cullEntries;
        bl.name = "terrain cull bindings";
        cullLayout = dev.create_bind_layout(bl);
        const conduit::bind_layout_entry hizEntries[] = {
            {0, conduit::binding_type::sampled_texture, cs},
            {1, conduit::binding_type::storage_buffer_rw, cs},
        };
        bl.entries = hizEntries;
        bl.name = "depth pyramid bindings";
        hizLayout = dev.create_bind_layout(bl);

        conduit::pipeline_layout_desc pl;
        pl.sets[0] = cullLayout;
        pl.push_constant_bytes = sizeof(CullPush);
        pl.push_constant_stages = cs;
        pl.name = "terrain cull layout";
        cullPipelineLayout = dev.create_pipeline_layout(pl);
        pl.sets[0] = hizLayout;
        pl.push_constant_bytes = sizeof(HizPush);
        pl.name = "depth pyramid layout";
        hizPipelineLayout = dev.create_pipeline_layout(pl);

        conduit::compute_pipeline_desc cp;
        cp.compute = cullShader;
        cp.layout = cullPipelineLayout;
        cp.name = "terrain_cull";
        cullPipeline = dev.create_compute_pipeline(cp);
        cp.compute = hizShader;
        cp.layout = hizPipelineLayout;
        cp.name = "hiz_build";
        hizPipeline = dev.create_compute_pipeline(cp);
        return cullLayout && hizLayout && cullPipelineLayout && hizPipelineLayout && cullPipeline && hizPipeline;
    }

    bool GpuTerrain::Init(Gpu& g, ChunkMeshStore& store) {
        Shutdown();
        gpu = &g;
        meshes = &store;
        const conduit::device_features& f = g.Caps().features;
        if (!f.multi_draw_indirect || !f.draw_indirect_count)
            return false;
        if (!CreatePipelines())
            return false;
        conduit::device& dev = g.Dev();
        const uint32_t frames = std::max(1u, g.Frames());
        for (uint32_t i = 0; i < frames; i++) {
            conduit::buffer_desc bd;
            bd.size = 512;
            bd.usage = conduit::buffer_usage::uniform;
            bd.memory = conduit::memory_class::upload;
            bd.name = "terrain cull params";
            const conduit::buffer_handle b = dev.create_buffer(bd);
            uint8_t* map = b ? dev.map(b) : nullptr;
            if (!map)
                return false;
            paramBuffers.push_back(b);
            paramMaps.push_back(map);
        }
        groups.assign(frames, {});
        if (!EnsureRecordCapacity(16384) || !EnsureBuckets(64) || !EnsureCaveCapacity(4096))
            return false;
        chunkTable = MakeBuffer(g, 4096 * sizeof(ChunkRow), conduit::buffer_usage::storage | conduit::buffer_usage::copy_dst,
                                sizeof(ChunkRow), "terrain chunk table");
        if (!chunkTable)
            return false;
        chunkTableCapacity = 4096;
        TryWorkGraph();
        store.TrackChanges(true);
        resync = true;
        ready = true;
        return true;
    }

    void GpuTerrain::Shutdown() {
        if (meshes)
            meshes->TrackChanges(false);
        if (gpu) {
            conduit::device& dev = gpu->Dev();
            for (conduit::bind_group_handle h : groups)
                dev.destroy(h);
            dev.destroy(graphPipeline);
            for (conduit::buffer_handle b : {recordBuffer, visibility, bucketBuffer, caveBuffer, commandsEarly,
                                             commandsLate, counts, hiz, chunkTable, commandsShadow})
                if (b) {
                    gpu->ForgetBuffer(b);
                    dev.destroy(b);
                }
            for (conduit::buffer_handle b : paramBuffers)
                dev.destroy(b);
            dev.destroy(cullPipeline);
            dev.destroy(hizPipeline);
            dev.destroy(cullPipelineLayout);
            dev.destroy(hizPipelineLayout);
            dev.destroy(cullLayout);
            dev.destroy(hizLayout);
            dev.destroy(cullShader);
            dev.destroy(hizShader);
        }
        *this = GpuTerrain{};
    }

    bool GpuTerrain::EnsureRecordCapacity(uint32_t wanted) {
        if (wanted <= recordCapacity)
            return true;
        uint32_t size = std::max(recordCapacity, 16384u);
        while (size < wanted)
            size *= 2;
        Drop(*gpu, recordBuffer);
        Drop(*gpu, visibility);
        const auto storage = conduit::buffer_usage::storage | conduit::buffer_usage::copy_dst;
        recordBuffer = MakeBuffer(*gpu, uint64_t{size} * sizeof(Record), storage, sizeof(Record), "terrain records");
        visibility = MakeBuffer(*gpu, uint64_t{size} * 4, storage, 4, "terrain visibility");
        if (!recordBuffer || !visibility)
            return false;
        recordCapacity = size;
        mirror.resize(size, Record{});
        // the cpu mirror is the source of truth: a new buffer is filled from it, the gpu never copies the old one
        const std::vector<uint32_t> zeros(size, 0u);
        gpu->WriteBuffer(visibility, conduit::state::shader_write, 0, zeros.data(), zeros.size() * 4);
        if (highWater)
            dirtyRanges.push_back({0, highWater});
        groupsDirty = true;
        return true;
    }

    bool GpuTerrain::EnsureBuckets(uint32_t wanted) {
        if (wanted > bucketCapacity) {
            uint32_t size = std::max(bucketCapacity, 64u);
            while (size < wanted)
                size *= 2;
            Drop(*gpu, bucketBuffer);
            Drop(*gpu, counts);
            bucketBuffer = MakeBuffer(*gpu, uint64_t{size} * 8,
                                      conduit::buffer_usage::storage | conduit::buffer_usage::copy_dst, 8,
                                      "terrain buckets");
            counts = MakeBuffer(*gpu, uint64_t{size} * 3 * 4,
                                conduit::buffer_usage::storage | conduit::buffer_usage::indirect |
                                    conduit::buffer_usage::copy_dst,
                                4, "terrain draw counts");
            if (!bucketBuffer || !counts)
                return false;
            bucketCapacity = size;
            layoutDirty = groupsDirty = true;
        }
        if (bucketNeed.size() < wanted) {
            bucketNeed.resize(wanted, 0);
            bucketBase.resize(wanted, 0);
            bucketCap.resize(wanted, 0);
            layoutDirty = true;
        }
        return true;
    }

    bool GpuTerrain::EnsureCaveCapacity(uint32_t wanted) {
        if (wanted <= caveCapacity)
            return true;
        uint32_t size = std::max(caveCapacity, 4096u);
        while (size < wanted)
            size *= 2;
        Drop(*gpu, caveBuffer);
        caveBuffer = MakeBuffer(*gpu, uint64_t{size} * 4,
                                conduit::buffer_usage::storage | conduit::buffer_usage::copy_dst, 4, "terrain caves");
        if (!caveBuffer)
            return false;
        caveCapacity = size;
        caveBits.resize(size, ~0u);
        caveBuilt = ~0ull;
        groupsDirty = true;
        return true;
    }

    bool GpuTerrain::EnsureHiz(uint32_t w, uint32_t h) {
        if (w == hizW && h == hizH && hiz)
            return true;
        uint32_t lw = (w + 1) / 2, lh = (h + 1) / 2, total = 0, levels = 0;
        while (levels < 16) {
            hizOffsets[levels++] = total;
            total += lw * lh;
            if (lw == 1 && lh == 1)
                break;
            lw = std::max(1u, (lw + 1) / 2);
            lh = std::max(1u, (lh + 1) / 2);
        }
        if (total > hizFloats) {
            Drop(*gpu, hiz);
            hiz = MakeBuffer(*gpu, uint64_t{total} * 4, conduit::buffer_usage::storage, 4, "depth pyramid");
            if (!hiz)
                return false;
            hizFloats = total;
            groupsDirty = true;
        }
        hizW = w;
        hizH = h;
        hizLevels = levels;
        return true;
    }

    void GpuTerrain::RebuildGroups() {
        conduit::device& dev = gpu->Dev();
        if (!hiz && !EnsureHiz(2, 2))
            return;
        for (size_t i = 0; i < groups.size(); i++) {
            dev.destroy(groups[i]);
            const conduit::bind_group_entry entries[] = {
                {0, 0, {recordBuffer, 0, 0}, {}, {}},  {1, 0, {bucketBuffer, 0, 0}, {}, {}},
                {2, 0, {caveBuffer, 0, 0}, {}, {}},    {3, 0, {visibility, 0, 0}, {}, {}},
                {4, 0, {commandsEarly, 0, 0}, {}, {}}, {5, 0, {commandsLate, 0, 0}, {}, {}},
                {6, 0, {counts, 0, 0}, {}, {}},        {7, 0, {hiz, 0, 0}, {}, {}},
                {8, 0, {paramBuffers[i], 0, sizeof(Params)}, {}, {}},
                {9, 0, {chunkTable, 0, 0}, {}, {}},
                {10, 0, {commandsShadow, 0, 0}, {}, {}},
            };
            conduit::bind_group_desc bg;
            bg.layout = cullLayout;
            bg.entries = entries;
            bg.name = "terrain cull group";
            groups[i] = dev.create_bind_group(bg);
        }
        groupsDirty = false;
    }

    uint32_t GpuTerrain::AllocBlock(uint32_t size) {
        if (const auto it = freeBlocks.find(size); it != freeBlocks.end() && !it->second.empty()) {
            const uint32_t start = it->second.back();
            it->second.pop_back();
            return start;
        }
        const uint32_t start = highWater;
        highWater += size;
        return start;
    }

    void GpuTerrain::FreeBlock(uint32_t start, uint32_t size) {
        for (uint32_t i = start; i < start + size; i++) {
            const Record& r = mirror[i];
            if (r.bucket != ~0u && r.bucket < bucketNeed.size())
                bucketNeed[r.bucket] -= MaxRuns(r);
            mirror[i] = Record{};
            mirror[i].bucket = ~0u;
        }
        dirtyRanges.push_back({start, size});
        freeBlocks[size].push_back(start);
    }

    void GpuTerrain::BuildRecords(const ChunkMeshStore::Chunk& g, bool lod, uint32_t caveSlot,
                                  std::vector<Record>& out) const {
        out.clear();
        if (g.page < 0)
            return;
        const float s = static_cast<float>(g.size);
        const float x0 = static_cast<float>(g.cx) * s, z0 = static_cast<float>(g.cz) * s;
        for (size_t li = 0; li < kLayers.size(); li++) {
            const RenderLayer layer = kLayers[li];
            const size_t slot = static_cast<size_t>(layer);
            if (!g.layerCount[slot])
                continue;
            const int split = SplitSlot(layer);
            for (const ChunkMeshStore::Section& sec : g.sections) {
                if (!sec.count[slot])
                    continue;
                Record r{};
                r.minX = x0;
                r.minY = sec.minY;
                r.minZ = z0;
                r.maxX = x0 + s;
                r.maxY = sec.maxY;
                r.maxZ = z0 + s;
                r.first = g.iOff[slot] + sec.start[slot];
                r.bucket = static_cast<uint32_t>(g.page) * static_cast<uint32_t>(kLayers.size()) +
                           static_cast<uint32_t>(li);
                bool faced = false;
                if (split >= 0 && sec.split) {
                    uint32_t sum = 0;
                    for (uint32_t n : sec.faces[static_cast<size_t>(split)])
                        sum += n;
                    if (sum == sec.count[slot]) {
                        for (int k = 0; k < kFaceGroups; k++)
                            r.faces[k] = sec.faces[static_cast<size_t>(split)][static_cast<size_t>(k)];
                        faced = true;
                    }
                }
                if (!faced)
                    r.faces[kOtherGroup] = sec.count[slot];
                r.flags = lod ? kFlagLod : 0u;
                r.chunkX = g.cx;
                r.chunkZ = g.cz;
                r.caveSlot = caveSlot;
                r.caveBit = 255u;
                if (!lod && !g.conn.empty()) {
                    const int bit = sec.sy - g.visMin;
                    if (bit >= 0 && bit < 32)
                        r.caveBit = static_cast<uint32_t>(bit);
                }
                out.push_back(r);
            }
        }
    }

    void GpuTerrain::Release(const Key& key) {
        const auto it = entries.find(key);
        if (it == entries.end())
            return;
        if (it->second.size)
            FreeBlock(it->second.start, it->second.size);
        if (it->second.caveSlot != ~0u) {
            caveBits[it->second.caveSlot] = ~0u;
            caveFree.push_back(it->second.caveSlot);
        }
        entries.erase(it);
        chunkTableDirty = true;
    }

    void GpuTerrain::Place(const Key& key, const ChunkMeshStore::Chunk& g) {
        static thread_local std::vector<Record> built;
        uint32_t caveSlot = ~0u;
        if (!key.lod) {
            if (!caveFree.empty()) {
                caveSlot = caveFree.back();
                caveFree.pop_back();
            } else {
                caveSlot = caveHigh++;
            }
            if (!EnsureCaveCapacity(caveHigh))
                return;
            caveBuilt = ~0ull;
        }
        BuildRecords(g, key.lod, caveSlot, built);
        Entry e;
        e.caveSlot = caveSlot;
        e.size = static_cast<uint32_t>(built.size());
        if (e.size) {
            e.start = AllocBlock(e.size);
            if (!EnsureRecordCapacity(highWater))
                return;
            const uint32_t buckets = static_cast<uint32_t>(meshes->Pages() * kLayers.size());
            EnsureBuckets(buckets);
            for (uint32_t i = 0; i < e.size; i++) {
                mirror[e.start + i] = built[i];
                const uint32_t b = built[i].bucket;
                if (b >= bucketNeed.size())
                    EnsureBuckets(b + 1);
                bucketNeed[b] += MaxRuns(built[i]);
                if (bucketNeed[b] > bucketCap[b])
                    layoutDirty = true;
            }
            dirtyRanges.push_back({e.start, e.size});
            stats.uploadedRecords += e.size;
        }
        stats.rebuiltChunks++;
        entries[key] = e;
        chunkTableDirty = true;
    }

    void GpuTerrain::FullResync() {
        for (auto& [key, e] : entries)
            if (e.size)
                for (uint32_t i = e.start; i < e.start + e.size; i++)
                    mirror[i] = Record{}, mirror[i].bucket = ~0u;
        entries.clear();
        freeBlocks.clear();
        caveFree.clear();
        caveHigh = 0;
        std::fill(caveBits.begin(), caveBits.end(), ~0u);
        std::fill(bucketNeed.begin(), bucketNeed.end(), 0u);
        if (highWater)
            dirtyRanges.push_back({0, highWater});
        highWater = 0;
        for (const auto& [key, g] : meshes->ChunkMap())
            Place({key, false}, g);
        for (const auto& [key, g] : meshes->LodMap())
            Place({key, true}, g);
        caveBuilt = ~0ull;
        layoutDirty = true;
        // fresh records start invisible: the late pass tests them all once
        const std::vector<uint32_t> zeros(recordCapacity, 0u);
        gpu->WriteBuffer(visibility, conduit::state::shader_write, 0, zeros.data(), zeros.size() * 4);
    }

    void GpuTerrain::SyncChunks() {
        bool cleared = false;
        meshes->TakeChanges(changes, cleared);
        if (cleared || resync) {
            resync = false;
            changes.clear();
            FullResync();
            return;
        }
        if (changes.empty())
            return;
        std::unordered_set<Key, KeyHash> seen;
        seen.reserve(changes.size());
        std::vector<uint32_t> fresh;
        for (const ChunkMeshStore::Change& c : changes) {
            const Key key{c.key, c.lod};
            if (!seen.insert(key).second)
                continue;
            Release(key);
            const auto& map = c.lod ? meshes->LodMap() : meshes->ChunkMap();
            const auto it = map.find(c.key);
            if (it == map.end() || it->second.page < 0)
                continue;
            Place(key, it->second);
            const auto placed = entries.find(key);
            if (placed != entries.end() && placed->second.size)
                for (uint32_t i = 0; i < placed->second.size; i++)
                    fresh.push_back(placed->second.start + i);
        }
        changes.clear();
        // a remeshed section starts invisible so the late pass decides; a stale bit from the slot's previous owner
        // would only cost an early draw, but zero is cheap to send
        if (!fresh.empty()) {
            std::sort(fresh.begin(), fresh.end());
            static const uint32_t zero[256] = {};
            size_t i = 0;
            while (i < fresh.size()) {
                size_t j = i + 1;
                while (j < fresh.size() && fresh[j] == fresh[j - 1] + 1 && j - i < 256)
                    j++;
                gpu->WriteBuffer(visibility, conduit::state::shader_write, uint64_t{fresh[i]} * 4, zero,
                                 (j - i) * 4);
                i = j;
            }
        }
    }

    void GpuTerrain::Layout() {
        const uint32_t buckets = static_cast<uint32_t>(meshes->Pages() * kLayers.size());
        EnsureBuckets(std::max<uint32_t>(buckets, static_cast<uint32_t>(bucketNeed.size())));
        bucketCount = static_cast<uint32_t>(bucketNeed.size());
        uint32_t total = 0;
        for (uint32_t b = 0; b < bucketCount; b++) {
            if (bucketNeed[b] > bucketCap[b] || bucketCap[b] > 4 * std::max(bucketNeed[b], 256u)) {
                // headroom so streaming chunks in does not relayout every frame
                bucketCap[b] = bucketNeed[b] ? std::bit_ceil(bucketNeed[b] + bucketNeed[b] / 4 + 64) : 0;
            }
            bucketBase[b] = total;
            total += bucketCap[b];
        }
        if (total > commandCapacity) {
            uint32_t size = std::max(commandCapacity, 65536u);
            while (size < total)
                size *= 2;
            Drop(*gpu, commandsEarly);
            Drop(*gpu, commandsLate);
            Drop(*gpu, commandsShadow);
            const auto usage = conduit::buffer_usage::storage | conduit::buffer_usage::indirect;
            commandsEarly = MakeBuffer(*gpu, uint64_t{size} * kCommandBytes, usage, 4, "terrain draws early");
            commandsLate = MakeBuffer(*gpu, uint64_t{size} * kCommandBytes, usage, 4, "terrain draws late");
            commandsShadow = MakeBuffer(*gpu, uint64_t{size} * kCommandBytes, usage, 4, "terrain shadow casters");
            commandCapacity = size;
            groupsDirty = true;
        }
        std::vector<uint32_t> table(static_cast<size_t>(bucketCount) * 2);
        for (uint32_t b = 0; b < bucketCount; b++) {
            table[b * 2] = bucketBase[b];
            table[b * 2 + 1] = bucketCap[b];
        }
        gpu->WriteBuffer(bucketBuffer, conduit::state::shader_read_other, 0, table.data(), table.size() * 4);
        layoutDirty = false;
    }

    void GpuTerrain::UploadRange(uint32_t first, uint32_t count) {
        if (!count)
            return;
        gpu->WriteBuffer(recordBuffer, conduit::state::shader_read_other, uint64_t{first} * sizeof(Record),
                         mirror.data() + first, uint64_t{count} * sizeof(Record));
    }

    void GpuTerrain::UploadCave() {
        if (caveHigh == 0)
            return;
        for (const auto& [key, e] : entries) {
            if (key.lod || e.caveSlot == ~0u)
                continue;
            const auto it = meshes->ChunkMap().find(key.key);
            caveBits[e.caveSlot] = it == meshes->ChunkMap().end() ? ~0u : it->second.secVis;
        }
        gpu->WriteBuffer(caveBuffer, conduit::state::shader_read_other, 0, caveBits.data(), uint64_t{caveHigh} * 4);
    }

    void GpuTerrain::TryWorkGraph() {
        static const bool wanted = PERLIN_ENV("PERLIN_WORK_GRAPHS") != nullptr;
        if (!wanted)
            return;
        const auto say = [](const char* what) { std::fprintf(stderr, "[workgraph] %s\n", what); };
        if (gpu->Api() != conduit::api::d3d12)
            return say("off: work graphs need the Direct3D 12 renderer");
        if (!gpu->Caps().features.work_graphs)
            return say("off: this gpu or driver has no work graphs tier 1.0 (shader model 6.8)");
        if (!kTerrainGraphDxilSize)
            return say("off: this build has no work graph shader (dxc was not available for d3d12)");
        conduit::work_graph_desc wd;
        wd.library = std::span<const uint8_t>(kTerrainGraphDxil, kTerrainGraphDxilSize);
        wd.program = "terrain_cull_graph";
        wd.layout = cullPipelineLayout;
        wd.name = "terrain cull graph";
        graphPipeline = gpu->Dev().create_work_graph(wd);
        say(graphPipeline ? "on: the early terrain cull runs as a work graph (chunks -> sections)"
                          : "off: creating the work graph failed, see the conduit error above");
    }

    void GpuTerrain::UploadChunkTable() {
        std::vector<ChunkRow> rows;
        rows.reserve(entries.size());
        for (const auto& [key, e] : entries) {
            if (!e.size)
                continue;
            ChunkRow row{};
            row.start = e.start;
            row.count = e.size;
            const Record& first = mirror[e.start];
            row.chunkX = first.chunkX;
            row.chunkZ = first.chunkZ;
            row.flags = key.lod ? kFlagLod : 0u;
            row.minX = first.minX;
            row.minZ = first.minZ;
            row.maxX = first.maxX;
            row.maxZ = first.maxZ;
            row.minY = first.minY;
            row.maxY = first.maxY;
            for (uint32_t i = e.start; i < e.start + e.size; i++) {
                row.minY = std::min(row.minY, mirror[i].minY);
                row.maxY = std::max(row.maxY, mirror[i].maxY);
            }
            rows.push_back(row);
        }
        if (rows.size() > chunkTableCapacity) {
            uint32_t size = chunkTableCapacity;
            while (size < rows.size())
                size *= 2;
            Drop(*gpu, chunkTable);
            chunkTable = MakeBuffer(*gpu, uint64_t{size} * sizeof(ChunkRow),
                                    conduit::buffer_usage::storage | conduit::buffer_usage::copy_dst, sizeof(ChunkRow),
                                    "terrain chunk table");
            chunkTableCapacity = size;
            groupsDirty = true;
        }
        chunkRows = static_cast<uint32_t>(rows.size());
        if (!rows.empty())
            gpu->WriteBuffer(chunkTable, conduit::state::shader_read_other, 0, rows.data(), rows.size() * sizeof(ChunkRow));
        chunkTableDirty = false;
    }

    bool GpuTerrain::Prepare(const FrameInput& in) {
        prepared = false;
        occluding = false;
        shadowPending = false;
        if (!ready)
            return false;
        stats.uploadedRecords = stats.rebuiltChunks = 0;
        SyncChunks();
        if (!ready)
            return false;
        if (highWater > recordCapacity && !EnsureRecordCapacity(highWater))
            return false;
        // compaction: a fragmented block heap costs threads every frame; rebuilding it is one upload
        if (highWater > 65536) {
            size_t freeRecords = 0;
            for (const auto& [size, list] : freeBlocks)
                freeRecords += size_t{size} * list.size();
            if (freeRecords * 2 > highWater) {
                resync = true;
                SyncChunks();
            }
        }
        if (!dirtyRanges.empty()) {
            std::sort(dirtyRanges.begin(), dirtyRanges.end());
            uint32_t from = dirtyRanges[0].first, to = from + dirtyRanges[0].second;
            for (size_t i = 1; i < dirtyRanges.size(); i++) {
                const uint32_t a = dirtyRanges[i].first, b = a + dirtyRanges[i].second;
                // gaps of a few records are cheaper to resend than to split into another copy
                if (a <= to + 64) {
                    to = std::max(to, b);
                    continue;
                }
                UploadRange(from, std::min(to, recordCapacity) - from);
                from = a;
                to = b;
            }
            UploadRange(from, std::min(to, recordCapacity) - from);
            dirtyRanges.clear();
        }
        if (layoutDirty || static_cast<uint32_t>(meshes->Pages() * kLayers.size()) > bucketCount)
            Layout();
        if (graphPipeline && chunkTableDirty)
            UploadChunkTable();
        if (in.caveCulling && caveBuilt != meshes->CaveEpoch()) {
            UploadCave();
            caveBuilt = meshes->CaveEpoch();
        }
        occluding = in.occlusion && in.depthWidth >= 8 && in.depthHeight >= 8 && EnsureHiz(in.depthWidth, in.depthHeight);
        if (groupsDirty)
            RebuildGroups();
        frameSlot = gpu->FrameSlot();
        if (frameSlot >= groups.size() || !groups[frameSlot])
            return false;

        Params p{};
        std::memcpy(p.planes, in.frustum.planes, sizeof(p.planes));
        p.camera[0] = in.camera[0];
        p.camera[1] = in.camera[1];
        p.camera[2] = in.camera[2];
        p.camera[3] = kFaceMargin;
        for (int row = 0; row < 4; row++)
            for (int col = 0; col < 4; col++)
                p.clipRow[row][col] = in.clipFromWorld[col][row];
        p.recordCount = highWater;
        p.cullRadius = in.cullRadius;
        p.cameraChunkX = static_cast<int32_t>(std::floor(in.camera[0] / 16.0f));
        p.cameraChunkZ = static_cast<int32_t>(std::floor(in.camera[2] / 16.0f));
        p.hizLevels = hizLevels;
        p.depthWidth = in.depthWidth;
        p.depthHeight = in.depthHeight;
        static const bool noFaces = PERLIN_ENV("PERLIN_NO_FACE_CULL") != nullptr;
        p.options = (noFaces ? 0u : kOptFaces) | (in.caveCulling ? kOptCave : 0u) | (occluding ? kOptHiz : 0u);
        std::memcpy(p.hizOffsets, hizOffsets.data(), sizeof(p.hizOffsets));
        std::memcpy(paramMaps[frameSlot], &p, sizeof(p));
        gpu->Dev().flush(paramBuffers[frameSlot], 0, sizeof(p));
        depthW = in.depthWidth;
        depthH = in.depthHeight;

        stats.records = highWater;
        stats.liveChunks = static_cast<uint32_t>(entries.size());
        stats.recordCapacity = recordCapacity;
        stats.buckets = bucketCount;
        stats.commandCapacity = commandCapacity;
        stats.occlusion = occluding;
        prepared = highWater > 0 && bucketCount > 0;
        return prepared;
    }

    void GpuTerrain::RecordEarly(conduit::command_list& list) {
        if (!prepared)
            return;
        list.begin_debug_label("terrain cull", 0xFF40C040u);
        const conduit::buffer_barrier toClear[] = {
            {counts, conduit::state::indirect_argument, conduit::state::copy_dst},
            {commandsEarly, conduit::state::indirect_argument, conduit::state::shader_write},
            {commandsLate, conduit::state::indirect_argument, conduit::state::shader_write},
            {commandsShadow, conduit::state::indirect_argument, conduit::state::shader_write},
        };
        list.barrier({}, toClear);
        list.fill_buffer(counts, 0, uint64_t{bucketCount} * 3 * 4, 0);
        const conduit::buffer_barrier toWrite[] = {
            {counts, conduit::state::copy_dst, conduit::state::shader_write},
        };
        list.barrier({}, toWrite);
        // last frame's late pass wrote the visibility bits this pass reads
        list.uav_barrier(visibility);
        const CullPush push{occluding ? 1u : 0u, bucketCount, 0u, chunkRows};
        if (graphPipeline && chunkRows) {
            struct {
                uint32_t grid, chunkCount;
            } entry{(chunkRows + 63) / 64, chunkRows};
            list.set_pipeline(graphPipeline);
            list.set_bind_group(0, groups[frameSlot]);
            list.push_constants(&push, sizeof push);
            list.dispatch_graph(&entry, 1, sizeof entry);
        } else {
            list.set_pipeline(cullPipeline);
            list.set_bind_group(0, groups[frameSlot]);
            list.push_constants(&push, sizeof push);
            list.dispatch((highWater + 63) / 64, 1, 1);
        }
        if (shadowPending && shadowEnd > shadowFirst) {
            CullPush shadowPush{3u, bucketCount, 0u, chunkRows};
            shadowPush.sliceFirst = shadowFirst;
            shadowPush.sliceEnd = shadowEnd;
            std::memcpy(shadowPush.planes, shadowPlanes, sizeof(shadowPlanes));
            list.set_pipeline(cullPipeline);
            list.set_bind_group(0, groups[frameSlot]);
            list.push_constants(&shadowPush, sizeof shadowPush);
            list.dispatch((shadowEnd - shadowFirst + 63) / 64, 1, 1);
        }
        std::vector<conduit::buffer_barrier> toDraw = {
            {counts, conduit::state::shader_write, conduit::state::indirect_argument},
            {commandsEarly, conduit::state::shader_write, conduit::state::indirect_argument},
            {commandsShadow, conduit::state::shader_write, conduit::state::indirect_argument},
        };
        if (!occluding)
            toDraw.push_back({commandsLate, conduit::state::shader_write, conduit::state::indirect_argument});
        list.barrier({}, toDraw);
        list.end_debug_label();
    }

    void GpuTerrain::RecordLate(conduit::command_list& list, conduit::texture_handle depth) {
        if (!prepared || !occluding)
            return;
        list.begin_debug_label("terrain occlusion", 0xFF4080C0u);
        bool built = false;
        if (depth) {
            gpu->Transition(list, depth, conduit::state::depth_read_sampled);
            conduit::bind_group_entry entries[2] = {};
            entries[0].binding = 0;
            entries[0].texture.texture = depth;
            entries[1].binding = 1;
            entries[1].buffer = {hiz, 0, 0};
            conduit::bind_group_desc bg;
            bg.layout = hizLayout;
            bg.entries = entries;
            bg.per_frame = true;
            bg.name = "depth pyramid group";
            const conduit::bind_group_handle group = gpu->Dev().create_bind_group(bg);
            if (group) {
                list.set_pipeline(hizPipeline);
                list.set_bind_group(0, group);
                uint32_t sw = depthW, sh = depthH;
                for (uint32_t level = 0; level < hizLevels; level++) {
                    const uint32_t dw = std::max(1u, (sw + 1) / 2), dh = std::max(1u, (sh + 1) / 2);
                    const HizPush push{sw, sh, dw, dh, level ? hizOffsets[level - 1] : 0u, hizOffsets[level],
                                       level == 0 ? 1u : 0u, 0};
                    list.push_constants(&push, sizeof push);
                    list.dispatch((dw + 7) / 8, (dh + 7) / 8, 1);
                    list.uav_barrier(hiz);
                    sw = dw;
                    sh = dh;
                }
                built = true;
            }
        }
        const conduit::buffer_barrier toWrite[] = {
            {counts, conduit::state::indirect_argument, conduit::state::shader_write},
        };
        list.barrier({}, toWrite);
        list.set_pipeline(cullPipeline);
        list.set_bind_group(0, groups[frameSlot]);
        // no pyramid (depth unavailable): the late pass still draws whatever the early one skipped
        const CullPush push{2u, bucketCount, built ? 1u : 0u, chunkRows};
        list.push_constants(&push, sizeof push);
        list.dispatch((highWater + 63) / 64, 1, 1);
        const conduit::buffer_barrier toDraw[] = {
            {counts, conduit::state::shader_write, conduit::state::indirect_argument},
            {commandsLate, conduit::state::shader_write, conduit::state::indirect_argument},
        };
        list.barrier({}, toDraw);
        list.end_debug_label();
    }

    void GpuTerrain::CullShadows(const float planes[6][4], uint32_t slice, uint32_t slices) {
        if (!prepared || !slices)
            return;
        std::memcpy(shadowPlanes, planes, sizeof(shadowPlanes));
        shadowFirst = static_cast<uint32_t>(uint64_t{highWater} * slice / slices);
        shadowEnd = static_cast<uint32_t>(uint64_t{highWater} * (slice + 1) / slices);
        shadowPending = true;
    }

    void GpuTerrain::ShadowDraws(RenderLayer layer, std::vector<Draw>& out) const {
        out.clear();
        const int li = LayerSlot(layer);
        if (!prepared || !shadowPending || li < 0)
            return;
        for (uint32_t b = static_cast<uint32_t>(li); b < bucketCount; b += static_cast<uint32_t>(kLayers.size())) {
            if (!bucketCap[b] || !bucketNeed[b])
                continue;
            Draw d;
            d.page = b / static_cast<uint32_t>(kLayers.size());
            d.commandOffset = uint64_t{bucketBase[b]} * kCommandBytes;
            d.maxDraws = bucketCap[b];
            d.countOffset = (uint64_t{bucketCount} * 2 + b) * 4;
            out.push_back(d);
        }
    }

    void GpuTerrain::Draws(RenderLayer layer, bool late, std::vector<Draw>& out) const {
        out.clear();
        const int li = LayerSlot(layer);
        if (!prepared || li < 0 || (late && !occluding))
            return;
        for (uint32_t b = static_cast<uint32_t>(li); b < bucketCount; b += static_cast<uint32_t>(kLayers.size())) {
            if (!bucketCap[b] || !bucketNeed[b])
                continue;
            Draw d;
            d.page = b / static_cast<uint32_t>(kLayers.size());
            d.commandOffset = uint64_t{bucketBase[b]} * kCommandBytes;
            d.maxDraws = bucketCap[b];
            d.countOffset = (uint64_t{late ? bucketCount : 0u} + b) * 4;
            out.push_back(d);
        }
    }

    GpuTerrain::Reference GpuTerrain::RunReference(const FrameInput& in, bool faces) const {
        Reference out;
        std::vector<uint32_t> bySlot(caveHigh, ~0u);
        for (const auto& [key, e] : entries) {
            if (key.lod || e.caveSlot >= bySlot.size())
                continue;
            const auto c = meshes->ChunkMap().find(key.key);
            if (c != meshes->ChunkMap().end())
                bySlot[e.caveSlot] = c->second.secVis;
        }
        const int camX = static_cast<int>(std::floor(in.camera[0] / 16.0f));
        const int camZ = static_cast<int>(std::floor(in.camera[2] / 16.0f));
        for (uint32_t i = 0; i < highWater; i++) {
            const Record& r = mirror[i];
            if (r.bucket == ~0u)
                continue;
            if (!(r.flags & kFlagLod) && in.cullRadius > 0) {
                const int dx = r.chunkX - camX, dz = r.chunkZ - camZ, reach = in.cullRadius + 1;
                if (std::abs(dx) > in.cullRadius || std::abs(dz) > in.cullRadius || dx * dx + dz * dz > reach * reach)
                    continue;
            }
            if (in.caveCulling && r.caveSlot < bySlot.size() && r.caveBit < 32 && !(bySlot[r.caveSlot] & (1u << r.caveBit)))
                continue;
            if (in.frustum.Test(r.minX, r.minY, r.minZ, r.maxX, r.maxY, r.maxZ) == Frustum::Result::Outside)
                continue;
            uint32_t first[4], count[4];
            const uint32_t mask = faces ? VisibleFaces(r, in.camera, kFaceMargin) : 0x7Fu;
            const uint32_t runs = Runs(r, mask, first, count);
            if (!runs)
                continue;
            out.sections++;
            out.commands += runs;
            for (uint32_t k = 0; k < runs; k++)
                out.indices += count[k];
        }
        return out;
    }
}
