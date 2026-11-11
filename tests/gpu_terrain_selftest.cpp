// the gpu-driven terrain records against the meshes they describe, on the null device: facing groups add up,
// every camera-facing quad stays inside an emitted draw, records match the cpu path's sections, and chunk
// replacement / removal / clearing keeps the record heap consistent
#include "client/renderer/Frustum.hpp"
#include "client/renderer/Tessellator.hpp"
#include "client/renderer/gpu/ChunkMeshStore.hpp"
#include "client/renderer/gpu/GpuTerrain.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <random>

using namespace perlin;

namespace {
    int failures = 0;
    void Check(bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok)
            failures++;
    }

    // a quad facing direction d (0..5 = +x -x +y -y +z -z) on the block at (x, y, z), or an odd slanted one
    void Quad(Tessellator& t, int d, float x, float y, float z, RenderLayer layer, std::vector<glm::vec4>& faces) {
        static const float axes[6][2][3] = {
            {{0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {0, 0, 1}}, {{1, 0, 0}, {0, 0, 1}},
            {{0, 0, 1}, {1, 0, 0}}, {{0, 1, 0}, {1, 0, 0}}, {{1, 0, 0}, {0, 1, 0}}};
        static const float origin[6][3] = {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {0, 0, 0}, {0, 0, 1}, {0, 0, 0}};
        static const float normal[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
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
        // plane: normal and offset, for the facing test below
        faces.push_back({normal[d][0], normal[d][1], normal[d][2],
                         -(normal[d][0] * p[0] + normal[d][1] * p[1] + normal[d][2] * p[2])});
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: gpu_terrain_selftest <pack>\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    const std::vector<uint8_t> pack{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    gpu::GpuSettings settings;
    settings.api = conduit::api_from_id("null");
    settings.width = 64;
    settings.height = 64;
    settings.offscreen = true;
    settings.validation = true;
    gpu::Gpu g;
    std::string error;
    if (!g.Init(settings, pack, &error)) {
        std::printf("no device: %s\n", error.c_str());
        return 77;
    }
    gpu::ChunkMeshStore meshes;
    meshes.Init(g);
    gpu::GpuTerrain terrain;
    Check(terrain.Init(g, meshes), "terrain initializes on a device with indirect count");

    std::mt19937 rng(7);
    struct Truth {
        std::vector<uint32_t> solid;
        std::vector<glm::vec4> planes;
    };
    std::map<std::pair<int, int>, Truth> truth;
    int groupedChecks = 0;
    const auto build = [&](int cx, int cz, uint64_t version) {
        Tessellator t;
        Truth tr;
        std::vector<glm::vec4> planes;
        t.Begin();
        for (int sy = -2; sy < 3; sy++) {
            t.EnterSection(sy);
            const int n = static_cast<int>(rng() % 40);
            for (int i = 0; i < n; i++) {
                const int d = static_cast<int>(rng() % 6);
                const float x = static_cast<float>(cx * 16 + static_cast<int>(rng() % 16));
                const float y = static_cast<float>(sy * 16 + static_cast<int>(rng() % 16));
                const float z = static_cast<float>(cz * 16 + static_cast<int>(rng() % 16));
                const RenderLayer layer = rng() % 4 == 0 ? RenderLayer::Cutout
                                          : rng() % 3 == 0 ? RenderLayer::CutoutCull
                                                           : RenderLayer::Solid;
                Quad(t, d, x, y, z, layer, planes);
            }
        }
        t.End();
        bool sums = true;
        for (const MeshSection& s : t.Sections())
            for (int k = 0; k < kSplitLayers; k++) {
                uint32_t sum = 0;
                for (uint32_t v : s.faces[static_cast<size_t>(k)])
                    sum += v;
                const RenderLayer layer = k == 0 ? RenderLayer::Solid : RenderLayer::CutoutCull;
                sums &= !s.split || sum == s.count[static_cast<size_t>(layer)];
            }
        if (!sums)
            Check(false, "facing groups add up to the section counts");
        // each grouped quad's stored normal must match the group it was put in
        static const int groupAxis[7] = {0, 1, 2, -1, 2, 1, 0};
        static const int groupSign[7] = {1, 1, 1, 0, -1, -1, -1};
        bool grouped = true;
        for (const MeshSection& s : t.Sections()) {
            if (!s.split)
                continue;
            for (int k = 0; k < kSplitLayers; k++) {
                const RenderLayer layer = k == 0 ? RenderLayer::Solid : RenderLayer::CutoutCull;
                const auto& idx = t.Indices(layer);
                uint32_t at = s.start[static_cast<size_t>(layer)];
                for (int grp = 0; grp < 7; grp++) {
                    const uint32_t n = s.faces[static_cast<size_t>(k)][static_cast<size_t>(grp)];
                    for (uint32_t q = at; q < at + n; q += 6) {
                        const Vertex& v = t.Vertices()[idx[q]];
                        const float comp[3] = {((v.normal >> 0) & 255) / 127.5f - 1.0f,
                                               ((v.normal >> 8) & 255) / 127.5f - 1.0f,
                                               ((v.normal >> 16) & 255) / 127.5f - 1.0f};
                        if (grp != 3)
                            grouped &= comp[groupAxis[grp]] * static_cast<float>(groupSign[grp]) > 0.9f;
                    }
                    at += n;
                }
            }
        }
        if (!grouped)
            Check(false, "quads sit in the group of their facing");
        groupedChecks++;
        tr.planes = planes;
        truth[{cx, cz}] = tr;
        meshes.Update(false, cx, cz, 16, t, version);
    };
    uint64_t version = 1;
    for (int cz = -3; cz <= 3; cz++)
        for (int cx = -3; cx <= 3; cx++)
            build(cx, cz, version++);

    const auto frame = [&](const glm::vec3& eye, const glm::vec3& at, gpu::GpuTerrain::FrameInput& fi) {
        const glm::mat4 vp = glm::perspectiveRH_ZO(glm::radians(80.0f), 1.0f, 0.05f, 500.0f) *
                             glm::lookAt(eye, at, glm::vec3(0, 1, 0));
        fi.frustum = Frustum::FromViewProjection(vp);
        fi.clipFromWorld = vp;
        fi.camera[0] = eye.x;
        fi.camera[1] = eye.y;
        fi.camera[2] = eye.z;
        fi.caveCulling = false;
        g.BeginFrame();
        meshes.Select(fi.frustum, eye.x, eye.y, eye.z, 0, false, 0.0);
        return terrain.Prepare(fi);
    };

    std::printf("     %d chunk meshes checked for facing groups\n", groupedChecks);
    gpu::GpuTerrain::FrameInput fi;
    Check(frame({5, 20, 5}, {0, 0, 0}, fi), "prepare with chunks");
    {
        uint64_t cpu = 0;
        std::vector<gpu::ChunkMeshStore::Run> runs;
        for (RenderLayer layer : gpu::GpuTerrain::kLayers) {
            meshes.CollectRuns(layer, false, runs);
            for (const auto& r : runs)
                cpu += r.count;
        }
        const auto ref = terrain.RunReference(fi, false);
        std::printf("     cpu path %llu indices, gpu records %llu\n", static_cast<unsigned long long>(cpu),
                    static_cast<unsigned long long>(ref.indices));
        Check(ref.indices >= cpu, "gpu records cover everything the cpu path draws");
    }

    // a facing group may only be dropped when the camera is behind every plane of it in the section
    bool covered = true;
    int cameras = 0, checkedQuads = 0;
    for (int trial = 0; trial < 40; trial++) {
        const glm::vec3 eye(static_cast<float>(static_cast<int>(rng() % 120) - 60),
                            static_cast<float>(static_cast<int>(rng() % 80) - 40),
                            static_cast<float>(static_cast<int>(rng() % 120) - 60));
        cameras++;
        const gpu::GpuTerrain::Record* records = terrain.RecordData();
        // facing test on the records themselves: a group is dropped only when the camera is behind every plane
        for (uint32_t i = 0; i < terrain.RecordCount(); i++) {
            const auto& r = records[i];
            if (r.bucket == ~0u)
                continue;
            const float cam[3] = {eye.x, eye.y, eye.z};
            const uint32_t mask = gpu::GpuTerrain::VisibleFaces(r, cam, 0.0f);
            const float lo[3] = {r.minX, r.minY, r.minZ}, hi[3] = {r.maxX, r.maxY, r.maxZ};
            static const int axisOf[7] = {0, 1, 2, -1, 2, 1, 0};
            static const float signOf[7] = {1, 1, 1, 0, -1, -1, -1};
            for (int grp = 0; grp < 7; grp++) {
                if (grp == 3 || !r.faces[grp])
                    continue;
                const int ax = axisOf[grp];
                // the most permissive plane of this facing inside the box
                const float plane = signOf[grp] > 0 ? lo[ax] : hi[ax];
                const bool someFront = signOf[grp] > 0 ? cam[ax] > plane : cam[ax] < plane;
                checkedQuads++;
                if (someFront && !(mask & (1u << grp)))
                    covered = false;
            }
        }
    }
    std::printf("     %d cameras, %d facing groups checked\n", cameras, checkedQuads);
    Check(covered, "no camera-facing group is culled");

    // churn: replace, remove and re-add chunks, the heap must stay consistent with the store
    for (int round = 0; round < 30; round++) {
        const int cx = static_cast<int>(rng() % 7) - 3, cz = static_cast<int>(rng() % 7) - 3;
        if (rng() % 3 == 0)
            meshes.Remove(false, cx, cz);
        else
            build(cx, cz, version++);
        gpu::GpuTerrain::FrameInput f2;
        frame({0, 10, 0}, {1, 0, 1}, f2);
    }
    {
        uint64_t live = 0;
        for (const auto& [key, chunk] : meshes.ChunkMap())
            for (const auto& sec : chunk.sections)
                for (RenderLayer layer : gpu::GpuTerrain::kLayers)
                    live += sec.count[static_cast<size_t>(layer)] ? 1 : 0;
        uint64_t records = 0;
        for (uint32_t i = 0; i < terrain.RecordCount(); i++)
            records += terrain.RecordData()[i].bucket != ~0u;
        std::printf("     %llu live sections, %llu live records\n", static_cast<unsigned long long>(live),
                    static_cast<unsigned long long>(records));
        Check(live == records, "records match the store after churn");
    }
    meshes.Clear();
    gpu::GpuTerrain::FrameInput f3;
    frame({0, 10, 0}, {1, 0, 1}, f3);
    uint64_t left = 0;
    for (uint32_t i = 0; i < terrain.RecordCount(); i++)
        left += terrain.RecordData()[i].bucket != ~0u;
    Check(left == 0, "clearing the store empties the records");

    terrain.Shutdown();
    meshes.Shutdown();
    g.Shutdown();
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
