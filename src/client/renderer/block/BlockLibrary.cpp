#include "client/renderer/block/BlockLibrary.hpp"
#include "client/resources/ServerBlocks.hpp"
#include "client/resources/ServerPacks.hpp"
#include "client/ui/Json.hpp"
#include "client/renderer/block/BiomeTints.hpp"
#include <cstring>
#include <map>
#include <unordered_set>
#include <algorithm>
#include "world/level/block/BlockPalette.hpp"
#include "client/resources/TextureLoader.hpp"
#include "util/Resources.hpp"
#include "client/resources/TexturePack.hpp"

#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "util/Json.hpp"
#include <sstream>
#include "util/Paths.hpp"
#include "util/ThreadPool.hpp"
#include <unordered_map>
#include "client/Options.hpp"
#include "util/FileIo.hpp"
#include "util/Log.hpp"

namespace perlin {
    namespace {

        void WriteFile(const std::string& path, const char* content) {
            if (FILE* f = fopen(path.c_str(), "w")) {
                fwrite(content, 1, strlen(content), f);
                fclose(f);
            }
        }

        bool SeedFromExeDir(const std::string& fileName, const std::string& destPath) {
            std::string content;
            if (!resources::Read(fileName, content) || content.empty())
                return false;
            WriteFile(destPath, content.c_str());
            return true;
        }

    }

    int BlockLibrary::NewTile() {
        const int t = nextTile++;
        atlas.resize((size_t)nextTile * tilePxValue * tilePxValue * 4, 0);
        return t;
    }

    void BlockLibrary::SetTexturePack(bool active) {
        int widest = 16;
        if (active) {
            static const char* const kProbe[] = {"stone",  "dirt",       "cobblestone", "planks_oak",
                                                 "sand",   "grass_side", "log_oak",     "bedrock",
                                                 "gravel", "iron_block"};
            TexturePack& pack = TexturePack::Instance();
            for (const char* name : kProbe) {
                Texture t;
                for (const char* ext : {".png", ".tga"})
                    if (pack.Load(std::string("textures/blocks/") + name + ext, t)) {
                        widest = std::max(widest, t.width);
                        break;
                    }
            }
        }
        int size = 16;
        while (size < widest && size < 256)
            size <<= 1;
        tilePxValue = size;
        Log("textures", "pack={} tile={}px", (active ? TexturePack::Instance().Description() : "<vanilla>"),
            size);
    }

    namespace {
        void ResampleFrame(const unsigned char* data, int w, int h, size_t stride, int size, uint8_t* out) {
            for (int y = 0; y < size; y++) {
                for (int x = 0; x < size; x++) {
                    unsigned char avg[4];
                    const unsigned char* src = avg;
                    if (w <= size || h <= size) {
                        const int sx = std::min(w - 1, (int)((int64_t)x * w / size));
                        const int sy = std::min(h - 1, (int)((int64_t)y * h / size));
                        src = data + ((size_t)sy * stride + sx) * 4;
                    } else {
                        const int x0 = (int)((int64_t)x * w / size),
                                  x1 = std::max(x0 + 1, (int)((int64_t)(x + 1) * w / size));
                        const int y0 = (int)((int64_t)y * h / size),
                                  y1 = std::max(y0 + 1, (int)((int64_t)(y + 1) * h / size));
                        double r = 0, g = 0, b = 0, a = 0;
                        int n = 0;
                        for (int yy = y0; yy < y1 && yy < h; yy++)
                            for (int xx = x0; xx < x1 && xx < w; xx++) {
                                const unsigned char* p = data + ((size_t)yy * stride + xx) * 4;
                                r += p[0] * p[3];
                                g += p[1] * p[3];
                                b += p[2] * p[3];
                                a += p[3];
                                n++;
                            }
                        if (a > 0) {
                            avg[0] = (unsigned char)(r / a + 0.5);
                            avg[1] = (unsigned char)(g / a + 0.5);
                            avg[2] = (unsigned char)(b / a + 0.5);
                        } else {
                            avg[0] = avg[1] = avg[2] = 0;
                        }
                        avg[3] = n ? (unsigned char)(a / n + 0.5) : 0;
                    }
                    uint8_t* d = out + ((size_t)y * size + x) * 4;
                    d[0] = src[0];
                    d[1] = src[1];
                    d[2] = src[2];
                    d[3] = src[3];
                }
            }
        }
    }

    int BlockLibrary::GlassTile(int baseTile, int mode, int) const {
        if (mode != 2 && mode != 3)
            return baseTile;
        const auto it = glassTiles.find(baseTile);
        if (it == glassTiles.end() || it->second.size() < 2)
            return baseTile;
        const int t = it->second[mode == 2 ? 0 : 1];
        return t > 0 ? t : baseTile;
    }

    void BlockLibrary::BuildGlassVariants() {
        if (glassBuilt)
            return;
        glassBuilt = true;
        glassTiles.clear();
        const int T = tilePxValue;
        const int fw = std::max(1, T / 16);
        std::vector<int> bases;
        std::unordered_set<int> tinted;
        for (const auto& [path, tile] : tileByPng) {
            const bool tintedGlass = path.contains("textures/blocks/tinted_glass") && !path.contains("pane");
            const bool plain = path.contains("textures/blocks/glass.") || path == "textures/blocks/glass";
            if (plain || tintedGlass || (path.contains("textures/blocks/glass_") && !path.contains("pane"))) {
                bases.push_back(tile);
                if (!plain)
                    tinted.insert(tile);
            }
        }
        std::sort(bases.begin(), bases.end());
        bases.erase(std::unique(bases.begin(), bases.end()), bases.end());
        constexpr int kPerBase = 2;
        for (int base : bases) {
            if (nextTile + kPerBase + 32 > MaxTiles()) {
                Log("blocks", "atlas full: glass connection disabled for remaining glass tiles");
                break;
            }
            const std::vector<uint8_t> src(atlas.begin() + (size_t)base * T * T * 4,
                                           atlas.begin() + ((size_t)base + 1) * T * T * 4);
            const TileAlpha baseAlpha = AlphaOf(base);
            const bool keepInterior = tinted.contains(base);
            std::vector<int> list(2, 0);
            uint8_t wash[4] = {0, 0, 0, 0};
            if (keepInterior) {
                double r = 0, g = 0, b = 0, a = 0;
                int count = 0;
                for (int y = fw; y < T - fw; y++)
                    for (int x = fw; x < T - fw; x++) {
                        const uint8_t* in = src.data() + ((size_t)y * T + x) * 4;
                        r += in[0] * (double)in[3];
                        g += in[1] * (double)in[3];
                        b += in[2] * (double)in[3];
                        a += in[3];
                        count++;
                    }
                if (a > 0 && count > 0) {
                    wash[0] = static_cast<uint8_t>(r / a);
                    wash[1] = static_cast<uint8_t>(g / a);
                    wash[2] = static_cast<uint8_t>(b / a);
                    wash[3] = static_cast<uint8_t>(std::clamp(a / count, 70.0, 210.0));
                }
            }
            const auto make = [&](int mode, int mask) {
                const int t = NewTile();
                uint8_t* out = atlas.data() + (size_t)t * T * T * 4;
                std::copy(src.begin(), src.end(), out);
                for (int y = 0; y < T; y++)
                    for (int x = 0; x < T; x++) {
                        const bool l = x < fw, r = x >= T - fw, tp = y < fw, bt = y >= T - fw;
                        const bool frame = l || r || tp || bt;
                        uint8_t* px = out + ((size_t)y * T + x) * 4;
                        if (!frame) {
                            if (mode >= 2) {
                                if (keepInterior && wash[3]) {
                                    px[0] = wash[0], px[1] = wash[1], px[2] = wash[2], px[3] = wash[3];
                                } else {
                                    px[3] = 0;
                                }
                            }
                            continue;
                        }
                        if (mode == 3)
                            continue;
                        const bool exposed = (l && (mask & 1)) || (r && (mask & 2)) || (tp && (mask & 4)) ||
                                             (bt && (mask & 8));
                        if (exposed)
                            continue;
                        if (keepInterior && mode < 2) {
                            const int ix = std::clamp(x, fw, T - fw - 1), iy = std::clamp(y, fw, T - fw - 1);
                            const uint8_t* in = src.data() + ((size_t)iy * T + ix) * 4;
                            std::copy(in, in + 4, px);
                        } else {
                            px[3] = 0;
                        }
                    }
                alphaByTile[t] = baseAlpha;
                pathByTile[t] = "__glass_variant";
                return t;
            };
            list[0] = make(2, 15);
            list[1] = make(3, 0);
            glassTiles[base] = std::move(list);
        }
    }

