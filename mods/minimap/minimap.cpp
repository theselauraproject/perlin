#include <perlin/plugin.hpp>

#include <mod_settings.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

using namespace perlin;

namespace {
    constexpr int kRadius = 12;
    constexpr int kChunks = kRadius * 2 + 1;
    constexpr int kSlots = kChunks * kChunks;
    constexpr int kMapSize = kChunks * 16;
    constexpr int kTexW = kMapSize;
    constexpr int kWhiteRow = kMapSize;
    constexpr int kTexH = kMapSize + 2;
    constexpr int kIconScale = 8;
    constexpr int kRetryFrames = 20;
    constexpr double kPi = 3.14159265358979323846;
    constexpr int kFetchPerFrame = 6;
    constexpr int kPaintPerFrame = 24;
    constexpr int kPollPerFrame = 4;
    constexpr int64_t kBudgetMs = 2;
    constexpr int64_t kNoSlot = INT64_MIN;
    constexpr std::array<double, 7> kZoomSteps{0.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    constexpr double kZoomScale = 2.0;
    constexpr double kZoomEase = 0.8;
    constexpr double kZoomStepMs = 1000.0 / 60.0;
    constexpr int kSunSteps = 192;

    int64_t ChunkKey(int cx, int cz) {
        return (static_cast<int64_t>(cx) << 32) ^ static_cast<uint32_t>(cz);
    }

    int Wrap(int v) {
        const int m = v % kChunks;
        return m < 0 ? m + kChunks : m;
    }

    int SlotOf(int cx, int cz) {
        return Wrap(cz) * kChunks + Wrap(cx);
    }

    struct Cached {
        uint64_t version = 0;
        int32_t layer = 0;
        bool loaded = false;
        uint64_t fetchedAt = 0;
        std::array<game::MapColumn, 256> columns{};
    };

    struct Slot {
        int64_t owner = kNoSlot;
        uint64_t version = 0;
        int32_t layer = 0;
        bool dirty = true;
    };

    struct Waypoint {
        double x = 0, z = 0;
        uint32_t color = 0;
        bool death = false;
    };

    uint8_t Clamp8(float v) {
        return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
    }

    const std::vector<int>& SpiralOrder() {
        static const std::vector<int> order = [] {
            std::vector<int> o(kSlots);
            for (int i = 0; i < kSlots; i++)
                o[static_cast<size_t>(i)] = i;
            std::sort(o.begin(), o.end(), [](int a, int b) {
                const int ax = a % kChunks - kRadius, az = a / kChunks - kRadius, bx = b % kChunks - kRadius,
                          bz = b / kChunks - kRadius;
                return ax * ax + az * az < bx * bx + bz * bz;
            });
            return o;
        }();
        return order;
    }
}

struct Minimap : Plugin {
    void onEnable(PluginCtx& ctx) override {
        ctx.bind<"toggle">("Toggle minimap", Key::None, this, &Minimap::toggle);
        ctx.bind<"zoom_in">("Zoom minimap in", Key::None, this, &Minimap::zoomIn);
        ctx.bind<"zoom_out">("Zoom minimap out", Key::None, this, &Minimap::zoomOut);
        ctx.bind<"waypoint">("Add waypoint here", Key::B, this, &Minimap::addWaypoint);
        ctx.on_press(settings::clear_waypoints, [this] {
            waypoints.clear();
            saveWaypoints();
        });
        ctx.on_setting_changed([this](const abi::EventSettingChanged&) {
            for (Slot& s : slots)
                s.dirty = true;
            anyDirty = true;
            cursor = 0;
        });
        pixels.assign(static_cast<size_t>(kTexW) * kTexH * 4, 0);
        for (int x = 0; x < kTexW * 2; x++) {
            uint8_t* p = &pixels[(static_cast<size_t>(kWhiteRow) * kTexW + x) * 4];
            p[0] = p[1] = p[2] = p[3] = 255;
        }
        loadWaypoints();
    }

    void onDisable(PluginCtx&) override { (void)gfx::clear("minimap"); }

    void onDeath(const abi::EventText&) {
        if (!settings::death_waypoint)
            return;
        if (const auto p = game::player()) {
            std::erase_if(waypoints, [](const Waypoint& w) { return w.death; });
            waypoints.push_back({p->x, p->z, gfx::rgba8(230, 40, 40), true});
            saveWaypoints();
        }
    }

