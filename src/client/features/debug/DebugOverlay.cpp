#include "client/features/debug/DebugOverlay.hpp"
#include "client/systems/bindings/Keys.hpp"
#include "client/systems/i18n/Lang.hpp"
#include "client/systems/resources/Brand.hpp"
#include "client/systems/sprites/UiAssets.hpp"
#include "util/SystemInfo.hpp"
#include "util/Version.hpp"
#include "world/level/block/BlockPalette.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace perlin::debug {
    using Clock = std::chrono::steady_clock;
    Overlay::Overlay(Deps d)
        : options(d.options), blockPalette(d.blockPalette), uiAssets(d.uiAssets), input(d.input), inputManager(d.inputManager), camera(d.camera), chat(d.chat),
          hudState(d.hudState), entityRenderer(d.entityRenderer), sounds(d.sounds),
          levelRenderer(d.levelRenderer), renderer(d.renderer), hud(d.hud), aimHit(d.aimHit),
          hudTicks(d.hudTicks), framebufferSize(std::move(d.framebufferSize)),
          setClipboard(std::move(d.setClipboard)), applyModDebugLines(std::move(d.applyModDebugLines)) {}

    void Overlay::RayTargets(Level& level, glm::vec3 origin, glm::vec3 dir, double reach, ui::DebugInfo& info) {
        BlockPalette& palette = blockPalette;
        const auto classify = [&](uint16_t id) {
            const auto it = blockKinds.find(id);
            if (it != blockKinds.end())
                return it->second;
            const std::string name = palette.Name(id);
            int k = 2;
            if (name == "minecraft:air" || name == "minecraft:cave_air" || name == "minecraft:void_air" ||
                name.empty())
                k = 0;
            else if (name == "minecraft:water" || name == "minecraft:flowing_water" ||
                     name == "minecraft:lava" || name == "minecraft:flowing_lava")
                k = 1;
            return blockKinds[id] = k;
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

    void Overlay::Fill(ui::HudState& s, BedrockSession* live, Level* level) {
        s.debug = state.on && s.visible;
        if (!s.debug)
            return;
        const auto now = Clock::now();
        if (live && now - state.rateAt >= std::chrono::seconds(1)) {
            state.txRate = static_cast<int>(live->PacketsSent() - state.rateTx);
            state.rxRate = static_cast<int>(live->PacketsReceived() - state.rateRx);
            state.rateTx = live->PacketsSent();
            state.rateRx = live->PacketsReceived();
            state.rateAt = now;
        }
        ui::DebugInfo d;
        d.version = std::string("Perlin ") + kVersion + " (bedrock " + kBedrockVersion + "/vanilla)";
        d.fps = state.fps;
        d.maxFps = options.maxFps;
        d.vsync = options.vsync;
        d.fancyGraphics = options.fancyGraphics;
        const GraphicsSettings gfx = GraphicsSettings::From(options);
        d.clouds = gfx.clouds;
        d.fancyClouds = gfx.fancyClouds;
        d.biomeBlend = gfx.biomeBlendRadius;
        d.tx = state.txRate;
        d.rx = state.rxRate;
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
        d.skyLight = state.sky;
        d.customSky = state.customSky;
        d.blockLight = state.block;
        d.compiler = sysinfo::Compiler();
        d.memUsed = sysinfo::ProcessMemory();
        d.memPeak = sysinfo::ProcessPeakMemory();
        d.memTotal = sysinfo::TotalMemory();
        {
                        const auto now = std::chrono::steady_clock::now();
            if (renderer && now - vramQueryAt > std::chrono::milliseconds(500)) {
                vramQueryAt = now;
                vramKnown = renderer->GpuMemoryUsage(vramDedicated, vramShared);
            }
            d.vramKnown = vramKnown;
            d.vramDedicated = vramDedicated;
            d.vramShared = vramShared;
        }
        d.cpuThreads = sysinfo::CpuThreads();
        static const std::string cpu = sysinfo::CpuName();
        d.cpuName = cpu;
        int w = 0, h = 0;
        framebufferSize(w, h);
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
                if (now - state.heightAt > std::chrono::milliseconds(500)) {
                    state.heightAt = now;
                    BlockPalette& palette = blockPalette;
                    state.heightSurface = level->FindBlockBelow(bx, bz, 319, [&](uint16_t id) {
                        return palette.Name(id) != "minecraft:air";
                    }) + 1;
                }
                d.heightWorldSurface = d.heightMotionBlocking = state.heightSurface;
                d.biome = levelRenderer ? levelRenderer->Tints().NameForId(
                                              level->GetBiome(bx, static_cast<int>(std::floor(d.y)), bz))
                                        : "minecraft:plains";
                if (!d.biome.starts_with("minecraft:"))
                    d.biome = "minecraft:" + d.biome;
            }
            RayTargets(*level, eye, camera.Direction(), 20.0, d);
        }
        if (cachedLeft.empty() || now - cachedAt > std::chrono::milliseconds(100)) {
            cachedAt = now;
            cachedLeft.clear();
            cachedRight.clear();
            ui::BuildDebugLines(d, cachedLeft, cachedRight);
        }
        s.debugLeft = cachedLeft;
        s.debugRight = cachedRight;
        applyModDebugLines(s.debugLeft, s.debugRight);
    }

    void Overlay::HandleKeys(FrameContext& f, Level* previewLevel) {
        auto& live = f.live;
        const bool f3 = input.IsKeyDown(Key::F3) && !input.TextMode();
        const auto edge = [&](int key) {
            const bool down = f3 && input.IsKeyDown(key);
            const bool fired = down && !keyWas[key & 255];
            keyWas[key & 255] = down;
            if (fired)
                combo = true;
            return fired;
        };
        const auto say = [&](const std::string& text) {
            chat.AddMessage(i18n::Tr("perlin.state.prefix") + " " + text, hudTicks);
        };
        const auto num = [](double v) {
            char b[32];
            std::snprintf(b, sizeof b, "%.2f", v);
            return std::string(b);
        };
        const float feetY = camera.y - 1.62f;
        const std::string dimName = live ? (live->DimensionId() == 1   ? "minecraft:the_nether"
                                            : live->DimensionId() == 2 ? "minecraft:the_end"
                                                                       : "minecraft:overworld")
                                         : "minecraft:overworld";
        (void)dimName;
        if (edge('A')) {
            if (live)
                live->ReloadChunks();
            else if (levelRenderer)
                levelRenderer->RemeshAll();
            say(i18n::Tr("perlin.state.reload_chunks"));
        }
        {
            const bool cDown = f3 && input.IsKeyDown('C');
            if (edge('C')) {
                const std::string tp = "/tp @s " + num(camera.x) + " " + num(feetY) + " " + num(camera.z) +
                                       " " + num(camera.yaw - 90.0f) + " " + num(-camera.pitch);
                setClipboard(tp);
                say(i18n::Tr("perlin.state.copied_location", {tp}));
                cHeldSince = std::chrono::steady_clock::now();
                cHeld = true;
            }
            if (!cDown)
                cHeld = false;
            else if (cHeld && std::chrono::steady_clock::now() - cHeldSince > std::chrono::seconds(10))
                throw std::runtime_error(i18n::Tr("perlin.state.manual_crash"));
        }
        if (edge('B')) {
            state.hitboxes = !state.hitboxes;
            say(i18n::Tr(state.hitboxes ? "perlin.state.hitboxes_on" : "perlin.state.hitboxes_off"));
        }
        if (edge('D')) {
            chat.Clear();
            say(i18n::Tr("perlin.state.chat_cleared"));
        }
        if (edge('G')) {
            state.chunkBorders = !state.chunkBorders;
            say(i18n::Tr(state.chunkBorders ? "perlin.state.chunk_borders_on"
                                            : "perlin.state.chunk_borders_off"));
        }
        if (edge('H')) {
            state.advancedTooltips = !state.advancedTooltips;
            say(i18n::Tr(state.advancedTooltips ? "perlin.state.advanced_tooltips_on"
                                                : "perlin.state.advanced_tooltips_off"));
        }
        if (edge('I')) {
            if (aimHit.kind == AimHit::Kind::Block && (live || previewLevel)) {
                bool loaded = false;
                Level& l = live ? live->NetLevel() : *previewLevel;
                const std::string name =
                    blockPalette.Name(l.GetBlock(aimHit.bx, aimHit.by, aimHit.bz, loaded));
                const std::string cmd = "/setblock " + std::to_string(aimHit.bx) + " " +
                                        std::to_string(aimHit.by) + " " + std::to_string(aimHit.bz) + " " +
                                        name;
                setClipboard(cmd);
                say(i18n::Tr("perlin.state.copied_block", {cmd}));
            } else if (aimHit.kind == AimHit::Kind::Entity && live) {
                entity::Entity target;
                if (live->Entities().Find(aimHit.entity, target)) {
                    const std::string cmd = "/summon " + target.type + " " + num(target.pos.x) + " " +
                                            num(target.pos.y) + " " + num(target.pos.z);
                    setClipboard(cmd);
                    say(i18n::Tr("perlin.state.copied_entity", {cmd}));
                }
            } else {
                say(i18n::Tr("perlin.state.nothing_targeted"));
            }
        }
        if (edge('N')) {
            const ui::GameMode current = hudState.mode;
            const auto modeName = [](int m) {
                return m == 0 ? "survival" : m == 1 ? "creative" : m == 2 ? "adventure" : "spectator";
            };
            if (live) {
                if (current != ui::GameMode::Spectator) {
                    previousMode = static_cast<int>(current);
                    live->SendCommand("gamemode spectator");
                } else {
                    live->SendCommand(std::string("gamemode ") + modeName(previousMode));
                }
            }
            say(i18n::Tr("perlin.state.gamemode_toggle"));
        }
        if (edge('P')) {
            input.pauseOnLostFocus = !input.pauseOnLostFocus;
            say(i18n::Tr(input.pauseOnLostFocus ? "perlin.state.pause_on_lost_focus_on"
                                                : "perlin.state.pause_on_lost_focus_off"));
        }
        if (edge('T')) {
            if (levelRenderer)
                levelRenderer->ReloadResources();
            entityRenderer.ClearCaches();
            uiAssets.ClearItemCache();
            i18n::Lang::Get().Invalidate();
            sounds.Reload();
            if (hud)
                hud->Reload();
            say(i18n::Tr("perlin.state.reload_resources"));
        }
        if (edge(Key::F4)) {
            if (state.switcherIndex < 0) {
                const int cur = static_cast<int>(hudState.mode);
                state.switcherIndex = cur == 1 ? 0 : cur == 0 ? 1 : cur == 2 ? 2 : 3;
            }
            state.switcherIndex = (state.switcherIndex + 1) % 4;
        }
        if (edge(Key::Escape)) {
            state.softPaused = !state.softPaused;
            inputManager.SetBlocked(state.softPaused);
            say(i18n::Tr(state.softPaused ? "perlin.state.paused" : "perlin.state.unpaused"));
        }
        if (f3Was && !f3) {
            if (state.switcherIndex >= 0) {
                static const char* kModes[4] = {"creative", "survival", "adventure", "spectator"};
                if (live)
                    live->SendCommand(std::string("gamemode ") + kModes[state.switcherIndex]);
                state.switcherIndex = -1;
            } else if (!combo) {
                state.on = !state.on;
            }
            combo = false;
        }
        f3Was = f3;
        if (!f3)
            std::fill(std::begin(keyWas), std::end(keyWas), false);
        if (state.switcherIndex >= 0) {
            static const char* kKeys[4] = {"selectWorld.gameMode.creative", "selectWorld.gameMode.survival",
                                           "selectWorld.gameMode.adventure",
                                           "selectWorld.gameMode.spectator"};
            std::string line;
            for (int i = 0; i < 4; i++) {
                const std::string name = i18n::Tr(kKeys[i]);
                line += (i == state.switcherIndex ? "\u00a7"
                                                    "e[ " +
                                                        name +
                                                        " ]\u00a7"
                                                        "r"
                                                  : "\u00a7"
                                                    "7" +
                                                        name +
                                                        "\u00a7"
                                                        "r");
                if (i < 3)
                    line += "   ";
            }
            hudState.actionBar = i18n::Tr("perlin.gamemode.switcher") + ": " + line;
            hudState.actionBarAlpha = 1.0f;
        }
    }
}
