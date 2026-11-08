#pragma once

#include "core/Json.hpp"
#include "world/vanilla/DataPack.hpp"
#include "world/vanilla/Noise.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace perlin::server::vanilla {

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

    uint64_t NextBackendInstance();

    struct DfNode;
    using DfPtr = std::shared_ptr<const DfNode>;

    struct SplineNode {
        DfPtr coordinate;
        std::vector<float> locations, derivatives;

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
            FindTopSurface,
            Slice,
            DistanceToPoint,
            EndIslands,
            Unsupported
        };
        Op op = Op::Constant;
        float value = 0;
        std::vector<DfPtr> in;
        const NormalNoise* noise = nullptr;
        double xzScale = 1, yScale = 1;

        float lo = 0, hi = 0;
        int axis = 1;
        int fromCoordinate = 0, minCoordinate = 0, maxCoordinate = 0;
        float fromValue = 0, coordinateFactor = 0;
        std::vector<float> thresholds;
        int cellXz = 4, cellY = 8;
        std::shared_ptr<const SplineNode> spline;
        std::shared_ptr<const BlendedNoise> blended;
        std::shared_ptr<const SimplexNoise> simplex;
        int point[3] = {0, 0, 0};
        int metric = 0;
        std::string unsupported;
        uint8_t cacheKind = 0;
        bool yDependent = true;

        float Eval(int x, int y, int z) const;
        float EvalUncached(int x, int y, int z) const;
    };

    class DensityGraph {
    public:
        DensityGraph(const DataPack& pack, int64_t seed, bool legacyRandom = false);
        ~DensityGraph();

        DfPtr Load(const std::string& id);
        DfPtr Inline(const json::Value& v) { return Build(v); }
        const NormalNoise* Noise(const std::string& id) { return NoiseFor(id); }
        const WorldRandom::Factory& Positional() const { return positional; }
        int64_t Seed() const { return seed; }
        bool Legacy() const { return legacy; }

        const std::vector<std::string>& Unsupported() const { return unsupported; }

    private:
        DfPtr Build(const json::Value& v);
        std::shared_ptr<const SplineNode> BuildSpline(const json::Value& v);
        const NormalNoise* NoiseFor(const std::string& id);

        const DataPack& pack;
        WorldRandom::Factory positional;
        int64_t seed = 0;
        bool legacy = false;
        std::map<std::string, DfPtr> functions;
        std::map<std::string, std::unique_ptr<NormalNoise>> noises;
        std::vector<std::string> unsupported;
    };
}
