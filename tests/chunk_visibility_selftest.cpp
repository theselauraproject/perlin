#include "client/systems/renderer/gpu/ChunkMeshStore.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string_view>

namespace perlin::gpu {
    struct ChunkMeshStoreTestAccess {
        static uint64_t Selections(const ChunkMeshStore& store) { return store.selectionBuilds; }
        static uint64_t Runs(const ChunkMeshStore& store) { return store.runBuilds; }
        static size_t Capacity(const ChunkMeshStore& store) {
            size_t bytes = 0;
            for (const auto& selection : store.selections) {
                bytes += selection.visible.capacity() * sizeof(ChunkMeshStore::Visible);
                bytes += selection.sections.capacity() * sizeof(const ChunkMeshStore::Section*);
                for (const auto& run : selection.runs)
                    bytes += run.runs.capacity() * sizeof(ChunkMeshStore::Run);
            }
            return bytes;
        }
        static void Miss(ChunkMeshStore& store) { store.selections[store.selected].valid = false; }
        static void Synthetic(ChunkMeshStore& store, int radius) {
            store.Clear();
            for (int z = -radius; z <= radius; z++)
                for (int x = -radius; x <= radius; x++) {
                    ChunkMeshStore::Chunk chunk;
                    chunk.page = ((x + radius) + (z + radius) * (radius * 2 + 1)) % 4;
                    chunk.cx = x;
                    chunk.cz = z;
                    chunk.minY = 0;
                    chunk.maxY = 128;
                    chunk.visMin = 0;
                    chunk.conn.assign(8, 0x7FFF);
                    for (int layer = 0; layer < kRenderLayerCount; layer++) {
                        chunk.iOff[layer] = static_cast<uint32_t>(((z + radius) * (radius * 2 + 1) + x + radius) * 300 + layer * 48);
                        chunk.layerCount[layer] = 48;
                    }
                    for (int sy = 0; sy < 8; sy++) {
                        ChunkMeshStore::Section section;
                        section.sy = sy;
                        section.minY = static_cast<float>(sy * 16);
                        section.maxY = section.minY + 16;
                        section.start.fill(static_cast<uint32_t>(sy * 6));
                        section.count.fill(6);
                        chunk.sections.push_back(section);
                    }
                    auto& stored = store.chunks[ChunkMeshStore::Key(x, z)] = std::move(chunk);
                    store.blended.insert(&stored);
                }
            store.meshEpoch++;
            store.generation++;
        }
    };
}

namespace {
    using namespace perlin;
    using Store = gpu::ChunkMeshStore;
    using Access = gpu::ChunkMeshStoreTestAccess;
    int checks = 0, failures = 0;
    void Check(bool ok, const char* message) {
        checks++;
        if (!ok) {
            failures++;
            std::printf("FAIL %s\n", message);
        }
    }
    Frustum All() {
        Frustum f{};
        for (auto& plane : f.planes)
            plane[3] = 1;
        return f;
    }
    bool Same(const std::vector<Store::Run>& a, const std::vector<Store::Run>& b) {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
            if (a[i].page != b[i].page || a[i].first != b[i].first || a[i].count != b[i].count)
                return false;
        return true;
    }
    struct Query {
        Frustum frustum = All();
        float x = .23f, y = 4.5f, z = .37f;
        int radius = 20;
        bool caves = false, translucent = false;
        double now = 1;
        void Apply(Store& store) const {
            store.Select(frustum, x, y, z, radius, caves, now, translucent);
        }
    };
    using RunSet = std::array<std::vector<Store::Run>, kRenderLayerCount>;
    RunSet Runs(Store& store, bool reverse, const float* faceCamera = nullptr) {
        RunSet result;
        for (int layer = 0; layer < kRenderLayerCount; layer++)
            store.CollectRuns(static_cast<RenderLayer>(layer), reverse, result[layer], faceCamera);
        return result;
    }
    bool Same(const RunSet& a, const RunSet& b) {
        for (int i = 0; i < kRenderLayerCount; i++)
            if (!Same(a[i], b[i]))
                return false;
        return true;
    }
    uint64_t Indices(const RunSet& set) {
        uint64_t sum = 0;
        for (const auto& layer : set)
            for (const auto& run : layer)
                sum += run.count;
        return sum;
    }
    void Differential(Store& store, const Query& query, const char* description) {
        for (bool reverse : {false, true})
            for (bool faces : {false, true}) {
                const float camera[] = {query.x, query.y, query.z};
                query.Apply(store);
                const RunSet first = Runs(store, reverse, faces ? camera : nullptr);
                query.Apply(store);
                const RunSet admitted = Runs(store, reverse, faces ? camera : nullptr);
                const auto selections = Access::Selections(store), runs = Access::Runs(store);
                query.Apply(store);
                const RunSet hit = Runs(store, reverse, faces ? camera : nullptr);
                Check(Access::Selections(store) == selections, "exact same selection reuses its CPU description");
                Check(Access::Runs(store) == runs, "exact same per-layer runs are reused");
                Access::Miss(store);
                query.Apply(store);
                const RunSet rebuilt = Runs(store, reverse, faces ? camera : nullptr);
                Check(Same(first, admitted) && Same(first, hit) && Same(hit, rebuilt), description);
            }
    }

