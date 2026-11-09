#include "client/PerlinBrand.hpp"
#include "client/ui/PerlinBadge.hpp"
#include "client/PerlinInternal.hpp"

namespace perlin {
    namespace {
        void RayTargets(Level& level, glm::vec3 origin, glm::vec3 dir, double reach, ui::DebugInfo& info) {
            BlockPalette& palette = BlockPalette::Instance();
            static std::unordered_map<uint16_t, int> kind;
            const auto classify = [&](uint16_t id) {
                const auto it = kind.find(id);
                if (it != kind.end())
                    return it->second;
                const std::string name = palette.Name(id);
                int k = 2;
                if (name == "minecraft:air" || name == "minecraft:cave_air" || name == "minecraft:void_air" ||
                    name.empty())
                    k = 0;
                else if (name == "minecraft:water" || name == "minecraft:flowing_water" ||
                         name == "minecraft:lava" || name == "minecraft:flowing_lava")
                    k = 1;
                return kind[id] = k;
            };
            const auto describe = [&](ui::DebugInfo::Target& t, int x, int y, int z, uint16_t id) {
                t.valid = true;
                t.x = x;
                t.y = y;
                t.z = z;
                t.name = palette.Name(id);
                for (const auto& [key, value] : palette.States(id)) {
                    std::string k = key;
                    if (k.starts_with("minecraft:"))
                        k.erase(0, 10);
                    std::string v;
                    switch (value.kind) {
                    case BlockPalette::StateValue::Bool:
                        v = value.i ? "true" : "false";
                        break;
                    case BlockPalette::StateValue::Int:
                        v = std::to_string(value.i);
                        break;
                    default:
                        v = value.s;
                        break;
                    }
                    t.properties.emplace_back(k, v);
                }
            };
            int x = static_cast<int>(std::floor(origin.x)), y = static_cast<int>(std::floor(origin.y)),
                z = static_cast<int>(std::floor(origin.z));
            const int stepX = dir.x > 0 ? 1 : -1, stepY = dir.y > 0 ? 1 : -1, stepZ = dir.z > 0 ? 1 : -1;
            const auto inv = [](float d) { return d != 0.0f ? std::fabs(1.0f / d) : 1e30f; };
            const float dx = inv(dir.x), dy = inv(dir.y), dz = inv(dir.z);
            const auto firstBoundary = [](float o, int cell, int step, float delta) {
                return (step > 0 ? (static_cast<float>(cell) + 1.0f - o) : (o - static_cast<float>(cell))) *
                       delta;
            };
            float tx = firstBoundary(origin.x, x, stepX, dx), ty = firstBoundary(origin.y, y, stepY, dy),
                  tz = firstBoundary(origin.z, z, stepZ, dz);
            float t = 0.0f;
            while (t <= reach) {
                bool loaded = true;
                const uint16_t id = level.GetBlock(x, y, z, loaded);
                if (!loaded)
                    return;
                const int k = classify(id);
                if (k == 2) {
                    describe(info.block, x, y, z, id);
                    return;
                }
                if (k == 1 && !info.fluid.valid)
                    describe(info.fluid, x, y, z, id);
                if (tx < ty && tx < tz) {
                    t = tx;
                    tx += dx;
                    x += stepX;
                } else if (ty < tz) {
                    t = ty;
                    ty += dy;
                    y += stepY;
                } else {
                    t = tz;
                    tz += dz;
                    z += stepZ;
                }
            }
        }
    }

