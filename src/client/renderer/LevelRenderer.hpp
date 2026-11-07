#pragma once
#include "client/Options.hpp"
#include "client/renderer/Frustum.hpp"
#include "client/renderer/RenderContext.hpp"
#include "client/renderer/Tessellator.hpp"
#include "client/renderer/chunk/LodStore.hpp"
#include "client/renderer/block/BiomeTints.hpp"
#include "client/renderer/block/BlockLibrary.hpp"
#include "util/ThreadPool.hpp"
#include "world/level/Level.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace perlin {
    class LevelRenderer {
    public:
        LevelRenderer(RenderContext& context, const Options& options);
        ~LevelRenderer();

        void Stop();

        void ReloadAssetsIfNeeded();
        // blocks until a background asset load finishes: call before changing what it reads (the texture pack stack)
        void WaitAssetJob() {
            if (assetJob.valid())
                assetJob.wait();
        }
        void ReloadResources();
        void RemeshAll() { InvalidateMeshes(); }
        void SetGlassMode(int mode) {
            if (blocks->GlassMode() != mode) {
                const bool rebuild = blocks->NeedsGlassRebuild(mode);
                blocks->SetGlassMode(mode);
                if (rebuild)
                    ReloadResources();
                else
                    InvalidateMeshes();
            }
        }
        void SetBetterGrass(int mode) {
            if (blocks->BetterGrass() != mode) {
                blocks->SetBetterGrass(mode);
                InvalidateMeshes();
            }
        }
        void Update(Level* level, float camX, float camZ, const Frustum& frustum);
        void RebindLevel(Level* level) {
            lastLevel = level;
            lastClear = level ? level->clearCounter.load() : 0;
        }
        void Render(const Frustum& frustum, float camX, float camZ);
        void TickAnimation(uint64_t step);

        // called on the render thread right after a background load replaces the live set, for holders of Blocks()
        void OnLibrarySwap(std::function<void(const BlockLibrary*)> fn) { onLibrarySwap = std::move(fn); }
        // render thread: the live set, replaced whole when a background load finishes
        const BlockLibrary& Blocks() const { return *blocks; }
        const BiomeTints& Tints() const { return *biomeTints; }
        size_t PendingMeshes() const { return pending.size(); }
        size_t MeshesInFlight() const { return inFlight.size(); }
        size_t MaxMeshesInFlight() const { return maxInFlight; }
        size_t MeshedChunks() const { return meshKey.size(); }
        void BenchmarkMeshing(Level& level, int rounds);
        bool TexturesLoaded() const { return atlasUploaded; }
        void CollectSpecials(float camX, float camZ, float range, std::vector<SpecialBlock>& out) const;

    private:
        double RemainingMs() const;
        std::chrono::steady_clock::time_point lastUpdate{};
        std::chrono::steady_clock::time_point maintenanceDeadline{};
        double frameEma = 16.0;
        double uploadBudgetMs = 1.5;
        struct ChunkInfo {
            uint64_t version = 0;
            uint64_t borderVersion = 0;
            bool streaming = false;
            uint64_t firstSeen = 0;
            uint64_t readyAt = 0;
        };

        struct MeshResult {
            ChunkPos pos;
            uint64_t key;
            uint32_t generation;
            std::unique_ptr<Tessellator> mesh;
        };

        struct Pending {
            float distance;
            ChunkPos pos;
            uint64_t key;
        };

        using PosSet = std::unordered_set<ChunkPos, ChunkPosHash>;
        template <class T> using PosMap = std::unordered_map<ChunkPos, T, ChunkPosHash>;

        void UploadFinishedMeshes();
        void ResetAll(Level& level);
        void ApplyDirty(Level& level);
        void Recompute(const ChunkPos& pos);
        bool Ready(const ChunkPos& pos, uint64_t now) const;
        void Schedule(Level& level, float camX, float camZ, const Frustum& frustum);
        void InvalidateMeshes();
        std::chrono::steady_clock::time_point firstWaitAt;
        size_t captured = 0;
        double captureMs = 0;
        void ProcessLod(Level& level, float camX, float camZ);
        void MarkLodTile(const ChunkPos& chunk);
        void ResetLod();
        bool LodEnabled() const {
            return options.enableLods && options.renderDistance > LodStore::kRealChunks;
        }
        static int64_t TileKey(int tx, int tz) {
            return (static_cast<int64_t>(tx) << 32) | static_cast<uint32_t>(tz);
        }
        void ConfigureLighting(const std::string& packDir);
        void LoadSkyTextures();

        bool lodWasEnabled = false;
        uint32_t urgentTick = 0;
        RenderContext& context;
        const Options& options;
        // written only by the render thread, under libraryMutex; other threads read through LiveBlocks()
        std::shared_ptr<BlockLibrary> blocks = std::make_shared<BlockLibrary>();
        std::shared_ptr<BiomeTints> biomeTints = std::make_shared<BiomeTints>();
        mutable std::mutex libraryMutex;
        std::shared_ptr<BlockLibrary> LiveBlocks() const {
            std::lock_guard lock(libraryMutex);
            return blocks;
        }
        // block textures load off the render thread (cold disk: seconds), into a fresh set swapped in when done
        struct AssetLoad {
            bool blocksOk = false, tintsOk = false;
            long long blocksMs = 0, tintsMs = 0;
        };
        std::future<AssetLoad> assetJob;
        std::shared_ptr<BlockLibrary> stagedBlocks;
        // the set before the last swap stays alive one more round: pointers handed out earlier remain valid
        std::shared_ptr<BlockLibrary> retiredBlocks;
        std::shared_ptr<BiomeTints> retiredTints;
        std::function<void(const BlockLibrary*)> onLibrarySwap;
        std::shared_ptr<BiomeTints> stagedTints;
        // the live set no longer matches the packs: a reload is wanted, the old one keeps drawing meanwhile
        bool assetsStale = false;
        // bumped by every reload; a load started for an older epoch is thrown away
        uint32_t assetEpoch = 0, stagedEpoch = 0;

        std::unique_ptr<ThreadPool> meshPool;
        size_t maxInFlight = 8;
        std::atomic<bool> stopping{false};
        std::atomic<uint32_t> generation{0};

        std::mutex doneMutex;
        std::vector<MeshResult> done;
        PosSet inFlight;
        PosMap<ChunkInfo> info;
        PosMap<uint64_t> meshKey;
        PosMap<std::vector<SpecialBlock>> specials;
        PosMap<uint64_t> wantKey;
        std::vector<DirtyChunk> drained;
        std::vector<Pending> pending;

        LodStore lod;
        PosSet shown;
        std::unordered_map<int64_t, int> lodTiles;
        std::unordered_set<int64_t> lodDirty;
        std::vector<ChunkPos> capQueue;
        PosSet capSet;
        int lodScanX = 0x7fffffff, lodScanZ = 0;
        bool lodScanForced = true;
        uint64_t lodVersion = 0;
        Tessellator lodScratch;

        const Level* lastLevel = nullptr;
        uint64_t lastClear = 0;

        bool lightingConfigured = false;
        bool atlasUploaded = false;
        bool skyTexturesLoaded = false;
        bool texturePackResolved = false;
        // server packs mount after the vanilla assets loaded (during the join): their block textures need a reload
        uint32_t serverPackGeneration = 0;
        bool serverPackGenerationKnown = false, serverPackReloadPending = false;
        std::chrono::steady_clock::time_point serverPacksChangedAt;
        bool crosshairLoaded = false;
        bool tintsWereReady = false;
        int uploadedTiles = 0;
        std::chrono::steady_clock::time_point lastAssetAttempt;
    };
}
