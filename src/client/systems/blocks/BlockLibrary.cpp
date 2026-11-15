#include "client/systems/blocks/BlockLibrary.hpp"
#include "client/systems/resources/ServerBlocks.hpp"
#include "client/systems/resources/ServerPacks.hpp"
#include "util/JsonValue.hpp"
#include "client/systems/blocks/BiomeTints.hpp"
#include <cstring>
#include <map>
#include <unordered_set>
#include <algorithm>
#include "world/level/block/BlockPalette.hpp"
#include "client/systems/resources/TextureLoader.hpp"
#include "util/Resources.hpp"
#include "util/ResourcePaths.hpp"
#include "client/systems/resources/TexturePack.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include "util/Json.hpp"
#include "util/Paths.hpp"
#include "util/ThreadPool.hpp"
#include <unordered_map>
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

        bool SeedFromExeDir(const char* resourcePath, const std::string& destPath) {
            std::string content;
            if (!resources::Read(resourcePath, content) || content.empty())
                return false;
            WriteFile(destPath, content.c_str());
            return true;
        }

    }

    int BlockLibrary::NewTile() {
        const int t = nextTile++;
        atlas.resize(static_cast<size_t>(nextTile) * tilePx * tilePx * 4, 0);
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
        tilePx = size;
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
                        const int sx = std::min(w - 1, static_cast<int>((int64_t)x * w / size));
                        const int sy = std::min(h - 1, static_cast<int>((int64_t)y * h / size));
                        src = data + (static_cast<size_t>(sy) * stride + sx) * 4;
                    } else {
                        const int x0 = static_cast<int>((int64_t)x * w / size),
                                  x1 = std::max(x0 + 1, static_cast<int>((int64_t)(x + 1) * w / size));
                        const int y0 = static_cast<int>((int64_t)y * h / size),
                                  y1 = std::max(y0 + 1, static_cast<int>((int64_t)(y + 1) * h / size));
                        double r = 0, g = 0, b = 0, a = 0;
                        int n = 0;
                        for (int yy = y0; yy < y1 && yy < h; yy++)
                            for (int xx = x0; xx < x1 && xx < w; xx++) {
                                const unsigned char* p = data + (static_cast<size_t>(yy) * stride + xx) * 4;
                                r += p[0] * p[3];
                                g += p[1] * p[3];
                                b += p[2] * p[3];
                                a += p[3];
                                n++;
                            }
                        if (a > 0) {
                            avg[0] = static_cast<unsigned char>(r / a + 0.5);
                            avg[1] = static_cast<unsigned char>(g / a + 0.5);
                            avg[2] = static_cast<unsigned char>(b / a + 0.5);
                        } else {
                            avg[0] = avg[1] = avg[2] = 0;
                        }
                        avg[3] = n ? static_cast<unsigned char>(a / n + 0.5) : 0;
                    }
                    uint8_t* d = out + (static_cast<size_t>(y) * size + x) * 4;
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
        const int T = tilePx;
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
        std::ranges::sort(bases);
        bases.erase(std::unique(bases.begin(), bases.end()), bases.end());
        constexpr int kPerBase = 2;
        for (int base : bases) {
            if (nextTile + kPerBase + 32 > MaxTiles()) {
                Log("blocks", "atlas full: glass connection disabled for remaining glass tiles");
                break;
            }
            const std::vector<uint8_t> src(atlas.begin() + static_cast<size_t>(base) * T * T * 4,
                                           atlas.begin() + (static_cast<size_t>(base) + 1) * T * T * 4);
            const TileAlpha baseAlpha = AlphaOf(base);
            const bool keepInterior = tinted.contains(base);
            std::vector<int> list(2, 0);
            uint8_t wash[4] = {0, 0, 0, 0};
            if (keepInterior) {
                double r = 0, g = 0, b = 0, a = 0;
                int count = 0;
                for (int y = fw; y < T - fw; y++)
                    for (int x = fw; x < T - fw; x++) {
                        const uint8_t* in = src.data() + (static_cast<size_t>(y) * T + x) * 4;
                        r += in[0] * static_cast<double>(in[3]);
                        g += in[1] * static_cast<double>(in[3]);
                        b += in[2] * static_cast<double>(in[3]);
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
                uint8_t* out = atlas.data() + static_cast<size_t>(t) * T * T * 4;
                std::copy(src.begin(), src.end(), out);
                for (int y = 0; y < T; y++)
                    for (int x = 0; x < T; x++) {
                        const bool l = x < fw, r = x >= T - fw, tp = y < fw, bt = y >= T - fw;
                        const bool frame = l || r || tp || bt;
                        uint8_t* px = out + (static_cast<size_t>(y) * T + x) * 4;
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
                            const uint8_t* in = src.data() + (static_cast<size_t>(iy) * T + ix) * 4;
                            std::copy(in, in + 4, px);
                        } else {
                            px[3] = 0;
                        }
                    }
                SetAlpha(t, baseAlpha);
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
            std::ranges::sort(paths);
            paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
            std::vector<Texture> decoded(paths.size());
            std::vector<char> ok(paths.size(), 0);
            ThreadPool pool(BackgroundThreads(), true);
            pool.ParallelFor(paths.size(),
                             [&](size_t i) { ok[i] = LoadTexture(paths[i], decoded[i]) ? 1 : 0; });
            auto& cache = PrefetchCache();
            cache.clear();
            cache.reserve(paths.size());
            for (size_t i = 0; i < paths.size(); i++)
                if (ok[i])
                    cache.emplace(std::move(paths[i]), std::move(decoded[i]));
        }

        std::unordered_map<std::string, Texture>& ServerPrefetchCache() {
            static std::unordered_map<std::string, Texture> cache;
            return cache;
        }

        void PrefetchServerTextures(const std::vector<std::string>& dirs) {
            namespace fs = std::filesystem;
            std::vector<std::string> paths;
            std::error_code ec;
            for (const std::string& dir : dirs) {
                const fs::path root = fs::path(dir) / "textures";
                if (!fs::is_directory(root, ec))
                    continue;
                for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
                     !ec && it != end; it.increment(ec)) {
                    const std::string name = it->path().filename().string();
                    if (it->is_directory(ec)) {
                        if (name == "entity" || name == "items" || name == "item" || name == "ui" || name == "gui" ||
                            name == "particle" || name == "environment" || name == "painting" || name == "map")
                            it.disable_recursion_pending();
                        continue;
                    }
                    const std::string ext = it->path().extension().string();
                    if ((ext == ".png" || ext == ".tga") && it->file_size(ec) < (4u << 20))
                        paths.push_back(it->path().generic_string());
                }
            }
            std::vector<Texture> decoded(paths.size());
            std::vector<char> ok(paths.size(), 0);
            ThreadPool pool(BackgroundThreads(), true);
            pool.ParallelFor(paths.size(),
                             [&](size_t i) { ok[i] = LoadTexture(paths[i], decoded[i]) ? 1 : 0; });
            auto& cache = ServerPrefetchCache();
            cache.clear();
            cache.reserve(paths.size());
            for (size_t i = 0; i < paths.size(); i++)
                if (ok[i])
                    cache.emplace(std::move(paths[i]), std::move(decoded[i]));
        }

        struct PrefetchGuard {
            ~PrefetchGuard() { std::unordered_map<std::string, Texture>().swap(PrefetchCache()); }
        };
    }

    int BlockLibrary::FetchTile(const std::string& pngRelPath, const std::string& vanillaRpRoot) {
        auto it = tileByPng.find(pngRelPath);
        if (it != tileByPng.end())
            return it->second;
        if (nextTile >= MaxTiles()) {

            if (!atlasFullLogged.exchange(true))
                Log("blocks", "atlas full at {} tiles: further textures fall back to tile 0", MaxTiles());
            return 0;
        }
        const int kTilePx = tilePx;
        std::string full;
        int w = 0, h = 0;
        Texture tex;
        std::vector<std::string> tries;

        std::string stem = pngRelPath;
        float crop[4] = {0, 0, 1, 1};
        bool cropped = false;
        if (const size_t hash = stem.find('#'); hash != std::string::npos) {
            cropped = std::sscanf(stem.c_str() + hash + 1, "%f,%f,%f,%f", &crop[0], &crop[1], &crop[2], &crop[3]) == 4;
            stem.resize(hash);
        }
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
            for (auto& t : tries) {
                const std::string path = dir + "/" + t;
                const auto warm = ServerPrefetchCache().find(path);
                if (warm != ServerPrefetchCache().end())
                    tex = warm->second;
                if ((warm != ServerPrefetchCache().end() || LoadTexture(path, tex)) && tex.width > 0 &&
                    tex.height > 0) {
                    w = tex.width;
                    h = tex.height;
                    ok = true;
                    break;
                }
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
        if (cropped) {
            const int x0 = std::clamp(static_cast<int>(std::lround(crop[0] * w)), 0, w - 1);
            const int y0 = std::clamp(static_cast<int>(std::lround(crop[1] * h)), 0, h - 1);
            const int x1 = std::clamp(static_cast<int>(std::lround(crop[2] * w)), x0 + 1, w);
            const int y1 = std::clamp(static_cast<int>(std::lround(crop[3] * h)), y0 + 1, h);
            std::vector<unsigned char> cell(static_cast<size_t>(x1 - x0) * (y1 - y0) * 4);
            for (int y = y0; y < y1; y++)
                std::copy_n(tex.pixels.data() + (static_cast<size_t>(y) * w + x0) * 4, (x1 - x0) * 4,
                            cell.data() + static_cast<size_t>(y - y0) * (x1 - x0) * 4);
            tex.pixels = std::move(cell);
            tex.width = w = x1 - x0;
            tex.height = h = y1 - y0;
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
        uint8_t* tilePixels = atlas.data() + static_cast<size_t>(tile) * kTilePx * kTilePx * 4;
        ResampleFrame(data, fw, fh, static_cast<size_t>(w), kTilePx, tilePixels);
        for (int y = 0; y < kTilePx; y++) {
            for (int x = 0; x < kTilePx; x++) {
                const uint8_t* src = tilePixels + (static_cast<size_t>(y) * kTilePx + x) * 4;
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
                    satSum += static_cast<double>(mx - mn) / mx;
                    satN++;
                    if (x == 0 || x == kTilePx - 1 || y == 0 || y == kTilePx - 1)
                        frameOpaque++;
                } else if (x == 0 || x == kTilePx - 1 || y == 0 || y == kTilePx - 1) {
                    frameOpaque++;
                }
            }
        }
        alpha.centerHollow =
            tilePixels[(static_cast<size_t>(kTilePx / 2) * kTilePx + kTilePx / 2) * 4 + 3] < 128;
        alpha.translucent = partialAlpha >= kTilePx * kTilePx / 2;
        if (satN > 0)
            alpha.saturation = static_cast<float>(satSum / satN);
        alpha.frame = static_cast<float>(frameOpaque) / static_cast<float>(4 * (kTilePx - 1));
        alpha.holeFrac = 1.0f - static_cast<float>(opaqueAll) / static_cast<float>(kTilePx * kTilePx);
        SetAlpha(tile, alpha);
        if (frames == 1) {
            uint64_t hash = 1469598103934665603ull;
            const uint64_t* words = reinterpret_cast<const uint64_t*>(tilePixels);
            const size_t count = static_cast<size_t>(kTilePx) * kTilePx / 2;
            for (size_t i = 0; i < count; i++)
                hash = (hash ^ words[i]) * 1099511628211ull;
            const auto same = tileByHash.find(hash);
            if (same != tileByHash.end() &&
                std::memcmp(atlas.data() + static_cast<size_t>(same->second) * kTilePx * kTilePx * 4,
                            tilePixels, static_cast<size_t>(kTilePx) * kTilePx * 4) == 0) {
                EraseAlpha(tile);
                nextTile--;
                atlas.resize(static_cast<size_t>(nextTile) * kTilePx * kTilePx * 4);
                tileByPng[pngRelPath] = same->second;
                return same->second;
            }
            tileByHash[hash] = tile;
        }
        pathByTile[tile] = pngRelPath;
        if (frames > 1) {
            AnimSrc anim;
            anim.frames = frames;
            anim.pixels.resize(static_cast<size_t>(frames) * kTilePx * kTilePx * 4);
            for (int f = 0; f < frames; f++)
                ResampleFrame(data + static_cast<size_t>(f) * fh * w * 4, fw, fh, static_cast<size_t>(w),
                              kTilePx, anim.pixels.data() + static_cast<size_t>(f) * kTilePx * kTilePx * 4);
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
        const int px = tilePx;
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
        SetAlpha(tile, TileAlpha{});
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

    static uint64_t NextAlphaStamp() {
        static std::atomic<uint64_t> counter{1};
        return counter.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    void BlockLibrary::SetAlpha(int tile, const TileAlpha& alpha) {
        if (tile <= 0)
            return;
        std::unique_lock alphaLock(alphaMutex);
        if (static_cast<size_t>(tile) >= alphaByTile.size()) {
            alphaByTile.resize(static_cast<size_t>(tile) + 1);
            alphaPresent.resize(static_cast<size_t>(tile) + 1, 0);
        }
        alphaByTile[static_cast<size_t>(tile)] = alpha;
        alphaPresent[static_cast<size_t>(tile)] = 1;
        alphaVersion.store(NextAlphaStamp(), std::memory_order_release);
    }

    void BlockLibrary::EraseAlpha(int tile) {
        if (tile <= 0)
            return;
        std::unique_lock alphaLock(alphaMutex);
        if (static_cast<size_t>(tile) < alphaByTile.size()) {
            alphaByTile[static_cast<size_t>(tile)] = TileAlpha{};
            alphaPresent[static_cast<size_t>(tile)] = 0;
        }
        alphaVersion.store(NextAlphaStamp(), std::memory_order_release);
    }

    void BlockLibrary::ClearAlpha() {
        std::unique_lock alphaLock(alphaMutex);
        alphaByTile.clear();
        alphaPresent.clear();
        alphaVersion.store(NextAlphaStamp(), std::memory_order_release);
    }

    BlockLibrary::TileAlpha BlockLibrary::AlphaOf(int tile) const {
        if (tile <= 0)
            return {};
        struct Snapshot {
            const BlockLibrary* owner = nullptr;
            uint64_t version = 0;
            std::vector<TileAlpha> data;
        };
        thread_local Snapshot snap;
        const uint64_t current = alphaVersion.load(std::memory_order_acquire);
        if (snap.owner != this || snap.version != current) {
            std::shared_lock alphaLock(alphaMutex);
            snap.data = alphaByTile;
            snap.version = alphaVersion.load(std::memory_order_relaxed);
            snap.owner = this;
        }
        return static_cast<size_t>(tile) < snap.data.size() ? snap.data[static_cast<size_t>(tile)] : TileAlpha{};
    }

    float BlockLibrary::SaturationOf(int tile) const {
        return AlphaOf(tile).saturation;
    }

    std::string BlockLibrary::TilePathOf(int tile) const {
        auto it = pathByTile.find(tile);
        return it == pathByTile.end() ? std::string() : it->second;
    }

    void BlockLibrary::PollAnimation(uint64_t step,
                                     const std::function<void(int tile, const uint8_t* rgba)>& emit) {
        if (!ready || animByTile.empty())
            return;
        const size_t frameBytes = static_cast<size_t>(tilePx) * tilePx * 4;
        for (auto& [tile, src] : animByTile) {
            if (src.frames <= 1)
                continue;
            emit(tile, src.pixels.data() + frameBytes * static_cast<size_t>(step % (uint64_t)src.frames));
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
        ClearAlpha();
        overlayMaskTiles.clear();
        pathByTile.clear();
        glassTiles.clear();
        std::ranges::fill(models.tiles, 0);
        std::ranges::fill(localLook, nullptr);
    }

    namespace {
        using Tint = std::array<float, 3>;

        struct VanillaDefinitions {
            std::optional<json::Document> blocksDocument, terrainDocument;
            json::View blocks, terrainData;
            std::unordered_map<std::string, std::string> flipbooks;

            bool Load(const std::string& vanillaRpRoot, bool prefetch,
                      const std::function<void(const std::string&)>& prefetchTextures) {
                const std::string blocksText = file::Read(vanillaRpRoot + "/blocks.json");
                const std::string terrainText = file::Read(vanillaRpRoot + "/textures/terrain_texture.json");
                if (prefetch)
                    prefetchTextures(terrainText);
                if (blocksText.empty() || terrainText.empty()) {
                    Log("blocks", "cannot read blocks.json/terrain_texture.json under {}", vanillaRpRoot);
                    return false;
                }
                blocksDocument = json::Document::Parse(blocksText, true);
                terrainDocument = json::Document::Parse(terrainText, true);
                if (!blocksDocument || !terrainDocument) {
                    Log("blocks", "cannot parse vanilla blocks.json/terrain_texture.json");
                    return false;
                }
                blocks = blocksDocument->root();
                const json::View terrain = terrainDocument->root();
                terrainData = terrain.contains("texture_data") ? terrain["texture_data"] : terrain;
                LoadFlipbooks(vanillaRpRoot);
                return true;
            }

        private:
            void LoadFlipbooks(const std::string& vanillaRpRoot) {
                const std::string text = file::Read(vanillaRpRoot + "/textures/flipbook_textures.json");
                if (text.empty())
                    return;
                if (const auto doc = json::Document::Parse(text, true))
                    for (const json::View e : doc->root().items())
                        if (e.isObject() && e["atlas_tile"].isString() && e["flipbook_texture"].isString())
                            flipbooks[e["atlas_tile"].str()] = e["flipbook_texture"].str();
            }
        };

        struct LocalDefinitions {
            std::optional<json::Document> document;
            json::View overrides;
            std::unordered_map<std::string, std::string> aliases = {{"iron_chain", "chain"}};
            std::unordered_map<std::string, Tint> tints;
            std::vector<std::string> flatContains;

            static std::optional<json::Document> ReadSeeded(const std::string& dir, const char* resourcePath) {
                const std::string path = dir + "/" + std::filesystem::path(resourcePath).filename().string();
                if (file::Read(path).empty())
                    SeedFromExeDir(resourcePath, path);
                const std::string text = file::Read(path);
                if (text.empty())
                    return std::nullopt;
                auto parsed = json::Document::Parse(text, true);
                if (!parsed)
                    Log("blocks", "ignoring unreadable {}", path);
                return parsed;
            }

            void Load(const std::string& perlinPackDir) {
                document = ReadSeeded(perlinPackDir, resources::path::kBlockDefaults);
                const json::View root = document ? document->root() : json::View();
                for (const auto [k, v] : root["aliases"].members())
                    aliases[k] = v.str();
                for (const auto [k, v] : root["tints"].members())
                    if (v.isArray() && v.size() >= 3)
                        tints[k] = {v[0].real(), v[1].real(), v[2].real()};
                overrides = root["blocks"].isObject() ? root["blocks"] : root;
                if (const auto shapes = ReadSeeded(perlinPackDir, resources::path::kBlockShapes))
                    for (const json::View e : shapes->root()["flat_contains"].items())
                        if (e.isString())
                            flatContains.push_back(e.str());
            }
        };

        std::string FaceTexture(json::View e, const char* face) {
            if (e.isString())
                return e.str();
            if (!e.isObject())
                return {};
            if (const json::View f = e[face]; f.isString())
                return f.str();
            if (!std::strcmp(face, "side")) {
                for (const char* key : {"side", "east", "west", "north", "south"})
                    if (const json::View s = e[key]; s.isString())
                        return s.str();
            }
            if (!std::strcmp(face, "up") || !std::strcmp(face, "top"))
                if (const json::View t = e["top"]; t.isString())
                    return t.str();
            return {};
        }

        struct FaceNames {
            std::string top, side, bottom;
            bool Empty() const { return top.empty() && side.empty() && bottom.empty(); }
            bool PerFace() const { return !(top == side && side == bottom); }
        };

        FaceNames VanillaFaces(json::View vit) {
            std::string top, side, bottom;
            if (vit.isObject() && vit.contains("textures")) {
                const json::View t = vit["textures"];
                top = FaceTexture(t, "up");
                bottom = FaceTexture(t, "down");
                side = FaceTexture(t, "side");
                if (side.empty())
                    side = top;
                if (bottom.empty())
                    bottom = top;
                if (top.empty())
                    top = side;
            }
            return {top, side, bottom};
        }

        bool WaterNamed(const std::string& t) {
            return t.contains("still_water") || t.contains("water_flow") || t.contains("flowing_water") ||
                   t.contains("water_still");
        }
    }

    class TextureResolver {
    public:
        TextureResolver(BlockLibrary& library, const std::string& vanillaRoot,
                        const VanillaDefinitions& vanilla)
            : lib(library), root(vanillaRoot), vanilla(vanilla) {}

        int Resolve(const std::string& texName);

    private:
        int Fetch(const std::string& path) { return lib.FetchTile(path, root); }
        int ResolveUnlisted(const std::string& texName);
        int ResolveListed(const json::View& entry);
        void NoteOverlay(const json::View& e, int tile);
        void RememberBaseTint(const std::string& texName, const json::View& entry);

        BlockLibrary& lib;
        const std::string& root;
        const VanillaDefinitions& vanilla;
    };

    class AppearanceBuilder {
    public:
        AppearanceBuilder(BlockLibrary& library, TextureResolver& textures, const VanillaDefinitions& vanilla,
                          const LocalDefinitions& local)
            : lib(library), textures(textures), vanilla(vanilla), local(local) {}

        std::optional<BlockAppearance> Build(const std::string& name) const;

    private:
        struct VanillaFlags {
            bool carried = false, isotropic = false, ambientOcclusion = false;
        };

        std::string VanillaKey(const std::string& name, const std::string& bare) const;
        FaceNames ChooseFaceNames(const json::View& ov, const FaceNames& vanillaFaces) const;
        void ResolveFaces(BlockAppearance& app, const FaceNames& names, const std::string& bare,
                          bool hasOverride) const;
        void ApplyTints(BlockAppearance& app, const json::View& ov, const FaceNames& names) const;
        const Tint* FindTint(const std::string& tex) const;
        static void ApplyCarriedTexture(BlockAppearance& app, const json::View& vit, const FaceNames& names,
                                        const VanillaFlags& flags);
        BlockAppearance::Shape ChooseShape(const BlockAppearance& app, const std::string& name,
                                           const std::string& bare, const FaceNames& names,
                                           const VanillaFlags& flags) const;
        static void ApplyCutoutOverride(BlockAppearance& app, const json::View& ov);

        BlockLibrary& lib;
        TextureResolver& textures;
        const VanillaDefinitions& vanilla;
        const LocalDefinitions& local;
    };

    void TextureResolver::NoteOverlay(const json::View& e, int tile) {
        if (tile && e.isObject() && e["overlay_color"].isString())
            lib.overlayMaskTiles.insert(tile);
    }

    void TextureResolver::RememberBaseTint(const std::string& texName, const json::View& entry) {
        const json::View tintColor = entry["tint_color"];
        if (!tintColor.isString())
            return;
        const std::string hex = tintColor.str();
        unsigned r = 255, g = 255, b = 255;
        if (hex.size() == 7 && hex[0] == '#' && sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) == 3)
            lib.baseTints[texName] = {static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                                      static_cast<float>(b) / 255.0f};
    }

    int TextureResolver::ResolveUnlisted(const std::string& texName) {
        int t = Fetch(texName);
        if (t == 0 && !texName.contains('/'))
            t = Fetch(std::string("textures/blocks/") + texName);
        return t;
    }

    int TextureResolver::ResolveListed(const json::View& entry) {
        json::View list;
        if (entry.isObject() && entry.contains("textures")) {
            const json::View tx = entry["textures"];
            if (tx.isObject() && tx["path"].isString()) {
                const int t = Fetch(tx["path"].str());
                NoteOverlay(tx, t);
                return t;
            }
            if (tx.isString())
                return Fetch(tx.str());
            list = tx;
        } else if (entry.isArray()) {
            list = entry;
        }
        if (list.empty())
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
            const int tile = Fetch(path);
            NoteOverlay(e, tile);
            if (tile != 0 && !first)
                first = tile;
        }
        return first;
    }

    int TextureResolver::Resolve(const std::string& texName) {
        if (texName.starts_with("path:"))
            return Fetch(texName.substr(5));
        if (const auto fb = vanilla.flipbooks.find(texName); fb != vanilla.flipbooks.end())
            if (const int t = Fetch(fb->second))
                return t;
        if (!vanilla.terrainData.contains(texName))
            return ResolveUnlisted(texName);
        const json::View entry = vanilla.terrainData[texName];
        RememberBaseTint(texName, entry);
        if (entry.isString())
            return Fetch(entry.str());
        return ResolveListed(entry);
    }

    std::string AppearanceBuilder::VanillaKey(const std::string& name, const std::string& bare) const {
        const json::View& blocks = vanilla.blocks;
        const auto al = local.aliases.find(bare);
        std::string vkey = al == local.aliases.end() ? bare : al->second;
        if (!blocks.contains(vkey) && blocks.contains(bare))
            vkey = bare;
        if (!blocks.contains(vkey))
            for (const auto& [current, previous] : local.aliases)
                if (previous == bare && blocks.contains(current)) {
                    vkey = current;
                    break;
                }
        if (!blocks.contains(vkey)) {
            const std::string rpKey = BlockPalette::Instance().ResourcePackKey(name);
            if (!rpKey.empty() && blocks.contains(rpKey))
                vkey = rpKey;
        }
        return vkey;
    }

    FaceNames AppearanceBuilder::ChooseFaceNames(const json::View& ov, const FaceNames& v) const {
        const auto pick = [&](const char* key, const std::string& vanillaName) {
            return ov[key].isString() ? ov[key].str() : vanillaName;
        };
        const std::string all = ov["texture"].str();
        if (!all.empty())
            return {all, all, all};
        return {pick("top", v.top), pick("side", v.side.empty() ? v.top : v.side),
                pick("bottom", v.bottom.empty() ? v.top : v.bottom)};
    }

    void AppearanceBuilder::ResolveFaces(BlockAppearance& app, const FaceNames& names,
                                         const std::string& bare, bool hasOverride) const {
        app.top = names.top.empty() ? 0 : textures.Resolve(names.top);
        app.side = names.side.empty() ? app.top : textures.Resolve(names.side);
        app.bottom = names.bottom.empty() ? app.top : textures.Resolve(names.bottom);
        app.varTop.clear();
        app.varSide.clear();
        app.varBottom.clear();
        app.texTop = names.top;
        app.texSide = names.side;
        app.texBottom = names.bottom;
        if (bare == "kelp" && !hasOverride)
            for (const char* texture : {"kelp_a", "kelp_b", "kelp_c", "kelp_d"})
                app.varTop.push_back(textures.Resolve(texture));
    }

    const Tint* AppearanceBuilder::FindTint(const std::string& tex) const {
        if (const auto base = lib.baseTints.find(tex); base != lib.baseTints.end())
            return &base->second;
        if (const auto tint = local.tints.find(tex); tint != local.tints.end())
            return &tint->second;
        return nullptr;
    }

    void AppearanceBuilder::ApplyTints(BlockAppearance& app, const json::View& ov,
                                       const FaceNames& names) const {
        if (ov["tint_faces"].isArray()) {
            const auto has = [&](const char* f) {
                for (const json::View e : ov["tint_faces"].items())
                    if (e.isString() && e.str() == f)
                        return true;
                return false;
            };
            if (has("top") || has("side") || has("bottom")) {
                Tint colour = {1, 1, 1};
                if (ov["tint"].isArray() && ov["tint"].size() >= 3)
                    colour = {ov["tint"][0].real(), ov["tint"][1].real(), ov["tint"][2].real()};
                else if (const Tint* found = FindTint(names.top))
                    colour = *found;
                app.tintR = colour[0];
                app.tintG = colour[1];
                app.tintB = colour[2];
                app.tintTop = has("top");
                app.tintSide = has("side");
                app.tintBottom = has("bottom");
            }
            if (ov["cutout"].isBool())
                app.cutout = ov["cutout"].boolean();
            return;
        }
        const Tint* top = FindTint(names.top);
        const Tint* side = FindTint(names.side);
        const Tint* bottom = FindTint(names.bottom);
        if (!top && !side && !bottom)
            return;
        const Tint& colour = top ? *top : (side ? *side : *bottom);
        app.tintR = colour[0];
        app.tintG = colour[1];
        app.tintB = colour[2];
        app.tintTop = top != nullptr;
        app.tintSide = side != nullptr;
        app.tintBottom = bottom != nullptr;
    }

    void AppearanceBuilder::ApplyCarriedTexture(BlockAppearance& app, const json::View& vit,
                                                const FaceNames& names, const VanillaFlags& flags) {
        if (!flags.carried || flags.isotropic || flags.ambientOcclusion || names.PerFace())
            return;
        const json::View ct = vit["carried_textures"];
        if (ct.isString()) {
            app.carriedTex = ct.str();
            app.useCarriedColor = app.carriedTex != names.top;
        } else if (ct["side"].isString()) {
            app.carriedTex = ct["side"].str();
            app.useCarriedColor = app.carriedTex != names.side;
        }
    }

    BlockAppearance::Shape AppearanceBuilder::ChooseShape(const BlockAppearance& app, const std::string& name,
                                                          const std::string& bare, const FaceNames& names,
                                                          const VanillaFlags& flags) const {
        using Shape = BlockAppearance::Shape;
        BlockPalette& palette = BlockPalette::Instance();
        const bool perFace = names.PerFace();
        bool flat = palette.BlockHasProperty(name, "height");
        if (!flat && !perFace && flags.carried && palette.BlockHasProperty(name, "growth"))
            flat = lib.AlphaOf(app.top).holes;
        if (!flat)
            flat = std::ranges::any_of(local.flatContains, [&](const std::string& sub) {
                return !sub.empty() && bare.contains(sub);
            });
        const bool hasUpper = palette.BlockHasProperty(name, "upper_block_bit");
        const bool hasHalf = palette.BlockHasProperty(name, "minecraft:vertical_half");
        if ((app.top | app.side | app.bottom) == 0)
            return Shape::Cube;
        if (flat)
            return Shape::Flat;
        if (hasHalf || hasUpper)
            return hasUpper ? Shape::Cross : Shape::Cube;
        if (!perFace && (palette.BlockHasProperty(name, "facing_direction") ||
                         palette.BlockHasProperty(name, "vine_direction_bits")))
            return lib.AlphaOf(app.top).holes ? Shape::Wall : Shape::Cube;
        if (!perFace) {
            const BlockLibrary::TileAlpha a = lib.AlphaOf(app.top);
            if (!a.holes || (flags.carried && (flags.isotropic || flags.ambientOcclusion)) || a.frame >= 0.5f)
                return Shape::Cube;
            return Shape::Cross;
        }
        const BlockLibrary::TileAlpha at = lib.AlphaOf(app.top), as = lib.AlphaOf(app.side),
                                      ab = lib.AlphaOf(app.bottom);
        const bool anySolid = (app.top && !at.holes) || (app.side && !as.holes) || (app.bottom && !ab.holes);
        return anySolid ? Shape::Cube : Shape::Cross;
    }

    bool BlockLibrary::SplitOverlayTile(BlockAppearance& app) {
        app.overlayFrac = 0;
        if (!app.side || !overlayMaskTiles.contains(app.side) || nextTile + 2 > MaxTiles())
            return false;
        const int src = app.side;
        const size_t px = static_cast<size_t>(tilePx) * tilePx;
        bool anyMask = false, anyClear = false;
        for (size_t i = 0; i < px; i++) {
            const bool opaque = atlas[(static_cast<size_t>(src) * px + i) * 4 + 3] >= 128;
            anyMask = anyMask || opaque;
            anyClear = anyClear || !opaque;
        }
        if (!anyMask || !anyClear)
            return false;
        const int maskTile = NewTile();
        const int baseTile = NewTile();
        for (size_t i = 0; i < px; i++) {
            const uint8_t* s = atlas.data() + (static_cast<size_t>(src) * px + i) * 4;
            uint8_t* o = atlas.data() + (static_cast<size_t>(maskTile) * px + i) * 4;
            uint8_t* b = atlas.data() + (static_cast<size_t>(baseTile) * px + i) * 4;
            o[0] = b[0] = s[0];
            o[1] = b[1] = s[1];
            o[2] = b[2] = s[2];
            o[3] = s[3] >= 128 ? 255 : 0;
            b[3] = 255;
        }
        TileAlpha overlayAlpha;
        overlayAlpha.holes = true;
        SetAlpha(maskTile, overlayAlpha);
        pathByTile[maskTile] = std::string("__grass_overlay_") + std::to_string(src);
        SetAlpha(baseTile, TileAlpha{});
        pathByTile[baseTile] = std::string("__grass_base_") + std::to_string(src);
        app.overlayTile = maskTile;
        app.overlayFrac = 1.0f;
        app.side = baseTile;
        app.varSide.clear();
        return true;
    }

    void AppearanceBuilder::ApplyCutoutOverride(BlockAppearance& app, const json::View& ov) {
        using Shape = BlockAppearance::Shape;
        if (!ov["cutout"].isBool())
            return;
        if (ov["cutout"].boolean()) {
            if (app.shape != Shape::Flat && app.shape != Shape::Wall)
                app.shape = Shape::Cross;
            app.cutout = true;
        } else {
            app.cutout = false;
            if (app.shape == Shape::Cross || app.shape == Shape::Wall)
                app.shape = Shape::Cube;
        }
    }

    std::optional<BlockAppearance> AppearanceBuilder::Build(const std::string& name) const {
        using Shape = BlockAppearance::Shape;
        const std::string bare = name.starts_with("minecraft:") ? name.substr(10) : name;
        const json::View vit = vanilla.blocks[VanillaKey(name, bare)];
        VanillaFlags flags;
        if (vit.isObject()) {
            flags.carried = vit.contains("carried_textures");
            flags.isotropic = vit.contains("isotropic");
            flags.ambientOcclusion = vit.contains("ambient_occlusion_exponent");
        }
        const json::View ov = local.overrides[name];
        const FaceNames names = ChooseFaceNames(ov, VanillaFaces(vit));
        if (names.Empty())
            return std::nullopt;

        BlockAppearance app;
        ResolveFaces(app, names, bare, ov.isObject());
        ApplyTints(app, ov, names);
        ApplyCarriedTexture(app, vit, names, flags);
        app.shape = ChooseShape(app, name, bare, names, flags);
        if (app.shape == Shape::Cross || app.shape == Shape::Wall)
            app.cutout = true;
        lib.SplitOverlayTile(app);
        ApplyCutoutOverride(app, ov);
        return app;
    }

    namespace {
        struct AtlasAverage {
            double r = 0, g = 0, b = 0;
            int count = 0;
        };

        AtlasAverage AverageOpaque(const std::vector<uint8_t>& atlas, int tile, int tilePx) {
            AtlasAverage sum;
            const size_t base = static_cast<size_t>(tile) * tilePx * tilePx;
            for (int y = 0; y < tilePx; y++)
                for (int x = 0; x < tilePx; x++) {
                    const uint8_t* d = atlas.data() + (base + static_cast<size_t>(y) * tilePx + x) * 4;
                    if (d[3] < 128)
                        continue;
                    sum.r += d[0];
                    sum.g += d[1];
                    sum.b += d[2];
                    sum.count++;
                }
            return sum;
        }

        void WriteBlockLog(const std::vector<std::string>& allNames,
                           const std::unordered_map<std::string, BlockAppearance>& appearances, int tiles,
                           const std::function<std::string(int)>& tilePath) {
            FILE* f = fopen((paths::DataDir() + "/blocks.log").c_str(), "w");
            if (!f)
                return;
            fprintf(f, "tiles=%d\n", tiles);
            for (const std::string& name : allNames) {
                const auto ait = appearances.find(name);
                if (ait == appearances.end()) {
                    fprintf(f, "  %s MISSING\n", name.c_str());
                    continue;
                }
                const BlockAppearance& a = ait->second;
                const char* sh = a.shape == BlockAppearance::Shape::Cross  ? "cross"
                                 : a.shape == BlockAppearance::Shape::Flat ? "flat"
                                 : a.shape == BlockAppearance::Shape::Wall ? "wall"
                                                                           : "cube";
                fprintf(f, "  %s top=%d side=%d bottom=%d tint=%d shape=%s%s%s\n", name.c_str(), a.top,
                        a.side, a.bottom, static_cast<int>(a.tintTop || a.tintSide), sh,
                        a.hasCarriedRGB ? " carriedRGB" : "", a.overlayFrac > 0 ? " overlay" : "");
                fprintf(f, "    tex top=%s side=%s bottom=%s%s%s\n", a.texTop.c_str(), a.texSide.c_str(),
                        a.texBottom.c_str(), a.carriedTex.empty() ? "" : " carried=",
                        a.carriedTex.empty() ? "" : a.carriedTex.c_str());
                fprintf(f, "    path top=%s side=%s bottom=%s\n", tilePath(a.top).c_str(),
                        tilePath(a.side).c_str(), tilePath(a.bottom).c_str());
            }
            fclose(f);
        }
    }

    bool BlockLibrary::EnsureNameTables(const std::string& vanillaRpRoot) {
        BlockPalette& palette = BlockPalette::Instance();
        if (!palette.NamesLoaded()) {
            const std::string defs = vanillaRpRoot + "/../metadata/vanilladata_modules/mojang-blocks.json";
            if (!palette.LoadVanillaDefinitions(defs)) {
                Log("blocks", "cannot load {}", defs);
                return false;
            }
        }
        if (!palette.SequenceLoaded()) {
            std::string table;
            if (resources::Read(resources::path::kBlockStates, table))
                palette.LoadServerStatesText(table, "block_states.json");
            else
                Log("blocks", "block_states.json is not bundled: sequential-id servers will read as air");
            if (std::string versions; resources::Read(resources::path::kBlockVersions, versions))
                palette.LoadVersionDeltas(versions);
        }
        return true;
    }

    void BlockLibrary::LoadBlockModels(const std::string& perlinPackDir, const std::string& vanillaRpRoot) {
        if (!modelsTried) {
            modelsTried = true;
            std::string bundled;
            if (!models.Load(perlinPackDir + "/block_models.bin") &&
                !(resources::Read(resources::path::kBlockModels, bundled) && models.LoadFromMemory(bundled)))
                Log("models", "block_models.bin not found; using hand-built block shapes");
        }
        if (!models.Loaded())
            return;
        const auto& names = models.Textures();
        for (size_t i = 0; i < names.size(); i++)
            if (models.tiles[i] == 0)
                models.tiles[i] = FetchTile(names[i], vanillaRpRoot);
    }

    void BlockLibrary::ScanBlockGeometry(const std::string& perlinPackDir, const std::string& vanillaRpRoot) {
        if (geometryTried)
            return;
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

    void BlockLibrary::AverageCarriedColours(TextureResolver& textures) {
        for (auto& [name, app] : appearances) {
            if (app.carriedTex.empty())
                continue;
            const int tile = textures.Resolve(app.carriedTex);
            if (tile <= 0)
                continue;
            const AtlasAverage avg = AverageOpaque(atlas, tile, tilePx);
            if (avg.count == 0)
                continue;
            app.hasCarriedRGB = true;
            app.carriedR = static_cast<float>(avg.r / avg.count / 255.0);
            app.carriedG = static_cast<float>(avg.g / avg.count / 255.0);
            app.carriedB = static_cast<float>(avg.b / avg.count / 255.0);
        }
    }

    void BlockLibrary::TintWaterAppearances(const std::string& vanillaRpRoot) {
        const std::array<float, 3> water = ResolveDefaultWater(vanillaRpRoot);
        for (auto& [name, app] : appearances) {
            if (!WaterNamed(app.texTop) && !WaterNamed(app.texSide))
                continue;
            app.tintR = water[0];
            app.tintG = water[1];
            app.tintB = water[2];
            app.tintTop = app.tintSide = app.tintBottom = true;
        }
    }

    bool BlockLibrary::EnsureLoaded(const std::string& perlinPackDir, const std::string& vanillaRpRoot) {
        if (ready && complete)
            return true;
        fetchFails = 0;
        PrefetchGuard prefetchGuard;

        VanillaDefinitions vanilla;
        if (!vanilla.Load(vanillaRpRoot, HardwareThreads() >= 4, [&](const std::string& terrainText) {
                PrefetchTextures(vanillaRpRoot, terrainText);
            }))
            return false;
        if (!EnsureNameTables(vanillaRpRoot))
            return false;
        const std::vector<std::string> allNames = BlockPalette::Instance().AllNames();

        LocalDefinitions local;
        local.Load(perlinPackDir);
        if (atlas.empty()) {
            nextTile = 1;
            synthTiles.clear();
            atlas.assign(static_cast<size_t>(tilePx) * tilePx * 4, 255);
        }

        TextureResolver textures(*this, vanillaRpRoot, vanilla);
        const AppearanceBuilder builder(*this, textures, vanilla, local);
        appearances["minecraft:air"] = BlockAppearance{};
        int noTexture = 0;
        for (const std::string& name : allNames) {
            if (name == "minecraft:air")
                continue;
            try {
                if (auto app = builder.Build(name))
                    appearances[name] = std::move(*app);
                else
                    noTexture++;
            } catch (const std::exception& e) {
                printf("[blocks] bad entry for %s: %s\n", name.c_str(), e.what());
            }
        }

        LoadBlockModels(perlinPackDir, vanillaRpRoot);
        ScanBlockGeometry(perlinPackDir, vanillaRpRoot);
        if (fetchFails > 0)
            Log("blocks", "{} textures missing; continuing without them", fetchFails);
        lastFileMisses = 0;
        stallStreak = 0;
        BuildGlassVariants();
        complete = true;
        ready = true;
        AverageCarriedColours(textures);
        TintWaterAppearances(vanillaRpRoot);
        Log("blocks", "appearances: {} names, {} without vanilla textures, {} atlas tiles", allNames.size(),
            noTexture, nextTile);
        WriteBlockLog(allNames, appearances, nextTile, [this](int tile) { return TilePathOf(tile); });
        return true;
    }

    namespace {
        std::string AfterColon(const std::string& s) {
            const size_t c = s.find(':');
            return c == std::string::npos ? s : s.substr(c + 1);
        }

        using PathMap = std::unordered_map<std::string, std::string>;

        void ReadServerTexturePaths(const std::vector<std::string>& dirs, PathMap& pathByKey) {
            for (const std::string& dir : dirs) {
                std::string text;
                {
                    std::string contents;
                    if (!file::Read(dir + "/textures/terrain_texture.json", contents))
                        continue;
                    text = contents;
                }
                json::Value doc;
                std::string error;
                if (!json::Value::Parse(text, doc, error))
                    continue;
                const json::Value* data = doc.Find("texture_data");
                if (!data || !data->IsObject())
                    continue;
                for (const auto& [key, value] : data->object) {
                    const json::Value* tex = value.Find("textures");
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
        }

        void ReadVanillaTexturePaths(const std::string& vanillaRpRoot, PathMap& vanillaPathByKey) {
            {
                std::string terrainText;
                if (file::Read(vanillaRpRoot + "/textures/terrain_texture.json", terrainText)) {
                    if (const auto doc = json::Document::Parse(terrainText, true))
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
        }

        void ReadLegacyFaces(const std::vector<std::string>& serverDirs,
                             std::unordered_map<std::string, std::map<std::string, std::string>>& legacyFaces) {
            for (const std::string& dir : serverDirs) {
                std::string contents;
                if (!file::Read(dir + "/blocks.json", contents))
                    continue;
                json::Value doc;
                std::string error;
                if (!json::Value::Parse(contents, doc, error) || !doc.IsObject())
                    continue;
                for (const auto& [blockName, entry] : doc.object) {
                    if (!entry.IsObject())
                        continue;
                    const json::Value* tex = entry.Find("textures");
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
        }

        void ScanTextureStems(const std::vector<std::string>& serverDirs, PathMap& stems) {
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
        }
    }

    void BlockLibrary::BuildCustomContext(const std::string& vanillaRpRoot, const std::vector<std::string>& serverDirs,
                                          const std::string& ctxKey) {
        customCtx = CustomContext{};
        customCtx.key = ctxKey;
        PrefetchServerTextures(serverDirs);
        ReadServerTexturePaths(serverDirs, customCtx.pathByKey);
        ReadVanillaTexturePaths(vanillaRpRoot, customCtx.vanillaPathByKey);
        ReadLegacyFaces(serverDirs, customCtx.legacyFaces);
        ScanTextureStems(serverDirs, customCtx.stems);
    }

    int BlockLibrary::ResolveCustomFace(const CustomBlockDef& def,
                                        const std::map<std::string, std::string>& faceTex,
                                        std::initializer_list<const char*> faces, std::string& pathOut,
                                        const std::string& vanillaRpRoot) {
        for (const char* face : faces) {
            const auto f = faceTex.find(face);
            if (f == faceTex.end())
                continue;
            const auto p = customCtx.pathByKey.find(f->second);
            const auto v = customCtx.vanillaPathByKey.find(f->second);
            const std::string path = p != customCtx.pathByKey.end()          ? p->second
                                     : v != customCtx.vanillaPathByKey.end() ? v->second
                                                                             : "textures/blocks/" + f->second;
            pathOut = path;
            return FetchOrSynth(path, vanillaRpRoot, def.name + "/" + f->second, customCtx.stems);
        }
        return -1;
    }

    const BlockAppearance* BlockLibrary::FindVanillaBaseFor(const std::string& blockName) const {
        const std::string shortName = AfterColon(blockName);
        static const char* const kSuffixes[] = {"_double_slab", "_stairs", "_slab", "_wall", "_fence", "_pillar"};
        for (const char* suffix : kSuffixes) {
            const size_t n = std::strlen(suffix);
            if (shortName.size() <= n || shortName.compare(shortName.size() - n, n, suffix) != 0)
                continue;
            const std::string stem = shortName.substr(0, shortName.size() - n);
            for (const std::string& candidate : {stem, stem + "_planks", stem + "s", stem + "_block"}) {
                const auto found = appearances.find("minecraft:" + candidate);
                if (found != appearances.end() && found->second.shape == BlockAppearance::Shape::Cube &&
                    (found->second.top | found->second.side | found->second.bottom) != 0)
                    return &found->second;
            }
            break;
        }
        return nullptr;
    }

    void BlockLibrary::StoreAppearance(const std::string& blockName, BlockAppearance appearance) {
        std::lock_guard<std::mutex> lock(lookMutex);
        appearances[blockName] = std::move(appearance);
    }

    void BlockLibrary::AddCustomAppearance(const CustomBlockDef& def, const std::string& vanillaRpRoot) {
        std::map<std::string, std::string> faceTex = def.faceTexture;
        if (faceTex.empty())
            if (const auto legacy = customCtx.legacyFaces.find(def.name); legacy != customCtx.legacyFaces.end())
                faceTex = legacy->second;
        const auto resolve = [&](std::initializer_list<const char*> faces, std::string& pathOut) {
            return ResolveCustomFace(def, faceTex, faces, pathOut, vanillaRpRoot);
        };
        std::string pTop, pSide, pBottom;
        int top = resolve({"up", "*"}, pTop);
        int side = resolve({"side", "north", "south", "east", "west", "*"}, pSide);
        int bottom = resolve({"down", "*"}, pBottom);
        if (top < 0 && side < 0 && bottom < 0) {
            if (const BlockAppearance* base = FindVanillaBaseFor(def.name)) {
                BlockAppearance copy = *base;
                copy.cutout = def.cutout || base->cutout;
                StoreAppearance(def.name, std::move(copy));
                return;
            }
            const std::string guess = "textures/blocks/" + AfterColon(def.name);
            side = FetchOrSynth(guess, vanillaRpRoot, def.name, customCtx.stems);
            pSide = guess;
        }
        if (side < 0)
            side = top >= 0 ? top : bottom;
        if (top < 0)
            top = side;
        if (bottom < 0)
            bottom = side;
        BlockAppearance a;
        static constexpr const char* kFaces[4] = {"east", "west", "south", "north"};
        for (int i = 0; i < 4; i++) {
            std::string unusedPath;
            if (faceTex.contains(kFaces[i])) {
                const int t = resolve({kFaces[i]}, unusedPath);
                if (t > 0 && t != side)
                    a.sideTile[i] = t;
            }
        }
        a.top = top;
        a.side = side;
        a.bottom = bottom;
        a.texTop = pTop.empty() ? pSide : pTop;
        a.texSide = pSide;
        a.texBottom = pBottom.empty() ? pSide : pBottom;
        a.cutout = def.cutout;
        StoreAppearance(def.name, std::move(a));
    }

    bool BlockLibrary::ApplyCustom(const std::string& vanillaRpRoot, double budgetMs) {
        if (!ServerBlocks::Instance().Pending()) {

            ServerPrefetchCache().clear();
            return false;
        }
        std::vector<CustomBlockDef> defs = ServerBlocks::Instance().Take();

        const std::vector<std::string> serverDirs = ServerPacks::Instance().Directories();
        std::string ctxKey = vanillaRpRoot;
        for (const std::string& dir : serverDirs)
            ctxKey += "|" + dir;
        if (customCtx.key != ctxKey)
            BuildCustomContext(vanillaRpRoot, serverDirs, ctxKey);
        const auto& stems = customCtx.stems;
        bool any = false;
        const BlockGeometry::FetchFn fetchGeometry = [this, &stems](const std::string& p,
                                                                    const std::string& root) {
            fetchingGeometry = true;
            int tile = FetchTile(p, root);
            if (tile == 0) {
                const size_t hash = p.find('#');
                const std::string base = p.substr(0, hash), cell = hash == std::string::npos ? "" : p.substr(hash);
                const size_t slash = base.find_last_of('/');
                const auto found = stems.find(slash == std::string::npos ? base : base.substr(slash + 1));
                if (found != stems.end())
                    tile = FetchTile(found->second + cell, root);
            }
            fetchingGeometry = false;
            return tile;
        };

        const auto isSlabSheet = [](const CustomBlockDef& d) {
            const std::string shortName = AfterColon(d.name);
            return d.json.contains(".slab.") || (shortName.size() > 5 && shortName.ends_with("_slab"));
        };
        const auto buildStart = std::chrono::steady_clock::now();
        const size_t slabCount =
            static_cast<size_t>(std::stable_partition(defs.begin(), defs.end(), isSlabSheet) - defs.begin());
        for (size_t defIndex = 0; defIndex < defs.size(); defIndex++) {

            if (budgetMs > 0 && defIndex > 0 &&
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart).count() >
                    budgetMs) {
                ServerBlocks::Instance().Return(
                    std::vector<CustomBlockDef>(defs.begin() + static_cast<std::ptrdiff_t>(defIndex), defs.end()));
                defs.resize(defIndex);
                break;
            }
            const CustomBlockDef& def = defs[defIndex];
            if (defIndex == slabCount && slabCount > 0)
                geometry.CutCustomSheets(vanillaRpRoot, fetchGeometry);
            if (!def.json.empty())
                geometry.AddCustom(def.name, def.json, serverDirs, vanillaRpRoot, fetchGeometry);
            AddCustomAppearance(def, vanillaRpRoot);
            any = true;
        }
        for (const CustomBlockDef& def : defs)
            ServerBlocks::Instance().MarkBuilt(def.name);
        if (!ServerBlocks::Instance().Pending())
            std::unordered_map<std::string, Texture>().swap(ServerPrefetchCache());

        if (any)
            geometry.CutCustomSheets(vanillaRpRoot, fetchGeometry);
        Log("blocks", "custom blocks built: {} definitions, {} atlas tiles in total, {} ms", defs.size(),
            TilesLoaded(),
            static_cast<int>(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart).count()));
        return any;
    }

    const BlockAppearance& BlockLibrary::Look(int32_t localId) const {
        static BlockAppearance fallback;
        if (localId <= 0 || localId >= static_cast<int32_t>(BlockPalette::kMaxLocal))
            return fallback;
        std::lock_guard<std::mutex> lookLock(lookMutex);
        if (localLook.empty())
            localLook.assign(BlockPalette::kMaxLocal, nullptr);
        const BlockAppearance*& cached = localLook[static_cast<size_t>(localId)];
        if (cached)
            return *cached;
        std::string name = BlockPalette::Instance().Name(static_cast<uint16_t>(localId));
        if (name.empty()) {
            if (!unknownLogged[localId]) {
                unknownLogged[localId] = true;
                if (FILE* f = fopen((paths::DataDir() + "/blocks.log").c_str(), "a")) {
                    fprintf(f,
                            "unknown network block id %d (no vanilla definition produces this hash; "
                            "custom block or newer game data than mojang-blocks.json)\n",
                            BlockPalette::Instance().NetworkId(static_cast<uint16_t>(localId)));
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
        std::ranges::fill(localLook, nullptr);
    }

    const BlockAppearance* BlockLibrary::LookByName(const std::string& name) const {
        auto it = appearances.find(name);
        if (it == appearances.end()) {
            if (name.contains(':') && !name.starts_with("minecraft:"))
                ServerBlocks::Instance().Want(name, ServerBlocks::kNoChunk, 0);
            return nullptr;
        }
        return &it->second;
    }

    bool BlockLibrary::TilePixels(int tile, std::vector<uint8_t>& rgba) const {
        const size_t bytes = static_cast<size_t>(tilePx) * tilePx * 4;
        if (tile < 0 || static_cast<size_t>(tile + 1) * bytes > atlas.size())
            return false;
        rgba.assign(atlas.begin() + static_cast<size_t>(tile) * bytes,
                    atlas.begin() + static_cast<size_t>(tile + 1) * bytes);
        return true;
    }

    void BlockLibrary::TileUV(int tile, float& u0, float& v0, float& u1, float& v1) {
        const int tx = tile % kAtlasTiles, ty = tile / kAtlasTiles;
        const float inv = 1.0f / static_cast<float>(kAtlasTiles);
        const float half = 0.5f * inv / static_cast<float>(tilePxValue);
        u0 = tx * inv + half;
        v0 = ty * inv + half;
        u1 = (tx + 1) * inv - half;
        v1 = (ty + 1) * inv - half;
    }

}
