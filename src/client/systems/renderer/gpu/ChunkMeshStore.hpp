#pragma once
#include <memory>
#include <future>
#include "client/systems/gfx/Frustum.hpp"
#include "client/systems/gfx/Tessellator.hpp"
#include "client/systems/renderer/gpu/GpuCore.hpp"

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
        struct SortCache {
            struct Range {
                uint32_t quad, quads;
            };
            std::vector<uint8_t> vertices;
            std::vector<uint8_t> ext;
            std::vector<float> centers;
            std::vector<Range> ranges;
            std::array<uint32_t, 2> layerQuads{};
            std::array<uint32_t, 2> firstVertex{};
            float x = 1e30f, y = 1e30f, z = 1e30f;
        };
        struct Chunk {
            int page = -1;
            std::shared_ptr<SortCache> sort;
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

        void Select(const Frustum& frustum, float camX, float camY, float camZ, int cullRadius,
                    bool caveCulling, double now, bool translucentOnly = false);
        // faceCamera: leave out the faces of solid and culled-cutout sections that point away from that
        // position (what the GPU culling does for the plain renderer); null keeps every face.
        void CollectRuns(RenderLayer layer, bool farToNear, std::vector<Run>& out, const float* faceCamera = nullptr);
        bool SectionReached(int sx, int sy, int sz, bool caveCulling) const;

        using ExtendFn = std::function<void(const Tessellator&, std::vector<uint8_t>&)>;
        void SetExtension(uint32_t bytesPerVertex, ExtendFn convert);
        uint32_t ExtensionBytes() const { return extBytes; }
        void SetVertexStride(uint32_t bytes);
        uint32_t VertexStride() const { return vertexStride; }
        uint64_t FormatEpoch() const { return formatEpoch; }
        conduit::buffer_handle Extension(uint32_t page) const { return pages[page].ext.Buffer(); }

        conduit::buffer_handle Vertices(uint32_t page) const { return pages[page].vb.Buffer(); }
        conduit::buffer_handle Indices(uint32_t page, RenderLayer layer) const {
            (void)page;
            (void)layer;
            return pattern.Buffer();
        }
        uint32_t PatternIndices() const { return patternQuads * 6; }
        void ResortTranslucent(float x, float y, float z);
        size_t Pages() const { return pages.size(); }
        size_t Chunks() const { return chunks.size(); }
        size_t LodTiles() const { return lods.size(); }
        const std::unordered_map<int64_t, Chunk>& ChunkMap() const { return chunks; }
        const std::unordered_map<int64_t, Chunk>& LodMap() const { return lods; }

        struct Change {
            int64_t key;
            bool lod;
        };
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
        friend struct ChunkMeshStoreTestAccess;
        struct RunCache {
            bool valid = false, farToNear = false, faceCull = false;
            std::array<float, 3> camera{};
            std::vector<Run> runs;
        };
        struct SelectionCache {
            bool valid = false, caveCulling = false, reused = false;
            uint64_t meshEpoch = 0, caveEpoch = 0;
            Frustum frustum{};
            std::array<float, 3> camera{};
            int radius = 0;
            std::vector<Visible> visible;
            std::vector<const Section*> sections;
            std::array<RunCache, kRenderLayerCount> runs;
        };
        void ClearSelections();
        void BuildRuns(const SelectionCache& selection, RenderLayer layer, bool farToNear,
                       std::vector<Run>& out, const float* faceCamera) const;
        struct Page {
            Arena vb;
            Arena ext;
            int emptyFrames = 0;
            bool live = false;
        };
        static int64_t Key(int cx, int cz) {
            return (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cz);
        }
        int CreatePage(uint32_t minVerts);
        bool EnsurePattern(uint32_t quads);
        bool SortQuads(SortCache& sc, float x, float y, float z) const;
        static bool BuildPage(conduit::device& dev, uint32_t vCap, uint32_t extBytes,
                              uint32_t vertexStride, Page& out);
        static void DestroyPage(conduit::device& dev, Page& p);
        bool Alloc(uint32_t v, Chunk& out);
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
        uint32_t vertexStride = sizeof(Vertex);
        uint64_t formatEpoch = 0;
        void DropPages();
        ExtendFn extConvert;
        Gpu* gpu = nullptr;
        std::vector<Page> pages;
        Arena pattern;
        float sortX = 0, sortY = 0, sortZ = 0;
        bool sortKnown = false;
        uint32_t patternQuads = 0;
        std::vector<std::pair<Arena, int>> retiredPatterns;
        std::unordered_map<int64_t, Chunk> chunks;
        std::unordered_set<const Chunk*> blended;
        std::unordered_map<int64_t, Chunk> lods;
        // The renderer alternates all-terrain and translucent-only queries when GPU culling or its
        // CPU fallback/statistics is used. Retain only those two CPU descriptions, never frame handles.
        std::array<SelectionCache, 2> selections;
        size_t selected = 0;
        uint64_t selectionBuilds = 0, runBuilds = 0;
        uint64_t generation = 1;
        uint64_t caveGeneration = 0;
        uint64_t caveEpoch = 0;
        uint64_t meshEpoch = 0;
        double caveLastRun = 0.0;
        int caveCam[3] = {0, 0, 0};
    };
}
