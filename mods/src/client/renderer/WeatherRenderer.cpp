#include "client/renderer/WeatherRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "client/renderer/RenderContext.hpp"
#include "client/renderer/block/BiomeTints.hpp"
#include "client/ui/UiAssets.hpp"
#include "world/level/Level.hpp"
#include "world/level/block/BlockPalette.hpp"

namespace perlin {
    namespace {
        int64_t Key(int x, int z) {
            return (static_cast<int64_t>(x) << 32) ^ static_cast<int64_t>(static_cast<uint32_t>(z));
        }

        uint32_t Hash(int x, int z) {
            return static_cast<uint32_t>(x * x * 3121 + x * 45238971) ^
                   static_cast<uint32_t>(z * z * 418711 + z * 13761);
        }

        class JavaRandom {
        public:
            void SetSeed(int64_t seed) {
                state = (static_cast<uint64_t>(seed) ^ 0x5DEECE66Dull) & ((1ull << 48) - 1);
                haveGaussian = false;
            }
            float NextFloat() { return static_cast<float>(Next(24)) / static_cast<float>(1 << 24); }
            double NextDouble() {
                return static_cast<double>((static_cast<int64_t>(Next(26)) << 27) + Next(27)) *
                       (1.0 / static_cast<double>(1ll << 53));
            }
            double NextGaussian() {
                if (haveGaussian) {
                    haveGaussian = false;
                    return nextGaussian;
                }
                double v1, v2, sq;
                do {
                    v1 = 2.0 * NextDouble() - 1.0;
                    v2 = 2.0 * NextDouble() - 1.0;
                    sq = v1 * v1 + v2 * v2;
                } while (sq >= 1.0 || sq == 0.0);
                const double mul = std::sqrt(-2.0 * std::log(sq) / sq);
                nextGaussian = v2 * mul;
                haveGaussian = true;
                return v1 * mul;
            }

        private:
            int32_t Next(int bits) {
                state = (state * 0x5DEECE66Dull + 0xBull) & ((1ull << 48) - 1);
                return static_cast<int32_t>(static_cast<int64_t>(state) >> (48 - bits));
            }
            uint64_t state = 0;
            bool haveGaussian = false;
            double nextGaussian = 0.0;
        };

        uint32_t Pack(float brightness, float alpha) {
            const auto q = [](float v) {
                return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            return (q(alpha) << 24) | (q(brightness) << 16) | (q(brightness) << 8) | q(brightness);
        }
    }

    void WeatherRenderer::Reset() {
        columns.clear();
        blocking.clear();
    }

    bool WeatherRenderer::Blocks(Level&, uint16_t id) {
        if (id == 0)
            return false;
        const auto found = blocking.find(id);
        if (found != blocking.end())
            return found->second;
        const std::string name = BlockPalette::Instance().Name(id);
        // token match: substring "air" would also hit stairs, "rail" would hit grail-like names
        auto hasToken = [&](const char* word) {
            const size_t len = std::strlen(word);
            for (size_t p = name.find(word); p != std::string::npos; p = name.find(word, p + 1)) {
                const bool left = p == 0 || name[p - 1] == ':' || name[p - 1] == '_';
                const bool right = p + len >= name.size() || name[p + len] == '_' || name[p + len] == '.';
                if (left && right)
                    return true;
            }
            return false;
        };
        static const char* const kThrough[] = {
            "air",      "short_grass", "tallgrass",     "tall_grass", "fern",       "flower",
            "poppy",    "dandelion",   "torch",         "sapling",    "vine",       "sugar_cane",
            "wheat",    "rail",        "button",        "lever",      "tripwire",   "web",
            "fire",     "mushroom",    "sweet_berry",   "carpet",     "snow_layer", "sign",
            "painting", "button",      "pressure_plate"};
        bool blocks = true;
        for (const char* word : kThrough)
            if (hasToken(word)) {
                blocks = false;
                break;
            }
        blocking[id] = blocks;
        return blocks;
    }

