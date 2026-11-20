#include "client/features/worldview/FrameParamsBuilder.hpp"
#include "client/ui/GuiScale.hpp"
#include <glm/gtc/type_ptr.hpp>
#include "util/Log.hpp"
#include "util/Env.hpp"

namespace perlin::worldview {
    FrameParamsBuilder::FrameParamsBuilder(Deps d)
        : uiAssets(d.uiAssets), serverPacks(d.serverPacks), debugOverlay(d.debugOverlay), modHost(d.modHost), replayStudio(d.replayStudio), view(d.view), options(d.options), renderer(d.renderer), levelRenderer(d.levelRenderer), hud(d.hud), hudState(d.hudState), fogDefs(d.fogDefs), fgActive(d.fgActive), camera(d.camera), player(d.player), blockLight(d.blockLight), tickDebt(d.tickDebt), rainLevel(d.rainLevel), previousRainLevel(d.previousRainLevel), thunderLevel(d.thunderLevel), previousThunderLevel(d.previousThunderLevel), loop(d.loop), hudPartial(d.hudPartial), shaderPackCache(d.shaderPackCache), replayCtl(d.replayCtl), session(d.session), sessionReady(d.sessionReady), menuOpen(std::move(d.menuOpen)) {}

    void FrameParamsBuilder::PrepareView(FrameContext& f) {
        auto& live = f.live;
        auto& worldTicks = f.worldTicks;
        auto& partial = f.partial;
        auto& rain = f.rain;
        auto& thunder = f.thunder;
        auto& dimension = f.dimension;
        auto& env = f.env;
        auto& handViewEffect = f.handViewEffect;
        auto& viewProjection = f.viewProjection;
        auto& frustum = f.frustum;
        auto& eye = f.eye;
        auto& graphics = f.graphics;
        auto& damage = loop->damage;
        partial = static_cast<float>(tickDebt);
        hudPartial = partial;
        rain = glm::mix(previousRainLevel, rainLevel, partial);
        thunder = glm::mix(previousThunderLevel, thunderLevel, partial) * rain;
        modHost.ModClientWeather(rain, thunder);

        EnvironmentInput envIn;
        envIn.ticks = worldTicks;
        envIn.rain = rain;
        envIn.thunder = thunder;
        if (live)
            envIn.dimension = static_cast<Dimension>(std::clamp(live->DimensionId(), 0, 2));
        dimension = envIn.dimension;
        env = SampleEnvironment(envIn);

        glm::mat4 bobMatrix(1.0f);
        if (live && modHost.ModHand().viewBobbing && camera.mode == PerspectiveMode::FirstPerson &&
            !modHost.CameraDetachActive())
            bobMatrix = BobViewMatrix(player.Bob(), player.PartialTick(), modHost.ModHand().bobStrength);
        const glm::mat4 hurtMatrix =
            sessionReady ? damage.ViewMatrix(partial, options.damageTilt) : glm::mat4(1.0f);
        camera.viewEffect = hurtMatrix * (modHost.ModHand().bobCamera ? bobMatrix : glm::mat4(1.0f));
        if (sessionReady && session)
            camera.viewEffect = session->Camera().ShakeMatrix() * camera.viewEffect;
        handViewEffect = hurtMatrix * (modHost.ModHand().bobHand ? bobMatrix : glm::mat4(1.0f));
        {
            camera.farPlane = std::max(static_cast<float>(options.renderDistance * 16) * 1.6f,
                                       static_cast<float>(GraphicsSettings::From(options).cloudRange * 16));
            camera.nearPlane = std::clamp(0.05f + (camera.farPlane - 1500.0f) * 0.00004f, 0.05f,
                                          camera.farPlane > 8000.0f ? 0.4f : 0.25f);
        }
        if (replayCtl.Playing()) {
            if (replayStudio.FovOverride() > 0)
                camera.fov = replayStudio.FovOverride();
            if (replayStudio.Roll() != 0.0f)
                camera.viewEffect =
                    glm::rotate(glm::mat4(1.0f), glm::radians(replayStudio.Roll()), glm::vec3(0.0f, 0.0f, 1.0f)) *
                    camera.viewEffect;
        }
        modHost.ApplyModCamera(camera.fov, camera.viewEffect);
        viewProjection = camera.ViewProjection();
        {
            std::lock_guard lock(modHost.modAdv.mutex);
            std::memcpy(modHost.modAdv.viewProjection.data(), &viewProjection[0][0], sizeof(float) * 16);
        }
        frustum = Frustum::FromViewProjection(viewProjection);
        eye = camera.EyePosition();
        graphics = GraphicsSettings::From(options);
    }

