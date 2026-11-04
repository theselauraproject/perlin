#include "client/renderer/Frustum.hpp"
#include "client/renderer/Tessellator.hpp"
#include "client/renderer/gpu/ChunkMeshStore.hpp"
#include "client/renderer/gpu/GpuCore.hpp"
#include "client/renderer/gpu/JavaPack.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace perlin;

namespace {
    std::vector<uint8_t> EmptyShaderLibrary() {
        std::vector<uint8_t> index;
        const auto u16 = [&](uint16_t v) { index.insert(index.end(), {uint8_t(v), uint8_t(v >> 8)}); };
        const auto u32 = [&](uint32_t v) {
            for (int i = 0; i < 4; i++)
                index.push_back(uint8_t(v >> (8 * i)));
        };
        u16(0);
        u32(1);
        const std::string name = "bench_uniforms";
        u16(static_cast<uint16_t>(name.size()));
        index.insert(index.end(), name.begin(), name.end());
        u32(65536);
        u16(0);
        index.push_back(0);
        std::vector<uint8_t> out;
        const auto o32 = [&](uint32_t v) {
            for (int i = 0; i < 4; i++)
                out.push_back(uint8_t(v >> (8 * i)));
        };
        o32(0x48535250);
        o32(1);
        o32(static_cast<uint32_t>(index.size()));
        o32(1);
        out.insert(out.end(), index.begin(), index.end());
        return out;
    }

    const char* kBlocks[] = {"minecraft:grass_block", "minecraft:stone", "minecraft:dirt", "minecraft:oak_leaves",
                             "minecraft:water", "minecraft:sand", "minecraft:oak_log", "minecraft:short_grass"};

    float Height(int x, int z) {
        return 64.0f + 6.0f * std::sin(x * 0.11f) + 5.0f * std::cos(z * 0.13f) + 3.0f * std::sin((x + z) * 0.31f);
    }

