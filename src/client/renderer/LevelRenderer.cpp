#include <cstdio>
#include "util/Env.hpp"
#include "client/renderer/LevelRenderer.hpp"
#include "client/resources/BedrockSamples.hpp"
#include "util/Resources.hpp"
#include "client/resources/TextureLoader.hpp"
#include "client/resources/ServerBlocks.hpp"
#include "client/resources/TexturePack.hpp"
#include "client/resources/ServerPacks.hpp"
#include <iostream>
#include "client/renderer/chunk/ChunkMesher.hpp"
#include "util/Paths.hpp"
#include "world/level/lighting/LightRegistry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>
#include "util/Log.hpp"

namespace perlin {
    namespace {
        constexpr auto kAssetRetry = std::chrono::seconds(1);

        std::string ReadText(const std::string& path) {
            std::ifstream file(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(file)), {});
        }

    }

    LevelRenderer::LevelRenderer(RenderContext& context, const Options& options)
        : context(context), options(options) {
        blocks->SetFancy(options.fancyGraphics);
        const unsigned cores = HardwareThreads();
        const unsigned threads = std::clamp(cores > 3 ? cores - 2 : 2u, 2u, 16u);
        meshPool = std::make_unique<ThreadPool>(threads, true);
        maxInFlight = threads * 2;
    }

    LevelRenderer::~LevelRenderer() {
        Stop();
    }

    void LevelRenderer::Stop() {
        stopping = true;
        if (meshPool)
            meshPool->Stop();
    }

    void LevelRenderer::ConfigureLighting(const std::string& packDir) {
        LightRegistry& registry = LightRegistry::Instance();
        const std::string path = packDir + "/light.json";
        std::string rules = ReadText(path);
        if (rules.empty()) {
            rules = resources::ReadText("light.json");
            if (!rules.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(packDir, ec);
                std::ofstream(path, std::ios::binary) << rules;
            }
        }
        if (!rules.empty())
            registry.LoadRules(rules);
        registry.LoadBehaviorPack(options.bedrockSamplesDir + "/repo/behavior_pack/blocks");

        registry.SetDerivedOpacity([this](uint16_t id) { return BlockLightOpacity(*LiveBlocks(), id); });
        registry.SetOcclusion([](uint16_t id, FaceOcclusion& out) { return BlockFaceOcclusion(id, out); });
        lightingConfigured = true;
    }

    void LevelRenderer::LoadSkyTextures() {
        if (skyTexturesLoaded)
            return;
        const std::string root = options.bedrockSamplesDir + "/repo/resource_pack/textures/environment/";
        Texture sun, moon, clouds;
        auto load = [&](const char* file, Texture& out) {
            for (const std::string& dir : ServerPacks::Instance().Directories())
                if (LoadTexture(dir + "/textures/environment/" + file, out) && out.width > 0)
                    return true;
            if (TexturePack::Instance().Load(std::string("textures/environment/") + file, out))
                return true;
            return LoadTexture(root + file, out);
        };
        if (!crosshairLoaded) {

            Texture cross;
            bool found = TexturePack::Instance().Load("textures/ui/cross_hair.png", cross) && cross.width > 0;
            if (!found)
                found = LoadTexture(options.bedrockSamplesDir + "/repo/resource_pack/textures/ui/cross_hair.png",
                                    cross) &&
                        cross.width > 0;
            if (!found) {
                Texture icons;
                const std::string guiRoot = options.bedrockSamplesDir + "/repo/resource_pack/textures/gui/";
                if (LoadTexture(guiRoot + "icons.png", icons) && icons.width >= 16) {
                    const int size = std::max(1, icons.width * 15 / 256);
                    cross.width = cross.height = size;
                    cross.pixels.resize(static_cast<size_t>(size) * size * 4);
                    for (int y = 0; y < size; y++)
                        std::copy_n(icons.pixels.data() + static_cast<size_t>(y) * icons.width * 4,
                                    static_cast<size_t>(size) * 4,
                                    cross.pixels.data() + static_cast<size_t>(y) * size * 4);
                    found = true;
                }
            }
            if (found) {
                context.UploadCrosshair(cross);
                crosshairLoaded = true;
            }
        }
        if (!load("sun.png", sun) || !load("moon_phases.png", moon) || !load("clouds.png", clouds))
            return;

        unsigned char lowest = 255, highest = 0;
        for (size_t i = 3; i < clouds.pixels.size(); i += 4) {
            lowest = std::min(lowest, clouds.pixels[i]);
            highest = std::max(highest, clouds.pixels[i]);
        }
        const bool byAlpha = lowest != highest;
        for (size_t i = 0; i < clouds.pixels.size(); i += 4) {
            const unsigned char mask = byAlpha ? clouds.pixels[i + 3] : clouds.pixels[i];
            clouds.pixels[i] = clouds.pixels[i + 1] = clouds.pixels[i + 2] = 255;
            clouds.pixels[i + 3] = mask >= 10 ? 255 : 0;
        }

        context.UploadSkyTextures(sun, moon, clouds);
        Texture endSky;
        if (load("end_sky.png", endSky))
            context.UploadEndSkyTexture(endSky);
        skyTexturesLoaded = true;
    }

    void LevelRenderer::InvalidateMeshes() {
        LightRegistry::Instance().Invalidate();
        MesherInvalidateCaches();
        meshKey.clear();
        inFlight.clear();
        generation++;
        {
            std::lock_guard lock(doneMutex);
            done.clear();
        }
        context.ClearChunkMeshes();
        shown.clear();
        lod.InvalidateColours();
        lodTiles.clear();
        lodDirty.clear();
        lodScanForced = true;
        for (const auto& entry : info)
            if (LodEnabled() && !entry.second.streaming && capSet.insert(entry.first).second)
                capQueue.push_back(entry.first);
        wantKey.clear();
        for (const auto& entry : info)
            Recompute(entry.first);
    }

    bool LevelRenderer::AssetsCurrent() const {
        return !assetsStale && !assetJob.valid() && !serverPackReloadPending &&
               builtForPacks == ServerPacks::Instance().Generation();
    }

    void LevelRenderer::ReloadResources() {

        assetsStale = true;
        assetEpoch++;
        ServerBlocks::Instance().Requeue();
        texturePackResolved = false;
        lastAssetAttempt = {};
        crosshairLoaded = false;
        skyTexturesLoaded = false;
    }

    void LevelRenderer::ReloadAssetsIfNeeded() {
        if (const uint32_t packs = ServerPacks::Instance().Generation(); !serverPackGenerationKnown) {
            serverPackGeneration = packs;
            serverPackGenerationKnown = true;
        } else if (packs != serverPackGeneration) {
            serverPackGeneration = packs;
            serverPacksChangedAt = std::chrono::steady_clock::now();
            serverPackReloadPending = true;
        }

        if (serverPackReloadPending && !ServerPacks::Instance().Mounting() &&
            std::chrono::steady_clock::now() - serverPacksChangedAt > std::chrono::milliseconds(300)) {
            serverPackReloadPending = false;
            Log("packs", "server packs changed: reloading block textures");
            ReloadResources();
        }
        const bool complete =
            !assetsStale && !assetJob.valid() && atlasUploaded && blocks->Complete() && biomeTints->Ready();
        if (complete) {

            const auto now = std::chrono::steady_clock::now();

            const bool packsSettling = serverPackReloadPending || ServerPacks::Instance().Mounting();
            if (!packsSettling && ServerBlocks::Instance().Pending() &&
                (fastBuild || now - lastCustomBuild >= std::chrono::milliseconds(60))) {
                lastCustomBuild = now;
                if (blocks->ApplyCustom(options.bedrockSamplesDir + "/repo/resource_pack", fastBuild ? 40.0 : 6.0) &&
                    blocks->TilesLoaded() > uploadedTiles) {
                    const int tiles = blocks->TilesLoaded();
                    if (!context.AppendAtlasTiles(blocks->AtlasPixels().data(), BlockLibrary::TilePx(), uploadedTiles,
                                                  tiles))
                        context.UploadAtlas(blocks->AtlasPixels().data(), BlockLibrary::TilePx(), tiles);
                    uploadedTiles = tiles;
                }
            }
            RemeshWaiting();
            return;
        }

        const std::string packDir = options.resourcePackDir + "/Perlin";
        const std::string vanillaDir = options.bedrockSamplesDir + "/repo/resource_pack";
        bool blocksOk = false, tintsOk = false;
        if (assetJob.valid()) {
            if (assetJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            const AssetLoad result = assetJob.get();
            Log("startup", "asset pass: blocks {} ms, tints {} ms (in the background)", result.blocksMs,
                result.tintsMs);
            if (PERLIN_ENV("PERLIN_PREVIEW"))
                Log("preview", "blocksOk={} tintsOk={}", result.blocksOk, result.tintsOk);

            if (stagedEpoch != assetEpoch || !result.blocksOk) {
                if (stagedEpoch != assetEpoch) {
                    stagedBlocks.reset();
                    stagedTints.reset();
                }
                return;
            }
            {
                std::lock_guard lock(libraryMutex);
                retiredBlocks = std::exchange(blocks, std::move(stagedBlocks));
                retiredTints = std::exchange(biomeTints, std::move(stagedTints));
            }
            builtForPacks = stagedPacks;

            ServerBlocks::Instance().TakeWaitingChunks();
            if (onLibrarySwap)
                onLibrarySwap(blocks.get());
            assetsStale = false;
            atlasUploaded = false;
            tintsWereReady = false;
            uploadedTiles = 0;
            blocksOk = true;
            tintsOk = result.tintsOk;
        } else if (!assetsStale && blocks->Complete()) {

            blocksOk = true;
            tintsOk = biomeTints->EnsureLoaded(vanillaDir);
        } else {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastAssetAttempt < kAssetRetry)
                return;
            lastAssetAttempt = now;
            if (!BedrockSamples::Available(options.bedrockSamplesDir)) {
                if (PERLIN_ENV("PERLIN_PREVIEW"))
                    Log("preview", "samples not available at {}", options.bedrockSamplesDir);
                return;
            }
            if (!texturePackResolved) {
                texturePackResolved = true;
                std::string error;
                TexturePack::Instance().OpenStack(paths::DataDir() + "/texturepacks", options.texturePacks,
                                                  error);
                if (!error.empty())
                    Log("textures", "{} (using default textures)", error);
                BlockLibrary::SetMaxTiles(context.MaxAtlasLayers(BlockLibrary::TilePx()));
                stagedBlocks.reset();
            }
            LoadSkyTextures();
            if (!stagedBlocks || stagedEpoch != assetEpoch) {
                stagedBlocks = std::make_shared<BlockLibrary>();
                stagedBlocks->SetFancy(options.fancyGraphics);
                stagedBlocks->SetTexturePack(TexturePack::Instance().Active());
                stagedBlocks->SetGlassMode(blocks->GlassMode());
                stagedBlocks->SetBetterGrass(blocks->BetterGrass());
                stagedTints = std::make_shared<BiomeTints>();
                stagedEpoch = assetEpoch;
                stagedPacks = ServerPacks::Instance().Generation();
            }

            assetJob = std::async(std::launch::async, [lib = stagedBlocks, tints = stagedTints, packDir, vanillaDir] {
                AssetLoad r;
                const auto t0 = std::chrono::steady_clock::now();
                r.blocksOk = lib->EnsureLoaded(packDir, vanillaDir);

                if (r.blocksOk)
                    lib->ApplyCustom(vanillaDir);
                const auto t1 = std::chrono::steady_clock::now();
                r.tintsOk = r.blocksOk && tints->EnsureLoaded(vanillaDir);
                const auto t2 = std::chrono::steady_clock::now();
                r.blocksMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                r.tintsMs = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
                return r;
            });
            return;
        }
        if (!blocksOk)
            return;
        if (!lightingConfigured)
            ConfigureLighting(packDir);

        const bool changed =
            !atlasUploaded || blocks->TilesLoaded() > uploadedTiles || (tintsOk && !tintsWereReady);
        if (!changed)
            return;

        tintsWereReady = tintsOk;
        uploadedTiles = blocks->TilesLoaded();
        context.UploadAtlas(blocks->AtlasPixels().data(), BlockLibrary::TilePx(), blocks->TilesLoaded());
        atlasUploaded = true;
        InvalidateMeshes();
    }

    void LevelRenderer::UploadFinishedMeshes() {
        std::vector<MeshResult> finished;
        {
            std::lock_guard lock(doneMutex);
            finished.swap(done);
        }
        const uint32_t current = generation.load();
        const auto uploadStart = std::chrono::steady_clock::now();
        size_t consumed = 0;
        for (MeshResult& r : finished) {
            if (consumed > 0 && std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - uploadStart)
                                        .count() > uploadBudgetMs)
                break;
            consumed++;
            inFlight.erase(r.pos);
            if (!meshKey.contains(r.pos))
                continue;
            if (r.generation == current) {
                context.UpdateChunkMesh(r.pos.x, r.pos.z, *r.mesh, r.key);
                if (r.mesh->Specials().empty())
                    specials.erase(r.pos);
                else
                    specials[r.pos] = std::move(r.mesh->Specials());
                if (shown.insert(r.pos).second)
                    MarkLodTile(r.pos);
            }
        }
        if (consumed < finished.size()) {
            std::lock_guard lock(doneMutex);
            done.insert(done.begin(), std::make_move_iterator(finished.begin() + consumed),
                        std::make_move_iterator(finished.end()));
        }
    }

    void LevelRenderer::ResetAll(Level& level) {
        for (const auto& entry : meshKey)
            context.RemoveChunkMesh(entry.first.x, entry.first.z);
        generation++;
        {
            std::lock_guard lock(doneMutex);
            done.clear();
        }
        info.clear();
        specials.clear();
        meshKey.clear();
        wantKey.clear();
        inFlight.clear();
        ResetLod();
        level.MarkAllDirty();
    }

    void LevelRenderer::RemeshWaiting() {
        const auto chunks = ServerBlocks::Instance().TakeWaitingChunks();
        if (chunks.empty())
            return;
        MesherInvalidateCaches();
        PosSet targets;

        for (const auto& [cx, cz] : chunks)
            for (int dz = -1; dz <= 1; dz++)
                for (int dx = -1; dx <= 1; dx++)
                    targets.insert(ChunkPos{cx + dx, cz + dz});
        for (const ChunkPos& pos : inFlight)
            targets.insert(pos);
        for (const ChunkPos& pos : targets) {

            if (meshKey.erase(pos))
                remeshOutstanding.insert(pos);
            Recompute(pos);
        }
    }

    bool LevelRenderer::ServerBlocksSettled() {
        if (ServerBlocks::Instance().Pending() || ServerBlocks::Instance().HasWaiting())
            return false;
        for (auto it = remeshOutstanding.begin(); it != remeshOutstanding.end();) {
            if (inFlight.contains(*it) || wantKey.contains(*it))
                ++it;
            else
                it = remeshOutstanding.erase(it);
        }
        return remeshOutstanding.empty();
    }

    void LevelRenderer::Recompute(const ChunkPos& pos) {
        const auto self = info.find(pos);
        if (self == info.end()) {
            wantKey.erase(pos);
            return;
        }
        uint64_t key = 1469598103934665603ull;
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++) {
                const auto n = info.find(ChunkPos{pos.x + dx, pos.z + dz});
                key = (key ^ (n == info.end()
                                  ? 0
                                  : (dx == 0 && dz == 0 ? n->second.version : n->second.borderVersion) + 1)) *
                      1099511628211ull;
            }
        const auto built = meshKey.find(pos);
        if (built != meshKey.end() && built->second == key)
            wantKey.erase(pos);
        else
            wantKey[pos] = key;
    }

    void LevelRenderer::ApplyDirty(Level& level) {
        drained.clear();
        level.DrainDirty(drained);
        if (drained.empty())
            return;
        PosSet touched;
        touched.reserve(drained.size() * 3);
        for (const DirtyChunk& d : drained) {
            if (d.exists && d.lodOnly) {
                if (LodEnabled() && !d.streaming && capSet.insert(d.pos).second)
                    capQueue.push_back(d.pos);
                continue;
            }
            if (d.exists) {
                info[d.pos] = {d.version, d.borderVersion, d.streaming, d.firstSeen, d.readyAt};
                if (LodEnabled() && !d.streaming && capSet.insert(d.pos).second)
                    capQueue.push_back(d.pos);
            } else {
                info.erase(d.pos);
                specials.erase(d.pos);
                if (meshKey.erase(d.pos))
                    context.RemoveChunkMesh(d.pos.x, d.pos.z);
                wantKey.erase(d.pos);
                if (shown.erase(d.pos))
                    MarkLodTile(d.pos);
            }
            for (int dz = -1; dz <= 1; dz++)
                for (int dx = -1; dx <= 1; dx++)
                    touched.insert(ChunkPos{d.pos.x + dx, d.pos.z + dz});
        }
        for (const ChunkPos& pos : touched)
            Recompute(pos);
    }

    bool LevelRenderer::Ready(const ChunkPos& pos, uint64_t now) const {
        const ChunkInfo& self = info.at(pos);
        if (self.streaming)
            return now - self.firstSeen > 3000;
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++) {
                if (!dx && !dz)
                    continue;
                const auto n = info.find(ChunkPos{pos.x + dx, pos.z + dz});
                if (n == info.end()) {
                    if (now - self.readyAt < 200)
                        return false;
                } else if (n->second.streaming && now - n->second.firstSeen < 900) {
                    return false;
                }
            }
        return true;
    }

    void LevelRenderer::Schedule(Level& level, float camX, float camZ, const Frustum& frustum) {
        if (wantKey.empty() || !AssetsCurrent())
            return;
        const bool async = atlasUploaded && blocks->Complete() && biomeTints->Ready();
        size_t freeSlots = async ? (maxInFlight > inFlight.size() ? maxInFlight - inFlight.size() : 0) : 64;
        bool urgentOnly = false;
        if (!freeSlots && async) {
            if ((++urgentTick & 3) != 0)
                return;
            const size_t urgentLimit = maxInFlight + 3;
            if (inFlight.size() >= urgentLimit)
                return;
            freeSlots = urgentLimit - inFlight.size();
            urgentOnly = true;
        }

        const uint64_t now = Level::NowMs();
        pending.clear();
        pending.reserve(wantKey.size());
        static const int debugNear =
            PERLIN_ENV("PERLIN_LOD_NEAR") ? std::atoi(PERLIN_ENV("PERLIN_LOD_NEAR")) : -1;
        const int meshRadius =
            std::max(3, std::min(options.renderDistance, static_cast<int>(LodStore::kRealChunks))) + 3;
        const int camChunkX = static_cast<int>(std::floor(camX / 16.0f)),
                  camChunkZ = static_cast<int>(std::floor(camZ / 16.0f));
        for (const auto& [pos, key] : wantKey) {
            if (debugNear >= 0 &&
                std::max(std::abs(pos.x - camChunkX), std::abs(pos.z - camChunkZ)) > debugNear)
                continue;
            if (std::max(std::abs(pos.x - camChunkX), std::abs(pos.z - camChunkZ)) > meshRadius)
                continue;
            if (inFlight.contains(pos) || !Ready(pos, now))
                continue;
            const float dx = pos.x * 16.0f + 8.0f - camX;
            const float dz = pos.z * 16.0f + 8.0f - camZ;
            float distance = dx * dx + dz * dz;
            if (urgentOnly && distance > 1600.0f)
                continue;
            const float x0 = pos.x * 16.0f, z0 = pos.z * 16.0f;
            if (!frustum.Visible(x0, static_cast<float>(Chunk::MIN_Y), z0, x0 + 16.0f,
                                 static_cast<float>(Chunk::MIN_Y + Chunk::HEIGHT), z0 + 16.0f))
                distance += 1e9f;
            pending.push_back({distance, pos, key});
        }
        if (pending.empty())
            return;
        const size_t take = std::min(freeSlots, pending.size());
        std::partial_sort(pending.begin(), pending.begin() + take, pending.end(),
                          [](const Pending& a, const Pending& b) { return a.distance < b.distance; });

        if (!async) {
            const auto waitNow = std::chrono::steady_clock::now();
            if (firstWaitAt.time_since_epoch().count() == 0)
                firstWaitAt = waitNow;
            if (waitNow - firstWaitAt < std::chrono::seconds(20))
                return;
            const auto start = std::chrono::steady_clock::now();
            for (size_t i = 0; i < take; i++) {
                if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(6))
                    break;
                const Pending& job = pending[i];
                meshKey[job.pos] = job.key;
                wantKey.erase(job.pos);
                Tessellator mesh;
                mesh.Begin();
                BuildChunkMesh(level, job.pos, mesh, blocks.get(), biomeTints.get());
                mesh.End();
                context.UpdateChunkMesh(job.pos.x, job.pos.z, mesh, job.key);
            }
            return;
        }

        for (size_t i = 0; i < take; i++) {
            const Pending job = pending[i];
            meshKey[job.pos] = job.key;
            wantKey.erase(job.pos);
            inFlight.insert(job.pos);

            const uint32_t gen = generation.load();
            Level* target = &level;

            meshPool->Submit([this, target, job, gen, lib = blocks, tints = biomeTints] {
                if (stopping)
                    return;
                auto mesh = std::make_unique<Tessellator>();
                const auto t0 = std::chrono::steady_clock::now();
                mesh->Begin();
                BuildChunkMesh(*target, job.pos, *mesh, lib.get(), tints.get());
                mesh->End();
                context.PrepareChunkMesh(*mesh);
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - t0)
                                    .count();
                if (ms > 60)
                    std::printf("[perf] slow chunk mesh (%d,%d): %lld ms, %zu vertices\n", job.pos.x,
                                job.pos.z, static_cast<long long>(ms), mesh->Vertices().size());
                std::lock_guard lock(doneMutex);
                done.push_back({job.pos, job.key, gen, std::move(mesh)});
            });
        }
    }

    namespace {
        int LodStride(int tx, int tz, int ccx, int ccz) {
            const int d = std::max(std::abs(tx * LodStore::kTileChunks + 2 - ccx),
                                   std::abs(tz * LodStore::kTileChunks + 2 - ccz));

            return d > 256 ? 8 : d > 128 ? 4 : d > 64 ? 2 : 1;
        }
    }

    void LevelRenderer::ResetLod() {
        lod.Clear();
        shown.clear();
        lodTiles.clear();
        lodDirty.clear();
        capQueue.clear();
        capSet.clear();
        lodScanForced = true;
    }

    void LevelRenderer::MarkLodTile(const ChunkPos& chunk) {
        if (lod.Find(chunk))
            lodDirty.insert(TileKey(LodStore::TileOf(chunk.x), LodStore::TileOf(chunk.z)));
    }

    void LevelRenderer::ProcessLod(Level& level, float camX, float camZ) {
        if (!LodEnabled())
            return;
        const bool assets = AssetsCurrent() && atlasUploaded && blocks->Complete() && biomeTints->Ready();
        if (!assets)
            return;
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        auto elapsed = [&] {
            return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        };

        size_t taken = 0;
        const double captureBudget = std::max(capQueue.size() > 200 ? 0.5 : 0.25, RemainingMs() * 0.5);
        while (taken < capQueue.size() && elapsed() < captureBudget) {
            const ChunkPos pos = capQueue[taken++];
            capSet.erase(pos);
            captured++;
            if (lod.Capture(level, pos, *blocks, *biomeTints))
                MarkLodTile(pos);
            level.RemoveLodOnly(pos.x, pos.z);
        }
        capQueue.erase(capQueue.begin(), capQueue.begin() + static_cast<std::ptrdiff_t>(taken));
        if (taken && PERLIN_ENV("PERLIN_STATS")) {
            captureMs += elapsed();
            if (captured >= 100) {
                std::printf("[lod] %zu captures, %.3f ms each\n", captured,
                            captureMs / static_cast<double>(captured));
                captured = 0;
                captureMs = 0;
            }
        }

        const int radius = std::max(2, options.renderDistance);
        const int ccx = static_cast<int>(std::floor(camX / 16.0f)),
                  ccz = static_cast<int>(std::floor(camZ / 16.0f));
        const int scanStep = radius > 128 ? 4 : 2;
        if (lodScanForced || std::abs(ccx - lodScanX) >= scanStep || std::abs(ccz - lodScanZ) >= scanStep) {
            lodScanForced = false;
            lodScanX = ccx;
            lodScanZ = ccz;
            std::vector<ChunkPos> evicted;
            lod.Evict(ccx, ccz, radius + 6, evicted);
            for (const ChunkPos& p : evicted)
                lodDirty.insert(TileKey(LodStore::TileOf(p.x), LodStore::TileOf(p.z)));
            std::unordered_map<int64_t, int> wanted;

            for (const auto& [key, count] : lod.Tiles()) {
                const int tx = static_cast<int>(key >> 32), tz = static_cast<int>(static_cast<uint32_t>(key));
                const int a = tx * LodStore::kTileChunks, b = a + LodStore::kTileChunks - 1;
                const int c0 = tz * LodStore::kTileChunks, c1 = c0 + LodStore::kTileChunks - 1;
                const int nearest = std::max(a > ccx ? a - ccx : (b < ccx ? ccx - b : 0),
                                             c0 > ccz ? c0 - ccz : (c1 < ccz ? ccz - c1 : 0));
                const int farthest = std::max(std::max(std::abs(a - ccx), std::abs(b - ccx)),
                                              std::max(std::abs(c0 - ccz), std::abs(c1 - ccz)));
                if (nearest > radius + 2 || farthest <= LodStore::kRealChunks)
                    continue;
                wanted[key] = LodStride(tx, tz, ccx, ccz);
            }
            for (const auto& [key, stride] : wanted) {
                const int tx = static_cast<int>(key >> 32), tz = static_cast<int>(static_cast<uint32_t>(key));
                auto span = [](int tile, int centre, int& lo, int& hi) {
                    const int a = tile * LodStore::kTileChunks, b = a + LodStore::kTileChunks - 1;
                    lo = (centre < a) ? a - centre : (centre > b ? centre - b : 0);
                    hi = std::max(std::abs(a - centre), std::abs(b - centre));
                };
                int xl, xh, zl, zh;
                span(tx, ccx, xl, xh);
                span(tz, ccz, zl, zh);
                if (std::max(xl, zl) <= LodStore::kRealChunks + 3 &&
                    std::max(xh, zh) >= LodStore::kRealChunks - 3 && lodTiles.contains(key) &&
                    lodTiles[key] != 0)
                    lodDirty.insert(key);
            }
            for (auto it = lodTiles.begin(); it != lodTiles.end();) {
                if (!wanted.contains(it->first)) {
                    context.RemoveLodTile(static_cast<int>(it->first >> 32),
                                          static_cast<int>(static_cast<uint32_t>(it->first)));
                    lodDirty.erase(it->first);
                    it = lodTiles.erase(it);
                } else {
                    ++it;
                }
            }
            for (const auto& [key, stride] : wanted) {
                const auto built = lodTiles.find(key);
                if (built == lodTiles.end()) {
                    lodTiles[key] = 0;
                    lodDirty.insert(key);
                } else if (built->second != 0 && built->second != stride) {
                    lodDirty.insert(key);
                }
            }
        }

        if (lodDirty.empty())
            return;
        std::vector<std::pair<float, int64_t>> order;
        order.reserve(lodDirty.size());
        for (int64_t key : lodDirty) {
            const int tx = static_cast<int>(key >> 32), tz = static_cast<int>(static_cast<uint32_t>(key));
            const float dx = tx * 64.0f + 32.0f - camX, dz = tz * 64.0f + 32.0f - camZ;
            order.emplace_back(dx * dx + dz * dz, key);
        }
        const size_t budget = std::min<size_t>(order.size(), 192);
        std::partial_sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(budget), order.end());
        for (size_t i = 0; i < budget && (i == 0 || RemainingMs() > 0.0); i++) {
            const int64_t key = order[i].second;
            lodDirty.erase(key);
            const int tx = static_cast<int>(key >> 32), tz = static_cast<int>(static_cast<uint32_t>(key));
            const int stride = LodStride(tx, tz, lodScanX, lodScanZ);
            lodTiles[key] = stride;
            const bool drawn = lod.BuildTile(
                tx, tz, stride,
                [&](const ChunkPos& p) {
                    return shown.contains(p) || std::max(std::abs(p.x - lodScanX),
                                                         std::abs(p.z - lodScanZ)) <= LodStore::kRealChunks;
                },
                lodScratch);
            if (drawn)
                context.UpdateLodTile(tx, tz, LodStore::kTileChunks * 16, lodScratch, ++lodVersion);
            else
                context.RemoveLodTile(tx, tz);
        }
    }

    double LevelRenderer::RemainingMs() const {
        return std::chrono::duration<double, std::milli>(maintenanceDeadline - std::chrono::steady_clock::now()).count();
    }

    void LevelRenderer::Update(Level* level, float camX, float camZ, const Frustum& frustum) {
        if (!level)
            return;
        {
            const auto now = std::chrono::steady_clock::now();
            if (lastUpdate.time_since_epoch().count() != 0) {
                const double dt = std::chrono::duration<double, std::milli>(now - lastUpdate).count();
                if (dt > 0 && dt < 250)
                    frameEma = frameEma * 0.9 + dt * 0.1;
            }
            lastUpdate = now;
            static const double scale = [] {
                const char* v = PERLIN_ENV("PERLIN_WORLD_BUDGET");
                return v ? std::max(0.02, std::atof(v)) : 0.16;
            }();
            const double budget = std::clamp(frameEma * scale, 0.6, 3.0);
            maintenanceDeadline = now + std::chrono::microseconds(static_cast<int64_t>(budget * 1000.0));
            uploadBudgetMs = std::max(0.4, budget * 0.55);
        }

        UploadFinishedMeshes();

        if (level != lastLevel || level->clearCounter.load() != lastClear) {
            lastLevel = level;
            lastClear = level->clearCounter.load();
            ResetAll(*level);
            context.ClearChunkMeshes();
        }
        ApplyDirty(*level);
        {
            const bool lodNow = LodEnabled();
            if (lodNow != lodWasEnabled) {
                lodWasEnabled = lodNow;
                if (lodNow) {
                    for (const auto& entry : info)
                        if (!entry.second.streaming && capSet.insert(entry.first).second)
                            capQueue.push_back(entry.first);
                    lodScanForced = true;
                } else {
                    for (const auto& tile : lodTiles)
                        context.RemoveLodTile(static_cast<int>(tile.first >> 32),
                                              static_cast<int>(static_cast<uint32_t>(tile.first)));
                    lodTiles.clear();
                    lodDirty.clear();
                    lod.Clear();
                    capQueue.clear();
                    capSet.clear();
                }
            }
        }
        Schedule(*level, camX, camZ, frustum);
        ProcessLod(*level, camX, camZ);
    }

    void LevelRenderer::CollectSpecials(float camX, float camZ, float range,
                                        std::vector<SpecialBlock>& out) const {
        const int reach = static_cast<int>(std::ceil(range / 16.0f)) + 1;
        const int cx = static_cast<int>(std::floor(camX / 16.0f)),
                  cz = static_cast<int>(std::floor(camZ / 16.0f));
        for (int dz = -reach; dz <= reach; dz++)
            for (int dx = -reach; dx <= reach; dx++) {
                const auto it = specials.find(ChunkPos{cx + dx, cz + dz});
                if (it != specials.end())
                    out.insert(out.end(), it->second.begin(), it->second.end());
            }
    }

    void LevelRenderer::Render(const Frustum& frustum, float camX, float camZ) {
        context.SetChunkCullRadius(
            std::max(3, std::min(options.renderDistance, static_cast<int>(LodStore::kRealChunks))) + 2);
        context.DrawChunkMeshes(frustum, camX, camZ);
    }

    void LevelRenderer::BenchmarkMeshing(Level& level, int rounds) {
        const auto start = std::chrono::steady_clock::now();
        size_t quads = 0;
        for (int r = 0; r < rounds; r++)
            for (const auto& entry : meshKey) {
                Tessellator mesh;
                mesh.Begin();
                BuildChunkMesh(level, entry.first, mesh, blocks.get(), biomeTints.get());
                mesh.End();
                quads += mesh.Vertices().size();
            }
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("[meshbench] %d rounds x %zu chunks: %.1f ms total, %.3f ms/chunk, %zu vertices\n",
                    rounds, meshKey.size(), ms,
                    ms / std::max<size_t>(1, static_cast<size_t>(rounds) * meshKey.size()), quads);
    }

    void LevelRenderer::TickAnimation(uint64_t step) {
        if (!atlasUploaded)
            return;
        blocks->PollAnimation(step,
                             [&](int tile, const uint8_t* rgba) { context.UpdateAtlasRegion(tile, rgba); });
    }
}
