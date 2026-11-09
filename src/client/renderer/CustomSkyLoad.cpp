#include "client/renderer/CustomSky.hpp"

#include "client/resources/TexturePack.hpp"
#include "util/Log.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>

namespace perlin::sky {
    namespace {
        // 3x2 sheets past this side are box-filtered down: a 9600px sheet is ~245 MB on the gpu and over the
        // texture limit of some drivers
        constexpr int kMaxSkySide = 5120;

        void Shrink(Texture& t, const std::string& name) {
            int factor = 1;
            while (std::max(t.width, t.height) / factor > kMaxSkySide)
                factor++;
            // prefer a factor that keeps the 3x2 cells whole: a cell edge mid-texel bleeds with point sampling
            for (int f = factor; f < factor * 2; f++)
                if ((t.width / 3) % f == 0 && (t.height / 2) % f == 0) {
                    factor = f;
                    break;
                }
            if (factor == 1 || t.pixels.size() < static_cast<size_t>(t.width) * t.height * 4)
                return;
            const int w = t.width / factor, h = t.height / factor;
            std::vector<unsigned char> out(static_cast<size_t>(w) * h * 4);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    uint32_t sum[4] = {};
                    for (int dy = 0; dy < factor; dy++) {
                        const unsigned char* row =
                            t.pixels.data() + (static_cast<size_t>(y * factor + dy) * t.width + x * factor) * 4;
                        for (int dx = 0; dx < factor * 4; dx++)
                            sum[dx & 3] += row[dx];
                    }
                    const uint32_t n = static_cast<uint32_t>(factor * factor);
                    for (int c = 0; c < 4; c++)
                        out[(static_cast<size_t>(y) * w + x) * 4 + c] = static_cast<unsigned char>(sum[c] / n);
                }
            Log("sky", "{}: {}x{} scaled to {}x{}", name, t.width, t.height, w, h);
            t.width = w;
            t.height = h;
            t.pixels = std::move(out);
        }
    }

    Set Load(const std::vector<std::string>& serverPackDirs) {
        Set set;
        TexturePack& pack = TexturePack::Instance();
        const auto addTexture = [&](Texture&& t) {
            set.textures.push_back(std::move(t));
            return static_cast<int>(set.textures.size()) - 1;
        };
        const auto loadCubemap = [&](const std::function<bool(int, Texture&)>& face) {
            std::vector<Texture> faces(6);
            for (int i = 0; i < 6; i++)
                if (!face(i, faces[i]) || faces[i].width <= 0 || faces[i].height <= 0)
                    return false;
            Layer l;
            l.cubemap = true;
            l.rotate = false;
            l.blend = Blend::Alpha;
            l.texture = static_cast<int>(set.textures.size());
            for (Texture& f : faces)
                addTexture(std::move(f));
            set.layers.push_back(std::move(l));
            return true;
        };
        const auto cubemapPath = [](int i) {
            return "textures/environment/overworld_cubemap/cubemap_" + std::to_string(i) + ".png";
        };
        for (size_t layer = 0; layer < pack.LayerCount() && set.Empty(); layer++) {
            if (pack.LayerJava(layer)) {
                // world0 is the overworld, world1 the End; optifine/ first, then the MCPatcher location
                for (const int world : {0, 1}) {
                    std::vector<Layer>& out = world == 0 ? set.layers : set.endLayers;
                    for (const char* root : {"optifine/sky/world", "mcpatcher/sky/world"}) {
                        if (!out.empty())
                            break;
                        const std::string dir = root + std::to_string(world);
                        // CustomSky.readSkyLayers: gaps allowed, the list ends after more than 10 missing numbers
                        int missing = 0;
                        for (int i = 0; i < 1000; i++) {
                            std::string text;
                            if (!pack.LayerReadText(layer, dir + "/sky" + std::to_string(i) + ".properties", text)) {
                                if (++missing > 10)
                                    break;
                                continue;
                            }
                            Layer l;
                            std::string error;
                            if (!ParseLayer(text, dir, i, l, error)) {
                                Log("sky", "{}/sky{}.properties: {}", dir, i, error);
                                continue;
                            }
                            Texture t;
                            if (!pack.LayerLoad(layer, l.source, t) || t.width <= 0) {
                                Log("sky", "texture not found: {}", l.source);
                                continue;
                            }
                            Shrink(t, l.source);
                            l.texture = addTexture(std::move(t));
                            out.push_back(std::move(l));
                        }
                        // MCPatcher's loadCelestialObject: a properties file replaces the vanilla texture and its
                        // blend; the source defaults to the png of the same name beside it
                        if (world != 0)
                            continue;
                        for (const auto& [name, target] : {std::pair{"sun", &set.sun}, std::pair{"moon_phases", &set.moon}}) {
                            std::string text;
                            if (target->texture >= 0 ||
                                !pack.LayerReadText(layer, dir + "/" + name + ".properties", text))
                                continue;
                            Layer l;
                            std::string error;
                            const std::string withSource =
                                text.find("source") == std::string::npos ? "source=./" + std::string(name) + ".png\n" + text
                                                                          : text;
                            if (!ParseLayer(withSource, dir, 0, l, error)) {
                                Log("sky", "{}/{}.properties: {}", dir, name, error);
                                continue;
                            }
                            Texture t;
                            if (!pack.LayerLoad(layer, l.source, t) || t.width <= 0) {
                                Log("sky", "texture not found: {}", l.source);
                                continue;
                            }
                            target->texture = addTexture(std::move(t));
                            target->blend = l.blend;
                        }
                    }
                }
            } else {
                loadCubemap([&](int i, Texture& out) { return pack.LayerLoad(layer, cubemapPath(i), out); });
            }
            if (!set.Empty())
                set.origin = pack.Description();
        }
        for (const std::string& dir : serverPackDirs) {
            if (!set.Empty())
                break;
            if (loadCubemap([&](int i, Texture& out) { return LoadTexture(dir + "/" + cubemapPath(i), out); }))
                set.origin = dir;
        }
        if (!set.Empty())
            Log("sky", "custom sky from {}: {} overworld and {} End layer(s){}{}", set.origin, set.layers.size(),
                set.endLayers.size(), set.sun.texture >= 0 ? ", sun" : "", set.moon.texture >= 0 ? ", moon" : "");
        return set;
    }

}
