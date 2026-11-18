#include "client/systems/sprites/ItemData.hpp"
#include "client/systems/renderer/chunk/ChunkMesher.hpp"
#include "client/application/PerlinFrame.hpp"
#include "util/Log.hpp"
#include <filesystem>
#include <algorithm>
#include <functional>
#include <ctime>
#include <vector>

namespace perlin {
    Perlin::Perlin(LaunchArgs args) : args(std::move(args)) {}

    Perlin::~Perlin() = default;

    int Perlin::FrameGenMultiplier() {
        if (options.frameGen == "off" || !renderer || !renderer->CanGenerateFrames())
            return 1;
        return options.frameGen == "4x" ? 4 : options.frameGen == "3x" ? 3 : options.frameGen == "2x" ? 2 : 1;
    }

    void Perlin::RenderThreadMain() {
        NameThisThread("perlin-render");
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
        replayStudio.Finalize();
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
        levelRenderer->OnLibrarySwap([samples = options.bedrockSamplesDir](const BlockLibrary* library) {
            auto& assets = ui::UiAssets::Instance();
            std::string root = assets.VanillaRoot();
            if (root.empty()) {
                std::error_code ec;
                const std::string candidate = samples + "/repo/resource_pack";
                if (std::filesystem::exists(candidate + "/textures/ui", ec))
                    root = candidate;
            }
            if (!root.empty())
                assets.Configure(root, library);
        });
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
                debugOverlay.state.on = true;
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
                        {ui::CommandParam{.name = "target", .target = true},
                         ui::CommandParam{.name = "mode", .values = {"survival", "creative", "adventure"}, .optional = true}});
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
        renderReady = false;
    }

    void Perlin::FrameStep() {
        FrameContext f;
        BeginFrame(f);
        ImportDroppedFiles();
        RestorePlayerCamera();
        {
            frame::ProfileScope p(frame::Profiler::Input);
            UpdateLook(f);
            TrackDamage();
        }
        {
            frame::ProfileScope p(frame::Profiler::Session);
            UpdateSession(f);
            UpdateDiscord();
        }
        {
            frame::ProfileScope p(frame::Profiler::Input);
            UpdateViewSettings(f);
            UpdateChat(f);
            interaction.UpdateGameInput(f, {&loop->previewLevel, &loop->frameEntities, loop->preview});
            view.UpdatePreviewWorld(f);
            view.UpdateThirdPersonDistance(f.live, f.dt);
        }
        {
            frame::ProfileScope p(frame::Profiler::Prep);
            view.SampleEyeEnvironment(f);
            hudScreen.FillFrameHud(f);
            debugOverlay.HandleKeys(f, loop->previewLevel.get());
            debugOverlay.Fill(hudState, f.live, f.live ? &f.live->NetLevel() : loop->previewLevel.get());
            audioDirector.Update(f.live, detail::SecondsSince(loop->startedAt, f.frameStart), f.dt);
            if (PERLIN_ENV("PERLIN_PREVIEW_BORDERS"))
                debugOverlay.state.chunkBorders = true;
            if (PERLIN_ENV("PERLIN_PREVIEW_SUBTITLES")) {
                hudState.subtitles = {{"Cow moos", -1, 1.0f},
                                      {"Footsteps", 0, 0.8f},
                                      {"Zombie groans", 1, 0.5f},
                                      {"Explosion", 1, 0.3f}};
            }
        }
        {
            frame::ProfileScope p(frame::Profiler::Tick);
            gameTick.TickGame(f);
        }
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

        UpdateCameraEffects();
        frameParams.PrepareView(f);
        frameParams.BuildFrameParams(f);
        levelPass.RenderLevel(f);
        levelPass.HandleLiveScreenshot(f);
        entityPass.RenderEntities(f);
        levelPass.RenderHand(f);
        UpdateHudAndScreens(f);
        UpdateTouch(f);
        EndFrame(f);
        RecordFrameStats();
        PaceFrame(f);
    }

    void Perlin::RecordFrameStats() {
        static const double every = [] {
            const char* v = PERLIN_ENV("PERLIN_FRAME_STATS");
            return v ? std::max(1.0, std::atof(v)) : 0.0;
        }();
        if (every <= 0.0)
            return;
        struct Stats {
            std::vector<double> frameMs;
            std::chrono::steady_clock::time_point last{}, windowStart{};
            double cpuStart = 0;
        };
        static Stats st;
        const auto threadCpuMs = [] {
#if defined(__linux__) || defined(__APPLE__)
            timespec ts{};
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
            return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1e6;
#else
            return 0.0;
#endif
        };
        const auto now = std::chrono::steady_clock::now();
        if (st.last.time_since_epoch().count() != 0)
            st.frameMs.push_back(std::chrono::duration<double, std::milli>(now - st.last).count());
        else {
            st.windowStart = now;
            st.cpuStart = threadCpuMs();
        }
        st.last = now;
        const double window = std::chrono::duration<double>(now - st.windowStart).count();
        if (window < every || st.frameMs.size() < 2)
            return;
        std::vector<double> sorted = st.frameMs;
        std::sort(sorted.begin(), sorted.end());
        const auto pct = [&](double q) {
            return sorted[std::min(sorted.size() - 1, static_cast<size_t>(q * static_cast<double>(sorted.size())))];
        };
        double sum = 0;
        for (double v : sorted)
            sum += v;
        const double cpuNow = threadCpuMs();
        char line[256];
        std::snprintf(line, sizeof(line),
                      "%zu frames in %.1f s: %.2f fps, frame ms avg %.2f p50 %.2f p95 %.2f p99 %.2f max %.2f, "
                      "render thread cpu %.2f ms/frame",
                      sorted.size(), window, static_cast<double>(sorted.size()) / window,
                      sum / static_cast<double>(sorted.size()), pct(0.5), pct(0.95), pct(0.99), sorted.back(),
                      (cpuNow - st.cpuStart) / static_cast<double>(sorted.size()));
        Log("framestats", "{}", line);
        st.frameMs.clear();
        st.windowStart = now;
        st.cpuStart = cpuNow;
    }

    void Perlin::BeginFrame(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& dt = f.dt;
        auto& zoom = loop->zoom;
        auto& previous = loop->previous;
        auto& startedAt = loop->startedAt;
        auto& frames = loop->frames;
        if (packManager.TakeLevelUnloadPending()) {
            packManager.UnloadServerPacks(true);
        }
#ifdef __EMSCRIPTEN__
        platform->PumpFrame();
#endif
        frameStart = Clock::now();
        if ((frames & 15) == 0)
            i18n::Lang::Get().Configure(ui::UiAssets::Instance().VanillaRoot(), paths::DataDir(),
                                        options.language);

        if (static const char* anyShot = PERLIN_ENV("PERLIN_SHOT"); anyShot) {
            bool& anyShotDone = frameLocals.anyShotDone;
            int& anyShotFrames = frameLocals.anyShotFrames;
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
        controlBridge.Poll(ControlContext());
        inputManager.Poll();
        if (replayStudio.Exporting())
            replayStudio.ExportPreFrame(frameStart, dt);
        if (replayCtl.Playing())
            camera.mode = PerspectiveMode::FirstPerson;
        else if (inputManager.Pressed(Action::CyclePerspective))
            camera.CycleMode();
        zoom.SetConfig(options.zoom);
        packManager.PollHotReload();
        if (fontsDownloaded.exchange(false))
            ui::UiAssets::Instance().LoadFont(paths::DataDir());
    }

    void Perlin::ImportDroppedFiles() {
        namespace fsys = std::filesystem;
        for (const std::string& dropped : platform->TakeDropped()) {

            if (dropped.starts_with("perlin://")) {
                if (const std::string server = discord::ServerFromUrl(dropped); !server.empty())
                    serverBrowser.JoinServer(server);
                continue;
            }
            std::error_code e;
            const fsys::path src(dropped);
            const std::string ext = str::Lower(src.extension().string());
            const bool onShaders = pause.IsOpen() && pause.view == 1 && pause.optionsUi.page == 8;
            const bool onWardrobe = pause.IsOpen() && pause.view == 5 && ext == ".png";
            const bool onMods = pause.IsOpen() && pause.view == 4;
            const bool dropIsCape = str::Lower(src.filename().string()).contains("cape");
            const char* sub = onWardrobe       ? (dropIsCape ? "capes" : "skins")
                              : ext == ".wasm" || ext == ".perlinmod" || onMods ? "mods"
                              : onShaders      ? "shaderpacks"
                                               : "texturepacks";
            if (ext == ".perlinreplay") {
                replayLibrary.Import(dropped);
                continue;
            }
            if (!onWardrobe && ext != ".wasm" && ext != ".zip" && ext != ".mcpack" && ext != ".perlinmod" &&
                !fsys::is_directory(src, e))
                continue;
            fsys::path destDir = fsys::path(paths::DataDir()) / sub;
            if (ext == ".wasm")
                destDir /= src.stem();
            fsys::create_directories(destDir, e);
            fsys::copy(src, destDir / src.filename(),
                       fsys::copy_options::overwrite_existing | fsys::copy_options::recursive, e);
            chat.AddMessage(e ? "Could not add " + src.filename().string()
                              : "Added " + src.filename().string() + " to " + sub,
                            hudAnim.ticks);
            pause.packUi.foundAt = {};
            pause.wardrobe.listedAt = {};
            pause.optionsUi.signature.clear();
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
                ev.underwater = view.headUnderwater;
                ev.thirdPerson = camera.mode != PerspectiveMode::FirstPerson;
                ev.onGround = ps.onGround;
                {
                    std::lock_guard lock(modHost.modAdv.mutex);
                    modHost.modAdv.lookTaken = false;
                }
                float& glideLostFor = frameLocals.glideLostFor;
                glideLostFor = ev.gliding ? 0.0f : glideLostFor + dt;
                abi::Writer lw;
                lw.write(ev);
                lookMods.Dispatch(abi::Event::Look, lw, 20.0);
                if (glideLostFor > 0.35f) {
                    std::lock_guard lock(modHost.modAdv.mutex);
                    modHost.modAdv.cameraUntil = {};
                    modHost.modAdv.poseUntil = {};
                    modHost.modAdv.lookTaken = false;
                }
                std::lock_guard lock(modHost.modAdv.mutex);
                if (modHost.modAdv.lookTaken) {
                    lookTaken = true;
                    const glm::dvec3 d = modHost.modAdv.lookDir;
                    camera.yaw = static_cast<float>(glm::degrees(std::atan2(d.z, d.x)));
                    camera.pitch =
                        std::clamp(static_cast<float>(glm::degrees(std::asin(std::clamp(d.y, -1.0, 1.0)))),
                                   -89.9f, 89.9f);
                    modHost.modAdv.lookTaken = false;
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

    void Perlin::RestorePlayerCamera() {
        if (!cameraOverridden)
            return;
        cameraOverridden = false;
        camera.x = cameraPlayerView[0];
        camera.y = cameraPlayerView[1];
        camera.z = cameraPlayerView[2];
        camera.yaw = cameraPlayerView[3];
        camera.pitch = cameraPlayerView[4];
    }

    void Perlin::UpdateCameraEffects() {
        cameraDetached = false;
        if (!(sessionReady && session)) {
            if (cameraModeForced) {
                camera.mode = cameraSavedMode;
                cameraModeForced = false;
            }
            return;
        }
        CameraEffects& fx = session->Camera();
        const CameraPose base{glm::vec3(camera.x, camera.y, camera.z), camera.yaw, camera.pitch};
        const EntityLookup lookup = [this, &base](int64_t uid, TrackedEntity& out) {
            entity::Entity e;
            if (!session->Entities().FindUnique(uid, e))
                return false;
            const float h = e.boxHeight > 0.0f ? e.boxHeight : 1.8f;
            if (e.isSelf) {
                out.eye = base.position;
                out.center = base.position - glm::vec3(0.0f, 0.2f, 0.0f);
                out.yaw = base.yaw;
                out.pitch = base.pitch;
                return true;
            }
            const glm::vec3 feet(static_cast<float>(e.pos.x), static_cast<float>(e.pos.y),
                                 static_cast<float>(e.pos.z));
            out.center = feet + glm::vec3(0.0f, h * 0.5f, 0.0f);
            out.eye = feet + glm::vec3(0.0f, e.isPlayer ? 1.62f : h * 0.9f, 0.0f);
            out.yaw = e.headYaw + 90.0f;
            out.pitch = -e.pitch;
            return true;
        };
        fx.Tick(base, lookup);
        using Kind = CameraEffects::Kind;
        const Kind kind = fx.PresetKind();
        std::optional<PerspectiveMode> wanted;
        if (kind == Kind::FirstPerson)
            wanted = PerspectiveMode::FirstPerson;
        else if (kind == Kind::ThirdPerson)
            wanted = PerspectiveMode::ThirdPersonBack;
        else if (kind == Kind::ThirdPersonFront)
            wanted = PerspectiveMode::ThirdPersonFront;
        const bool pose = fx.PoseActive();
        if (pose)
            wanted = PerspectiveMode::FirstPerson;
        if (wanted) {
            if (!cameraModeForced) {
                cameraSavedMode = camera.mode;
                cameraModeForced = true;
            }
            camera.mode = *wanted;
        } else if (cameraModeForced) {
            camera.mode = cameraSavedMode;
            cameraModeForced = false;
        }
        if (pose) {
            cameraPlayerView[0] = camera.x;
            cameraPlayerView[1] = camera.y;
            cameraPlayerView[2] = camera.z;
            cameraPlayerView[3] = camera.yaw;
            cameraPlayerView[4] = camera.pitch;
            cameraOverridden = true;
            const CameraPose p = fx.Pose();
            camera.x = p.position.x;
            camera.y = p.position.y;
            camera.z = p.position.z;
            camera.yaw = p.yaw;
            camera.pitch = glm::clamp(p.pitch, -89.9f, 89.9f);
            cameraDetached = true;
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

        replayStudio.FramebufferSizeEffective(width, height);
        if (width > 0 && height > 0)
            camera.aspect = static_cast<float>(width) / static_cast<float>(height);

        replayStudio.Pump(dt);

        if (connectsInFlight.load() == 0)
            std::erase_if(retiredSessions,
                          [](const std::unique_ptr<BedrockSession>& s) { return !s->Transferring(); });
        live = sessionReady && session && !session->Transferring() ? session.get() : nullptr;
        if (live) {
            const physics::State& viewerState = player.State();
            live->Entities().SetViewer(live->GetRuntimeId(), {viewerState.x, viewerState.y, viewerState.z});
            live->Entities().Snapshot(frameEntities);
        } else {
            previewEntities.Snapshot(frameEntities);
        }
        modHost.PumpMods(live, live ? &live->NetLevel() : previewLevel.get(), hud.get(), dt);
        if (!live)
            return;
        live->Update();
        live->PumpRequests(256);
        UpdateRiding(*live, frameEntities);
        player.SetToggleModes(options.sprintMode == "toggle", options.sneakMode == "toggle");
        hudState.moveStatus.clear();
        interaction.UpdateEatingAndDrinking(f, *live);
        {
            CameraEffects& cam = live->Camera();
            const int scheme = cam.ControlScheme();
            player.SetCameraControl(scheme, scheme >= 0 ? cam.Pose().yaw : 0.0f);
        }
        if (!replayCtl.Playing()) {
            player.Update(*live, inputManager, camera, levelRenderer->Blocks(), dt);
        } else {
            double& clock = loop->replayActorClock;
            clock += std::min(static_cast<double>(dt), 0.1) * std::max(0.0, replayCtl.Speed());
            for (int i = 0; clock >= physics::kTickSeconds && i < 10; ++i) {
                clock -= physics::kTickSeconds;
                live->Entities().TickInterpolation();
            }
            clock = std::min(clock, static_cast<double>(physics::kTickSeconds));
        }

        live->Entities().Snapshot(frameEntities);
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
            bool& wasSeated = frameLocals.wasSeated;
            entity::Entity& lastSeat = frameLocals.lastSeat;
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

            const bool fallingBlock = e.type == "minecraft:falling_block";
            if (!e.Has(entity::flag::Stackable) && !fallingBlock && e.type != "minecraft:cushion")
                continue;
            float w = 0.999f, h = 0.249f;
            entityRenderer.CollisionBoxOf(e.type, w, h);
            if (e.boxWidth > 0.0f && e.boxHeight > 0.0f) {
                w = e.boxWidth;
                h = e.boxHeight;
            }

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

    void Perlin::ApplyRendererSwitch() {

        if (options.renderer == notifiedRenderer || pause.optionsUi.drag >= 0)
            return;
        notifiedRenderer = options.renderer;
        options.Save();
        if (options.renderer == runningRenderer)
            chat.AddMessage("Renderer change cancelled", hudAnim.ticks);
        else
            chat.AddMessage("Restart the game to use the new renderer", hudAnim.ticks);
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
            auto& lastTrim = frameLocals.lastTrim;
            const auto nowTrim = std::chrono::steady_clock::now();
            if (nowTrim - lastTrim > std::chrono::seconds(45)) {
                lastTrim = nowTrim;
                tasks.Run([] { TrimMemory(false); });
            }
        }
        levelRenderer->SetBetterGrass(options.betterGrass ? 1 : 0);
        levelRenderer->SetGlassMode(options.glass == "connected"        ? 1
                                    : options.glass == "connectedClear" ? 2
                                    : options.glass == "clear"          ? 3
                                                                        : 0);
        levelRenderer->SetServerRadius(live ? live->GrantedRadius() : 0);
        levelRenderer->ReloadAssetsIfNeeded();
        packManager.PollJavaPack(live);
    }

    void Perlin::UpdateChat(FrameContext& f) {
        auto& live = f.live;
        auto& preview = loop->preview;
        if (live)
            for (const std::string& line : live->TakeChat())
                if (!modHost.FilterIncomingChat(line))
                    chat.AddMessage(line, hudAnim.ticks);
        modHost.MergePluginCommands(live);
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
                if (r.send && modHost.InterceptOutgoing(live, r.text)) {
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
        if (replayStudio.Exporting())
            replayStudio.ExportPostRender();
        controlBridge.Publish(ControlContext());
        {
            frame::ProfileScope p(frame::Profiler::EndFrame);
            renderer->EndFrame();
        }
        if (!replayStudio.Exporting()) {
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
        if (frame::Profiler::Get().Enabled()) {
            if (const double gpuMs = renderer->LastGpuMs(); gpuMs >= 0)
                frame::Profiler::Get().Add(frame::Profiler::Gpu, gpuMs);
            if (frame::Profiler::Get().FinishFrame(frameStart))
                MeshProfilePrint();
        }

        if (frameStart - animationAt >= frame::kAnimationStep) {
            animationAt = frameStart;
            levelRenderer->TickAnimation(++animationStep);
        }

        frames++;
        const auto now = Clock::now();
        {
            const float ms = std::chrono::duration<float, std::milli>(now - loop->lastFrameEnd).count();
            loop->lastFrameEnd = now;
            if (ms > 0.0f && ms < 10000.0f) {
                loop->frameMs[loop->frameMsHead] = ms;
                loop->frameMsHead = (loop->frameMsHead + 1) % loop->frameMs.size();
                loop->frameMsCount = std::min(loop->frameMsCount + 1, loop->frameMs.size());
            }
        }
        if (now - titleAt >= frame::kTitleInterval) {
            const double seconds = std::chrono::duration<double>(now - titleAt).count();
            debugOverlay.state.fps = static_cast<int>(frames / seconds + 0.5);
            if (loop->frameMsCount > 0) {
                std::vector<float> recent;
                recent.reserve(loop->frameMsCount);
                float span = 0.0f;
                for (size_t i = 0; i < loop->frameMsCount && span < 10000.0f; i++) {
                    const float ms =
                        loop->frameMs[(loop->frameMsHead + loop->frameMs.size() - 1 - i) % loop->frameMs.size()];
                    recent.push_back(ms);
                    span += ms;
                }
                const size_t worst = std::max<size_t>(1, recent.size() / 100);
                std::partial_sort(recent.begin(), recent.begin() + static_cast<std::ptrdiff_t>(worst), recent.end(),
                                  std::greater<float>());
                double sum = 0.0;
                for (size_t i = 0; i < worst; i++)
                    sum += recent[i];
                const double avg = sum / static_cast<double>(worst);
                debugOverlay.state.lowFps = avg > 0.0 ? static_cast<int>(1000.0 / avg + 0.5) : 0;
            }
            UpdateTitle(debugOverlay.state.fps);
            titleAt = now;
            frames = 0;
        }
    }

    void Perlin::PaceFrame(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& appliedVsync = loop->appliedVsync;
#ifndef __EMSCRIPTEN__
        {

            int& appliedFullscreen = frameLocals.appliedFullscreen;
            Clock::time_point& fullscreenSettle = frameLocals.fullscreenSettle;
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
        const bool wantVsync = options.vsync && !replayStudio.Exporting();
        if (appliedVsync != wantVsync) {
            appliedVsync = wantVsync;
            renderer->SetVsync(appliedVsync);
        }
        {
            const auto away = [&] {
#if defined(_WIN32)
                return ProcessInBackground();
#else
                return !platform->WindowFocused();
#endif
            };
            const bool background = !replayStudio.Exporting() && away();
            const bool minimized = !replayStudio.Exporting() && platform->WindowMinimized();
            Clock::time_point& minimizedSince = frameLocals.minimizedSince;
            bool& trimmed = frameLocals.trimmed;
            if (!minimized) {
                minimizedSince = {};
                trimmed = false;
            } else if (minimizedSince == Clock::time_point{}) {
                minimizedSince = frameStart;
            } else if (!trimmed && frameStart - minimizedSince > std::chrono::seconds(5)) {
                trimmed = true;
                tasks.Run([] { TrimMemory(true); });
            }
            const int cap = minimized ? 5 : background ? options.backgroundFps : 0;
            if (cap > 0) {
                const auto target = frameStart + std::chrono::microseconds(1000000 / cap);
                while (Clock::now() < target && away())
                    std::this_thread::sleep_until(
                        std::min(target, Clock::now() + std::chrono::milliseconds(5)));
            }
        }
        const int fpsCap = modHost.ModFrameLimit(options.maxFps);
        if (fpsCap > 0 && !replayStudio.Exporting())
            FrameGenPacer::SleepUntil(frameStart +
                                      std::chrono::microseconds(1000000LL * std::max(1, fgActive) / fpsCap));
#endif
    }
}
