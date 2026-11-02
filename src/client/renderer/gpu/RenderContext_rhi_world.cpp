#include "client/renderer/gpu/RenderContext_rhi.hpp"

#include "client/renderer/MaterialDb.hpp"
#include "util/Env.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <cstdio>
#include "util/Log.hpp"

namespace perlin {
    namespace {
        void DownsampleAlphaAware(const uint8_t* src, const size_t idx[4], uint8_t* d) {
            unsigned sum[4] = {0, 0, 0, 0};
            unsigned n = 0, na = 0;
            for (int k = 0; k < 4; k++) {
                na += src[idx[k] + 3];
                if (src[idx[k] + 3] > 128) {
                    for (int c = 0; c < 4; c++)
                        sum[c] += src[idx[k] + c];
                    n++;
                }
            }
            if (n == 0) {
                for (int c = 0; c < 3; c++) {
                    unsigned total = 0;
                    for (int k = 0; k < 4; k++)
                        total += src[idx[k] + c];
                    d[c] = static_cast<uint8_t>((total + 2) / 4);
                }
                d[3] = static_cast<uint8_t>((na + 2) / 4);
            } else {
                for (int c = 0; c < 3; c++)
                    d[c] = static_cast<uint8_t>((sum[c] + n / 2) / n);
                d[3] = static_cast<uint8_t>((na + 2) / 4);
            }
        }

        int MipCount(int size) {
            int n = 1;
            while ((size >> n) > 0)
                n++;
            return n;
        }

        // transparent texels take their neighbours' colour first so mips do not darken cutout edges
        std::vector<std::vector<uint8_t>> BuildTileMips(int tile, const uint8_t* rgba, size_t stride) {
            const int count = MipCount(tile);
            std::vector<std::vector<uint8_t>> mips;
            mips.reserve(static_cast<size_t>(count));
            std::vector<uint8_t> level(static_cast<size_t>(tile) * tile * 4);
            bool anyClear = false;
            for (int y = 0; y < tile; y++) {
                std::memcpy(level.data() + static_cast<size_t>(y) * tile * 4,
                            rgba + static_cast<size_t>(y) * stride, static_cast<size_t>(tile) * 4);
                for (int x = 0; x < tile; x++)
                    anyClear = anyClear || level[(static_cast<size_t>(y) * tile + x) * 4 + 3] == 0;
            }
            if (anyClear) {
                std::vector<uint8_t> bled = level;
                for (int y = 0; y < tile; y++)
                    for (int x = 0; x < tile; x++) {
                        uint8_t* px = bled.data() + (static_cast<size_t>(y) * tile + x) * 4;
                        if (level[(static_cast<size_t>(y) * tile + x) * 4 + 3] != 0)
                            continue;
                        unsigned sum[3] = {0, 0, 0}, n = 0;
                        for (int dy = -1; dy <= 1; dy++)
                            for (int dx = -1; dx <= 1; dx++) {
                                const int nx = x + dx, ny = y + dy;
                                if (nx < 0 || ny < 0 || nx >= tile || ny >= tile)
                                    continue;
                                const uint8_t* q = level.data() + (static_cast<size_t>(ny) * tile + nx) * 4;
                                if (!q[3])
                                    continue;
                                sum[0] += q[0];
                                sum[1] += q[1];
                                sum[2] += q[2];
                                n++;
                            }
                        if (n) {
                            px[0] = static_cast<uint8_t>(sum[0] / n);
                            px[1] = static_cast<uint8_t>(sum[1] / n);
                            px[2] = static_cast<uint8_t>(sum[2] / n);
                        }
                    }
                level = std::move(bled);
            }
            for (int l = 0; l < count; l++) {
                const int w = std::max(1, tile >> l);
                if (l + 1 == count) {
                    mips.push_back(std::move(level));
                    break;
                }
                const int h = std::max(1, w >> 1);
                std::vector<uint8_t> next(static_cast<size_t>(h) * h * 4);
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < h; x++) {
                        const size_t idx[4] = {
                            (static_cast<size_t>(y * 2) * w + x * 2) * 4,
                            (static_cast<size_t>(y * 2) * w + x * 2 + 1) * 4,
                            (static_cast<size_t>(y * 2 + 1) * w + x * 2) * 4,
                            (static_cast<size_t>(y * 2 + 1) * w + x * 2 + 1) * 4,
                        };
                        DownsampleAlphaAware(level.data(), idx,
                                             next.data() + (static_cast<size_t>(y) * h + x) * 4);
                    }
                mips.push_back(std::move(level));
                level = std::move(next);
            }
            return mips;
        }

