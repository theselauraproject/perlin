#pragma once
#include "client/systems/config/Options.hpp"
#include "client/systems/gfx/Frustum.hpp"
#include "client/systems/gfx/RenderContext.hpp"
#include "client/systems/renderer/CustomSky.hpp"
#include "client/systems/gfx/Tessellator.hpp"
#include "client/systems/renderer/chunk/ChunkMesher.hpp"
#include "client/systems/blocks/BiomeTints.hpp"
#include "client/systems/blocks/BlockLibrary.hpp"
#include "util/ThreadPool.hpp"
#include "world/level/Level.hpp"

#include <cmath>
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

        void WaitAssetJob() {
            if (assetJob.valid())
                assetJob.wait();
        }
        void ReloadResources();
        void RemeshAll() { InvalidateMeshes(); }
        sky::Set& CustomSky() { return customSky; }

        void SetFastBuild(bool fast) { fastBuild = fast; }
        bool ServerBlocksSettled();
        bool AssetsBusy() const;
        struct LoadStatus {
            std::string message, detail;
            float progress = -1.0f;
        };
        LoadStatus AssetStatus() const;
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

        void OnLibrarySwap(std::function<void(const BlockLibrary*)> fn) { onLibrarySwap = std::move(fn); }

        const BlockLibrary& Blocks() const { return *blocks; }
        const BiomeTints& Tints() const { return *biomeTints; }
        size_t PendingMeshes() const { return queue.size() - std::min(queueHead, queue.size()); }
        size_t MeshesInFlight() const { return inFlight.size(); }
        size_t MaxMeshesInFlight() const { return maxInFlight; }
        size_t MeshedChunks() const { return meshKey.size(); }
        void BenchmarkMeshing(Level& level, int rounds);
        void PrintLoadProfile() const;
        bool TexturesLoaded() const { return atlasUploaded; }
        void CollectSpecials(float camX, float camZ, float range, std::vector<SpecialBlock>& out) const;

        void SetServerRadius(int chunks) { serverRadius = chunks; }
        int EffectiveDistance() const {
            int base = options.renderDistance;
            if (serverRadius > 0)
                base = std::min(base, std::max(2, serverRadius));
            if (base <= 8)
                return base;
            return std::max(8, static_cast<int>(std::lround(static_cast<double>(base) * distanceScale)));
        }

    private:
        double RemainingMs() const;
        std::chrono::steady_clock::time_point lastUpdate{};
        std::chrono::steady_clock::time_point maintenanceDeadline{};
        double frameEma = 16.0;
        double distanceScale = 1.0;
        std::chrono::steady_clock::time_point lastScaleStep{};
        double uploadBudgetMs = 1.5;
        struct FillProbeState {
            std::chrono::steady_clock::time_point start{};
            int cx = 0, cz = 0;
            double visibleMs = 0, settledMs = 0, lastLogMs = 0;
            bool remesh = false;
        } probe;
        void FillProbe(int radius, float camX, float camZ);
        std::atomic<uint64_t> probeJobs{0}, probeJobUs{0}, probeRemesh{0};
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
            std::shared_ptr<Tessellator> mesh;
        };

        struct Pending {
            float distance;
            float raw;
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
        void RemeshWaiting();
        bool fastBuild = false;
        PosSet remeshOutstanding;
        std::chrono::steady_clock::time_point lastCustomBuild{};
        std::chrono::steady_clock::time_point firstWaitAt;
        void ConfigureLighting(const std::string& packDir);
        void LoadSkyTextures();

        uint32_t urgentTick = 0;
        RenderContext& context;
        const Options& options;

        std::shared_ptr<BlockLibrary> blocks = std::make_shared<BlockLibrary>();
        std::shared_ptr<BiomeTints> biomeTints = std::make_shared<BiomeTints>();
        mutable std::mutex libraryMutex;
        std::shared_ptr<BlockLibrary> LiveBlocks() const {
            std::lock_guard lock(libraryMutex);
            return blocks;
        }

        struct AssetLoad {
            bool blocksOk = false, tintsOk = false;
            long long blocksMs = 0, tintsMs = 0;
        };
        std::future<AssetLoad> assetJob;
        std::shared_ptr<BlockLibrary> stagedBlocks;

        std::shared_ptr<BlockLibrary> retiredBlocks;
        std::shared_ptr<BiomeTints> retiredTints;
        std::chrono::steady_clock::time_point retiredAt{};
        std::function<void(const BlockLibrary*)> onLibrarySwap;
        std::shared_ptr<BiomeTints> stagedTints;

        bool assetsStale = false;

        uint32_t assetEpoch = 0, stagedEpoch = 0;

        uint32_t builtForPacks = 0, stagedPacks = 0;

        bool AssetsCurrent() const;

        std::unique_ptr<ThreadPool> meshPool;
        size_t maxInFlight = 8;
        std::atomic<bool> stopping{false};
        std::atomic<uint32_t> generation{0};

        std::mutex doneMutex;
        std::vector<MeshResult> done;

        static constexpr size_t kReuseColumns = 16;
        std::mutex reuseMutex;
        std::vector<std::pair<ChunkPos, std::shared_ptr<MeshReuse>>> reuseCache;
        std::shared_ptr<MeshReuse> TakeReuse(const ChunkPos& pos);
        void KeepReuse(const ChunkPos& pos, std::shared_ptr<MeshReuse> reuse);
        void DropReuse(const ChunkPos* pos);
        PosSet inFlight;
        PosSet shown;
        PosMap<ChunkInfo> info;
        PosMap<uint64_t> meshKey;
        PosMap<std::vector<SpecialBlock>> specials;
        PosMap<uint64_t> wantKey;
        std::vector<DirtyChunk> drained;
        std::vector<Pending> queue;
        size_t queueHead = 0;
        std::chrono::steady_clock::time_point queueBuilt{};
        int queueCamX = 0x7fffffff, queueCamZ = 0;
        int serverRadius = 0;
        bool cullLeavesApplied = false;
        int viewChunkX = 0, viewChunkZ = 0;
        bool viewKnown = false;
        std::chrono::steady_clock::time_point lastEvict{};
        void EvictFarMeshes();
        bool queueForce = true;
        void RebuildQueue(float camX, float camZ, const Frustum& frustum);
        struct LoadProfile {
            size_t rebuilds = 0, submitted = 0, uploaded = 0;
            double rebuildMs = 0, uploadMs = 0;
        } loadProfile;


        const Level* lastLevel = nullptr;
        uint64_t lastClear = 0;

        bool lightingConfigured = false;
        bool atlasUploaded = false;
        bool skyTexturesLoaded = false;
        sky::Set customSky;
        bool texturePackResolved = false;

        uint32_t serverPackGeneration = 0;
        bool serverPackGenerationKnown = false, serverPackReloadPending = false;
        std::chrono::steady_clock::time_point serverPacksChangedAt;
        bool crosshairLoaded = false;
        bool tintsWereReady = false;
        int uploadedTiles = 0;
        std::chrono::steady_clock::time_point lastAssetAttempt;
    };
}
