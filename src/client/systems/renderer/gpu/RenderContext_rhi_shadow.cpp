#include "client/systems/renderer/gpu/RenderContext_rhi.hpp"

#include "client/systems/renderer/gpu/ShadowMath.hpp"
#include "client/systems/resources/ShaderPack.hpp"
#include "util/Env.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include "util/Log.hpp"

namespace perlin {
    namespace {
        conduit::texture_handle MakeShadowTarget(gpu::Gpu& gpu, uint32_t size, conduit::format format,
                                            conduit::texture_usage usage, const char* name) {
            conduit::texture_desc td;
            td.format = format;
            td.width = td.height = std::max(1u, size);
            td.usage = usage;
            td.name = name;
            const conduit::texture_handle t = gpu.Dev().create_texture(td);
            if (t)
                gpu.SetState(t, conduit::state::undefined);
            return t;
        }

        constexpr conduit::texture_usage kSampledTarget =
            conduit::texture_usage::color_target | conduit::texture_usage::sampled;
        constexpr RenderLayer kShadowLayers[5] = {RenderLayer::Solid, RenderLayer::Cutout,
                                                  RenderLayer::CutoutCull, RenderLayer::Translucent,
                                                  RenderLayer::TranslucentCull};
    }

    void RenderContext_rhi::DestroyShadows() {
        for (ShadowMap& m : shadow.maps)
            for (conduit::texture_handle* t : {&m.color, &m.tint})
                if (*t) {
                    gpu.DestroyTexture(*t);
                    *t = {};
                }
        for (conduit::texture_handle* t : {&shadow.buildDepth, &shadow.entityColor, &shadow.entityDepth})
            if (*t) {
                gpu.DestroyTexture(*t);
                *t = {};
            }
        shadow = ShadowState{};
        shadowStages.clear();
        shadowConfigDirty = true;
    }

    void RenderContext_rhi::InitShadows(int quality, int distance) {
        if (!initialized && !gpu.Chain() && !gpu.OffscreenTexture())
            return;
        gpu.Dev().wait_idle();
        DestroyShadows();

        shadowConfigDirty = false;
        if ((!packActive && !javaActive) || quality <= 0 || PERLIN_ENV("PERLIN_NO_SHADOWS"))
            return;
        if (ProgramIndex(Program::Shadow) < 0 || ProgramIndex(Program::ShadowCutout) < 0)
            return;
        const conduit::device_caps& caps = gpu.Caps();
        static constexpr int kSizes[5] = {0, 1024, 2048, 3072, 4096};
        int size = kSizes[std::clamp(quality, 1, 4)];
        if (caps.limits.max_texture2d > 0)
            size = std::min<int>(size, static_cast<int>(caps.limits.max_texture2d));
        shadow.size = size;
        shadow.distance = std::clamp(distance > 0 ? static_cast<float>(distance) : 80.0f, 24.0f, 256.0f);

        const uint32_t s = static_cast<uint32_t>(size);
        shadow.buildDepth =
            MakeShadowTarget(gpu, s, conduit::format::d32_float, conduit::texture_usage::depth_target, "shadow depth");
        bool ok = static_cast<bool>(shadow.buildDepth);
        for (ShadowMap& m : shadow.maps) {
            if (!ok)
                break;
            m.color = MakeShadowTarget(gpu, s, conduit::format::r32_float, kSampledTarget, "shadow map");
            ok = static_cast<bool>(m.color);
        }
        if (!ok) {
            Log("shadows", "could not create the shadow maps; shadows off");
            DestroyShadows();
            shadowConfigDirty = false;
            return;
        }
        if (ProgramIndex(Program::ShadowGlass) >= 0) {
            bool tintOk = true;
            for (ShadowMap& m : shadow.maps) {
                m.tint = MakeShadowTarget(gpu, std::max(1u, s / 2), conduit::format::rgba16_float, kSampledTarget,
                                          "shadow tint");
                tintOk = tintOk && m.tint;
            }
            if (!tintOk)
                for (ShadowMap& m : shadow.maps)
                    if (m.tint) {
                        gpu.DestroyTexture(m.tint);
                        m.tint = {};
                    }
            shadow.tintEnabled = tintOk;
        }
        shadow.enabled = true;
        Log("shadows", "{}x{} map, {} blocks{}", size, size, shadow.distance,
            (shadow.tintEnabled ? ", coloured light through glass" : ""));
    }