    void MakeMesh(Tessellator& tess, int cx, int cz, int quads = 1, uint16_t connectivity = 0x7FFF) {
        tess.Begin();
        for (int sy = 0; sy < 2; sy++) {
            tess.EnterSection(sy);
            tess.AddVisibility(sy, connectivity);
            for (int layer = 0; layer < kRenderLayerCount; layer++) {
                tess.SetLayer(static_cast<RenderLayer>(layer));
                for (int i = 0; i < quads; i++) {
                    const float x = static_cast<float>(cx * 16) + i * 2.f;
                    const float z = static_cast<float>(cz * 16) + layer * 2.f;
                    const float y = static_cast<float>(sy * 16 + 1);
                    const float vertices[4][3] = {{x,y,z},{x+1,y,z},{x+1,y+1,z},{x,y+1,z}};
                    const float colors[4][3] = {{1,1,1},{1,1,1},{1,1,1},{1,1,1}};
                    const float uv[4][2] = {{0,0},{1,0},{1,1},{0,1}};
                    tess.AddQuad(vertices, colors, uv, false, 0.f, 15.f);
                }
            }
        }
        tess.End();
    }

    void CpuTests() {
        Store store;
        Access::Synthetic(store, 4);
        Query q;
        Differential(store, q, "cached all-terrain ordering matches forced rebuild");
        q.translucent = true;
        Differential(store, q, "cached translucent ordering matches forced rebuild");
        const auto before = Access::Selections(store);
        q.translucent = false; q.Apply(store);
        q.translucent = true; q.Apply(store);
        q.translucent = false; q.Apply(store);
        Check(Access::Selections(store) == before, "all/translucent queries retain independent bounded entries");

        for (int changed = 0; changed < 7; changed++) {
            Query next = q;
            if (changed == 0) next.x += .1f;
            if (changed == 1) next.y += 17;
            if (changed == 2) next.z += .1f;
            if (changed == 3) next.radius = 1;
            if (changed == 4) next.frustum.planes[0][0] = 1;
            if (changed == 5) next.frustum.planes[0][3] = -1;
            if (changed == 6) next.caves = true;
            const auto old = Access::Selections(store);
            next.Apply(store);
            Check(Access::Selections(store) == old + 1, "changing one query input invalidates selection");
            Differential(store, next, "single input change agrees with forced ordered rebuild");
            q.Apply(store);
        }

        q.Apply(store);
        const float firstCamera[] = {0,0,0}, otherCamera[] = {128,128,-128};
        Runs(store, false, firstCamera);
        const auto oldRuns = Access::Runs(store);
        const auto first = Runs(store, false, otherCamera);
        Check(Access::Runs(store) == oldRuns + kRenderLayerCount, "face-camera change invalidates every requested run layer");
        Access::Miss(store); q.Apply(store);
        Check(Same(first, Runs(store, false, otherCamera)), "face-camera change preserves exact recomputed order");

        store.Remove(false, 0, 0);
        Check(Indices(Runs(store, false)) == 0, "unload before Select never dereferences cached section pointers");
        Differential(store, q, "chunk unload refreshes selection and run ordering");
        store.Clear();
        Check(Access::Capacity(store) == 0, "world reset releases retained visibility and run capacities");
        Differential(store, q, "empty selection is cached safely after Clear");
        Check(Indices(Runs(store, false)) == 0, "Clear leaves no cached geometry");
    }

