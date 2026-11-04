#pragma once
// the density function graphs of worldgen/density_function/**.json, evaluated the way the 26.3 samplers do (float arithmetic, per block position)
#include "core/Json.hpp"
#include "world/vanilla/DataPack.hpp"
#include "world/vanilla/Noise.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace perlin::server::vanilla {

    // while a chunk is generated on a thread, interpolated nodes remember their cell corners: each is shared by up to eight cells
    class DfCacheScope {
    public:
        explicit DfCacheScope(bool keep = false);
        ~DfCacheScope();
        DfCacheScope(const DfCacheScope&) = delete;
        DfCacheScope& operator=(const DfCacheScope&) = delete;

    private:
        bool owner = false;
        bool kept = false;
    };

    struct DfNode;
    using DfPtr = std::shared_ptr<const DfNode>;

    // a cubic spline of the climate parameters (worldgen "spline" nodes)
    struct SplineNode {
        DfPtr coordinate;
        std::vector<float> locations, derivatives;
        // each point's value is a constant or another spline
        std::vector<float> constants;
        std::vector<std::shared_ptr<const SplineNode>> nested;
        float Eval(int x, int y, int z) const;
    };

    struct DfNode {
        enum class Op : uint8_t {
            Constant,
            Add,
            Sub,
            Mul,
            Div,
            Min,
            Max,
            Abs,
            Square,
            Cube,
            HalfNegative,
            QuarterNegative,
            Squeeze,
            Negate,
            Clamp,
            Noise,
            ShiftA,
            ShiftB,
            Shift,
            Gradient,
            Lerp,
            RangeChoice,
            IntervalSelect,
            Spline,
            Pass,
            Interpolated,
            OldBlendedNoise,
            Unsupported
        };
        Op op = Op::Constant;
        float value = 0;
        std::vector<DfPtr> in;
        const NormalNoise* noise = nullptr;
        double xzScale = 1, yScale = 1;
        // clamp / range choice / gradient
        float lo = 0, hi = 0;
        int axis = 1;
        int fromCoordinate = 0, minCoordinate = 0, maxCoordinate = 0;
        float fromValue = 0, coordinateFactor = 0;
        std::vector<float> thresholds;
        int cellXz = 4, cellY = 8;
        std::shared_ptr<const SplineNode> spline;
        std::shared_ptr<const BlendedNoise> blended;
        std::string unsupported;
        uint8_t cacheKind = 0;
        bool yDependent = true;

        float Eval(int x, int y, int z) const;
        float EvalUncached(int x, int y, int z) const;
    };

    // loads named functions from a data pack for one world seed
    class DensityGraph {
    public:
        DensityGraph(const DataPack& pack, int64_t seed);
        // "minecraft:overworld/final_density"; null when the file is missing
        DfPtr Load(const std::string& id);
        DfPtr Inline(const json::Value& v) { return Build(v); }
        const Xoroshiro::Factory& Positional() const { return positional; }
        // node types the evaluator does not know yet, found while loading (empty when everything loaded is supported)
        const std::vector<std::string>& Unsupported() const { return unsupported; }

    private:
        DfPtr Build(const json::Value& v);
        std::shared_ptr<const SplineNode> BuildSpline(const json::Value& v);
        const NormalNoise* NoiseFor(const std::string& id);

        const DataPack& pack;
        Xoroshiro::Factory positional;
        std::map<std::string, DfPtr> functions;
        std::map<std::string, std::unique_ptr<NormalNoise>> noises;
        std::vector<std::string> unsupported;
    };
}