    void BuildChunk(int cx, int cz, Tessellator& t) {
        t.Begin();
        const float white[4][3] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
        const auto tileUv = [](int tile, float uv[4][2]) {
            const float inv = 1.0f / 128.0f, half = 0.5f * inv / 16.0f;
            const float u0 = (tile % 128) * inv + half, v0 = (tile / 128) * inv + half;
            const float u1 = (tile % 128 + 1) * inv - half, v1 = (tile / 128 + 1) * inv - half;
            const float q[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
            std::memcpy(uv, q, sizeof(q));
        };
        for (int sy = 3; sy <= 5; sy++) {
            t.EnterSection(sy);
            for (int lz = 0; lz < 16; lz++)
                for (int lx = 0; lx < 16; lx++) {
                    const int x = cx * 16 + lx, z = cz * 16 + lz;
                    const int h = static_cast<int>(Height(x, z));
                    if (h < sy * 16 || h >= sy * 16 + 16)
                        continue;
                    const uint16_t block = static_cast<uint16_t>(1 + ((x * 7 + z * 13) & 3));
                    float uv[4][2];
                    t.SetLayer(RenderLayer::Solid);
                    t.SetBlock(block, float(x), float(h), float(z));
                    tileUv(block, uv);
                    const float top[4][3] = {{float(x), h + 1.0f, float(z)},
                                             {x + 1.0f, h + 1.0f, float(z)},
                                             {x + 1.0f, h + 1.0f, z + 1.0f},
                                             {float(x), h + 1.0f, z + 1.0f}};
                    t.AddQuad(top, white, uv, false, 0.0f, 15.0f);
                    const int nx = static_cast<int>(Height(x + 1, z)), nz = static_cast<int>(Height(x, z + 1));
                    for (int y = std::min(h, nx) + 1; y <= h; y++) {
                        const float side[4][3] = {{x + 1.0f, y + 1.0f, float(z)},
                                                  {x + 1.0f, float(y), float(z)},
                                                  {x + 1.0f, float(y), z + 1.0f},
                                                  {x + 1.0f, y + 1.0f, z + 1.0f}};
                        t.AddQuad(side, white, uv, false, 0.0f, 15.0f);
                    }
                    for (int y = std::min(h, nz) + 1; y <= h; y++) {
                        const float side[4][3] = {{float(x), y + 1.0f, z + 1.0f},
                                                  {float(x), float(y), z + 1.0f},
                                                  {x + 1.0f, float(y), z + 1.0f},
                                                  {x + 1.0f, y + 1.0f, z + 1.0f}};
                        t.AddQuad(side, white, uv, false, 0.0f, 15.0f);
                    }
                    if (((x * 31 + z * 17) % 23) == 0) {
                        t.SetLayer(RenderLayer::Cutout);
                        t.SetBlock(7, float(x), h + 1.0f, float(z));
                        tileUv(7, uv);
                        const float cross[4][3] = {{float(x), h + 1.0f, float(z)},
                                                   {x + 1.0f, h + 1.0f, z + 1.0f},
                                                   {x + 1.0f, h + 2.0f, z + 1.0f},
                                                   {float(x), h + 2.0f, float(z)}};
                        t.AddQuad(cross, white, uv, false, 0.0f, 15.0f);
                    }
                    if (h < 62) {
                        t.SetLayer(RenderLayer::Translucent);
                        t.SetBlock(4, float(x), 62.0f, float(z));
                        tileUv(4, uv);
                        const float water[4][3] = {{float(x), 62.9f, float(z)},
                                                   {x + 1.0f, 62.9f, float(z)},
                                                   {x + 1.0f, 62.9f, z + 1.0f},
                                                   {float(x), 62.9f, z + 1.0f}};
                        t.AddQuad(water, white, uv, false, 0.0f, 15.0f);
                    }
                }
        }
        t.End();
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: java_pack_bench <pack> [width height frames] [-o NAME=VALUE]...\n");
        return 2;
    }
    uint32_t W = 960, H = 540;
    int frames = 120;
    std::vector<std::pair<std::string, std::string>> opts;
    std::vector<std::string> positional;
    for (int i = 2; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) {
            const std::string kv = argv[++i];
            const size_t eq = kv.find('=');
            if (eq != std::string::npos)
                opts.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        } else {
            positional.push_back(a);
        }
    }
    if (positional.size() >= 2) {
        W = static_cast<uint32_t>(std::atoi(positional[0].c_str()));
        H = static_cast<uint32_t>(std::atoi(positional[1].c_str()));
    }
    if (positional.size() >= 3)
        frames = std::atoi(positional[2].c_str());

    gpu::Gpu gpu;
    gpu::GpuSettings s;
    s.api = conduit::api::vulkan;
    s.offscreen = true;
    s.width = W;
    s.height = H;
    s.framesInFlight = 2;
    const std::vector<uint8_t> library = EmptyShaderLibrary();
    std::string error;
    if (!gpu.Init(s, library, &error)) {
        std::fprintf(stderr, "gpu: %s\n", error.c_str());
        return 77;
    }
    std::printf("adapter: %s\n", gpu.Caps().adapter.name.c_str());

