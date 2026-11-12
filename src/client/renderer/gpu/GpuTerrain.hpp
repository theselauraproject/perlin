#pragma once
#include "client/renderer/Frustum.hpp"
#include "client/renderer/gpu/ChunkMeshStore.hpp"
#include "client/renderer/gpu/GpuCore.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace perlin::gpu {

    // opaque terrain on the gpu: every chunk section lives in a persistent record buffer that only changes when a
    // chunk is meshed or dropped; a compute pass culls the records (frustum, render radius, caves, facing groups and
    // optionally a depth pyramid) and writes the indirect draws plus their counts. needs multi draw indirect with a
    // gpu count; without it the renderer keeps culling on the cpu
    class GpuTerrain {
    public:
        static constexpr std::array<RenderLayer, 3> kLayers = {RenderLayer::Solid, RenderLayer::Cutout,
                                                               RenderLayer::CutoutCull};
        static int LayerSlot(RenderLayer layer);
        static bool Handles(RenderLayer layer) { return LayerSlot(layer) >= 0; }

        struct Record {
            float minX, minY, minZ;
            uint32_t first;
            float maxX, maxY, maxZ;
            uint32_t bucket;
            uint32_t faces[kFaceGroups];
            uint32_t flags;
            int32_t chunkX, chunkZ;
            uint32_t caveSlot, caveBit;
        };
        static_assert(sizeof(Record) == 80, "Record must match SectionRecord in terrain_cull.slang");

        struct FrameInput {
            Frustum frustum;
            glm::mat4 clipFromWorld{1.0f};
            float camera[3] = {};
            int cullRadius = 0;
            bool caveCulling = true;
            bool occlusion = false;
            bool faceCull = true;
            uint32_t depthWidth = 0, depthHeight = 0;
        };

        struct Draw {
            uint32_t page;
            uint64_t commandOffset;
            uint32_t maxDraws;
            uint64_t countOffset;
        };

        struct Stats {
            uint32_t records = 0, liveChunks = 0, recordCapacity = 0, buckets = 0, commandCapacity = 0;
            uint32_t uploadedRecords = 0, rebuiltChunks = 0;
            bool occlusion = false;
        };

        bool Init(Gpu& gpu, ChunkMeshStore& meshes);
        void Shutdown();
        bool Ready() const { return ready; }

        // cpu side of a frame, after ChunkMeshStore::Select ran (it refreshes the cave visibility)
        bool Prepare(const FrameInput& in);
        // outside any pass, after Gpu::Flush: clears the counts and culls; early draws are ready afterwards
        void RecordEarly(conduit::command_list& list);
        // occlusion only: depth pyramid from the early pass depth, then the late cull
        void RecordLate(conduit::command_list& list, conduit::texture_handle depth);
        bool Occluding() const { return occluding; }
        // PERLIN_WORK_GRAPHS=1 on a device with work graphs: the early cull runs as a d3d12 work graph
        bool UsingWorkGraph() const { return static_cast<bool>(graphPipeline); }

        void Draws(RenderLayer layer, bool late, std::vector<Draw>& out) const;

        // shadow casters: the light's box as six inward planes (n.p + d >= 0 inside), culled over one slice of the
        // records in the same compute pass; call after Prepare, draw with ShadowDraws
        void CullShadows(const float planes[6][4], uint32_t slice, uint32_t slices);
        bool ShadowsPending() const { return shadowPending; }
        void ShadowDraws(RenderLayer layer, std::vector<Draw>& out) const;
        conduit::buffer_handle ShadowCommands() const { return commandsShadow; }
        conduit::buffer_handle Commands(bool late) const { return late ? commandsLate : commandsEarly; }
        conduit::buffer_handle Counts() const { return counts; }
        const Stats& GetStats() const { return stats; }
        const Record* RecordData() const { return mirror.data(); }
        // what the last Prepare handed the gpu, for running the shader on the cpu in tests
        struct DebugView {
            const void* params = nullptr;
            std::vector<uint32_t> buckets;
            const std::vector<uint32_t>* caveBits = nullptr;
            uint32_t bucketCount = 0, commandCapacity = 0, hizLevels = 0;
            std::array<uint32_t, 16> hizOffsets{};
        };
        DebugView Debug() const {
            DebugView d;
            d.params = paramMaps.empty() ? nullptr : paramMaps[frameSlot];
            for (uint32_t b = 0; b < bucketCount; b++) {
                d.buckets.push_back(bucketBase[b]);
                d.buckets.push_back(bucketCap[b]);
            }
            d.caveBits = &caveBits;
            d.bucketCount = bucketCount;
            d.commandCapacity = commandCapacity;
            d.hizLevels = hizLevels;
            d.hizOffsets = hizOffsets;
            return d;
        }
        uint32_t RecordCount() const { return highWater; }

        // the cull shader's mode 0 run on the cpu over the record mirror: tests and the benchmark use it to count what
        // the gpu would draw
        struct Reference {
            uint64_t indices = 0;
            uint32_t commands = 0, sections = 0;
        };
        Reference RunReference(const FrameInput& in, bool faces) const;

        // pure culling helpers, shared with the cpu reference used by tests and the benchmark
        static uint32_t VisibleFaces(const Record& r, const float camera[3], float margin);
        static uint32_t Runs(const Record& r, uint32_t mask, uint32_t first[4], uint32_t count[4]);

    private:
        struct Entry {
            uint32_t start = 0, size = 0;
            uint32_t caveSlot = ~0u;
        };
        struct Key {
            int64_t key;
            bool lod;
            bool operator==(const Key& o) const { return key == o.key && lod == o.lod; }
        };
        struct KeyHash {
            size_t operator()(const Key& k) const {
                return std::hash<int64_t>()(k.key) ^ (k.lod ? 0x9e3779b97f4a7c15ull : 0);
            }
        };

        bool CreatePipelines();
        void TryWorkGraph();
        void UploadChunkTable();
        bool EnsureRecordCapacity(uint32_t records);
        bool EnsureBuckets(uint32_t bucketCount);
        bool EnsureCaveCapacity(uint32_t slots);
        bool EnsureHiz(uint32_t w, uint32_t h);
        void RebuildGroups();
        void SyncChunks();
        void BuildRecords(const ChunkMeshStore::Chunk& g, bool lod, uint32_t caveSlot, std::vector<Record>& out) const;
        uint32_t AllocBlock(uint32_t size);
        void FreeBlock(uint32_t start, uint32_t size);
        void Release(const Key& key);
        void Place(const Key& key, const ChunkMeshStore::Chunk& g);
        void UploadRange(uint32_t first, uint32_t count);
        void Layout();
        void UploadCave();
        void FullResync();

        Gpu* gpu = nullptr;
        ChunkMeshStore* meshes = nullptr;
        bool ready = false;

        conduit::shader_handle cullShader, hizShader;
        conduit::bind_layout_handle cullLayout, hizLayout;
        conduit::pipeline_layout_handle cullPipelineLayout, hizPipelineLayout;
        conduit::pipeline_handle cullPipeline, hizPipeline, graphPipeline;
        conduit::buffer_handle chunkTable;
        uint32_t chunkTableCapacity = 0, chunkRows = 0;
        bool chunkTableDirty = true;

        conduit::buffer_handle recordBuffer, visibility, bucketBuffer, caveBuffer, commandsEarly, commandsLate, counts,
            hiz, commandsShadow;
        bool shadowPending = false;
        float shadowPlanes[6][4] = {};
        uint32_t shadowFirst = 0, shadowEnd = 0;
        std::vector<conduit::buffer_handle> paramBuffers;
        std::vector<uint8_t*> paramMaps;
        std::vector<conduit::bind_group_handle> groups;
        bool groupsDirty = true;

        uint32_t recordCapacity = 0, bucketCapacity = 0, caveCapacity = 0, commandCapacity = 0;
        uint32_t hizW = 0, hizH = 0, hizLevels = 0;
        std::array<uint32_t, 16> hizOffsets{};
        uint32_t hizFloats = 0;

        std::vector<Record> mirror;
        uint32_t highWater = 0;
        std::unordered_map<uint32_t, std::vector<uint32_t>> freeBlocks;
        std::unordered_map<Key, Entry, KeyHash> entries;
        std::vector<uint32_t> caveFree;
        uint32_t caveHigh = 0;
        std::vector<uint32_t> caveBits;
        uint64_t caveBuilt = ~0ull;
        bool resync = true;
        std::vector<std::pair<uint32_t, uint32_t>> dirtyRanges;

        uint32_t bucketCount = 0;
        std::vector<uint32_t> bucketNeed, bucketBase, bucketCap;
        bool layoutDirty = true;

        bool prepared = false, occluding = false;
        uint32_t frameSlot = 0;
        uint32_t depthW = 0, depthH = 0;
        std::vector<ChunkMeshStore::Change> changes;
        Stats stats;
    };
}
