#include "client/PerlinFrame.hpp"
#include "util/Log.hpp"

namespace perlin {
    Perlin::Perlin(LaunchArgs args) : args(std::move(args)) {}

    Perlin::~Perlin() = default;

    int Perlin::FrameGenMultiplier() {
        if (options.frameGen == "auto") {
            double target = platform ? platform->DisplayRefreshHz() : 0.0;
            if (target < 30.0)
                target = 60.0;
            if (options.maxFps > 0)
                target = std::min(target, static_cast<double>(options.maxFps));
            return fgPacer.AutoTotal(target, Clock::now());
        }
        return options.frameGen == "4x" ? 4 : options.frameGen == "3x" ? 3 : options.frameGen == "2x" ? 2 : 1;
    }

    void Perlin::RenderThreadMain() {
        if (!renderer->Initialize(*platform, options)) {
            Log("render", "renderer initialisation failed");
            platform->isRunning = false;
            return;
        }
        InitRenderThread();
#ifdef __EMSCRIPTEN__
        RunWebMainLoop([this] { return renderRunning && platform->isRunning ? (FrameStep(), true) : false; },
                       [this] { TearDownRenderThread(); });
#else
        while (renderRunning && platform->isRunning)
            FrameStep();
        if (exportJob.active)
            ExportFinish(true, "");
        if (replayCtl.Playing()) {
            SaveTimeline();
            if (replayUi.dirty) {
                std::string saveError;
                replayCtl.SaveMarkers(saveError);
            }
        }
        TearDownRenderThread();
#endif
    }