    bool RenderContext_rhi::EnsureEntityShadowMap() {
        if (shadow.entityColor)
            return true;
        if (shadow.entityFailed)
            return false;
        const uint32_t s = static_cast<uint32_t>(shadow.size);
        shadow.entityColor =
            MakeShadowTarget(gpu, s, conduit::format::r32_float, kSampledTarget, "mob shadow map");
        shadow.entityDepth = MakeShadowTarget(gpu, s, conduit::format::d32_float, conduit::texture_usage::depth_target,
                                              "mob shadow depth");
        if (!shadow.entityColor || !shadow.entityDepth) {
            for (conduit::texture_handle* t : {&shadow.entityColor, &shadow.entityDepth})
                if (*t) {
                    gpu.DestroyTexture(*t);
                    *t = {};
                }
            shadow.entityFailed = true;
            Log("shadows", "could not create the mob shadow map; mobs and players cast no shadows");
            return false;
        }
        return true;
    }

    void RenderContext_rhi::BindShadowSampling() {
        const gpu::SamplerKind point = gpu::SamplerKind::PointClamp;
        const conduit::texture_handle white = gpu.White2D();
        if (!shadow.active) {
            for (const char* name : {"u_shadowInfo", "u_shadowInfoB", "u_shadowInfoE"})
                uni.Vec4(name, 0, 0, 0, 0);
            return;
        }
        const ShadowMap& f = shadow.maps[shadow.front];
        const bool fading = shadow.fading;
        const ShadowMap& next = shadow.maps[shadow.front == 0 ? 1 : 0];
        uni.Mat4("u_shadowMtx", f.sample);
        uni.Set("u_shadowInfo", f.info, 4);
        const ShadowMap& b = fading ? next : f;
        uni.Mat4("u_shadowMtxB", b.sample);
        uni.Vec4("u_shadowInfoB", b.info[0], b.info[1], b.info[2], fading ? shadow.fadeWeight : 0.0f);
        uni.Mat4("u_shadowMtxE", shadow.sampleE);
        uni.Vec4("u_shadowInfoE", shadow.infoE[0], shadow.infoE[1], shadow.infoE[2],
                 shadow.entities ? 1.0f : 0.0f);
        BindTexture("s_shadow", f.color, point);
        BindTexture("s_shadowTint", shadow.tintEnabled ? f.tint : white, point);
        BindTexture("s_shadowB", b.color, point);
        BindTexture("s_shadowTintB", shadow.tintEnabled ? b.tint : white, point);
        BindTexture("s_shadowEnt", shadow.entities ? shadow.entityColor : white, point);
    }

    void RenderContext_rhi::StartShadowBuild(const glm::vec3& light, const glm::vec3& cam,
                                             const gpu::shadow::Volume& volume, float step,
                                             const std::array<int64_t, 6>& key, double nowSeconds) {
        shadow.lastBuildAt = nowSeconds;
        shadow.building = true;
        shadow.haveKey = true;
        shadow.framesSince = 0;
        std::memcpy(shadow.key, key.data(), sizeof(shadow.key));
        const gpu::shadow::Basis eb = gpu::shadow::BasisOf(light);
        const glm::vec3 centre = gpu::shadow::SnappedCentre(eb, cam, step);
        shadow.buildRight = eb.right;
        shadow.buildUp = eb.up;
        shadow.buildFwd = eb.fwd;
        shadow.buildCx = centre.x;
        shadow.buildCy = centre.y;
        shadow.buildCz = centre.z;
        ShadowMap& m = shadow.maps[shadow.front == 0 ? 1 : 0];
        const gpu::shadow::Matrices matrices = gpu::shadow::BuildMatrices(eb, centre, volume);
        std::memcpy(m.sample, matrices.sample, sizeof(m.sample));
        std::memcpy(m.raster, matrices.raster, sizeof(m.raster));
        std::memcpy(m.info, matrices.info, sizeof(m.info));

        const bool cpuCasters = !features.On(gpu::GpuFeatures::ShadowCull);
        shadow.gpuCasters = !cpuCasters && gpuCullFrame;
        gpu::shadow::CasterPlanes(eb, centre, volume, shadow.casterPlanes);
        CollectShadowCasters(eb, centre, volume);
        shadow.buildCursor = 0;
        shadow.buildSlice = 0;
        shadow.buildSlices = shadow.front < 0 ? 1 : 4;
    }