    gpu::ChunkMeshStore meshes;
    meshes.Init(gpu);
    gpu::JavaPack pack(gpu, meshes);
    gpu::JavaPack::Config cfg;
    cfg.path = argv[1];
    for (const auto& [k, v] : opts)
        cfg.options[k] = v;
    cfg.blockName = [](uint16_t id) {
        return std::string(id < std::size(kBlocks) ? kBlocks[id] : "minecraft:stone");
    };
    cfg.blockEmission = [](uint16_t) { return 0; };
    if (const char* r = std::getenv("BENCH_SHADOW_RES"))
        cfg.shadowResolutionLimit = std::atoi(r);
    cfg.shadowCache = std::getenv("BENCH_NO_SHADOW_CACHE") == nullptr;
    if (const char* r = std::getenv("BENCH_SHADOW_INTERVAL"))
        cfg.shadowInterval = std::atoi(r);
    if (!pack.Load(cfg, error)) {
        std::fprintf(stderr, "pack: %s\n", error.c_str());
        return 1;
    }
    {
        const int tiles = 64;
        std::vector<uint8_t> atlas(static_cast<size_t>(tiles) * 16 * 16 * 4);
        for (int t = 0; t < tiles; t++)
            for (int p = 0; p < 256; p++) {
                uint8_t* px = &atlas[(static_cast<size_t>(t) * 256 + p) * 4];
                const uint32_t h = static_cast<uint32_t>(t * 977 + p * 131) * 2654435761u;
                px[0] = uint8_t(80 + (h >> 24) % 100 + t * 3);
                px[1] = uint8_t(90 + (h >> 16) % 100);
                px[2] = uint8_t(60 + (h >> 8) % 80);
                px[3] = t == 3 || t == 7 ? uint8_t((p * 7) % 3 ? 255 : 0) : 255;
            }
        pack.SetAtlas(atlas.data(), 16, tiles);
    }
    meshes.SetExtension(gpu::kPackVertexBytes,
                        [&](const Tessellator& t, std::vector<uint8_t>& out) { pack.Convert(t, out); });
    Tessellator::SetCaptureQuadInfo(true);
    const int radius = std::getenv("BENCH_RADIUS") ? std::atoi(std::getenv("BENCH_RADIUS")) : 6;
    size_t verts = 0;
    for (int cz = -radius; cz <= radius; cz++)
        for (int cx = -radius; cx <= radius; cx++) {
            Tessellator t;
            BuildChunk(cx, cz, t);
            verts += t.Vertices().size();
            meshes.Update(false, cx, cz, 16, t, 1);
        }
    std::printf("terrain: %d chunks, %zu vertices\n", (2 * radius + 1) * (2 * radius + 1), verts);

    conduit::texture_desc dd;
    dd.format = conduit::format::d32_float;
    dd.width = W;
    dd.height = H;
    dd.usage = conduit::texture_usage::depth_target | conduit::texture_usage::sampled | conduit::texture_usage::copy_src;
    dd.name = "bench depth";
    const conduit::texture_handle depth = gpu.Dev().create_texture(dd);