    void GpuTests(gpu::Gpu& gpu) {
        Store store;
        store.Init(gpu);
        uint64_t version = 1;
        for (int z = -2; z <= 2; z++)
            for (int x = -3; x <= 3; x++) {
                Tessellator tess;
                MakeMesh(tess, x, z);
                store.Update(false, x, z, 16, tess, version++);
            }
        Query q;
        Differential(store, q, "real uploaded terrain matches cached ordered runs");
        Tessellator replacement;
        MakeMesh(replacement, 0, 0, 3);
        store.Update(false, 0, 0, 16, replacement, version++);
        Check(Indices(Runs(store, false)) == 0, "replacement invalidates stale selected pointers before use");
        Differential(store, q, "real mesh replacement rebuilds all affected runs");
        const auto generation = store.Generation();
        store.Update(true, 1, 2, 32, replacement, version++);
        Check(store.Generation() == generation, "fixture covers LOD-only MeshEpoch change without Generation change");
        Differential(store, q, "LOD-only addition invalidates selection");
        store.Remove(true, 1, 2);
        Differential(store, q, "LOD-only removal invalidates selection");
        store.ReleaseGpu();
        Differential(store, q, "GPU resource release invalidates descriptions without retaining GPU handles");
        Check(store.Load({}), "GPU resource recreation succeeds on test backend");
        Differential(store, q, "GPU resource recreation refreshes cache epochs");

        store.Clear();
        for (int x = 0; x < 5; x++) {
            Tessellator tess;
            MakeMesh(tess, x, 0, 1, 0);
            store.Update(false, x, 0, 16, tess, version++);
        }
        q.caves = true;
        q.now = 10;
        q.Apply(store);
        const auto clipped = Indices(Runs(store, false));
        Check(!store.SectionReached(4, 0, 0, true), "cave fixture hides a disconnected far section");
        q.caves = false;
        q.Apply(store);
        const auto unclipped = Indices(Runs(store, false));
        Check(unclipped == 5u * 2u * kRenderLayerCount * 6u && unclipped > clipped,
              "disabling cave culling stops applying stale section visibility masks");
        Differential(store, q, "cave-disabled output retains every visible section");
        q.caves = true; q.Apply(store);
        Tessellator open;
        MakeMesh(open, 2, 0);
        store.Update(false, 2, 0, 16, open, version++);
        q.now += .01;
        q.Apply(store);
        const auto epoch = store.CaveEpoch(), builds = Access::Selections(store);
        q.now += .2;
        q.Apply(store);
        Check(store.CaveEpoch() > epoch && Access::Selections(store) > builds,
              "deferred cave refresh invalidates an otherwise unchanged query");
        Differential(store, q, "deferred cave update matches forced recomputation");
        store.SetVertexStride(gpu::kCompactVertexBytes);
        Differential(store, q, "vertex format reset removes old cached pointers");
        Check(Indices(Runs(store, false)) == 0, "vertex format reset leaves no old runs");
        store.Shutdown();
    }

    void Benchmark() {
        Store store;
        Access::Synthetic(store, 32);
        Query q;
        q.radius = 64;
        std::vector<Store::Run> runs;
        uint64_t checksum = 0;
        const auto run = [&](bool force) {
            const auto start = std::chrono::steady_clock::now();
            for (int frame = 0; frame < 200; frame++) {
                if (force) Access::Miss(store);
                q.Apply(store);
                for (int l = 0; l < kRenderLayerCount; l++) {
                    store.CollectRuns(static_cast<RenderLayer>(l), l >= 3, runs);
                    for (const auto& r : runs) checksum += r.count;
                }
            }
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        };
        const double recompute = run(true), cached = run(false);
        std::printf("BENCH visibility chunks=%zu frames=200 recompute_ms=%.3f cached_ms=%.3f speedup=%.2f checksum=%llu\n",
                    store.Chunks(), recompute, cached, recompute / cached, static_cast<unsigned long long>(checksum));
    }
}

int main(int argc, char** argv) {
    CpuTests();
    if (argc > 1 && std::string_view(argv[1]) != "--bench") {
        std::ifstream file(argv[1], std::ios::binary);
        const std::vector<uint8_t> pack{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        perlin::gpu::GpuSettings settings;
        settings.api = conduit::api_from_id("null");
        settings.width = settings.height = 64;
        settings.offscreen = true;
        settings.validation = true;
        perlin::gpu::Gpu gpu;
        std::string error;
        if (!gpu.Init(settings, pack, &error)) {
            std::printf("FAIL null test backend: %s\n", error.c_str());
            return 1;
        }
        GpuTests(gpu);
        gpu.Shutdown();
    }
    if (argc > 1 && std::string_view(argv[argc - 1]) == "--bench") Benchmark();
    std::printf("chunk_visibility_selftest: %s (%d checks, %d failures)\n", failures ? "FAIL" : "ok", checks, failures);
    return failures ? 1 : 0;
}