    void onFrame(const abi::EventFrame&) {
        if (!settings::enabled) {
            hide();
            return;
        }
        const auto player = game::player();
        const auto disp = gfx::display();
        if (!player || !player->connected || !disp || !disp->inGame) {
            hide();
            return;
        }
        if (!texture) {
            auto t = gfx::Texture::create(kTexW, kTexH, pixels, gfx::Filter::Nearest);
            if (!t)
                return;
            texture = std::move(*t);
        }
        if (!iconTexture && iconRetry-- <= 0)
            loadIcon();

        const int dim = static_cast<int>(player->dimension);
        if (dim != dimension) {
            dimension = dim;
            cache.clear();
            for (Slot& s : slots)
                s = Slot{};
            underRoof = roofCandidate = false;
        }
        const int pcx = static_cast<int>(std::floor(player->x / 16.0));
        const int pcz = static_cast<int>(std::floor(player->z / 16.0));
        const int32_t nextLayer = Layer(*player);
        if (pcx != centerX || pcz != centerZ || nextLayer != layer) {
            centerX = pcx;
            centerZ = pcz;
            layer = nextLayer;
            cursor = 0;
        }
        UpdateSun();
        frame++;
        Refresh();
        Upload();
        Draw(*player, *disp);
    }

    void toggle() { (void)settings::enabled.set(!settings::enabled); }
    void zoomIn(bool down) {
        if (!down)
            return;
        const double current = static_cast<double>(settings::zoom);
        for (const double step : kZoomSteps)
            if (step > current + 1e-6) {
                (void)settings::zoom.set(step);
                return;
            }
    }
    void zoomOut(bool down) {
        if (!down)
            return;
        const double current = static_cast<double>(settings::zoom);
        for (auto it = kZoomSteps.rbegin(); it != kZoomSteps.rend(); ++it)
            if (*it < current - 1e-6) {
                (void)settings::zoom.set(*it);
                return;
            }
    }
    void addWaypoint(bool down) {
        if (!down)
            return;
        if (const auto p = game::player()) {
            static const uint32_t colors[] = {gfx::rgba8(80, 200, 255), gfx::rgba8(255, 210, 60), gfx::rgba8(120, 255, 120),
                                              gfx::rgba8(255, 120, 220), gfx::rgba8(255, 255, 255)};
            waypoints.push_back({p->x, p->z, colors[waypoints.size() % 5], false});
            saveWaypoints();
            (void)act::toast("Waypoint added");
        }
    }

private:
    void hide() {
        if (shown) {
            (void)gfx::clear("minimap");
            shown = false;
        }
    }

    int32_t Layer(const abi::PlayerState& p) {
        const int feet = static_cast<int>(std::floor(p.y - p.eyeHeight + 0.01));
        const auto mode = settings::cave_mode.get();
        bool cave = mode == settings::CaveMode::On;
        if (mode == settings::CaveMode::Auto) {
            const int wx = static_cast<int>(std::floor(p.x)), wz = static_cast<int>(std::floor(p.z));
            int seen = 0, roofs = 0;
            for (const auto [ox, oz] : {std::pair{0, 0}, std::pair{3, 0}, std::pair{-3, 0}, std::pair{0, 3}, std::pair{0, -3}}) {
                const int x = wx + ox, z = wz + oz;
                const auto it = cache.find(ChunkKey(x >> 4, z >> 4));
                if (it == cache.end() || !it->second.loaded)
                    continue;
                const game::MapColumn& col = it->second.columns[static_cast<size_t>((z & 15) * 16 + (x & 15))];
                if (!(col.flags & game::kMapLoaded))
                    continue;
                seen++;
                if (col.top >= feet + 3)
                    roofs++;
            }
            if (seen > 0) {
                const bool roof = roofs * 5 >= seen * 4;
                const int64_t now = game::now_ms();
                if (roof != roofCandidate) {
                    roofCandidate = roof;
                    roofSince = now;
                }
                if (roofCandidate != underRoof && now - roofSince >= (roofCandidate ? 600 : 300))
                    underRoof = roofCandidate;
            }
            cave = underRoof;
        }
        if (!cave)
            return game::kMapSurface;
        const int32_t want = feet + 1;
        if (layer != game::kMapSurface && std::abs(want - layer) < 2)
            return layer;
        return want;
    }

    double TargetZoom() const {
        double target = std::clamp(static_cast<double>(settings::zoom), kZoomSteps.front(), kZoomSteps.back());
        if (layer != game::kMapSurface)
            target = std::round(target * static_cast<double>(std::max(1, static_cast<int>(settings::cave_zoom))));
        return std::min(target, kZoomSteps.back());
    }