    const auto t0 = std::chrono::steady_clock::now();
    int left = 1;
    while ((left = pack.WarmUp()) > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (std::chrono::steady_clock::now() - t0 > std::chrono::minutes(20))
            break;
    }
    std::printf("warm up: %.1f s (%d left)\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), left);

    FrameParams f{};
    const glm::vec3 eye(0.5f, 80.0f, 0.5f);
    double gpuWall = 0.0, cpuRecord = 0.0;
    int measured = 0;
    for (int frame = 0; frame < frames + 10; frame++) {
        const float yaw = 0.4f + 0.002f * frame;
        const glm::vec3 target = eye + glm::vec3(std::cos(yaw), -0.35f, std::sin(yaw));
        const glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
        const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(70.0f), float(W) / float(H), 0.05f, 1024.0f);
        const glm::mat4 vp = proj * view;
        std::memcpy(f.viewProjection, glm::value_ptr(vp), sizeof(f.viewProjection));
        std::memcpy(f.skyViewProjection, glm::value_ptr(vp), sizeof(f.skyViewProjection));
        std::memcpy(f.view, glm::value_ptr(view), sizeof(f.view));
        std::memcpy(f.projection, glm::value_ptr(proj), sizeof(f.projection));
        f.cameraPosition[0] = eye.x;
        f.cameraPosition[1] = eye.y;
        f.cameraPosition[2] = eye.z;
        f.fogColor[0] = 0.6f;
        f.fogColor[1] = 0.7f;
        f.fogColor[2] = 1.0f;
        f.fogColor[3] = 1.0f;
        f.sky.enabled = true;
        f.sky.sunAngle = 0.6f;
        f.sky.skyColor[0] = 0.5f;
        f.sky.skyColor[1] = 0.7f;
        f.sky.skyColor[2] = 1.0f;
        f.sky.rainBrightness = 1.0f;
        f.timeSeconds = 10.0f + frame / 60.0f;
        f.frameIndex = static_cast<uint32_t>(frame);
        f.renderDistance = radius;
        f.shadowEntities = true;
        f.worldTick = 6000;
        f.worldDay = 1;
        std::memset(f.lightmap, 255, sizeof(f.lightmap));

        gpu.BeginFrame();
        const Frustum frustum = Frustum::FromViewProjection(vp);
        meshes.Select(frustum, eye.x, eye.y, eye.z, 0, false, f.timeSeconds);
        pack.SetViewFrustum(frustum);
        pack.GatherGeometry(eye.x, eye.z);
        conduit::swapchain::image image;
        gpu.AcquireImage(image);
        conduit::command_list* list = gpu.Dev().acquire_command_list(conduit::queue_type::graphics);
        gpu.Flush(*list);
        gpu::JavaPack::Output out;
        out.color = image.texture;
        out.colorFormat = gpu.TargetFormat();
        out.depth = depth;
        out.width = W;
        out.height = H;
        out.lightmap = gpu.White2D();
        const auto c0 = std::chrono::steady_clock::now();
        const bool trace = std::getenv("BENCH_TRACE") != nullptr;
        if (trace)
            std::fprintf(stderr, "frame %d record\n", frame);
        const bool ok = pack.Record(*list, f, out);
        if (trace)
            std::fprintf(stderr, "frame %d submit\n", frame);
        const auto c1 = std::chrono::steady_clock::now();
        pack.DropQueued();
        gpu.Transition(*list, image.texture, conduit::state::copy_src);
        const char* savePath = std::getenv("BENCH_SAVE");
        conduit::buffer_handle readback;
        const uint32_t pitch = (W * 4 + 255) / 256 * 256;
        if (savePath && frame == frames + 9) {
            conduit::buffer_desc bd;
            bd.size = static_cast<uint64_t>(pitch) * H;
            bd.usage = conduit::buffer_usage::copy_dst;
            bd.memory = conduit::memory_class::readback;
            bd.name = "bench readback";
            readback = gpu.Dev().create_buffer(bd);
            conduit::buffer_texture_copy c;
            c.buffer_row_pitch = pitch;
            c.width = W;
            c.height = H;
            list->copy_texture_to_buffer(readback, image.texture, std::span<const conduit::buffer_texture_copy>(&c, 1));
        }
        gpu.FlushRings();
        gpu.Dev().submit(conduit::queue_type::graphics, list);
        gpu.Dev().end_frame();
        const auto g0 = std::chrono::steady_clock::now();
        gpu.Dev().wait_idle();
        if (trace)
            std::fprintf(stderr, "frame %d done\n", frame);
        const auto g1 = std::chrono::steady_clock::now();
        if (!ok)
            std::printf("frame %d not recorded: %s\n", frame, pack.Error().c_str());
        if (readback) {
            gpu.Dev().invalidate(readback, 0, static_cast<uint64_t>(pitch) * H);
            if (const uint8_t* px = gpu.Dev().map(readback)) {
                if (FILE* fp = std::fopen(savePath, "wb")) {
                    std::fprintf(fp, "P6\n%u %u\n255\n", W, H);
                    for (uint32_t y = 0; y < H; y++)
                        for (uint32_t x = 0; x < W; x++)
                            std::fwrite(px + static_cast<size_t>(y) * pitch + x * 4, 1, 3, fp);
                    std::fclose(fp);
                }
            }
            gpu.Dev().destroy(readback);
        }
        if (frame >= 10) {
            cpuRecord += std::chrono::duration<double, std::milli>(c1 - c0).count();
            gpuWall += std::chrono::duration<double, std::milli>(g1 - g0).count();
            measured++;
        }
    }
    std::printf("RESULT frames %d: record %.3f ms/frame, gpu %.2f ms/frame\n", measured, cpuRecord / measured,
                gpuWall / measured);
    gpu.Dev().destroy(depth);
    return 0;
}