    void RenderContext_rhi::CollectShadowCasters(const gpu::shadow::Basis& basis, const glm::vec3& centre,
                                                 const gpu::shadow::Volume& volume) {
        struct Found {
            uint8_t pass;
            int page;
            uint32_t first;
            int64_t key;
        };
        const uint8_t firstPass = shadow.gpuCasters ? 3 : 0;
        const uint8_t lastPass = shadow.tintEnabled ? 5 : 3;
        std::vector<Found> found;
        for (const auto& entry : meshes.ChunkMap()) {
            const gpu::ChunkMeshStore::Chunk& g = entry.second;
            if (g.page < 0)
                continue;
            const glm::vec3 chunkCentre(g.cx * 16.0f + 8.0f, (g.minY + g.maxY) * 0.5f, g.cz * 16.0f + 8.0f);
            const float radius = std::sqrt(128.0f + 0.25f * (g.maxY - g.minY) * (g.maxY - g.minY));
            if (!gpu::shadow::ChunkInVolume(chunkCentre, radius, basis, centre, volume))
                continue;
            for (uint8_t pass = firstPass; pass < lastPass; pass++) {
                const size_t slot = static_cast<size_t>(kShadowLayers[pass]);
                if (g.layerCount[slot])
                    found.push_back({pass, g.page, g.iOff[slot], entry.first});
            }
        }
        std::ranges::sort(found, [](const Found& a, const Found& b) {
            if (a.pass != b.pass)
                return a.pass < b.pass;
            return a.page != b.page ? a.page < b.page : a.first < b.first;
        });
        shadow.buildList.clear();
        shadow.buildList.reserve(found.size());
        for (const Found& f : found)
            shadow.buildList.push_back({f.key, f.pass});
    }

    bool RenderContext_rhi::PrimeShadowPass(int pass, const ShadowMap& m, DrawCall& templ) {
        const bool glass = pass >= 3;
        const Program program = glass ? Program::ShadowGlass : (pass == 0 ? Program::Shadow : Program::ShadowCutout);
        DrawState state = Base(program, !glass, !glass, conduit::compare_op::less);
        state.layout = gpu::VertexLayout::Terrain;
        state.cull = conduit::cull_mode::none;
        state.mask = conduit::color_mask::all;
        if (glass) {
            state.blend = true;
            state.srcColor = conduit::blend_factor::dst_color;
            state.dstColor = conduit::blend_factor::zero;
            state.srcAlpha = conduit::blend_factor::one;
            state.dstAlpha = conduit::blend_factor::one;
            state.alphaOp = conduit::blend_op::minimum;
        }
        SetMatrix(m.raster);
        uni.Mat4("u_shadowMtx", m.sample);
        uni.Set("u_shadowInfo", m.info, 4);
        BindAtlas();
        BindTexture("s_lightmap", lightmap.handle, gpu::SamplerKind::LinearClamp);
        if (!tileFlagsTex.used)
            UploadTileFlags({});
        BindTexture("s_tileFlags", tileFlagsTex.handle, gpu::SamplerKind::PointClamp);
        if (glass)
            BindTexture("s_shadow", m.color, gpu::SamplerKind::PointClamp);
        const bool ok = Prepare(glass ? View::ShadowGlass : View::ShadowBuild, state, templ);
        ResetBindings();
        return ok;
    }

    void RenderContext_rhi::FlushShadowBatch(CasterBatch& batch, DrawCall& templ) {
        if (batch.runs.empty() || batch.pass < 0)
            return;
        const RenderLayer layer = kShadowLayers[batch.pass];
        const View target = batch.pass >= 3 ? View::ShadowGlass : View::ShadowBuild;
        const conduit::buffer_handle vb = meshes.Vertices(batch.page), ib = meshes.Indices(batch.page, layer);
        if (!batch.multi || !PushRuns(target, templ, vb, ib, batch.runs, 0, batch.runs.size())) {
            const Source vertices{vb, 0};
            const Source indices{ib, 0};
            for (const auto& r : batch.runs)
                Push(target, templ, vertices, &indices, conduit::index_type::u32, r.first, r.count);
        }
        batch.runs.clear();
    }