    double AnimatedZoom() {
        const double target = TargetZoom();
        const int64_t now = game::now_ms();
        if (shownZoom <= 0.0) {
            shownZoom = target;
        } else {
            const double steps = static_cast<double>(std::clamp<int64_t>(now - zoomClock, 0, 250)) / kZoomStepMs;
            double off = target - shownZoom;
            off = std::abs(off) > 0.01 ? off * std::pow(kZoomEase, steps) : 0.0;
            shownZoom = target - off;
        }
        zoomClock = now;
        return shownZoom;
    }

    void UpdateSun() {
        int bucket = -1;
        float lx = -0.55f, ly = 0.75f, lz = -0.37f, strength = 1.0f;
        if (settings::sun_shadows && layer == game::kMapSurface && dimension == 0)
            if (const auto w = game::world()) {
                const int t = static_cast<int>(((w->timeOfDay % 24000) + 24000) % 24000);
                bucket = t * kSunSteps / 24000;
                const double a = (static_cast<double>(bucket) + 0.5) / kSunSteps * 2.0 * kPi;
                double sx = std::cos(a), sy = std::sin(a);
                if (sy < 0.06) {
                    sx = -sx;
                    sy = -sy;
                    strength = 0.55f;
                }
                lx = static_cast<float>(sx);
                ly = static_cast<float>(std::max(sy, 0.06));
                lz = 0.28f;
            }
        if (bucket == sunBucket)
            return;
        sunBucket = bucket;
        const float len = std::sqrt(lx * lx + ly * ly + lz * lz);
        sun = {lx / len, ly / len, lz / len};
        sunStrength = strength;
        for (Slot& slot : slots)
            if (slot.owner != kNoSlot)
                slot.dirty = true;
        anyDirty = true;
    }

    void Refresh() {
        const int64_t started = game::now_ms();
        const auto overBudget = [&] { return game::now_ms() - started >= kBudgetMs; };
        const std::vector<int>& order = SpiralOrder();
        int fetched = 0, painted = 0;
        while (cursor < kSlots) {
            const int i = order[static_cast<size_t>(cursor)];
            const int cx = centerX - kRadius + i % kChunks, cz = centerZ - kRadius + i / kChunks;
            const int64_t key = ChunkKey(cx, cz);
            const auto it = cache.find(key);
            if (it == cache.end() || it->second.layer != layer ||
                (!it->second.loaded && frame - it->second.fetchedAt >= kRetryFrames)) {
                if (fetched >= kFetchPerFrame || overBudget() || !Fetch(cx, cz))
                    return;
                fetched++;
            } else {
                const Slot& s = slots[static_cast<size_t>(SlotOf(cx, cz))];
                if (s.owner != key || s.dirty || s.version != it->second.version || s.layer != layer) {
                    if (painted >= kPaintPerFrame || overBudget())
                        return;
                    Paint(cx, cz);
                    painted++;
                }
            }
            cursor++;
        }
        Poll(order, overBudget);
        Repaint(order, painted, overBudget);
        Trim();
    }

    template <class Budget> void Poll(const std::vector<int>& order, const Budget& overBudget) {
        int retried = 0;
        for (int n = 0; frame % 8 == 0 && n < kSlots && retried < kPollPerFrame && !overBudget(); n++) {
            const int i = order[static_cast<size_t>(n)];
            const int cx = centerX - kRadius + i % kChunks, cz = centerZ - kRadius + i / kChunks;
            const auto it = cache.find(ChunkKey(cx, cz));
            if (it == cache.end() || it->second.loaded || frame - it->second.fetchedAt < kRetryFrames)
                continue;
            if (!Fetch(cx, cz))
                return;
            retried++;
        }
        for (int n = 0; n < kPollPerFrame / 2 && !overBudget(); n++) {
            const int next = (pollCursor + 1) % kSlots;
            const int i = order[static_cast<size_t>(next)];
            if (!Fetch(centerX - kRadius + i % kChunks, centerZ - kRadius + i / kChunks))
                break;
            pollCursor = next;
        }
    }

