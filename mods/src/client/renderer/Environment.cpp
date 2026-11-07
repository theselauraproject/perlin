#include "client/renderer/Environment.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>

namespace perlin {
    namespace {
        constexpr float kDegToRad = 0.017453292519943295f;

        struct Keyframe {
            int ticks;
            float v[4];
        };

        struct Bezier {
            float ax, bx, cx, ay, by, cy;
        };

        constexpr Bezier MakeBezier(float x1, float y1, float x2, float y2) {
            return {3.0f * x1 - 3.0f * x2 + 1.0f, -6.0f * x1 + 3.0f * x2, 3.0f * x1,
                    3.0f * y1 - 3.0f * y2 + 1.0f, -6.0f * y1 + 3.0f * y2, 3.0f * y1};
        }

        constexpr Bezier kSunCurve = MakeBezier(0.362f, 0.241f, 0.638f, 0.759f);

        float Cubic(float a, float b, float c, float t) {
            return ((a * t + b) * t + c) * t;
        }

        float Slope(float a, float b, float c, float t) {
            return (3.0f * a * t + 2.0f * b) * t + c;
        }

        float SolveBezier(const Bezier& curve, float x) {
            float t = x;
            for (int i = 0; i < 4; i++) {
                const float error = Cubic(curve.ax, curve.bx, curve.cx, t) - x;
                if (std::fabs(error) < 1.0e-5f)
                    return t;
                const float gradient = Slope(curve.ax, curve.bx, curve.cx, t);
                if (gradient < 1.0e-5f)
                    break;
                t -= std::clamp(error / gradient, -0.25f, 0.25f);
            }
            float t0 = 0.0f;
            float t1 = 1.0f;
            while (t0 < t1) {
                const float error = Cubic(curve.ax, curve.bx, curve.cx, t) - x;
                if (std::fabs(error) < 1.0e-5f)
                    return t;
                if (error < 0.0f)
                    t0 = t;
                else
                    t1 = t;
                t = (t1 + t0) / 2.0f;
            }
            return t;
        }

        float Linear(float x) {
            return x;
        }

        float SunEase(float x) {
            return Cubic(kSunCurve.ay, kSunCurve.by, kSunCurve.cy, SolveBezier(kSunCurve, x));
        }

        const Keyframe kSunAngle[] = {{6000, {360.0f}}, {6000, {0.0f}}};

        const Keyframe kMoonAngle[] = {{6000, {540.0f}}, {6000, {180.0f}}};

        const Keyframe kStarAngle[] = {{6000, {360.0f}}, {6000, {0.0f}}};

        const Keyframe kSkyColor[] = {{133, {1.0f, 1.0f, 1.0f}},
                                      {11867, {1.0f, 1.0f, 1.0f}},
                                      {13670, {0.0f, 0.0f, 0.0f}},
                                      {22330, {0.0f, 0.0f, 0.0f}}};

        const Keyframe kFogColor[] = {
            {133, {1.0f, 1.0f, 1.0f}},
            {11867, {1.0f, 1.0f, 1.0f}},
            {13670, {0.058823529411764705f, 0.058823529411764705f, 0.08627450980392157f}},
            {22330, {0.058823529411764705f, 0.058823529411764705f, 0.08627450980392157f}}};

        const Keyframe kCloudColor[] = {
            {133, {1.0f, 1.0f, 1.0f, 1.0f}},
            {11867, {1.0f, 1.0f, 1.0f, 1.0f}},
            {13670, {0.09803921568627451f, 0.09803921568627451f, 0.14901960784313725f, 1.0f}},
            {22330, {0.09803921568627451f, 0.09803921568627451f, 0.14901960784313725f, 1.0f}}};

        const Keyframe kSkyLightColor[] = {{730, {1.0f, 1.0f, 1.0f}},
                                           {11270, {1.0f, 1.0f, 1.0f}},
                                           {13140, {0.47843137254901963f, 0.47843137254901963f, 1.0f}},
                                           {22860, {0.47843137254901963f, 0.47843137254901963f, 1.0f}}};

        const Keyframe kSkyLightFactor[] = {
            {730, {1.0f}}, {11270, {1.0f}}, {13140, {0.24f}}, {22860, {0.24f}}};

