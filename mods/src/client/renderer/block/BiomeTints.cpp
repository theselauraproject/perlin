#include "client/renderer/block/BiomeTints.hpp"
#include "client/resources/TextureLoader.hpp"
#include "client/resources/TexturePack.hpp"
#include "util/Json.hpp"
#include "util/FileIo.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <filesystem>
#include "util/Log.hpp"

namespace perlin {
    namespace {

        bool ParseHex(const std::string& hex, std::array<float, 3>& out) {
            if (hex.size() != 7 || hex[0] != '#')
                return false;
            unsigned r = 0, g = 0, b = 0;
            if (sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) != 3)
                return false;
            out = {(float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f};
            return true;
        }

        bool ResolveTint(json::View value,
                         const std::unordered_map<std::string, std::array<float, 3>>& mapAvg,
                         std::array<float, 3>& out) {
            if (value.isString())
                return ParseHex(value.str(), out);
            const json::View colorMap = value["color_map"];
            if (!colorMap.isString())
                return false;
            const std::string mapName = colorMap.str();
            if (mapName == "grass" || mapName == "foliage" || mapName == "dry_foliage")
                return false;
            const auto it = mapAvg.find(colorMap.str());
            if (it == mapAvg.end())
                return false;
            out = it->second;
            return true;
        }

    }