        constexpr rhi::ColorMask kRgbA = rhi::ColorMask::All;
    }

    // -- textures ---------------------------------------------------------------------------------------------------

    int RenderContext_rhi::MaxAtlasLayers(int tilePx) const {
        const int limit =
            initialized ? std::max<int>(
                              1, static_cast<int>(const_cast<gpu::Gpu&>(gpu).Caps().limits.maxTextureLayers))
                        : 2048;
        const double perLayer =
            static_cast<double>(std::max(1, tilePx)) * std::max(1, tilePx) * 4.0 * 4.0 / 3.0;
        const double budget = 1.3e9;
        return std::max(256, std::min(limit, static_cast<int>(budget / perLayer)));
    }

    void RenderContext_rhi::UploadAtlas(const uint8_t* rgba, int tilePx, int tiles) {
        if (!initialized || !rgba || tilePx <= 0 || tiles <= 0)
            return;
        animatedTiles.clear();
        const int limit = std::max<int>(1, static_cast<int>(gpu.Caps().limits.maxTextureLayers));
        const int layers = std::min(limit, (tiles + 255) / 256 * 256);
        TexRec tex;
        if (!MakeTexture(tex, static_cast<uint32_t>(tilePx), static_cast<uint32_t>(tilePx),
                         static_cast<uint32_t>(layers), static_cast<uint32_t>(MipCount(tilePx)),
                         rhi::Format::RGBA8Unorm, gpu::SamplerKind::PointMipClamp, "block atlas")) {
            Log("render", "atlas upload failed ({} layers of {}px)", layers, tilePx);
            return;
        }
        const size_t stride = static_cast<size_t>(tilePx) * 4;
        const size_t tileBytes = static_cast<size_t>(tilePx) * tilePx * 4;
        const int count = std::min(tiles, layers);
        for (int tile = 0; tile < count; tile++) {
            const auto mips = BuildTileMips(tilePx, rgba + tileBytes * static_cast<size_t>(tile), stride);
            for (size_t l = 0; l < mips.size(); l++) {
                const uint32_t w = static_cast<uint32_t>(std::max(1, tilePx >> static_cast<int>(l)));
                gpu.WriteTexture(tex.handle, static_cast<uint32_t>(tile), 0, 0, w, w, mips[l].data(), 0,
                                 static_cast<uint32_t>(l));
            }
        }
        FreeTexture(atlas);
        atlas = tex;
        atlasLayers = layers;
        tileSize = tilePx;
        atlasTiles = count;
        atlasSource.assign(rgba, rgba + tileBytes * static_cast<size_t>(count));
        if (jpack)
            jpack->SetAtlas(rgba, tilePx, count);
    }

    void RenderContext_rhi::UpdateAtlasRegion(int tile, const uint8_t* rgba) {
        if (!initialized || !atlas.used || !rgba || tile < 0 || tile >= atlasLayers)
            return;
        auto it = animatedTiles.find(rgba);
        if (it == animatedTiles.end()) {
            if (animatedTiles.size() > 4096)
                animatedTiles.clear();
            it = animatedTiles.emplace(rgba, BuildTileMips(tileSize, rgba, static_cast<size_t>(tileSize) * 4))
                     .first;
        }
        const auto& mips = it->second;
        if (tile < atlasTiles)
            std::memcpy(atlasSource.data() + static_cast<size_t>(tile) * tileSize * tileSize * 4,
                        mips[0].data(), mips[0].size());
        if (jpack)
            jpack->UpdateTile(tile, mips[0].data());
        for (size_t l = 0; l < mips.size(); l++) {
            const uint32_t w = static_cast<uint32_t>(std::max(1, tileSize >> static_cast<int>(l)));
            gpu.WriteTexture(atlas.handle, static_cast<uint32_t>(tile), 0, 0, w, w, mips[l].data(), 0,
                             static_cast<uint32_t>(l));
        }
    }

    void RenderContext_rhi::UploadSkyTextures(const Texture& sun, const Texture& moon,
                                              const Texture& clouds) {
        if (!initialized)
            return;
        const auto create = [&](TexRec& rec, const Texture& texture, gpu::SamplerKind sampler,
                                const char* name) {
            FreeTexture(rec);
            if (texture.width <= 0 || texture.height <= 0)
                return;
            if (MakeTexture(rec, static_cast<uint32_t>(texture.width), static_cast<uint32_t>(texture.height),
                            1, 1, rhi::Format::RGBA8Unorm, sampler, name))
                gpu.WriteTexture(rec.handle, 0, 0, 0, rec.w, rec.h, texture.pixels.data());
        };
        sunSource = sun;
        moonSource = moon;
        cloudSource = clouds;
        create(sunTex, sun, gpu::SamplerKind::PointClamp, "sun");
        create(moonTex, moon, gpu::SamplerKind::PointClamp, "moon");
        create(cloudMapTex, clouds, gpu::SamplerKind::PointRepeat, "cloud map");
        cloudTexture.width = clouds.width;
        cloudTexture.height = clouds.height;
        cloudTexture.solid.assign(static_cast<size_t>(std::max(0, clouds.width)) * std::max(0, clouds.height),
                                  0);
        if (clouds.pixels.size() >= cloudTexture.solid.size() * 4)
            for (size_t i = 0; i < cloudTexture.solid.size(); i++)
                cloudTexture.solid[i] = clouds.pixels[i * 4 + 3] >= 10 ? 1 : 0;
        cloudBuilt = false;
    }

    void RenderContext_rhi::UploadTileFlags(const std::vector<uint8_t>& flags) {
        if (!initialized)
            return;
        constexpr int kW = 256, kH = 64;
        tileFlagData = flags;
        std::vector<uint8_t> rgba(static_cast<size_t>(kW) * kH * 4, 0);
        for (size_t i = 0; i < flags.size() && i < static_cast<size_t>(kW) * kH; i++)
            rgba[i * 4] = flags[i];
        if (!tileFlagsTex.used)
            MakeTexture(tileFlagsTex, kW, kH, 1, 1, rhi::Format::RGBA8Unorm, gpu::SamplerKind::PointClamp,
                        "tile flags");
        if (tileFlagsTex.used)
            gpu.WriteTexture(tileFlagsTex.handle, 0, 0, 0, kW, kH, rgba.data());
    }

    void RenderContext_rhi::UploadCrosshair(const Texture& icons) {
        if (!initialized || icons.width < 16 || icons.height < 16)
            return;
        iconSource = icons;
        const int size = std::max(1, icons.width * 15 / 256);
        std::vector<uint8_t> crop(static_cast<size_t>(size) * size * 4);
        for (int y = 0; y < size; y++)
            std::memcpy(crop.data() + static_cast<size_t>(y) * size * 4,
                        icons.pixels.data() + static_cast<size_t>(y) * icons.width * 4,
                        static_cast<size_t>(size) * 4);
        if (MakeTexture(crosshair, static_cast<uint32_t>(size), static_cast<uint32_t>(size), 1, 1,
                        rhi::Format::RGBA8Unorm, gpu::SamplerKind::PointClamp, "crosshair")) {
            gpu.WriteTexture(crosshair.handle, 0, 0, 0, crosshair.w, crosshair.h, crop.data());
            crosshairPx = size;
        }
    }

    // -- meshes -----------------------------------------------------------------------------------------------------

    void RenderContext_rhi::UpdateChunkMesh(int cx, int cz, const Tessellator& tess, uint64_t version) {
        if (initialized)
            meshes.Update(false, cx, cz, 16, tess, version);
    }

    void RenderContext_rhi::UpdateLodTile(int tx, int tz, int size, const Tessellator& tess,
                                          uint64_t version) {
        if (initialized)
            meshes.Update(true, tx, tz, size, tess, version);
    }

    void RenderContext_rhi::RemoveChunkMesh(int cx, int cz) {
        if (initialized)
            meshes.Remove(false, cx, cz);
    }

    void RenderContext_rhi::RemoveLodTile(int tx, int tz) {
        if (initialized)
            meshes.Remove(true, tx, tz);
    }

    void RenderContext_rhi::UnloadLevelResources() {
        ClearChunkMeshes();
        for (TexRec* texture : {&atlas, &sunTex, &moonTex, &cloudMapTex, &tileFlagsTex, &crosshair})
            FreeTexture(*texture);
        animatedTiles.clear();
        std::vector<uint8_t>().swap(atlasSource);
        std::vector<uint8_t>().swap(tileFlagData);
        sunSource = moonSource = cloudSource = iconSource = Texture{};
        cloudTexture = {};
        cloudBuilt = false;
        atlasLayers = atlasTiles = crosshairPx = 0;
    }

    void RenderContext_rhi::ClearChunkMeshes() {
        if (initialized)
            meshes.Clear();
    }

    // -- world ------------------------------------------------------------------------------------------------------

    RenderContext_rhi::DrawState RenderContext_rhi::PassState(RenderLayer layer) const {
        DrawState s = Base(Program::TerrainSolid, true, true, rhi::CompareOp::Less);
        s.layout = gpu::VertexLayout::Terrain;
        s.mask = rhi::ColorMask::RGB;
        const auto blend = [&] {
            s.blend = true;
            s.srcColor = rhi::BlendFactor::SrcAlpha;
            s.dstColor = rhi::BlendFactor::InvSrcAlpha;
            s.srcAlpha = rhi::BlendFactor::One;
            s.dstAlpha = rhi::BlendFactor::InvSrcAlpha;
        };
        switch (layer) {
        case RenderLayer::Solid:
            s.mask = kRgbA;
            s.cull = cullMode;
            break;
        case RenderLayer::Cutout:
            s.depthOp = rhi::CompareOp::LessEqual;
            s.mask = kRgbA;
            break;
        case RenderLayer::CutoutCull:
            s.depthOp = rhi::CompareOp::LessEqual;
            s.mask = kRgbA;
            s.cull = cullMode;
            break;
        case RenderLayer::TranslucentCull:
            s.cull = cullMode;
            s.depthWrite = false;
            blend();
            break;
        case RenderLayer::Translucent:
            blend();
            break;
        }
        return s;
    }

    void RenderContext_rhi::DrawLayer(RenderLayer layer, Program program, View view) {
        static const bool hideBlend = PERLIN_ENV("PERLIN_HIDE_TRANSLUCENT") != nullptr;
        if (hideBlend && (layer == RenderLayer::Translucent || layer == RenderLayer::TranslucentCull))
            return;
        const bool ordered = layer == RenderLayer::Translucent || layer == RenderLayer::TranslucentCull;
        meshes.CollectRuns(layer, ordered, runs);
        if (javaActive && PERLIN_ENV("PERLIN_JAVA_DBG")) {
            static int m = 0;
            if (m++ < 10)
                std::fprintf(stderr, "[javadbg] runs layer=%d count=%zu\n", static_cast<int>(layer),
                             runs.size());
        }
        if (runs.empty())
            return;
        DrawState state = PassState(layer);
        state.program = program;
        state.layout = gpu::VertexLayout::Terrain;

        SetMatrix(mvp);
        BindTexture("s_atlas", atlas.used ? atlas.handle : gpu.WhiteArray(), gpu::SamplerKind::PointMipClamp);
        BindTexture("s_lightmap", lightmap.handle, gpu::SamplerKind::LinearClamp);
        if (cloudMapTex.used)
            BindTexture("s_clouds", cloudMapTex.handle, gpu::SamplerKind::PointRepeat);
        if (!tileFlagsTex.used)
            UploadTileFlags({});
        BindTexture("s_tileFlags", tileFlagsTex.handle, gpu::SamplerKind::PointClamp);
        BindShadowSampling();
        if (javaActive)
            BindJavaExtras();
        if (javaActive && ordered && post.sceneValid) {
            BindTexture("s_gaux2", post.javaC5, gpu::SamplerKind::PointClamp);
            BindTexture("s_colortex0", post.sceneColor, gpu::SamplerKind::LinearClamp);
            BindTexture("s_sceneDepth", post.sceneDepth, gpu::SamplerKind::PointClamp);
            BindTexture("s_depth", post.sceneDepth, gpu::SamplerKind::PointClamp);
        }
        if (ordered && post.sceneValid) {
            BindTexture("s_sceneColor", post.sceneColor, gpu::SamplerKind::LinearClamp);
            BindTexture("s_sceneDepth", post.sceneDepth, gpu::SamplerKind::PointClamp);
        }

        DrawCall templ;
        if (!Prepare(view, state, templ)) {
            if (javaActive && PERLIN_ENV("PERLIN_JAVA_DBG"))
                std::fprintf(stderr, "[javadbg] prepare failed layer=%d prog=%d\n", static_cast<int>(layer),
                             static_cast<int>(program));
            ResetBindings();
            return;
        }
        if (javaActive && PERLIN_ENV("PERLIN_JAVA_DBG")) {
            static int n = 0;
            if (n++ % 300 == 0)
                std::fprintf(stderr, "[javadbg] draw layer=%d prog=%d runs=%zu pid=%d\n",
                             static_cast<int>(layer), static_cast<int>(program), runs.size(),
                             ProgramIndex(program));
        }
        for (const gpu::ChunkMeshStore::Run& r : runs) {
            const Source vertices{meshes.Vertices(r.page), 0};
            const Source indices{meshes.Indices(r.page, layer), 0};
            Push(view, templ, vertices, &indices, rhi::IndexType::U32, r.first, r.count);
        }
        if (javaActive && PERLIN_ENV("PERLIN_JAVA_DBG") && layer == RenderLayer::Solid && !runs.empty()) {
            static int q = 0;
            if (q++ < 3) {
                std::string msg = "[javadbg] solid runs=" + std::to_string(runs.size());
                for (size_t i = 0; i < runs.size() && i < 6; i++)
                    msg += " (p" + std::to_string(runs[i].page) + " f" + std::to_string(runs[i].first) +
                           " c" + std::to_string(runs[i].count) + ")";
                std::fprintf(stderr, "%s\n", msg.c_str());
            }
        }
        ResetBindings();
    }

    void RenderContext_rhi::DrawScreenTint() {
        const float* t = frame.screenTint;
        if (t[3] <= 0.001f || !screenTri)
            return;
        uni.Vec4("u_modulator", t[0], t[1], t[2], t[3]);
        DrawState state = Base(Program::Screen, false, false, rhi::CompareOp::LessEqual);
        state.layout = gpu::VertexLayout::Position;
        state.mask = rhi::ColorMask::RGB;
        state.blend = true;
        state.srcColor = rhi::BlendFactor::SrcAlpha;
        state.dstColor = rhi::BlendFactor::InvSrcAlpha;
        state.srcAlpha = rhi::BlendFactor::One;
        state.dstAlpha = rhi::BlendFactor::InvSrcAlpha;
        Submit(View::Late, state, {screenTri, 0}, nullptr, rhi::IndexType::U32, 0, 3);
    }

    void RenderContext_rhi::DrawChunkMeshes(const Frustum& frustum, float camX, float camZ) {
        if (!initialized || !frameOpen)
            return;
        const bool pack = PackRunning();
        if (!pack)
            DrawSky();
        meshes.ReleaseEmptyPages();
        if (meshes.Empty()) {
            if (pack) {
                jpack->ClearGeometry();
                jpackGathered = true;
            }
            DrawClouds();
            DrawScreenTint();
            return;
        }
        if (javaActive && PERLIN_ENV("PERLIN_JAVA_DBG")) {
            static int c = 0;
            if (c++ % 300 == 0) {
                size_t solidChunks = 0, total = 0;
                for (const auto& [key, g] : meshes.ChunkMap()) {
                    total++;
                    if (g.page >= 0 && g.layerCount[0])
                        solidChunks++;
                }
                std::fprintf(stderr, "[javadbg] census chunks=%zu solid=%zu\n", total, solidChunks);
            }
        }
        meshes.Select(frustum, camX, frame.cameraPosition[1], camZ, chunkCullRadius, caveCulling,
                      frame.timeSeconds);
        if (pack) {
            // the pack draws terrain, sky and shadows itself when the frame executes
            jpack->GatherGeometry(camX, camZ);
            jpackGathered = true;
            if (jpack->VanillaClouds())
                DrawClouds();
            DrawScreenTint();
            return;
        }
        RenderShadows(frame.cameraPosition[0], frame.cameraPosition[1], frame.cameraPosition[2]);
        DrawLayer(RenderLayer::Solid, Program::TerrainSolid, View::World);
        DrawLayer(RenderLayer::Cutout, Program::TerrainCutout, View::World);
        DrawLayer(RenderLayer::CutoutCull, Program::TerrainCutout, View::World);
        DrawLayer(RenderLayer::Translucent, Program::TerrainTranslucent, View::Late);
        DrawLayer(RenderLayer::TranslucentCull, Program::TerrainTranslucent, View::Late);
        DrawClouds();
        DrawScreenTint();
    }

    namespace {
        glm::mat4 FromArray(const float m[16]) {
            glm::mat4 out;
            std::memcpy(&out[0][0], m, sizeof(float) * 16);
            return out;
        }
    }

    void RenderContext_rhi::DrawSky() {
        const SkyFrame& sky = frame.sky;
        if (!sky.enabled)
            return;
        const glm::mat4 view = FromArray(frame.skyViewProjection);
        constexpr float kHalfPi = 1.57079632679489661923f;
        const auto matrix = [&](const glm::mat4& m) { SetMatrix(glm::value_ptr(m)); };
        const auto drawState = [&](Program program, gpu::VertexLayout layout) {
            DrawState s = Base(program, false, false, rhi::CompareOp::LessEqual);
            s.layout = layout;
            s.mask = kRgbA;
            return s;
        };

        matrix(view);
        uni.Vec4("u_modulator", sky.skyColor[0], sky.skyColor[1], sky.skyColor[2], 1.0f);
        if (cloudMapTex.used)
            BindTexture("s_clouds", cloudMapTex.handle, gpu::SamplerKind::PointRepeat);
        if (javaActive) {
            BindShadowSampling();
            BindJavaExtras();
            Submit(View::World, drawState(Program::Sky, gpu::VertexLayout::Position), {screenTri, 0}, nullptr,
                   rhi::IndexType::U32, 0, 3);
            ResetBindings();
            return;
        }
        {
            const Source indices{fanIdx, 0};
            Submit(View::World, drawState(Program::Sky, gpu::VertexLayout::Terrain), {topSky, 0}, &indices,
                   rhi::IndexType::U16, 0, skyFanIndexCount);
        }

        // converted pack draws its own horizon glow
        if (!javaActive && sky.sunrise[3] > 0.001f) {
            glm::mat4 pose = glm::rotate(view, kHalfPi, glm::vec3(1.0f, 0.0f, 0.0f));
            const float flip = std::sin(sky.sunAngle) < 0.0f ? 3.14159265358979323846f : 0.0f;
            pose = glm::rotate(pose, flip + kHalfPi, glm::vec3(0.0f, 0.0f, 1.0f));
            pose = glm::scale(pose, glm::vec3(1.0f, 1.0f, sky.sunrise[3]));
            matrix(pose);
            uni.Vec4("u_modulator", sky.sunrise[0], sky.sunrise[1], sky.sunrise[2], sky.sunrise[3]);
            DrawState s = drawState(Program::Sunrise, gpu::VertexLayout::Color);
            s.blend = true;
            s.srcColor = rhi::BlendFactor::SrcAlpha;
            s.dstColor = rhi::BlendFactor::InvSrcAlpha;
            s.srcAlpha = rhi::BlendFactor::One;
            s.dstAlpha = rhi::BlendFactor::InvSrcAlpha;
            const Source indices{sunriseIdx, 0};
            Submit(View::World, s, {sunriseVerts, 0}, &indices, rhi::IndexType::U16, 0, sunriseIndexCount);
        }

        const glm::mat4 celestial = glm::rotate(view, -kHalfPi, glm::vec3(0.0f, 1.0f, 0.0f));
        const auto body = [&](float angle, float scale) {
            glm::mat4 pose = glm::rotate(celestial, angle, glm::vec3(1.0f, 0.0f, 0.0f));
            pose = glm::translate(pose, glm::vec3(0.0f, 100.0f, 0.0f));
            return glm::scale(pose, glm::vec3(scale, 1.0f, scale));
        };
        const Source quad{quadIdx, 0};
        if (!javaActive && sunTex.used) {
            matrix(body(sky.sunAngle, 30.0f));
            uni.Vec4("u_modulator", 1.0f, 1.0f, 1.0f, sky.rainBrightness);
            BindTexture("s_sky", sunTex.handle, gpu::SamplerKind::PointClamp);
            DrawState s = drawState(Program::Celestial, gpu::VertexLayout::Texture);
            ApplyMaterial(s, "sun_moon");
            Submit(View::World, s, {sunVerts, 0}, &quad, rhi::IndexType::U16, 0, 6);
        }
        if (!javaActive && moonTex.used) {
            matrix(body(sky.moonAngle, 20.0f));
            uni.Vec4("u_modulator", 1.0f, 1.0f, 1.0f, sky.rainBrightness);
            BindTexture("s_sky", moonTex.handle, gpu::SamplerKind::PointClamp);
            DrawState s = drawState(Program::Celestial, gpu::VertexLayout::Texture);
            ApplyMaterial(s, "sun_moon");
            Submit(View::World, s,
                   {moonVerts, static_cast<uint64_t>(sky.moonPhase) * 4 * sizeof(TextureVertex)}, &quad,
                   rhi::IndexType::U16, 0, 6);
        }
        if (!javaActive && sky.starBrightness > 0.0f) {
            matrix(glm::rotate(celestial, sky.starAngle, glm::vec3(1.0f, 0.0f, 0.0f)));
            uni.Vec4("u_modulator", sky.starBrightness, sky.starBrightness, sky.starBrightness,
                     sky.starBrightness);
            DrawState s = drawState(Program::Star, gpu::VertexLayout::Position);
            ApplyMaterial(s, "stars");
            const Source indices{starIdx, 0};
            Submit(View::World, s, {starVerts, 0}, &indices, rhi::IndexType::U16, 0, starIndexCount);
        }
        if (sky.darkDisc) {
            matrix(glm::translate(view, glm::vec3(0.0f, 12.0f, 0.0f)));
            uni.Vec4("u_modulator", 0.0f, 0.0f, 0.0f, 1.0f);
            const Source indices{fanIdx, 0};
            Submit(View::World, drawState(Program::Sky, gpu::VertexLayout::Terrain), {bottomSky, 0}, &indices,
                   rhi::IndexType::U16, 0, skyFanIndexCount);
        }
        SetMatrix(mvp);
    }

    void RenderContext_rhi::DrawClouds() {
        if (javaActive)
            return;
        const CloudDraw& cloudsIn = frame.cloud;
        CloudDraw clouds = cloudsIn;
        const bool pack = PackRunning();
        if (pack) {
            // iris: shaders.properties clouds=off|fast|fancy overrides the player's setting
            const std::string& mode = jpack->CloudSetting();
            if (mode == "off")
                return;
            if (mode == "fast")
                clouds.fancy = false;
            else if (mode == "fancy")
                clouds.fancy = true;
            if (cloudCpu.vertices.empty() && cloudBuilt && cloudIndexCount > 0)
                cloudBuilt = false; // rebuild once to keep a cpu copy
        }
        material::Registry::Instance().Configure(clouds.fancy);
        if (!clouds.enabled || cloudTexture.solid.empty())
            return;

        const CloudFrame now =
            ComputeCloudFrame(frame.cameraPosition[0], frame.cameraPosition[1], frame.cameraPosition[2],
                              clouds.height, clouds.gameTicks, cloudTexture);
        const auto upload = [&](const CloudMesh& mesh, const CloudFrame& builtFor, bool fancy, int range) {
            const uint32_t vertexCount = static_cast<uint32_t>(mesh.vertices.size());
            const uint32_t indexCount = static_cast<uint32_t>(mesh.indices.size());
            if (vertexCount > cloudVertexCapacity) {
                cloudVb.Destroy(gpu.Dev());
                cloudVertexCapacity = vertexCount + vertexCount / 4;
                cloudVb.Create(gpu.Dev(), cloudVertexCapacity, sizeof(TextureVertex),
                               rhi::BufferUsage::Vertex, "cloud vertices");
            }
            if (indexCount > cloudIndexCapacity) {
                cloudIb.Destroy(gpu.Dev());
                cloudIndexCapacity = indexCount + indexCount / 4;
                cloudIb.Create(gpu.Dev(), cloudIndexCapacity, sizeof(uint32_t), rhi::BufferUsage::Index,
                               "cloud indices");
            }
            if (vertexCount && indexCount) {
                gpu.WriteBuffer(cloudVb, 0, mesh.vertices.data(),
                                static_cast<uint64_t>(vertexCount) * sizeof(TextureVertex));
                gpu.WriteBuffer(cloudIb, 0, mesh.indices.data(),
                                static_cast<uint64_t>(indexCount) * sizeof(uint32_t));
            }
            cloudIndexCount = vertexCount && indexCount ? indexCount : 0;
            if (PackRunning())
                cloudCpu = mesh;
            else
                cloudCpu = {};
            cloudBuiltFrame = builtFor;
            cloudBuiltFancy = fancy;
            cloudBuiltRange = range;
            cloudBuilt = true;
        };
        if (cloudJobRunning && cloudJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const CloudMesh mesh = cloudJob.get();
            cloudJobRunning = false;
            upload(mesh, cloudJobFrame, clouds.fancy, clouds.rangeChunks);
        }
        const bool stale = !cloudBuilt || now.position != cloudBuiltFrame.position ||
                           clouds.fancy != cloudBuiltFancy || clouds.rangeChunks != cloudBuiltRange;
        const bool moved = now.cellX != cloudBuiltFrame.cellX || now.cellZ != cloudBuiltFrame.cellZ;
        if (stale) {
            if (cloudJobRunning) {
                cloudJob.wait();
                cloudJobRunning = false;
            }
            upload(BuildCloudMesh(cloudTexture, now, clouds.fancy, clouds.rangeChunks), now, clouds.fancy,
                   clouds.rangeChunks);
        } else if (moved && !cloudJobRunning) {
            cloudJobFrame = now;
            cloudJob = std::async(std::launch::async, [tex = cloudTexture, now, fancy = clouds.fancy,
                                                       range = clouds.rangeChunks] {
                return BuildCloudMesh(tex, now, fancy, range);
            });
            cloudJobRunning = true;
        }
        if (!cloudIndexCount || !cloudVb.Buffer() || !cloudIb.Buffer())
            return;

        const auto wrapDelta = [](int d, int w) {
            if (w <= 0)
                return d;
            d %= w;
            if (d > w / 2)
                d -= w;
            else if (d < -w / 2)
                d += w;
            return d;
        };
        const float cellDx =
            static_cast<float>(wrapDelta(now.cellX - cloudBuiltFrame.cellX, cloudTexture.width)) *
            kCloudCellSize;
        const float cellDz =
            static_cast<float>(wrapDelta(now.cellZ - cloudBuiltFrame.cellZ, cloudTexture.height)) *
            kCloudCellSize;
        if (pack) {
            if (cloudCpu.vertices.empty())
                return;
            // through the pack (gbuffers_clouds): vanilla bakes each face's shade into the vertex colour
            std::vector<UiVertex> verts;
            verts.reserve(cloudCpu.vertices.size());
            for (const TextureVertex& v : cloudCpu.vertices) {
                const float shade = v.u;
                const auto ch = [&](int i) {
                    return static_cast<uint32_t>(
                        std::lround(std::clamp(clouds.color[i] * shade, 0.0f, 1.0f) * 255.0f));
                };
                const uint32_t a =
                    static_cast<uint32_t>(std::lround(std::clamp(clouds.color[3], 0.0f, 1.0f) * 255.0f));
                verts.push_back(
                    {v.x, v.y, v.z, ch(0) | (ch(1) << 8) | (ch(2) << 16) | (a << 24), 0.0f, 0.0f});
            }
            float model[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            model[12] = -(now.xInCell + cellDx) + frame.cameraPosition[0];
            model[13] = now.relativeBottomY + frame.cameraPosition[1];
            model[14] = -(now.zInCell + cellDz) + frame.cameraPosition[2];
            gpu::JavaPack::ExternalDraw d;
            d.kind = gpu::JavaPack::ExternalDraw::Kind::Clouds;
            d.alphaRef = 0.1f;
            gpu::JavaPack::BuildVertices(verts.data(), verts.size(), cloudCpu.indices.data(),
                                         cloudCpu.indices.size(), model, 0, 15, d.vertices, nullptr,
                                         frame.cameraPosition);
            d.count = static_cast<uint32_t>(d.vertices.size() / gpu::kPackVertexBytes);
            d.depthTest = true;
            d.depthWrite = clouds.fancy;
            d.blend = true;
            jpack->QueueDraw(std::move(d));
            return;
        }
        uni.Vec4("u_cloudOffset", -(now.xInCell + cellDx), now.relativeBottomY, -(now.zInCell + cellDz),
                 0.0f);
        uni.Set("u_cloudColor", clouds.color, 4);
        SetMatrix(skyMvp);
        DrawState s = Base(Program::Cloud, true, true, rhi::CompareOp::LessEqual);
        s.layout = gpu::VertexLayout::Texture;
        s.mask = rhi::ColorMask::RGB;
        ApplyMaterial(s, "clouds");
        if (clouds.fancy)
            s.cull = cullMode == rhi::CullMode::Back    ? rhi::CullMode::Front
                     : cullMode == rhi::CullMode::Front ? rhi::CullMode::Back
                                                        : rhi::CullMode::None;
        const Source indices{cloudIb.Buffer(), 0};
        Submit(View::Late, s, {cloudVb.Buffer(), 0}, &indices, rhi::IndexType::U32, 0, cloudIndexCount);
        SetMatrix(mvp);
    }
}