    int BlockLibrary::TileContaining(const std::string& part) const {
        for (const auto& [path, tile] : tileByPng)
            if (path.contains(part))
                return tile;
        return -1;
    }

    std::vector<uint8_t> BlockLibrary::FoliageFlags() const {
        std::vector<uint8_t> flags;
        const auto has = [](const std::string& s, const char* part) { return s.contains(part); };
        for (const auto& [path, tile] : tileByPng) {
            if (tile < 0)
                continue;
            const size_t slash = path.rfind('/');
            const std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
            uint8_t kind = 0;
            if (has(name, "leaves") || name.starts_with("vine") || has(name, "cave_vines") ||
                has(name, "weeping_vines") || has(name, "twisting_vines"))
                kind = 1;
            else if (has(name, "double_plant") || has(name, "doubletall") || has(name, "pitcher_plant"))
                kind = has(name, "bottom") ? 3 : 4;
            else if (has(name, "tallgrass") || has(name, "short_grass") || has(name, "fern") ||
                     name.starts_with("flower_") || has(name, "sapling") || has(name, "deadbush") ||
                     has(name, "_stage") || has(name, "sweet_berry_bush") || has(name, "seagrass") ||
                     has(name, "torchflower") || has(name, "wither_rose") || has(name, "nether_sprouts") ||
                     has(name, "crimson_roots") || has(name, "warped_roots") || has(name, "azalea_plant") ||
                     has(name, "pink_petals") || has(name, "wildflowers") || has(name, "bush") ||
                     has(name, "firefly_bush") || has(name, "cactus_flower"))
                kind = 2;
            if (has(name, "melon_stem") || has(name, "pumpkin_stem") || has(name, "cocoa") ||
                has(name, "nether_wart") || has(name, "_cake"))
                kind = 0;
            if (!kind)
                continue;
            if (flags.size() <= static_cast<size_t>(tile))
                flags.resize(static_cast<size_t>(tile) + 1, 0);
            flags[static_cast<size_t>(tile)] = kind;
        }
        return flags;
    }

    namespace {
        std::unordered_map<std::string, Texture>& PrefetchCache() {
            static std::unordered_map<std::string, Texture> cache;
            return cache;
        }

        void PrefetchTextures(const std::string& root, const std::string& terrainText) {
            namespace fs = std::filesystem;
            std::vector<std::string> paths;
            std::error_code ec;
            size_t at = 0;
            while ((at = terrainText.find("\"textures/", at)) != std::string::npos) {
                const size_t end = terrainText.find('"', at + 1);
                if (end == std::string::npos)
                    break;
                const std::string stem = terrainText.substr(at + 1, end - at - 1);
                at = end + 1;
                for (const char* ext : {".png", ".tga"}) {
                    const std::string path = root + "/" + stem + ext;
                    if (fs::is_regular_file(path, ec))
                        paths.push_back(path);
                }
            }
            std::sort(paths.begin(), paths.end());
            paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
            std::vector<Texture> decoded(paths.size());
            std::vector<char> ok(paths.size(), 0);
            ThreadPool pool(HardwareThreads());
            pool.ParallelFor(paths.size(),
                             [&](size_t i) { ok[i] = LoadTexture(paths[i], decoded[i]) ? 1 : 0; });
            auto& cache = PrefetchCache();
            cache.clear();
            cache.reserve(paths.size());
            for (size_t i = 0; i < paths.size(); i++)
                if (ok[i])
                    cache.emplace(std::move(paths[i]), std::move(decoded[i]));
        }

        struct PrefetchGuard {
            ~PrefetchGuard() { PrefetchCache().clear(); }
        };
    }

