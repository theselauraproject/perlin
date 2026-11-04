// face masks, greedy meshing and residency policy, all against brute-force references
#include "render/chunk/GreedyMesher.hpp"
#include "render/chunk/Residency.hpp"

#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <set>

using namespace perlin::render;

static int g_failed = 0;
#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                             \
            g_failed++;                                                                                      \
        }                                                                                                    \
    } while (0)

static BlockTable MakeTable() {
    BlockTable t;
    t.Set(1, BlockTable::Opaque | BlockTable::Meshed, 10);
    t.Set(2, BlockTable::Opaque | BlockTable::Meshed, 20);
    // glass: drawn, but does not hide what is behind it
    t.Set(3, BlockTable::Meshed, 30);
    return t;
}

struct Section {
    std::vector<uint16_t> cells = std::vector<uint16_t>(kSectionCells, 0);
    SectionBlocks View() const { return {cells.data(), 0}; }
    uint16_t& At(int x, int y, int z) { return cells[static_cast<size_t>((y * 16 + z) * 16 + x)]; }
};

static const int kDir[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};

// visible faces counted the slow way: (x, y, z, face) set
static std::set<std::array<int, 4>> NaiveFaces(const Section& s, const SectionNeighbours& nb,
                                               const BlockTable& t) {
    std::set<std::array<int, 4>> faces;
    for (int y = 0; y < 16; y++)
        for (int z = 0; z < 16; z++)
            for (int x = 0; x < 16; x++) {
                const uint16_t id = s.cells[static_cast<size_t>((y * 16 + z) * 16 + x)];
                if (!(t.Flags(id) & BlockTable::Meshed))
                    continue;
                for (int f = 0; f < 6; f++) {
                    const int nx = x + kDir[f][0], ny = y + kDir[f][1], nz = z + kDir[f][2];
                    bool opaque;
                    if (nx < 0 || nx > 15 || ny < 0 || ny > 15 || nz < 0 || nz > 15) {
                        opaque = true;
                        if (nb.Known(f))
                            opaque = (t.Flags(nb.section[static_cast<size_t>(f)].At(
                                          (nx + 16) & 15, (ny + 16) & 15, (nz + 16) & 15)) &
                                      BlockTable::Opaque) != 0;
                    } else {
                        opaque = (t.Flags(s.cells[static_cast<size_t>((ny * 16 + nz) * 16 + nx)]) &
                                  BlockTable::Opaque) != 0;
                    }
                    if (!opaque)
                        faces.insert({x, y, z, f});
                }
            }
    return faces;
}

static void TestSingleBlock() {
    const BlockTable t = MakeTable();
    Section s;
    s.At(5, 6, 7) = 1;
    SectionNeighbours nb;
    for (int f = 0; f < 6; f++)
        nb.Set(f, {nullptr, 0});
    const FaceMask m = ComputeFaceMask(s.View(), nb, t);
    CHECK(m.faceCount == 6);
    CHECK(m.borderUnknown == 0);
    std::vector<PackedQuad> q;
    const MeshStats st = GreedyMesh(s.View(), m, t, {}, q);
    CHECK(st.quads == 6 && q.size() == 6);
}

static void TestSolidSection() {
    const BlockTable t = MakeTable();
    const SectionBlocks solid{nullptr, 1};
    SectionNeighbours none;
    FaceMask m = ComputeFaceMask(solid, none, t);
    CHECK(m.faceCount == 0);
    CHECK(m.borderUnknown == 63);

    SectionNeighbours air;
    for (int f = 0; f < 6; f++)
        air.Set(f, {nullptr, 0});
    m = ComputeFaceMask(solid, air, t);
    CHECK(m.faceCount == 6 * 256);
    std::vector<PackedQuad> q;
    GreedyMesh(solid, m, t, {}, q);
    CHECK(q.size() == 6);
    for (const PackedQuad& p : q)
        CHECK(p.W() == 16 && p.H() == 16);

    SectionNeighbours stone;
    for (int f = 0; f < 6; f++)
        stone.Set(f, {nullptr, 1});
    CHECK(ComputeFaceMask(solid, stone, t).faceCount == 0);
}

