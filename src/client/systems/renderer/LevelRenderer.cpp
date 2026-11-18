#include <cstdio>
#include "util/Env.hpp"
#include "util/ThreadPool.hpp"
#include "client/systems/renderer/LevelRenderer.hpp"
#include "client/systems/renderer/gpu/SafeMode.hpp"
#include "client/systems/resources/BedrockSamples.hpp"
#include "util/Resources.hpp"
#include "util/ResourcePaths.hpp"
#include "client/systems/resources/TextureLoader.hpp"
#include "client/systems/resources/ServerBlocks.hpp"
#include "client/systems/resources/TexturePack.hpp"
#include "client/systems/resources/ServerPacks.hpp"
#include <iostream>
#include "client/systems/renderer/chunk/ChunkMesher.hpp"
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
        BlockLibrary::SetLiveTilePx(blocks->OwnTilePx());
        const unsigned cores = HardwareThreads();
        const unsigned threads = std::clamp(cores > 3 ? cores - 2 : 2u, 2u, 16u);
        meshPool = std::make_unique<ThreadPool>(threads, true, "perlin-mesh");
        maxInFlight = threads * 6;
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
            rules = resources::ReadRequiredText(resources::path::kLight);
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
        customSky = sky::Load(ServerPacks::Instance().Directories());
        context.UploadCustomSkyTextures(customSky.textures);
        for (Texture& t : customSky.textures)
            t = Texture{};
        Texture endSky;
        if (load("end_sky.png", endSky))
            context.UploadEndSkyTexture(endSky);
        skyTexturesLoaded = true;
    }

    void LevelRenderer::InvalidateMeshes(bool keepVisible) {
        LightRegistry::Instance().Invalidate();
        MesherInvalidateCaches();
        meshKey.clear();
        inFlight.clear();
        generation++;
        DropReuse(nullptr);
        {
            std::lock_guard lock(doneMutex);
            done.clear();
        }
        if (!keepVisible) {
            context.ClearChunkMeshes();
            shown.clear();
        }
        wantKey.clear();
        for (const auto& entry : info)
            Recompute(entry.first);
        queueForce = true;
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
        ServerBlocks& serverBlocks = ServerBlocks::Instance();
        if (retiredBlocks && std::chrono::steady_clock::now() - retiredAt > std::chrono::seconds(10)) {
            std::shared_ptr<BlockLibrary> oldBlocks;
            std::shared_ptr<BiomeTints> oldTints;
            {
                std::lock_guard lock(libraryMutex);
                oldBlocks = std::move(retiredBlocks);
                oldTints = std::move(retiredTints);
            }
        }
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
            if (!packsSettling && serverBlocks.Pending() &&
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
                serverBlocks.Requeue();
                lastAssetAttempt = {};
                return;
            }
            {
                std::lock_guard lock(libraryMutex);
                retiredBlocks = std::exchange(blocks, std::move(stagedBlocks));
                retiredTints = std::exchange(biomeTints, std::move(stagedTints));
                retiredAt = std::chrono::steady_clock::now();
            }
            BlockLibrary::SetLiveTilePx(blocks->OwnTilePx());
            builtForPacks = stagedPacks;

            serverBlocks.TakeWaitingChunks();
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
                serverBlocks.Requeue();
            }

            assetJob = std::async(std::launch::async, [lib = stagedBlocks, tints = stagedTints, packDir, vanillaDir] {
                LowerThreadPriority();
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
        InvalidateMeshes(true);
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
        double budgetMs = uploadBudgetMs;
        if (finished.size() > 6 && gpu::SafeModeLevel() == 0)
            budgetMs = std::max(budgetMs, std::min(frameEma * 0.45, 7.0));
        for (MeshResult& r : finished) {
            if (consumed > 0 && std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - uploadStart)
                                        .count() > budgetMs)
                break;
            consumed++;
            inFlight.erase(r.pos);
            if (!meshKey.contains(r.pos))
                continue;
            if (r.generation == current) {
                const auto u0 = std::chrono::steady_clock::now();
                context.UpdateChunkMesh(r.pos.x, r.pos.z, *r.mesh, r.key);
                shown.insert(r.pos);
                loadProfile.uploaded++;
                loadProfile.uploadMs +=
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - u0).count();
                if (r.mesh->Specials().empty())
                    specials.erase(r.pos);
                else
                    specials[r.pos] = r.mesh->Specials();

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
        DropReuse(nullptr);
        {
            std::lock_guard lock(doneMutex);
            done.clear();
        }
        info.clear();
        specials.clear();
        shown.clear();
        meshKey.clear();
        wantKey.clear();
        inFlight.clear();
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

    bool LevelRenderer::AssetsBusy() const {
        return assetsStale || assetJob.valid() || serverPackReloadPending || BlockLibrary::CustomBuildTotal() > 0;
    }

    LevelRenderer::LoadStatus LevelRenderer::AssetStatus() const {
        LoadStatus s;
        const int total = BlockLibrary::CustomBuildTotal();
        if (total > 0) {
            const int done = std::min(BlockLibrary::CustomBuildDone(), total);
            s.message = "Building the server's custom blocks";
            s.detail = std::to_string(done) + " / " + std::to_string(total);
            s.progress = static_cast<float>(done) / static_cast<float>(total);
        } else if (serverPackReloadPending) {
            s.message = "Loading the server's resource packs";
        } else if (assetJob.valid() || assetsStale) {
            s.message = "Preparing block textures and models";
        } else if (!atlasUploaded) {
            s.message = "Uploading textures";
        }
        return s;
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
        else {
            wantKey[pos] = key;
            queueForce = true;
        }
    }

    void LevelRenderer::ApplyDirty(Level& level) {
        drained.clear();
        level.DrainDirty(drained);
        if (drained.empty())
            return;
        PosSet touched;
        touched.reserve(drained.size() * 3);
        for (const DirtyChunk& d : drained) {
            if (d.exists) {
                info[d.pos] = {d.version, d.borderVersion, d.streaming, d.firstSeen, d.readyAt};
            } else {
                info.erase(d.pos);
                specials.erase(d.pos);
                if (meshKey.erase(d.pos))
                    context.RemoveChunkMesh(d.pos.x, d.pos.z);
                DropReuse(&d.pos);
                wantKey.erase(d.pos);
                shown.erase(d.pos);
            }
            const bool farRemoval = !d.exists && viewKnown &&
                                    std::max(std::abs(d.pos.x - viewChunkX), std::abs(d.pos.z - viewChunkZ)) > 2;
            if (farRemoval)
                continue;
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
                    if (now - self.readyAt < 80)
                        return false;
                } else if (n->second.streaming && now - n->second.firstSeen < 400) {
                    return false;
                }
            }
        return true;
    }

    void LevelRenderer::EvictFarMeshes() {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastEvict < std::chrono::milliseconds(750))
            return;
        lastEvict = now;
        const int keep = std::max(2, EffectiveDistance()) + 4;
        std::vector<ChunkPos> far;
        for (const auto& [pos, key] : meshKey)
            if (std::max(std::abs(pos.x - viewChunkX), std::abs(pos.z - viewChunkZ)) > keep && !inFlight.contains(pos))
                far.push_back(pos);
        for (const ChunkPos& pos : far) {
            meshKey.erase(pos);
            specials.erase(pos);
            shown.erase(pos);
            context.RemoveChunkMesh(pos.x, pos.z);
            DropReuse(&pos);
            if (info.contains(pos))
                Recompute(pos);
        }
        if (!far.empty())
            queueForce = true;
    }

    void LevelRenderer::RebuildQueue(float camX, float camZ, const Frustum& frustum) {
        const auto t0 = std::chrono::steady_clock::now();
        const uint64_t now = Level::NowMs();
        queue.clear();
        queueHead = 0;
        const int meshRadius = std::max(2, EffectiveDistance()) + 1;
        const int camChunkX = static_cast<int>(std::floor(camX / 16.0f)),
                  camChunkZ = static_cast<int>(std::floor(camZ / 16.0f));
        queueCamX = camChunkX;
        queueCamZ = camChunkZ;
        for (const auto& [pos, key] : wantKey) {
            const int ax = std::abs(pos.x - camChunkX), az = std::abs(pos.z - camChunkZ);
            if (std::max(ax, az) > meshRadius)
                continue;
            if (inFlight.contains(pos))
                continue;
            const float dx = pos.x * 16.0f + 8.0f - camX;
            const float dz = pos.z * 16.0f + 8.0f - camZ;
            const float d = dx * dx + dz * dz;
            queue.push_back({d, d, pos, key});
        }
        constexpr size_t kQueueLimit = 6144;
        if (queue.size() > kQueueLimit) {
            std::nth_element(queue.begin(), queue.begin() + kQueueLimit, queue.end(),
                             [](const Pending& a, const Pending& b) { return a.distance < b.distance; });
            queue.resize(kQueueLimit);
        }
        size_t kept = 0;
        for (size_t i = 0; i < queue.size(); i++) {
            Pending job = queue[i];
            if (!Ready(job.pos, now))
                continue;
            const float x0 = job.pos.x * 16.0f, z0 = job.pos.z * 16.0f;
            if (!frustum.Visible(x0, static_cast<float>(Chunk::MIN_Y), z0, x0 + 16.0f,
                                 static_cast<float>(Chunk::MIN_Y + Chunk::HEIGHT), z0 + 16.0f))
                job.distance += 1e9f;
            queue[kept++] = job;
        }
        queue.resize(kept);
        std::sort(queue.begin(), queue.end(),
                  [](const Pending& a, const Pending& b) { return a.distance < b.distance; });
        queueBuilt = t0;
        queueForce = false;
        loadProfile.rebuilds++;
        loadProfile.rebuildMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }

    void LevelRenderer::Schedule(Level& level, float camX, float camZ, const Frustum& frustum) {
        if (wantKey.empty() || !AssetsCurrent()) {
            queue.clear();
            queueHead = 0;
            return;
        }
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

        const auto tnow = std::chrono::steady_clock::now();
        const double sinceBuild = std::chrono::duration<double, std::milli>(tnow - queueBuilt).count();
        const bool drained = queueHead >= queue.size();
        const int camChunkX = static_cast<int>(std::floor(camX / 16.0f)),
                  camChunkZ = static_cast<int>(std::floor(camZ / 16.0f));
        const bool moved = camChunkX != queueCamX || camChunkZ != queueCamZ;
        const bool small = wantKey.size() < 4096;
        if (queueForce && (small || sinceBuild > 40.0) || (drained && sinceBuild > (small ? 0.0 : 25.0)) ||
            (moved && sinceBuild > (small ? 0.0 : 120.0)) || sinceBuild > (small ? 60.0 : 250.0))
            RebuildQueue(camX, camZ, frustum);
        if (queueHead >= queue.size())
            return;

        if (!async) {
            const auto waitNow = std::chrono::steady_clock::now();
            if (firstWaitAt.time_since_epoch().count() == 0)
                firstWaitAt = waitNow;
            if (waitNow - firstWaitAt < std::chrono::seconds(20))
                return;
            const auto start = std::chrono::steady_clock::now();
            size_t taken = 0;
            while (queueHead < queue.size() && taken < freeSlots) {
                if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(6))
                    break;
                const Pending job = queue[queueHead++];
                const auto w = wantKey.find(job.pos);
                if (w == wantKey.end() || w->second != job.key)
                    continue;
                taken++;
                meshKey[job.pos] = job.key;
                wantKey.erase(job.pos);
                Tessellator mesh;
                mesh.Begin();
                BuildChunkMesh(level, job.pos, mesh, blocks.get(), biomeTints.get());
                mesh.End();
                context.UpdateChunkMesh(job.pos.x, job.pos.z, mesh, job.key);
                shown.insert(job.pos);
            }
            return;
        }

        while (freeSlots > 0 && queueHead < queue.size()) {
            const Pending job = queue[queueHead];
            if (urgentOnly && job.raw > 1600.0f) {
                break;
            }
            queueHead++;
            const auto w = wantKey.find(job.pos);
            if (w == wantKey.end() || w->second != job.key || inFlight.contains(job.pos))
                continue;
            freeSlots--;
            const bool remesh = meshKey.contains(job.pos);
            meshKey[job.pos] = job.key;
            wantKey.erase(w);
            inFlight.insert(job.pos);
            loadProfile.submitted++;

            const uint32_t gen = generation.load();
            Level* target = &level;

            meshPool->Submit([this, target, job, gen, remesh, lib = blocks, tints = biomeTints] {
                if (stopping)
                    return;
                auto mesh = std::make_shared<Tessellator>();
                std::shared_ptr<MeshReuse> reuse = remesh ? TakeReuse(job.pos) : nullptr;
                if (reuse)
                    reuse->generation = gen;
                const auto t0 = std::chrono::steady_clock::now();
                mesh->Begin();
                BuildChunkMesh(*target, job.pos, *mesh, lib.get(), tints.get(), reuse.get());
                mesh->End();
                mesh->ComputeBounds();
                context.PrepareChunkMesh(*mesh);
                if (reuse && generation.load() == gen) {
                    reuse->mesh = mesh;
                    KeepReuse(job.pos, std::move(reuse));
                }
                const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::steady_clock::now() - t0)
                                    .count();
                const long long ms = us / 1000;
                probeJobs++;
                probeJobUs += static_cast<uint64_t>(us);
                if (remesh)
                    probeRemesh++;
                if (ms > 60)
                    std::printf("[perf] slow chunk mesh (%d,%d): %lld ms, %zu vertices\n", job.pos.x,
                                job.pos.z, ms, mesh->Vertices().size());
                std::lock_guard lock(doneMutex);
                done.push_back({job.pos, job.key, gen, std::move(mesh)});
            });
        }
    }

    void LevelRenderer::PrintLoadProfile() const {
        std::printf("[loadprof] submitted %zu, uploaded %zu (%.3f ms each), queue rebuilds %zu (%.3f ms each)\n",
                    loadProfile.submitted, loadProfile.uploaded,
                    loadProfile.uploadMs / std::max<size_t>(1, loadProfile.uploaded), loadProfile.rebuilds,
                    loadProfile.rebuildMs / std::max<size_t>(1, loadProfile.rebuilds));
    }

    double LevelRenderer::RemainingMs() const {
        return std::chrono::duration<double, std::milli>(maintenanceDeadline - std::chrono::steady_clock::now()).count();
    }

    void LevelRenderer::FillProbe(int radius, float camX, float camZ) {
        const auto now = std::chrono::steady_clock::now();
        const int ccx = static_cast<int>(std::floor(camX / 16.0f)), ccz = static_cast<int>(std::floor(camZ / 16.0f));
        if (info.empty() || probe.start.time_since_epoch().count() == 0 ||
            std::max(std::abs(ccx - probe.cx), std::abs(ccz - probe.cz)) > radius) {
            probe = {};
            probe.start = now;
            probe.cx = ccx;
            probe.cz = ccz;
        }
        int total = 0, visible = 0, settled = 0;
        for (int dz = -radius; dz <= radius; dz++)
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx * dx + dz * dz > radius * radius)
                    continue;
                const ChunkPos p{ccx + dx, ccz + dz};
                total++;
                if (!shown.contains(p))
                    continue;
                visible++;
                if (!wantKey.contains(p) && !inFlight.contains(p))
                    settled++;
            }
        const double ms = std::chrono::duration<double, std::milli>(now - probe.start).count();
        if (!probe.visibleMs && visible == total)
            probe.visibleMs = ms;
        if (!probe.settledMs && settled == total) {
            probe.settledMs = ms;
            std::printf("[fill] r=%d %d columns: all visible %.0f ms, all settled %.0f ms%s\n", radius, total,
                        probe.visibleMs, probe.settledMs, probe.remesh ? " (remesh)" : "");
            std::fflush(stdout);
            static int rounds = PERLIN_ENV("PERLIN_FILL_REMESH") ? std::atoi(PERLIN_ENV("PERLIN_FILL_REMESH")) : 0;
            if (rounds > 0) {
                rounds--;
                InvalidateMeshes();
                probe.start = std::chrono::steady_clock::now();
                probe.visibleMs = probe.settledMs = probe.lastLogMs = 0;
                probe.remesh = true;
            }
        }
        if (!probe.settledMs && ms - probe.lastLogMs >= 500.0) {
            probe.lastLogMs = ms;
            size_t finished = 0;
            {
                std::lock_guard lock(doneMutex);
                finished = done.size();
            }
            const uint64_t jobs = probeJobs.load();
            int unknown = 0, streaming = 0, gapWait = 0, nbrStreaming = 0;
            const uint64_t nowMs = Level::NowMs();
            for (int dz = -radius; dz <= radius; dz++)
                for (int dx = -radius; dx <= radius; dx++) {
                    const ChunkPos p{ccx + dx, ccz + dz};
                    if (dx * dx + dz * dz > radius * radius || shown.contains(p) || inFlight.contains(p))
                        continue;
                    const auto self = info.find(p);
                    if (self == info.end()) {
                        unknown++;
                        continue;
                    }
                    if (self->second.streaming) {
                        streaming++;
                        continue;
                    }
                    for (int ny = -1; ny <= 1; ny++)
                        for (int nx = -1; nx <= 1; nx++) {
                            const auto n = info.find(ChunkPos{p.x + nx, p.z + ny});
                            if (n == info.end() && nowMs - self->second.readyAt < 80) {
                                gapWait++;
                                nx = ny = 2;
                            } else if (n != info.end() && n->second.streaming && nowMs - n->second.firstSeen < 400) {
                                nbrStreaming++;
                                nx = ny = 2;
                            }
                        }
                }
            std::printf("[fill] %.0f ms: visible %d/%d settled %d known %zu want %zu inflight %zu done %zu scale %.2f "
                        "jobs %llu remesh %llu avg %.1f ms | hidden: unknown %d streaming %d gap %d nbr %d\n",
                        ms, visible, total, settled, info.size(), wantKey.size(), inFlight.size(), finished,
                        distanceScale, static_cast<unsigned long long>(jobs),
                        static_cast<unsigned long long>(probeRemesh.load()),
                        jobs ? static_cast<double>(probeJobUs.load()) / 1000.0 / static_cast<double>(jobs) : 0.0,
                        unknown, streaming, gapWait, nbrStreaming);
            std::fflush(stdout);
        }
    }

    void LevelRenderer::Update(Level* level, float camX, float camZ, const Frustum& frustum) {
        if (!level)
            return;
        if (options.cullInnerLeaves != cullLeavesApplied) {
            cullLeavesApplied = options.cullInnerLeaves;
            MesherSetCullInnerLeaves(cullLeavesApplied);
            InvalidateMeshes(true);
        }
        {
            const auto now = std::chrono::steady_clock::now();
            if (lastUpdate.time_since_epoch().count() != 0) {
                const double dt = std::chrono::duration<double, std::milli>(now - lastUpdate).count();
                if (dt > 0 && dt < 250)
                    frameEma = frameEma * 0.9 + dt * 0.1;
            }
            lastUpdate = now;
            static const bool adaptive = PERLIN_ENV("PERLIN_ADAPTIVE_DISTANCE") != nullptr;
            if (adaptive && options.renderDistance > 8) {
                const double since = lastScaleStep.time_since_epoch().count() == 0
                                         ? 1e9
                                         : std::chrono::duration<double, std::milli>(now - lastScaleStep).count();
                if (frameEma > 22.0 && since > 700.0) {
                    distanceScale = std::max(0.4, distanceScale - 0.05);
                    lastScaleStep = now;
                } else if (frameEma < 17.5 && distanceScale < 1.0 && since > 5000.0) {
                    distanceScale = std::min(1.0, distanceScale + 0.03);
                    lastScaleStep = now;
                }
            } else {
                distanceScale = 1.0;
            }
            static const double scale = [] {
                const char* v = PERLIN_ENV("PERLIN_WORLD_BUDGET");
                return v ? std::max(0.02, std::atof(v)) : 0.16;
            }();
            const double budget = std::clamp(frameEma * scale, 0.6, 3.0);
            maintenanceDeadline = now + std::chrono::microseconds(static_cast<int64_t>(budget * 1000.0));
            uploadBudgetMs = std::max(0.4, budget * 0.55);
        }

        viewChunkX = static_cast<int>(std::floor(camX / 16.0f));
        viewChunkZ = static_cast<int>(std::floor(camZ / 16.0f));
        viewKnown = true;
        UploadFinishedMeshes();
        EvictFarMeshes();

        if (level != lastLevel || level->clearCounter.load() != lastClear) {
            lastLevel = level;
            lastClear = level->clearCounter.load();
            ResetAll(*level);
            context.ClearChunkMeshes();
        }
        ApplyDirty(*level);
        if (static const int probeRadius = PERLIN_ENV("PERLIN_FILL_PROBE") ? std::atoi(PERLIN_ENV("PERLIN_FILL_PROBE")) : 0;
            probeRadius > 0)
            FillProbe(probeRadius, camX, camZ);
        Schedule(*level, camX, camZ, frustum);
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
        context.SetChunkCullRadius(std::max(2, EffectiveDistance()));
        context.DrawChunkMeshes(frustum, camX, camZ);
    }

    std::shared_ptr<MeshReuse> LevelRenderer::TakeReuse(const ChunkPos& pos) {
        std::lock_guard lock(reuseMutex);
        for (auto it = reuseCache.begin(); it != reuseCache.end(); ++it)
            if (it->first == pos) {
                std::shared_ptr<MeshReuse> found = std::move(it->second);
                reuseCache.erase(it);
                return found;
            }
        return std::make_shared<MeshReuse>();
    }

    void LevelRenderer::KeepReuse(const ChunkPos& pos, std::shared_ptr<MeshReuse> reuse) {
        std::lock_guard lock(reuseMutex);
        std::erase_if(reuseCache, [&](const auto& e) { return e.first == pos; });
        reuseCache.insert(reuseCache.begin(), {pos, std::move(reuse)});
        if (reuseCache.size() > kReuseColumns)
            reuseCache.resize(kReuseColumns);
    }

    void LevelRenderer::DropReuse(const ChunkPos* pos) {
        std::lock_guard lock(reuseMutex);
        if (!pos)
            reuseCache.clear();
        else
            std::erase_if(reuseCache, [&](const auto& e) { return e.first == *pos; });
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