    int BlockLibrary::FetchTile(const std::string& pngRelPath, const std::string& vanillaRpRoot) {
        auto it = tileByPng.find(pngRelPath);
        if (it != tileByPng.end())
            return it->second;
        if (nextTile >= MaxTiles())
            return 0;
        const int kTilePx = tilePxValue;
        std::string full;
        int w = 0, h = 0;
        Texture tex;
        std::vector<std::string> tries;
        std::string stem = pngRelPath;
        const std::string suffix = "_opaque";
        if (stem.size() > suffix.size() &&
            stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0)
            stem.resize(stem.size() - suffix.size());
        const std::string order[2] = {fancy ? stem : stem + suffix, fancy ? stem + suffix : stem};
        for (const std::string& base : order) {
            tries.push_back(base + ".tga");
            tries.push_back(base + ".png");
        }
        int tile = 0;
        bool ok = false;
        TexturePack& pack = TexturePack::Instance();
        for (const std::string& dir : ServerPacks::Instance().Directories()) {
            for (auto& t : tries)
                if (LoadTexture(dir + "/" + t, tex) && tex.width > 0 && tex.height > 0) {
                    w = tex.width;
                    h = tex.height;
                    ok = true;
                    break;
                }
            if (ok)
                break;
        }
        if (!ok && pack.Active())
            for (auto& t : tries)
                if (pack.Load(t, tex)) {
                    w = tex.width;
                    h = tex.height;
                    ok = true;
                    break;
                }
        if (!ok)
            for (auto& t : tries) {
                full = vanillaRpRoot + "/" + t;
                const auto cached = PrefetchCache().find(full);
                if (cached != PrefetchCache().end())
                    tex = cached->second;
                if ((cached != PrefetchCache().end() || LoadTexture(full, tex)) && tex.width > 0 &&
                    tex.height > 0) {
                    w = tex.width;
                    h = tex.height;
                    ok = true;
                    break;
                }
            }
        if (!ok) {
            fetchFails++;
            return 0;
        }
        if (stem.contains("water_") && !stem.contains("lava")) {
            double luma = 0;
            size_t count = 0;
            for (size_t i = 0; i < tex.pixels.size(); i += 4) {
                luma += 0.299 * tex.pixels[i] + 0.587 * tex.pixels[i + 1] + 0.114 * tex.pixels[i + 2];
                count++;
            }
            const double scale = count && luma > 0 ? 0.82 * 255.0 * count / luma : 1.0;
            for (size_t i = 0; i < tex.pixels.size(); i += 4) {
                const double y =
                    0.299 * tex.pixels[i] + 0.587 * tex.pixels[i + 1] + 0.114 * tex.pixels[i + 2];
                const unsigned char gray = static_cast<unsigned char>(std::min(255.0, y * scale));
                tex.pixels[i] = tex.pixels[i + 1] = tex.pixels[i + 2] = gray;
                tex.pixels[i + 3] = 178;
            }
        }
        const unsigned char* data = tex.pixels.data();
        const bool strip = !fetchingGeometry && h > w && h % w == 0;
        const int fw = w, fh = strip ? w : h;
        const int frames = strip ? h / w : 1;
        tile = NewTile();
        tileByPng[pngRelPath] = tile;
        TileAlpha alpha;
        double satSum = 0;
        int satN = 0;
        int frameOpaque = 0;
        int opaqueAll = 0;
        int partialAlpha = 0;
        uint8_t* tilePixels = atlas.data() + (size_t)tile * kTilePx * kTilePx * 4;
        ResampleFrame(data, fw, fh, (size_t)w, kTilePx, tilePixels);
        for (int y = 0; y < kTilePx; y++) {
            for (int x = 0; x < kTilePx; x++) {
                const uint8_t* src = tilePixels + ((size_t)y * kTilePx + x) * 4;
                if (src[3] > 20 && src[3] < 235)
                    partialAlpha++;
                if (src[3] < 128) {
                    alpha.holes = true;
                    continue;
                }
                opaqueAll++;
                if (src[0] | src[1] | src[2]) {
                    int mx = src[0] > src[1] ? (src[0] > src[2] ? src[0] : src[2])
                                             : (src[1] > src[2] ? src[1] : src[2]);
                    int mn = src[0] < src[1] ? (src[0] < src[2] ? src[0] : src[2])
                                             : (src[1] < src[2] ? src[1] : src[2]);
                    satSum += (double)(mx - mn) / mx;
                    satN++;
                    if (x == 0 || x == kTilePx - 1 || y == 0 || y == kTilePx - 1)
                        frameOpaque++;
                } else if (x == 0 || x == kTilePx - 1 || y == 0 || y == kTilePx - 1) {
                    frameOpaque++;
                }
            }
        }
        alpha.centerHollow = tilePixels[((size_t)(kTilePx / 2) * kTilePx + kTilePx / 2) * 4 + 3] < 128;
        alpha.translucent = partialAlpha >= kTilePx * kTilePx / 2;
        alphaByTile[tile] = alpha;
        if (satN > 0)
            alphaByTile[tile].saturation = (float)(satSum / satN);
        alphaByTile[tile].frame = (float)frameOpaque / (float)(4 * (kTilePx - 1));
        alphaByTile[tile].holeFrac = 1.0f - (float)opaqueAll / (float)(kTilePx * kTilePx);
        if (frames == 1) {
            uint64_t hash = 1469598103934665603ull;
            const uint64_t* words = reinterpret_cast<const uint64_t*>(tilePixels);
            const size_t count = (size_t)kTilePx * kTilePx / 2;
            for (size_t i = 0; i < count; i++)
                hash = (hash ^ words[i]) * 1099511628211ull;
            const auto same = tileByHash.find(hash);
            if (same != tileByHash.end() &&
                std::memcmp(atlas.data() + (size_t)same->second * kTilePx * kTilePx * 4, tilePixels,
                            (size_t)kTilePx * kTilePx * 4) == 0) {
                alphaByTile.erase(tile);
                nextTile--;
                atlas.resize((size_t)nextTile * kTilePx * kTilePx * 4);
                tileByPng[pngRelPath] = same->second;
                return same->second;
            }
            tileByHash[hash] = tile;
        }
        pathByTile[tile] = pngRelPath;
        if (frames > 1) {
            AnimSrc anim;
            anim.frames = frames;
            anim.pixels.resize((size_t)frames * kTilePx * kTilePx * 4);
            for (int f = 0; f < frames; f++)
                ResampleFrame(data + (size_t)f * fh * w * 4, fw, fh, (size_t)w, kTilePx,
                              anim.pixels.data() + (size_t)f * kTilePx * kTilePx * 4);
            animByTile[tile] = std::move(anim);
        }
        return tile;
    }

    int BlockLibrary::SynthTile(const std::string& label) {
        uint32_t h = 2166136261u;
        for (char c : label)
            h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
        if (nextTile + 24 >= MaxTiles()) {
            if (!synthTiles.empty())
                return synthTiles[h % synthTiles.size()];
            if (nextTile >= MaxTiles())
                return 0;
        }
        const int px = tilePxValue;
        const int tile = NewTile();
        uint8_t* out = atlas.data() + static_cast<size_t>(tile) * px * px * 4;
        const float base[3] = {90.0f + static_cast<float>(h & 0x7F),
                               90.0f + static_cast<float>((h >> 7) & 0x7F),
                               90.0f + static_cast<float>((h >> 14) & 0x7F)};
        for (int y = 0; y < px; y++)
            for (int x = 0; x < px; x++) {
                uint32_t n = h ^ (static_cast<uint32_t>(x / std::max(1, px / 16)) * 374761393u) ^
                             (static_cast<uint32_t>(y / std::max(1, px / 16)) * 668265263u);
                n = (n ^ (n >> 13)) * 1274126177u;
                const float jitter = 0.88f + 0.12f * static_cast<float>(n & 0xFF) / 255.0f;
                const bool edge = x < std::max(1, px / 16) || y < std::max(1, px / 16) ||
                                  x >= px - std::max(1, px / 16) || y >= px - std::max(1, px / 16);
                const float shade = edge ? 0.72f : jitter;
                uint8_t* p = out + (static_cast<size_t>(y) * px + x) * 4;
                for (int c = 0; c < 3; c++)
                    p[c] = static_cast<uint8_t>(std::min(255.0f, base[c] * shade));
                p[3] = 255;
            }
        alphaByTile[tile] = TileAlpha{};
        pathByTile[tile] = "synthetic/" + label;
        synthTiles.push_back(tile);
        return tile;
    }

    int BlockLibrary::FetchOrSynth(const std::string& pngRelPath, const std::string& vanillaRpRoot,
                                   const std::string& label,
                                   const std::unordered_map<std::string, std::string>& stems) {
        int tile = FetchTile(pngRelPath, vanillaRpRoot);
        if (tile > 0)
            return tile;
        const size_t slash = pngRelPath.find_last_of('/');
        const std::string stem = slash == std::string::npos ? pngRelPath : pngRelPath.substr(slash + 1);
        std::string blockName = label.substr(0, label.find('/'));
        if (const size_t colon = blockName.find(':'); colon != std::string::npos)
            blockName.erase(0, colon + 1);
        for (const std::string& candidate : {stem, blockName}) {
            const auto found = stems.find(candidate);
            if (found != stems.end()) {
                tile = FetchTile(found->second, vanillaRpRoot);
                if (tile > 0) {
                    tileByPng[pngRelPath] = tile;
                    return tile;
                }
            }
        }
        Log("blocks", "no texture for {} ({}), using a generated tile", label, pngRelPath);
        tile = SynthTile(label);
        if (tile > 0)
            tileByPng[pngRelPath] = tile;
        return tile;
    }

    BlockLibrary::TileAlpha BlockLibrary::AlphaOf(int tile) const {
        if (tile <= 0)
            return {};
        auto it = alphaByTile.find(tile);
        return it == alphaByTile.end() ? TileAlpha{} : it->second;
    }

    float BlockLibrary::SaturationOf(int tile) const {
        if (tile <= 0)
            return 0;
        auto it = alphaByTile.find(tile);
        return it == alphaByTile.end() ? 0 : it->second.saturation;
    }

    std::string BlockLibrary::TilePathOf(int tile) const {
        auto it = pathByTile.find(tile);
        return it == pathByTile.end() ? std::string() : it->second;
    }