    FogState FrameParamsBuilder::ComputeFrameFog(FrameContext& f) {
        auto& live = f.live;
        auto& worldTicks = f.worldTicks;
        auto& underwater = f.underwater;
        auto& eyeInLava = f.eyeInLava;
        auto& eyeBiome = f.eyeBiome;
        auto& rain = f.rain;
        auto& thunder = f.thunder;
        auto& env = f.env;
        auto& eye = f.eye;
        auto& graphics = f.graphics;
        auto& preview = loop->preview;
        auto& waterVisionTime = loop->waterVisionTime;
        FogInput fogInput;
        fogInput.renderDistanceChunks = levelRenderer ? levelRenderer->EffectiveDistance() : options.renderDistance;
        fogInput.cloudRangeChunks = graphics.cloudRange;
        fogInput.forward = camera.Direction();
        fogInput.cameraY = eye.y;
        if (session && session->FlatWorld())
            fogInput.voidDarknessOnsetRange = 1.0f;
        fogInput.rain = rain;
        fogInput.thunder = thunder;
        fogInput.underwater = underwater;
        fogInput.inLava = eyeInLava;
        if (underwater) {
            const float t = static_cast<float>(waterVisionTime);
            fogInput.waterVision = waterVisionTime >= 600
                                       ? 1.0f
                                       : std::clamp(t / 100.0f, 0.0f, 1.0f) * 0.6f +
                                             std::clamp((t - 100.0f) / 500.0f, 0.0f, 1.0f) * 0.39999998f;
            struct WaterFog {
                const char* biome;
                uint32_t rgb;
            };
            static const WaterFog kWaterFog[] = {
                {"cherry_grove", 0x5db7ef},   {"dappled_forest", 0x375154}, {"deep_lukewarm_ocean", 0x041633},
                {"lukewarm_ocean", 0x041633}, {"mangrove_swamp", 0x4d7a60}, {"pale_garden", 0x556980},
                {"sulfur_caves", 0x17543c},   {"swamp", 0x232317},          {"warm_ocean", 0x041f33}};
            uint32_t rgb = 0x050533;
            std::string shortName = eyeBiome.starts_with("minecraft:") ? eyeBiome.substr(10) : eyeBiome;
            for (const WaterFog& w : kWaterFog)
                if (shortName == w.biome)
                    rgb = w.rgb;
            fogInput.waterFogColor = {((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f,
                                      (rgb & 255) / 255.0f};
            if (shortName == "swamp")
                fogInput.waterFogEndScale = 0.85f;
        }
        FogState fog = ComputeFog(env, fogInput);
        if (sessionReady && session) {
            if (std::chrono::steady_clock::now() - fogCheck > std::chrono::seconds(2)) {
                fogCheck = std::chrono::steady_clock::now();
                fogDefs.Configure(serverPacks.Directories());
                std::string key;
                for (const std::string& d : serverPacks.Directories())
                    key += d + "|";
                if (key != hudPackKey) {
                    hudPackKey = key;
                    uiAssets.ClearItemCache();
                    if (hud)
                        hud->Reload();
                }
            }
            const std::vector<std::string> fogStack = session->Camera().FogStack();
            if (!fogStack.empty()) {
                const FogMedium m = fogDefs.Resolve(fogStack, fogInput.inLava       ? 2
                                                              : fogInput.underwater ? 1
                                                                                    : 0);
                const float distance = static_cast<float>(fogInput.renderDistanceChunks * 16);
                if (m.start && m.end) {
                    const float scale = m.relative ? distance : 1.0f;
                    fog.environmentalStart = *m.start * scale;
                    fog.environmentalEnd = std::max(*m.end * scale, fog.environmentalStart + 0.01f);
                    fog.skyEnd = std::min(fog.skyEnd, std::max(fog.environmentalEnd, 1.0f));
                }
                if (m.color)
                    fog.color = glm::vec4(*m.color, fog.color.w);
            } else if (live &&
                       static_cast<Dimension>(std::clamp(live->DimensionId(), 0, 2)) == Dimension::Nether) {
                const std::string bn = eyeBiome.starts_with("minecraft:") ? eyeBiome.substr(10) : eyeBiome;
                glm::vec3 tint(0.2f, 0.05f, 0.05f);
                if (bn == "crimson_forest")
                    tint = glm::vec3(0.35f, 0.05f, 0.07f);
                else if (bn == "warped_forest")
                    tint = glm::vec3(0.05f, 0.25f, 0.22f);
                else if (bn == "basalt_deltas")
                    tint = glm::vec3(0.18f, 0.16f, 0.18f);
                else if (bn == "soul_sand_valley")
                    tint = glm::vec3(0.15f, 0.12f, 0.14f);
                fog.color = glm::vec4(tint, fog.color.w);
            }
        }

        modHost.ApplyModFog(fog);
        if (preview) {
            if (once++ == 20)
                Log("preview", "fog env {}..{} rd {}..{} sky {} rdchunks {} ticks {} sun {}",
                    fog.environmentalStart, fog.environmentalEnd, fog.renderDistanceStart,
                    fog.renderDistanceEnd, fog.skyEnd, options.renderDistance, worldTicks, env.sunAngle);
        }
        return fog;
    }

    void FrameParamsBuilder::CollectDynamicLights(FrameContext& f) {
        auto& live = f.live;
        auto& dyn = f.dyn;
        auto& dynMode = f.dynMode;
        auto& previewLevel = loop->previewLevel;
        auto& frameEntities = loop->frameEntities;
        dynMode = options.dynamicLights == "fancy" ? 2 : options.dynamicLights == "fast" ? 1 : 0;
        if (const char* dl = PERLIN_ENV("PERLIN_DYNLIGHTS"))
            dynMode = std::string(dl) == "fancy" ? 2 : std::string(dl) == "fast" ? 1 : 0;
        if (dynMode && (live || previewLevel)) {
            const std::vector<entity::Entity>& lightEntities = frameEntities;
            for (const entity::Entity& le : lightEntities)
                if (!le.isSelf)
                    dynlight::Collect(le, dyn);
            if (hudState.mode != ui::GameMode::Spectator) {
                const int own = std::max(
                    dynlight::ItemLevel(hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)].name),
                    dynlight::ItemLevel(hudState.offhand.name));
                if (own > 0)
                    dyn.push_back({glm::dvec3(camera.x, camera.y - 0.25, camera.z), static_cast<float>(own)});
            }
            dynlight::Select(dyn, glm::dvec3(camera.x, camera.y, camera.z), 8);
        }
    }