    void Perlin::InitRenderThread() {
        if (args.hidden) {
            renderer->SetForcedSize(1280, 720);
            renderer->SetExportTarget(true);
        }
        renderReady = true;
        StartSampleProfiler();
        levelRenderer = std::make_unique<LevelRenderer>(*renderer, options);
        handRenderer.Initialize(*renderer);
        entityRenderer.Initialize(*renderer);
        entityRenderer.SetItemRenderer(&handRenderer);

        camera.fov = options.fov;
        camera.nearPlane = 0.05f;
        camera.farPlane = std::max(static_cast<float>(options.renderDistance * 16 * 4),
                                   static_cast<float>(GraphicsSettings::From(options).cloudRange * 16));
        loop = std::make_unique<FrameLoop>(options);
        if (loop->preview) {
            if (const char* show = PERLIN_ENV("PERLIN_PREVIEW_HUD"))
                options.showHud = std::atoi(show) != 0;
            camera.x = 24.0f;
            camera.y = 74.0f;
            camera.z = -22.0f;
            camera.yaw = 100.0f;
            camera.pitch = -6.0f;
            if (const char* c = PERLIN_ENV("PERLIN_PREVIEW_CAM"))
                std::sscanf(c, "%f,%f,%f,%f,%f", &camera.x, &camera.y, &camera.z, &camera.yaw, &camera.pitch);
            if (const char* m = PERLIN_ENV("PERLIN_PREVIEW_PERSPECTIVE"))
                camera.mode = static_cast<PerspectiveMode>(std::atoi(m) % 3);
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_TIME"))
                loop->previewTicks = std::atof(t);
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_SHOT"))
                loop->previewShot = t;
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_WAIT"))
                loop->previewWait = std::atof(t);
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_SWING"))
                handRenderer.ForceSwingProgress(static_cast<float>(std::atof(t)));
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_SLOT"))
                previewSlot = std::clamp(std::atoi(t), 0, 8);
            if (PERLIN_ENV("PERLIN_PREVIEW_DEBUG"))
                debug.on = true;
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_CHAT")) {
                std::string all = t;
                size_t from = 0;
                while (from <= all.size()) {
                    size_t bar = all.find('|', from);
                    if (bar == std::string::npos)
                        bar = all.size();
                    chat.AddMessage(all.substr(from, bar - from), UINT64_MAX / 2);
                    from = bar + 1;
                }
            }
            if (const char* t = PERLIN_ENV("PERLIN_PREVIEW_CHATOPEN")) {
                std::vector<ui::CommandInfo> fake;
                for (const char* n :
                     {"time", "tp", "teleport", "tell", "title", "tag", "tellraw", "gamemode", "give"}) {
                    ui::CommandInfo c;
                    c.name = n;
                    c.overloads.push_back(
                        {ui::CommandParam{"target", {}, true, false},
                         ui::CommandParam{"mode", {"survival", "creative", "adventure"}, false, true}});
                    fake.push_back(std::move(c));
                }
                chat.SetCommands(std::move(fake));
                chat.Open(t, detail::MsNow());
                chatOpenedAt = Clock::now() - std::chrono::seconds(5);
                input.SetTextMode(true);
            }
        }
    }

    void Perlin::TearDownRenderThread() {
        audioEngine.Close();
        StopSampleProfiler();
        entityRenderer.Shutdown();
        handRenderer.Shutdown();
        if (hud)
            hud->Shutdown();
        hud.reset();
        levelRenderer.reset();
        renderer->Shutdown();
    }

    void Perlin::FrameStep() {
        FrameContext f;
        BeginFrame(f);
        ImportDroppedFiles();
        UpdateLook(f);
        UpdateCameraEffects();
        TrackDamage();
        UpdateSession(f);
        UpdateViewSettings(f);
        UpdateChat(f);
        UpdateGameInput(f);
        UpdatePreviewWorld(f);
        UpdateThirdPersonDistance(f.live, f.dt);
        SampleEyeEnvironment(f);
        FillFrameHud(f);
        HandleDebugKeys(f);
        FillDebug(hudState, f.live, f.live ? &f.live->NetLevel() : loop->previewLevel.get());
        UpdateAudio(f.live, detail::SecondsSince(loop->startedAt, f.frameStart), f.dt);
        TickGame(f);
        if (args.benchmarkSeconds > 0 && !benchmarkCliStarted) {
            benchmarkCliStarted = true;
            bench::Settings s;
            s.seconds = args.benchmarkSeconds;
            if (args.benchmarkSeed)
                s.seed = args.benchmarkSeed;
            s.output = args.benchmarkOut;
            s.exitWhenDone = true;
            StartBenchmark(std::move(s));
        }
        UpdateBenchmark(f);
        PrepareView(f);
        BuildFrameParams(f);
        RenderLevel(f);
        HandleLiveScreenshot(f);
        RenderEntities(f);
        RenderHand(f);
        UpdateHudAndScreens(f);
        UpdateTouch(f);
        EndFrame(f);
        PaceFrame(f);
    }

    void Perlin::BeginFrame(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& dt = f.dt;
        auto& zoom = loop->zoom;
        auto& previous = loop->previous;
        auto& startedAt = loop->startedAt;
        auto& frames = loop->frames;
        if (levelUnloadPending) {
            levelUnloadPending = false;
            UnloadServerPacks(true);
        }
#ifdef __EMSCRIPTEN__
        platform->PumpFrame();
#endif
        frameStart = Clock::now();
        if ((frames & 15) == 0)
            i18n::Lang::Get().Configure(ui::UiAssets::Instance().VanillaRoot(), paths::DataDir(),
                                        options.language);
        // PERLIN_SHOT=<file.bmp>@<seconds>: capture our own framebuffer on any screen, then quit
        if (static const char* anyShot = PERLIN_ENV("PERLIN_SHOT"); anyShot) {
            static bool anyShotDone = false;
            static int anyShotFrames = 0;
            const std::string spec = anyShot;
            const size_t at = spec.rfind('@');
            const double when = at == std::string::npos ? 15.0 : std::atof(spec.c_str() + at + 1);
            if (!anyShotDone && std::chrono::duration<double>(frameStart - startedAt).count() > when) {
                anyShotDone = true;
                renderer->RequestScreenshot(spec.substr(0, at));
            } else if (anyShotDone && ++anyShotFrames > 8) {
                platform->isRunning = false;
            }
        }
        dt = std::chrono::duration<float>(frameStart - previous).count();
        previous = frameStart;
        fgActive = FrameGenMultiplier();
        if (dt <= 0.0f || dt > frame::kMaxFrameDelta)
            dt = frame::kDefaultFrameDelta;

        input.SetPadDeadzone(options.padDeadzone);
        PollControl();
        inputManager.Poll();
        if (exportJob.active)
            ExportPreFrame(frameStart, dt);
        if (replayCtl.Playing())
            camera.mode = PerspectiveMode::FirstPerson;
        else if (inputManager.Pressed(Action::CyclePerspective))
            camera.CycleMode();
        zoom.SetConfig(options.zoom);
        PollHotReload();
        if (fontsDownloaded.exchange(false))
            ui::UiAssets::Instance().LoadFont(paths::DataDir());
    }

    void Perlin::ImportDroppedFiles() {
        namespace fsys = std::filesystem;
        for (const std::string& dropped : platform->TakeDropped()) {
            std::error_code e;
            const fsys::path src(dropped);
            const std::string ext = str::Lower(src.extension().string());
            const bool onShaders = pause.shown && pause.view == 1 && optionsUi.page == 8;
            const bool onWardrobe = pause.shown && pause.view == 5 && ext == ".png";
            const bool dropIsCape = str::Lower(src.filename().string()).contains("cape");
            const char* sub = onWardrobe       ? (dropIsCape ? "capes" : "skins")
                              : ext == ".wasm" ? "mods"
                              : onShaders      ? "shaderpacks"
                                               : "texturepacks";
            if (ext == ".perlinreplay") {
                ImportReplayFile(dropped);
                continue;
            }
            if (!onWardrobe && ext != ".wasm" && ext != ".zip" && ext != ".mcpack" &&
                !fsys::is_directory(src, e))
                continue;
            const fsys::path destDir = fsys::path(paths::DataDir()) / sub;
            fsys::create_directories(destDir, e);
            fsys::copy(src, destDir / src.filename(),
                       fsys::copy_options::overwrite_existing | fsys::copy_options::recursive, e);
            chat.AddMessage(e ? "Could not add " + src.filename().string()
                              : "Added " + src.filename().string() + " to " + sub,
                            hudAnim.ticks);
            packUi.foundAt = {};
            wardrobe.listedAt = {};
            optionsUi.signature.clear();
        }
    }

    void Perlin::UpdateLook(FrameContext& f) {
        auto& dt = f.dt;
        auto& zoom = loop->zoom;
        auto& scopeFov = loop->scopeFov;
        inputManager.SetServerLocks(sessionReady && session &&
                                            session->GetState() == BedrockSession::State::InGame
                                        ? session->InputLocks()
                                        : 0u);
        const LookDelta look =
            zoom.Update(dt, inputManager.IsDown(Action::Zoom), inputManager.Scroll(), inputManager.Look());
        {
            auto& lookMods = mods::ModManager::Instance();
            bool lookTaken = false;
            if (lookMods.WantsEvent(abi::Event::Look)) {
                abi::EventLook ev;
                ev.dx = look.yaw;
                ev.dy = look.pitch;
                ev.dt = dt;
                const glm::vec3 dir = camera.Direction();
                ev.dirX = dir.x;
                ev.dirY = dir.y;
                ev.dirZ = dir.z;
                const physics::State& ps = player.State();
                ev.vx = ps.vx;
                ev.vy = ps.vy;
                ev.vz = ps.vz;
                ev.keys = (inputManager.IsDown(Action::Forward) ? 1u : 0u) |
                          (inputManager.IsDown(Action::Back) ? 2u : 0u) |
                          (inputManager.IsDown(Action::Left) ? 4u : 0u) |
                          (inputManager.IsDown(Action::Right) ? 8u : 0u) |
                          (inputManager.IsDown(Action::Jump) ? 16u : 0u) |
                          (inputManager.IsDown(Action::Sneak) ? 32u : 0u) |
                          (inputManager.IsDown(Action::Sprint) ? 64u : 0u);
                ev.gliding = player.Gliding() && !ps.onGround && !ps.inWater;
                ev.underwater = headUnderwater;
                ev.thirdPerson = camera.mode != PerspectiveMode::FirstPerson;
                ev.onGround = ps.onGround;
                {
                    std::lock_guard lock(modAdv.mutex);
                    modAdv.lookTaken = false;
                }
                static float glideLostFor = 0.0f;
                glideLostFor = ev.gliding ? 0.0f : glideLostFor + dt;
                abi::Writer lw;
                lw.write(ev);
                lookMods.Dispatch(abi::Event::Look, lw, 20.0);
                if (glideLostFor > 0.35f) {
                    std::lock_guard lock(modAdv.mutex);
                    modAdv.cameraUntil = {};
                    modAdv.poseUntil = {};
                    modAdv.lookTaken = false;
                }
                std::lock_guard lock(modAdv.mutex);
                if (modAdv.lookTaken) {
                    lookTaken = true;
                    const glm::dvec3 d = modAdv.lookDir;
                    camera.yaw = static_cast<float>(glm::degrees(std::atan2(d.z, d.x)));
                    camera.pitch =
                        std::clamp(static_cast<float>(glm::degrees(std::asin(std::clamp(d.y, -1.0, 1.0)))),
                                   -89.9f, 89.9f);
                    modAdv.lookTaken = false;
                }
            }
            if (!lookTaken)
                camera.Rotate(look.yaw, look.pitch);
        }
        {
            const float target =
                handRenderer.Scoping() && camera.mode == PerspectiveMode::FirstPerson ? 0.1f : 1.0f;
            scopeFov += (target - scopeFov) * (1.0f - std::pow(0.5f, dt * 20.0f));
        }
    }

    void Perlin::UpdateCameraEffects() {
        if (sessionReady && session) {
            CameraEffects& fx = session->Camera();
            fx.Tick({glm::vec3(camera.x, camera.y, camera.z), camera.yaw - 90.0f, -camera.pitch});
            if (fx.PoseActive()) {
                const CameraPose pose = fx.Pose();
                camera.x = pose.position.x;
                camera.y = pose.position.y;
                camera.z = pose.position.z;
                camera.yaw = pose.yaw + 90.0f;
                camera.pitch = glm::clamp(-pose.pitch, -89.0f, 89.0f);
            }
        }
    }

    void Perlin::TrackDamage() {
        auto& damage = loop->damage;
        auto& lastHurtCount = loop->lastHurtCount;
        auto& lastHurtSerial = loop->lastHurtSerial;
        auto& hurtSession = loop->hurtSession;
        if (sessionReady && session) {
            const uint32_t hurts = session->HurtCount();
            const auto source = session->LatestHurtSource();
            if (hurtSession != session.get()) {
                hurtSession = session.get();
                lastHurtCount = hurts;
                lastHurtSerial = source.serial;
            }
            const auto bearing = [&] {
                if (!source.known)
                    return static_cast<float>(std::rand() % 2) * 180.0f;
                const float javaYaw = camera.yaw - 90.0f;
                return static_cast<float>(std::atan2(source.dz, source.dx) * 57.29577951308232) - javaYaw;
            };
            if (hurts != lastHurtCount) {
                lastHurtCount = hurts;
                lastHurtSerial = source.serial;
                damage.Hurt(bearing());
            } else if (source.serial != lastHurtSerial) {
                lastHurtSerial = source.serial;
                if (source.known)
                    damage.Redirect(bearing());
            }
        } else {
            damage = DamageEffect();
        }
    }

    void Perlin::UpdateSession(FrameContext& f) {
        auto& dt = f.dt;
        auto& width = f.width;
        auto& height = f.height;
        auto& live = f.live;
        auto& previewLevel = loop->previewLevel;
        auto& frameEntities = loop->frameEntities;

        FramebufferSizeEffective(width, height);
        if (width > 0 && height > 0)
            camera.aspect = static_cast<float>(width) / static_cast<float>(height);

        PumpReplay(dt);
        live = sessionReady && session && !session->Transferring() ? session.get() : nullptr;
        if (live) {
            const physics::State& viewerState = player.State();
            live->Entities().SetViewer(live->GetRuntimeId(), {viewerState.x, viewerState.y, viewerState.z});
            live->Entities().Snapshot(frameEntities);
        } else {
            previewEntities.Snapshot(frameEntities);
        }
        PumpMods(live, live ? &live->NetLevel() : previewLevel.get(), hud.get(), dt);
        if (!live)
            return;
        live->Update();
        live->PumpRequests(256);
        UpdateRiding(*live, frameEntities);
        player.SetToggleModes(options.sprintMode == "toggle", options.sneakMode == "toggle");
        hudState.moveStatus.clear();
        UpdateEatingAndDrinking(f, *live);
        if (!replayCtl.Playing())
            player.Update(*live, inputManager, camera, levelRenderer->Blocks(), dt);
    }

    void Perlin::UpdateRiding(BedrockSession& live, const std::vector<entity::Entity>& frameEntities) {
        const int64_t rideUid = live.RidingUid();
        std::vector<physics::Box> solid;
        entity::Entity seatVehicle;
        bool seated = false;
        if (rideUid != 0 && live.Entities().FindUnique(rideUid, seatVehicle)) {
            const glm::dvec3 seat = entityRenderer.SeatPosition(seatVehicle);
            player.SetRidePosition(true, seat.x, seat.y, seat.z);
            seated = true;
        }
        {
            static bool wasSeated = false;
            static entity::Entity lastSeat;
            if (seated) {
                wasSeated = true;
                lastSeat = seatVehicle;
            } else if (wasSeated) {
                wasSeated = false;
                if (lastSeat.type == "minecraft:cushion")
                    player.ModTeleport(lastSeat.pos.x, lastSeat.pos.y + 0.26, lastSeat.pos.z, false);
            }
        }
        if (!seated)
            player.SetRidePosition(false, 0, 0, 0);
        const double feetY = player.State().y;
        std::vector<physics::Box> mobs;
        for (const entity::Entity& e : frameEntities) {
            if (e.runtimeId == live.GetRuntimeId() || (seated && e.uniqueId == rideUid))
                continue;
            // riptide hits only mobs (MobFlagComponent, 0x14B316910): actors with health stand in for it
            if (e.isPlayer || e.attributes.contains("minecraft:health")) {
                float w = 0.6f, h = 1.8f;
                entityRenderer.CollisionBoxOf(e.type, w, h);
                if (e.boxWidth > 0.0f && e.boxHeight > 0.0f) {
                    w = e.boxWidth;
                    h = e.boxHeight;
                }
                const float x = static_cast<float>(e.pos.x), y = static_cast<float>(e.pos.y),
                            z = static_cast<float>(e.pos.z);
                mobs.push_back({x - w * 0.5f, y, z - w * 0.5f, x + w * 0.5f, y + h, z + w * 0.5f});
            }
            // stackable actors are solid to movers (0x143A10780); falling blocks only once below the feet (0x143A10AC0)
            const bool fallingBlock = e.type == "minecraft:falling_block";
            if (!e.Has(entity::flag::Stackable) && !fallingBlock && e.type != "minecraft:cushion")
                continue;
            float w = 0.999f, h = 0.249f;
            entityRenderer.CollisionBoxOf(e.type, w, h);
            if (e.boxWidth > 0.0f && e.boxHeight > 0.0f) {
                w = e.boxWidth;
                h = e.boxHeight;
            }
            // aabb rebuilt in float like the game: centre -/+ w*0.5, top = bottom + h
            const float x = static_cast<float>(e.pos.x), y = static_cast<float>(e.pos.y),
                        z = static_cast<float>(e.pos.z);
            const float top = y + h;
            if (fallingBlock && !e.Has(entity::flag::Stackable) && top > static_cast<float>(feetY))
                continue;
            solid.push_back({x - w * 0.5f, y, z - w * 0.5f, x + w * 0.5f, top, z + w * 0.5f});
        }
        SetEntityCollisionBoxes(std::move(solid));
        player.SetMobBoxes(std::move(mobs));
    }

    void Perlin::UpdateEatingAndDrinking(FrameContext& f, BedrockSession& live) {
        auto& earlyItemUse = f.earlyItemUse;
        auto& blockedFoodUse = f.blockedFoodUse;
        const auto& held = hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)];
        const auto& name = held.name;
        const bool use =
            input.Captured() && inputManager.IsDown(Action::Use) && detail::HasAmmo(hudState, &live, name);
        const bool drink = name == "minecraft:potion" || name == "minecraft:milk_bucket" ||
                           name == "minecraft:honey_bottle" || name == "minecraft:ominous_bottle";
        const bool always = drink || name == "minecraft:golden_apple" ||
                            name == "minecraft:enchanted_golden_apple" || name == "minecraft:chorus_fruit" ||
                            name == "minecraft:suspicious_stew";
        const bool eating =
            hudState.Survival() && HandRenderer::IsFood(name) && (always || hudState.food < 20);
        blockedFoodUse = HandRenderer::IsFood(name) && !eating;
        bool aimInteracts = false;
        if (aimHit.kind == AimHit::Kind::Block) {
            bool loaded = false;
            const uint16_t id = live.NetLevel().GetBlock(aimHit.bx, aimHit.by, aimHit.bz, loaded);
            aimInteracts =
                !loaded || (blockuse::IsInteractive(BlockPalette::Instance().Name(id)) && !player.Sneaking());
        } else if (aimHit.kind == AimHit::Kind::Entity) {
            entity::Entity target;
            aimInteracts = !live.Entities().Find(aimHit.entity, target) || frame::IsInteractable(target.type);
        }
        if (use && eating && !aimInteracts && inputManager.Pressed(Action::Use) &&
            !DispatchModInteract(abi::InteractKind::UseItem, 0, 0, 0, 0, 0)) {
            const float eye[3] = {static_cast<float>(camera.x), static_cast<float>(camera.y),
                                  static_cast<float>(camera.z)};
            earlyItemUse = live.SendUseItemInAir(eye, true);
            usingItem = earlyItemUse;
        }
        if (!use && usingItem) {
            const float eye[3] = {static_cast<float>(camera.x), static_cast<float>(camera.y),
                                  static_cast<float>(camera.z)};
            ReleaseHeldItem(&live, held, eye);
        }
        const uint32_t useTicks = eating ? (name == "minecraft:dried_kelp"     ? 16
                                            : name == "minecraft:honey_bottle" ? 40
                                                                               : 32)
                                         : 0;
        player.SetItemSlow(use && (eating || name == "minecraft:bow" || name == "minecraft:trident" ||
                                   name == "minecraft:spyglass"),
                           useTicks, inputManager.Pressed(Action::Use));
    }

    // every release of a held use, from both input paths: the server's releaseUsing launches the riptide on any
    void Perlin::ReleaseHeldItem(BedrockSession* live, const ui::HudItem& held, const float eye[3]) {
        usingItem = false;
        if (!live)
            return;
        live->SendReleaseItem(eye);
        // riptide is enchantment 30
        if (held.name != "minecraft:trident")
            return;
        // rain at the player, Weather 0x145E27B00: overworld, (cur - prev) + prev > 0.2f, the column
        // clear from the probe up, biome precipitation and temperature > 0.15f (0x145D3B680).
        // the probe is floor(position + box height); the network position is at eye level
        const auto rainingOnPlayer = [&] {
            if (live->DimensionId() != 0)
                return false;
            const float delta = rainLevel - previousRainLevel;
            if (!(delta + previousRainLevel > 0.2f))
                return false;
            const physics::State& pst = player.State();
            const float eyeY = static_cast<float>(pst.y) + static_cast<float>(physics::kStandEye);
            const int x = static_cast<int>(std::floor(static_cast<float>(pst.x)));
            const int y = static_cast<int>(std::floor(eyeY + pst.collisionHeight));
            const int z = static_cast<int>(std::floor(static_cast<float>(pst.z)));
            auto& level = live->NetLevel();
            for (int top = 319; top >= y; --top) {
                bool loaded = false;
                const uint16_t id = level.GetBlock(x, top, z, loaded);
                if (!loaded || weather.BlocksRain(level, id) || level.GetLiquid(x, top, z) != 0)
                    return false;
            }
            const BiomeTints& tints = levelRenderer->Tints();
            float temperature = 0.5f, downfall = 0.5f;
            if (!tints.Ready() ||
                !tints.Climate(tints.NameForId(level.GetBiome(x, y, z)), temperature, downfall))
                return false;
            // TODO: block precipitation_interactions, biome rain flag and the temperature noise/altitude
            // terms (0x145D3B680) are approximated by the renderer's blocker list and climate data
            return downfall > 0.0f && temperature > 0.15f;
        };
        const bool raining = rainingOnPlayer();
        for (const auto& [id, level] : held.enchants)
            if (id == 30)
                player.ReleaseTrident(level, raining);
    }

    void Perlin::ApplyRendererSwitch() {
        if (options.renderer != runningRenderer && optionsUi.drag < 0 && renderer) {
            runningRenderer = options.renderer;
            options.Save();
            if (!renderer->RequestRendererSwitch(options.renderer))
                chat.AddMessage("Renderer cannot be changed right now", hudAnim.ticks);
        }
        if (renderer && !renderer->RendererSwitchPending()) {
            // a switch that fell back to the old api must not leave the options claiming the new one
            const std::string active = renderer->ActiveRendererId();
            if (!active.empty() && options.renderer != "auto" && options.renderer != active &&
                options.renderer == runningRenderer) {
                chat.AddMessage("Could not switch renderer; staying on " + renderer->RendererName(),
                                hudAnim.ticks);
                options.renderer = runningRenderer = active;
                options.Save();
            }
        }
    }

    void Perlin::UpdateViewSettings(FrameContext& f) {
        auto& live = f.live;
        auto& zoom = loop->zoom;
        auto& scopeFov = loop->scopeFov;
        auto& preview = loop->preview;
        float movementFov = player.FovModifier();
        if (preview && !live) {
            if (const char* movement = PERLIN_ENV("PERLIN_PREVIEW_FOV_MOVEMENT")) {
                float speed = 0.1f, walk = 0.1f;
                int fly = 0, slowness = -1;
                if (std::sscanf(movement, "%f,%f,%d,%d", &speed, &walk, &fly, &slowness) >= 2)
                    movementFov = MovementFovTarget(speed, walk, fly != 0, slowness);
            }
        }
        camera.fov = zoom.Fov(options.fov * (options.fovEffects ? movementFov : 1.0f)) * scopeFov;
        if (sessionReady && session)
            camera.fov = session->Camera().Fov(camera.fov);
        if (const char* g = PERLIN_ENV("PERLIN_GLASS"))
            options.glass = g;
        ApplyRendererSwitch();
        {
            static auto lastTrim = std::chrono::steady_clock::now();
            const auto nowTrim = std::chrono::steady_clock::now();
            if (nowTrim - lastTrim > std::chrono::seconds(45)) {
                lastTrim = nowTrim;
                std::thread([] { TrimMemory(false); }).detach();
            }
        }
        levelRenderer->SetBetterGrass(options.betterGrass ? 1 : 0);
        levelRenderer->SetGlassMode(options.glass == "connected"        ? 1
                                    : options.glass == "connectedClear" ? 2
                                    : options.glass == "clear"          ? 3
                                                                        : 0);
        levelRenderer->ReloadAssetsIfNeeded();
        PollJavaPack(live);
    }

    void Perlin::UpdateChat(FrameContext& f) {
        auto& live = f.live;
        auto& preview = loop->preview;
        if (live)
            for (const std::string& line : live->TakeChat())
                if (!FilterIncomingChat(line))
                    chat.AddMessage(line, hudAnim.ticks);
        MergePluginCommands(live);
        if (live) {
            if (chat.IsOpen() && Clock::now() - playersAt > std::chrono::seconds(1)) {
                playersAt = Clock::now();
                std::vector<std::string> names;
                for (const auto& p : live->PlayerList())
                    names.push_back(p.name);
                chat.SetPlayers(std::move(names));
            }
        }
        if ((live || preview) && !replayCtl.Playing()) {
            if (!chat.IsOpen()) {
                if (inputManager.Pressed(Action::Chat) || inputManager.Pressed(Action::Command)) {
                    chat.Open(inputManager.Pressed(Action::Chat) ? "" : "/", detail::MsNow());
                    chatOpenedAt = Clock::now();
                    input.SetTextMode(true);
                }
            } else {
                auto typed = inputManager.TakeTyped();
                if (Clock::now() - chatOpenedAt < std::chrono::milliseconds(150))
                    typed.erase(std::remove_if(typed.begin(), typed.end(),
                                               [](const InputManager::Typed& t) {
                                                   return t.character &&
                                                          (t.key == 't' || t.key == 'T' || t.key == '/');
                                               }),
                                typed.end());
                const ui::ChatLog::Result r = chat.Type(typed, inputManager.Scroll());
                if (r.send && InterceptOutgoing(live, r.text)) {
                } else if (r.send) {
                    if (live && r.text[0] == '/')
                        live->SendCommand(r.text.substr(1));
                    else if (live)
                        live->SendChat(r.text);
                    else
                        chat.AddMessage(
                            i18n::Tr("chat.type.text", {i18n::Tr("perlin.chat.preview_player"), r.text}),
                            hudAnim.ticks);
                }
                if (r.close) {
                    chat.Close();
                    input.SetTextMode(false);
                }
            }
        } else if (chat.IsOpen()) {
            chat.Close();
            input.SetTextMode(false);
        }
    }

    void Perlin::EndFrame(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& titleAt = loop->titleAt;
        auto& animationAt = loop->animationAt;
        auto& animationStep = loop->animationStep;
        auto& frames = loop->frames;
        if (inputManager.Pressed(Action::Screenshot))
            TakeScreenshot();
        if (exportJob.active)
            ExportPostRender();
        PublishControl();
        {
            frame::ProfileScope p(frame::Profiler::EndFrame);
            renderer->EndFrame();
        }
        if (!exportJob.active) {
            const auto realEnd = Clock::now();
            const double work = std::chrono::duration<double, std::milli>(realEnd - frameStart).count() -
                                fgPacer.TakeWaitedMs();
            fgPacer.RealFrame(realEnd, fgActive, work);
            if (fgActive > 1) {
                const auto idleUntil =
                    options.maxFps > 0
                        ? frameStart + std::chrono::microseconds(1000000LL * fgActive / options.maxFps)
                        : realEnd;
                frames += fgPacer.Pump(FrameGenPacer::Stage::AfterPresent, !options.vsync, idleUntil,
                                       [&](int i, int n) { return renderer->PresentGenerated(i, n); });
            }
        }
        frame::Profiler::Get().FinishFrame(frameStart);

        if (frameStart - animationAt >= frame::kAnimationStep) {
            animationAt = frameStart;
            levelRenderer->TickAnimation(++animationStep);
        }

        frames++;
        const auto now = Clock::now();
        if (now - titleAt >= frame::kTitleInterval) {
            const double seconds = std::chrono::duration<double>(now - titleAt).count();
            debug.fps = static_cast<int>(frames / seconds + 0.5);
            UpdateTitle(debug.fps);
            titleAt = now;
            frames = 0;
        }
    }

    void Perlin::PaceFrame(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& appliedVsync = loop->appliedVsync;
#ifndef __EMSCRIPTEN__
        {
            // fullscreen: the option is the intent; the window can also change on its own (os shortcut), so adopt that once settled
            static int appliedFullscreen = -1;
            static Clock::time_point fullscreenSettle{};
            if (inputManager.Pressed(Action::Fullscreen)) {
                options.fullscreen = !options.fullscreen;
                options.Save();
            }
            const bool windowFullscreen = platform->IsFullscreen();
            const auto fsNow = Clock::now();
            if (appliedFullscreen < 0 || static_cast<bool>(appliedFullscreen) != options.fullscreen) {
                if (windowFullscreen != options.fullscreen)
                    platform->SetFullscreen(options.fullscreen);
                appliedFullscreen = options.fullscreen;
                fullscreenSettle = fsNow + std::chrono::milliseconds(1500);
            } else if (fsNow >= fullscreenSettle && windowFullscreen != options.fullscreen) {
                options.fullscreen = windowFullscreen;
                appliedFullscreen = windowFullscreen;
                options.Save();
            }
        }
        const bool wantVsync = options.vsync && !exportJob.active;
        if (appliedVsync != wantVsync) {
            appliedVsync = wantVsync;
            renderer->SetVsync(appliedVsync);
        }
        {
            static bool background = false;
            static int backgroundCheck = 0;
            if (exportJob.active) {
                background = false;
            } else if (++backgroundCheck >= 30) {
                backgroundCheck = 0;
                background = ProcessInBackground();
            }
            if (background) {
                const auto target = frameStart + std::chrono::microseconds(1000000 / 60);
                if (Clock::now() < target)
                    std::this_thread::sleep_until(target);
            }
        }
        const int fpsCap = ModFrameLimit(options.maxFps);
        if (fpsCap > 0 && !exportJob.active)
            FrameGenPacer::SleepUntil(frameStart +
                                      std::chrono::microseconds(1000000LL * std::max(1, fgActive) / fpsCap));
#endif
    }
}
