#include "client/renderer/CustomSky.hpp"

#include "client/resources/TexturePack.hpp"
#include "util/Log.hpp"

#include <functional>
#include <utility>

namespace perlin::sky {
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
                        // sky0 is optional; the list ends at the first missing file after it
                        for (int i = 0; i < 1000; i++) {
                            std::string text;
                            if (!pack.LayerReadText(layer, dir + "/sky" + std::to_string(i) + ".properties", text)) {
                                if (i == 0)
                                    continue;
                                break;
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