    bool BiomeTints::EnsureLoaded(const std::string& vanillaRpRoot) {
        if (ready.load(std::memory_order_acquire))
            return true;
        attempts++;
        mapAvg.clear();
        mapRGB.clear();
        rules.clear();
        climate.clear();
        idToName.clear();
        nameToId.clear();
        packWater.clear();
        packDefaultWater.reset();

        static const char* kMaps[] = {"grass",     "foliage",     "dry_foliage",   "birch",
                                      "evergreen", "swamp_grass", "swamp_foliage", "mangrove_swamp_foliage"};
        bool anyMap = false;
        for (const char* m : kMaps) {
            std::string full = vanillaRpRoot + "/textures/colormap/" + m + ".png";
            Texture tex;
            bool fromPack = false;
            if (TexturePack::Instance().Active())
                for (const char* ext : {".png", ".tga"})
                    if (TexturePack::Instance().Load(std::string("textures/colormap/") + m + ext, tex) &&
                        tex.width > 0 && tex.height > 0) {
                        fromPack = true;
                        break;
                    }
            if (!fromPack && (!LoadTexture(full, tex) || tex.width <= 0 || tex.height <= 0)) {
                std::string tga = vanillaRpRoot + "/textures/colormap/" + m + ".tga";
                if (!LoadTexture(tga, tex) || tex.width <= 0 || tex.height <= 0)
                    continue;
            }
            double sr = 0, sg = 0, sb = 0;
            int n = 0;
            for (size_t i = 0; i < tex.pixels.size(); i += 4) {
                int r = tex.pixels[i], g = tex.pixels[i + 1], b = tex.pixels[i + 2];
                if (r > 240 && g > 240 && b > 240)
                    continue;
                sr += r;
                sg += g;
                sb += b;
                n++;
            }
            if (n > 0) {
                mapAvg[m] = {(float)(sr / n / 255.0), (float)(sg / n / 255.0), (float)(sb / n / 255.0)};
                anyMap = true;
            }
            bool wantFull = !strcmp(m, "grass") || !strcmp(m, "foliage") || !strcmp(m, "dry_foliage");
            if (wantFull) {
                std::vector<uint8_t> rgb((size_t)256 * 256 * 3, 255);
                for (int yy = 0; yy < 256; yy++)
                    for (int xx = 0; xx < 256; xx++) {
                        int sx = xx * tex.width / 256, sy = yy * tex.height / 256;
                        const uint8_t* s = tex.pixels.data() + ((size_t)sy * tex.width + sx) * 4;
                        uint8_t* d = rgb.data() + ((size_t)yy * 256 + xx) * 3;
                        d[0] = s[0];
                        d[1] = s[1];
                        d[2] = s[2];
                    }
                mapRGB[m] = std::move(rgb);
            }
        }
        if (!anyMap)
            return false;

        {
            std::string defs =
                file::Read(vanillaRpRoot + "/../metadata/vanilladata_modules/mojang-biomes.json");
            if (const auto document = json::Document::Parse(defs)) {
                for (const json::View e : document->root()["data_items"].items()) {
                    if (!e["id"].isNumber() || !e["name"].isString())
                        continue;
                    const int id = e["id"].integer();
                    const std::string name = e["name"].str();
                    idToName[id] = name;
                    nameToId[name] = id;
                }
            }
        }

        {
            std::error_code dirEc;
            for (const auto& dirEntry :
                 std::filesystem::directory_iterator(vanillaRpRoot + "/../behavior_pack/biomes", dirEc)) {
                if (!dirEntry.path().filename().string().ends_with(".biome.json"))
                    continue;
                {
                    std::string full = dirEntry.path().string();
                    std::string txt = file::Read(full);
                    if (txt.empty())
                        continue;
                    const auto document = json::Document::Parse(txt, true);
                    if (!document)
                        continue;
                    const json::View biome = document->root()["minecraft:biome"];
                    const json::View ident = biome["description"]["identifier"];
                    const json::View climateNode = biome["components"]["minecraft:climate"];
                    const json::View temperature = climateNode["temperature"];
                    const json::View downfall = climateNode["downfall"];
                    if (!ident.isString() || !temperature.isNumber() || !downfall.isNumber())
                        continue;
                    climate[ident.str()] = {temperature.real(), downfall.real()};
                }
            }
        }
        const auto applyClientBiome = [&](const std::string& txt, bool packLayer) {
            if (txt.empty())
                return;
            const auto document = json::Document::Parse(txt, true);
            if (!document)
                return;
            const json::View biome = document->root()["minecraft:client_biome"];
            const json::View ident = biome["description"]["identifier"];
            const json::View components = biome["components"];
            if (!ident.isString() || !components.valid())
                return;
            BiomeColorRule& rule = rules[ident.str()];
            std::array<float, 3> c;
            if (const auto v = components["minecraft:grass_appearance"]["color"]; ResolveTint(v, mapAvg, c)) {
                rule.grass = c;
                rule.hasGrass = true;
            }
            if (const auto v = components["minecraft:foliage_appearance"]["color"];
                ResolveTint(v, mapAvg, c)) {
                rule.foliage = c;
                rule.hasFoliage = true;
            }
            if (const auto v = components["minecraft:dry_foliage_color"]["color"];
                ResolveTint(v, mapAvg, c)) {
                rule.dryFoliage = c;
                rule.hasDry = true;
            }
            if (const auto v = components["minecraft:water_appearance"]["surface_color"];
                v.isString() && ParseHex(v.str(), c)) {
                rule.water = c;
                rule.hasWater = true;
                if (packLayer) {
                    std::string name = ident.str();
                    packWater[name] = c;
                    if (name.starts_with("minecraft:"))
                        packWater[name.substr(10)] = c;
                }
            }
        };
        {
            std::error_code dirEc;
            for (const auto& dirEntry :
                 std::filesystem::directory_iterator(vanillaRpRoot + "/biomes", dirEc)) {
                if (!dirEntry.path().filename().string().ends_with(".client_biome.json"))
                    continue;
                applyClientBiome(file::Read(dirEntry.path().string()), false);
            }
        }
        if (TexturePack::Instance().Active()) {
            const auto names = TexturePack::Instance().ListFiles("biomes", ".client_biome.json");
            for (const std::string& rel : names)
                for (const std::string& text : TexturePack::Instance().ReadTextLayers(rel))
                    applyClientBiome(text, true);
        }

        defaultWater = ResolveDefaultWater(vanillaRpRoot);
        if (TexturePack::Instance().Active())
            for (const char* file : {"biomes_client.json", "biome_client.json"})
                for (const std::string& text : TexturePack::Instance().ReadTextLayers(file)) {
                    const auto document = json::Document::Parse(text, true);
                    if (!document)
                        continue;
                    const json::View biomes = document->root()["biomes"];
                    for (const auto member : biomes.members()) {
                        const std::string key = member.key;
                        const json::View value = member.value;
                        std::array<float, 3> c;
                        if (!ParseHex(value["water_surface_color"].str(), c))
                            continue;
                        if (key == "default")
                            packDefaultWater = c;
                        else {
                            packWater[key] = c;
                            if (key.starts_with("minecraft:"))
                                packWater[key.substr(10)] = c;
                        }
                    }
                }

        auto avg = [&](const char* m, std::array<float, 3> fb) {
            auto it = mapAvg.find(m);
            return it == mapAvg.end() ? fb : it->second;
        };
        std::array<float, 3> plainsG = Grass("minecraft:plains");
        std::array<float, 3> plainsF = Foliage("minecraft:plains");
        Log("biomes",
            "colormaps={} rules={} climates={} plainsGrass=({},{},{}) plainsFoliage=({},{},{}) "
            "grassAvg=({},{},{}) water=({},{},{})",
            mapAvg.size(), rules.size(), climate.size(), plainsG[0], plainsG[1], plainsG[2], plainsF[0],
            plainsF[1], plainsF[2], avg("grass", {0, 0, 0})[0], avg("grass", {0, 0, 0})[1],
            avg("grass", {0, 0, 0})[2], defaultWater[0], defaultWater[1], defaultWater[2]);

        bool complete = mapRGB.contains("grass") && mapRGB.contains("foliage") && !climate.empty() &&
                        !rules.empty() && !idToName.empty();
        if (!complete && attempts < 2)
            return false;

        ready.store(true, std::memory_order_release);
        return true;
    }

    std::array<float, 3> BiomeTints::MapAverage(const std::string& mapName) const {
        auto it = mapAvg.find(mapName);
        if (it != mapAvg.end())
            return it->second;
        return {1, 1, 1};
    }

