#include "world/vanilla/DensityFunction.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace perlin::server::vanilla {
    namespace {
        struct CornerMap {
            struct Entry {
                const DfNode* node = nullptr;
                int x = 0, y = 0, z = 0;
                float value = 0;
            };
            std::vector<Entry> table = std::vector<Entry>(1 << 16);
            size_t used = 0;
            static size_t Hash(const DfNode* node, int x, int y, int z) {
                uint64_t h = reinterpret_cast<uintptr_t>(node) * 0x9E3779B97F4A7C15ull;
                h ^= (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32 | static_cast<uint32_t>(z)) *
                     0xC2B2AE3D27D4EB4Full;
                h ^= static_cast<uint64_t>(static_cast<uint32_t>(y)) * 0x165667B19E3779F9ull;
                return static_cast<size_t>(h ^ (h >> 29));
            }
            Entry* Find(const DfNode* node, int x, int y, int z, bool& found) {
                if (used * 2 >= table.size()) {
                    if (table.size() >= (size_t{1} << 20)) {
                        std::fill(table.begin(), table.end(), Entry{});
                        used = 0;
                    } else {
                        Grow();
                    }
                }
                const size_t mask = table.size() - 1;
                for (size_t i = Hash(node, x, y, z) & mask;; i = (i + 1) & mask) {
                    Entry& e = table[i];
                    if (!e.node) {
                        found = false;
                        return &e;
                    }
                    if (e.node == node && e.x == x && e.y == y && e.z == z) {
                        found = true;
                        return &e;
                    }
                }
            }
            void Grow() {
                std::vector<Entry> old(table.size() * 2);
                old.swap(table);
                const size_t mask = table.size() - 1;
                for (const Entry& e : old)
                    if (e.node) {
                        size_t i = Hash(e.node, e.x, e.y, e.z) & mask;
                        while (table[i].node)
                            i = (i + 1) & mask;
                        table[i] = e;
                    }
            }
        };
        thread_local CornerMap* t_corners = nullptr;

        struct FlatSlot {
            const DfNode* node = nullptr;
            int x = 0, z = 0;
            float value = 0;
        };
        struct OnceSlot {
            const DfNode* node = nullptr;
            int x = 0, y = 0, z = 0;
            float value = 0;
        };
        struct CellSlot {
            const DfNode* node = nullptr;
            int x = 0, y = 0, z = 0;
            float v[8] = {};
        };
        constexpr size_t kFlatSlots = 1 << 14, kOnceSlots = 1 << 8, kCellSlots = 1 << 7;
        struct Memo {
            std::vector<FlatSlot> flat = std::vector<FlatSlot>(kFlatSlots);
            std::vector<OnceSlot> once = std::vector<OnceSlot>(kOnceSlots);
            std::vector<CellSlot> cells = std::vector<CellSlot>(kCellSlots);
        };
        thread_local Memo* t_memo = nullptr;

        size_t PtrHash(const void* p) {
            const uint64_t h = reinterpret_cast<uintptr_t>(p) * 0x9E3779B97F4A7C15ull;
            return static_cast<size_t>(h >> 40);
        }

        float Lerp(float t, float a, float b) {
            return a + t * (b - a);
        }
        float Clamp(float v, float lo, float hi) {
            return v < lo ? lo : std::min(v, hi);
        }

        float JMin(float a, float b) {
            if (a != a)
                return a;
            if (a == 0.0f && b == 0.0f)
                return std::signbit(a) ? a : b;
            return a <= b ? a : b;
        }
        float JMax(float a, float b) {
            if (a != a)
                return a;
            if (a == 0.0f && b == 0.0f)
                return std::signbit(a) ? b : a;
            return a >= b ? a : b;
        }

        std::string Strip(const std::string& s) {
            return s.starts_with("minecraft:") ? s.substr(10) : s;
        }
    }

    namespace {
        thread_local CornerMap* t_keptCorners = nullptr;
        thread_local Memo* t_keptMemo = nullptr;
        thread_local uint64_t t_keptEpoch = 0;
        std::atomic<uint64_t> g_graphEpoch{1};
    }

    DfCacheScope::DfCacheScope(bool keep) {
        if (t_corners)
            return;
        owner = true;
        kept = keep;
        if (keep) {
            const uint64_t epoch = g_graphEpoch.load(std::memory_order_acquire);
            if (t_keptCorners && t_keptEpoch != epoch) {
                delete t_keptCorners;
                delete t_keptMemo;
                t_keptCorners = nullptr;
                t_keptMemo = nullptr;
            }
            if (!t_keptCorners) {
                t_keptCorners = new CornerMap();
                t_keptMemo = new Memo();
                t_keptEpoch = epoch;
            }
            t_corners = t_keptCorners;
            t_memo = t_keptMemo;
            return;
        }
        t_corners = new CornerMap();
        t_memo = new Memo();
    }
    DfCacheScope::~DfCacheScope() {
        if (!owner)
            return;
        if (!kept) {
            delete t_corners;
            delete t_memo;
        }
        t_corners = nullptr;
        t_memo = nullptr;
    }

    float SplineNode::Eval(int x, int y, int z) const {
        const float c = coordinate->Eval(x, y, z);
        const int n = static_cast<int>(locations.size());

        int lo = 0, hi = n;
        while (lo < hi) {
            const int mid = (lo + hi) / 2;
            if (c < locations[static_cast<size_t>(mid)])
                hi = mid;
            else
                lo = mid + 1;
        }
        const int i = lo - 1;
        const auto valueAt = [&](int k) {
            return nested[static_cast<size_t>(k)] ? nested[static_cast<size_t>(k)]->Eval(x, y, z)
                                                  : constants[static_cast<size_t>(k)];
        };
        const auto extend = [&](int k) {
            const float v = valueAt(k);
            const float d = derivatives[static_cast<size_t>(k)];
            return d == 0.0f ? v : v + d * (c - locations[static_cast<size_t>(k)]);
        };
        if (i < 0)
            return extend(0);
        if (i == n - 1)
            return extend(n - 1);
        const float x1 = locations[static_cast<size_t>(i)], x2 = locations[static_cast<size_t>(i + 1)];
        const float t = (c - x1) / (x2 - x1);
        const float y1 = valueAt(i), y2 = valueAt(i + 1);
        const float d1 = derivatives[static_cast<size_t>(i)], d2 = derivatives[static_cast<size_t>(i + 1)];
        const float a = d1 * (x2 - x1) - (y2 - y1);
        const float b = -d2 * (x2 - x1) + (y2 - y1);
        return Lerp(t, y1, y2) + t * (1.0f - t) * Lerp(t, a, b);
    }

    float DfNode::Eval(int x, int y, int z) const {
        if (cacheKind == 0 || !t_memo)
            return EvalUncached(x, y, z);
        if (cacheKind == 1) {
            const uint64_t h = (PtrHash(this) * 0x100000001B3ull) ^
                               (static_cast<uint64_t>(static_cast<uint32_t>(x)) * 0xC2B2AE3D27D4EB4Full) ^
                               (static_cast<uint64_t>(static_cast<uint32_t>(z)) * 0x165667B19E3779F9ull);
            FlatSlot& slot = t_memo->flat[static_cast<size_t>((h ^ (h >> 31)) & (kFlatSlots - 1))];
            if (slot.node == this && slot.x == x && slot.z == z)
                return slot.value;
            const float v = EvalUncached(x, y, z);
            slot = {this, x, z, v};
            return v;
        }
        OnceSlot& slot = t_memo->once[PtrHash(this) & (kOnceSlots - 1)];
        if (slot.node == this && slot.x == x && slot.y == y && slot.z == z)
            return slot.value;
        const float v = EvalUncached(x, y, z);
        slot = {this, x, y, z, v};
        return v;
    }

    float DfNode::EvalUncached(int x, int y, int z) const {
        using O = Op;
        switch (op) {
        case O::Constant:
            return value;
        case O::Add:
            return in[0]->Eval(x, y, z) + in[1]->Eval(x, y, z);
        case O::Sub:
            return in[0]->Eval(x, y, z) - in[1]->Eval(x, y, z);
        case O::Mul:
            return in[0]->Eval(x, y, z) * in[1]->Eval(x, y, z);
        case O::Div: {
            const float l = in[0]->Eval(x, y, z);
            return l == 0.0f ? 0.0f : l / in[1]->Eval(x, y, z);
        }
        case O::Min:
            return JMin(in[0]->Eval(x, y, z), in[1]->Eval(x, y, z));
        case O::Max:
            return JMax(in[0]->Eval(x, y, z), in[1]->Eval(x, y, z));
        case O::Abs:
            return std::fabs(in[0]->Eval(x, y, z));
        case O::Square: {
            const float v = in[0]->Eval(x, y, z);
            return v * v;
        }
        case O::Cube: {
            const float v = in[0]->Eval(x, y, z);
            return v * v * v;
        }
        case O::HalfNegative: {
            const float v = in[0]->Eval(x, y, z);
            return v > 0.0f ? v : v * 0.5f;
        }
        case O::QuarterNegative: {
            const float v = in[0]->Eval(x, y, z);
            return v > 0.0f ? v : v * 0.25f;
        }
        case O::Squeeze: {
            const float c = Clamp(in[0]->Eval(x, y, z), -1.0f, 1.0f);
            return c / 2.0f - (c * c * c) / 24.0f;
        }
        case O::Negate:
            return -in[0]->Eval(x, y, z);
        case O::Clamp:
            return Clamp(in[0]->Eval(x, y, z), lo, hi);
        case O::Noise: {

            double nx = static_cast<double>(x) * xzScale, ny = static_cast<double>(y) * yScale,
                   nz = static_cast<double>(z) * xzScale;
            if (!in.empty()) {
                nx += static_cast<double>(in[0]->Eval(x, y, z));
                nz += static_cast<double>(in[2]->Eval(x, y, z));
                if (in[1])
                    ny += static_cast<double>(in[1]->Eval(x, y, z));
            }
            return noise->Get(nx, ny, nz);
        }
        case O::ShiftA:
            return noise->Get(static_cast<double>(x) * 0.25, static_cast<double>(y) * 0.0,
                              static_cast<double>(z) * 0.25) *
                   4.0f;
        case O::ShiftB:
            return noise->Get(static_cast<double>(z) * 0.25, static_cast<double>(x) * 0.25,
                              static_cast<double>(y) * 0.0) *
                   4.0f;
        case O::Shift:
            return noise->Get(static_cast<double>(x) * 0.25, static_cast<double>(y) * 0.25,
                              static_cast<double>(z) * 0.25) *
                   4.0f;
        case O::Gradient: {
            const int c = axis == 0 ? x : axis == 1 ? y : z;
            const int clamped = std::clamp(c, minCoordinate, maxCoordinate);
            return fromValue + static_cast<float>(clamped - fromCoordinate) * coordinateFactor;
        }
        case O::Lerp: {
            const float alpha = in[0]->Eval(x, y, z);
            if (alpha == 0.0f)
                return in[1]->Eval(x, y, z);
            if (alpha == 1.0f)
                return in[2]->Eval(x, y, z);
            return Lerp(alpha, in[1]->Eval(x, y, z), in[2]->Eval(x, y, z));
        }
        case O::RangeChoice: {
            const float v = in[0]->Eval(x, y, z);
            return v >= lo && v < hi ? in[1]->Eval(x, y, z) : in[2]->Eval(x, y, z);
        }
        case O::IntervalSelect: {
            const float v = in[0]->Eval(x, y, z);
            size_t i = 0;
            while (i < thresholds.size() && v >= thresholds[i])
                i++;
            return in[1 + i]->Eval(x, y, z);
        }
        case O::Spline:
            return spline->Eval(x, y, z);
        case O::Pass:
            return in[0]->Eval(x, y, z);
        case O::Interpolated: {

            const auto mod = [](int a, int b) {
                const int r = a % b;
                return r < 0 ? r + b : r;
            };
            const int fx = mod(x, cellXz), fy = mod(y, cellY), fz = mod(z, cellXz);
            if (fx == 0 && fy == 0 && fz == 0)
                return in[0]->Eval(x, y, z);
            const int x0 = x - fx, y0 = y - fy, z0 = z - fz;
            const float tx = static_cast<float>(fx) / static_cast<float>(cellXz),
                        ty = static_cast<float>(fy) / static_cast<float>(cellY),
                        tz = static_cast<float>(fz) / static_cast<float>(cellXz);
            const auto corner = [&](int cx, int cy, int cz) {
                if (!t_corners)
                    return in[0]->Eval(cx, cy, cz);
                bool found = false;
                CornerMap::Entry* e = t_corners->Find(this, cx, cy, cz, found);
                if (found)
                    return e->value;
                const float v = in[0]->Eval(cx, cy, cz);
                e = t_corners->Find(this, cx, cy, cz, found);
                *e = {this, cx, cy, cz, v};
                t_corners->used++;
                return v;
            };
            float c[8];
            CellSlot* cell = t_memo ? &t_memo->cells[PtrHash(this) & (kCellSlots - 1)] : nullptr;
            if (cell && cell->node == this && cell->x == x0 && cell->y == y0 && cell->z == z0) {
                std::copy_n(cell->v, 8, c);
            } else {
                c[0] = corner(x0, y0, z0);
                c[1] = corner(x0 + cellXz, y0, z0);
                c[2] = corner(x0, y0 + cellY, z0);
                c[3] = corner(x0 + cellXz, y0 + cellY, z0);
                c[4] = corner(x0, y0, z0 + cellXz);
                c[5] = corner(x0 + cellXz, y0, z0 + cellXz);
                c[6] = corner(x0, y0 + cellY, z0 + cellXz);
                c[7] = corner(x0 + cellXz, y0 + cellY, z0 + cellXz);
                if (cell) {
                    cell->node = this;
                    cell->x = x0;
                    cell->y = y0;
                    cell->z = z0;
                    std::copy_n(c, 8, cell->v);
                }
            }
            const float v000 = c[0], v100 = c[1], v010 = c[2], v110 = c[3], v001 = c[4], v101 = c[5],
                        v011 = c[6], v111 = c[7];
            const float a = Lerp(ty, Lerp(tx, v000, v100), Lerp(tx, v010, v110));
            const float b = Lerp(ty, Lerp(tx, v001, v101), Lerp(tx, v011, v111));
            return Lerp(tz, a, b);
        }
        case O::OldBlendedNoise:
            return blended->Get(x, y, z);
        case O::Slice:
            return in[0]->Eval(axis == 0 ? fromCoordinate : x, axis == 1 ? fromCoordinate : y, axis == 2 ? fromCoordinate : z);
        case O::DistanceToPoint: {
            const float dx = static_cast<float>(point[0] - x), dy = static_cast<float>(point[1] - y),
                        dz = static_cast<float>(point[2] - z);
            switch (metric) {
            case 1:
                return dx * dx + dy * dy + dz * dz;
            case 2:
                return std::abs(dx) + std::abs(dy) + std::abs(dz);
            case 3:
                return std::max(std::max(std::abs(dx), std::abs(dy)), std::abs(dz));
            default:
                return static_cast<float>(std::sqrt(static_cast<double>(dx * dx + dy * dy + dz * dz)));
            }
        }
        case O::EndIslands: {
            const int sectionX = x / 8, sectionZ = z / 8;
            const int chunkX = sectionX / 2, chunkZ = sectionZ / 2;
            const int subX = sectionX % 2, subZ = sectionZ % 2;
            float offs = -100.0f;
            for (int xo = -12; xo <= 12; xo++)
                for (int zo = -12; zo <= 12; zo++) {
                    const int64_t tx = static_cast<int64_t>(chunkX + xo), tz = static_cast<int64_t>(chunkZ + zo);
                    if (tx * tx + tz * tz <= 4096 ||
                        !(simplex->Get(static_cast<double>(tx), static_cast<double>(tz)) < -0.9f))
                        continue;
                    const float size =
                        std::fmod(std::abs(static_cast<float>(tx)) * 3439.0f + std::abs(static_cast<float>(tz)) * 147.0f, 13.0f) + 9.0f;
                    const float xd = static_cast<float>(subX - xo * 2), zd = static_cast<float>(subZ - zo * 2);
                    float v = 100.0f - std::sqrt(xd * xd + zd * zd) * size;
                    v = std::clamp(v, -100.0f, 80.0f);
                    offs = std::max(offs, v);
                }
            return (offs - 8.0f) / 128.0f;
        }
        case O::FindTopSurface: {
            const float upper = in[1]->Eval(x, 0, z);
            const float q = upper / static_cast<float>(cellY);
            int floored = static_cast<int>(q);
            if (q < static_cast<float>(floored))
                floored--;
            const int top = floored * cellY;
            if (top <= minCoordinate)
                return static_cast<float>(minCoordinate);
            for (int probe = top; probe >= minCoordinate; probe -= cellY)
                if (in[0]->Eval(x, probe, z) > 0.0f)
                    return static_cast<float>(probe);
            return static_cast<float>(minCoordinate);
        }
        case O::Unsupported:
            return 0.0f;
        }
        return 0.0f;
    }

    uint64_t NextBackendInstance() {
        static std::atomic<uint64_t> next{1};
        return next.fetch_add(1);
    }

    DensityGraph::~DensityGraph() { g_graphEpoch.fetch_add(1, std::memory_order_acq_rel); }

    DensityGraph::DensityGraph(const DataPack& p, int64_t worldSeed, bool legacyRandom)
        : pack(p), seed(worldSeed), legacy(legacyRandom) {
        WorldRandom root = legacyRandom ? WorldRandom::Legacy(worldSeed) : WorldRandom::FromSeed(worldSeed);
        positional = root.ForkPositional();
    }

    const NormalNoise* DensityGraph::NoiseFor(const std::string& id) {
        const std::string full = !id.contains(':') ? "minecraft:" + id : id;
        if (const auto it = noises.find(full); it != noises.end())
            return it->second.get();
        json::Value v;
        if (!json::Parse(pack.Read("worldgen/noise/" + Strip(full) + ".json"), v))
            return nullptr;
        NoiseParameters p;
        p.baseAmplitude = v["base_amplitude"].Number(1.0);
        p.baseOctave = static_cast<int>(v["base_octave"].Int());
        p.octaveCount = static_cast<int>(v["octave_count"].Int(1));
        for (const json::Value& m : v["amplitude_modifiers"].items)
            p.amplitudeModifiers.push_back(m.Number());
        std::unique_ptr<NormalNoise> noise;
        if (full == "minecraft:nether/temperature" || full == "minecraft:nether/vegetation")
            noise = std::make_unique<NormalNoise>(NormalNoise::LegacyNetherBiome(
                p, WorldRandom::Legacy(seed + (full == "minecraft:nether/vegetation" ? 1 : 0))));
        else
            noise = std::make_unique<NormalNoise>(p, positional.FromHashOf(full));
        const NormalNoise* raw = noise.get();
        noises[full] = std::move(noise);
        return raw;
    }

    DfPtr DensityGraph::Load(const std::string& id) {
        const std::string name = Strip(id);
        if (const auto it = functions.find(name); it != functions.end())
            return it->second;
        json::Value v;
        if (!json::Parse(pack.Read("worldgen/density_function/" + name + ".json"), v))
            return nullptr;
        DfPtr node = Build(v);
        functions[name] = node;
        return node;
    }

    std::shared_ptr<const SplineNode> DensityGraph::BuildSpline(const json::Value& v) {
        auto s = std::make_shared<SplineNode>();
        s->coordinate = Build(v["coordinate"]);
        for (const json::Value& p : v["points"].items) {
            s->locations.push_back(static_cast<float>(p["location"].Number()));
            s->derivatives.push_back(static_cast<float>(p["derivative"].Number()));
            if (p["value"].IsObject()) {
                s->constants.push_back(0.0f);
                s->nested.push_back(BuildSpline(p["value"]));
            } else {
                s->constants.push_back(static_cast<float>(p["value"].Number()));
                s->nested.push_back(nullptr);
            }
        }
        return s;
    }

    namespace {
        bool SplineYDependent(const SplineNode& s) {
            if (s.coordinate && s.coordinate->yDependent)
                return true;
            for (const auto& n : s.nested)
                if (n && SplineYDependent(*n))
                    return true;
            return false;
        }
        bool YDependent(const DfNode& n) {
            using O = DfNode::Op;
            switch (n.op) {
            case O::Constant:
            case O::Unsupported:
            case O::ShiftA:
            case O::ShiftB:
            case O::FindTopSurface:
            case O::EndIslands:
                return false;
            case O::DistanceToPoint:
                return true;
            case O::Slice:
                if (n.axis == 1)
                    return false;
                break;
            case O::Gradient:
                return n.axis == 1;
            case O::Shift:
            case O::Interpolated:
            case O::OldBlendedNoise:
                return true;
            case O::Spline:
                return !n.spline || SplineYDependent(*n.spline);
            case O::Noise:
                if (n.yScale != 0.0)
                    return true;
                break;
            default:
                break;
            }
            for (const DfPtr& c : n.in)
                if (c && c->yDependent)
                    return true;
            return false;
        }
    }

    DfPtr DensityGraph::Build(const json::Value& v) {
        auto node = std::make_shared<DfNode>();
        using O = DfNode::Op;
        if (v.kind == json::Value::Kind::Number) {
            node->op = O::Constant;
            node->value = static_cast<float>(v.number);
            return node;
        }
        if (v.IsString())
            return Load(v.text);
        const std::string type = Strip(v["type"].String());
        const auto child = [&](const char* key) { return Build(v[key]); };
        const auto f = [&](const char* key, double fallback = 0.0) {
            return static_cast<float>(v[key].Number(fallback));
        };
        if (type == "add" || type == "sub" || type == "mul" || type == "div" || type == "min" ||
            type == "max") {
            node->op = type == "add"   ? O::Add
                       : type == "sub" ? O::Sub
                       : type == "mul" ? O::Mul
                       : type == "div" ? O::Div
                       : type == "min" ? O::Min
                                       : O::Max;
            node->in = {child("left"), child("right")};
        } else if (type == "abs" || type == "square" || type == "cube" || type == "half_negative" ||
                   type == "quarter_negative" || type == "squeeze" || type == "negate") {
            node->op = type == "abs"                ? O::Abs
                       : type == "square"           ? O::Square
                       : type == "cube"             ? O::Cube
                       : type == "half_negative"    ? O::HalfNegative
                       : type == "quarter_negative" ? O::QuarterNegative
                       : type == "squeeze"          ? O::Squeeze
                                                    : O::Negate;
            node->in = {child("input")};
        } else if (type == "clamp") {
            node->op = O::Clamp;
            node->in = {child("input")};
            node->lo = f("min");
            node->hi = f("max");
        } else if (type == "noise") {
            node->op = O::Noise;
            node->noise = NoiseFor(v["noise"].String());
            node->xzScale = v["xz_scale"].Number(1.0);
            node->yScale = v["y_scale"].Number(1.0);
            if (v["shift_x"].kind != json::Value::Kind::Null ||
                v["shift_y"].kind != json::Value::Kind::Null ||
                v["shift_z"].kind != json::Value::Kind::Null) {
                const auto zero = [] {
                    auto z = std::make_shared<DfNode>();
                    return DfPtr(z);
                };
                node->in = {v["shift_x"].kind == json::Value::Kind::Null ? zero() : child("shift_x"),
                            v["shift_y"].kind == json::Value::Kind::Null ? nullptr : child("shift_y"),
                            v["shift_z"].kind == json::Value::Kind::Null ? zero() : child("shift_z")};
            }
        } else if (type == "shift_a" || type == "shift_b" || type == "shift") {
            node->op = type == "shift_a" ? O::ShiftA : type == "shift_b" ? O::ShiftB : O::Shift;
            node->noise = NoiseFor(v["noise"].String());
        } else if (type == "gradient") {
            node->op = O::Gradient;
            const std::string axis = v["axis"].String();
            node->axis = axis == "x" ? 0 : axis == "y" ? 1 : 2;
            const int from = static_cast<int>(v["from_coordinate"].Int()),
                      to = static_cast<int>(v["to_coordinate"].Int());
            node->fromCoordinate = from;
            node->minCoordinate = std::min(from, to);
            node->maxCoordinate = std::max(from, to);
            node->fromValue = f("from_value");
            node->coordinateFactor = (f("to_value") - f("from_value")) / static_cast<float>(to - from);
        } else if (type == "lerp") {
            node->op = O::Lerp;
            node->in = {child("alpha"), child("first"), child("second")};
        } else if (type == "range_choice") {
            node->op = O::RangeChoice;
            node->in = {child("input"), child("when_in_range"), child("when_out_of_range")};
            node->lo = f("min_inclusive");
            node->hi = f("max_exclusive");
        } else if (type == "interval_select") {
            node->op = O::IntervalSelect;
            node->in.push_back(child("input"));
            for (const json::Value& fn : v["functions"].items)
                node->in.push_back(Build(fn));
            for (const json::Value& t : v["thresholds"].items)
                node->thresholds.push_back(static_cast<float>(t.Number()));
        } else if (type == "spline") {
            node->op = O::Spline;
            node->spline = BuildSpline(v["spline"]);
        } else if (type == "interpolated") {
            node->op = O::Interpolated;
            node->in = {child("input")};
            node->cellXz = static_cast<int>(v["cell_size_xz"].Int(4));
            node->cellY = static_cast<int>(v["cell_size_y"].Int(8));
        } else if (type == "cache" || type == "flat_cache" || type == "cache_2d" || type == "cache_once" ||
                   type == "cache_all_in_cell") {
            node->op = O::Pass;
            node->in = {child("input")};
            node->cacheKind = type == "flat_cache" || type == "cache_2d" ? 1 : 2;
        } else if (type == "blend_density") {
            node->op = O::Pass;
            node->in = {child("input")};
        } else if (type == "find_top_surface") {
            node->op = O::FindTopSurface;
            node->in = {child("density"), child("upper_bound")};
            node->minCoordinate = static_cast<int>(v["lower_bound"].Int());
            node->cellY = static_cast<int>(v["cell_height"].Int(8));
        } else if (type == "old_blended_noise") {
            node->op = O::OldBlendedNoise;

            node->blended = std::make_shared<BlendedNoise>(
                v["xz_scale"].Number(1.0), v["y_scale"].Number(1.0), v["xz_factor"].Number(80.0),
                v["y_factor"].Number(160.0), v["smear_scale_multiplier"].Number(8.0),
                legacy ? WorldRandom::Legacy(seed) : positional.FromHashOf("minecraft:terrain"));
        } else if (type == "slice") {
            node->op = O::Slice;
            node->in = {child("input")};
            const std::string axis = v["axis"].String();
            node->axis = axis == "x" ? 0 : axis == "y" ? 1 : 2;
            node->fromCoordinate = static_cast<int>(v["coordinate"].Int());
        } else if (type == "distance_to_point") {
            node->op = O::DistanceToPoint;
            for (size_t i = 0; i < 3 && i < v["point"].items.size(); i++)
                node->point[i] = static_cast<int>(v["point"].items[i].Int());
            const std::string metric = v["metric"].String();
            node->metric = metric == "euclidean_squared" ? 1 : metric == "manhattan" ? 2 : metric == "chebyshev" ? 3 : 0;
        } else if (type == "end_outer_islands") {
            node->op = O::EndIslands;
            WorldRandom r = WorldRandom::Legacy(seed);
            r.Consume(17292);
            node->simplex = std::make_shared<SimplexNoise>(r, true);
        } else if (type == "blend_alpha") {
            node->op = O::Constant;
            node->value = 1.0f;
        } else if (type == "blend_offset" || type == "beardifier") {
            node->op = O::Constant;
            node->value = 0.0f;
        } else {
            node->op = O::Unsupported;
            node->unsupported = type;
            unsupported.push_back(type);
        }
        node->yDependent = YDependent(*node);
        if (node->cacheKind == 1 && node->yDependent)
            node->cacheKind = 2;
        return node;
    }
}