    void FrameParamsBuilder::BuildFrameParams(FrameContext& f) {
        const FogState fog = ComputeFrameFog(f);
        CollectDynamicLights(f);
        FillSettingsParams(f);
        FillPackAndLightParams(f);
        FillCameraParams(f, fog);
        FillUpscaleParams(f);
        FillSkyParams(f);
        FillCloudParams(f);
        FillOverlayParams(f);
        FillTimeAndTileParams(f);
        FillLightmapParams(f);
    }

    void FrameParamsBuilder::FillSettingsParams(FrameContext& f) {
        auto& dynMode = f.dynMode;
        auto& params = f.params;
        params.motionBlur = options.frameGen != "off" ? 0.0f : options.motionBlur;
        params.bloom = options.bloom;
        params.shadowQuality = options.shadowQuality;
        params.shadowDistance = options.shadowDistance;
        params.shadowEntities = options.shadowEntities;
        params.cameraMedium = view.headUnderwater ? 1 : (view.headInLava ? 2 : 0);
        params.waterSurfaceAbove = view.waterSurfaceAbove;
        params.renderDistance = levelRenderer ? levelRenderer->EffectiveDistance() : options.renderDistance;
        params.eyeSkyLight = loop->handSky;
        params.eyeBlockLight = loop->handBlock;
        std::snprintf(params.biome, sizeof(params.biome), "%s", f.eyeBiome.c_str());
        params.thunder = f.thunder;
        if (menuOpen() && options.menuBlur > 0)
            params.menuBlur = static_cast<float>(options.menuBlur * std::max(1, hud->OverlayScale()));
        if (const char* mb = PERLIN_ENV("PERLIN_MOTION_BLUR"))
            params.motionBlur = std::clamp(static_cast<float>(std::atof(mb)), 0.0f, 1.0f);
        params.dynMode = dynMode;
        params.playerHealth = hudState.health;
        params.playerMaxHealth = hudState.maxHealth;
        params.playerFood = static_cast<float>(hudState.food);
        params.playerAir = static_cast<float>(hudState.air);
        params.playerMaxAir = static_cast<float>(hudState.maxAir);
        params.playerArmor = hudState.armor;
        params.playerSpectator = hudState.mode == ui::GameMode::Spectator;
        params.playerBurning = hudState.burning;
        params.playerSneaking = player.Sneaking();
        params.playerSprinting = player.Sprinting();
        params.playerOnGround = player.State().onGround;
        params.playerElytra = player.Gliding();
        params.playerRiding = player.Riding();
        params.playerFeetInWater = player.InWater();
        params.playerSwimming = player.Swimming();
        params.firstPerson = camera.mode == PerspectiveMode::FirstPerson;
        const glm::vec3 look = camera.ViewDirection();
        params.playerLook[0] = look.x;
        params.playerLook[1] = look.y;
        params.playerLook[2] = look.z;
    }