        const Keyframe kStarBrightness[] = {{92, {0.037f}},    {627, {0.0f}},     {11373, {0.0f}},
                                            {11732, {0.016f}}, {11959, {0.044f}}, {12399, {0.143f}},
                                            {12729, {0.258f}}, {13228, {0.5f}},   {22772, {0.5f}},
                                            {23032, {0.364f}}, {23356, {0.225f}}, {23758, {0.101f}}};

        const Keyframe kSunriseColor[] = {
            {71, {0.9372549019607843f, 0.6392156862745098f, 0.2f, 0.37254901960784315f}},
            {310, {0.9607843137254902f, 0.7294117647058823f, 0.2f, 0.1607843137254902f}},
            {565, {0.984313725490196f, 0.8313725490196079f, 0.2f, 0.023529411764705882f}},
            {730, {1.0f, 0.8980392156862745f, 0.2f, 0.0f}},
            {11270, {1.0f, 0.8980392156862745f, 0.2f, 0.0f}},
            {11397, {0.9882352941176471f, 0.8470588235294118f, 0.2f, 0.01568627450980392f}},
            {11522, {0.9764705882352941f, 0.796078431372549f, 0.2f, 0.058823529411764705f}},
            {11690, {0.9607843137254902f, 0.7294117647058823f, 0.2f, 0.1607843137254902f}},
            {11929, {0.9372549019607843f, 0.6392156862745098f, 0.2f, 0.37254901960784315f}},
            {12243, {0.9058823529411765f, 0.5294117647058824f, 0.2f, 0.6941176470588235f}},
            {12358, {0.8941176470588236f, 0.49411764705882355f, 0.2f, 0.8f}},
            {12512, {0.8784313725490196f, 0.4470588235294118f, 0.2f, 0.9137254901960784f}},
            {12613, {0.8666666666666667f, 0.4196078431372549f, 0.2f, 0.9647058823529412f}},
            {12732, {0.8549019607843137f, 0.38823529411764707f, 0.2f, 0.996078431372549f}},
            {12841, {0.8431372549019608f, 0.3607843137254902f, 0.2f, 0.996078431372549f}},
            {13035, {0.8235294117647058f, 0.3176470588235294f, 0.2f, 0.9254901960784314f}},
            {13252, {0.8f, 0.2784313725490196f, 0.2f, 0.7568627450980392f}},
            {13775, {0.7450980392156863f, 0.21568627450980393f, 0.2f, 0.21176470588235294f}},
            {13888, {0.7333333333333333f, 0.20784313725490197f, 0.2f, 0.12156862745098039f}},
            {14039, {0.7176470588235294f, 0.2f, 0.2f, 0.03529411764705882f}},
            {14192, {0.7019607843137254f, 0.2f, 0.2f, 0.0f}},
            {21807, {0.6980392156862745f, 0.2f, 0.2f, 0.0f}},
            {21961, {0.7176470588235294f, 0.2f, 0.2f, 0.03529411764705882f}},
            {22112, {0.7333333333333333f, 0.20784313725490197f, 0.2f, 0.12156862745098039f}},
            {22225, {0.7450980392156863f, 0.21568627450980393f, 0.2f, 0.21176470588235294f}},
            {22748, {0.8f, 0.2784313725490196f, 0.2f, 0.7568627450980392f}},
            {22965, {0.8235294117647058f, 0.3176470588235294f, 0.2f, 0.9254901960784314f}},
            {23159, {0.8431372549019608f, 0.3607843137254902f, 0.2f, 0.996078431372549f}},
            {23272, {0.8549019607843137f, 0.38823529411764707f, 0.2f, 0.996078431372549f}},
            {23488, {0.8784313725490196f, 0.4470588235294118f, 0.2f, 0.9137254901960784f}},
            {23642, {0.8941176470588236f, 0.49411764705882355f, 0.2f, 0.8f}},
            {23757, {0.9058823529411765f, 0.5294117647058824f, 0.2f, 0.6941176470588235f}}};

        struct Segment {
            const Keyframe* from;
            int fromTicks;
            const Keyframe* to;
            int toTicks;
        };

        int FloorMod(int64_t a, int b) {
            const int64_t m = a % b;
            return static_cast<int>(m < 0 ? m + b : m);
        }