    template <class Budget> void Repaint(const std::vector<int>& order, int painted, const Budget& overBudget) {
        if (!anyDirty)
            return;
        bool left = false;
        for (int n = 0; n < kSlots; n++) {
            if (painted >= kPaintPerFrame || overBudget()) {
                left = true;
                break;
            }
            const int i = order[static_cast<size_t>(n)];
            const int cx = centerX - kRadius + i % kChunks, cz = centerZ - kRadius + i / kChunks;
            const Slot& s = slots[static_cast<size_t>(SlotOf(cx, cz))];
            if (s.dirty && s.owner == ChunkKey(cx, cz)) {
                Paint(cx, cz);
                painted++;
            }
        }
        anyDirty = left;
    }

    void Trim() {
        if (cache.size() > static_cast<size_t>(kSlots * 3))
            std::erase_if(cache, [&](const auto& e) {
                const int cx = static_cast<int>(e.first >> 32), cz = static_cast<int>(static_cast<int32_t>(e.first));
                return std::abs(cx - centerX) > kRadius + 3 || std::abs(cz - centerZ) > kRadius + 3;
            });
    }

    bool Fetch(int cx, int cz) {
        auto data = game::map_chunk(cx, cz, layer);
        if (!data)
            return false;
        Cached& c = cache[ChunkKey(cx, cz)];
        c.fetchedAt = frame;
        if (c.layer == layer && c.loaded == data->loaded && c.version == data->version &&
            slots[static_cast<size_t>(SlotOf(cx, cz))].owner == ChunkKey(cx, cz))
            return true;
        c.layer = layer;
        c.loaded = data->loaded;
        c.version = data->version;
        c.columns = data->columns;
        Paint(cx, cz);
        for (const auto [dx, dz] : {std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}}) {
            Slot& s = slots[static_cast<size_t>(SlotOf(cx + dx, cz + dz))];
            if (s.owner == ChunkKey(cx + dx, cz + dz)) {
                s.dirty = true;
                anyDirty = true;
            }
        }
        return true;
    }

    void Paint(int cx, int cz) {
        const int64_t key = ChunkKey(cx, cz);
        const auto self = cache.find(key);
        if (self == cache.end())
            return;
        const Cached& c = self->second;
        const int slotIndex = SlotOf(cx, cz);
        Slot& slot = slots[static_cast<size_t>(slotIndex)];
        if (slot.owner != key)
            layoutVersion++;
        slot.owner = key;
        slot.version = c.version;
        slot.layer = c.layer;
        slot.dirty = false;

        const auto find = [&](int x, int z) -> const Cached* {
            const auto it = cache.find(ChunkKey(x, z));
            return it != cache.end() && it->second.loaded && it->second.layer == c.layer ? &it->second : nullptr;
        };
        const Cached* west = find(cx - 1, cz);
        const Cached* north = find(cx, cz - 1);
        const Cached* northWest = find(cx - 1, cz - 1);
        const auto column = [&](int x, int z) -> const game::MapColumn* {
            const Cached* from = &c;
            if (x < 0 && z < 0)
                from = northWest;
            else if (x < 0)
                from = west;
            else if (z < 0)
                from = north;
            if (!from)
                return nullptr;
            return &from->columns[static_cast<size_t>(((z + 16) & 15) * 16 + ((x + 16) & 15))];
        };
        const float relief = static_cast<float>(settings::relief);
        const bool depthShade = settings::water_depth;
        const bool cave = c.layer != game::kMapSurface;
        std::array<uint8_t, 16 * 16 * 4> tile{};
        for (int z = 0; z < 16; z++)
            for (int x = 0; x < 16; x++) {
                uint8_t* out = &tile[static_cast<size_t>((z * 16 + x) * 4)];
                const game::MapColumn& col = c.columns[static_cast<size_t>(z * 16 + x)];
                if (!c.loaded || !(col.flags & game::kMapLoaded)) {
                    out[3] = 0;
                    continue;
                }
                float r = static_cast<float>((col.rgb >> 16) & 255), g = static_cast<float>((col.rgb >> 8) & 255),
                      b = static_cast<float>(col.rgb & 255);
                if (col.flags & game::kMapRock) {
                    out[0] = static_cast<uint8_t>(r);
                    out[1] = static_cast<uint8_t>(g);
                    out[2] = static_cast<uint8_t>(b);
                    out[3] = 255;
                    continue;
                }
                float shade = 1.0f;
                if (col.flags & game::kMapWater) {
                    if (depthShade)
                        shade -= std::min<float>(col.depth, 14.0f) * 0.022f;
                    if (const game::MapColumn* w = column(x - 1, z - 1); w && !(w->flags & game::kMapWater))
                        shade += 0.12f * relief;
                } else {
                    const auto heightOf = [&](const game::MapColumn* n) {
                        return n && !(n->flags & game::kMapRock) && (n->flags & game::kMapLoaded)
                                   ? static_cast<float>(n->height)
                                   : static_cast<float>(col.height);
                    };
                    const float h = static_cast<float>(col.height);
                    const float gx = h - heightOf(column(x - 1, z)), gz = h - heightOf(column(x, z - 1));
                    const float nl = std::sqrt(gx * gx + 1.0f + gz * gz);
                    const float lit = (-gx * sun[0] + sun[1] - gz * sun[2]) / nl;
                    const float flat = sun[1];
                    float direct = (lit - flat) / std::max(flat, 0.3f) * 0.6f;
                    if (lit <= 0.0f)
                        direct -= 0.25f;
                    shade += std::clamp(direct * sunStrength, -0.55f, 0.5f) * relief;
                    if (col.flags & game::kMapLeaves)
                        shade -= 0.04f;
                    if (cave)
                        shade -= std::clamp(static_cast<float>(c.layer - col.height) / 24.0f, 0.0f, 0.45f);
                    else
                        shade *= 1.0f + std::clamp((h - 63.0f) / 160.0f, -0.3f, 0.35f);
                }
                shade = std::clamp(shade, 0.3f, 1.6f);
                out[0] = Clamp8(r * shade);
                out[1] = Clamp8(g * shade);
                out[2] = Clamp8(b * shade);
                out[3] = 255;
            }
        const int ox = (slotIndex % kChunks) * 16, oz = (slotIndex / kChunks) * 16;
        for (int z = 0; z < 16; z++)
            std::copy_n(&tile[static_cast<size_t>(z * 16 * 4)], 16 * 4,
                        &pixels[(static_cast<size_t>(oz + z) * kTexW + static_cast<size_t>(ox)) * 4]);
        dirtyX0 = std::min(dirtyX0, ox);
        dirtyY0 = std::min(dirtyY0, oz);
        dirtyX1 = std::max(dirtyX1, ox + 16);
        dirtyY1 = std::max(dirtyY1, oz + 16);
    }