    void Perlin::FillDebug(ui::HudState& s, BedrockSession* live, Level* level) {
        s.debug = debug.on && s.visible;
        if (!s.debug)
            return;
        const auto now = Clock::now();
        if (live && now - debug.rateAt >= std::chrono::seconds(1)) {
            debug.txRate = static_cast<int>(live->PacketsSent() - debug.rateTx);
            debug.rxRate = static_cast<int>(live->PacketsReceived() - debug.rateRx);
            debug.rateTx = live->PacketsSent();
            debug.rateRx = live->PacketsReceived();
            debug.rateAt = now;
        }
        ui::DebugInfo d;
        d.version = std::string("Perlin ") + PERLIN_VERSION + " (bedrock " + kBedrockVersion + "/vanilla)";
        d.fps = debug.fps;
        d.maxFps = options.maxFps;
        d.vsync = options.vsync;
        d.fancyGraphics = options.fancyGraphics;
        const GraphicsSettings gfx = GraphicsSettings::From(options);
        d.clouds = gfx.clouds;
        d.fancyClouds = gfx.fancyClouds;
        d.biomeBlend = gfx.biomeBlendRadius;
        d.tx = debug.txRate;
        d.rx = debug.rxRate;
        d.renderDistance = options.renderDistance;
        d.simulationDistance = options.renderDistance;
        if (levelRenderer) {
            d.chunksRendered = static_cast<int>(levelRenderer->MeshedChunks());
            d.pendingChunks = static_cast<int>(levelRenderer->PendingMeshes());
            d.pendingUploads = static_cast<int>(levelRenderer->MeshesInFlight());
            d.availableBuffers = static_cast<int>(
                levelRenderer->MaxMeshesInFlight() -
                std::min(levelRenderer->MaxMeshesInFlight(), levelRenderer->MeshesInFlight()));
        }
        if (level) {
            std::lock_guard lock(level->mutex);
            d.chunksLoaded = static_cast<int>(level->chunks.size());
        }
        static const char* kDimensions[] = {"minecraft:overworld", "minecraft:the_nether",
                                            "minecraft:the_end"};
        if (live)
            d.dimension = kDimensions[std::clamp(live->DimensionId(), 0, 2)];

        const glm::vec3 eye = camera.EyePosition();
        d.x = camera.x;
        d.y = camera.y - 1.62;
        d.z = camera.z;
        d.yaw = camera.yaw - 90.0f;
        d.pitch = -camera.pitch;
        d.skyLight = debug.sky;
        d.customSky = debug.customSky;
        d.blockLight = debug.block;
        d.compiler = sysinfo::Compiler();
        d.memUsed = sysinfo::ProcessMemory();
        d.memPeak = sysinfo::ProcessPeakMemory();
        d.memTotal = sysinfo::TotalMemory();
        {
            // the dxgi query walks the driver's residency lists: twice a second is plenty for F3
            static auto lastQuery = std::chrono::steady_clock::time_point{};
            static bool known = false;
            static uint64_t dedicated = 0, shared = 0;
            const auto now = std::chrono::steady_clock::now();
            if (renderer && now - lastQuery > std::chrono::milliseconds(500)) {
                lastQuery = now;
                known = renderer->GpuMemoryUsage(dedicated, shared);
            }
            d.vramKnown = known;
            d.vramDedicated = dedicated;
            d.vramShared = shared;
        }
        d.cpuThreads = sysinfo::CpuThreads();
        static const std::string cpu = sysinfo::CpuName();
        d.cpuName = cpu;
        int w = 0, h = 0;
        platform->GetFramebufferSize(w, h);
        d.displayW = w;
        d.displayH = h;
        if (renderer) {
            d.gpuLine1 = renderer->RendererName();
            d.displayVendor = renderer->GpuVendor();
            d.gpuLine2 = renderer->GpuDescription();
        }
        if (level) {
            const int bx = static_cast<int>(std::floor(d.x)), bz = static_cast<int>(std::floor(d.z));
            bool loaded = false;
            level->GetBlock(bx, static_cast<int>(std::floor(d.y)), bz, loaded);
            d.inLoadedChunk = loaded;
            if (loaded) {
                if (now - debug.heightAt > std::chrono::milliseconds(500)) {
                    debug.heightAt = now;
                    BlockPalette& palette = BlockPalette::Instance();
                    debug.heightSurface = level->FindBlockBelow(bx, bz, 319, [&](uint16_t id) {
                        return palette.Name(id) != "minecraft:air";
                    }) + 1;
                }
                d.heightWorldSurface = d.heightMotionBlocking = debug.heightSurface;
                d.biome = levelRenderer ? levelRenderer->Tints().NameForId(
                                              level->GetBiome(bx, static_cast<int>(std::floor(d.y)), bz))
                                        : "minecraft:plains";
                if (!d.biome.starts_with("minecraft:"))
                    d.biome = "minecraft:" + d.biome;
            }
            RayTargets(*level, eye, camera.Direction(), 20.0, d);
        }
        static std::vector<std::string> cachedLeft, cachedRight;
        static auto cachedAt = Clock::time_point{};
        if (cachedLeft.empty() || now - cachedAt > std::chrono::milliseconds(100)) {
            cachedAt = now;
            cachedLeft.clear();
            cachedRight.clear();
            ui::BuildDebugLines(d, cachedLeft, cachedRight);
        }
        s.debugLeft = cachedLeft;
        s.debugRight = cachedRight;
        ApplyModDebugLines(s.debugLeft, s.debugRight);
    }