    void FrameParamsBuilder::FillPackAndLightParams(FrameContext& f) {
        auto& dyn = f.dyn;
        auto& params = f.params;
        {
            const std::vector<ShaderSetting>& packSettingList =
                shaderPackCache.Settings(options.shaderPack);
            for (const ShaderSetting& st : packSettingList) {
                if (st.slot < 0 || st.slot >= 32)
                    continue;
                const auto found = options.shaderSettings.find(st.id);
                params.packSettings[st.slot] = static_cast<float>(
                    found == options.shaderSettings.end() ? st.def
                                                          : std::clamp(found->second, st.min, st.max));
            }
        }
        params.dynCount = static_cast<int>(dyn.size());
        for (size_t i = 0; i < dyn.size(); i++) {
            params.dynLights[i][0] = static_cast<float>(dyn[i].pos.x - camera.x);
            params.dynLights[i][1] = static_cast<float>(dyn[i].pos.y - camera.y);
            params.dynLights[i][2] = static_cast<float>(dyn[i].pos.z - camera.z);
            params.dynLights[i][3] = dyn[i].level;
        }
    }

    void FrameParamsBuilder::FillCameraParams(FrameContext& f, const FogState& fog) {
        auto& viewProjection = f.viewProjection;
        auto& eye = f.eye;
        auto& graphics = f.graphics;
        auto& params = f.params;
        std::copy_n(glm::value_ptr(viewProjection), 16, params.viewProjection);
        std::copy_n(glm::value_ptr(camera.SkyViewProjection()), 16, params.skyViewProjection);
        std::copy_n(glm::value_ptr(eye), 3, params.cameraPosition);
        std::copy_n(glm::value_ptr(fog.color), 4, params.fogColor);
        params.environmentalFogStart = fog.environmentalStart;
        params.environmentalFogEnd = fog.environmentalEnd;
        params.renderDistanceFogStart = fog.renderDistanceStart;
        params.renderDistanceFogEnd = fog.renderDistanceEnd;
        params.skyFogEnd = fog.skyEnd;
        params.cloudFogEnd = fog.cloudEnd;
        params.rgss = graphics.rgss;
        {
            glm::mat4 viewMatrix, projectionMatrix;
            camera.Matrices(viewMatrix, projectionMatrix);
            std::copy_n(glm::value_ptr(viewMatrix), 16, params.view);
            std::copy_n(glm::value_ptr(projectionMatrix), 16, params.projection);
            const glm::vec3 now(camera.x, camera.y, camera.z);
        if (!haveLastCamera) {
                lastCamera = now;
                haveLastCamera = true;
            }
            params.teleported = glm::distance(now, lastCamera) > 6.0f;
            lastCamera = now;
        }
    }

