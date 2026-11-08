#include "gpu/VanillaGpuProgram.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <tuple>

namespace perlin::server::vanilla {
    namespace {
        uint32_t Bits(float f) {
            uint32_t u;
            std::memcpy(&u, &f, 4);
            return u;
        }
        void Bits(double d, uint32_t& lo, uint32_t& hi) {
            uint64_t u;
            std::memcpy(&u, &d, 8);
            lo = static_cast<uint32_t>(u);
            hi = static_cast<uint32_t>(u >> 32);
        }

        struct Builder {
            GpuProgram& p;
            std::string error;
            std::map<std::tuple<const DfNode*, uint32_t, int32_t>, uint32_t> index;
            uint32_t ctxMask = 0;
            int32_t ctxValue = 0;
            bool haveCell = false;
            std::map<const NormalNoise*, uint32_t> noiseIndex;
            std::vector<std::vector<uint32_t>> children;

            uint32_t Layers(const std::vector<std::pair<const PerlinNoise*, std::pair<double, float>>>& list,
                            const std::vector<double>& fudge) {
                const uint32_t start = static_cast<uint32_t>(p.layers.size() / GpuProgram::kLayerStride);
                for (size_t i = 0; i < list.size(); i++) {
                    const PerlinNoise& n = *list[i].first;
                    uint32_t rec[GpuProgram::kLayerStride] = {};
                    Bits(list[i].second.first, rec[0], rec[1]);
                    rec[2] = Bits(list[i].second.second);
                    Bits(n.OffsetX(), rec[3], rec[4]);
                    Bits(n.OffsetY(), rec[5], rec[6]);
                    Bits(n.OffsetZ(), rec[7], rec[8]);
                    rec[9] = static_cast<uint32_t>(p.perms.size());
                    Bits(i < fudge.size() ? fudge[i] : 0.0, rec[10], rec[11]);
                    p.layers.insert(p.layers.end(), rec, rec + GpuProgram::kLayerStride);
                    for (uint8_t v : n.Perms())
                        p.perms.push_back(v);
                }
                return start;
            }

            uint32_t Noise(const NormalNoise* noise) {
                if (const auto it = noiseIndex.find(noise); it != noiseIndex.end())
                    return it->second;
                std::vector<std::pair<const PerlinNoise*, std::pair<double, float>>> list;
                for (const NormalNoise::Layer& l : noise->Layers())
                    list.push_back({&l.noise, {l.frequency, l.amplitude}});
                const uint32_t start = Layers(list, {});
                const uint32_t id = static_cast<uint32_t>(p.noises.size() / 2);
                p.noises.push_back(start);
                p.noises.push_back(static_cast<uint32_t>(list.size()));
                noiseIndex[noise] = id;
                return id;
            }

            uint32_t Stack(const std::vector<BlendedNoise::Layer>& stack, uint32_t& count) {
                std::vector<std::pair<const PerlinNoise*, std::pair<double, float>>> list;
                std::vector<double> fudge;
                for (const BlendedNoise::Layer& l : stack) {
                    list.push_back({&l.noise, {l.frequency, l.amplitude}});
                    fudge.push_back(l.noise.FudgeYScale());
                }
                count = static_cast<uint32_t>(list.size());
                return Layers(list, fudge);
            }

            static uint32_t Depth(const SplineNode& s) {
                uint32_t d = 0;
                for (const auto& nested : s.nested)
                    if (nested)
                        d = std::max(d, Depth(*nested) + 1);
                return d;
            }

            uint32_t Spline(const SplineNode& s, std::vector<uint32_t>& deps) {
                std::vector<uint32_t> nestedOffsets(s.locations.size(), 0);
                for (size_t k = 0; k < s.locations.size(); k++)
                    if (s.nested[k])
                        nestedOffsets[k] = Spline(*s.nested[k], deps);
                deps.push_back(Visit(*s.coordinate));
                const uint32_t at = static_cast<uint32_t>(p.aux.size());
                p.aux.push_back(deps.back());
                p.aux.push_back(static_cast<uint32_t>(s.locations.size()));
                for (size_t k = 0; k < s.locations.size(); k++) {
                    p.aux.push_back(Bits(s.locations[k]));
                    p.aux.push_back(Bits(s.derivatives[k]));
                    p.aux.push_back(s.nested[k] ? 1u : 0u);
                    p.aux.push_back(s.nested[k] ? nestedOffsets[k] : Bits(s.constants[k]));
                }
                return at;
            }