        template <size_t N>
        glm::vec4 Sample(const Keyframe (&k)[N], int64_t ticks, int period, float (*ease)(float) = Linear) {
            const int t = FloorMod(ticks, period);
            Segment segments[N + 1];
            size_t count = 0;
            segments[count++] = {&k[N - 1], k[N - 1].ticks - period, &k[0], k[0].ticks};
            for (size_t i = 0; i + 1 < N; i++)
                segments[count++] = {&k[i], k[i].ticks, &k[i + 1], k[i + 1].ticks};
            segments[count++] = {&k[N - 1], k[N - 1].ticks, &k[0], k[0].ticks + period};
            const Segment* segment = &segments[count - 1];
            for (size_t i = 0; i < count; i++)
                if (t < segments[i].toTicks) {
                    segment = &segments[i];
                    break;
                }
            const auto read = [](const Keyframe* key) {
                return glm::vec4(key->v[0], key->v[1], key->v[2], key->v[3]);
            };
            if (t <= segment->fromTicks)
                return read(segment->from);
            if (t >= segment->toTicks)
                return read(segment->to);
            const float alpha = static_cast<float>(t - segment->fromTicks) /
                                static_cast<float>(segment->toTicks - segment->fromTicks);
            return glm::mix(read(segment->from), read(segment->to), ease(alpha));
        }

        glm::vec3 Rgb(int hex) {
            return {static_cast<float>((hex >> 16) & 0xFF) / 255.0f,
                    static_cast<float>((hex >> 8) & 0xFF) / 255.0f, static_cast<float>(hex & 0xFF) / 255.0f};
        }

        struct Base {
            glm::vec3 sky;
            glm::vec3 fog;
            glm::vec4 cloud;
            glm::vec3 ambient;
            glm::vec3 skyLight;
            float skyLightFactor;
            float fogStart;
            float fogEnd;
        };

        Base DimensionBase(Dimension dimension) {
            switch (dimension) {
            case Dimension::Nether:
                // dense tinted haze, biome code overrides when present
                return {glm::vec3(0.0f),
                        Rgb(0x331414),
                        glm::vec4(0.0f),
                        Rgb(0x302821),
                        Rgb(0x7a7aff),
                        0.0f,
                        4.0f,
                        48.0f};
            case Dimension::End:
                return {
                    glm::vec3(0.0f), Rgb(0x181318), glm::vec4(0.0f), Rgb(0x3f473f), Rgb(0xac60cd), 0.0f, 0.0f,
                    1024.0f};
            default:
                return {Rgb(0x78a7ff), Rgb(0xc0d8ff),   glm::vec4(1.0f, 1.0f, 1.0f, 0.8f),
                        Rgb(0x0a0a0a), glm::vec3(1.0f), 1.0f,
                        0.0f,          1024.0f};
            }
        }

        float Grey(const glm::vec3& c) {
            return c.x * 0.3f + c.y * 0.59f + c.z * 0.11f;
        }

        glm::vec3 BlendToGray(const glm::vec3& c, float brightness, float factor) {
            return glm::mix(c, glm::vec3(Grey(c) * brightness), factor);
        }

        glm::vec4 BlendToGray(const glm::vec4& c, float brightness, float factor) {
            return glm::vec4(BlendToGray(glm::vec3(c), brightness, factor), c.w);
        }