    void Upload() {
        if (dirtyX1 <= dirtyX0 || dirtyY1 <= dirtyY0)
            return;
        const int w = dirtyX1 - dirtyX0, h = dirtyY1 - dirtyY0;
        region.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
        for (int y = 0; y < h; y++)
            std::copy_n(&pixels[(static_cast<size_t>(dirtyY0 + y) * kTexW + static_cast<size_t>(dirtyX0)) * 4],
                        static_cast<size_t>(w) * 4, &region[static_cast<size_t>(y) * static_cast<size_t>(w) * 4]);
        (void)texture.update(dirtyX0, dirtyY0, w, h, region);
        dirtyX0 = dirtyY0 = INT32_MAX;
        dirtyX1 = dirtyY1 = INT32_MIN;
    }

    void Draw(const abi::PlayerState& p, const gfx::Display& d) {
        const float gs = static_cast<float>(std::max(1, d.guiScale));
        const float size = static_cast<float>(static_cast<int>(settings::size)) * gs;
        const float margin = 4.0f * gs;
        float x0 = static_cast<float>(d.width) - size - margin, y0 = margin;
        switch (settings::corner.get()) {
        case settings::Corner::TopLeft:
            x0 = margin;
            break;
        case settings::Corner::BottomRight:
            y0 = static_cast<float>(d.height) - size - margin;
            break;
        case settings::Corner::BottomLeft:
            x0 = margin;
            y0 = static_cast<float>(d.height) - size - margin;
            break;
        default:
            break;
        }
        const float zoom = static_cast<float>(AnimatedZoom() * kZoomScale) * gs;
        const float span = size / zoom;
        const bool rotate = settings::rotate;
        const float yaw = rotate ? static_cast<float>((180.0f - p.yaw) * kPi / 180.0) : 0.0f;
        const float cs = std::cos(yaw), sn = std::sin(yaw);
        const float cxs = x0 + size * 0.5f, cys = y0 + size * 0.5f;
        const auto toScreen = [&](double wx, double wz, float& sx, float& sy) {
            const float dx = static_cast<float>(wx - p.x) * zoom, dz = static_cast<float>(wz - p.z) * zoom;
            sx = cxs + dx * cs - dz * sn;
            sy = cys + dx * sn + dz * cs;
        };
        const float light = static_cast<float>(static_cast<double>(settings::opacity));
        float bright = 1.0f;
        if (settings::day_night && layer == game::kMapSurface)
            if (const auto w = game::world()) {
                const double t = std::fmod(static_cast<double>(w->timeOfDay), 24000.0);
                const double sun = std::cos((t / 24000.0 - 0.25) * 2.0 * kPi);
                bright = static_cast<float>(std::clamp(0.55 + 0.45 * std::clamp(sun * 2.0 + 0.5, 0.0, 1.0), 0.55, 1.0));
            }
        const uint8_t lb = Clamp8(bright * 255.0f), la = Clamp8(light * 255.0f);
        const uint32_t tint = gfx::rgba8(lb, lb, lb, la);
        {
            uint64_t sig = 1469598103934665603ull;
            const auto mix = [&](uint64_t v) {
                sig ^= v;
                sig *= 1099511628211ull;
            };
            const auto mixd = [&](double v) {
                uint64_t b;
                std::memcpy(&b, &v, sizeof b);
                mix(b);
            };
            mixd(p.x);
            mixd(p.z);
            mixd(p.yaw);
            mixd(zoom);
            mixd(size);
            mix(static_cast<uint64_t>(d.width) << 32 | static_cast<uint32_t>(d.height));
            mix(static_cast<uint64_t>(d.guiScale));
            mix(tint);
            mix(static_cast<uint64_t>(settings::corner.get()));
            mix(settings::grid ? 1u : 0u);
            mix(settings::waypoints ? 1u : 0u);
            mix(rotate ? 1u : 0u);
            mix(layoutVersion);
            mix(iconTexture ? iconTexture.id() : 0u);
            mix(texture.id());
            mix(static_cast<uint64_t>(centerX) << 32 ^ static_cast<uint32_t>(centerZ));
            if (shown && sig == drawSignature)
                return;
            drawSignature = sig;
        }
        const float wu = 0.5f / kTexW, wv = (kWhiteRow + 0.5f) / kTexH;

        list.clear();
        list.batch(texture.id(), x0, y0, x0 + size, y0 + size);
        const auto solid = [&](float ax, float ay, float bx, float by, float cx2, float cy2, float dx, float dy, uint32_t color) {
            const uint32_t a = list.vertex(ax, ay, wu, wv, color), b = list.vertex(bx, by, wu, wv, color),
                           c = list.vertex(cx2, cy2, wu, wv, color), e = list.vertex(dx, dy, wu, wv, color);
            list.triangle(a, b, c).triangle(a, c, e);
        };
        solid(x0, y0, x0 + size, y0, x0 + size, y0 + size, x0, y0 + size, gfx::rgba8(10, 12, 16, la));

        const float reach = span * (rotate ? 0.75f : 0.5f) + 16.0f;
        const int fx = static_cast<int>(std::floor((p.x - reach) / 16.0)), lx = static_cast<int>(std::floor((p.x + reach) / 16.0));
        const int fz = static_cast<int>(std::floor((p.z - reach) / 16.0)), lz = static_cast<int>(std::floor((p.z + reach) / 16.0));
        constexpr float inset = 0.02f;
        for (int cz = std::max(fz, centerZ - kRadius); cz <= std::min(lz, centerZ + kRadius); cz++)
            for (int cx = std::max(fx, centerX - kRadius); cx <= std::min(lx, centerX + kRadius); cx++) {
                const int slotIndex = SlotOf(cx, cz);
                if (slots[static_cast<size_t>(slotIndex)].owner != ChunkKey(cx, cz))
                    continue;
                const float u0 = (static_cast<float>((slotIndex % kChunks) * 16) + inset) / kTexW;
                const float v0 = (static_cast<float>((slotIndex / kChunks) * 16) + inset) / kTexH;
                const float u1 = u0 + (16.0f - 2 * inset) / kTexW, v1 = v0 + (16.0f - 2 * inset) / kTexH;
                const double wx = cx * 16.0, wz = cz * 16.0;
                float sx[4], sy[4];
                toScreen(wx, wz, sx[0], sy[0]);
                toScreen(wx + 16, wz, sx[1], sy[1]);
                toScreen(wx + 16, wz + 16, sx[2], sy[2]);
                toScreen(wx, wz + 16, sx[3], sy[3]);
                const uint32_t a = list.vertex(sx[0], sy[0], u0, v0, tint), b = list.vertex(sx[1], sy[1], u1, v0, tint),
                               c = list.vertex(sx[2], sy[2], u1, v1, tint), e = list.vertex(sx[3], sy[3], u0, v1, tint);
                list.triangle(a, b, c).triangle(a, c, e);
            }

        const auto line = [&](double ax, double az, double bx, double bz, float width, uint32_t color) {
            float sx0, sy0, sx1, sy1;
            toScreen(ax, az, sx0, sy0);
            toScreen(bx, bz, sx1, sy1);
            const float vx = sx1 - sx0, vy = sy1 - sy0;
            const float len = std::max(0.001f, std::sqrt(vx * vx + vy * vy));
            const float nx = -vy / len * width * 0.5f, ny = vx / len * width * 0.5f;
            solid(sx0 + nx, sy0 + ny, sx1 + nx, sy1 + ny, sx1 - nx, sy1 - ny, sx0 - nx, sy0 - ny, color);
        };
        if (settings::grid) {
            const uint32_t gridColor = gfx::rgba8(60, 60, 60, static_cast<uint8_t>(150 * light));
            for (int c = fx; c <= lx + 1; c++)
                line(c * 16.0, p.z - reach, c * 16.0, p.z + reach, std::max(1.0f, gs * 0.75f), gridColor);
            for (int c = fz; c <= lz + 1; c++)
                line(p.x - reach, c * 16.0, p.x + reach, c * 16.0, std::max(1.0f, gs * 0.75f), gridColor);
            const double bx = centerX * 16.0, bz = centerZ * 16.0;
            const uint32_t white = gfx::rgba8(255, 255, 255, la);
            const float w = std::max(1.0f, gs);
            line(bx, bz, bx + 16, bz, w, white);
            line(bx + 16, bz, bx + 16, bz + 16, w, white);
            line(bx + 16, bz + 16, bx, bz + 16, w, white);
            line(bx, bz + 16, bx, bz, w, white);
        }
        if (settings::waypoints)
            for (const Waypoint& wp : waypoints) {
                float sx, sy;
                toScreen(wp.x, wp.z, sx, sy);
                const float ex = std::clamp(sx, x0 + 4 * gs, x0 + size - 4 * gs), ey = std::clamp(sy, y0 + 4 * gs, y0 + size - 4 * gs);
                const float r = 3.5f * gs;
                solid(ex, ey - r, ex + r, ey, ex, ey + r, ex - r, ey, gfx::rgba8(0, 0, 0, la));
                const float r2 = 2.5f * gs;
                solid(ex, ey - r2, ex + r2, ey, ex, ey + r2, ex - r2, ey, (wp.color & 0x00FFFFFFu) | (static_cast<uint32_t>(la) << 24));
            }
        {
            const float heading = rotate ? 0.0f : static_cast<float>((p.yaw + 180.0f) * kPi / 180.0);
            const float hc = std::cos(heading), hs = std::sin(heading);
            const auto rot = [&](float ax, float ay, float& ox, float& oy) {
                ox = cxs + ax * hc - ay * hs;
                oy = cys + ax * hs + ay * hc;
            };
            if (iconTexture) {
                const float h = 5.0f * gs;
                const float cornersX[4] = {-h, h, h, -h}, cornersY[4] = {-h, -h, h, h};
                const float us[4] = {0, 1, 1, 0}, vs[4] = {0, 0, 1, 1};
                list.batch(iconTexture.id(), x0, y0, x0 + size, y0 + size);
                uint32_t idx[4];
                for (int i = 0; i < 4; i++) {
                    float sx, sy;
                    rot(cornersX[i], cornersY[i], sx, sy);
                    idx[i] = list.vertex(sx, sy, us[i], vs[i], gfx::rgba8(255, 255, 255, 255));
                }
                list.triangle(idx[0], idx[1], idx[2]).triangle(idx[0], idx[2], idx[3]);
                list.batch(texture.id(), x0, y0, x0 + size, y0 + size);
            } else {
                const float s = 4.5f * gs;
                float ax, ay, bx, by, cx2, cy2, dx, dy;
                rot(0, -s * 1.2f, ax, ay);
                rot(s * 0.8f, s, bx, by);
                rot(0, s * 0.45f, cx2, cy2);
                rot(-s * 0.8f, s, dx, dy);
                const float o = 1.0f * gs;
                float oax, oay, obx, oby, ocx, ocy, odx, ody;
                rot(0, -s * 1.2f - o * 1.6f, oax, oay);
                rot(s * 0.8f + o, s + o, obx, oby);
                rot(0, s * 0.45f + o, ocx, ocy);
                rot(-s * 0.8f - o, s + o, odx, ody);
                solid(oax, oay, obx, oby, ocx, ocy, odx, ody, gfx::rgba8(0, 0, 0, 230));
                solid(ax, ay, bx, by, cx2, cy2, dx, dy, gfx::rgba8(255, 255, 255, 255));
            }
        }
        const uint32_t frame = gfx::rgba8(20, 20, 20, la);
        const float fw = std::max(1.0f, gs);
        solid(x0, y0, x0 + size, y0, x0 + size, y0 + fw, x0, y0 + fw, frame);
        solid(x0, y0 + size - fw, x0 + size, y0 + size - fw, x0 + size, y0 + size, x0, y0 + size, frame);
        solid(x0, y0, x0 + fw, y0, x0 + fw, y0 + size, x0, y0 + size, frame);
        solid(x0 + size - fw, y0, x0 + size, y0, x0 + size, y0 + size, x0 + size - fw, y0 + size, frame);
        (void)gfx::submit("minimap", 50, list);
        shown = true;
    }