            uint32_t Visit(const DfNode& n) {
                const auto key = std::make_tuple(&n, ctxMask, ctxValue);
                if (const auto it = index.find(key); it != index.end())
                    return it->second;
                std::vector<uint32_t> deps;
                uint32_t rec[GpuProgram::kNodeStride] = {};
                rec[0] = static_cast<uint32_t>(n.op);
                rec[1] = rec[2] = rec[3] = 0xFFFFFFFFu;
                rec[12] = ctxMask;
                rec[13] = static_cast<uint32_t>(ctxValue);
                using O = DfNode::Op;
                switch (n.op) {
                case O::Constant:
                    rec[4] = Bits(n.value);
                    break;
                case O::Add:
                case O::Sub:
                case O::Mul:
                case O::Div:
                case O::Min:
                case O::Max:
                    rec[1] = Visit(*n.in[0]);
                    rec[2] = Visit(*n.in[1]);
                    deps = {rec[1], rec[2]};
                    break;
                case O::Abs:
                case O::Square:
                case O::Cube:
                case O::HalfNegative:
                case O::QuarterNegative:
                case O::Squeeze:
                case O::Negate:
                case O::Pass:
                    rec[1] = Visit(*n.in[0]);
                    deps = {rec[1]};
                    break;
                case O::Clamp:
                    rec[1] = Visit(*n.in[0]);
                    deps = {rec[1]};
                    rec[4] = Bits(n.lo);
                    rec[5] = Bits(n.hi);
                    break;
                case O::Noise:
                    rec[6] = Noise(n.noise);
                    if (!n.in.empty()) {
                        rec[7] = 1;
                        rec[1] = Visit(*n.in[0]);
                        deps.push_back(rec[1]);
                        if (n.in[1]) {
                            rec[2] = Visit(*n.in[1]);
                            deps.push_back(rec[2]);
                        }
                        rec[3] = Visit(*n.in[2]);
                        deps.push_back(rec[3]);
                    }
                    Bits(n.xzScale, rec[8], rec[9]);
                    Bits(n.yScale, rec[10], rec[11]);
                    break;
                case O::ShiftA:
                case O::ShiftB:
                case O::Shift:
                    rec[6] = Noise(n.noise);
                    break;
                case O::Gradient:
                    rec[6] = static_cast<uint32_t>(n.axis);
                    rec[7] = static_cast<uint32_t>(n.fromCoordinate);
                    rec[1] = static_cast<uint32_t>(n.minCoordinate);
                    rec[2] = static_cast<uint32_t>(n.maxCoordinate);
                    rec[4] = Bits(n.fromValue);
                    rec[5] = Bits(n.coordinateFactor);
                    break;
                case O::Lerp:
                    rec[1] = Visit(*n.in[0]);
                    rec[2] = Visit(*n.in[1]);
                    rec[3] = Visit(*n.in[2]);
                    deps = {rec[1], rec[2], rec[3]};
                    break;
                case O::RangeChoice:
                    rec[1] = Visit(*n.in[0]);
                    rec[2] = Visit(*n.in[1]);
                    rec[3] = Visit(*n.in[2]);
                    deps = {rec[1], rec[2], rec[3]};
                    rec[4] = Bits(n.lo);
                    rec[5] = Bits(n.hi);
                    break;
                case O::IntervalSelect: {
                    rec[1] = Visit(*n.in[0]);
                    deps.push_back(rec[1]);
                    std::vector<uint32_t> fns;
                    for (size_t i = 1; i < n.in.size(); i++) {
                        fns.push_back(Visit(*n.in[i]));
                        deps.push_back(fns.back());
                    }
                    rec[6] = static_cast<uint32_t>(p.aux.size());
                    p.aux.push_back(static_cast<uint32_t>(n.thresholds.size()));
                    for (float t : n.thresholds)
                        p.aux.push_back(Bits(t));
                    for (uint32_t f : fns)
                        p.aux.push_back(f);
                    break;
                }
                case O::Spline:
                    if (Depth(*n.spline) >= GpuProgram::kMaxSplineDepth)
                        error = "spline nested " + std::to_string(Depth(*n.spline)) +
                                " deep: the kernel has " + std::to_string(GpuProgram::kMaxSplineDepth) +
                                " levels";
                    rec[6] = Spline(*n.spline, deps);
                    break;
                case O::Interpolated:
                    if (ctxMask != 0)
                        error = "an interpolated node inside a slice";
                    if (!haveCell) {
                        p.cellXz = static_cast<uint32_t>(n.cellXz);
                        p.cellY = static_cast<uint32_t>(n.cellY);
                        haveCell = true;
                        if (16 % n.cellXz != 0)
                            error = "interpolation cells of " + std::to_string(n.cellXz) + " do not divide a chunk";
                    } else if (p.cellXz != static_cast<uint32_t>(n.cellXz) || p.cellY != static_cast<uint32_t>(n.cellY)) {
                        error = "interpolated nodes with different cell sizes";
                    }
                    rec[1] = Visit(*n.in[0]);
                    deps = {rec[1]};
                    rec[6] = static_cast<uint32_t>(p.interpolatedInputs.size());
                    p.interpolatedInputs.push_back(rec[1]);
                    break;
                case O::OldBlendedNoise: {
                    const BlendedNoise& b = *n.blended;
                    uint32_t blend[GpuProgram::kBlendStride] = {};
                    Bits(b.XzMultiplier(), blend[0], blend[1]);
                    Bits(b.YMultiplier(), blend[2], blend[3]);
                    Bits(b.XzMultiplier() / b.XzFactor(), blend[4], blend[5]);
                    Bits(b.YMultiplier() / b.YFactor(), blend[6], blend[7]);
                    uint32_t c;
                    blend[8] = Stack(b.MinLimit(), c);
                    blend[9] = c;
                    blend[10] = Stack(b.MaxLimit(), c);
                    blend[11] = c;
                    blend[12] = Stack(b.Main(), c);
                    blend[13] = c;
                    rec[6] = static_cast<uint32_t>(p.blends.size() / GpuProgram::kBlendStride);
                    p.blends.insert(p.blends.end(), blend, blend + GpuProgram::kBlendStride);
                    break;
                }
                case O::Slice: {
                    const uint32_t bit = 1u << static_cast<uint32_t>(n.axis);
                    if (ctxMask != 0 && ctxMask != bit)
                        error = "slices on two axes";
                    const uint32_t savedMask = ctxMask;
                    const int32_t savedValue = ctxValue;
                    ctxMask = bit;
                    ctxValue = n.fromCoordinate;
                    const uint32_t child = Visit(*n.in[0]);
                    ctxMask = savedMask;
                    ctxValue = savedValue;
                    rec[1] = child;
                    deps = {child};
                    break;
                }
                case O::DistanceToPoint:
                    rec[4] = static_cast<uint32_t>(n.point[0]);
                    rec[5] = static_cast<uint32_t>(n.point[1]);
                    rec[6] = static_cast<uint32_t>(n.point[2]);
                    rec[7] = static_cast<uint32_t>(n.metric);
                    break;
                case O::EndIslands:
                    if (!n.simplex || !n.simplex->OffsetFree())
                        error = "end islands noise with an offset";
                    rec[6] = static_cast<uint32_t>(p.perms.size());
                    if (n.simplex)
                        for (uint8_t v : n.simplex->Perms())
                            p.perms.push_back(v);
                    break;
                case O::FindTopSurface:
                    error = "find_top_surface is not supported on the gpu";
                    break;
                case O::Unsupported:
                    error = "unsupported density function: " + n.unsupported;
                    break;
                }
                const uint32_t id = static_cast<uint32_t>(p.nodes.size() / GpuProgram::kNodeStride);
                p.nodes.insert(p.nodes.end(), rec, rec + GpuProgram::kNodeStride);
                children.push_back(deps);
                p.source.push_back(&n);
                index[key] = id;
                return id;
            }
        };
    }