    void Perlin::FillHudState(ui::HudState& s, BedrockSession* live, bool preview) {
        ApplyModSlotOverlays(s);

        s.visible = options.showHud && (live || preview) && !(replayCtl.Playing() && replayUi.hideHud) &&
                    !form.shown && !inventory.shown && !WorldCovered();
        s.tick = preview ? static_cast<uint64_t>(
                               std::chrono::duration<double>(Clock::now().time_since_epoch()).count() * 20.0)
                         : hudAnim.ticks;
        s.inWater = headUnderwater;
        chat.Fill(s, hudAnim.ticks, detail::MsNow());
        s.sidebarVisible = false;
        s.sidebarTitle.clear();
        s.sidebar.clear();
        if (live) {
            const auto sb = live->Scoreboard();
            s.sidebarVisible = sb.visible;
            if (sb.visible) {
                s.sidebarTitle = sb.title;
                for (const auto& r : sb.rows)
                    s.sidebar.push_back({r.name, r.score});
            }
        } else if (preview && PERLIN_ENV("PERLIN_PREVIEW_SCOREBOARD")) {
            s.sidebarVisible = true;
            s.sidebarTitle = "\xC2\xA7"
                             "e\xC2\xA7"
                             "lPERLIN";
            s.sidebar = {{"Kills", 12},
                         {"Deaths", 3},
                         {"\xC2\xA7"
                          "aCoins",
                          450}};
        }
        if (preview) {
            s.health = 20.0f;
            s.food = 20;
            s.xpProgress = 0.55f;
            s.xpLevel = 7;
            s.armor = 0;
            s.selectedSlot = previewSlot;
            static const struct {
                const char* name;
                int count;
                int maxDamage;
                int aux;
            } kItems[9] = {{"minecraft:diamond_sword", 1, 1561, 120},
                           {"minecraft:grass_block", 64, 0, 0},
                           {"minecraft:oak_planks", 12, 0, 0},
                           {"minecraft:torch", 33, 0, 0},
                           {"minecraft:apple", 5, 0, 0},
                           {"minecraft:cooked_beef", 2, 0, 0},
                           {"minecraft:iron_pickaxe", 1, 250, 40},
                           {"minecraft:oak_log", 20, 0, 0},
                           {"minecraft:bucket", 1, 0, 0}};
            for (int i = 0; i < 9; i++) {
                s.hotbar[i] = {};
                s.hotbar[i].name = kItems[i].name;
                s.hotbar[i].count = kItems[i].count;
                s.hotbar[i].maxDamage = kItems[i].maxDamage;
                s.hotbar[i].aux = kItems[i].aux;
                s.hotbar[i].enchanted = PERLIN_ENV("PERLIN_PREVIEW_ENCHANT") != nullptr;
                if (s.hotbar[i].enchanted)
                    s.hotbar[i].enchants = {{9, 3}, {12, 2}, {17, 3}, {26, 1}};
                s.hotbar[i].nbtTags = 2;
            }
            s.burning = PERLIN_ENV("PERLIN_PREVIEW_FIRE") != nullptr;
            s.fireFrame = static_cast<int>(s.tick % 32);

            if (const char* anim = PERLIN_ENV("PERLIN_PREVIEW_SCREENANIM")) {
                const double t = std::fmod(std::chrono::duration<double>(
                                               std::chrono::steady_clock::now().time_since_epoch())
                                               .count(),
                                           2.5) /
                                 2.0;
                if (t < 1.0) {
                    s.screenAnimation = std::string(anim) == "totem" ? BedrockSession::kTotemScreenAnimation
                                                                     : std::atoi(anim);
                    s.screenAnimationT = static_cast<float>(t);
                }
            }
            if (const char* item = PERLIN_ENV("PERLIN_PREVIEW_ITEM")) {
                ui::HudItem& h = s.hotbar[std::clamp(previewSlot, 0, 8)];
                h.name = item;
                h.count = 1;
                h.maxDamage = h.aux = 0;
            }
            if (const char* list = PERLIN_ENV("PERLIN_PREVIEW_ITEMS")) {
                std::string all = list;
                size_t at = 0;
                for (int slot = 0; slot < 9 && at <= all.size(); slot++) {
                    size_t comma = all.find(',', at);
                    if (comma == std::string::npos)
                        comma = all.size();
                    ui::HudItem& h = s.hotbar[slot];
                    h = {};
                    h.name = all.substr(at, comma - at);
                    h.count = 1;
                    if (const char* dye = PERLIN_ENV("PERLIN_PREVIEW_DYE")) {
                        h.hasColor = true;
                        h.color = static_cast<int32_t>(std::strtoul(dye, nullptr, 16));
                    }
                    at = comma + 1;
                }
            }
            if (const char* tab = PERLIN_ENV("PERLIN_PREVIEW_TAB")) {
                s.tabOpen = true;
                s.tabEntries.clear();
                Texture head;
                head.width = head.height = 8;
                head.pixels.assign(8 * 8 * 4, 255);
                for (size_t i = 0; i < 64; i++) {
                    head.pixels[i * 4] = 170;
                    head.pixels[i * 4 + 1] = 120;
                    head.pixels[i * 4 + 2] = 90;
                }
                ui::UiAssets::Instance().SetHead("preview", head);
                for (int i = 0; i < std::atoi(tab); i++)
                    s.tabEntries.push_back({"Player" + std::to_string(i + 1), "preview"});
            }
            if (PERLIN_ENV("PERLIN_PREVIEW_EMPTYHAND"))
                s.hotbar[previewSlot] = {};
            if (const char* n = PERLIN_ENV("PERLIN_PREVIEW_ITEMNAME")) {
                s.itemName = n;
                s.itemNameAlpha = 1.0f;
            }
            s.displayHealth = s.health;
            s.effects.clear();
            if (PERLIN_ENV("PERLIN_PREVIEW_EFFECTS")) {
                s.effects.push_back({13, 0, 200, false});
                s.effects.push_back({1, 1, 3600, false});
                s.effects.push_back({10, 0, 1200, true});
                s.effects.push_back({19, 0, 600, false});
                s.effects.push_back({2, 2, 90, false});
            }
            s.locator.clear();
            s.locatorActive = false;
            if (const char* loc = PERLIN_ENV("PERLIN_PREVIEW_LOCATOR")) {
                s.locatorActive = true;
                const int count = std::max(1, std::atoi(loc));
                for (int i = 0; i < count; i++) {
                    ui::LocatorMarker m;
                    m.angle = count == 1
                                  ? 0.0f
                                  : -55.0f + 110.0f * static_cast<float>(i) / static_cast<float>(count - 1);
                    m.distance = static_cast<float>(i) * 400.0f / static_cast<float>(std::max(1, count - 1));
                    m.pitch = i % 4 == 1 ? 1 : (i % 4 == 3 ? -1 : 0);
                    std::array<uint8_t, 16> id{};
                    id[0] = static_cast<uint8_t>(37 * (i + 3));
                    id[9] = static_cast<uint8_t>(91 * (i + 1));
                    m.color = ui::LocatorColorFor(id);
                    s.locator.push_back(m);
                }
            }
            return;
        }
        if (!live)
            return;

        const auto vitals = live->GetVitals();
        const auto inv = live->GetInventory();
        switch (live->GameType()) {
        case 1:
            s.mode = ui::GameMode::Creative;
            break;
        case 2:
            s.mode = ui::GameMode::Adventure;
            break;
        case 3:
        case 4:
        case 6:
            s.mode = ui::GameMode::Spectator;
            break;
        default:
            s.mode = ui::GameMode::Survival;
            break;
        }
        s.health = vitals.health;
        s.maxHealth = std::max(1.0f, vitals.maxHealth);
        s.absorption = vitals.absorption;
        s.food = static_cast<int>(std::lround(vitals.hunger));
        s.saturation = vitals.saturation;
        s.xpLevel = vitals.level;
        s.xpProgress = vitals.xpProgress;
        s.air = vitals.air;
        s.burning = vitals.onFire && s.mode != ui::GameMode::Spectator;
        s.fireFrame = static_cast<int>(s.tick % 32);
        if (auto next = live->TakeScreenAnimation(); next.effect >= 0)
            screenAnimation = next;
        if (screenAnimation.effect >= 0) {

            const float t =
                std::chrono::duration<float>(std::chrono::steady_clock::now() - screenAnimation.started)
                    .count() /
                2.0f;
            if (t >= 1.0f) {
                screenAnimation = {};
            } else {
                s.screenAnimation = screenAnimation.effect;
                s.screenAnimationT = t;
            }
        }
        s.maxAir = std::max(1, vitals.maxAir);
        s.poison = vitals.poison;
        s.wither = vitals.wither;
        s.regeneration = vitals.regeneration;
        s.hungerEffect = vitals.hungerEffect;

        s.selectedSlot = std::clamp(inv.selected, 0, 8);
        for (int i = 0; i < 9; i++)
            s.hotbar[i] = detail::ToHudItem(inv.hotbar[i]);
        s.offhand = options.hand.showOffhand ? detail::ToHudItem(inv.offhand) : ui::HudItem{};
        s.offhandRight = options.hand.leftHanded;
        int armor = 0;
        for (int i = 0; i < 4; i++) {
            s.armorItems[i] = detail::ToHudItem(inv.armor[i]);
            armor += ui::ArmorPoints(inv.armor[i].name);
        }
        s.armor = std::min(20, armor);

        s.healthBlinkUntil = hudAnim.blinkUntil;
        s.displayHealth = static_cast<float>(hudAnim.displayHealth);

        s.effects.clear();
        for (const auto& e : live->GetEffects())
            s.effects.push_back({e.id, e.amplifier, e.ticks, e.ambient});
        s.locator.clear();
        s.locatorActive = false;
        {
            const std::vector<BedrockSession::LocatorWaypoint> waypoints = live->GetLocatorWaypoints();
            const double eyeX = camera.x, eyeY = camera.y, eyeZ = camera.z;
            const double lookElevation = static_cast<double>(camera.pitch);
            const double halfFov = static_cast<double>(camera.fov) * 0.5;
            for (const BedrockSession::LocatorWaypoint& wp : waypoints) {
                if (!wp.visible)
                    continue;
                double tx = wp.x, ty = wp.y, tz = wp.z;
                bool known = wp.hasPosition;
                if (wp.hasActor) {
                    entity::Entity target;
                    if (live->Entities().FindUnique(wp.actorId, target)) {
                        if (target.isSelf)
                            continue;
                        tx = target.pos.x;
                        ty = target.pos.y + 1.0;
                        tz = target.pos.z;
                        known = true;
                    }
                }
                if (!known)
                    continue;
                s.locatorActive = true;
                const double dx = tx - eyeX, dy = ty - eyeY, dz = tz - eyeZ;
                const double horizontal = std::sqrt(dx * dx + dz * dz);
                double angle =
                    std::atan2(dz, dx) * 180.0 / 3.14159265358979323846 - static_cast<double>(camera.yaw);
                angle = std::fmod(angle + 540.0, 360.0) - 180.0;
                ui::LocatorMarker m;
                m.angle = static_cast<float>(angle);
                m.distance = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
                const double elevation =
                    std::atan2(dy, std::max(horizontal, 1e-6)) * 180.0 / 3.14159265358979323846;
                const double rel = elevation - lookElevation;
                m.pitch = std::fabs(rel) <= halfFov ? 0 : (rel > 0 ? 1 : -1);
                m.color =
                    wp.hasColor ? (static_cast<uint32_t>(wp.color) & 0xFFFFFFu) : ui::LocatorColorFor(wp.id);
                if (angle > -60.0 && angle <= 60.0)
                    s.locator.push_back(m);
            }
        }

        if (!pause.shown)
            pause.players = false;
        s.tabOpen = inputManager.IsDown(Action::PlayerList) && !inputManager.Blocked();
        s.tabEntries.clear();
        if (s.tabOpen) {
            // the ui backend loads this key as a mipmapped texture (RmlBackend::LoadTexture)
            std::string badgeKey;
            if (options.perlinBadges && ui::PerlinBadgeSource().width > 0) {
                badgeKey = "perlin:badge";
                if (!ui::UiAssets::Instance().HasHead(badgeKey))
                    ui::UiAssets::Instance().SetHead(badgeKey, ui::PerlinBadgeSource());
            }
            auto players = live->PlayerList();
            std::stable_sort(players.begin(), players.end(),
                             [](const BedrockSession::TabPlayer& a, const BedrockSession::TabPlayer& b) {
                                 std::string x = a.name, y = b.name;
                                 std::transform(x.begin(), x.end(), x.begin(), ::tolower);
                                 std::transform(y.begin(), y.end(), y.begin(), ::tolower);
                                 return x < y;
                             });
            for (const BedrockSession::TabPlayer& p : players) {
                std::string key = "default";
                if (p.skin && p.skin->skinWidth >= 64 && p.skin->skinHeight >= 16) {
                    key = p.uuid + ":" + std::to_string(p.skin->serial);
                    if (!ui::UiAssets::Instance().HasHead(key)) {
                        const int sc = std::max(1, p.skin->skinWidth / 64);
                        Texture t;
                        t.width = t.height = 8;
                        t.pixels.assign(8 * 8 * 4, 255);
                        for (int y = 0; y < 8; y++)
                            for (int x = 0; x < 8; x++) {
                                const auto px = [&](int ox, int oy, uint8_t out[4]) {
                                    const size_t i =
                                        (static_cast<size_t>((oy + y) * sc + sc / 2) * p.skin->skinWidth +
                                         static_cast<size_t>((ox + x) * sc + sc / 2)) *
                                        4;
                                    for (int c = 0; c < 4; c++)
                                        out[c] =
                                            i + 3 < p.skin->skinRgba.size() ? p.skin->skinRgba[i + c] : 255;
                                };
                                uint8_t face[4], hat[4];
                                px(8, 8, face);
                                px(40, 8, hat);
                                uint8_t* o = &t.pixels[static_cast<size_t>(y * 8 + x) * 4];
                                const float a = hat[3] / 255.0f;
                                for (int c = 0; c < 3; c++)
                                    o[c] = static_cast<uint8_t>(face[c] * (1.0f - a) + hat[c] * a);
                                o[3] = 255;
                            }
                        ui::UiAssets::Instance().SetHead(key, std::move(t));
                    }
                } else if (!ui::UiAssets::Instance().HasHead(key)) {
                    Texture t;
                    t.width = t.height = 8;
                    t.pixels.assign(8 * 8 * 4, 255);
                    for (size_t i = 0; i < 64; i++) {
                        t.pixels[i * 4] = 170;
                        t.pixels[i * 4 + 1] = 120;
                        t.pixels[i * 4 + 2] = 90;
                    }
                    ui::UiAssets::Instance().SetHead(key, std::move(t));
                }
                const bool perlinUser =
                    (p.skin && brand::IsPerlinSkinId(p.skin->skinId)) || p.name == live->PlayerName();
                s.tabEntries.push_back({p.name, key, perlinUser ? badgeKey : std::string()});
            }
        }

        const ui::HudItem& held = s.hotbar[s.selectedSlot];
        if (!held.Empty() && hudAnim.toolTimer > 0 && s.mode != ui::GameMode::Spectator) {
            const std::string shortName =
                held.name.starts_with("minecraft:") ? held.name.substr(10) : held.name;
            int rarity = 0;
            static const char* const kUncommon[] = {"experience_bottle", "nether_star",   "totem_of_undying",
                                                    "heart_of_the_sea",  "dragon_breath", "enchanted_book"};
            static const char* const kRare[] = {"golden_apple", "beacon", "conduit"};
            static const char* const kEpic[] = {
                "enchanted_golden_apple",  "dragon_egg",      "command_block", "chain_command_block",
                "repeating_command_block", "structure_block", "jigsaw"};
            for (const char* n : kUncommon)
                if (shortName == n)
                    rarity = 1;
            for (const char* n : kRare)
                if (shortName == n)
                    rarity = 2;
            for (const char* n : kEpic)
                if (shortName == n)
                    rarity = 3;
            if (shortName.starts_with("music_disc_"))
                rarity = 2;
            if (held.enchanted && shortName != "enchanted_book" && shortName != "enchanted_golden_apple")
                rarity = rarity <= 1 ? 2 : 3;
            static const char* const kRarityCode[] = {"f", "e", "b", "d"};
            s.itemName = held.customName.empty() ? ui::UiAssets::Instance().ItemName(held.name, held.aux)
                                                 : held.customName;
            s.itemName = std::string("\xC2\xA7") + kRarityCode[rarity] +
                         (held.customName.empty() ? "" : "\xC2\xA7o") + s.itemName;
            s.itemNameAlpha =
                std::min(255.0f, static_cast<float>(hudAnim.toolTimer) * 256.0f / 10.0f) / 255.0f;
        } else {
            s.itemName.clear();
            s.itemNameAlpha = 0.0f;
        }

        const auto text = live->GetTitleText();
        if (hudAnim.overlayTime > 0 && !text.actionBar.empty()) {
            const float f = static_cast<float>(hudAnim.overlayTime) - hudPartial;
            s.actionBar = text.actionBar;
            s.actionBarAlpha = std::clamp(static_cast<int>(f * 255.0f / 20.0f), 0, 255) > 8
                                   ? std::min(255, static_cast<int>(f * 255.0f / 20.0f)) / 255.0f
                                   : 0.0f;
        } else {
            s.actionBar.clear();
            s.actionBarAlpha = 0.0f;
        }
        if (hudAnim.titleTime > 0 && !text.title.empty()) {
            const float f = static_cast<float>(hudAnim.titleTime) - hudPartial;
            int alpha = 255;
            if (hudAnim.titleTime > hudAnim.titleFadeOut + hudAnim.titleStay) {
                const float f1 =
                    static_cast<float>(hudAnim.titleFadeIn + hudAnim.titleStay + hudAnim.titleFadeOut) - f;
                alpha = static_cast<int>(f1 * 255.0f / std::max(1, hudAnim.titleFadeIn));
            }
            if (hudAnim.titleTime <= hudAnim.titleFadeOut)
                alpha = static_cast<int>(f * 255.0f / std::max(1, hudAnim.titleFadeOut));
            alpha = std::clamp(alpha, 0, 255);
            s.title = text.title;
            s.subtitle = text.subtitle;
            s.titleAlpha = alpha > 8 ? static_cast<float>(alpha) / 255.0f : 0.0f;
        } else {
            s.title.clear();
            s.subtitle.clear();
            s.titleAlpha = 0.0f;
        }
    }

