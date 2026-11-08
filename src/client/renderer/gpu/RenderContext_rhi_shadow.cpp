#include "client/renderer/gpu/RenderContext_rhi.hpp"

#include "client/resources/ShaderPack.hpp"
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

        struct Basis {
            glm::vec3 right, up, fwd;
        };

        Basis BasisOf(const glm::vec3& light) {
            Basis b;
            b.fwd = -light;
            const glm::vec3 upRef = std::abs(b.fwd.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
            b.right = glm::normalize(glm::cross(upRef, b.fwd));
            b.up = glm::cross(b.fwd, b.right);
            return b;
        }
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

    void RenderContext_rhi::RenderShadows(float camX, float camY, float camZ) {
        shadow.active = false;
        shadow.entities = false;
        if (!shadow.enabled || !frame.sky.enabled)
            return;
        const float sunA = frame.sky.sunAngle;
        glm::vec3 L(-std::sin(sunA), std::cos(sunA), 0.0f);
        if (L.y < 0.0f)
            L = -L;

        if (L.y < 0.03f)
            return;
        const int64_t lightKey[2] = {static_cast<int64_t>(std::llround(L.x * 300.0f)),
                                     static_cast<int64_t>(std::llround(L.y * 300.0f))};
        const glm::vec3 Lq =
            glm::normalize(glm::vec3(static_cast<float>(lightKey[0]), static_cast<float>(lightKey[1]),
                                     std::round(L.z * 300.0f)) /
                           300.0f);

        const float R = shadow.distance;
        const float D = R + 80.0f;
        const float texel = 2.0f * R / static_cast<float>(shadow.size);
        const float step = texel * 32.0f;
        const glm::vec3 cam(camX, camY, camZ);

        const auto fillMatrices = [&](const Basis& b, float cx, float cy, float cz, float* sample,
                                      float* raster, float* info) {
            float S[16] = {};
            auto col = [&](int c, int r) -> float& { return S[c * 4 + r]; };
            for (int j = 0; j < 3; j++) {
                col(j, 0) = b.right[j] / (2.0f * R);
                col(j, 1) = b.up[j] / (2.0f * R);
                col(j, 2) = b.fwd[j] / (2.0f * D);
            }
            col(3, 0) = 0.5f - cx / (2.0f * R);
            col(3, 1) = 0.5f - cy / (2.0f * R);
            col(3, 2) = 0.5f - cz / (2.0f * D);
            col(3, 3) = 1.0f;
            std::memcpy(sample, S, sizeof(S));

            for (int c = 0; c < 4; c++) {
                const float u = S[c * 4 + 0], v = S[c * 4 + 1], d = S[c * 4 + 2], w = S[c * 4 + 3];
                raster[c * 4 + 0] = 2.0f * u - (c == 3 ? 1.0f : 0.0f);
                raster[c * 4 + 1] = -(2.0f * v - (c == 3 ? 1.0f : 0.0f));
                raster[c * 4 + 2] = d;
                raster[c * 4 + 3] = w;
            }
            info[0] = 1.0f / static_cast<float>(shadow.size);
            info[1] = 2.0f * R;
            info[2] = 2.0f * D;
            info[3] = shadow.tintEnabled ? 2.0f : 1.0f;
        };

        const double nowSeconds = frame.timeSeconds;
        const float dtFrame = shadow.lastTime > 0.0
                                  ? static_cast<float>(std::clamp(nowSeconds - shadow.lastTime, 0.0, 0.1))
                                  : 0.0f;
        shadow.lastTime = nowSeconds;

        const Basis kb = BasisOf(Lq);
        const int64_t key[6] = {
            static_cast<int64_t>(std::llround(std::floor(glm::dot(cam, kb.right) / step))),
            static_cast<int64_t>(std::llround(std::floor(glm::dot(cam, kb.up) / step))),
            static_cast<int64_t>(std::llround(std::floor(glm::dot(cam, kb.fwd) / step))),
            lightKey[0],
            lightKey[1],
            static_cast<int64_t>(meshes.Generation())};
        shadow.framesSince++;
        const bool keyChanged = !shadow.haveKey || std::memcmp(key, shadow.key, 5 * sizeof(int64_t)) != 0;
        const bool meshOnly = !keyChanged && key[5] != shadow.key[5];
        if (!shadow.building && !shadow.fading &&
            (keyChanged || shadow.framesSince > 600 || (meshOnly && nowSeconds - shadow.lastBuildAt > 0.5))) {
            shadow.lastBuildAt = nowSeconds;
            shadow.building = true;
            shadow.haveKey = true;
            shadow.framesSince = 0;
            std::memcpy(shadow.key, key, sizeof(key));
            const Basis eb = BasisOf(L);
            const float cx = std::floor(glm::dot(cam, eb.right) / step) * step;
            const float cy = std::floor(glm::dot(cam, eb.up) / step) * step;
            const float cz = std::floor(glm::dot(cam, eb.fwd) / step) * step;
            shadow.buildRight = eb.right;
            shadow.buildUp = eb.up;
            shadow.buildFwd = eb.fwd;
            shadow.buildCx = cx;
            shadow.buildCy = cy;
            shadow.buildCz = cz;
            ShadowMap& m = shadow.maps[shadow.front == 0 ? 1 : 0];
            fillMatrices(eb, cx, cy, cz, m.sample, m.raster, m.info);

            struct Found {
                uint8_t pass;
                int page;
                uint32_t first;
                int64_t key;
            };
            std::vector<Found> found;
            for (const auto& entry : meshes.ChunkMap()) {
                const gpu::ChunkMeshStore::Chunk& g = entry.second;
                if (g.page < 0)
                    continue;
                const glm::vec3 centre(g.cx * 16.0f + 8.0f, (g.minY + g.maxY) * 0.5f, g.cz * 16.0f + 8.0f);
                const float radius = std::sqrt(128.0f + 0.25f * (g.maxY - g.minY) * (g.maxY - g.minY));
                if (std::abs(glm::dot(centre, eb.right) - cx) > R + radius ||
                    std::abs(glm::dot(centre, eb.up) - cy) > R + radius ||
                    std::abs(glm::dot(centre, eb.fwd) - cz) > D + radius)
                    continue;
                for (uint8_t pass = 0; pass < (shadow.tintEnabled ? 5 : 3); pass++) {
                    const size_t slot = static_cast<size_t>(kShadowLayers[pass]);
                    if (g.layerCount[slot])
                        found.push_back({pass, g.page, g.iOff[slot], entry.first});
                }
            }
            std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
                if (a.pass != b.pass)
                    return a.pass < b.pass;
                return a.page != b.page ? a.page < b.page : a.first < b.first;
            });
            shadow.buildList.clear();
            shadow.buildList.reserve(found.size());
            for (const Found& f : found)
                shadow.buildList.push_back({f.key, f.pass});
            shadow.buildCursor = 0;
            shadow.buildSlice = 0;

            shadow.buildSlices = shadow.front < 0 ? 1 : 4;
        }

        const conduit::format mapFormat = conduit::format::r32_float;
        for (View v : {View::ShadowBuild, View::ShadowEntity}) {
            ViewData& vd = views[static_cast<size_t>(v)];
            vd.colorFormat = mapFormat;
            vd.depthFormat = conduit::format::d32_float;
            vd.w = vd.h = static_cast<uint32_t>(shadow.size);
        }
        {
            ViewData& vd = views[static_cast<size_t>(View::ShadowGlass)];
            vd.colorFormat = conduit::format::rgba16_float;
            vd.depthFormat = conduit::format::unknown;
            vd.w = vd.h = std::max(1u, static_cast<uint32_t>(shadow.size) / 2);
        }

        if (shadow.building) {
            ShadowMap& m = shadow.maps[shadow.front == 0 ? 1 : 0];
            const bool firstSlice = shadow.buildSlice == 0;
            const size_t remainingSlices =
                static_cast<size_t>(std::max(1, shadow.buildSlices - shadow.buildSlice));
            const size_t left = shadow.buildList.size() - shadow.buildCursor;
            const size_t count = (left + remainingSlices - 1) / remainingSlices;

            const auto prime = [&](int pass, DrawCall& templ) {
                const bool glass = pass >= 3;
                const Program program =
                    glass ? Program::ShadowGlass : (pass == 0 ? Program::Shadow : Program::ShadowCutout);
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
            };

            int primedPass = -1;
            bool primedOk = false;
            DrawCall templ;
            const auto& chunkMap = meshes.ChunkMap();
            for (size_t i = 0; i < count; i++) {
                const auto [chunkKey, pass] = shadow.buildList[shadow.buildCursor + i];
                const auto it = chunkMap.find(chunkKey);
                if (it == chunkMap.end() || it->second.page < 0)
                    continue;
                const gpu::ChunkMeshStore::Chunk& g = it->second;
                const RenderLayer layer = kShadowLayers[pass];
                const size_t slot = static_cast<size_t>(layer);
                if (!g.layerCount[slot])
                    continue;
                if (primedPass != pass) {
                    primedOk = prime(pass, templ);
                    primedPass = pass;
                }
                if (!primedOk)
                    continue;
                const View target = pass >= 3 ? View::ShadowGlass : View::ShadowBuild;
                const Source vertices{meshes.Vertices(static_cast<uint32_t>(g.page)), 0};
                const Source indices{meshes.Indices(static_cast<uint32_t>(g.page), layer), 0};
                std::vector<DrawCall>& queued = views[static_cast<size_t>(target)].draws;
                if (!queued.empty() && queued.back().pipeline == templ.pipeline &&
                    queued.back().textures == templ.textures && queued.back().uniformOffset == templ.uniformOffset &&
                    queued.back().uniformChunk == templ.uniformChunk && queued.back().vertices == vertices.buffer &&
                    queued.back().indices == indices.buffer &&
                    queued.back().first + queued.back().count == g.iOff[slot]) {
                    queued.back().count += g.layerCount[slot];
                    continue;
                }
                Push(target, templ, vertices, &indices, conduit::index_type::u32, g.iOff[slot], g.layerCount[slot]);
            }

            const float one[4] = {1, 1, 1, 1};
            Stage build;
            build.target.color = m.color;
            build.target.depth = shadow.buildDepth;
            build.target.w = build.target.h = static_cast<uint32_t>(shadow.size);
            build.target.colorFormat = mapFormat;
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

            shadow.buildCursor += count;
            shadow.buildSlice++;
            if (shadow.buildSlice >= shadow.buildSlices || shadow.buildCursor >= shadow.buildList.size()) {
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
        if (shadow.front < 0)
            return;

        shadow.fadeWeight = 0.0f;
        if (shadow.fading) {
            static constexpr float kFadeSeconds = 0.45f;
            shadow.fadeT += dtFrame / kFadeSeconds;
            if (shadow.fadeT >= 1.0f) {
                shadow.front = shadow.front == 0 ? 1 : 0;
                shadow.fading = false;
                shadow.fadeT = 0.0f;
            } else {
                shadow.fadeWeight = shadow.fadeT * shadow.fadeT * (3.0f - 2.0f * shadow.fadeT);
            }
        }
        shadow.active = true;

        if (frame.shadowEntities && ProgramIndex(Program::EntityShadow) >= 0 && EnsureEntityShadowMap()) {
            const Basis eb = BasisOf(L);
            const float ex = std::floor(glm::dot(cam, eb.right) / texel) * texel;
            const float ey = std::floor(glm::dot(cam, eb.up) / texel) * texel;
            const float ez = std::floor(glm::dot(cam, eb.fwd) / texel) * texel;
            fillMatrices(eb, ex, ey, ez, shadow.sampleE, shadow.rasterE, shadow.infoE);
            Stage ents;
            ents.target.color = shadow.entityColor;
            ents.target.depth = shadow.entityDepth;
            ents.target.w = ents.target.h = static_cast<uint32_t>(shadow.size);
            ents.target.colorFormat = mapFormat;
            ents.views = {View::ShadowEntity};
            ents.clearColor = ents.clearDepth = true;
            const float one[4] = {1, 1, 1, 1};
            std::memcpy(ents.clear, one, sizeof(one));
            shadowStages.push_back(ents);
            shadow.entities = true;
        }
    }
}
