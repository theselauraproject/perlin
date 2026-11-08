#include "world/vanilla/Noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace perlin::server::vanilla {
    namespace {

        struct Md5 {
            uint32_t a = 0x67452301, b = 0xefcdab89, c = 0x98badcfe, d = 0x10325476;
            void Block(const uint8_t* p) {
                static const uint32_t k[64] = {
                    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613,
                    0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193,
                    0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d,
                    0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
                    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122,
                    0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
                    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244,
                    0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
                    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb,
                    0xeb86d391};
                static const int s[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                          5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                          4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                          6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
                uint32_t m[16];
                for (int i = 0; i < 16; i++)
                    m[i] = static_cast<uint32_t>(p[i * 4]) | static_cast<uint32_t>(p[i * 4 + 1]) << 8 |
                           static_cast<uint32_t>(p[i * 4 + 2]) << 16 |
                           static_cast<uint32_t>(p[i * 4 + 3]) << 24;
                uint32_t A = a, B = b, C = c, D = d;
                for (int i = 0; i < 64; i++) {
                    uint32_t f;
                    int g;
                    if (i < 16) {
                        f = (B & C) | (~B & D);
                        g = i;
                    } else if (i < 32) {
                        f = (D & B) | (~D & C);
                        g = (5 * i + 1) & 15;
                    } else if (i < 48) {
                        f = B ^ C ^ D;
                        g = (3 * i + 5) & 15;
                    } else {
                        f = C ^ (B | ~D);
                        g = (7 * i) & 15;
                    }
                    f = f + A + k[i] + m[g];
                    A = D;
                    D = C;
                    C = B;
                    B = B + std::rotl(f, s[i]);
                }
                a += A;
                b += B;
                c += C;
                d += D;
            }
        };

        constexpr double kHalfRoundOff = 16777216.0;
        constexpr double kRoundOff = 33554432.0;

        double Wrap(double x) {
            if (x >= -kHalfRoundOff && x < kHalfRoundOff)
                return x;
            return x - std::floor(x / kRoundOff + 0.5) * kRoundOff;
        }

        int JavaFloor(double d) {
            const double f = std::floor(d);
            if (f != f)
                return 0;
            if (f >= 2147483647.0)
                return 2147483647;
            if (f <= -2147483648.0)
                return static_cast<int>(-2147483647 - 1);
            return static_cast<int>(f);
        }

        float Lerp(float t, float a, float b) {
            return a + t * (b - a);
        }
        float Lerp2(float d1, float d2, float v00, float v10, float v01, float v11) {
            return Lerp(d2, Lerp(d1, v00, v10), Lerp(d1, v01, v11));
        }
        float Smoothstep(float f) {
            return f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
        }

        struct Grad {
            int x, y, z;
        };
        constexpr Grad kGradients[16] = {
            {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
            {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1}};

        float GradDot(int hash, float x, float y, float z) {
            const Grad& g = kGradients[hash & 15];
            return static_cast<float>(g.x) * x + static_cast<float>(g.y) * y + static_cast<float>(g.z) * z;
        }
    }

    Seed128 SeedFromHashOf(std::string_view text) {

        std::string msg(text);
        const uint64_t bits = static_cast<uint64_t>(msg.size()) * 8;
        msg.push_back(static_cast<char>(0x80));
        while (msg.size() % 64 != 56)
            msg.push_back(0);
        for (int i = 0; i < 8; i++)
            msg.push_back(static_cast<char>((bits >> (8 * i)) & 0xFF));
        Md5 md5;
        for (size_t i = 0; i < msg.size(); i += 64)
            md5.Block(reinterpret_cast<const uint8_t*>(msg.data()) + i);
        uint8_t digest[16];
        const uint32_t words[4] = {md5.a, md5.b, md5.c, md5.d};
        for (int w = 0; w < 4; w++)
            for (int i = 0; i < 4; i++)
                digest[w * 4 + i] = static_cast<uint8_t>((words[w] >> (8 * i)) & 0xFF);
        Seed128 out;
        for (int i = 0; i < 8; i++) {
            out.lo = (out.lo << 8) | digest[i];
            out.hi = (out.hi << 8) | digest[8 + i];
        }
        return out;
    }

    PerlinNoise::PerlinNoise(WorldRandom& random) {
        offsetX = random.NextDouble() * 256.0;
        offsetY = random.NextDouble() * 256.0;
        offsetZ = random.NextDouble() * 256.0;
        for (int i = 0; i < 256; i++)
            perms[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
        for (int i = 0; i < 256; i++) {
            const int j = random.NextInt(256 - i);
            std::swap(perms[static_cast<size_t>(i)], perms[static_cast<size_t>(i + j)]);
        }
    }

    float PerlinNoise::Get(double x, double y, double z) const {
        const double px = Wrap(x) + offsetX, py = Wrap(y) + offsetY, pz = Wrap(z) + offsetZ;
        const int ix = JavaFloor(px), iy = JavaFloor(py), iz = JavaFloor(pz);
        const float fx = static_cast<float>(px - static_cast<double>(ix));
        const float fy = static_cast<float>(py - static_cast<double>(iy));
        const float fz = static_cast<float>(pz - static_cast<double>(iz));
        return SampleAndLerp(ix, iy, iz, fx, fy, fz, fy);
    }

    float PerlinNoise::SampleAndLerp(int ix, int iy, int iz, float fx, float fy, float fz,
                                     float smoothY) const {
        const int p0 = Permute(ix), p1 = Permute(ix + 1);
        const int i10 = Permute(p0 + iy), i11 = Permute(p0 + iy + 1);
        const int i12 = Permute(p1 + iy), i13 = Permute(p1 + iy + 1);
        const float g000 = GradDot(Permute(i10 + iz), fx, fy, fz);
        const float g100 = GradDot(Permute(i12 + iz), fx - 1.0f, fy, fz);
        const float g010 = GradDot(Permute(i11 + iz), fx, fy - 1.0f, fz);
        const float g110 = GradDot(Permute(i13 + iz), fx - 1.0f, fy - 1.0f, fz);
        const float g001 = GradDot(Permute(i10 + iz + 1), fx, fy, fz - 1.0f);
        const float g101 = GradDot(Permute(i12 + iz + 1), fx - 1.0f, fy, fz - 1.0f);
        const float g011 = GradDot(Permute(i11 + iz + 1), fx, fy - 1.0f, fz - 1.0f);
        const float g111 = GradDot(Permute(i13 + iz + 1), fx - 1.0f, fy - 1.0f, fz - 1.0f);
        const float sx = Smoothstep(fx), sy = Smoothstep(smoothY), sz = Smoothstep(fz);

        return Lerp(sz, Lerp2(sx, sy, g000, g100, g010, g110), Lerp2(sx, sy, g001, g101, g011, g111));
    }

    float SmearedPerlinNoise::Get(double x, double y, double z) const {
        const double px = Wrap(x) + OffsetX(), py = Wrap(y) + OffsetY(), pz = Wrap(z) + OffsetZ();
        const int ix = JavaFloor(px), iy = JavaFloor(py), iz = JavaFloor(pz);
        const float fx = static_cast<float>(px - static_cast<double>(ix));
        const double fyd = py - static_cast<double>(iy);
        const float fz = static_cast<float>(pz - static_cast<double>(iz));

        const double d = (y >= 0.0 && y < fyd) ? y : fyd;
        const double fudge =
            static_cast<double>(JavaFloor(d / fudgeYScale + 1.0000000116860974E-7)) * fudgeYScale;
        return SampleAndLerp(ix, iy, iz, fx, static_cast<float>(fyd - fudge), fz, static_cast<float>(fyd));
    }

    std::vector<BlendedNoise::Layer> BlendedNoise::Fbm(WorldRandom& random, int firstOctave, double smearScale,
                                                       double amplitude) {
        const int count = -firstOctave + 1;
        double frequency = 1.0;
        amplitude = amplitude / (std::pow(2.0, static_cast<double>(count)) - 1.0);
        std::vector<Layer> out;
        for (int i = count - 1; i >= 0; i--) {
            out.push_back({SmearedPerlinNoise(random, smearScale * frequency), frequency,
                           static_cast<float>(amplitude)});
            frequency /= 2.0;
            amplitude *= 2.0;
        }
        return out;
    }

    float BlendedNoise::Sum(const std::vector<Layer>& stack, double x, double y, double z) {
        float sum = 0.0f;
        for (const Layer& l : stack)
            sum += l.amplitude * l.noise.Get(x * l.frequency, y * l.frequency, z * l.frequency);
        return sum;
    }

    BlendedNoise::BlendedNoise(double xzScale, double yScale, double xzFactorIn, double yFactorIn,
                               double smear, WorldRandom random)
        : xzMultiplier(684.412 * xzScale), yMultiplier(684.412 * yScale), xzFactor(xzFactorIn),
          yFactor(yFactorIn) {
        const double d2 = yMultiplier * smear;
        const double d4 = d2 / yFactor;
        minLimit = Fbm(random, -15, d2, 0.9999847412109375);
        maxLimit = Fbm(random, -15, d2, 0.9999847412109375);
        main = Fbm(random, -7, d4, 12.75);
    }

    float BlendedNoise::Get(int x, int y, int z) const {
        const double dx = static_cast<double>(x), dy = static_cast<double>(y), dz = static_cast<double>(z);
        float alpha = Sum(main, dx * (xzMultiplier / xzFactor), dy * (yMultiplier / yFactor),
                          dz * (xzMultiplier / xzFactor)) +
                      0.5f;
        alpha = alpha < 0.0f ? 0.0f : std::min(alpha, 1.0f);
        if (alpha == 0.0f)
            return Sum(minLimit, dx * xzMultiplier, dy * yMultiplier, dz * xzMultiplier);
        if (alpha == 1.0f)
            return Sum(maxLimit, dx * xzMultiplier, dy * yMultiplier, dz * xzMultiplier);
        const float a = Sum(minLimit, dx * xzMultiplier, dy * yMultiplier, dz * xzMultiplier);
        const float b = Sum(maxLimit, dx * xzMultiplier, dy * yMultiplier, dz * xzMultiplier);
        return a + alpha * (b - a);
    }

    SimplexNoise::SimplexNoise(WorldRandom& random, bool discardOffset) {
        const double scale = discardOffset ? 0.0 : 256.0;
        offsetX = random.NextDouble() * scale;
        offsetY = random.NextDouble() * scale;
        offsetZ = random.NextDouble() * scale;
        for (int i = 0; i < 256; i++)
            perms[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
        for (int i = 0; i < 256; i++) {
            const int j = random.NextInt(256 - i);
            std::swap(perms[static_cast<size_t>(i)], perms[static_cast<size_t>(i + j)]);
        }
    }

    float SimplexNoise::Get(double inX, double inY) const {
        static const double kF2 = 0.5 * (std::sqrt(3.0) - 1.0);
        static const double kG2 = (3.0 - std::sqrt(3.0)) / 6.0;
        static const int kGrad[16][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}, {1, 0}, {-1, 0}, {1, 0}, {-1, 0},
                                         {0, 1}, {0, -1}, {0, 1}, {0, -1}, {1, 1}, {0, -1}, {-1, 1}, {0, -1}};
        const double xin = inX + offsetX, yin = inY + offsetY;
        const double s = (xin + yin) * kF2;
        const int i = JavaFloor(xin + s);
        const int j = JavaFloor(yin + s);
        const double t = static_cast<double>(i + j) * kG2;
        const double x0 = xin - (static_cast<double>(i) - t);
        const double y0 = yin - (static_cast<double>(j) - t);
        const int i1 = x0 > y0 ? 1 : 0, j1 = x0 > y0 ? 0 : 1;
        const double x1 = x0 - i1 + kG2, y1 = y0 - j1 + kG2;
        const double x2 = x0 - 1.0 + 2.0 * kG2, y2 = y0 - 1.0 + 2.0 * kG2;
        const int ii = i & 255, jj = j & 255;
        const int g0 = Permute(ii + Permute(jj)) % 12;
        const int g1 = Permute(ii + i1 + Permute(jj + j1)) % 12;
        const int g2 = Permute(ii + 1 + Permute(jj + 1)) % 12;
        const auto corner = [&](int g, double x, double y) {
            double c = 0.5 - x * x - y * y;
            if (c < 0.0)
                return 0.0;
            c *= c;
            return c * c * (static_cast<double>(kGrad[g][0]) * x + static_cast<double>(kGrad[g][1]) * y);
        };
        return static_cast<float>(70.0 * (corner(g0, x0, y0) + corner(g1, x1, y1) + corner(g2, x2, y2)));
    }

    double NormalNoise::Normalization(const NoiseParameters& p) {
        double amplitude = p.baseAmplitude;
        if (p.normalize)
            amplitude = amplitude * std::pow(0.5, static_cast<double>(-(p.octaveCount - 1))) /
                        (std::pow(0.5, static_cast<double>(-p.octaveCount)) - 1.0);
        double sum = 0, deviation = 0;
        for (int i = 0; i < p.octaveCount; i++) {
            const double modifier =
                p.amplitudeModifiers.empty() ? 1.0 : p.amplitudeModifiers[static_cast<size_t>(i)];
            if (modifier != 0.0) {
                const double a = std::abs(amplitude * modifier);
                sum += a;
                const double d = 0.2702247831245211 * a;
                deviation += d * d;
            }
            amplitude *= 0.5;
        }
        deviation = std::sqrt(deviation);
        return deviation == 0.0 ? 0.0 : (sum * 0.3333333333333333) / (deviation * std::sqrt(2.0));
    }

    NormalNoise NormalNoise::LegacyNetherBiome(const NoiseParameters& p, WorldRandom random) {
        std::vector<double> amplitudes = p.amplitudeModifiers;
        if (amplitudes.empty())
            amplitudes.assign(static_cast<size_t>(p.octaveCount), 1.0);
        const float valueFactor = static_cast<float>(Normalization(p) * p.baseAmplitude);
        NormalNoise out;
        const auto stack = [&](double frequencyScale) {
            const int octaves = static_cast<int>(amplitudes.size());
            const int zero = -p.baseOctave;
            std::vector<std::unique_ptr<PerlinNoise>> levels(static_cast<size_t>(octaves));
            auto zeroOctave = std::make_unique<PerlinNoise>(random);
            if (zero >= 0 && zero < octaves && amplitudes[static_cast<size_t>(zero)] != 0.0)
                levels[static_cast<size_t>(zero)] = std::move(zeroOctave);
            for (int i = zero - 1; i >= 0; i--) {
                if (i < octaves && amplitudes[static_cast<size_t>(i)] != 0.0)
                    levels[static_cast<size_t>(i)] = std::make_unique<PerlinNoise>(random);
                else
                    random.Consume(262);
            }
            double factor = std::pow(2.0, static_cast<double>(-zero));
            double vf = std::pow(2.0, static_cast<double>(octaves - 1)) / (std::pow(2.0, static_cast<double>(octaves)) - 1.0);
            for (int i = 0; i < octaves; i++) {
                if (levels[static_cast<size_t>(i)])
                    out.layers.push_back({*levels[static_cast<size_t>(i)], factor * frequencyScale,
                                          static_cast<float>(vf * amplitudes[static_cast<size_t>(i)]) * valueFactor});
                factor *= 2.0;
                vf /= 2.0;
            }
        };
        stack(1.0);
        stack(1.0181268882175227);
        return out;
    }

    NormalNoise::NormalNoise(const NoiseParameters& p, WorldRandom random) {
        struct Octave {
            int index;
            double frequency, amplitude;
        };
        std::vector<Octave> octaves;
        double frequency = std::pow(2.0, static_cast<double>(p.baseOctave));
        double amplitude = p.baseAmplitude;
        if (p.normalize)
            amplitude = amplitude * std::pow(0.5, static_cast<double>(-(p.octaveCount - 1))) /
                        (std::pow(0.5, static_cast<double>(-p.octaveCount)) - 1.0);
        for (int i = 0; i < p.octaveCount; i++) {
            const double modifier =
                p.amplitudeModifiers.empty() ? 1.0 : p.amplitudeModifiers[static_cast<size_t>(i)];
            if (modifier != 0.0)
                octaves.push_back({p.baseOctave + i, frequency, amplitude * modifier});
            frequency *= 2.0;
            amplitude *= 0.5;
        }
        double sum = 0, deviation = 0;
        for (const Octave& o : octaves) {
            sum += std::abs(o.amplitude);
            const double d = 0.2702247831245211 * std::abs(o.amplitude);
            deviation += d * d;
        }
        deviation = std::sqrt(deviation);
        const double normalization =
            deviation == 0.0 ? 0.0 : (sum * 0.3333333333333333) / (deviation * std::sqrt(2.0));

        const WorldRandom::Factory a = random.ForkPositional();
        const WorldRandom::Factory b = random.ForkPositional();
        for (const Octave& o : octaves) {
            const std::string seed = "octave_" + std::to_string(o.index);
            WorldRandom ra = a.FromHashOf(seed);
            WorldRandom rb = b.FromHashOf(seed);
            const float amp = static_cast<float>(normalization * o.amplitude);
            layers.push_back({PerlinNoise(ra), o.frequency, amp});
            layers.push_back({PerlinNoise(rb), o.frequency * 1.0181268882175227, amp});
        }
    }

    float NormalNoise::Get(double x, double y, double z) const {
        float sum = 0.0f;
        for (const Layer& l : layers)
            sum += l.amplitude * l.noise.Get(x * l.frequency, y * l.frequency, z * l.frequency);
        return sum;
    }
}
