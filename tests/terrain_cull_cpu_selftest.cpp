// runs the real terrain_cull.slang (compiled by slangc to c++) over GpuTerrain's records on the cpu: the draws it
// writes must match the cpu reference, stay inside their buckets, and the occlusion passes must only drop sections
// that are fully behind the depth pyramid
#include "terrain_cull_cpu.cpp"

#include "client/renderer/Frustum.hpp"
#include "client/renderer/Tessellator.hpp"
#include "client/renderer/gpu/ChunkMeshStore.hpp"
#include "client/renderer/gpu/GpuTerrain.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>

using namespace perlin;

namespace {
    int failures = 0;
    void Check(bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok)
            failures++;
    }

    void Face(Tessellator& t, int d, float x, float y, float z, RenderLayer layer) {
        static const float axes[6][2][3] = {
            {{0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {0, 0, 1}}, {{1, 0, 0}, {0, 0, 1}},
            {{0, 0, 1}, {1, 0, 0}}, {{0, 1, 0}, {1, 0, 0}}, {{1, 0, 0}, {0, 1, 0}}};
        static const float origin[6][3] = {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {0, 0, 0}, {0, 0, 1}, {0, 0, 0}};
        const float* a = axes[d][0];
        const float* b = axes[d][1];
        const float p[3] = {x + origin[d][0], y + origin[d][1], z + origin[d][2]};
        const float v[4][3] = {{p[0], p[1], p[2]},
                               {p[0] + a[0], p[1] + a[1], p[2] + a[2]},
                               {p[0] + a[0] + b[0], p[1] + a[1] + b[1], p[2] + a[2] + b[2]},
                               {p[0] + b[0], p[1] + b[1], p[2] + b[2]}};
        const float c[4][3] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
        const float uv[4][2] = {{0, 0}, {0.01f, 0}, {0.01f, 0.01f}, {0, 0.01f}};
        t.SetLayer(layer);
        t.AddQuad(v, c, uv, false, 0.0f, 15.0f);
    }

    struct Buffers {
        std::vector<uint32_t> visibility, early, late, shadow, counts, cave;
        float planes[6][4] = {};
        uint32_t sliceFirst = 0, sliceEnd = 0;
        std::vector<float> hiz;
    };

    void Run(const gpu::GpuTerrain& terrain, Buffers& b, uint32_t mode, uint32_t hizValid) {
        const gpu::GpuTerrain::DebugView d = terrain.Debug();
        GlobalParams_0 g{};
        auto* records = const_cast<SectionRecord_0*>(reinterpret_cast<const SectionRecord_0*>(terrain.RecordData()));
        g.records_0 = {records, terrain.RecordCount()};
        std::vector<Vector<uint32_t, 2>> buckets(d.bucketCount);
        for (uint32_t i = 0; i < d.bucketCount; i++)
            buckets[i] = Vector<uint32_t, 2>{d.buckets[i * 2], d.buckets[i * 2 + 1]};
        g.buckets_0 = {buckets.data(), buckets.size()};
        b.cave = *d.caveBits;
        if (b.cave.empty())
            b.cave.push_back(~0u);
        g.caveBits_0 = {b.cave.data(), b.cave.size()};
        b.visibility.resize(terrain.RecordCount(), 0);
        g.visibility_0 = {b.visibility.data(), b.visibility.size()};
        b.early.resize(size_t{d.commandCapacity} * 5);
        b.late.resize(size_t{d.commandCapacity} * 5);
        g.commandsEarly_0 = {b.early.data(), b.early.size()};
        g.commandsLate_0 = {b.late.data(), b.late.size()};
        b.counts.assign(size_t{d.bucketCount} * 3, 0u);
        b.shadow.resize(size_t{d.commandCapacity} * 5);
        g.commandsShadow_0 = {b.shadow.data(), b.shadow.size()};
        g.counts_0 = {b.counts.data(), b.counts.size()};
        if (b.hiz.empty())
            b.hiz.push_back(0.0f);
        g.hiz_0 = {b.hiz.data(), b.hiz.size()};
        CullParams_0 params;
        std::memcpy(&params, d.params, sizeof(params));
        g.params_0 = &params;
        PushData_0 push{};
        push.mode_0 = mode;
        push.bucketCount_0 = d.bucketCount;
        push.hizValid_0 = hizValid;
        push.sliceFirst_0 = b.sliceFirst;
        push.sliceEnd_0 = b.sliceEnd;
        for (int i = 0; i < 6; i++)
            push.shadowPlanes_0[i] = Vector<float, 4>{b.planes[i][0], b.planes[i][1], b.planes[i][2], b.planes[i][3]};
        g.push_0 = &push;
        ComputeVaryingInput vi{};
        vi.startGroupID = {0, 0, 0};
        vi.endGroupID = {(terrain.RecordCount() + 63) / 64, 1, 1};
        csMain(&vi, nullptr, &g);
    }

    struct Tally {
        uint64_t indices = 0;
        uint32_t commands = 0;
        bool inside = true;
    };
    Tally Count(const gpu::GpuTerrain& terrain, const Buffers& b, bool late) {
        const gpu::GpuTerrain::DebugView d = terrain.Debug();
        Tally t;
        for (uint32_t k = 0; k < d.bucketCount; k++) {
            const uint32_t n = b.counts[(late ? d.bucketCount : 0) + k];
            const uint32_t base = d.buckets[k * 2], cap = d.buckets[k * 2 + 1];
            if (n > cap)
                t.inside = false;
            for (uint32_t i = 0; i < std::min(n, cap); i++) {
                const uint32_t* c = (late ? b.late.data() : b.early.data()) + size_t{base + i} * 5;
                t.indices += c[0];
                t.commands++;
                t.inside &= c[1] == 1;
            }
        }
        return t;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: terrain_cull_cpu_selftest <pack>\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    const std::vector<uint8_t> pack{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    gpu::GpuSettings settings;
    settings.api = conduit::api_from_id("null");
    settings.width = settings.height = 64;
    settings.offscreen = true;
    gpu::Gpu g;
    std::string error;
    if (!g.Init(settings, pack, &error)) {
        std::printf("no device: %s\n", error.c_str());
        return 77;
    }
    gpu::ChunkMeshStore meshes;
    meshes.Init(g);
    gpu::GpuTerrain terrain;
    if (!terrain.Init(g, meshes)) {
        std::printf("no gpu terrain on this device\n");
        return 77;
    }
    std::mt19937 rng(11);
    uint64_t version = 1;
    for (int cz = -6; cz <= 6; cz++)
        for (int cx = -6; cx <= 6; cx++) {
            Tessellator t;
            t.Begin();
            for (int sy = -2; sy < 4; sy++) {
                t.EnterSection(sy);
                const int n = static_cast<int>(rng() % 30);
                for (int i = 0; i < n; i++) {
                    const RenderLayer layer = rng() % 4 == 0 ? RenderLayer::Cutout
                                              : rng() % 3 == 0 ? RenderLayer::CutoutCull
                                                               : RenderLayer::Solid;
                    Face(t, static_cast<int>(rng() % 6), static_cast<float>(cx * 16 + static_cast<int>(rng() % 16)),
                         static_cast<float>(sy * 16 + static_cast<int>(rng() % 16)),
                         static_cast<float>(cz * 16 + static_cast<int>(rng() % 16)), layer);
                }
            }
            t.End();
            meshes.Update(false, cx, cz, 16, t, version++);
        }

    bool match = true, inside = true;
    int frames = 0;
    for (int trial = 0; trial < 25; trial++) {
        const glm::vec3 eye(static_cast<float>(static_cast<int>(rng() % 160) - 80), static_cast<float>(rng() % 60),
                            static_cast<float>(static_cast<int>(rng() % 160) - 80));
        const glm::vec3 at(static_cast<float>(static_cast<int>(rng() % 40) - 20), 0.0f,
                           static_cast<float>(static_cast<int>(rng() % 40) - 20));
        const glm::mat4 vp = glm::perspectiveRH_ZO(glm::radians(80.0f), 1.0f, 0.05f, 500.0f) *
                             glm::lookAt(eye, at, glm::vec3(0, 1, 0));
        gpu::GpuTerrain::FrameInput fi;
        fi.frustum = Frustum::FromViewProjection(vp);
        fi.clipFromWorld = vp;
        fi.camera[0] = eye.x;
        fi.camera[1] = eye.y;
        fi.camera[2] = eye.z;
        fi.cullRadius = trial % 3 == 0 ? 4 : 0;
        fi.caveCulling = false;
        g.BeginFrame();
        if (!terrain.Prepare(fi))
            continue;
        Buffers b;
        Run(terrain, b, 0, 0);
        const Tally t = Count(terrain, b, false);
        const auto ref = terrain.RunReference(fi, true);
        match &= t.indices == ref.indices && t.commands == ref.commands;
        inside &= t.inside;
        frames++;
        if (t.indices != ref.indices)
            std::printf("     shader %llu indices / %u draws, reference %llu / %u\n",
                        static_cast<unsigned long long>(t.indices), t.commands,
                        static_cast<unsigned long long>(ref.indices), ref.commands);
    }
    std::printf("     %d cameras\n", frames);
    Check(match, "the shader draws exactly what the cpu reference draws");
    Check(inside, "draws stay inside their bucket and are single instance");

    // occlusion: a wall at reversed depth 0.95 over the left half of a 256x256 target, sky (0) on the right
    {
        const glm::vec3 eye(0, 30, -100);
        const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(80.0f), 1.0f, 0.05f, 500.0f);
        const glm::mat4 vp = proj * glm::lookAt(eye, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        glm::mat4 rev = vp;
        for (int c = 0; c < 4; c++)
            rev[c][2] = vp[c][3] - vp[c][2];
        gpu::GpuTerrain::FrameInput fi;
        fi.frustum = Frustum::FromViewProjection(vp);
        fi.clipFromWorld = rev;
        fi.camera[0] = eye.x;
        fi.camera[1] = eye.y;
        fi.camera[2] = eye.z;
        fi.caveCulling = false;
        fi.occlusion = true;
        fi.depthWidth = fi.depthHeight = 256;
        g.BeginFrame();
        Check(terrain.Prepare(fi) && terrain.Occluding(), "prepare with occlusion");
        const gpu::GpuTerrain::DebugView d = terrain.Debug();
        Buffers b;
        // pyramid like hiz_build.slang: level 0 is 128x128, each texel the farthest of its 2x2
        std::vector<float> depth(256 * 256);
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++)
                depth[y * 256 + x] = x < 128 ? 0.95f : 0.0f;
        uint32_t total = d.hizOffsets[d.hizLevels - 1] + 1;
        b.hiz.assign(total + 16, 0.0f);
        uint32_t sw = 256, sh = 256;
        for (uint32_t level = 0; level < d.hizLevels; level++) {
            const uint32_t dw = std::max(1u, (sw + 1) / 2), dh = std::max(1u, (sh + 1) / 2);
            for (uint32_t y = 0; y < dh; y++)
                for (uint32_t x = 0; x < dw; x++) {
                    float m = 1.0f;
                    for (uint32_t k = 0; k < 4; k++) {
                        const uint32_t sx = std::min(x * 2 + (k & 1), sw - 1), sy = std::min(y * 2 + (k >> 1), sh - 1);
                        m = std::min(m, level == 0 ? depth[sy * 256 + sx] : b.hiz[d.hizOffsets[level - 1] + sy * sw + sx]);
                    }
                    b.hiz[d.hizOffsets[level] + y * dw + x] = m;
                }
            sw = dw;
            sh = dh;
        }
        Run(terrain, b, 1, 0);
        Check(Count(terrain, b, false).commands == 0, "early pass draws nothing on the first frame");
        Run(terrain, b, 2, 1);
        const Tally late = Count(terrain, b, true);
        Buffers all;
        Run(terrain, all, 0, 0);
        const Tally every = Count(terrain, all, false);
        std::printf("     frustum %u draws, after occlusion %u\n", every.commands, late.commands);
        Check(late.commands < every.commands && late.commands > 0, "the wall hides some sections, not all");
        // a dropped section must project fully onto the wall and sit behind it
        bool sound = true;
        const auto* recs = terrain.RecordData();
        for (uint32_t i = 0; i < terrain.RecordCount(); i++) {
            const auto& r = recs[i];
            if (r.bucket == ~0u || b.visibility[i])
                continue;
            if (fi.frustum.Test(r.minX, r.minY, r.minZ, r.maxX, r.maxY, r.maxZ) == Frustum::Result::Outside)
                continue;
            for (int k = 0; k < 8 && sound; k++) {
                const glm::vec4 p = rev * glm::vec4(k & 1 ? r.maxX : r.minX, k & 2 ? r.maxY : r.minY,
                                                    k & 4 ? r.maxZ : r.minZ, 1.0f);
                if (p.w <= 0.0f)
                    sound = false;
                const float u = p.x / p.w * 0.5f + 0.5f, z = p.z / p.w;
                if (u * 256.0f >= 128.0f || z >= 0.95f)
                    sound = false;
            }
        }
        Check(sound, "every occluded section is fully behind the wall");
        // next frame, same view: the early pass now draws what the late pass found
        Run(terrain, b, 1, 0);
        const Tally early = Count(terrain, b, false);
        Check(early.commands == late.commands, "visible sections move to the early pass next frame");
    }

    // shadow casters: a 40 block box around the origin, culled in two slices
    {
        Buffers b;
        const float box[6][4] = {{1, 0, 0, 40}, {-1, 0, 0, 40}, {0, 1, 0, 40}, {0, -1, 0, 40}, {0, 0, 1, 40}, {0, 0, -1, 40}};
        std::memcpy(b.planes, box, sizeof(box));
        uint64_t expected = 0;
        for (uint32_t i = 0; i < terrain.RecordCount(); i++) {
            const auto& r = terrain.RecordData()[i];
            if (r.bucket != ~0u && r.maxX >= -40 && r.minX <= 40 && r.maxY >= -40 && r.minY <= 40 && r.maxZ >= -40 &&
                r.minZ <= 40)
                expected++;
        }
        const gpu::GpuTerrain::DebugView d = terrain.Debug();
        uint64_t got = 0;
        bool whole = true;
        for (uint32_t slice = 0; slice < 2; slice++) {
            b.sliceFirst = terrain.RecordCount() * slice / 2;
            b.sliceEnd = terrain.RecordCount() * (slice + 1) / 2;
            Run(terrain, b, 3, 0);
            for (uint32_t k = 0; k < d.bucketCount; k++) {
                const uint32_t n = b.counts[d.bucketCount * 2 + k];
                got += n;
                for (uint32_t i = 0; i < n; i++)
                    whole &= b.shadow[size_t{d.buckets[k * 2] + i} * 5 + 1] == 1;
            }
        }
        std::printf("     shadow casters %llu, expected %llu\n", static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(expected));
        Check(got == expected && whole, "shadow caster culling keeps exactly the sections in the light box");
    }

    terrain.Shutdown();
    meshes.Shutdown();
    g.Shutdown();
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