    void Perlin::TickHud() {
        HudAnim& a = hudAnim;
        a.ticks++;
        const int tick = static_cast<int>(a.ticks);
        ui::HudState& s = hudState;

        const int health = static_cast<int>(std::ceil(s.health));
        if (!a.haveHealth) {
            a.haveHealth = true;
            a.lastHealth = health;
            a.displayHealth = health;
            a.lastHealthTick = tick;
        }
        const bool invulnerable = tick - a.lastHurtTick < 20;
        if (health < a.lastHealth) {
            a.lastHurtTick = tick;
            a.lastHealthTick = tick;
            a.blinkUntil = tick + 20;
        } else if (health > a.lastHealth && invulnerable) {
            a.lastHealthTick = tick;
            a.blinkUntil = tick + 10;
        }
        if (tick - a.lastHealthTick > 20) {
            a.lastHealth = health;
            a.displayHealth = health;
            a.lastHealthTick = tick;
        }
        a.lastHealth = health;

        const ui::HudItem& held = s.hotbar[std::clamp(s.selectedSlot, 0, 8)];
        if (held.Empty()) {
            a.toolTimer = 0;
        } else if (!a.lastTool.Empty() && a.lastTool.name == held.name &&
                   (held.maxDamage > 0 || a.lastTool.aux == held.aux)) {
            if (a.toolTimer > 0)
                a.toolTimer--;
        } else {
            a.toolTimer = 40;
        }
        a.lastTool = held;

        if (a.overlayTime > 0)
            a.overlayTime--;
        if (a.titleTime > 0)
            a.titleTime--;
        if (session) {
            const auto text = session->GetTitleText();
            if (text.actionAt != a.actionAtSeen) {
                a.actionAtSeen = text.actionAt;
                if (!text.actionBar.empty())
                    a.overlayTime = 60;
            }
            if (text.titleAt != a.titleAtSeen) {
                a.titleAtSeen = text.titleAt;
                if (!text.title.empty()) {
                    a.titleFadeIn = std::max(0, text.fadeIn);
                    a.titleStay = std::max(0, text.stay);
                    a.titleFadeOut = std::max(0, text.fadeOut);
                    a.titleTime = a.titleFadeIn + a.titleStay + a.titleFadeOut;
                }
            }
            if (text.title.empty() && text.subtitle.empty())
                a.titleTime = 0;
        }
    }