static void TestRandomAgainstNaive() {
    const BlockTable t = MakeTable();
    std::mt19937 rng(1234);
    for (int round = 0; round < 60; round++) {
        Section s;
        // clustered ids so merging has something to do
        const int density = 10 + round % 4 * 25;
        for (int y = 0; y < 16; y++)
            for (int z = 0; z < 16; z++)
                for (int x = 0; x < 16; x++) {
                    const int r = static_cast<int>(rng() % 100);
                    uint16_t id = 0;
                    if (r < density) {
                        const int k = static_cast<int>(rng() % 8);
                        id = k < 5 ? 1 : (k < 7 ? 2 : 3);
                    }
                    if (y < 4 && round % 3 == 0)
                        id = 1;
                    s.At(x, y, z) = id;
                }
        std::vector<Section> side(6);
        SectionNeighbours nb;
        for (int f = 0; f < 6; f++) {
            if (round % 5 == 4 && f == 3)
                continue;
            for (auto& c : side[static_cast<size_t>(f)].cells)
                c = static_cast<uint16_t>(rng() % 4 == 0 ? 1 + rng() % 3 : 0);
            nb.Set(f, side[static_cast<size_t>(f)].View());
        }
        const FaceMask m = ComputeFaceMask(s.View(), nb, t);
        const auto naive = NaiveFaces(s, nb, t);
        CHECK(m.faceCount == naive.size());

        std::vector<PackedQuad> quads;
        const MeshStats st = GreedyMesh(s.View(), m, t, {}, quads);
        CHECK(st.quads == quads.size());
        CHECK(quads.size() <= naive.size());

        std::set<std::array<int, 4>> covered;
        for (const PackedQuad& q : quads) {
            const int face = q.FaceIndex();
            const int axis = AxisOf(face);
            for (int j = 0; j < q.H(); j++)
                for (int i = 0; i < q.W(); i++) {
                    int x = q.X(), y = q.Y(), z = q.Z();
                    // the quad grows along the in-plane axes in (p, q) order
                    if (axis == 0) {
                        y += i;
                        z += j;
                    } else if (axis == 1) {
                        x += i;
                        z += j;
                    } else {
                        x += i;
                        y += j;
                    }
                    CHECK(covered.insert({x, y, z, face}).second);
                    CHECK(q.Tile() == t.Tile(s.cells[static_cast<size_t>((y * 16 + z) * 16 + x)], face));
                }
        }
        CHECK(covered == naive);
    }
}