        Environment Evaluate(const EnvironmentInput& in, int64_t ticks) {
            Environment env;
            const Base base = DimensionBase(in.dimension);
            env.skyColor = in.biomeSkyColor.value_or(base.sky);
            env.fogColor = in.biomeFogColor.value_or(base.fog);
            env.cloudColor = base.cloud;
            env.ambientColor = base.ambient;
            env.skyLightColor = base.skyLight;
            env.skyLightFactor = base.skyLightFactor;
            env.fogStart = base.fogStart;
            env.fogEnd = base.fogEnd;
            env.blockLightTint = Rgb(0xffd88c);
            env.nightVisionColor = Rgb(0x999999);

            if (in.dimension == Dimension::Overworld) {
                constexpr int period = kTicksPerDay;
                env.sunAngle = Sample(kSunAngle, ticks, period, SunEase).x * kDegToRad;
                env.moonAngle = Sample(kMoonAngle, ticks, period, SunEase).x * kDegToRad;
                env.starAngle = Sample(kStarAngle, ticks, period, SunEase).x * kDegToRad;
                env.skyColor *= glm::vec3(Sample(kSkyColor, ticks, period));
                env.fogColor *= glm::vec3(Sample(kFogColor, ticks, period));
                env.cloudColor *= Sample(kCloudColor, ticks, period);
                env.skyLightColor *= glm::vec3(Sample(kSkyLightColor, ticks, period));
                env.skyLightFactor *= Sample(kSkyLightFactor, ticks, period).x;
                env.starBrightness = std::max(0.0f, Sample(kStarBrightness, ticks, period).x);
                env.sunriseColor = Sample(kSunriseColor, ticks, period);
                env.moonPhase = FloorMod(ticks, 8 * period) / period;
            }

            const float thunder = in.thunder;
            const float rain = in.rain - thunder;
            const glm::vec3 nightLight = Rgb(0x7a7aff);
            if (rain > 0.0f) {
                env.skyColor = glm::mix(env.skyColor, BlendToGray(env.skyColor, 0.6f, 0.75f), rain);
                env.fogColor = glm::mix(env.fogColor, env.fogColor * glm::vec3(0.5f, 0.5f, 0.6f), rain);
                env.cloudColor = glm::mix(env.cloudColor, BlendToGray(env.cloudColor, 0.24f, 0.5f), rain);
                env.skyLightColor =
                    glm::mix(env.skyLightColor, glm::mix(env.skyLightColor, nightLight, 0.3125f), rain);
                env.skyLightFactor =
                    glm::mix(env.skyLightFactor, glm::mix(env.skyLightFactor, 0.24f, 0.3125f), rain);
                env.starBrightness = glm::mix(env.starBrightness, 0.0f, rain);
                env.sunriseColor =
                    glm::mix(env.sunriseColor, env.sunriseColor * glm::vec4(0.5f, 0.5f, 0.6f, 1.0f), rain);
            }
            if (thunder > 0.0f) {
                env.skyColor = glm::mix(env.skyColor, BlendToGray(env.skyColor, 0.24f, 0.94f), thunder);
                env.fogColor = glm::mix(env.fogColor, env.fogColor * glm::vec3(0.25f, 0.25f, 0.3f), thunder);
                env.cloudColor =
                    glm::mix(env.cloudColor, BlendToGray(env.cloudColor, 0.095f, 0.94f), thunder);
                env.skyLightColor = glm::mix(env.skyLightColor,
                                             glm::mix(env.skyLightColor, nightLight, 0.52734375f), thunder);
                env.skyLightFactor =
                    glm::mix(env.skyLightFactor, glm::mix(env.skyLightFactor, 0.24f, 0.52734375f), thunder);
                env.starBrightness = glm::mix(env.starBrightness, 0.0f, thunder);
                env.sunriseColor = glm::mix(env.sunriseColor,
                                            env.sunriseColor * glm::vec4(0.25f, 0.25f, 0.3f, 1.0f), thunder);
            }
            return env;
        }

        float LerpDegrees(float alpha, float from, float to) {
            float delta = std::fmod(to * 57.29577951308232f - from * 57.29577951308232f, 360.0f);
            if (delta >= 180.0f)
                delta -= 360.0f;
            if (delta < -180.0f)
                delta += 360.0f;
            if (std::fabs(delta) >= 90.0f)
                return to;
            return from + alpha * delta * kDegToRad;
        }
    }

    float EaseSunCurve(float x) {
        return SunEase(x);
    }

    Environment SampleEnvironment(const EnvironmentInput& input) {
        const double floored = std::floor(input.ticks);
        const float partial = static_cast<float>(input.ticks - floored);
        const int64_t tick = static_cast<int64_t>(floored);
        const Environment last = Evaluate(input, tick - 1);
        Environment now = Evaluate(input, tick);

        const auto mix = [partial](auto a, auto b) { return glm::mix(a, b, partial); };
        Environment out = now;
        out.sunAngle = LerpDegrees(partial, last.sunAngle, now.sunAngle);
        out.moonAngle = LerpDegrees(partial, last.moonAngle, now.moonAngle);
        out.starAngle = LerpDegrees(partial, last.starAngle, now.starAngle);
        out.starBrightness = mix(last.starBrightness, now.starBrightness);
        out.skyColor = mix(last.skyColor, now.skyColor);
        out.fogColor = mix(last.fogColor, now.fogColor);
        out.cloudColor = mix(last.cloudColor, now.cloudColor);
        out.sunriseColor = mix(last.sunriseColor, now.sunriseColor);
        out.skyLightColor = mix(last.skyLightColor, now.skyLightColor);
        out.skyLightFactor = mix(last.skyLightFactor, now.skyLightFactor);
        return out;
    }
}