    void FrameParamsBuilder::FillUpscaleParams(FrameContext& f) {
        auto& dt = f.dt;
        auto& params = f.params;
        if (options.upscaler != "off") {
            float scale = static_cast<float>(options.upscaleCustom) / 100.0f;
            if (options.upscaleTargetFps > 0) {
        if (lastChange == std::chrono::steady_clock::time_point{})
                    lastChange = std::chrono::steady_clock::now();
                const double shownFrames = static_cast<double>(fgActive);
                frameMs += (static_cast<double>(dt) * 1000.0 / shownFrames - frameMs) * 0.05;
                const double targetMs = 1000.0 / static_cast<double>(options.upscaleTargetFps);
                const auto nowDyn = std::chrono::steady_clock::now();
                if (nowDyn - lastChange > std::chrono::milliseconds(1200)) {
                    if (frameMs > targetMs * 1.06 && dynamic > 0.34f) {
                        dynamic = std::max(0.33f, dynamic - 0.05f);
                        lastChange = nowDyn;
                    } else if (frameMs < targetMs * 0.85 && dynamic < 0.99f) {
                        dynamic = std::min(1.0f, dynamic + 0.05f);
                        lastChange = nowDyn;
                    }
                }
                scale = dynamic;
            }
            params.renderScale = scale;
            params.upscaler = options.upscaler == "temporal"  ? 3
                              : options.upscaler == "fsr1"    ? 2
                              : options.upscaler == "bicubic" ? 1
                                                              : 0;
            params.upscaleSharpness = static_cast<float>(options.upscaleSharpness) / 100.0f;
        }
        params.frameGen = fgActive;
        {
            const glm::mat4 handProjection = HandRenderer::Projection(70.0f, camera.aspect, camera.farPlane);
            std::copy_n(glm::value_ptr(handProjection), 16, params.handProjection);
            params.cameraNear = camera.nearPlane;
            params.cameraFar = camera.farPlane;
        }
    }