    void RenderContext_rhi::RecordCpuShadowCasters(const ShadowMap& m, size_t count, int& primedPass,
                                                   DrawCall& templ) {
        bool primedOk = false;
        const auto& chunkMap = meshes.ChunkMap();
        CasterBatch batch;
        batch.multi = UseMultiDraw();
        for (size_t i = 0; i < count; i++) {
            const auto [chunkKey, pass] = shadow.buildList[shadow.buildCursor + i];
            const auto it = chunkMap.find(chunkKey);
            if (it == chunkMap.end() || it->second.page < 0)
                continue;
            const gpu::ChunkMeshStore::Chunk& g = it->second;
            const size_t slot = static_cast<size_t>(kShadowLayers[pass]);
            if (!g.layerCount[slot])
                continue;
            if (primedPass != pass || batch.page != static_cast<uint32_t>(g.page)) {
                FlushShadowBatch(batch, templ);
                if (primedPass != pass) {
                    primedOk = PrimeShadowPass(pass, m, templ);
                    primedPass = pass;
                }
                batch.pass = pass;
                batch.page = static_cast<uint32_t>(g.page);
            }
            if (!primedOk)
                continue;
            if (!batch.runs.empty() && batch.runs.back().first + batch.runs.back().count == g.iOff[slot]) {
                batch.runs.back().count += g.layerCount[slot];
                continue;
            }
            batch.runs.push_back({static_cast<uint32_t>(g.page), g.iOff[slot], g.layerCount[slot]});
        }
        if (primedOk)
            FlushShadowBatch(batch, templ);
    }

    void RenderContext_rhi::RecordGpuShadowCasters(const ShadowMap& m, DrawCall& templ) {
        terrain.CullShadows(shadow.casterPlanes, static_cast<uint32_t>(shadow.buildSlice),
                            static_cast<uint32_t>(std::max(1, shadow.buildSlices)));
        for (int pass = 0; pass < 3; pass++) {
            const RenderLayer layer = kShadowLayers[pass];
            terrain.ShadowDraws(layer, terrainDraws);
            if (terrainDraws.empty() || !PrimeShadowPass(pass, m, templ))
                continue;
            for (const gpu::GpuTerrain::Draw& d : terrainDraws) {
                DrawCall dc = templ;
                dc.vertices = meshes.Vertices(d.page);
                dc.indices = meshes.Indices(d.page, layer);
                dc.indexType = conduit::index_type::u32;
                dc.indirect = terrain.ShadowCommands();
                dc.indirectOffset = d.commandOffset;
                dc.indirectDraws = d.maxDraws;
                dc.countBuffer = terrain.Counts();
                dc.countOffset = d.countOffset;
                views[static_cast<size_t>(View::ShadowBuild)].draws.push_back(dc);
            }
        }
    }

    void RenderContext_rhi::QueueShadowBuildStages(const ShadowMap& m, bool firstSlice) {
        const float one[4] = {1, 1, 1, 1};
        Stage build;
        build.target.color = m.color;
        build.target.depth = shadow.buildDepth;
        build.target.w = build.target.h = static_cast<uint32_t>(shadow.size);
        build.target.colorFormat = conduit::format::r32_float;
        build.views = {View::ShadowBuild};
        build.clearColor = build.clearDepth = firstSlice;
        std::memcpy(build.clear, one, sizeof(one));
        shadowStages.push_back(build);
        if (shadow.tintEnabled) {
            Stage glass;
            glass.target.color = m.tint;
            glass.target.w = glass.target.h = std::max(1u, static_cast<uint32_t>(shadow.size) / 2);
            glass.target.colorFormat = conduit::format::rgba16_float;
            glass.views = {View::ShadowGlass};
            glass.clearColor = firstSlice;
            std::memcpy(glass.clear, one, sizeof(one));
            shadowStages.push_back(glass);
        }
    }

    void RenderContext_rhi::RecordShadowSlice() {
        ShadowMap& m = shadow.maps[shadow.front == 0 ? 1 : 0];
        const bool firstSlice = shadow.buildSlice == 0;
        const size_t remainingSlices = static_cast<size_t>(std::max(1, shadow.buildSlices - shadow.buildSlice));
        const size_t left = shadow.buildList.size() - shadow.buildCursor;
        const size_t count = (left + remainingSlices - 1) / remainingSlices;

        int primedPass = -1;
        DrawCall templ;
        RecordCpuShadowCasters(m, count, primedPass, templ);
        if (shadow.gpuCasters && gpuCullFrame)
            RecordGpuShadowCasters(m, templ);
        QueueShadowBuildStages(m, firstSlice);

        shadow.buildCursor += count;
        shadow.buildSlice++;
        if (shadow.buildSlice >= shadow.buildSlices ||
            (!shadow.gpuCasters && shadow.buildCursor >= shadow.buildList.size())) {
            shadow.building = false;
            if (shadow.front < 0) {
                shadow.front = 0;
            } else {
                shadow.fading = true;
                shadow.fadeT = 0.0f;
            }
            shadow.buildList.clear();
            shadow.buildList.shrink_to_fit();
        }
    }