    std::array<float, 3> BiomeTints::SampleMap(const std::vector<uint8_t>& rgb, float temperature,
                                               float downfall) const {
        float t = Clamp01(temperature);
        float d = Clamp01(downfall);
        int x = (int)((1.0f - t) * 255.0f);
        int y = (int)((1.0f - t * d) * 255.0f);
        if (x < 0)
            x = 0;
        if (x > 255)
            x = 255;
        if (y < 0)
            y = 0;
        if (y > 255)
            y = 255;
        const uint8_t* p = rgb.data() + ((size_t)y * 256 + x) * 3;
        return {(float)p[0] / 255.0f, (float)p[1] / 255.0f, (float)p[2] / 255.0f};
    }

    std::array<float, 3> BiomeTints::SampleMapChecked(const std::vector<uint8_t>& rgb, float t, float d,
                                                      const char* mapName) const {
        auto c = SampleMap(rgb, t, d);
        if (c[0] > 0.94f && c[1] > 0.94f && c[2] > 0.94f) {
            auto it = mapAvg.find(mapName);
            if (it != mapAvg.end())
                return it->second;
        }
        return c;
    }

    std::array<float, 3> BiomeTints::Grass(const std::string& biomeName) const {
        auto it = rules.find(biomeName);
        if (it != rules.end() && it->second.hasGrass)
            return it->second.grass;
        auto cl = climate.find(biomeName);
        auto gm = mapRGB.find("grass");
        if (cl != climate.end() && gm != mapRGB.end())
            return SampleMapChecked(gm->second, cl->second[0], cl->second[1], "grass");
        return {0.4745f, 0.7529f, 0.3529f};
    }

    std::array<float, 3> BiomeTints::Foliage(const std::string& biomeName) const {
        auto it = rules.find(biomeName);
        if (it != rules.end() && it->second.hasFoliage)
            return it->second.foliage;
        auto cl = climate.find(biomeName);
        auto fm = mapRGB.find("foliage");
        if (cl != climate.end() && fm != mapRGB.end())
            return SampleMapChecked(fm->second, cl->second[0], cl->second[1], "foliage");
        return {0.42f, 0.65f, 0.28f};
    }

    std::array<float, 3> BiomeTints::DryFoliage(const std::string& biomeName) const {
        auto it = rules.find(biomeName);
        if (it != rules.end() && it->second.hasDry)
            return it->second.dryFoliage;
        auto g = mapAvg.find("dry_foliage");
        if (g != mapAvg.end())
            return g->second;
        return Foliage(biomeName);
    }

    std::array<float, 3> BiomeTints::Water(const std::string& biomeName) const {
        struct Entry {
            const char* name;
            uint32_t color;
        };
        static constexpr Entry kJava[] = {
            {"mangrove_swamp", 0x3A7A6A}, {"swamp", 0x617B64},        {"meadow", 0x0E4ECF},
            {"cherry_grove", 0x5DB7EF},   {"warm_ocean", 0x43D5EE},   {"lukewarm_ocean", 0x45ADF2},
            {"frozen_ocean", 0x3938C9},   {"frozen_river", 0x3938C9}, {"cold_ocean", 0x3D57D6},
            {"snowy_", 0x3D57D6},         {"ice_spikes", 0x3D57D6},   {"grove", 0x3D57D6},
            {"frozen_peaks", 0x3D57D6},   {"jagged_peaks", 0x3D57D6},
        };
        if (const auto it = packWater.find(biomeName); it != packWater.end())
            return it->second;
        for (const Entry& entry : kJava)
            if (biomeName.contains(entry.name))
                return {((entry.color >> 16) & 255) / 255.0f, ((entry.color >> 8) & 255) / 255.0f,
                        (entry.color & 255) / 255.0f};
        if (packDefaultWater)
            return *packDefaultWater;
        return {0x3F / 255.0f, 0x76 / 255.0f, 0xE4 / 255.0f};
    }

    std::string BiomeTints::NameForId(int id) const {
        auto it = idToName.find(id);
        if (it != idToName.end())
            return it->second;
        return "minecraft:plains";
    }

    int BiomeTints::IdForName(const std::string& name) const {
        auto it = nameToId.find(name);
        if (it != nameToId.end())
            return it->second;
        return 1;
    }

    std::array<float, 3> ResolveDefaultWater(const std::string& vanillaRpRoot) {
        std::array<float, 3> water = {0.266f, 0.686f, 0.961f};
        if (const auto document =
                json::Document::Parse(file::Read(vanillaRpRoot + "/biomes_client.json"), true)) {
            const json::View biomes = document->root()["biomes"];
            const json::View pick =
                biomes.contains("default") ? biomes["default"] : biomes["minecraft:plains"];
            std::array<float, 3> parsed;
            if (ParseHex(pick["water_surface_color"].str(), parsed))
                water = parsed;
        }
        if (TexturePack::Instance().Active())
            for (const char* file : {"biomes_client.json", "biome_client.json"})
                for (const std::string& text : TexturePack::Instance().ReadTextLayers(file))
                    if (const auto document = json::Document::Parse(text, true)) {
                        const json::View biomes = document->root()["biomes"];
                        if (biomes.contains("default")) {
                            std::array<float, 3> parsed;
                            if (ParseHex(biomes["default"]["water_surface_color"].str(), parsed))
                                water = parsed;
                        }
                    }
        return water;
    }
}