    void FrameParamsBuilder::FillSkyParams(FrameContext& f) {
        auto& dt = f.dt;
        auto& worldTicks = f.worldTicks;
        auto& rain = f.rain;
        auto& dimension = f.dimension;
        auto& env = f.env;
        auto& eye = f.eye;
        auto& params = f.params;
        params.sky.enabled = dimension == Dimension::Overworld;
        std::copy_n(glm::value_ptr(env.skyColor), 3, params.sky.skyColor);
        params.sky.sunAngle = env.sunAngle;
        params.sky.moonAngle = env.moonAngle;
        params.sky.starAngle = env.starAngle;
        params.sky.rainBrightness = 1.0f - rain;
        params.sky.starBrightness = env.starBrightness;
        std::copy_n(glm::value_ptr(env.sunriseColor), 4, params.sky.sunrise);
        params.sky.moonPhase = env.moonPhase;
        params.sky.darkDisc =
            eye.y < (session && session->FlatWorld() ? frame::kFlatHorizonHeight : frame::kHorizonHeight);
        params.sky.customCount = 0;
        debugOverlay.state.customSky = !options.customSkies ? "off in options"
                          : !levelRenderer || levelRenderer->CustomSky().Empty() ? "none loaded"
                          : !params.sky.enabled && dimension != Dimension::End ? "loaded, vanilla sky disabled here"
                                                                                : "loaded";
        params.sky.endSky = dimension == Dimension::End;
        params.sky.sunTexture = params.sky.moonTexture = -1;
        if (options.customSkies && levelRenderer && (params.sky.enabled || params.sky.endSky) &&
            !levelRenderer->CustomSky().Empty()) {
            sky::Set& custom = levelRenderer->CustomSky();
            const bool end = !params.sky.enabled;
            sky::Inputs skyIn;
            skyIn.worldTime = static_cast<int64_t>(worldTicks);
            constexpr float kTwoPi = 6.28318530717958647692f;
            skyIn.celestialAngle = end ? 0.0f : env.sunAngle / kTwoPi - std::floor(env.sunAngle / kTwoPi);
            skyIn.rain = end ? 0.0f : rain;
            skyIn.thunder = end ? 0.0f : f.thunder;
            skyIn.biome = f.eyeBiome;
            skyIn.eyeY = camera.y;
            skyIn.dt = dt;
            for (const sky::Draw& d : sky::Evaluate(end ? custom.endLayers : custom.layers, skyIn)) {
                if (params.sky.customCount >= SkyFrame::kMaxCustomLayers)
                    break;
                SkyFrame::CustomLayer& out = params.sky.custom[params.sky.customCount++];
                out.texture = d.texture;
                out.cubemap = d.cubemap;
                out.blend = static_cast<uint8_t>(d.blend);
                out.brightness = d.brightness;
                std::copy_n(glm::value_ptr(d.rotation), 16, out.rotation);
            }
            debugOverlay.state.customSky = custom.origin + ": " + std::to_string(custom.layers.size()) + " overworld, " +
                              std::to_string(custom.endLayers.size()) + " end layer(s), " +
                              std::to_string(params.sky.customCount) + " drawn now";
            if (!end) {
                params.sky.sunTexture = custom.sun.texture;
                params.sky.sunBlend = static_cast<uint8_t>(custom.sun.blend);
                params.sky.moonTexture = custom.moon.texture;
                params.sky.moonBlend = static_cast<uint8_t>(custom.moon.blend);
            }
        }
    }

    void FrameParamsBuilder::FillCloudParams(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& env = f.env;
        auto& graphics = f.graphics;
        auto& params = f.params;
        auto& startedAt = loop->startedAt;
        const glm::vec4 cloudColor = glm::round(glm::clamp(env.cloudColor, 0.0f, 1.0f) * 255.0f) / 255.0f;
        params.cloud.enabled = graphics.clouds && cloudColor.w > 0.0f;
        params.cloud.fancy = graphics.fancyClouds;
        params.cloud.rangeChunks = graphics.cloudRange;
        std::copy_n(glm::value_ptr(cloudColor), 4, params.cloud.color);
        params.cloud.height = env.cloudHeight;
        params.cloud.gameTicks = std::chrono::duration<double>(frameStart - startedAt).count() * 20.0;
        if (!f.live && !loop->preview) {
            static constexpr float kIdle[4] = {0.075f, 0.078f, 0.086f, 1.0f};
            params.sky.enabled = false;
            params.sky.endSky = false;
            params.sky.customCount = 0;
            params.cloud.enabled = false;
            std::copy_n(kIdle, 3, params.sky.skyColor);
            std::copy_n(kIdle, 4, params.fogColor);
        }
    }