    void RenderContext_rhi::StepShadowFade(float dt) {
        shadow.fadeWeight = 0.0f;
        if (!shadow.fading)
            return;
        const gpu::shadow::Fade fade = gpu::shadow::AdvanceFade(shadow.fadeT, dt);
        shadow.fadeT = fade.t;
        if (fade.finished) {
            shadow.front = shadow.front == 0 ? 1 : 0;
            shadow.fading = false;
        } else {
            shadow.fadeWeight = fade.weight;
        }
    }

    void RenderContext_rhi::QueueEntityShadow(const glm::vec3& light, const glm::vec3& cam,
                                              const gpu::shadow::Volume& volume) {
        const gpu::shadow::Basis eb = gpu::shadow::BasisOf(light);
        const glm::vec3 centre = gpu::shadow::SnappedCentre(eb, cam, gpu::shadow::TexelSize(volume));
        const gpu::shadow::Matrices matrices = gpu::shadow::BuildMatrices(eb, centre, volume);
        std::memcpy(shadow.sampleE, matrices.sample, sizeof(shadow.sampleE));
        std::memcpy(shadow.rasterE, matrices.raster, sizeof(shadow.rasterE));
        std::memcpy(shadow.infoE, matrices.info, sizeof(shadow.infoE));
        Stage ents;
        ents.target.color = shadow.entityColor;
        ents.target.depth = shadow.entityDepth;
        ents.target.w = ents.target.h = static_cast<uint32_t>(shadow.size);
        ents.target.colorFormat = conduit::format::r32_float;
        ents.views = {View::ShadowEntity};
        ents.clearColor = ents.clearDepth = true;
        const float one[4] = {1, 1, 1, 1};
        std::memcpy(ents.clear, one, sizeof(one));
        shadowStages.push_back(ents);
        shadow.entities = true;
    }

    void RenderContext_rhi::ConfigureShadowViews() {
        for (View v : {View::ShadowBuild, View::ShadowEntity}) {
            ViewData& vd = views[static_cast<size_t>(v)];
            vd.colorFormat = conduit::format::r32_float;
            vd.depthFormat = conduit::format::d32_float;
            vd.w = vd.h = static_cast<uint32_t>(shadow.size);
        }
        ViewData& glass = views[static_cast<size_t>(View::ShadowGlass)];
        glass.colorFormat = conduit::format::rgba16_float;
        glass.depthFormat = conduit::format::unknown;
        glass.w = glass.h = std::max(1u, static_cast<uint32_t>(shadow.size) / 2);
    }

    void RenderContext_rhi::RenderShadows(float camX, float camY, float camZ) {
        shadow.active = false;
        shadow.entities = false;
        if (!shadow.enabled || !frame.sky.enabled)
            return;
        const std::optional<gpu::shadow::Light> light = gpu::shadow::LightFromSunAngle(frame.sky.sunAngle);
        if (!light)
            return;
        const gpu::shadow::Volume volume = gpu::shadow::VolumeFor(shadow.distance, shadow.size, shadow.tintEnabled);
        const float step = gpu::shadow::SnapStep(volume);
        const glm::vec3 cam(camX, camY, camZ);

        const double nowSeconds = frame.timeSeconds;
        const float dtFrame = shadow.lastTime > 0.0
                                  ? static_cast<float>(std::clamp(nowSeconds - shadow.lastTime, 0.0, 0.1))
                                  : 0.0f;
        shadow.lastTime = nowSeconds;

        const std::array<int64_t, 6> key = gpu::shadow::RebuildKey(gpu::shadow::BasisOf(light->quantized), cam, step,
                                                                    light->key, meshes.Generation());
        shadow.framesSince++;
        const bool keyChanged = !shadow.haveKey || std::memcmp(key.data(), shadow.key, 5 * sizeof(int64_t)) != 0;
        const bool meshOnly = !keyChanged && key[5] != shadow.key[5];
        const bool due = keyChanged || shadow.framesSince > 600 || (meshOnly && nowSeconds - shadow.lastBuildAt > 0.5);
        if (!shadow.building && !shadow.fading && due)
            StartShadowBuild(light->direction, cam, volume, step, key, nowSeconds);

        ConfigureShadowViews();
        if (shadow.building)
            RecordShadowSlice();
        if (shadow.front < 0)
            return;

        StepShadowFade(dtFrame);
        shadow.active = true;
        if (frame.shadowEntities && ProgramIndex(Program::EntityShadow) >= 0 && EnsureEntityShadowMap())
            QueueEntityShadow(light->direction, cam, volume);
    }
}