static void TestWinding() {
    for (int face = 0; face < 6; face++) {
        const PackedQuad q = PackedQuad::Make(2, 3, 4, 3, 2, face, 7, 0);
        const QuadCorners c = CornersOf(q);
        float a[3], b[3];
        for (int i = 0; i < 3; i++) {
            a[i] = c.pos[1][i] - c.pos[0][i];
            b[i] = c.pos[2][i] - c.pos[0][i];
        }
        const float n[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
        const float dot = n[0] * static_cast<float>(kDir[face][0]) +
                          n[1] * static_cast<float>(kDir[face][1]) + n[2] * static_cast<float>(kDir[face][2]);
        CHECK(dot > 0);
        // the quad sits on the correct plane
        const int axis = AxisOf(face);
        const float plane = static_cast<float>(axis == 0   ? q.X()
                                               : axis == 1 ? q.Y()
                                                           : q.Z()) +
                            (Positive(face) ? 1.0f : 0.0f);
        for (int i = 0; i < 4; i++)
            CHECK(c.pos[i][axis] == plane);
        CHECK(q.W() == 3 && q.H() == 2 && q.Tile() == 7 && q.FaceIndex() == face);
    }
}

static void TestExtraKeySplits() {
    const BlockTable t = MakeTable();
    const SectionBlocks solid{nullptr, 1};
    SectionNeighbours air;
    for (int f = 0; f < 6; f++)
        air.Set(f, {nullptr, 0});
    const FaceMask m = ComputeFaceMask(solid, air, t);
    std::vector<PackedQuad> q;
    // light differs on the left and right half of every face
    GreedyMesh(
        solid, m, t,
        [](int x, int y, int z, int face) -> uint16_t {
            const int axis = AxisOf(face);
            const int coord = axis == 0 ? y : x;
            (void)z;
            (void)y;
            return coord < 8 ? 1 : 2;
        },
        q);
    CHECK(q.size() > 6);
    for (const PackedQuad& p : q)
        CHECK(p.Extra() == 1 || p.Extra() == 2);
}

static void TestMaskCache() {
    const BlockTable t = MakeTable();
    Section s;
    s.At(0, 0, 0) = 1;
    SectionNeighbours nb;
    nb.Set(NegX, {nullptr, 0});
    FaceMaskCache cache;
    const SectionKey key{1, 2, 3};
    const uint32_t faces0 = cache.Get(key, 1, s.View(), nb, t).faceCount;
    CHECK(cache.Misses() == 1 && cache.Hits() == 0);
    CHECK(cache.Get(key, 1, s.View(), nb, t).faceCount == faces0);
    CHECK(cache.Hits() == 1);
    // a newly known neighbour reveals the held-back faces
    nb.Set(NegY, {nullptr, 0});
    CHECK(cache.Get(key, 1, s.View(), nb, t).faceCount == faces0 + 1);
    CHECK(cache.Misses() == 2);
    CHECK(cache.Get(key, 2, s.View(), nb, t).faceCount == faces0 + 1);
    CHECK(cache.Misses() == 3);
    cache.InvalidateAround(key);
    CHECK(cache.Size() == 0);
}

// ---- residency

static bool InRangeCoverage(const ChunkResidency& r, const std::set<std::pair<int, int>>& present,
                            std::map<std::pair<int, int>, int>& count) {
    std::vector<ChunkResidency::DrawItem> draw;
    r.CollectDraw(draw);
    count.clear();
    for (const auto& d : draw) {
        const int w = d.unit.Width();
        for (int dz = 0; dz < w; dz++)
            for (int dx = 0; dx < w; dx++)
                count[{d.unit.x + dx, d.unit.z + dz}]++;
    }
    for (const auto& p : present)
        if (!count.count(p))
            return false;
    return true;
}

static ResidencyConfig SmallConfig() {
    ResidencyConfig c;
    c.fullRadius = 3;
    c.lodStep = 2;
    c.maxLevel = 2;
    c.lodRadius = 12;
    c.hysteresis = 0;
    c.retainMs = 1000;
    c.retainBudgetBytes = 1 << 30;
    c.maxBuildsPerTick = 100;
    c.maxInFlight = 1000;
    return c;
}

static void Populate(ChunkResidency& r, std::set<std::pair<int, int>>& present, int radius) {
    for (int z = -radius; z <= radius; z++)
        for (int x = -radius; x <= radius; x++) {
            r.SetData({x, z}, true);
            present.insert({x, z});
        }
}

// completes every requested build immediately
static void Settle(ChunkResidency& r, uint64_t& now, ColumnPos cam, float vx = 0, float vz = 0) {
    for (int i = 0; i < 20; i++) {
        Plan plan;
        r.Tick(now, cam, vx, vz, plan);
        for (const BuildRequest& b : plan.build)
            r.OnBuilt(b.unit, b.version, b.neighbourMask, 1000);
        now += 16;
        if (plan.build.empty() && plan.evict.empty())
            break;
    }
}

static void TestLevels() {
    ChunkResidency r;
    r.Configure(SmallConfig());
    CHECK(r.LevelForDistance(0) == 0);
    CHECK(r.LevelForDistance(3) == 0);
    CHECK(r.LevelForDistance(4) == 1);
    CHECK(r.LevelForDistance(5) == 1);
    CHECK(r.LevelForDistance(6) == 2);
    CHECK(r.LevelForDistance(12) == 2);
    CHECK(r.LevelForDistance(13) == -1);
    int prev = 0;
    for (int d = 0; d <= 12; d++) {
        CHECK(r.LevelForDistance(d) >= prev);
        prev = r.LevelForDistance(d);
    }
}

static void TestSteadyStateCoverage() {
    ChunkResidency r;
    r.Configure(SmallConfig());
    std::set<std::pair<int, int>> present;
    Populate(r, present, 11);
    uint64_t now = 1000;
    Settle(r, now, {0, 0});
    std::map<std::pair<int, int>, int> count;
    CHECK(InRangeCoverage(r, present, count));
    for (const auto& [col, n] : count)
        CHECK(n == 1);
    // near columns are full meshes, far ones merged
    std::vector<ChunkResidency::DrawItem> draw;
    r.CollectDraw(draw);
    size_t level0 = 0, coarse = 0;
    for (const auto& d : draw)
        (d.unit.level == 0 ? level0 : coarse)++;
    CHECK(level0 >= 49 && level0 <= 100);
    CHECK(coarse > 0);
    CHECK(draw.size() < present.size());
    // nothing left to do once settled
    Plan plan;
    r.Tick(now, {0, 0}, 0, 0, plan);
    CHECK(plan.build.empty() && plan.evict.empty());
}

static void TestBuildOrderAndLimits() {
    ChunkResidency r;
    ResidencyConfig c = SmallConfig();
    c.maxBuildsPerTick = 5;
    c.maxInFlight = 8;
    r.Configure(c);
    std::set<std::pair<int, int>> present;
    Populate(r, present, 3);
    Plan plan;
    r.Tick(0, {0, 0}, 0, 0, plan);
    CHECK(plan.build.size() == 5);
    // nearest first: the camera column leads
    CHECK(plan.build[0].unit.x == 0 && plan.build[0].unit.z == 0);
    for (size_t i = 1; i < plan.build.size(); i++) {
        const auto d = [](const UnitKey& u) { return std::max(std::abs(u.x), std::abs(u.z)); };
        CHECK(d(plan.build[i - 1].unit) <= d(plan.build[i].unit));
    }
    // requested builds are not asked for again while in flight
    Plan second;
    r.Tick(16, {0, 0}, 0, 0, second);
    CHECK(second.build.size() == 3);
    for (const auto& b : second.build)
        for (const auto& a : plan.build)
            CHECK(!(a.unit == b.unit));
    Plan third;
    r.Tick(32, {0, 0}, 0, 0, third);
    CHECK(third.build.empty());
}

static void TestRetention() {
    ChunkResidency r;
    r.Configure(SmallConfig());
    std::set<std::pair<int, int>> present;
    Populate(r, present, 3);
    uint64_t now = 0;
    Settle(r, now, {0, 0});
    const UnitKey far{0, -3, 0};
    CHECK(r.HasMesh(far) && !r.IsRetained(far));

    // camera walks east: the western columns fall out of range of the small world
    ColumnPos cam{0, 0};
    for (int i = 0; i < 12; i++) {
        cam.x++;
        Settle(r, now, cam);
    }
    // out of range now (distance 15 > 12) but kept for a while
    CHECK(r.IsRetained(far));
    std::vector<ChunkResidency::DrawItem> draw;
    r.CollectDraw(draw);
    for (const auto& d : draw)
        CHECK(!(d.unit == far));
    const uint64_t revivedBefore = r.Stats().retainedRevived;

    // coming back before the timeout needs no build
    for (int i = 0; i < 12; i++) {
        cam.x--;
        Plan plan;
        r.Tick(now, cam, 0, 0, plan);
        now += 16;
        for (const auto& b : plan.build)
            CHECK(!(b.unit == far));
        for (const BuildRequest& b : plan.build)
            r.OnBuilt(b.unit, b.version, b.neighbourMask, 1000);
    }
    Plan plan;
    r.Tick(now, cam, 0, 0, plan);
    CHECK(!r.IsRetained(far) && r.HasMesh(far));
    CHECK(r.Stats().retainedRevived > revivedBefore);

    // gone for longer than retainMs: dropped
    for (int i = 0; i < 12; i++)
        cam.x += 2;
    Settle(r, now, cam);
    CHECK(r.IsRetained(far));
    now += 5000;
    Plan late;
    r.Tick(now, cam, 0, 0, late);
    bool evicted = false;
    for (const UnitKey& k : late.evict)
        evicted = evicted || k == far;
    CHECK(evicted);
    CHECK(!r.HasMesh(far));
}

static void TestRetentionBudget() {
    ChunkResidency r;
    ResidencyConfig c = SmallConfig();
    c.retainMs = 1000000;
    c.retainBudgetBytes = 3000;
    r.Configure(c);
    std::set<std::pair<int, int>> present;
    Populate(r, present, 3);
    uint64_t now = 0;
    Settle(r, now, {0, 0});
    // the world teleports away; all 49 meshes become retained at 1000 bytes each
    ColumnPos cam{100, 0};
    Plan plan;
    r.Tick(now, cam, 0, 0, plan);
    CHECK(r.Stats().retainedBytes <= 3000);
    CHECK(r.Stats().retained == 3);
    CHECK(plan.evict.size() == 46);
    // the survivors are the ones closest to the camera, which is the eastern edge
    int eastern = 0;
    for (int z = -3; z <= 3; z++)
        if (r.HasMesh({0, 3, z}))
            eastern++;
    CHECK(eastern == 3);
}

static void TestLookaheadKeepsForwardMeshes() {
    ChunkResidency r;
    ResidencyConfig c = SmallConfig();
    c.retainMs = 1000000;
    c.retainBudgetBytes = 1000;
    c.lookaheadChunks = 40.0f;
    r.Configure(c);
    std::set<std::pair<int, int>> present;
    Populate(r, present, 3);
    uint64_t now = 0;
    Settle(r, now, {0, 0});
    // camera far east but moving west fast: the western meshes are the likely return trip
    Plan plan;
    r.Tick(now, {60, 0}, -2.0f, 0.0f, plan);
    CHECK(r.Stats().retained == 1);
    bool west = false;
    for (int z = -3; z <= 3; z++)
        west = west || r.HasMesh({0, -3, z});
    CHECK(west);
}

static void TestDirtyAndStale() {
    ChunkResidency r;
    r.Configure(SmallConfig());
    std::set<std::pair<int, int>> present;
    Populate(r, present, 2);
    uint64_t now = 0;
    Settle(r, now, {0, 0});
    r.MarkDirty({1, 1});
    Plan plan;
    r.Tick(now, {0, 0}, 0, 0, plan);
    CHECK(plan.build.size() == 1);
    CHECK(plan.build[0].unit.x == 1 && plan.build[0].unit.z == 1);
    // the old mesh keeps drawing until the new one lands
    std::map<std::pair<int, int>, int> count;
    CHECK(InRangeCoverage(r, present, count));
    CHECK((count[std::make_pair(1, 1)] == 1));
}

static void TestNeighbourArrival() {
    ChunkResidency r;
    r.Configure(SmallConfig());
    r.SetData({0, 0}, true);
    uint64_t now = 0;
    Plan plan;
    r.Tick(now, {0, 0}, 0, 0, plan);
    CHECK(plan.build.size() == 1 && plan.build[0].neighbourMask == 0);
    r.OnBuilt(plan.build[0].unit, plan.build[0].version, plan.build[0].neighbourMask, 100);
    Plan quiet;
    r.Tick(now + 16, {0, 0}, 0, 0, quiet);
    CHECK(quiet.build.empty());
    // the east neighbour loads: the border faces held back must be rebuilt
    r.SetData({1, 0}, true);
    Plan again;
    r.Tick(now + 32, {0, 0}, 0, 0, again);
    bool rebuilt = false;
    for (const auto& b : again.build)
        if (b.unit.x == 0 && b.unit.z == 0) {
            rebuilt = true;
            CHECK((b.neighbourMask & 2) != 0);
        }
    CHECK(rebuilt);
}

static void TestFailureBackoff() {
    ChunkResidency r;
    r.Configure(SmallConfig());
    r.SetData({0, 0}, true);
    Plan plan;
    r.Tick(1000, {0, 0}, 0, 0, plan);
    CHECK(plan.build.size() == 1);
    r.OnBuildFailed(plan.build[0].unit);
    Plan soon;
    r.Tick(1016, {0, 0}, 0, 0, soon);
    CHECK(soon.build.empty());
    Plan later;
    r.Tick(2000, {0, 0}, 0, 0, later);
    CHECK(later.build.size() == 1);
}

// a camera flying through the world with slow builds never leaves a hole once the world is loaded
static void TestNoHolesWhileMoving() {
    ChunkResidency r;
    ResidencyConfig c = SmallConfig();
    c.maxBuildsPerTick = 3;
    c.maxInFlight = 6;
    c.lodRadius = 10;
    r.Configure(c);
    std::set<std::pair<int, int>> present;
    const int half = 40;
    for (int z = -10; z <= 10; z++)
        for (int x = -half; x <= half; x++) {
            r.SetData({x, z}, true);
            present.insert({x, z});
        }
    std::vector<BuildRequest> inflight;
    uint64_t now = 0;
    ColumnPos cam{-20, 0};
    // preload around the start
    {
        uint64_t t = 0;
        for (int i = 0; i < 400; i++) {
            Plan plan;
            r.Tick(t, cam, 0, 0, plan);
            for (const BuildRequest& b : plan.build)
                r.OnBuilt(b.unit, b.version, b.neighbourMask, 500);
            t += 16;
        }
        now = t;
    }
    std::map<std::pair<int, int>, int> count;
    std::set<std::pair<int, int>> wasCovered;
    InRangeCoverage(r, {}, count);
    for (const auto& [col, n] : count)
        wasCovered.insert(col);
    for (int step = 0; step < 300; step++) {
        if (step % 4 == 0)
            cam.x++;
        Plan plan;
        r.Tick(now, cam, 0.25f, 0, plan);
        // builds land one tick after they were asked for
        for (const BuildRequest& b : inflight)
            r.OnBuilt(b.unit, b.version, b.neighbourMask, 500);
        inflight = plan.build;
        now += 16;
        // a column that was on screen and is still in range must never drop out while its replacement builds
        std::set<std::pair<int, int>> mustCover;
        for (const auto& p : wasCovered)
            if (present.count(p) &&
                std::max(std::abs(p.first - cam.x), std::abs(p.second - cam.z)) <= c.lodRadius)
                mustCover.insert(p);
        CHECK(InRangeCoverage(r, mustCover, count));
        wasCovered.clear();
        for (const auto& [col, n] : count)
            wasCovered.insert(col);
    }
}

int main() {
    TestSingleBlock();
    TestSolidSection();
    TestRandomAgainstNaive();
    TestWinding();
    TestExtraKeySplits();
    TestMaskCache();
    TestLevels();
    TestSteadyStateCoverage();
    TestBuildOrderAndLimits();
    TestRetention();
    TestRetentionBudget();
    TestLookaheadKeepsForwardMeshes();
    TestDirtyAndStale();
    TestNeighbourArrival();
    TestFailureBackoff();
    TestNoHolesWhileMoving();
    if (g_failed) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::puts("chunk_system_selftest ok");
    return 0;
}
