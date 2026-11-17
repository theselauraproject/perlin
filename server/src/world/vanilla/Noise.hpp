#pragma once

#include "world/vanilla/JavaRandom.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace perlin::server::vanilla {

    class PerlinNoise {
    public:
        explicit PerlinNoise(WorldRandom& random);
        float Get(double x, double y, double z) const;

        float SampleAndLerp(int ix, int iy, int iz, float fx, float fy, float fz, float smoothY) const;
        const std::array<uint8_t, 256>& Perms() const { return perms; }
        double OffsetX() const { return offsetX; }
        double OffsetY() const { return offsetY; }
        double OffsetZ() const { return offsetZ; }

    private:
        int Permute(int i) const { return perms[static_cast<size_t>(i & 255)]; }
        std::array<uint8_t, 256> perms{};
        double offsetX, offsetY, offsetZ;
    };

    class SmearedPerlinNoise : public PerlinNoise {
    public:
        SmearedPerlinNoise(WorldRandom& random, double fudgeYScale)
            : PerlinNoise(random), fudgeYScale(fudgeYScale) {}
        float Get(double x, double y, double z) const;
        double FudgeYScale() const { return fudgeYScale; }

    private:
        double fudgeYScale;
    };

    class BlendedNoise {
    public:
        BlendedNoise(double xzScale, double yScale, double xzFactor, double yFactor,
                     double smearScaleMultiplier, WorldRandom random);
        float Get(int x, int y, int z) const;

        struct Layer {
            SmearedPerlinNoise noise;
            double frequency;
            float amplitude;
        };
        const std::vector<Layer>& MinLimit() const { return minLimit; }
        const std::vector<Layer>& MaxLimit() const { return maxLimit; }
        const std::vector<Layer>& Main() const { return main; }
        double XzMultiplier() const { return xzMultiplier; }
        double YMultiplier() const { return yMultiplier; }
        double XzFactor() const { return xzFactor; }
        double YFactor() const { return yFactor; }

    private:
        static std::vector<Layer> Fbm(WorldRandom& random, int firstOctave, double smearScale,
                                      double amplitude);
        static float Sum(const std::vector<Layer>& stack, double x, double y, double z);
        std::vector<Layer> minLimit, maxLimit, main;
        double xzMultiplier, yMultiplier, xzFactor, yFactor;
    };

    struct NoiseParameters {
        double baseAmplitude = 1.0;
        int baseOctave = 0;
        int octaveCount = 1;
        bool normalize = true;
        std::vector<double> amplitudeModifiers;
    };

    class SimplexNoise {
    public:
        SimplexNoise(WorldRandom& random, bool discardOffset);
        float Get(double x, double y) const;
        const std::array<uint8_t, 256>& Perms() const { return perms; }
        bool OffsetFree() const { return offsetX == 0.0 && offsetY == 0.0 && offsetZ == 0.0; }

    private:
        int Permute(int i) const { return perms[static_cast<size_t>(i & 255)]; }
        std::array<uint8_t, 256> perms{};
        double offsetX = 0, offsetY = 0, offsetZ = 0;
    };

    class NormalNoise {
    public:

        NormalNoise(const NoiseParameters& parameters, WorldRandom random);

        static NormalNoise LegacyNetherBiome(const NoiseParameters& parameters, WorldRandom random);
        float Get(double x, double y, double z) const;

        struct Layer {
            PerlinNoise noise;
            double frequency;
            float amplitude;
        };
        const std::vector<Layer>& Layers() const { return layers; }

    private:
        NormalNoise() = default;
        static double Normalization(const NoiseParameters& parameters);
        std::vector<Layer> layers;
    };
}