    void FrameParamsBuilder::FillOverlayParams(FrameContext& f) {
        auto& params = f.params;
        auto& damage = loop->damage;
        params.crosshair = false;
        {
            int cw = 0, ch = 0;
            renderer->FramebufferSize(cw, ch);
            params.guiScale = ui::JavaGuiScale(cw, ch, options.guiScale);
        }
        params.screenTint[0] = 1.0f;
        params.screenTint[1] = 0.0f;
        params.screenTint[2] = 0.0f;
        params.screenTint[3] = options.damageTint ? damage.TintAlpha() : 0.0f;
        params.portalOverlay = view.portalTime;
        if (sessionReady && session) {
            const glm::vec4 fade = session->Camera().Tint();
            if (fade.a > 0.0f) {
                const float base = params.screenTint[3];
                const float total = fade.a + base * (1.0f - fade.a);
                for (int i = 0; i < 3; i++)
                    params.screenTint[i] =
                        (fade[i] * fade.a + params.screenTint[i] * base * (1.0f - fade.a)) /
                        std::max(total, 0.0001f);
                params.screenTint[3] = total;
            }
        }
    }

    void FrameParamsBuilder::FillTimeAndTileParams(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& worldTicks = f.worldTicks;
        auto& params = f.params;
        auto& startedAt = loop->startedAt;
        auto& frameCounter = loop->frameCounter;
        {
            if ((tileScan++ % 120) == 0 && levelRenderer->Blocks().Ready()) {
                waterTile = static_cast<float>(levelRenderer->Blocks().TileContaining("water_still"));
                flowTile = static_cast<float>(levelRenderer->Blocks().TileContaining("water_flow"));
                lavaTile = static_cast<float>(levelRenderer->Blocks().TileContaining("lava_still"));
                std::vector<uint8_t> foliage = levelRenderer->Blocks().FoliageFlags();
                if (foliage != lastFoliage) {
                    renderer->UploadTileFlags(foliage);
                    lastFoliage = std::move(foliage);
                }
            }
            params.waterTile = waterTile;
            params.waterFlowTile = flowTile;
            params.lavaTile = lavaTile;
            params.heldLight = static_cast<float>(options.heldLight);
            if (const char* h = PERLIN_ENV("PERLIN_HELDLIGHT"))
                params.heldLight = static_cast<float>(std::atoi(h));
        }
        params.timeSeconds = std::chrono::duration<float>(frameStart - startedAt).count();
        params.frameIndex = static_cast<uint32_t>(frameCounter++);
        {
            int64_t day = -1;
            int tick = -1;
            if (sessionReady && session && session->GetWorldTime(day, tick)) {
                params.worldDay = static_cast<int>(day);
                params.worldTick = tick;
            } else {
                params.worldDay = static_cast<int>(worldTicks / 24000.0);
                params.worldTick = static_cast<int>(std::fmod(worldTicks, 24000.0));
            }
        }
    }

    void FrameParamsBuilder::FillLightmapParams(FrameContext& f) {
        auto& env = f.env;
        auto& params = f.params;
        LightmapParams lightmap;
        lightmap.blockFactor = blockLight.Value() + 1.4f;
        lightmap.skyFactor = env.skyLightFactor;
        lightmap.skyLightColor = env.skyLightColor;
        lightmap.ambientColor = env.ambientColor;
        lightmap.blockLightTint = env.blockLightTint;
        lightmap.nightVisionColor = env.nightVisionColor;

        if (sessionReady && session) {
            bool active = false;
            int ticks = 0;
            for (const auto& fx : session->GetEffects())
                if (fx.id == 16 || fx.id == 26) {
                    if (!active || fx.ticks == -1 || (ticks != -1 && fx.ticks > ticks))
                        ticks = fx.ticks;
                    active = true;
                }
            if (active)
                lightmap.nightVisionFactor =
                    ticks == -1 || ticks > 200
                        ? 1.0f
                        : std::sin(static_cast<float>(ticks - 1) * 3.1415927f * 0.2f) * 0.3f + 0.7f;
        }
        lightmap.brightness = options.gamma;
        BuildLightmap(lightmap, params.lightmap);
    }
}