    void loadIcon() {
        iconRetry = 120;
        const auto image = gfx::load_image("textures/map/map_icons");
        if (!image)
            return;
        const int cell = image->width / 4;
        if (cell <= 0 || image->height < cell)
            return;
        const int big = cell * kIconScale;
        std::vector<uint8_t> icon(static_cast<size_t>(big) * static_cast<size_t>(big) * 4);
        for (int y = 0; y < big; y++)
            for (int x = 0; x < big; x++) {
                const uint8_t* src = &image->rgba[(static_cast<size_t>(y / kIconScale) * static_cast<size_t>(image->width) +
                                                   static_cast<size_t>(x / kIconScale)) *
                                                  4];
                std::copy_n(src, 4, &icon[(static_cast<size_t>(y) * static_cast<size_t>(big) + static_cast<size_t>(x)) * 4]);
            }
        auto t = gfx::Texture::create(big, big, icon, gfx::Filter::Linear);
        if (t)
            iconTexture = std::move(*t);
    }

    void loadWaypoints() {
        waypoints.clear();
        const auto text = fs::read_text("waypoints.txt");
        if (!text)
            return;
        size_t at = 0;
        while (at < text->size()) {
            size_t end = text->find('\n', at);
            if (end == std::string::npos)
                end = text->size();
            const std::string row = text->substr(at, end - at);
            Waypoint w;
            unsigned color = 0;
            int death = 0;
            if (std::sscanf(row.c_str(), "%lf %lf %x %d", &w.x, &w.z, &color, &death) == 4) {
                w.color = color;
                w.death = death != 0;
                waypoints.push_back(w);
            }
            at = end + 1;
        }
    }

