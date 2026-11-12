#pragma once
#include <memory>
#include <future>
#include "client/renderer/Frustum.hpp"
#include "client/renderer/Tessellator.hpp"
#include "client/renderer/gpu/GpuCore.hpp"

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace perlin::gpu {

    class ChunkMeshStore {
    public:
        struct Section {
            float minY, maxY;
            int sy = 0;
            std::array<uint32_t, kRenderLayerCount> start{};
            std::array<uint32_t, kRenderLayerCount> count{};
            bool split = false;
            std::array<std::array<uint32_t, kFaceGroups>, kSplitLayers> faces{};
        };
        struct Chunk {
            int page = -1;
            uint32_t vOff = 0, vCount = 0;
            std::array<uint32_t, kRenderLayerCount> iOff{};
            std::array<uint32_t, kRenderLayerCount> layerCount{};
            std::vector<Section> sections;
            uint64_t version = 0;
            float minY = 0, maxY = 0;
            int cx = 0, cz = 0;
            int size = 16;
            int visMin = 0;
            std::vector<uint16_t> conn;
            uint32_t secVis = ~0u;
        };
        struct Visible {
            float distance;
            const Chunk* chunk;
            bool fullyInside;
            uint32_t secBegin = 0, secEnd = 0;
        };
        struct Run {
            uint32_t page, first, count;
        };
        struct CullRecord {
            float minX, minY, minZ, maxX, maxY, maxZ;
            uint32_t first, count;
            int32_t chunkX, chunkZ;
            uint32_t flags, pad;
        };
        struct CullBucket {
            uint32_t page, first, count;
        };
        void BuildCullRecords(RenderLayer layer, std::vector<CullRecord>& records,
                              std::vector<CullBucket>& buckets) const;
        uint64_t CaveEpoch() const { return caveEpoch; }
        uint64_t MeshEpoch() const { return meshEpoch; }

        void Init(Gpu& gpu) { this->gpu = &gpu; }
        void Shutdown();
        void Clear();

        struct Backup {
            std::vector<std::vector<uint8_t>> blobs;
        };
        Backup Save();

        void ReleaseGpu();

        bool Load(Backup&& backup);

        void Update(bool lod, int cx, int cz, int size, const Tessellator& tess, uint64_t version);
        void Remove(bool lod, int cx, int cz);
        void ReleaseEmptyPages();

        // translucentOnly: the gpu draws the opaque layers, only chunks with blended geometry need sorting here
        void Select(const Frustum& frustum, float camX, float camY, float camZ, int cullRadius,
                    bool caveCulling, double now, bool translucentOnly = false);
        void CollectRuns(RenderLayer layer, bool farToNear, std::vector<Run>& out);
        bool SectionReached(int sx, int sy, int sz, bool caveCulling) const;

        using ExtendFn = std::function<void(const Tessellator&, std::vector<uint8_t>&)>;
        void SetExtension(uint32_t bytesPerVertex, ExtendFn convert);
        uint32_t ExtensionBytes() const { return extBytes; }
        conduit::buffer_handle Extension(uint32_t page) const { return pages[page].ext.Buffer(); }

        conduit::buffer_handle Vertices(uint32_t page) const { return pages[page].vb.Buffer(); }
        conduit::buffer_handle Indices(uint32_t page, RenderLayer layer) const {
            return pages[page].ib[static_cast<size_t>(layer)].Buffer();
        }
        size_t Pages() const { return pages.size(); }
        size_t Chunks() const { return chunks.size(); }
        size_t LodTiles() const { return lods.size(); }
        const std::unordered_map<int64_t, Chunk>& ChunkMap() const { return chunks; }
        const std::unordered_map<int64_t, Chunk>& LodMap() const { return lods; }

        struct Change {
            int64_t key;
            bool lod;
        };
        // chunk keys added, replaced or removed since the last call; cleared: every chunk went away first
        void TrackChanges(bool on) {
            tracking = on;
            changes.clear();
            clearedAll = false;
        }
        void TakeChanges(std::vector<Change>& out, bool& cleared) {
            out.swap(changes);
            changes.clear();
            cleared = clearedAll;
            clearedAll = false;
        }
        static int64_t KeyOf(int cx, int cz) { return Key(cx, cz); }
        static int KeyX(int64_t key) { return static_cast<int>(key >> 32); }
        static int KeyZ(int64_t key) { return static_cast<int>(static_cast<uint32_t>(key)); }
        bool Empty() const { return chunks.empty() && lods.empty(); }
        uint64_t Generation() const { return generation; }
        uint64_t VertexBytes() const;
        uint64_t IndexBytes() const;

    private:
        struct Page {
            Arena vb;
            Arena ext;
            std::array<Arena, kRenderLayerCount> ib;
            int emptyFrames = 0;
            bool live = false;
        };
        static int64_t Key(int cx, int cz) {
            return (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cz);
        }
        int CreatePage(uint32_t vCap, const std::array<uint32_t, kRenderLayerCount>& iCap);
        // builds a page's buffers; touches nothing of the store, so it can run on another thread
        static bool BuildPage(conduit::device& dev, uint32_t vCap,
                              const std::array<uint32_t, kRenderLayerCount>& iCap, uint32_t extBytes,
                              Page& out);
        static void DestroyPage(conduit::device& dev, Page& p);
        // the next page's size and index capacities, as Alloc would make it now
        void NextPageSize(uint32_t& verts, std::array<uint32_t, kRenderLayerCount>& caps) const;

        // a page made ahead of need on a background thread (vulkan, d3d12): a big buffer's creation can take
        // tens of milliseconds there (the os commits and zeroes the memory), which used to land mid-frame
        // whenever streaming filled the last page
        struct Spare {
            Page page;
            uint32_t ext = 0;
            bool ok = false;
        };
        std::shared_ptr<Spare> spare;
        std::future<void> spareJob;
        void PrepareSpare();
        bool AdoptSpare(uint32_t v, const std::array<uint32_t, kRenderLayerCount>& need);
        void DropSpare();
        bool Alloc(uint32_t v, const std::array<uint32_t, kRenderLayerCount>& need, Chunk& out);
        void Free(Chunk& c);
        void Upload(std::unordered_map<int64_t, Chunk>& target, bool lod, int cx, int cz, int size,
                    const Tessellator& tess, uint64_t version);
        void UpdateCave(float camX, float camY, float camZ, double now);

        struct LodGroup {
            float x0 = 0, z0 = 0, x1 = 0, z1 = 0, minY = 0, maxY = 0;
            std::vector<const Chunk*> members;
        };
        void LodAdd(const Chunk& chunk);
        void LodDrop(const Chunk& chunk);
        static void Recalc(LodGroup& group);
        std::unordered_map<int64_t, LodGroup> lodGroups;

        void Note(int64_t key, bool lod) {
            if (tracking)
                changes.push_back({key, lod});
        }
        void NoteAll() {
            if (tracking) {
                changes.clear();
                clearedAll = true;
            }
        }
        bool tracking = false, clearedAll = false;
        std::vector<Change> changes;

        uint32_t extBytes = 0;
        ExtendFn extConvert;
        Gpu* gpu = nullptr;
        std::vector<Page> pages;
        std::unordered_map<int64_t, Chunk> chunks;
        // chunks with blended layers: the gpu path still sorts those on the cpu, the rest it never visits
        std::unordered_set<const Chunk*> blended;
        std::unordered_map<int64_t, Chunk> lods;
        std::vector<Visible> visible;
        std::vector<const Section*> visibleSections;
        uint64_t generation = 1;
        uint64_t caveGeneration = 0;
        uint64_t caveEpoch = 0;
        uint64_t meshEpoch = 0;
        double caveLastRun = 0.0;
        int caveCam[3] = {0, 0, 0};
    };
}