    void BlockLibrary::PollAnimation(uint64_t step,
                                     const std::function<void(int tile, const uint8_t* rgba)>& emit) {
        if (!ready || animByTile.empty())
            return;
        const size_t frameBytes = (size_t)tilePxValue * tilePxValue * 4;
        for (auto& [tile, src] : animByTile) {
            if (src.frames <= 1)
                continue;
            emit(tile, src.pixels.data() + frameBytes * (size_t)(step % (uint64_t)src.frames));
        }
    }

    void BlockLibrary::Reset() {
        std::lock_guard<std::mutex> lock(lookMutex);
        ready = false;
        complete = false;
        glassBuilt = false;
        fetchFails = 0;
        lastFileMisses = -1;
        stallStreak = 0;
        nextTile = 1;
        synthTiles.clear();
        atlas.clear();
        tileByPng.clear();
        tileByHash.clear();
        animByTile.clear();
        baseTints.clear();
        geometry.Clear();
        geometryTried = false;
        alphaByTile.clear();
        overlayMaskTiles.clear();
        pathByTile.clear();
        glassTiles.clear();
        std::fill(models.tiles.begin(), models.tiles.end(), 0);
        std::fill(localLook.begin(), localLook.end(), nullptr);
    }

    bool BlockLibrary::EnsureLoaded(const std::string& perlinPackDir, const std::string& vanillaRpRoot) {
        if (ready && complete)
            return true;
        fetchFails = 0;
        PrefetchGuard prefetchGuard;

        std::string vanillaBlocks = file::Read(vanillaRpRoot + "/blocks.json");
        std::string terrainTex = file::Read(vanillaRpRoot + "/textures/terrain_texture.json");
        if (HardwareThreads() >= 4)
            PrefetchTextures(vanillaRpRoot, terrainTex);
        if (vanillaBlocks.empty() || terrainTex.empty()) {
            Log("blocks", "cannot read blocks.json/terrain_texture.json under {}", vanillaRpRoot);
            return false;
        }

        const auto blocksDocument = json::Document::Parse(vanillaBlocks, true);
        const auto terrainDocument = json::Document::Parse(terrainTex, true);
        if (!blocksDocument || !terrainDocument) {
            Log("blocks", "cannot parse vanilla blocks.json/terrain_texture.json");
            return false;
        }
        const json::View vBlocks = blocksDocument->root();
        const json::View vTerrain = terrainDocument->root();
        const json::View texData = vTerrain.contains("texture_data") ? vTerrain["texture_data"] : vTerrain;

        if (!BlockPalette::Instance().NamesLoaded()) {
            std::string defs = vanillaRpRoot + "/../metadata/vanilladata_modules/mojang-blocks.json";
            if (!BlockPalette::Instance().LoadVanillaDefinitions(defs)) {
                Log("blocks", "cannot load {}", defs);
                return false;
            }
        }
        if (!BlockPalette::Instance().SequenceLoaded()) {
            std::string table;
            if (resources::Read("block_states.json", table))
                BlockPalette::Instance().LoadServerStatesText(table, "block_states.json");
            else
                Log("blocks", "block_states.json is not bundled: sequential-id servers will read as air");
            if (std::string versions; resources::Read("block_versions.json", versions))
                BlockPalette::Instance().LoadVersionDeltas(versions);
        }
        const std::vector<std::string> allNames = BlockPalette::Instance().AllNames();
        int noTexture = 0;

        std::string blocksPath = perlinPackDir + "/blocks.json";
        if (file::Read(blocksPath).empty())
            SeedFromExeDir("blocks.json", blocksPath);
        std::optional<json::Document> blocksDocumentLocal;
        if (const std::string txt = file::Read(blocksPath); !txt.empty()) {
            blocksDocumentLocal = json::Document::Parse(txt, true);
            if (!blocksDocumentLocal)
                Log("blocks", "ignoring unreadable {}", blocksPath);
        }
        const json::View blocksJ = blocksDocumentLocal ? blocksDocumentLocal->root() : json::View();

        std::unordered_map<std::string, std::string> aliases = {{"iron_chain", "chain"}};
        std::unordered_map<std::string, std::array<float, 3>> tints;
        for (const auto [k, v] : blocksJ["aliases"].members())
            aliases[k] = v.str();
        for (const auto [k, v] : blocksJ["tints"].members())
            if (v.isArray() && v.size() >= 3)
                tints[k] = {v[0].real(), v[1].real(), v[2].real()};
        const json::View overrides = blocksJ["blocks"].isObject() ? blocksJ["blocks"] : blocksJ;

        std::vector<std::string> flatContains;
        {
            std::string shapesPath = perlinPackDir + "/shapes.json";
            if (file::Read(shapesPath).empty())
                SeedFromExeDir("shapes.json", shapesPath);
            if (const std::string txt = file::Read(shapesPath); !txt.empty()) {
                if (const auto shapes = json::Document::Parse(txt, true)) {
                    for (const json::View e : shapes->root()["flat_contains"].items())
                        if (e.isString())
                            flatContains.push_back(e.str());
                } else {
                    Log("blocks", "ignoring unreadable {}", shapesPath);
                }
            }
        }

        const int kTilePx = tilePxValue;
        if (atlas.empty()) {
            nextTile = 1;
            synthTiles.clear();
            atlas.assign((size_t)kTilePx * kTilePx * 4, 255);
        }

        auto resolveTexture = [&](const std::string& texName, std::vector<int>* all = nullptr) -> int {
            if (texName.starts_with("path:")) {
                int t = FetchTile(texName.substr(5), vanillaRpRoot);
                if (all && t)
                    all->push_back(t);
                return t;
            }
            if (!texData.contains(texName)) {
                int t = FetchTile(texName, vanillaRpRoot);
                if (t == 0 && !texName.contains('/'))
                    t = FetchTile(std::string("textures/blocks/") + texName, vanillaRpRoot);
                if (all && t)
                    all->push_back(t);
                return t;
            }
            const json::View entry = texData[texName];
            const auto noteOverlay = [&](const json::View e, int tile) {
                if (tile && e.isObject() && e["overlay_color"].isString())
                    overlayMaskTiles.insert(tile);
            };
            if (const json::View tintColor = entry["tint_color"]; tintColor.isString()) {
                const std::string hex = tintColor.str();
                unsigned r = 255, g = 255, b = 255;
                if (hex.size() == 7 && hex[0] == '#' &&
                    sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) == 3)
                    baseTints[texName] = {(float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f};
            }
            if (entry.isString()) {
                int t = FetchTile(entry.str(), vanillaRpRoot);
                if (all && t)
                    all->push_back(t);
                return t;
            }
            json::View list;
            if (entry.isObject() && entry.contains("textures")) {
                const json::View tx = entry["textures"];
                if (tx.isObject() && tx["path"].isString()) {
                    int t = FetchTile(tx["path"].str(), vanillaRpRoot);
                    noteOverlay(tx, t);
                    if (all && t)
                        all->push_back(t);
                    return t;
                }
                if (tx.isString()) {
                    int t = FetchTile(tx.str(), vanillaRpRoot);
                    if (all && t)
                        all->push_back(t);
                    return t;
                }
                list = tx;
            } else if (entry.isArray()) {
                list = entry;
            }
            if (list.size() == 0)
                return 0;
            int first = 0;
            for (const json::View e : list.items()) {
                std::string path;
                if (e.isString())
                    path = e.str();
                else if (e["path"].isString())
                    path = e["path"].str();
                if (path.empty())
                    continue;
                int tile = FetchTile(path, vanillaRpRoot);
                noteOverlay(e, tile);
                if (tile != 0) {
                    if (!first)
                        first = tile;
                    if (all)
                        all->push_back(tile);
                }
            }
            return first;
        };

        appearances["minecraft:air"] = BlockAppearance{};

        for (const std::string& name : allNames) {
            if (name == "minecraft:air")
                continue;
            try {
                std::string bare = name;
                if (bare.starts_with("minecraft:"))
                    bare = bare.substr(10);
                auto al = aliases.find(bare);
                std::string vkey = (al == aliases.end()) ? bare : al->second;
                // aliases run both ways: older servers and older samples still use the name before a rename
                if (!vBlocks.contains(vkey) && vBlocks.contains(bare))
                    vkey = bare;
                if (!vBlocks.contains(vkey))
                    for (const auto& [current, previous] : aliases)
                        if (previous == bare && vBlocks.contains(current)) {
                            vkey = current;
                            break;
                        }
                if (!vBlocks.contains(vkey)) {
                    std::string rpKey = BlockPalette::Instance().ResourcePackKey(name);
                    if (!rpKey.empty() && vBlocks.contains(rpKey))
                        vkey = rpKey;
                }

                std::string top, side, bottom;
                auto getFace = [&](json::View e, const char* face, std::string& out) {
                    if (e.isString()) {
                        out = e.str();
                        return;
                    }
                    if (!e.isObject())
                        return;
                    if (const json::View f = e[face]; f.isString()) {
                        out = f.str();
                        return;
                    }
                    if (!strcmp(face, "side")) {
                        static const char* kSideKeys[] = {"side", "east", "west", "north", "south"};
                        for (auto* sk : kSideKeys) {
                            if (const json::View s = e[sk]; s.isString()) {
                                out = s.str();
                                return;
                            }
                        }
                    }
                    if (!strcmp(face, "up") || !strcmp(face, "top")) {
                        if (const json::View t = e["top"]; t.isString())
                            out = t.str();
                    }
                };
                const json::View vit = vBlocks[vkey];
                bool hasCarried = false, hasIsotropic = false, hasAO = false;
                if (vit.isObject() && vit.contains("textures")) {
                    const json::View t = vit["textures"];
                    getFace(t, "up", top);
                    getFace(t, "down", bottom);
                    getFace(t, "side", side);
                    if (side.empty())
                        side = top;
                    if (bottom.empty())
                        bottom = top;
                    if (top.empty())
                        top = side;
                }
                if (vit.isObject()) {
                    hasCarried = vit.contains("carried_textures");
                    hasIsotropic = vit.contains("isotropic");
                    hasAO = vit.contains("ambient_occlusion_exponent");
                }

                BlockAppearance app;
                const json::View ov = overrides[name];
                const bool hasOverride = ov.isObject();
                auto pick = [&](const char* key, const std::string& vanilla) -> std::string {
                    if (ov[key].isString())
                        return ov[key].str();
                    return vanilla;
                };
                std::string allOv = ov["texture"].str();
                std::string topName = allOv.empty() ? pick("top", top) : allOv;
                std::string sideName = allOv.empty() ? pick("side", side.empty() ? top : side) : allOv;
                std::string botName = allOv.empty() ? pick("bottom", bottom.empty() ? top : bottom) : allOv;
                if (topName.empty() && sideName.empty() && botName.empty()) {
                    noTexture++;
                    continue;
                }
                std::vector<int> varTop, varSide, varBottom;
                app.top = topName.empty() ? 0 : resolveTexture(topName, &varTop);
                app.side = sideName.empty() ? app.top : resolveTexture(sideName, &varSide);
                if (sideName.empty())
                    varSide = varTop;
                app.bottom = botName.empty() ? app.top : resolveTexture(botName, &varBottom);
                if (botName.empty())
                    varBottom = varTop;
                auto collapseModes = [&](std::vector<int>& v) {
                    if (v.size() <= 1)
                        return;
                    auto stemOf = [&](int tile) {
                        auto it = pathByTile.find(tile);
                        std::string p = it == pathByTile.end() ? std::string() : it->second;
                        auto dot = p.find_last_of('.');
                        if (dot != std::string::npos)
                            p.resize(dot);
                        const std::string op = "_opaque";
                        if (p.size() > op.size() && p.compare(p.size() - op.size(), op.size(), op) == 0)
                            p.resize(p.size() - op.size());
                        return p;
                    };
                    std::string s0 = stemOf(v[0]);
                    for (size_t i = 1; i < v.size(); i++) {
                        if (stemOf(v[i]) != s0)
                            return;
                    }
                    v.resize(1);
                };
                collapseModes(varTop);
                collapseModes(varSide);
                collapseModes(varBottom);
                for (auto* vp : {&varTop, &varSide, &varBottom}) {
                    std::vector<int> u;
                    for (int t : *vp) {
                        if (u.empty() || u.back() != t)
                            u.push_back(t);
                    }
                    *vp = std::move(u);
                }
                (void)varTop;
                (void)varSide;
                (void)varBottom;
                app.varTop.clear();
                app.varSide.clear();
                app.varBottom.clear();
                app.texTop = topName;
                app.texSide = sideName;
                app.texBottom = botName;
                if (bare == "kelp" && !hasOverride) {
                    for (const char* texture : {"kelp_a", "kelp_b", "kelp_c", "kelp_d"})
                        app.varTop.push_back(resolveTexture(texture, nullptr));
                }
                if (ov["tint_faces"].isArray()) {
                    auto has = [&](const char* f) {
                        for (const json::View e : ov["tint_faces"].items())
                            if (e.isString() && e.str() == f)
                                return true;
                        return false;
                    };
                    auto tintFor = [&](const std::string& tex) -> std::array<float, 3> {
                        if (ov["tint"].isArray() && ov["tint"].size() >= 3)
                            return {ov["tint"][0].real(), ov["tint"][1].real(), ov["tint"][2].real()};
                        auto oo = baseTints.find(tex);
                        if (oo != baseTints.end())
                            return oo->second;
                        auto it = tints.find(tex);
                        if (it != tints.end())
                            return it->second;
                        return {1, 1, 1};
                    };
                    if (has("top") || has("side") || has("bottom")) {
                        auto tc = tintFor(topName);
                        app.tintR = tc[0];
                        app.tintG = tc[1];
                        app.tintB = tc[2];
                        app.tintTop = has("top");
                        app.tintSide = has("side");
                        app.tintBottom = has("bottom");
                    }
                    if (ov["cutout"].isBool())
                        app.cutout = ov["cutout"].boolean();
                } else {
                    auto findTint = [&](const std::string& tex) -> const std::array<float, 3>* {
                        auto oo = baseTints.find(tex);
                        if (oo != baseTints.end())
                            return &oo->second;
                        auto tt = tints.find(tex);
                        if (tt != tints.end())
                            return &tt->second;
                        return nullptr;
                    };
                    const std::array<float, 3>* tcTop = findTint(topName);
                    const std::array<float, 3>* tcSide = findTint(sideName);
                    const std::array<float, 3>* tcBottom = findTint(botName);
                    if (tcTop || tcSide || tcBottom) {
                        const std::array<float, 3>* tc = tcTop ? tcTop : (tcSide ? tcSide : tcBottom);
                        app.tintR = (*tc)[0];
                        app.tintG = (*tc)[1];
                        app.tintB = (*tc)[2];
                        app.tintTop = tcTop != nullptr;
                        app.tintSide = tcSide != nullptr;
                        app.tintBottom = tcBottom != nullptr;
                    }
                }

                {
                    bool perFace = !(topName == sideName && sideName == botName);
                    bool flat = BlockPalette::Instance().BlockHasProperty(name, "height");
                    if (!flat && !perFace && hasCarried &&
                        BlockPalette::Instance().BlockHasProperty(name, "growth")) {
                        TileAlpha ga = AlphaOf(app.top);
                        if (ga.holes)
                            flat = true;
                    }
                    if (!flat) {
                        for (auto& sub : flatContains) {
                            if (sub.empty())
                                continue;
                            if (bare.contains(sub)) {
                                flat = true;
                                break;
                            }
                        }
                    }
                    bool hasUpper = BlockPalette::Instance().BlockHasProperty(name, "upper_block_bit");
                    bool hasHalf = BlockPalette::Instance().BlockHasProperty(name, "minecraft:vertical_half");
                    if (hasCarried && !hasIsotropic && !hasAO && !perFace) {
                        const json::View ct = vit["carried_textures"];
                        if (ct.isString()) {
                            app.carriedTex = ct.str();
                            app.useCarriedColor = app.carriedTex != topName;
                        } else if (ct["side"].isString()) {
                            app.carriedTex = ct["side"].str();
                            app.useCarriedColor = app.carriedTex != sideName;
                        }
                    }
                    bool hasTile = (app.top | app.side | app.bottom) != 0;
                    if (!hasTile) {
                        app.shape = BlockAppearance::Shape::Cube;
                    } else if (flat) {
                        app.shape = BlockAppearance::Shape::Flat;
                    } else if (hasHalf || hasUpper) {
                        app.shape = hasUpper ? BlockAppearance::Shape::Cross : BlockAppearance::Shape::Cube;
                    } else if (!perFace &&
                               (BlockPalette::Instance().BlockHasProperty(name, "facing_direction") ||
                                BlockPalette::Instance().BlockHasProperty(name, "vine_direction_bits"))) {
                        TileAlpha aw = AlphaOf(app.top);
                        app.shape = aw.holes ? BlockAppearance::Shape::Wall : BlockAppearance::Shape::Cube;
                    } else if (!perFace) {
                        TileAlpha a = AlphaOf(app.top);
                        if (!a.holes)
                            app.shape = BlockAppearance::Shape::Cube;
                        else if (hasCarried && (hasIsotropic || hasAO))
                            app.shape = BlockAppearance::Shape::Cube;
                        else if (a.frame >= 0.5f)
                            app.shape = BlockAppearance::Shape::Cube;
                        else
                            app.shape = BlockAppearance::Shape::Cross;
                    } else {
                        TileAlpha at = AlphaOf(app.top), as = AlphaOf(app.side), ab = AlphaOf(app.bottom);
                        bool anySolid =
                            (app.top && !at.holes) || (app.side && !as.holes) || (app.bottom && !ab.holes);
                        app.shape = anySolid ? BlockAppearance::Shape::Cube : BlockAppearance::Shape::Cross;
                    }
                    if (app.shape == BlockAppearance::Shape::Cross ||
                        app.shape == BlockAppearance::Shape::Wall)
                        app.cutout = true;
                    app.overlayFrac = 0;
                    if (app.side && overlayMaskTiles.contains(app.side) && nextTile + 2 <= MaxTiles()) {
                        const int src = app.side;
                        const size_t px = (size_t)kTilePx * kTilePx;
                        bool anyMask = false, anyClear = false;
                        for (size_t i = 0; i < px; i++) {
                            const bool opaque = atlas[((size_t)src * px + i) * 4 + 3] >= 128;
                            anyMask = anyMask || opaque;
                            anyClear = anyClear || !opaque;
                        }
                        if (anyMask && anyClear) {
                            const int mt = NewTile();
                            const int bt = NewTile();
                            for (size_t i = 0; i < px; i++) {
                                const uint8_t* s = atlas.data() + ((size_t)src * px + i) * 4;
                                uint8_t* o = atlas.data() + ((size_t)mt * px + i) * 4;
                                uint8_t* b = atlas.data() + ((size_t)bt * px + i) * 4;
                                o[0] = b[0] = s[0];
                                o[1] = b[1] = s[1];
                                o[2] = b[2] = s[2];
                                o[3] = s[3] >= 128 ? 255 : 0;
                                b[3] = 255;
                            }
                            TileAlpha overlayAlpha;
                            overlayAlpha.holes = true;
                            alphaByTile[mt] = overlayAlpha;
                            pathByTile[mt] = std::string("__grass_overlay_") + std::to_string(src);
                            alphaByTile[bt] = TileAlpha{};
                            pathByTile[bt] = std::string("__grass_base_") + std::to_string(src);
                            app.overlayTile = mt;
                            app.overlayFrac = 1.0f;
                            app.side = bt;
                            app.varSide.clear();
                        }
                    }
                    if (ov["cutout"].isBool()) {
                        if (ov["cutout"].boolean()) {
                            if (app.shape != BlockAppearance::Shape::Flat &&
                                app.shape != BlockAppearance::Shape::Wall)
                                app.shape = BlockAppearance::Shape::Cross;
                            app.cutout = true;
                        } else {
                            app.cutout = false;
                            if (app.shape == BlockAppearance::Shape::Cross ||
                                app.shape == BlockAppearance::Shape::Wall)
                                app.shape = BlockAppearance::Shape::Cube;
                        }
                    }
                }
                appearances[name] = app;
            } catch (const std::exception& e) {
                printf("[blocks] bad entry for %s: %s\n", name.c_str(), e.what());
            } catch (...) {
                printf("[blocks] bad entry for %s\n", name.c_str());
            }
        }

        if (!modelsTried) {
            modelsTried = true;
            std::string bundled;
            if (!models.Load(perlinPackDir + "/block_models.bin") &&
                !(resources::Read("block_models.bin", bundled) && models.LoadFromMemory(bundled)))
                Log("models", "block_models.bin not found; using hand-built block shapes");
        }
        if (models.Loaded()) {
            const auto& names = models.Textures();
            for (size_t i = 0; i < names.size(); i++)
                if (models.tiles[i] == 0)
                    models.tiles[i] = FetchTile(names[i], vanillaRpRoot);
        }

        if (!geometryTried) {
            geometryTried = true;
            const BlockGeometry::FetchFn fetch = [this](const std::string& p, const std::string& root) {
                fetchingGeometry = true;
                const int tile = FetchTile(p, root);
                fetchingGeometry = false;
                return tile;
            };
            geometry.ScanPack(vanillaRpRoot, vanillaRpRoot + "/../behavior_pack/blocks", fetch);
            std::error_code ec;
            const std::filesystem::path extra = std::filesystem::path(perlinPackDir) / "packs";
            if (std::filesystem::is_directory(extra, ec))
                for (const auto& dir : std::filesystem::directory_iterator(extra, ec))
                    if (dir.is_directory())
                        geometry.ScanPack(dir.path().string(), (dir.path() / "blocks").string(), fetch);
        }

        if (fetchFails > 0) {
            Log("blocks", "{} textures missing; continuing without them", fetchFails);
        }
        lastFileMisses = 0;
        stallStreak = 0;
        BuildGlassVariants();
        complete = true;
        ready = true;
        for (auto& [name, app] : appearances) {
            if (app.carriedTex.empty())
                continue;
            int tile = resolveTexture(app.carriedTex);
            if (tile <= 0)
                continue;
            const size_t atlasW = (size_t)kTilePx;
            size_t base = (size_t)tile * kTilePx * kTilePx;
            double sr = 0, sg = 0, sb = 0;
            int n = 0;
            for (int y = 0; y < kTilePx; y++)
                for (int x = 0; x < kTilePx; x++) {
                    const uint8_t* d = atlas.data() + (base + (size_t)y * atlasW + x) * 4;
                    if (d[3] < 128)
                        continue;
                    sr += d[0];
                    sg += d[1];
                    sb += d[2];
                    n++;
                }
            if (n > 0) {
                app.hasCarriedRGB = true;
                app.carriedR = (float)(sr / n / 255.0);
                app.carriedG = (float)(sg / n / 255.0);
                app.carriedB = (float)(sb / n / 255.0);
            }
        }
        {
            const std::array<float, 3> wc = ResolveDefaultWater(vanillaRpRoot);
            for (auto& [name, app] : appearances) {
                auto isWaterNamed = [](const std::string& t) {
                    return t.contains("still_water") || t.contains("water_flow") ||
                           t.contains("flowing_water") || t.contains("water_still");
                };
                if (isWaterNamed(app.texTop) || isWaterNamed(app.texSide)) {
                    auto tintTowards = [&](float& r, float& g, float& b) {
                        r = wc[0];
                        g = wc[1];
                        b = wc[2];
                    };
                    tintTowards(app.tintR, app.tintG, app.tintB);
                    app.tintTop = true;
                    app.tintSide = true;
                    app.tintBottom = true;
                }
            }
        }
        Log("blocks", "appearances: {} names, {} without vanilla textures, {} atlas tiles", allNames.size(),
            noTexture, nextTile);
        {
            FILE* f = fopen((paths::DataDir() + "/blocks.log").c_str(), "w");
            if (f) {
                fprintf(f, "tiles=%d\n", nextTile);
                for (const std::string& name : allNames) {
                    auto ait = appearances.find(name);
                    if (ait == appearances.end()) {
                        fprintf(f, "  %s MISSING\n", name.c_str());
                    } else {
                        const BlockAppearance& a = ait->second;
                        const char* sh = a.shape == BlockAppearance::Shape::Cross  ? "cross"
                                         : a.shape == BlockAppearance::Shape::Flat ? "flat"
                                         : a.shape == BlockAppearance::Shape::Wall ? "wall"
                                                                                   : "cube";
                        fprintf(f, "  %s top=%d side=%d bottom=%d tint=%d shape=%s%s%s\n", name.c_str(),
                                a.top, a.side, a.bottom, (int)(a.tintTop || a.tintSide), sh,
                                a.hasCarriedRGB ? " carriedRGB" : "", a.overlayFrac > 0 ? " overlay" : "");
                        fprintf(f, "    tex top=%s side=%s bottom=%s%s%s\n", a.texTop.c_str(),
                                a.texSide.c_str(), a.texBottom.c_str(),
                                a.carriedTex.empty() ? "" : " carried=",
                                a.carriedTex.empty() ? "" : a.carriedTex.c_str());
                        fprintf(f, "    path top=%s side=%s bottom=%s\n", TilePathOf(a.top).c_str(),
                                TilePathOf(a.side).c_str(), TilePathOf(a.bottom).c_str());
                    }
                }
                fclose(f);
            }
        }
        return true;
    }