    std::unique_ptr<GpuProgram> GpuProgram::Build(const DfPtr& rootNode, const std::array<DfPtr, 6>& climate,
                                                  std::string& error) {
        auto p = std::make_unique<GpuProgram>();
        Builder b{*p, {}, {}, {}, {}};
        p->root = b.Visit(*rootNode);
        const size_t interpolatedBefore = p->interpolatedInputs.size();
        for (size_t i = 0; i < 6; i++) {
            if (!climate[i]) {
                error = "a climate function is missing";
                return nullptr;
            }
            p->climateRoots[i] = b.Visit(*climate[i]);
        }
        if (p->interpolatedInputs.size() != interpolatedBefore && b.error.empty())
            b.error =
                "the climate functions contain an interpolated node (they are sampled at single points)";
        if (!b.error.empty()) {
            error = b.error;
            return nullptr;
        }
        p->nodeCount = static_cast<uint32_t>(p->nodes.size() / kNodeStride);
        if (p->nodeCount > kMaxNodes) {
            error = "density graph has " + std::to_string(p->nodeCount) +
                    " nodes, more than the kernel's register file holds";
            return nullptr;
        }

        std::set<uint32_t> outer, inner;
        const auto isInterp = [&](uint32_t n) {
            return p->nodes[n * kNodeStride] == static_cast<uint32_t>(DfNode::Op::Interpolated);
        };
        std::vector<uint32_t> stack = {p->root};
        while (!stack.empty()) {
            const uint32_t n = stack.back();
            stack.pop_back();
            if (!outer.insert(n).second)
                continue;
            if (isInterp(n))
                continue;
            for (uint32_t c : b.children[n])
                stack.push_back(c);
        }
        for (uint32_t k : p->interpolatedInputs)
            stack.push_back(k);
        while (!stack.empty()) {
            const uint32_t n = stack.back();
            stack.pop_back();
            if (!inner.insert(n).second)
                continue;
            if (isInterp(n)) {
                error = "nested interpolated density functions are not supported on the gpu";
                return nullptr;
            }
            for (uint32_t c : b.children[n])
                stack.push_back(c);
        }

        std::set<uint32_t> climateSet;
        for (uint32_t r : p->climateRoots)
            stack.push_back(r);
        while (!stack.empty()) {
            const uint32_t n = stack.back();
            stack.pop_back();
            if (!climateSet.insert(n).second)
                continue;
            for (uint32_t c : b.children[n])
                stack.push_back(c);
        }
        p->climateOrder.assign(climateSet.begin(), climateSet.end());
        for (uint32_t n : inner)
            p->order.push_back(n);
        p->innerCount = static_cast<uint32_t>(inner.size());
        for (uint32_t n : outer)
            p->order.push_back(n);
        p->outerCount = static_cast<uint32_t>(outer.size());
        return p;
    }