    void WeatherRenderer::LoadTextures(RenderContext& renderer) {
        texturesTried = true;
        Texture t;
        if (!ui::UiAssets::Instance().Image("textures/environment/weather", t) || t.width < 32 ||
            t.height < 20)
            return;
        constexpr int kTile = 64;
        const auto tile = [&](int firstRow, int rows, int copies) {
            std::vector<uint8_t> px(static_cast<size_t>(kTile) * kTile * 4, 0);
            for (int c = 0; c < copies; c++)
                for (int y = 0; y < rows; y++)
                    std::copy_n(t.pixels.begin() + (static_cast<size_t>(firstRow + y) * t.width) * 4,
                                static_cast<size_t>(t.width) * 4,
                                px.begin() + (static_cast<size_t>((y + c * 40) % kTile) * kTile +
                                              static_cast<size_t>(c) * 32) *
                                                 4);
            return px;
        };
        const std::vector<uint8_t> rainPx = tile(4, 16, 1);
        const std::vector<uint8_t> snowPx = tile(0, 4, 1);
        rainTexture = renderer.UiCreateTexture(kTile, kTile, rainPx.data(), false, true);
        snowTexture = renderer.UiCreateTexture(kTile, kTile, snowPx.data(), false, true);
    }

    void WeatherRenderer::Draw(const In& in) {
        if (!in.enabled || !in.renderer || !in.level || in.rain < 0.01f)
            return;
        if (!texturesTried)
            LoadTextures(*in.renderer);
        if (!rainTexture || !snowTexture)
            return;
        Level& level = *in.level;
        const int radius = std::clamp(in.radius, 3, 15);
        const int cx = static_cast<int>(std::floor(in.camera.x)),
                  cz = static_cast<int>(std::floor(in.camera.z));
        const int camY = static_cast<int>(std::floor(in.camera.y));

        int budget = 48;
        if (columns.size() > static_cast<size_t>((2 * radius + 1) * (2 * radius + 1) * 3)) {
            for (auto it = columns.begin(); it != columns.end();) {
                const int x = static_cast<int>(it->first >> 32),
                          z = static_cast<int>(static_cast<int32_t>(it->first & 0xffffffff));
                if (std::abs(x - cx) > radius + 4 || std::abs(z - cz) > radius + 4)
                    it = columns.erase(it);
                else
                    ++it;
            }
        }
        const auto refresh = [&](int x, int z, Column& c) {
            bool loaded = false;
            level.GetBlock(x, camY, z, loaded);
            if (!loaded) {
                c.kind = 0;
                c.stamp = in.seconds - 0.8;
                return;
            }
            const int highest = std::min(camY + 40, 318);
            const int lowest = camY - 14;
            int top = lowest;
            {
                std::lock_guard lock(level.mutex);
                const auto found = level.chunks.find({x >> 4, z >> 4});
                if (found != level.chunks.end()) {
                    const Chunk& chunk = found->second;
                    for (int y = highest; y >= lowest; y--) {
                        if (Blocks(level, chunk.Get(x & 15, y, z & 15)) ||
                            chunk.GetLiquid(x & 15, y, z & 15) != 0) {
                            top = y + 1;
                            break;
                        }
                    }
                }
            }
            c.top = top;
            c.kind = 1;
            if (in.tints && in.tints->Ready()) {
                float temperature = 0.5f, downfall = 0.5f;
                const std::string name =
                    in.tints->NameForId(level.GetBiome(x, std::clamp(top - 1, -64, 318), z));
                if (in.tints->Climate(name, temperature, downfall)) {
                    if (downfall <= 0.0f)
                        c.kind = 0;
                    else if (temperature < 0.15f)
                        c.kind = 2;
                }
            }
            c.stamp = in.seconds;
        };

        static thread_local std::vector<UiVertex> rainV, snowV;
        static thread_local std::vector<uint32_t> rainI, snowI;
        rainV.clear();
        snowV.clear();
        rainI.clear();
        snowI.clear();
        const double ticks = in.seconds * 20.0;
        const int64_t gameTime = static_cast<int64_t>(std::floor(ticks));
        const float partial = static_cast<float>(ticks - static_cast<double>(gameTime));
        const float radiusSq = static_cast<float>(radius * radius);
        const float brightness = std::clamp(0.3f + 0.7f * in.light, 0.0f, 1.0f);
        JavaRandom random;
        for (int dz = -radius; dz <= radius; dz++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx == 0 && dz == 0)
                    continue;
                const int x = cx + dx, z = cz + dz;
                Column& c = columns[Key(x, z)];
                if (in.seconds - c.stamp > 1.2 && budget > 0) {
                    budget--;
                    refresh(x, z, c);
                }
                if (c.kind == 0)
                    continue;
                const int yTop = std::max(camY + radius, c.top), yBottom = std::max(camY - radius, c.top);
                if (yTop - yBottom == 0)
                    continue;
                random.SetSeed(static_cast<int64_t>(static_cast<int32_t>(Hash(x, z))));
                const float px = static_cast<float>(x + 0.5 - in.camera.x),
                            pz = static_cast<float>(z + 0.5 - in.camera.z);
                const float distSq = px * px + pz * pz;
                const float mixed = std::min(distSq / radiusSq, 1.0f);
                const float base = c.kind == 1 ? 1.0f : 0.8f;
                const float alpha = std::clamp((base + (0.5f - base) * mixed) * in.rain, 0.0f, 1.0f);
                if (alpha < 0.01f)
                    continue;
                const float len = std::sqrt(static_cast<float>(dx * dx + dz * dz));
                const float ax = -static_cast<float>(dz) / len * 0.5f,
                            az = static_cast<float>(dx) / len * 0.5f;
                const float lightBright = c.kind == 2 ? std::min(1.0f, brightness + 0.1f) : brightness;
                const uint32_t color = Pack(lightBright, alpha);
                const float x0 = static_cast<float>(x + 0.5) - ax, x1 = static_cast<float>(x + 0.5) + ax;
                const float z0 = static_cast<float>(z + 0.5) - az, z1 = static_cast<float>(z + 0.5) + az;
                const float y0 = static_cast<float>(yBottom), y1 = static_cast<float>(yTop);
                const float time = static_cast<float>(gameTime & 131071) + partial;
                float uOffset = 0.0f, uSpan = 1.0f, vOffset;
                if (c.kind == 1) {
                    uOffset = static_cast<float>((Hash(x, z) >> 3) & 255u) / 256.0f;
                    const uint32_t mix =
                        static_cast<uint32_t>(x * x * 3121 + x * 45238971 + z * z * 418711 + z * 13761) &
                        255u;
                    const float speed = 3.0f + random.NextFloat();
                    const float raw =
                        -(static_cast<float>(gameTime & 131071) + static_cast<float>(mix) + partial) / 32.0f *
                        speed;
                    vOffset = std::fmod(raw, 32.0f);
                } else {
                    // hash-seeded drift, stable per column instead of per-frame noise
                    const uint32_t h = Hash(x, z);
                    uOffset = static_cast<float>((h >> 5) & 255u) / 256.0f + time * 0.002f;
                    const float drift = static_cast<float>((h & 255u)) / 255.0f * 0.05f;
                    vOffset = -(static_cast<float>(gameTime & 511) + partial) / 512.0f + drift;
                }
                constexpr float kVScale = 4.0f;
                const float vTop = (static_cast<float>(yBottom) * 0.25f + vOffset) * kVScale;
                const float vBottom = (static_cast<float>(yTop) * 0.25f + vOffset) * kVScale;
                const float u0 = uOffset, u1 = uOffset + uSpan;
                std::vector<UiVertex>& vs = c.kind == 1 ? rainV : snowV;
                std::vector<uint32_t>& is = c.kind == 1 ? rainI : snowI;
                const uint32_t first = static_cast<uint32_t>(vs.size());
                vs.push_back({x0, y1, z0, color, u0, vTop});
                vs.push_back({x1, y1, z1, color, u1, vTop});
                vs.push_back({x1, y0, z1, color, u1, vBottom});
                vs.push_back({x0, y0, z0, color, u0, vBottom});
                for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
                    is.push_back(first + k);
            }
        }
        const auto submit = [&](const std::vector<UiVertex>& vs, const std::vector<uint32_t>& is,
                                uint32_t texture) {
            if (vs.empty())
                return;
            const uint32_t mesh = in.renderer->UiCreateFrameMesh(vs.data(), static_cast<uint32_t>(vs.size()),
                                                                 is.data(), static_cast<uint32_t>(is.size()));
            if (!mesh)
                return;
            UiDraw draw;
            draw.material = DrawMaterial::Weather;
            draw.mesh = mesh;
            draw.texture = texture;
            draw.premultiplied = false;
            draw.alphaCutoff = 0.004f;
            draw.blend = 1;
            draw.depth = true;
            draw.depthWrite = false;
            draw.emissive = 2;
            in.renderer->UiSubmit(UiLayer::WorldLate, draw);
        };
        submit(rainV, rainI, rainTexture);
        submit(snowV, snowI, snowTexture);
    }
}
