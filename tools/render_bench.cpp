// cpu cost of the terrain frame on the null backend: a generated world is meshed once, then the camera flies a loop
// while a few chunks are remeshed every frame (streaming). the null device records but draws nothing, so the times
// are what the renderer spends on the cpu: selection, culling setup, uploads and command recording.
// usage: render_bench <pack> [radius] [frames] [remesh per frame]
#include "client/renderer/Frustum.hpp"
#include "client/renderer/Tessellator.hpp"
#include "client/renderer/gpu/RenderContext_rhi.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numeric>
#include <vector>

#if __has_include("client/renderer/gpu/GpuTerrain.hpp")
#define BENCH_HAS_TERRAIN 1
#endif

using namespace perlin;

namespace {
    uint32_t Hash(int x, int y, int z) {
        uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u +
                     static_cast<uint32_t>(z) * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }
    float Value(float x, float z) {
        const int ix = static_cast<int>(std::floor(x)), iz = static_cast<int>(std::floor(z));
        const float fx = x - ix, fz = z - iz;
        const auto v = [&](int a, int b) { return (Hash(a, 0, b) & 0xFFFF) / 65535.0f; };
        const float sx = fx * fx * (3 - 2 * fx), sz = fz * fz * (3 - 2 * fz);
        const float a = v(ix, iz) + (v(ix + 1, iz) - v(ix, iz)) * sx;
        const float b = v(ix, iz + 1) + (v(ix + 1, iz + 1) - v(ix, iz + 1)) * sx;
        return a + (b - a) * sz;
    }
    int Height(int x, int z) {
        const float h = Value(x / 48.0f, z / 48.0f) * 40.0f + Value(x / 13.0f, z / 13.0f) * 8.0f;
        return 40 + static_cast<int>(h);
    }
    bool Solid(int x, int y, int z, int h) {
        if (y < -64 || y >= h)
            return false;
        // underground pockets: interior faces like caves and ravines give, below the surface sections
        const uint32_t cell = Hash(x >> 3, y >> 3, z >> 3);
        return y < -60 || (cell & 0xFF) > 22 || y > h - 4;
    }