    void Perlin::SpawnPreviewEntities() {
        const std::string list = PERLIN_ENV("PERLIN_PREVIEW_ENTITIES");
        std::vector<std::string> names;
        for (size_t start = 0; start <= list.size();) {
            const size_t comma = list.find(',', start);
            const std::string item =
                list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!item.empty())
                names.push_back(item);
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
        const float yawRad = glm::radians(camera.yaw);
        const glm::dvec3 forward(std::cos(yawRad), 0.0, std::sin(yawRad));
        const glm::dvec3 right(-forward.z, 0.0, forward.x);
        const double groundY = PERLIN_ENV("PERLIN_PREVIEW_ENTITY_Y")
                                   ? std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_Y"))
                                   : camera.y - 1.62;
        const double distance = PERLIN_ENV("PERLIN_PREVIEW_ENTITY_DIST")
                                    ? std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_DIST"))
                                    : 4.0;
        const double spacing = PERLIN_ENV("PERLIN_PREVIEW_ENTITY_SPACING")
                                   ? std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_SPACING"))
                                   : 1.8;
        for (size_t i = 0; i < names.size(); i++) {
            std::string type = names[i], variant;
            if (const size_t colon = type.find(':'); colon != std::string::npos) {
                variant = type.substr(colon + 1);
                type.resize(colon);
            }
            const glm::dvec3 p = glm::dvec3(camera.x, groundY, camera.z) + forward * distance +
                                 right * ((static_cast<double>(i) - (names.size() - 1) * 0.5) * spacing);
            const float yaw = camera.yaw - 90.0f + 180.0f +
                              (PERLIN_ENV("PERLIN_PREVIEW_ENTITY_YAW")
                                   ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_YAW")))
                                   : 0.0f);
            const uint64_t rid = 1000 + i;
            if (type == "item") {
                previewEntities.AddItem(rid, static_cast<int64_t>(rid), p,
                                        "minecraft:" +
                                            (variant.empty() ? std::string("diamond_sword") : variant),
                                        variant.contains("stack") ? 40 : 1, 0, 0, false);
            } else if (type == "player") {
                previewEntities.AddPlayer(rid, static_cast<int64_t>(rid), "preview" + std::to_string(i),
                                          "Steve", {p.x, p.y, p.z}, 0.0f, yaw, yaw, 0, "");
                auto skin = std::make_shared<entity::SkinData>();
                skin->serial = 100 + i;
                const bool slim = variant.contains("alex");
                Texture t;
                if (ui::UiAssets::Instance().Image(slim ? "textures/entity/alex" : "textures/entity/steve",
                                                   t)) {
                    skin->skinWidth = t.width;
                    skin->skinHeight = t.height;
                    skin->skinRgba.assign(t.pixels.begin(), t.pixels.end());
                }
                skin->slim = slim;
                if (variant.contains("cape")) {
                    skin->capeWidth = 64;
                    skin->capeHeight = 32;
                    skin->capeRgba.assign(64 * 32 * 4, 255);
                    for (int y = 0; y < 32; y++)
                        for (int x = 0; x < 64; x++) {
                            uint8_t* px = &skin->capeRgba[(static_cast<size_t>(y) * 64 + x) * 4];
                            const bool outer = x >= 1 && x < 11 && y >= 1 && y < 17;
                            const bool inner = x >= 12 && x < 22 && y >= 1 && y < 17;
                            px[0] = outer ? 30 : (inner ? 30 : 120);
                            px[1] = outer ? 200 : (inner ? 60 : 120);
                            px[2] = outer ? 40 : (inner ? 220 : 120);
                            px[3] = 255;
                        }
                }
                previewEntities.SetSkin("preview" + std::to_string(i), skin);
            } else {
                const uint64_t flags =
                    variant.contains("baby") ? static_cast<uint64_t>(entity::flag::Baby) : 0;

                std::string id = "minecraft:" + type;
                if (const size_t slash = type.find('/'); slash != std::string::npos)
                    id = type.substr(0, slash) + ":" + type.substr(slash + 1);
                previewEntities.AddEntity(
                    rid, static_cast<int64_t>(rid), id, p,
                    PERLIN_ENV("PERLIN_PREVIEW_ENTITY_PITCH")
                        ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_PITCH")))
                        : 0.0f,
                    yaw, yaw, flags, 1.0f, 0, 0, 0);
            }
            if (type == "fishing_hook")
                previewEntities.SetOwner(rid, 1000);
            if (const char* held = PERLIN_ENV("PERLIN_PREVIEW_HELD"))
                previewEntities.SetHeld(rid, held);
            if (variant == "aiming") {
                previewEntities.SetHeld(rid, "minecraft:bow");
                previewEntities.SetTarget(rid, -7);
                previewEntities.SetFlagsExtended(rid, entity::flag::FacingTargetToRangeAttack);
            }
            if (const char* armor = PERLIN_ENV("PERLIN_PREVIEW_ARMOR")) {
                static const char* parts[4] = {"helmet", "chestplate", "leggings", "boots"};
                for (int i = 0; i < 4; i++)
                    previewEntities.SetArmor(rid, i, std::string("minecraft:") + armor + "_" + parts[i]);
            }
            if (const char* nt = PERLIN_ENV("PERLIN_PREVIEW_NAMETAG"))
                previewEntities.SetNameTag(rid, nt);
            if (PERLIN_ENV("PERLIN_PREVIEW_SNEAK"))
                previewEntities.SetFlags(rid, entity::flag::Sneaking);
            if (PERLIN_ENV("PERLIN_PREVIEW_SWIM"))
                previewEntities.SetFlags(rid, entity::flag::Swimming | entity::flag::Sprinting);
            if (PERLIN_ENV("PERLIN_PREVIEW_GLIDE")) {
                previewEntities.SetFlags(rid, entity::flag::Gliding);
                previewEntities.SetArmor(rid, 1, "minecraft:elytra");
            }
            if (PERLIN_ENV("PERLIN_PREVIEW_DEAD"))
                previewEntities.Die(rid);
            if (PERLIN_ENV("PERLIN_PREVIEW_ENTITY_WALK") || PERLIN_ENV("PERLIN_PREVIEW_ENTITY_HEAD"))
                previewWalk.push_back(rid);
        }
    }
}
