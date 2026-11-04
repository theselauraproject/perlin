#include "world/vanilla/DensityFunction.hpp"

#include <algorithm>
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

        // java's Math.min/max on floats: NaN wins, -0.0 < 0.0
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
    }

    DfCacheScope::DfCacheScope(bool keep) {
        if (t_corners)
            return;
        owner = true;
        kept = keep;
        if (keep) {
            if (!t_keptCorners) {
                t_keptCorners = new CornerMap();
                t_keptMemo = new Memo();
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
        // first location above the coordinate, minus one
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
            // shifts (when present) are evaluated at the block and added after scaling
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
            // between cell corners the value is blended from the inner function sampled at the eight corners
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
        case O::Unsupported:
            return 0.0f;
        }
        return 0.0f;
    }

    DensityGraph::DensityGraph(const DataPack& p, int64_t seed) : pack(p) {
        Xoroshiro root = Xoroshiro::FromSeed(seed);
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
        auto noise = std::make_unique<NormalNoise>(p, positional.FromHashOf(full));
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
                return false;
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
        } else if (type == "old_blended_noise") {
            node->op = O::OldBlendedNoise;
            // the generator seeds it from the world's positional random by the name "minecraft:terrain"
            node->blended = std::make_shared<BlendedNoise>(
                v["xz_scale"].Number(1.0), v["y_scale"].Number(1.0), v["xz_factor"].Number(80.0),
                v["y_factor"].Number(160.0), v["smear_scale_multiplier"].Number(8.0),
                positional.FromHashOf("minecraft:terrain"));
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