    void Face(Tessellator& t, int dir, float x, float y, float z, RenderLayer layer) {
        static const float axes[6][2][3] = {
            {{0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {0, 0, 1}}, {{1, 0, 0}, {0, 0, 1}},
            {{0, 0, 1}, {1, 0, 0}}, {{0, 1, 0}, {1, 0, 0}}, {{1, 0, 0}, {0, 1, 0}}};
        static const float origin[6][3] = {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {0, 0, 0}, {0, 0, 1}, {0, 0, 0}};
        const float* a = axes[dir][0];
        const float* b = axes[dir][1];
        const float p[3] = {x + origin[dir][0], y + origin[dir][1], z + origin[dir][2]};
        const float v[4][3] = {{p[0], p[1], p[2]},
                               {p[0] + a[0], p[1] + a[1], p[2] + a[2]},
                               {p[0] + a[0] + b[0], p[1] + a[1] + b[1], p[2] + a[2] + b[2]},
                               {p[0] + b[0], p[1] + b[1], p[2] + b[2]}};
        const float c[4][3] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
        const float uv[4][2] = {{0, 0}, {0.01f, 0}, {0.01f, 0.01f}, {0, 0.01f}};
        t.SetLayer(layer);
        t.AddQuad(v, c, uv, false, 0.0f, 15.0f);
    }

    void Mesh(Tessellator& t, int cx, int cz) {
        int heights[18][18];
        for (int dz = 0; dz < 18; dz++)
            for (int dx = 0; dx < 18; dx++)
                heights[dz][dx] = Height(cx * 16 + dx - 1, cz * 16 + dz - 1);
        t.Begin();
        static const int d[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (int sy = -4; sy < 8; sy++) {
            t.EnterSection(sy);
            for (int lz = 0; lz < 16; lz++)
                for (int lx = 0; lx < 16; lx++) {
                    const int x = cx * 16 + lx, z = cz * 16 + lz, h = heights[lz + 1][lx + 1];
                    for (int y = sy * 16; y < sy * 16 + 16 && y < h; y++) {
                        if (!Solid(x, y, z, h))
                            continue;
                        for (int f = 0; f < 6; f++) {
                            const int nx = lx + d[f][0], nz = lz + d[f][2], ny = y + d[f][1];
                            if (!Solid(x + d[f][0], ny, z + d[f][2], heights[nz + 1][nx + 1]))
                                Face(t, f, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
                                     (Hash(x, y, z) & 7) == 0 ? RenderLayer::CutoutCull : RenderLayer::Solid);
                        }
                    }
                    if (h - 1 >= sy * 16 && h - 1 < sy * 16 + 16 && (Hash(x, h, z) & 15) == 0) {
                        // a plant: two crossed double-sided quads
                        const float c[4][3] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
                        const float uv[4][2] = {{0, 0}, {0.01f, 0}, {0.01f, 0.01f}, {0, 0.01f}};
                        const float fx = static_cast<float>(x), fy = static_cast<float>(h), fz = static_cast<float>(z);
                        const float q0[4][3] = {{fx, fy, fz}, {fx + 1, fy, fz + 1}, {fx + 1, fy + 1, fz + 1}, {fx, fy + 1, fz}};
                        const float q1[4][3] = {{fx + 1, fy, fz}, {fx, fy, fz + 1}, {fx, fy + 1, fz + 1}, {fx + 1, fy + 1, fz}};
                        t.SetLayer(RenderLayer::Cutout);
                        t.AddQuad(q0, c, uv, false, 0.0f, 15.0f);
                        t.AddQuad(q1, c, uv, false, 0.0f, 15.0f);
                    }
                    if (h < 62 && sy * 16 <= 61 && 61 < sy * 16 + 16)
                        Face(t, 2, static_cast<float>(x), 61.0f, static_cast<float>(z), RenderLayer::Translucent);
                }
        }
        t.End();
    }

    FrameParams MakeFrame(const glm::vec3& eye, const glm::vec3& target, uint32_t w, uint32_t h, float time) {
        FrameParams f{};
        const glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
        const glm::mat4 proj =
            glm::perspectiveRH_ZO(glm::radians(70.0f), static_cast<float>(w) / static_cast<float>(h), 0.05f, 1024.0f);
        const glm::mat4 vp = proj * view;
        std::memcpy(f.viewProjection, glm::value_ptr(vp), sizeof(f.viewProjection));
        std::memcpy(f.skyViewProjection, glm::value_ptr(vp), sizeof(f.skyViewProjection));
        std::memcpy(f.view, glm::value_ptr(view), sizeof(f.view));
        std::memcpy(f.projection, glm::value_ptr(proj), sizeof(f.projection));
        f.cameraPosition[0] = eye.x;
        f.cameraPosition[1] = eye.y;
        f.cameraPosition[2] = eye.z;
        f.fogColor[2] = f.fogColor[3] = 1.0f;
        f.environmentalFogStart = f.renderDistanceFogStart = 1000.0f;
        f.environmentalFogEnd = f.renderDistanceFogEnd = f.skyFogEnd = f.cloudFogEnd = 2000.0f;
        f.renderScale = 1.0f;
        f.timeSeconds = time;
        std::memset(f.lightmap, 255, sizeof(f.lightmap));
        return f;
    }

    struct Summary {
        double mean, p50, p99, worst;
    };
    Summary Summarize(std::vector<double> v) {
        std::sort(v.begin(), v.end());
        const double mean = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
        return {mean, v[v.size() / 2], v[std::min(v.size() - 1, v.size() * 99 / 100)], v.back()};
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: render_bench <pack> [radius] [frames] [remesh per frame]\n");
        return 2;
    }
    const int radius = argc > 2 ? std::atoi(argv[2]) : 16;
    const int frames = argc > 3 ? std::atoi(argv[3]) : 300;
    const int remesh = argc > 4 ? std::atoi(argv[4]) : 4;
    std::ifstream in(argv[1], std::ios::binary);
    const std::vector<uint8_t> pack{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    gpu::GpuSettings settings;
    settings.api = conduit::api_from_id("null");
    settings.width = 1920;
    settings.height = 1080;
    settings.offscreen = true;
    RenderContext_rhi ctx;
    std::string error;
    if (!ctx.InitializeOffscreen(settings, pack, &error)) {
        std::printf("no device: %s\n", error.c_str());
        return 1;
    }
    std::vector<uint8_t> tile(16 * 16 * 4, 255);
    ctx.UploadAtlas(tile.data(), 16, 1);
    ctx.SetChunkCullRadius(radius);

    struct Built {
        int cx, cz;
        Tessellator tess;
    };
    std::vector<Built> world;
    const auto t0 = std::chrono::steady_clock::now();
    size_t quads = 0;
    for (int cz = -radius; cz <= radius; cz++)
        for (int cx = -radius; cx <= radius; cx++) {
            if (cx * cx + cz * cz > (radius + 1) * (radius + 1))
                continue;
            world.push_back({cx, cz, {}});
            Mesh(world.back().tess, cx, cz);
            quads += world.back().tess.Vertices().size() / 4;
        }
    uint64_t version = 1;
    for (Built& b : world)
        ctx.UpdateChunkMesh(b.cx, b.cz, b.tess, version++);
    const double meshSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("world: %zu chunks, %.1fM quads, meshed in %.1f s\n", world.size(), quads / 1e6, meshSeconds);

    std::vector<double> frameMs, updateMs, drawMs, endMs, uiMs;
    // BENCH_UI=n: n entity-part draws per frame (a handful of textures, like mobs sharing skins)
    const int uiDraws = std::getenv("BENCH_UI") ? std::atoi(std::getenv("BENCH_UI")) : 0;
    const UiVertex box[4] = {{0, 0, 0, 0xFFFFFFFFu, 0, 0}, {1, 0, 0, 0xFFFFFFFFu, 1, 0},
                             {1, 1, 0, 0xFFFFFFFFu, 1, 1}, {0, 1, 0, 0xFFFFFFFFu, 0, 1}};
    const uint32_t boxIdx[6] = {0, 1, 2, 0, 2, 3};
    const uint32_t uiMesh = ctx.UiCreateMesh(box, 4, boxIdx, 6);
    std::vector<uint32_t> skins;
    for (int i = 0; i < 8; i++)
        skins.push_back(ctx.UiCreateTexture(16, 16, tile.data(), false));
    uint64_t gpuIndices = 0, gpuFaceIndices = 0, cpuIndices = 0, gpuCommands = 0;
    size_t cursor = 0;
    const int warmup = 20;
    for (int i = 0; i < frames + warmup; i++) {
        const float a = static_cast<float>(i) * 0.02f;
        const glm::vec3 eye(std::cos(a) * radius * 4.0f, 90.0f + std::sin(a * 3.0f) * 10.0f, std::sin(a) * radius * 4.0f);
        const glm::vec3 target = eye + glm::vec3(-std::sin(a), -0.25f, std::cos(a));
        const FrameParams f = MakeFrame(eye, target, settings.width, settings.height, 1.0f + static_cast<float>(i) / 60.0f);

        const auto u0 = std::chrono::steady_clock::now();
        for (int k = 0; k < remesh && !world.empty(); k++) {
            Built& b = world[cursor++ % world.size()];
            ctx.UpdateChunkMesh(b.cx, b.cz, b.tess, version++);
        }
        const auto f0 = std::chrono::steady_clock::now();
        ctx.BeginFrame(f);
        const auto d0 = std::chrono::steady_clock::now();
        ctx.DrawChunkMeshes(Frustum::FromViewProjection(glm::make_mat4(f.viewProjection)), eye.x, eye.z);
        const auto d1 = std::chrono::steady_clock::now();
        for (int k = 0; k < uiDraws; k++) {
            UiDraw d;
            d.material = DrawMaterial::Entity;
            d.mesh = uiMesh;
            d.texture = skins[static_cast<size_t>(k / std::max(1, uiDraws / 8)) % skins.size()];
            d.depth = true;
            const glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3((k % 64) - 32.0f, 70.0f, (k / 64) - 16.0f));
            std::memcpy(d.model, glm::value_ptr(m), 64);
            ctx.UiSubmit(UiLayer::World, d);
        }
        const auto u1 = std::chrono::steady_clock::now();
        ctx.EndFrame();
        const auto f1 = std::chrono::steady_clock::now();
        if (i < warmup)
            continue;
        updateMs.push_back(std::chrono::duration<double, std::milli>(f0 - u0).count());
        frameMs.push_back(std::chrono::duration<double, std::milli>(f1 - f0).count());
        drawMs.push_back(std::chrono::duration<double, std::milli>(d1 - d0).count());
        endMs.push_back(std::chrono::duration<double, std::milli>(f1 - u1).count());
        uiMs.push_back(std::chrono::duration<double, std::milli>(u1 - d1).count());
#ifdef BENCH_HAS_TERRAIN
        if (i % 10 == 0 && ctx.Terrain().Ready()) {
            gpu::GpuTerrain::FrameInput fi;
            fi.frustum = Frustum::FromViewProjection(glm::make_mat4(f.viewProjection));
            fi.camera[0] = eye.x;
            fi.camera[1] = eye.y;
            fi.camera[2] = eye.z;
            fi.cullRadius = radius;
            const auto all = ctx.Terrain().RunReference(fi, false);
            const auto faced = ctx.Terrain().RunReference(fi, true);
            gpuIndices += all.indices;
            gpuFaceIndices += faced.indices;
            gpuCommands += faced.commands;
            cpuIndices += ctx.CpuOpaqueIndices();
        }
#endif
    }
    const Summary fs = Summarize(frameMs), us = Summarize(updateMs), ds = Summarize(drawMs), es = Summarize(endMs);
    // DrawChunkMeshes: selection, culling setup, draw lists. EndFrame: command recording plus the null device's
    // emulated copies (a real gpu does those with dma, not the cpu)
    std::printf("terrain cull+build ms: mean %.3f  p99 %.3f  worst %.3f\n", ds.mean, ds.p99, ds.worst);
    std::printf("record+submit ms: mean %.3f  p99 %.3f  worst %.3f\n", es.mean, es.p99, es.worst);
    if (uiDraws) {
        const Summary ui = Summarize(uiMs);
        std::printf("entity submit ms (%d draws): mean %.3f  p99 %.3f\n", uiDraws, ui.mean, ui.p99);
    }
    std::printf("frame ms: mean %.3f  p50 %.3f  p99 %.3f  worst %.3f\n", fs.mean, fs.p50, fs.p99, fs.worst);
    std::printf("remesh upload ms: mean %.3f  p99 %.3f  worst %.3f\n", us.mean, us.p99, us.worst);
#ifdef BENCH_HAS_TERRAIN
    if (gpuIndices) {
        const auto& st = ctx.Terrain().GetStats();
        std::printf("gpu records %u, buckets %u, occlusion %d\n", st.records, st.buckets, st.occlusion ? 1 : 0);
        std::printf("opaque triangles/frame: cpu path %.2fM, gpu frustum %.2fM, gpu + facing %.2fM (%.0f%% fewer)\n",
                    cpuIndices / 3.0 / (frames / 10.0) / 1e6, gpuIndices / 3.0 / (frames / 10.0) / 1e6,
                    gpuFaceIndices / 3.0 / (frames / 10.0) / 1e6,
                    100.0 * (1.0 - static_cast<double>(gpuFaceIndices) / static_cast<double>(gpuIndices)));
        std::printf("indirect commands/frame: %.0f\n", gpuCommands / (frames / 10.0));
    }
#endif
    std::printf("RESULT frame=%.3f draw=%.3f draw_p99=%.3f end=%.3f\n", fs.mean, ds.mean, ds.p99, es.mean);
    return 0;
}