    std::vector<uint32_t> GpuProgram::Pack() const {
        std::vector<uint32_t> out(kHeaderWords, 0);
        const auto append = [&](size_t slot, const std::vector<uint32_t>& table) {
            out[slot] = static_cast<uint32_t>(out.size());
            out.insert(out.end(), table.begin(), table.end());
        };
        append(HNodes, nodes);
        append(HAux, aux);
        append(HNoises, noises);
        append(HLayers, layers);
        append(HPerms, perms);
        append(HBlends, blends);
        append(HOrder, order);
        append(HInterp, interpolatedInputs);
        std::vector<uint32_t> climate;
        climate.push_back(static_cast<uint32_t>(climateOrder.size()));
        climate.insert(climate.end(), climateOrder.begin(), climateOrder.end());
        climate.insert(climate.end(), climateRoots.begin(), climateRoots.end());
        append(HClimate, climate);
        return out;
    }

    std::vector<uint32_t> PackTables(const GpuProgram& program, const VanillaSurface& surface) {
        std::vector<uint32_t> out = program.Pack();
        out[GpuProgram::HBiomes] = static_cast<uint32_t>(out.size());
        out.push_back(static_cast<uint32_t>(kBiomePointCount));
        for (int i = 0; i < kBiomePointCount; i++) {
            for (int k = 0; k < kBiomePointStride; k++)
                out.push_back(static_cast<uint32_t>(static_cast<int32_t>(
                    kBiomePointData[static_cast<size_t>(i) * kBiomePointStride + static_cast<size_t>(k)])));
            out.push_back(kBiomePointBiome[i]);
        }
        out[GpuProgram::HMaterials] = static_cast<uint32_t>(out.size());
        for (uint32_t id :
             {uint32_t(surface.Stone()), uint32_t(surface.Deepslate()), uint32_t(surface.Bedrock()),
              uint32_t(surface.Dirt()), uint32_t(surface.GrassBlock()), 0xFFFFu, 0xFFFFu, 0xFFFFu})
            out.push_back(id);
        for (const SurfaceMaterial& m : surface.Materials()) {
            for (uint32_t w : {static_cast<uint32_t>(m.top), static_cast<uint32_t>(m.mid),
                               static_cast<uint32_t>(m.foundation), static_cast<uint32_t>(m.seaFloor),
                               static_cast<uint32_t>(m.sea), static_cast<uint32_t>(m.seaFloorDepth),
                               static_cast<uint32_t>(m.builder), 0u})
                out.push_back(w);
        }
        return out;
    }
}