    void saveWaypoints() {
        layoutVersion++;
        std::string out;
        char row[96];
        for (const Waypoint& w : waypoints) {
            std::snprintf(row, sizeof row, "%.2f %.2f %x %d\n", w.x, w.z, w.color, w.death ? 1 : 0);
            out += row;
        }
        (void)fs::write_text("waypoints.txt", out);
    }

    gfx::Texture texture;
    gfx::Texture iconTexture;
    gfx::DrawList list;
    std::vector<uint8_t> pixels;
    std::vector<uint8_t> region;
    int dirtyX0 = INT32_MAX, dirtyY0 = INT32_MAX, dirtyX1 = INT32_MIN, dirtyY1 = INT32_MIN;
    std::unordered_map<int64_t, Cached> cache;
    std::array<Slot, kSlots> slots{};
    std::vector<Waypoint> waypoints;
    int centerX = INT32_MIN, centerZ = INT32_MIN;
    int32_t layer = game::kMapSurface;
    int dimension = -1;
    int cursor = 0, pollCursor = 0;
    bool underRoof = false;
    bool roofCandidate = false;
    int64_t roofSince = 0;
    double shownZoom = 0.0;
    int sunBucket = -2;
    std::array<float, 3> sun{-0.55f, 0.75f, -0.37f};
    float sunStrength = 1.0f;
    int64_t zoomClock = 0;
    int iconRetry = 0;
    uint64_t frame = 0;
    bool shown = false;
    bool anyDirty = true;
    uint64_t layoutVersion = 0;
    uint64_t drawSignature = 0;
};
PERLIN_REGISTER(Minimap)