    namespace {
        std::string AfterColon(const std::string& s) {
            const size_t c = s.find(':');
            return c == std::string::npos ? s : s.substr(c + 1);
        }
    }

    bool BlockLibrary::ApplyCustom(const std::string& vanillaRpRoot) {
        if (!ServerBlocks::Instance().Pending())
            return false;
        std::vector<CustomBlockDef> defs = ServerBlocks::Instance().Take();
        std::unordered_map<std::string, std::string> pathByKey;
        for (const std::string& dir : ServerPacks::Instance().Directories()) {
            std::string text;
            {
                std::ifstream in(dir + "/textures/terrain_texture.json", std::ios::binary);
                if (!in)
                    continue;
                std::stringstream ss;
                ss << in.rdbuf();
                text = ss.str();
            }
            ui::Json doc;
            std::string error;
            if (!ui::Json::Parse(text, doc, error))
                continue;
            const ui::Json* data = doc.Find("texture_data");
            if (!data || !data->IsObject())
                continue;
            for (const auto& [key, value] : data->object) {
                const ui::Json* tex = value.Find("textures");
                if (!tex)
                    continue;
                if (tex->IsArray() && !tex->array.empty())
                    tex = &tex->array[0];
                if (tex->IsObject())
                    tex = tex->Find("path");
                if (tex && tex->IsString())
                    pathByKey[key] = tex->string;
            }
        }
        std::unordered_map<std::string, std::string> vanillaPathByKey;
        {
            std::ifstream in(vanillaRpRoot + "/textures/terrain_texture.json", std::ios::binary);
            if (in) {
                std::stringstream ss;
                ss << in.rdbuf();
                if (const auto doc = json::Document::Parse(ss.str(), true))
                    for (const auto member : doc->root()["texture_data"].members()) {
                        json::View tex = member.value["textures"];
                        if (tex.isArray())
                            tex = tex[static_cast<size_t>(0)];
                        if (tex.isObject())
                            tex = tex["path"];
                        if (tex.isString())
                            vanillaPathByKey.emplace(member.key, tex.str());
                    }
            }
        }
        bool any = false;
        const std::vector<std::string> serverDirs = ServerPacks::Instance().Directories();
        std::unordered_map<std::string, std::map<std::string, std::string>> legacyFaces;
        for (const std::string& dir : serverDirs) {
            std::ifstream in(dir + "/blocks.json", std::ios::binary);
            if (!in)
                continue;
            std::stringstream ss;
            ss << in.rdbuf();
            ui::Json doc;
            std::string error;
            if (!ui::Json::Parse(ss.str(), doc, error) || !doc.IsObject())
                continue;
            for (const auto& [blockName, entry] : doc.object) {
                if (!entry.IsObject())
                    continue;
                const ui::Json* tex = entry.Find("textures");
                if (!tex)
                    continue;
                std::string key = blockName;
                if (!key.contains(':'))
                    key = "minecraft:" + key;
                std::map<std::string, std::string> faces;
                if (tex->IsString()) {
                    faces["*"] = tex->string;
                } else if (tex->IsObject()) {
                    for (const auto& [face, v] : tex->object)
                        if (v.IsString())
                            faces[face] = v.string;
                }
                if (!faces.empty())
                    legacyFaces[key] = std::move(faces);
            }
        }
        std::unordered_map<std::string, std::string> stems;
        for (const std::string& dir : serverDirs) {
            std::error_code ec;
            for (std::filesystem::recursive_directory_iterator
                     it(std::filesystem::path(dir) / "textures",
                        std::filesystem::directory_options::skip_permission_denied, ec),
                 end;
                 !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file(ec))
                    continue;
                std::string name = it->path().filename().string();
                constexpr std::string_view kSet = ".texture_set.json";
                std::string stem;
                size_t extSize = 0;
                if (name.size() > kSet.size() &&
                    name.compare(name.size() - kSet.size(), kSet.size(), kSet) == 0) {
                    stem = name.substr(0, name.size() - kSet.size());
                    extSize = kSet.size();
                } else {
                    const std::string ext = it->path().extension().string();
                    if (ext != ".png" && ext != ".tga")
                        continue;
                    stem = it->path().stem().string();
                    extSize = ext.size();
                }
                std::string rel = std::filesystem::relative(it->path(), dir, ec).generic_string();
                rel.resize(rel.size() - extSize);
                stems.emplace(std::move(stem), std::move(rel));
            }
        }
        const BlockGeometry::FetchFn fetchGeometry = [this, &stems](const std::string& p,
                                                                    const std::string& root) {
            fetchingGeometry = true;
            int tile = FetchTile(p, root);
            if (tile == 0) {
                const size_t slash = p.find_last_of('/');
                const auto found = stems.find(slash == std::string::npos ? p : p.substr(slash + 1));
                if (found != stems.end())
                    tile = FetchTile(found->second, root);
            }
            fetchingGeometry = false;
            return tile;
        };
        for (const CustomBlockDef& def : defs) {
            if (!def.json.empty()) {
                geometry.AddCustom(def.name, def.json, serverDirs, vanillaRpRoot, fetchGeometry);
                any = true;
            }
            std::map<std::string, std::string> faceTex = def.faceTexture;
            if (faceTex.empty())
                if (const auto legacy = legacyFaces.find(def.name); legacy != legacyFaces.end())
                    faceTex = legacy->second;
            const auto resolve = [&](std::initializer_list<const char*> faces, std::string& pathOut) -> int {
                for (const char* face : faces) {
                    const auto f = faceTex.find(face);
                    if (f == faceTex.end())
                        continue;
                    const auto p = pathByKey.find(f->second);
                    const auto v = vanillaPathByKey.find(f->second);
                    const std::string path = p != pathByKey.end()          ? p->second
                                             : v != vanillaPathByKey.end() ? v->second
                                                                           : "textures/blocks/" + f->second;
                    pathOut = path;
                    return FetchOrSynth(path, vanillaRpRoot, def.name + "/" + f->second, stems);
                }
                return -1;
            };
            BlockAppearance a;
            std::string pTop, pSide, pBottom;
            int top = resolve({"up", "*"}, pTop);
            int side = resolve({"side", "north", "south", "east", "west", "*"}, pSide);
            int bottom = resolve({"down", "*"}, pBottom);
            if (top < 0 && side < 0 && bottom < 0) {
                const BlockAppearance* base = nullptr;
                {
                    std::string shortName = AfterColon(def.name);
                    static const char* const kSuffixes[] = {"_double_slab", "_stairs", "_slab",
                                                            "_wall",        "_fence",  "_pillar"};
                    for (const char* suffix : kSuffixes) {
                        const size_t n = std::strlen(suffix);
                        if (shortName.size() <= n || shortName.compare(shortName.size() - n, n, suffix) != 0)
                            continue;
                        const std::string stem = shortName.substr(0, shortName.size() - n);
                        for (const std::string& candidate :
                             {stem, stem + "_planks", stem + "s", stem + "_block"}) {
                            const auto found = appearances.find("minecraft:" + candidate);
                            if (found != appearances.end() &&
                                found->second.shape == BlockAppearance::Shape::Cube &&
                                (found->second.top | found->second.side | found->second.bottom) != 0) {
                                base = &found->second;
                                break;
                            }
                        }
                        break;
                    }
                }
                if (base) {
                    BlockAppearance copy = *base;
                    copy.cutout = def.cutout || base->cutout;
                    {
                        std::lock_guard<std::mutex> lock(lookMutex);
                        appearances[def.name] = std::move(copy);
                    }
                    any = true;
                    continue;
                }
                const std::string guess = "textures/blocks/" + AfterColon(def.name);
                side = FetchOrSynth(guess, vanillaRpRoot, def.name, stems);
                pSide = guess;
            }
            if (side < 0)
                side = top >= 0 ? top : bottom;
            if (top < 0)
                top = side;
            if (bottom < 0)
                bottom = side;
            {
                static constexpr const char* kFaces[4] = {"east", "west", "south", "north"};
                for (int i = 0; i < 4; i++) {
                    std::string unusedPath;
                    if (faceTex.contains(kFaces[i])) {
                        const int t = resolve({kFaces[i]}, unusedPath);
                        if (t > 0 && t != side)
                            a.sideTile[i] = t;
                    }
                }
            }
            a.top = top;
            a.side = side;
            a.bottom = bottom;
            a.texTop = pTop.empty() ? pSide : pTop;
            a.texSide = pSide;
            a.texBottom = pBottom.empty() ? pSide : pBottom;
            a.cutout = def.cutout;
            {
                std::lock_guard<std::mutex> lock(lookMutex);
                appearances[def.name] = std::move(a);
            }
            any = true;
        }
        return any;
    }

    const BlockAppearance& BlockLibrary::Look(int32_t localId) const {
        static BlockAppearance fallback;
        if (localId <= 0 || localId >= (int32_t)BlockPalette::kMaxLocal)
            return fallback;
        std::lock_guard<std::mutex> lookLock(lookMutex);
        if (localLook.empty())
            localLook.assign(BlockPalette::kMaxLocal, nullptr);
        const BlockAppearance*& cached = localLook[(size_t)localId];
        if (cached)
            return *cached;
        std::string name = BlockPalette::Instance().Name((uint16_t)localId);
        if (name.empty()) {
            if (!unknownLogged[localId]) {
                unknownLogged[localId] = true;
                if (FILE* f = fopen((paths::DataDir() + "/blocks.log").c_str(), "a")) {
                    fprintf(f,
                            "unknown network block id %d (no vanilla definition produces this hash; "
                            "custom block or newer game data than mojang-blocks.json)\n",
                            BlockPalette::Instance().NetworkId((uint16_t)localId));
                    fclose(f);
                }
            }
            return fallback;
        }
        auto ait = appearances.find(name);
        if (ait == appearances.end())
            return fallback;
        {
            std::string shortName = name;
            if (shortName.starts_with("minecraft:"))
                shortName.erase(0, 10);
            int bit = 0;
            if (shortName == "barrier")
                bit = kBarrier;
            else if (shortName.starts_with("light_block"))
                bit = kLightBlock;
            else if (shortName == "structure_void")
                bit = kStructureVoid;
            else if (shortName == "piston_arm_collision" || shortName == "sticky_piston_arm_collision" ||
                     shortName == "moving_block" || shortName == "movingblock")
                return fallback;
            if (bit && !(technicalMask.load(std::memory_order_relaxed) & bit))
                return fallback;
        }
        cached = &ait->second;
        return *cached;
    }

    void BlockLibrary::SetTechnicalVisible(int mask) const {
        if (technicalMask.exchange(mask) == mask)
            return;
        std::lock_guard<std::mutex> lookLock(lookMutex);
        std::fill(localLook.begin(), localLook.end(), nullptr);
    }

    const BlockAppearance* BlockLibrary::LookByName(const std::string& name) const {
        auto it = appearances.find(name);
        return it == appearances.end() ? nullptr : &it->second;
    }

    bool BlockLibrary::TilePixels(int tile, std::vector<uint8_t>& rgba) const {
        const size_t bytes = (size_t)tilePxValue * tilePxValue * 4;
        if (tile < 0 || (size_t)(tile + 1) * bytes > atlas.size())
            return false;
        rgba.assign(atlas.begin() + (size_t)tile * bytes, atlas.begin() + (size_t)(tile + 1) * bytes);
        return true;
    }

    void BlockLibrary::TileUV(int tile, float& u0, float& v0, float& u1, float& v1) {
        const int tx = tile % kAtlasTiles, ty = tile / kAtlasTiles;
        const float inv = 1.0f / (float)kAtlasTiles;
        const float half = 0.5f * inv / (float)tilePxValue;
        u0 = tx * inv + half;
        v0 = ty * inv + half;
        u1 = (tx + 1) * inv - half;
        v1 = (ty + 1) * inv - half;
    }

}
