#include "client/renderer/gpu/RenderContext_rhi.hpp"

#include "client/renderer/CustomSky.hpp"

#include "client/renderer/MaterialDb.hpp"
#include "util/Env.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>
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

        constexpr conduit::color_mask kRgbA = conduit::color_mask::all;
    }

    int RenderContext_rhi::MaxAtlasLayers(int tilePx) const {
        const int limit =
            initialized ? std::max<int>(
                              1, static_cast<int>(const_cast<gpu::Gpu&>(gpu).Caps().limits.max_texture_layers))
                        : kAtlasPageLayers;
        const double perLayer =
            static_cast<double>(std::max(1, tilePx)) * std::max(1, tilePx) * 4.0 * 4.0 / 3.0;
        const double budget = 1.3e9;

        const int total = limit >= kAtlasPageLayers ? kAtlasPageLayers * kAtlasMaxPages : limit;
        return std::max(256, std::min(total, static_cast<int>(budget / perLayer)));
    }

    int RenderContext_rhi::AtlasLayersFor(int tiles, int tilePx) const {
        const double perLayer =
            static_cast<double>(std::max(1, tilePx)) * std::max(1, tilePx) * 4.0 * 4.0 / 3.0;

        if (perLayer * atlasPageCap <= 96.0e6)
            return atlasPageCap;
        return std::min(atlasPageCap, (tiles + 255) / 256 * 256 + 256);
    }

    bool RenderContext_rhi::MakeAtlasPage(int page, int tiles, int tilePx) {
        const int layers = AtlasLayersFor(tiles, tilePx);
        TexRec& rec = AtlasPage(page);
        FreeTexture(rec);
        const std::string name = "block atlas " + std::to_string(page);
        if (!MakeTexture(rec, static_cast<uint32_t>(tilePx), static_cast<uint32_t>(tilePx),
                         static_cast<uint32_t>(layers), static_cast<uint32_t>(MipCount(tilePx)),
                         conduit::format::rgba8_unorm, gpu::SamplerKind::PointMipClamp, name.c_str())) {
            Log("render", "atlas page {} failed ({} layers of {}px)", page, layers, tilePx);
            return false;
        }
        return true;
    }

    void RenderContext_rhi::FreeAtlas() {
        for (int page = 0; page < kAtlasMaxPages; page++)
            FreeTexture(AtlasPage(page));
    }

    void RenderContext_rhi::BindAtlas() {
        static constexpr const char* names[kAtlasMaxPages] = {"s_atlas", "s_atlas2", "s_atlas3", "s_atlas4"};
        for (int page = 0; page < kAtlasMaxPages; page++) {
            const TexRec& rec = AtlasPage(page);
            BindTexture(names[page], rec.used ? rec.handle : gpu.WhiteArray(), gpu::SamplerKind::PointMipClamp);
        }
    }

    namespace {

        std::vector<std::vector<std::vector<uint8_t>>> BuildMipsParallel(const uint8_t* rgba, int tilePx, int first,
                                                                         int count) {
            std::vector<std::vector<std::vector<uint8_t>>> built(static_cast<size_t>(std::max(count, 0)));
            const size_t stride = static_cast<size_t>(tilePx) * 4;
            const size_t tileBytes = static_cast<size_t>(tilePx) * tilePx * 4;
            std::atomic<int> next{0};
            const auto work = [&] {
                for (int i; (i = next.fetch_add(1)) < count;)
                    built[static_cast<size_t>(i)] =
                        BuildTileMips(tilePx, rgba + tileBytes * static_cast<size_t>(first + i), stride);
            };
            const unsigned workers = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
            std::vector<std::thread> threads;
            for (unsigned i = 1; i < workers && count > 64; i++)
                threads.emplace_back(work);
            work();
            for (std::thread& t : threads)
                t.join();
            return built;
        }
    }

    void RenderContext_rhi::UploadAtlas(const uint8_t* rgba, int tilePx, int tiles) {
        if (!initialized || !rgba || tilePx <= 0 || tiles <= 0)
            return;
        animatedTiles.clear();
        const int limit = std::max<int>(1, static_cast<int>(gpu.Caps().limits.max_texture_layers));
        atlasPageCap = std::min(limit, kAtlasPageLayers);
        const int capacity = limit >= kAtlasPageLayers ? kAtlasPageLayers * kAtlasMaxPages : limit;
        const int count = std::min(tiles, capacity);
        const int pages = (count + atlasPageCap - 1) / atlasPageCap;
        const size_t tileBytes = static_cast<size_t>(tilePx) * tilePx * 4;
        const auto built = BuildMipsParallel(rgba, tilePx, 0, count);
        FreeAtlas();
        tileSize = tilePx;
        atlasTiles = 0;
        for (int page = 0; page < pages; page++) {
            const int first = page * atlasPageCap;
            const int end = std::min(count, first + atlasPageCap);

            if (!MakeAtlasPage(page, end - first, tilePx))
                break;
            const TexRec& rec = AtlasPage(page);
            for (int tile = first; tile < end; tile++) {
                const auto& mips = built[static_cast<size_t>(tile)];
                for (size_t l = 0; l < mips.size(); l++) {
                    const uint32_t w = static_cast<uint32_t>(std::max(1, tilePx >> static_cast<int>(l)));
                    gpu.WriteTexture(rec.handle, static_cast<uint32_t>(tile - first), 0, 0, w, w, mips[l].data(),
                                     0, static_cast<uint32_t>(l));
                }
            }
            atlasTiles = end;
        }
        if (!atlas.used)
            return;
        atlasSource.assign(rgba, rgba + tileBytes * static_cast<size_t>(atlasTiles));
        if (jpack)
            jpack->SetAtlas(rgba, tilePx, atlasTiles);
    }

    bool RenderContext_rhi::AppendAtlasTiles(const uint8_t* rgba, int tilePx, int first, int tiles) {
        const int capacity = atlasPageCap >= kAtlasPageLayers ? kAtlasPageLayers * kAtlasMaxPages : atlasPageCap;
        if (!initialized || !atlas.used || !rgba || tilePx != tileSize || first != atlasTiles || tiles <= first ||
            tiles > capacity)
            return false;

        const int firstPage = first / atlasPageCap, lastPage = (tiles - 1) / atlasPageCap;
        for (int page = firstPage; page <= lastPage; page++) {
            const TexRec& rec = AtlasPage(page);
            if (rec.used && static_cast<int>(rec.layers) < std::min(tiles - page * atlasPageCap, atlasPageCap))
                return false;
        }
        for (int page = firstPage; page <= lastPage; page++)
            if (!AtlasPage(page).used &&
                !MakeAtlasPage(page, std::min(tiles - page * atlasPageCap, atlasPageCap), tilePx))
                return false;
        const size_t tileBytes = static_cast<size_t>(tilePx) * tilePx * 4;
        const auto built = BuildMipsParallel(rgba, tilePx, first, tiles - first);
        for (int tile = first; tile < tiles; tile++) {
            const TexRec& rec = AtlasPage(tile / atlasPageCap);
            const auto& mips = built[static_cast<size_t>(tile - first)];
            for (size_t l = 0; l < mips.size(); l++) {
                const uint32_t w = static_cast<uint32_t>(std::max(1, tilePx >> static_cast<int>(l)));
                gpu.WriteTexture(rec.handle, static_cast<uint32_t>(tile % atlasPageCap), 0, 0, w, w,
                                 mips[l].data(), 0, static_cast<uint32_t>(l));
            }
        }
        atlasSource.insert(atlasSource.end(), rgba + tileBytes * static_cast<size_t>(first),
                           rgba + tileBytes * static_cast<size_t>(tiles));
        atlasTiles = tiles;
        if (jpack)
            jpack->SetAtlas(atlasSource.data(), tilePx, tiles);
        return true;
    }

    void RenderContext_rhi::UpdateAtlasRegion(int tile, const uint8_t* rgba) {
        if (!initialized || !atlas.used || !rgba || tile < 0)
            return;
        const int pageIndex = tile / atlasPageCap;
        if (pageIndex >= kAtlasMaxPages)
            return;
        const TexRec& page = AtlasPage(pageIndex);
        if (!page.used || tile % atlasPageCap >= static_cast<int>(page.layers))
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
            gpu.WriteTexture(page.handle, static_cast<uint32_t>(tile % atlasPageCap), 0, 0, w, w, mips[l].data(), 0,
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
                            1, 1, conduit::format::rgba8_unorm, sampler, name))
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

    void RenderContext_rhi::UploadEndSkyTexture(const Texture& endSky) {
        if (!initialized)
            return;
        FreeTexture(endSkyTex);
        endSkySource = endSky;
        if (endSky.width <= 0 || endSky.height <= 0)
            return;
        if (MakeTexture(endSkyTex, static_cast<uint32_t>(endSky.width), static_cast<uint32_t>(endSky.height), 1, 1,
                        conduit::format::rgba8_unorm, gpu::SamplerKind::PointRepeat, "end sky"))
            gpu.WriteTexture(endSkyTex.handle, 0, 0, 0, endSkyTex.w, endSkyTex.h, endSky.pixels.data());
    }

    void RenderContext_rhi::UploadCustomSkyTextures(const std::vector<Texture>& textures) {
        if (!initialized)
            return;
        for (TexRec& t : customSkyTex)
            FreeTexture(t);
        customSkyTex.clear();
        customSkyTex.resize(textures.size());
        for (size_t i = 0; i < textures.size(); i++) {
            const Texture& t = textures[i];
            if (t.width <= 0 || t.height <= 0 || t.pixels.empty())
                continue;
            // nearest, as OptiFine binds them: a 3x2 sheet must not bleed between its cells
            if (MakeTexture(customSkyTex[i], static_cast<uint32_t>(t.width), static_cast<uint32_t>(t.height), 1, 1,
                            conduit::format::rgba8_unorm, gpu::SamplerKind::PointClamp, "custom sky"))
                gpu.WriteTexture(customSkyTex[i].handle, 0, 0, 0, customSkyTex[i].w, customSkyTex[i].h,
                                 t.pixels.data());
        }
    }

    // Blender.setupBlend: the factors and the colour each method multiplies the texture by
    void RenderContext_rhi::ApplySkyBlend(DrawState& s, uint8_t blend, float b, float mod[4]) {
            using F = conduit::blend_factor;
            s.blend = true;
            s.mask = conduit::color_mask::rgb;
            s.srcAlpha = F::zero;
            s.dstAlpha = F::one;
            mod[0] = mod[1] = mod[2] = 1.0f;
            mod[3] = b;
            switch (static_cast<sky::Blend>(blend)) {
            case sky::Blend::Alpha:
                s.srcColor = F::src_alpha, s.dstColor = F::inv_src_alpha;
                break;
            case sky::Blend::Add:
                s.srcColor = F::src_alpha, s.dstColor = F::one;
                break;
            case sky::Blend::Subtract:
                s.srcColor = F::inv_dst_color, s.dstColor = F::zero;
                mod[0] = mod[1] = mod[2] = b, mod[3] = 1.0f;
                break;
            case sky::Blend::Multiply:
                s.srcColor = F::dst_color, s.dstColor = F::inv_src_alpha;
                mod[0] = mod[1] = mod[2] = b;
                break;
            case sky::Blend::Dodge:
                s.srcColor = F::one, s.dstColor = F::one;
                mod[0] = mod[1] = mod[2] = b, mod[3] = 1.0f;
                break;
            case sky::Blend::Burn:
                s.srcColor = F::zero, s.dstColor = F::inv_src_color;
                mod[0] = mod[1] = mod[2] = b, mod[3] = 1.0f;
                break;
            case sky::Blend::Screen:
                s.srcColor = F::one, s.dstColor = F::inv_src_color;
                mod[0] = mod[1] = mod[2] = b, mod[3] = 1.0f;
                break;
            case sky::Blend::Overlay:
                s.srcColor = F::dst_color, s.dstColor = F::src_color;
                mod[0] = mod[1] = mod[2] = b, mod[3] = 1.0f;
                break;
            case sky::Blend::Replace:
                s.blend = false;
                break;
            }
    }

    void RenderContext_rhi::DrawCustomSky(const glm::mat4& view, const glm::mat4& celestial) {
        const SkyFrame& sky = frame.sky;
        const Source quad{quadIdx, 0};
        for (int i = 0; i < std::min(sky.customCount, SkyFrame::kMaxCustomLayers); i++) {
            const SkyFrame::CustomLayer& layer = sky.custom[i];
            const float b = layer.brightness;
            DrawState s = Base(Program::Celestial, false, false, conduit::compare_op::less_equal);
            s.layout = gpu::VertexLayout::Texture;
            float mod[4];
            ApplySkyBlend(s, layer.blend, b, mod);
            uni.Vec4("u_modulator", mod[0], mod[1], mod[2], mod[3]);
            glm::mat4 rotation;
            std::memcpy(&rotation[0][0], layer.rotation, sizeof(layer.rotation));
            if (layer.cubemap) {
                SetMatrix(glm::value_ptr(view));
                for (int face = 0; face < 6; face++) {
                    const size_t slot = static_cast<size_t>(layer.texture + face);
                    if (slot >= customSkyTex.size() || !customSkyTex[slot].used)
                        continue;
                    BindTexture("s_sky", customSkyTex[slot].handle, gpu::SamplerKind::LinearClamp);
                    Submit(View::World, s, {customSkyCube, static_cast<uint64_t>(face) * 4 * sizeof(TextureVertex)},
                           &quad, conduit::index_type::u16, 0, 6);
                }
                continue;
            }
            const size_t slot = static_cast<size_t>(layer.texture);
            if (slot >= customSkyTex.size() || !customSkyTex[slot].used)
                continue;
            const glm::mat4 pose = celestial * rotation;
            SetMatrix(glm::value_ptr(pose));
            BindTexture("s_sky", customSkyTex[slot].handle, gpu::SamplerKind::PointClamp);
            for (int side = 0; side < 6; side++)
                Submit(View::World, s, {customSkyBox, static_cast<uint64_t>(side) * 4 * sizeof(TextureVertex)}, &quad,
                       conduit::index_type::u16, 0, 6);
        }
    }

    void RenderContext_rhi::DrawEndSky(const glm::mat4& view) {
        if (endSkyTex.used) {
            SetMatrix(glm::value_ptr(view));
            // renderSkyEnd: vertex colour 40/255, alpha blended, no depth
            uni.Vec4("u_modulator", 40.0f / 255.0f, 40.0f / 255.0f, 40.0f / 255.0f, 1.0f);
            BindTexture("s_sky", endSkyTex.handle, gpu::SamplerKind::PointRepeat);
            DrawState s = Base(Program::Celestial, false, false, conduit::compare_op::less_equal);
            s.layout = gpu::VertexLayout::Texture;
            s.mask = kRgbA;
            s.blend = true;
            s.srcColor = conduit::blend_factor::src_alpha;
            s.dstColor = conduit::blend_factor::inv_src_alpha;
            s.srcAlpha = conduit::blend_factor::one;
            s.dstAlpha = conduit::blend_factor::zero;
            const Source quad{quadIdx, 0};
            for (int face = 0; face < 6; face++)
                Submit(View::World, s, {endSkyBox, static_cast<uint64_t>(face) * 4 * sizeof(TextureVertex)}, &quad,
                       conduit::index_type::u16, 0, 6);
        }
        if (frame.sky.customCount > 0) {
            constexpr float kHalfPi = 1.57079632679489661923f;
            DrawCustomSky(view, glm::rotate(view, -kHalfPi, glm::vec3(0.0f, 1.0f, 0.0f)));
        }
        SetMatrix(mvp);
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
            MakeTexture(tileFlagsTex, kW, kH, 1, 1, conduit::format::rgba8_unorm, gpu::SamplerKind::PointClamp,
                        "tile flags");
        if (tileFlagsTex.used)
            gpu.WriteTexture(tileFlagsTex.handle, 0, 0, 0, kW, kH, rgba.data());
    }

    void RenderContext_rhi::UploadCrosshair(const Texture& image) {
        if (!initialized || image.width < 1 || image.height < 1 ||
            image.pixels.size() < static_cast<size_t>(image.width) * image.height * 4)
            return;

        iconSource = image;
        if (MakeTexture(crosshair, static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height), 1, 1,
                        conduit::format::rgba8_unorm, gpu::SamplerKind::PointClamp, "crosshair")) {
            gpu.WriteTexture(crosshair.handle, 0, 0, 0, crosshair.w, crosshair.h, image.pixels.data());
            crosshairPx = image.width;
        }
    }

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
        FreeAtlas();
        for (TexRec* texture : {&sunTex, &moonTex, &endSkyTex, &cloudMapTex, &tileFlagsTex, &crosshair})
            FreeTexture(*texture);
        animatedTiles.clear();
        std::vector<uint8_t>().swap(atlasSource);
        std::vector<uint8_t>().swap(tileFlagData);
        sunSource = moonSource = endSkySource = cloudSource = iconSource = Texture{};
        cloudTexture = {};
        cloudBuilt = false;
        atlasTiles = crosshairPx = 0;
    }

    void RenderContext_rhi::ClearChunkMeshes() {
        if (initialized)
            meshes.Clear();
    }

    RenderContext_rhi::DrawState RenderContext_rhi::PassState(RenderLayer layer) const {
        DrawState s = Base(Program::TerrainSolid, true, true, conduit::compare_op::less);
        s.layout = gpu::VertexLayout::Terrain;
        s.mask = conduit::color_mask::rgb;
        const auto blend = [&] {
            s.blend = true;
            s.srcColor = conduit::blend_factor::src_alpha;
            s.dstColor = conduit::blend_factor::inv_src_alpha;
            s.srcAlpha = conduit::blend_factor::one;
            s.dstAlpha = conduit::blend_factor::inv_src_alpha;
        };
        switch (layer) {
        case RenderLayer::Solid:
            s.mask = kRgbA;
            s.cull = cullMode;
            break;
        case RenderLayer::Cutout:
            s.depthOp = conduit::compare_op::less_equal;
            s.mask = kRgbA;
            break;
        case RenderLayer::CutoutCull:
            s.depthOp = conduit::compare_op::less_equal;
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
        const bool onGpu = gpuCullFrame && gpu::GpuChunkCull::Handles(layer);
        if (onGpu)
            runs.clear();
        else
            meshes.CollectRuns(layer, ordered, runs);
        if (javaActive && PERLIN_ENV("PERLIN_JAVA_DBG")) {
            static int m = 0;
            if (m++ < 10)
                std::fprintf(stderr, "[javadbg] runs layer=%d count=%zu\n", static_cast<int>(layer),
                             runs.size());
        }
        if (onGpu ? cull.Buckets(layer).empty() : runs.empty())
            return;
        DrawState state = PassState(layer);
        state.program = program;
        state.layout = gpu::VertexLayout::Terrain;

        SetMatrix(mvp);
        BindAtlas();
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
        const bool multi = UseMultiDraw();
        if (onGpu) {
            for (const gpu::GpuChunkCull::Bucket& b : cull.Buckets(layer)) {
                DrawCall dc = templ;
                dc.vertices = meshes.Vertices(b.page);
                dc.vertexOffset = 0;
                dc.indices = meshes.Indices(b.page, layer);
                dc.indexOffset = 0;
                dc.indexType = conduit::index_type::u32;
                dc.indirect = cull.Commands();
                dc.indirectOffset = static_cast<uint64_t>(b.firstCommand) * sizeof(conduit::draw_indexed_indirect_command);
                dc.indirectDraws = b.commands;
                views[static_cast<size_t>(view)].draws.push_back(dc);
            }
        }
        for (size_t i = 0; i < runs.size();) {
            size_t j = i + 1;
            while (multi && j < runs.size() && runs[j].page == runs[i].page)
                j++;
            const uint32_t page = runs[i].page;
            if (!multi || !PushRuns(view, templ, meshes.Vertices(page), meshes.Indices(page, layer), runs, i, j)) {
                for (size_t k = i; k < j; k++) {
                    const Source vertices{meshes.Vertices(runs[k].page), 0};
                    const Source indices{meshes.Indices(runs[k].page, layer), 0};
                    Push(view, templ, vertices, &indices, conduit::index_type::u32, runs[k].first, runs[k].count);
                }
            }
            i = j;
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
        // Java shader packs draw their own world; a flat red quad on top of their composite looks like
        // a bug (Java edition has no full-screen hit tint), so the pack path never gets one.
        if (t[3] <= 0.001f || !screenTri || PackRunning())
            return;
        uni.Vec4("u_modulator", t[0], t[1], t[2], t[3]);
        DrawState state = Base(Program::Screen, false, false, conduit::compare_op::less_equal);
        state.layout = gpu::VertexLayout::Position;
        state.mask = conduit::color_mask::rgb;
        state.blend = true;
        state.srcColor = conduit::blend_factor::src_alpha;
        state.dstColor = conduit::blend_factor::inv_src_alpha;
        state.srcAlpha = conduit::blend_factor::one;
        state.dstAlpha = conduit::blend_factor::inv_src_alpha;
        Submit(View::Late, state, {screenTri, 0}, nullptr, conduit::index_type::u32, 0, 3);
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

            jpack->SetViewFrustum(frustum);
            jpack->GatherGeometry(camX, camZ);
            jpackGathered = true;
            if (jpack->VanillaClouds())
                DrawClouds();
            DrawScreenTint();
            return;
        }
        gpuCullFrame = cull.Ready() && UseMultiDraw() && cull.Prepare(meshes, frustum, camX, camZ, chunkCullRadius);
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
        if (!sky.enabled) {
            if (sky.endSky && !javaActive)
                DrawEndSky(FromArray(frame.skyViewProjection));
            return;
        }
        const glm::mat4 view = FromArray(frame.skyViewProjection);
        constexpr float kHalfPi = 1.57079632679489661923f;
        const auto matrix = [&](const glm::mat4& m) { SetMatrix(glm::value_ptr(m)); };
        const auto drawState = [&](Program program, gpu::VertexLayout layout) {
            DrawState s = Base(program, false, false, conduit::compare_op::less_equal);
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
                   conduit::index_type::u32, 0, 3);
            ResetBindings();
            return;
        }
        {
            const Source indices{fanIdx, 0};
            Submit(View::World, drawState(Program::Sky, gpu::VertexLayout::Terrain), {topSky, 0}, &indices,
                   conduit::index_type::u16, 0, skyFanIndexCount);
        }

        if (!javaActive && sky.sunrise[3] > 0.001f) {
            glm::mat4 pose = glm::rotate(view, kHalfPi, glm::vec3(1.0f, 0.0f, 0.0f));
            const float flip = std::sin(sky.sunAngle) < 0.0f ? 3.14159265358979323846f : 0.0f;
            pose = glm::rotate(pose, flip + kHalfPi, glm::vec3(0.0f, 0.0f, 1.0f));
            pose = glm::scale(pose, glm::vec3(1.0f, 1.0f, sky.sunrise[3]));
            matrix(pose);
            uni.Vec4("u_modulator", sky.sunrise[0], sky.sunrise[1], sky.sunrise[2], sky.sunrise[3]);
            DrawState s = drawState(Program::Sunrise, gpu::VertexLayout::Color);
            s.blend = true;
            s.srcColor = conduit::blend_factor::src_alpha;
            s.dstColor = conduit::blend_factor::inv_src_alpha;
            s.srcAlpha = conduit::blend_factor::one;
            s.dstAlpha = conduit::blend_factor::inv_src_alpha;
            const Source indices{sunriseIdx, 0};
            Submit(View::World, s, {sunriseVerts, 0}, &indices, conduit::index_type::u16, 0, sunriseIndexCount);
        }

        const glm::mat4 celestial = glm::rotate(view, -kHalfPi, glm::vec3(0.0f, 1.0f, 0.0f));
        if (sky.customCount > 0)
            DrawCustomSky(view, celestial);
        const auto body = [&](float angle, float scale) {
            glm::mat4 pose = glm::rotate(celestial, angle, glm::vec3(1.0f, 0.0f, 0.0f));
            pose = glm::translate(pose, glm::vec3(0.0f, 100.0f, 0.0f));
            return glm::scale(pose, glm::vec3(scale, 1.0f, scale));
        };
        const Source quad{quadIdx, 0};
        // MCPatcher's setupCelestialObject: a pack's sun or moon texture with its own blend, at the rain-dimmed
        // brightness the vanilla one uses
        const auto replaced = [&](int index, uint8_t blend, DrawState& s) {
            if (index < 0 || static_cast<size_t>(index) >= customSkyTex.size() || !customSkyTex[index].used)
                return false;
            float mod[4];
            ApplySkyBlend(s, blend, sky.rainBrightness, mod);
            uni.Vec4("u_modulator", mod[0], mod[1], mod[2], mod[3]);
            BindTexture("s_sky", customSkyTex[index].handle, gpu::SamplerKind::PointClamp);
            return true;
        };
        if (!javaActive && (sunTex.used || sky.sunTexture >= 0)) {
            matrix(body(sky.sunAngle, 30.0f));
            DrawState s = drawState(Program::Celestial, gpu::VertexLayout::Texture);
            if (!replaced(sky.sunTexture, sky.sunBlend, s)) {
                uni.Vec4("u_modulator", 1.0f, 1.0f, 1.0f, sky.rainBrightness);
                BindTexture("s_sky", sunTex.handle, gpu::SamplerKind::PointClamp);
                ApplyMaterial(s, "sun_moon");
            }
            Submit(View::World, s, {sunVerts, 0}, &quad, conduit::index_type::u16, 0, 6);
        }
        if (!javaActive && (moonTex.used || sky.moonTexture >= 0)) {
            matrix(body(sky.moonAngle, 20.0f));
            DrawState s = drawState(Program::Celestial, gpu::VertexLayout::Texture);
            if (!replaced(sky.moonTexture, sky.moonBlend, s)) {
                uni.Vec4("u_modulator", 1.0f, 1.0f, 1.0f, sky.rainBrightness);
                BindTexture("s_sky", moonTex.handle, gpu::SamplerKind::PointClamp);
                ApplyMaterial(s, "sun_moon");
            }
            Submit(View::World, s,
                   {moonVerts, static_cast<uint64_t>(sky.moonPhase) * 4 * sizeof(TextureVertex)}, &quad,
                   conduit::index_type::u16, 0, 6);
        }
        if (!javaActive && sky.starBrightness > 0.0f) {
            matrix(glm::rotate(celestial, sky.starAngle, glm::vec3(1.0f, 0.0f, 0.0f)));
            uni.Vec4("u_modulator", sky.starBrightness, sky.starBrightness, sky.starBrightness,
                     sky.starBrightness);
            DrawState s = drawState(Program::Star, gpu::VertexLayout::Position);
            ApplyMaterial(s, "stars");
            const Source indices{starIdx, 0};
            Submit(View::World, s, {starVerts, 0}, &indices, conduit::index_type::u16, 0, starIndexCount);
        }
        if (sky.darkDisc) {
            matrix(glm::translate(view, glm::vec3(0.0f, 12.0f, 0.0f)));
            uni.Vec4("u_modulator", 0.0f, 0.0f, 0.0f, 1.0f);
            const Source indices{fanIdx, 0};
            Submit(View::World, drawState(Program::Sky, gpu::VertexLayout::Terrain), {bottomSky, 0}, &indices,
                   conduit::index_type::u16, 0, skyFanIndexCount);
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

            const std::string& mode = jpack->CloudSetting();
            if (mode == "off")
                return;
            if (mode == "fast")
                clouds.fancy = false;
            else if (mode == "fancy")
                clouds.fancy = true;
            if (cloudCpu.vertices.empty() && cloudBuilt && cloudIndexCount > 0)
                cloudBuilt = false;
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
                               conduit::buffer_usage::vertex, "cloud vertices");
            }
            if (indexCount > cloudIndexCapacity) {
                cloudIb.Destroy(gpu.Dev());
                cloudIndexCapacity = indexCount + indexCount / 4;
                cloudIb.Create(gpu.Dev(), cloudIndexCapacity, sizeof(uint32_t), conduit::buffer_usage::index,
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
        DrawState s = Base(Program::Cloud, true, true, conduit::compare_op::less_equal);
        s.layout = gpu::VertexLayout::Texture;
        s.mask = conduit::color_mask::rgb;
        ApplyMaterial(s, "clouds");
        if (clouds.fancy)
            s.cull = cullMode == conduit::cull_mode::back    ? conduit::cull_mode::front
                     : cullMode == conduit::cull_mode::front ? conduit::cull_mode::back
                                                        : conduit::cull_mode::none;
        const Source indices{cloudIb.Buffer(), 0};
        Submit(View::Late, s, {cloudVb.Buffer(), 0}, &indices, conduit::index_type::u32, 0, cloudIndexCount);
        SetMatrix(mvp);
    }
}
