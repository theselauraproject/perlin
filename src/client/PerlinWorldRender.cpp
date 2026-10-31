#include "client/PerlinFrame.hpp"
#include "util/Log.hpp"

namespace perlin {
    namespace {
        uint8_t CachedLight(Level& level, int bx, int by, int bz, std::chrono::steady_clock::time_point now) {
            struct Band {
                ChunkLighting::Result lit;
                std::chrono::steady_clock::time_point at{};
                uint64_t counter = 0;
            };
            static std::unordered_map<uint64_t, Band> bands;
            static const Level* owner = nullptr;
            if (owner != &level || bands.size() > 2048) {
                bands.clear();
                owner = &level;
            }
            // changeCounter is level-wide: busy servers bump it every tick, refreshing every band at once
            static std::chrono::steady_clock::time_point budgetFrame{};
            static int budget = 0;
            if (now != budgetFrame) {
                budgetFrame = now;
                budget = 2;
            }
            const int cx = bx >> 4, cz = bz >> 4, band = by >> 4;
            const uint64_t key = (static_cast<uint64_t>(cx & 0xFFFFF) << 28) |
                                 (static_cast<uint64_t>(cz & 0xFFFFF) << 8) |
                                 static_cast<uint64_t>((band + 128) & 0xFF);
            Band& b = bands[key];
            const uint64_t counter = level.changeCounter.load(std::memory_order_relaxed);
            const auto age = now - b.at;
            const bool fresh = b.at == std::chrono::steady_clock::time_point{};
            // out of budget: open sky until this band's turn comes
            if (fresh && budget <= 0)
                return 0xF0;
            if (fresh || (budget > 0 && ((counter != b.counter && age > std::chrono::milliseconds(400)) ||
                                         age > std::chrono::seconds(20)))) {
                budget--;
                // the level lock is held by chunk application and meshing: never wait for it on the render thread
                if (ChunkLighting::Compute(level, {cx, cz}, band * 16 - 8, band * 16 + 23, b.lit, true)) {
                    b.at = now;
                    b.counter = counter;
                } else if (fresh) {
                    return 0xF0;
                }
            }
            return b.lit.At(bx & 15, by, bz & 15);
        }
        void ApplyBedPose(Level& level, entity::Entity& e) {
            BlockPalette& pal = BlockPalette::Instance();
            double bestD = 1e9;
            for (int dy = 0; dy >= -1; dy--)
                for (int dz = -2; dz <= 2; dz++)
                    for (int dx = -2; dx <= 2; dx++) {
                        const int x = static_cast<int>(std::floor(e.pos.x)) + dx,
                                  y = static_cast<int>(std::floor(e.pos.y + 0.3)) + dy,
                                  z = static_cast<int>(std::floor(e.pos.z)) + dz;
                        bool loaded = false;
                        const uint16_t id = level.GetBlock(x, y, z, loaded);
                        if (!loaded || id == 0 || pal.Name(id) != "minecraft:bed")
                            continue;
                        const int dir = pal.StateInt(id, "direction", 0) & 3;
                        const bool head = pal.StateInt(id, "head_piece_bit", 0) != 0;
                        static const int kStep[4][2] = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}};
                        const int hx = head ? x : x + kStep[dir][0], hz = head ? z : z + kStep[dir][1];
                        const double cx = hx + 0.5, cz = hz + 0.5;
                        const double d = (cx - e.pos.x) * (cx - e.pos.x) + (cz - e.pos.z) * (cz - e.pos.z);
                        if (d >= bestD)
                            continue;
                        bestD = d;
                        e.pos = {cx - kStep[dir][0] * 1.3, y + 0.5625, cz - kStep[dir][1] * 1.3};
                        e.yaw = static_cast<float>(dir * 90 + 180);
                        e.headYaw = e.yaw;
                        e.pitch = 0.0f;
                    }
        }
        float StepWeather(float level, float target) {
            if (level < target)
                return std::min(target, level + 0.01f);
            return std::max(target, level - 0.01f);
        }
    }

    void Perlin::UpdatePreviewWorld(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& live = f.live;
        auto& worldTicks = f.worldTicks;
        auto& preview = loop->preview;
        auto& previewLevel = loop->previewLevel;
        auto& previewTicks = loop->previewTicks;
        auto& previewReadyAt = loop->previewReadyAt;
        worldTicks = preview ? previewTicks : frame::kNoonTick;
        if (live)
            live->GetWorldTicks(worldTicks);
        if (replayCtl.Playing()) {
            float forcedTicks = 0;
            if (timeline.TimeOfDayAt(replayCtl.TimeMs(), forcedTicks))
                worldTicks = forcedTicks;
        }
        if (preview && !previewLevel) {
            static auto lastNote = frameStart;
            if (frameStart - lastNote > std::chrono::seconds(3)) {
                lastNote = frameStart;
                Log("preview", "waiting: ready={} complete={}", levelRenderer->Blocks().Ready(), levelRenderer->Blocks().Complete());
            }
        }
        if (preview && !previewLevel && levelRenderer->Blocks().Ready()) {
            auto built = std::make_unique<Level>();
            if (BuildPreviewWorld(*built, 7)) {
                previewLevel = std::move(built);
                previewReadyAt = frameStart;
                Log("preview", "world built");
            }
        }

        if (preview && previewLevel && PERLIN_ENV("PERLIN_PREVIEW_EDIT")) {
            static bool edited = false;
            if (!edited && frameStart - previewReadyAt > std::chrono::seconds(4)) {
                edited = true;
                const std::string list = PERLIN_ENV("PERLIN_PREVIEW_EDIT");
                size_t start = 0;
                while (start < list.size()) {
                    size_t end = list.find(';', start);
                    if (end == std::string::npos)
                        end = list.size();
                    int dx = 0, dy = 0, dz = 0;
                    char name[128] = {0};
                    if (std::sscanf(list.substr(start, end - start).c_str(), "%d,%d,%d,%127s", &dx, &dy, &dz,
                                    name) == 4) {
                        BlockPalette& palette = BlockPalette::Instance();
                        std::string full = name;
                        std::vector<std::pair<std::string, BlockPalette::StateValue>> states;
                        if (const size_t lb = full.find('['); lb != std::string::npos) {
                            std::string body = full.substr(lb + 1, full.size() - lb - 2);
                            full = full.substr(0, lb);
                            size_t q = 0;
                            while (q < body.size()) {
                                size_t c = body.find(',', q);
                                if (c == std::string::npos)
                                    c = body.size();
                                const std::string kv = body.substr(q, c - q);
                                const size_t eq = kv.find('=');
                                if (eq != std::string::npos) {
                                    BlockPalette::StateValue sv;
                                    const std::string val = kv.substr(eq + 1);
                                    if (val == "true" || val == "false") {
                                        sv.kind = BlockPalette::StateValue::Bool;
                                        sv.i = val == "true";
                                    } else if (!val.empty() &&
                                               (std::isdigit(static_cast<unsigned char>(val[0])) ||
                                                val[0] == '-')) {
                                        sv.kind = BlockPalette::StateValue::Int;
                                        sv.i = std::atoi(val.c_str());
                                    } else {
                                        sv.kind = BlockPalette::StateValue::String;
                                        sv.s = val;
                                    }
                                    states.emplace_back(kv.substr(0, eq), sv);
                                }
                                q = c + 1;
                            }
                            std::sort(states.begin(), states.end(),
                                      [](const auto& a, const auto& b) { return a.first < b.first; });
                        }
                        const uint16_t id = palette.Intern(BlockPalette::HashState(full, states));
                        previewLevel->SetBlock(static_cast<int>(std::floor(camera.x)) + dx,
                                               static_cast<int>(std::floor(camera.y)) + dy,
                                               static_cast<int>(std::floor(camera.z)) + dz, id);
                    }
                    start = end + 1;
                }
                Log("preview", "edits applied");
            }
        }
    }

    namespace {
        // segment from a to b against a box; returns the entry fraction, or a value past 1 when missed
        float SegmentBoxEntry(const glm::vec3& a, const glm::vec3& b, const glm::vec3& lo, const glm::vec3& hi) {
            float enter = 0.0f, leave = 1.0f;
            for (int i = 0; i < 3; ++i) {
                const float d = b[i] - a[i];
                if (d == 0.0f) {
                    if (a[i] < lo[i] || a[i] > hi[i])
                        return 2.0f;
                    continue;
                }
                float t0 = (lo[i] - a[i]) / d, t1 = (hi[i] - a[i]) / d;
                if (t0 > t1)
                    std::swap(t0, t1);
                enter = std::max(enter, t0);
                leave = std::min(leave, t1);
                if (enter > leave)
                    return 2.0f;
            }
            return enter;
        }

        // BlockSource clip walk (0x1432DA390) with the camera shape (shape type 2): cells in ray order, at most 200,
        // the first hit against a block's camera box ends it
        std::optional<glm::vec3> CameraClip(const Level& level, const LevelCollision& col, glm::vec3 from, glm::vec3 to) {
            glm::ivec3 cell(static_cast<int>(std::floor(from.x)), static_cast<int>(std::floor(from.y)),
                            static_cast<int>(std::floor(from.z)));
            const glm::ivec3 last(static_cast<int>(std::floor(to.x)), static_cast<int>(std::floor(to.y)),
                                  static_cast<int>(std::floor(to.z)));
            const glm::vec3 d = to - from;
            glm::ivec3 step;
            glm::vec3 next, delta;
            for (int i = 0; i < 3; ++i) {
                step[i] = d[i] > 0.0f ? 1 : (d[i] < 0.0f ? -1 : 0);
                delta[i] = step[i] != 0 ? std::abs(1.0f / d[i]) : std::numeric_limits<float>::infinity();
                const float edge = step[i] > 0 ? static_cast<float>(cell[i] + 1) : static_cast<float>(cell[i]);
                next[i] = step[i] != 0 ? (edge - from[i]) / d[i] : std::numeric_limits<float>::infinity();
            }
            for (int visited = 0; visited < 200; ++visited) {
                physics::BlockShape shape = col.Query(cell.x, cell.y, cell.z);
                bool loaded = false;
                const std::string& name = BlockPalette::Instance().Name(level.GetBlock(cell.x, cell.y, cell.z, loaded));
                // camera shape overrides: grates never stop the camera (0x147726070), soul sand and mud use a
                // full cube instead of their lowered collision (0x148F936E0)
                if (name.ends_with("copper_grate"))
                    shape.boxes.clear();
                else if (name == "minecraft:soul_sand" || name == "minecraft:mud")
                    shape.boxes = {{0, 0, 0, 1, 1, 1}};
                float best = 2.0f;
                for (const physics::Box& b : shape.boxes) {
                    const glm::vec3 lo(static_cast<float>(cell.x + b.x0), static_cast<float>(cell.y + b.y0),
                                       static_cast<float>(cell.z + b.z0));
                    const glm::vec3 hi(static_cast<float>(cell.x + b.x1), static_cast<float>(cell.y + b.y1),
                                       static_cast<float>(cell.z + b.z1));
                    best = std::min(best, SegmentBoxEntry(from, to, lo, hi));
                }
                if (best <= 1.0f)
                    return from + d * best;
                if (cell == last)
                    break;
                const int axis = next.x < next.y ? (next.x < next.z ? 0 : 2) : (next.y < next.z ? 1 : 2);
                if (next[axis] > 1.0f)
                    break;
                cell[axis] += step[axis];
                next[axis] += delta[axis];
            }
            return std::nullopt;
        }
    }

    // CameraAvoidanceSystem, lambda 0x146F838E0, with the code defaults: no whisker rays, distance_constraint_min 0,
    // relax spring 4 (0x40800000)
    void Perlin::UpdateThirdPersonDistance(BedrockSession* live, float dt) {
        // TODO: the vanilla preset distance and whisker rays come from the camera presets json, 5 until they're read
        constexpr float desired = 5.0f;
        if (camera.mode == PerspectiveMode::FirstPerson) {
            // a fresh avoidance component starts at distance 0: entering third person springs out from the eye
            camera.thirdPersonDistance = 0.0f;
            camera.avoidanceVelocity = 0.0f;
            return;
        }
        float target = desired;
        if (live) {
            const LevelCollision col(live->NetLevel(), &levelRenderer->Blocks());
            const glm::vec3 pivot(camera.x, camera.y, camera.z);
            const glm::vec3 dir = camera.mode == PerspectiveMode::ThirdPersonBack ? -camera.Direction() : camera.Direction();
            const glm::vec3 wanted = pivot + dir * desired;
            const float nearClip = camera.nearPlane;
            // 8 rays from the corners of a +-0.1 box around the pivot, parallel to pivot -> camera; the hit distance
            // is measured from the pivot centre
            float best = std::numeric_limits<float>::max();
            for (int i = 0; i < 8; ++i) {
                const glm::vec3 o(static_cast<float>(((2 * i) & 2) - 1) * 0.1f, static_cast<float>((i & 2) - 1) * 0.1f,
                                  static_cast<float>(((i >> 1) & 2) - 1) * 0.1f);
                if (const auto hit = CameraClip(live->NetLevel(), col, pivot + o, wanted + o))
                    best = std::min(std::max(nearClip, glm::length(pivot - *hit) - nearClip), best);
            }
            target = desired < best ? desired : std::max(best, 0.0f);
        }
        // only the box rays pull the camera in at once; going back out is a critically damped spring
        if (camera.thirdPersonDistance > target) {
            camera.thirdPersonDistance = target;
            camera.avoidanceVelocity = 0.0f;
        }
        constexpr float k = 4.0f;
        const float x0 = camera.thirdPersonDistance - target;
        const float a = x0 * k;
        const float b = camera.avoidanceVelocity + a;
        const float c = -k * dt;
        const float e = std::exp(c);
        camera.avoidanceVelocity = ((c * b + b) - a) * e;
        camera.thirdPersonDistance = ((dt * b) + x0) * e + target;
    }

    void Perlin::SampleEyeEnvironment(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& live = f.live;
        auto& underwater = f.underwater;
        auto& eyeInLava = f.eyeInLava;
        auto& eyeBiome = f.eyeBiome;
        auto& previewLevel = loop->previewLevel;
        auto& handLightAt = loop->handLightAt;
        auto& handSky = loop->handSky;
        auto& handBlock = loop->handBlock;
        bool inPortalNow = false;
        if (live) {
            const glm::vec3 e = camera.EyePosition();
            const int bx = static_cast<int>(std::floor(e.x)), by = static_cast<int>(std::floor(e.y)),
                      bz = static_cast<int>(std::floor(e.z));
            const LevelCollision col(live->NetLevel(), &levelRenderer->Blocks());
            const physics::BlockShape cell = col.Query(bx, by, bz);
            if (cell.fluid == 1) {
                const float top = col.Query(bx, by + 1, bz).fluid == 1 ? 1.0f : cell.fluidLevel;
                underwater = e.y < static_cast<float>(by) + top;
                int k = 0;
                while (k < 64 && col.Query(bx, by + k + 1, bz).fluid == 1)
                    k++;
                const physics::BlockShape topCell = col.Query(bx, by + k, bz);
                waterSurfaceAbove = static_cast<float>(by + k) + (k == 0 ? top : topCell.fluidLevel) - e.y;
            }
            if (cell.fluid == 2) {
                const float top = col.Query(bx, by + 1, bz).fluid == 2 ? 1.0f : cell.fluidLevel;
                eyeInLava = e.y < static_cast<float>(by) + top;
            }
            eyeBiome = levelRenderer->Tints().NameForId(live->NetLevel().GetBiome(bx, by, bz));
            {
                const int fy = static_cast<int>(std::floor(camera.y - 1.62f));
                const int fx = static_cast<int>(std::floor(e.x)), fz = static_cast<int>(std::floor(e.z));
                inPortalNow = cell.portal || col.Query(fx, fy, fz).portal;
            }
        }

        headUnderwater = underwater;
        headInLava = eyeInLava;
        {
            // java nether portal overlay: ramps up over ~4 s inside a portal, fades fast outside
            const auto nowPortal = Clock::now();
            static auto lastPortalAt = nowPortal;
            const float portalDt =
                std::min(0.1f, std::chrono::duration<float>(nowPortal - lastPortalAt).count());
            lastPortalAt = nowPortal;
            if (inPortalNow && (live || previewLevel))
                portalTime = std::min(1.0f, portalTime + portalDt * 0.25f);
            else
                portalTime = std::max(0.0f, portalTime - portalDt);
        }
        if (live && session) {
            const int dim = live->DimensionId();
            if (lastDimId != -1 && dim != lastDimId) {
                // dimension hop (portal, end gateway): loading screen until the new terrain arrives
                loading.done = false;
                loading.startAt = {};
                loading.readyArmed = false;
                loading.chunkBase = session->ChunksReceived();
            }
            lastDimId = dim;
        }
        if (!underwater)
            waterSurfaceAbove = 0.0f;
        if ((live || previewLevel) && frameStart - handLightAt > std::chrono::milliseconds(200)) {
            handLightAt = frameStart;
            const glm::vec3 e = camera.EyePosition();
            const int bx = static_cast<int>(std::floor(e.x)), by = static_cast<int>(std::floor(e.y)),
                      bz = static_cast<int>(std::floor(e.z));
            Level& lightLevel = live ? live->NetLevel() : *previewLevel;
            const uint8_t packed = CachedLight(lightLevel, bx, by, bz, frameStart);
            handSky = ChunkLighting::Result::Sky(packed);
            handBlock = ChunkLighting::Result::Block(packed);
            debug.sky = handSky;
            debug.block = handBlock;
        }
    }

    void Perlin::FillFrameHud(FrameContext& f) {
        auto& live = f.live;
        auto& preview = loop->preview;
        if (hud)
            hud->SetJavaStyle(true);
        FillHudState(hudState, live, preview);
        hudState.bossBars.clear();
        if (live)
            for (const auto& b : live->BossBars()) {
                if (hudState.bossBars.size() == 8)
                    break;
                hudState.bossBars.push_back({b.title, b.progress, b.color, b.overlay});
            }
        hudState.sidebarVisible = false;
        hudState.sidebar.clear();
        if (live) {
            const auto sb = live->Scoreboard();
            // json ui sidebar when the server restyles it; else the java one
            hudState.sidebarVisible = sb.visible && !pause.shown && hud && hud->Overrides("scoreboard");
            hudState.sidebarTitle = sb.title;
            for (const auto& r : sb.rows)
                hudState.sidebar.push_back({r.name, r.score});
        }
        ApplyModHud(hudState);
    }

    void Perlin::TickGame(FrameContext& f) {
        auto& dt = f.dt;
        auto& live = f.live;
        auto& underwater = f.underwater;
        auto& damage = loop->damage;
        auto& preview = loop->preview;
        auto& waterVisionTime = loop->waterVisionTime;
        tickDebt += dt * 20.0f;
        while (tickDebt >= 1.0) {
            tickDebt -= 1.0;
            blockLight.Tick();
            damage.Tick();
            TickMods(live);
            if (live || preview) {
                TickHud();
                HandRenderer::TickIn handIn;
                handIn.yaw = camera.yaw - 90.0f;
                handIn.pitch = -camera.pitch;
                handIn.main = hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)];
                handIn.off = hudState.offhand;
                handIn.survival = hudState.Survival();
                handIn.sneaking = player.Sneaking();
                handIn.useHeld = ((input.Captured() && inputManager.IsDown(Action::Use)) ||
                                  PERLIN_ENV("PERLIN_PREVIEW_USE") != nullptr) &&
                                 detail::HasAmmo(hudState, live, handIn.main.name);
                {
                    const std::string& mn = handIn.main.name;
                    const bool drink = mn == "minecraft:potion" || mn == "minecraft:milk_bucket" ||
                                       mn == "minecraft:honey_bottle" || mn == "minecraft:ominous_bottle";
                    const bool always =
                        mn == "minecraft:golden_apple" || mn == "minecraft:enchanted_golden_apple" ||
                        mn == "minecraft:chorus_fruit" || drink || mn == "minecraft:suspicious_stew";
                    handIn.eating = handIn.survival && handIn.useHeld && HandRenderer::IsFood(mn) &&
                                    (always || hudState.food < 20);
                }
                handRenderer.Tick(handIn, options.hand);
                {
                    const int et = handRenderer.EatTicks();
                    if (et >= 7 && et % 4 == 3) {
                        const std::string& mn = handIn.main.name;
                        const bool drink = mn == "minecraft:potion" || mn == "minecraft:milk_bucket" ||
                                           mn == "minecraft:honey_bottle" || mn == "minecraft:ominous_bottle";
                        sounds.PlayEntityEvent("minecraft:player", drink ? "drink" : "eat",
                                               glm::dvec3(camera.x, camera.y, camera.z));
                    }
                }
            }
            waterVisionTime =
                underwater ? std::min(600, waterVisionTime + 1) : std::max(0, waterVisionTime - 10);
            if (live) {
                previousRainLevel = rainLevel;
                previousThunderLevel = thunderLevel;
                rainLevel = StepWeather(rainLevel, live->RainTarget());
                thunderLevel = StepWeather(thunderLevel, live->ThunderTarget());
            }
        }
    }

    void Perlin::PrepareView(FrameContext& f) {
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

        EnvironmentInput envIn;
        envIn.ticks = worldTicks;
        envIn.rain = rain;
        envIn.thunder = thunder;
        if (live)
            envIn.dimension = static_cast<Dimension>(std::clamp(live->DimensionId(), 0, 2));
        dimension = envIn.dimension;
        env = SampleEnvironment(envIn);

        glm::mat4 bobMatrix(1.0f);
        if (live && options.hand.viewBobbing && camera.mode == PerspectiveMode::FirstPerson)
            bobMatrix = BobViewMatrix(player.Bob(), player.PartialTick(), options.hand.bobStrength);
        const glm::mat4 hurtMatrix =
            sessionReady ? damage.ViewMatrix(partial, options.damageTilt) : glm::mat4(1.0f);
        camera.viewEffect = hurtMatrix * (options.hand.bobCamera ? bobMatrix : glm::mat4(1.0f));
        if (sessionReady && session)
            camera.viewEffect = session->Camera().ShakeMatrix() * camera.viewEffect;
        handViewEffect = hurtMatrix * (options.hand.bobHand ? bobMatrix : glm::mat4(1.0f));
        {
            camera.farPlane = std::max(static_cast<float>(options.renderDistance * 16) * 1.6f,
                                       static_cast<float>(GraphicsSettings::From(options).cloudRange * 16));
            camera.nearPlane = std::clamp(0.05f + (camera.farPlane - 1500.0f) * 0.00004f, 0.05f,
                                          camera.farPlane > 8000.0f ? 0.4f : 0.25f);
        }
        if (replayCtl.Playing()) {
            if (replayUi.fovOverride > 0)
                camera.fov = replayUi.fovOverride;
            if (replayUi.roll != 0.0f)
                camera.viewEffect =
                    glm::rotate(glm::mat4(1.0f), glm::radians(replayUi.roll), glm::vec3(0.0f, 0.0f, 1.0f)) *
                    camera.viewEffect;
        }
        ApplyModCamera(camera.fov, camera.viewEffect);
        viewProjection = camera.ViewProjection();
        {
            std::lock_guard lock(modAdv.mutex);
            std::memcpy(modAdv.viewProjection.data(), &viewProjection[0][0], sizeof(float) * 16);
        }
        frustum = Frustum::FromViewProjection(viewProjection);
        eye = camera.EyePosition();
        graphics = GraphicsSettings::From(options);
    }

    FogState Perlin::ComputeFrameFog(FrameContext& f) {
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
        fogInput.renderDistanceChunks = options.renderDistance;
        fogInput.cloudRangeChunks = graphics.cloudRange;
        fogInput.forward = camera.Direction();
        fogInput.cameraY = eye.y;
        // java scales the void darkening down to one block above the floor in flat worlds (clear colour scale 1 instead of 1/32)
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
            static auto fogCheck = std::chrono::steady_clock::now() - std::chrono::seconds(10);
            if (std::chrono::steady_clock::now() - fogCheck > std::chrono::seconds(2)) {
                fogCheck = std::chrono::steady_clock::now();
                fogDefs.Configure(ServerPacks::Instance().Directories());
                static std::string hudPackKey;
                std::string key;
                for (const std::string& d : ServerPacks::Instance().Directories())
                    key += d + "|";
                if (key != hudPackKey) {
                    hudPackKey = key;
                    ui::UiAssets::Instance().ClearItemCache();
                    if (hud)
                        hud->Reload();
                }
            }
            const std::vector<std::string> fogStack = session->Camera().FogStack();
            if (!fogStack.empty()) {
                const FogMedium m = fogDefs.Resolve(fogStack, fogInput.inLava       ? 2
                                                              : fogInput.underwater ? 1
                                                                                    : 0);
                const float distance = static_cast<float>(options.renderDistance * 16);
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
                // no server fog pushed: fall back to per-biome nether haze
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

        ApplyModFog(fog);
        if (preview) {
            static int once = 0;
            if (once++ == 20)
                Log("preview", "fog env {}..{} rd {}..{} sky {} rdchunks {} ticks {} sun {}", fog.environmentalStart, fog.environmentalEnd, fog.renderDistanceStart, fog.renderDistanceEnd, fog.skyEnd, options.renderDistance, worldTicks, env.sunAngle);
        }
        return fog;
    }

    void Perlin::CollectDynamicLights(FrameContext& f) {
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

    void Perlin::BuildFrameParams(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& dt = f.dt;
        auto& worldTicks = f.worldTicks;
        auto& rain = f.rain;
        auto& dimension = f.dimension;
        auto& env = f.env;
        auto& viewProjection = f.viewProjection;
        auto& eye = f.eye;
        auto& graphics = f.graphics;
        auto& dyn = f.dyn;
        auto& dynMode = f.dynMode;
        auto& params = f.params;
        auto& startedAt = loop->startedAt;
        auto& frameCounter = loop->frameCounter;
        auto& damage = loop->damage;
        const FogState fog = ComputeFrameFog(f);
        CollectDynamicLights(f);
        params.motionBlur = options.frameGen != "off" ? 0.0f : options.motionBlur;
        params.bloom = options.bloom;
        params.shadowQuality = options.shadowQuality;
        params.shadowDistance = options.shadowDistance;
        params.shadowEntities = options.shadowEntities;
        params.cameraMedium = headUnderwater ? 1 : (headInLava ? 2 : 0);
        params.waterSurfaceAbove = waterSurfaceAbove;
        if (pause.shown && options.menuBlur > 0)
            params.menuBlur = static_cast<float>(options.menuBlur * std::max(1, hud->OverlayScale()));
        if (const char* mb = PERLIN_ENV("PERLIN_MOTION_BLUR"))
            params.motionBlur = std::clamp(static_cast<float>(std::atof(mb)), 0.0f, 1.0f);
        params.dynMode = dynMode;
        {
            const std::vector<ShaderSetting>& packSettingList =
                detail::PackCache().Settings(options.shaderPack);
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
            static glm::vec3 lastCamera = now;
            params.teleported = glm::distance(now, lastCamera) > 6.0f;
            lastCamera = now;
        }
        if (options.upscaler != "off") {
            float scale = static_cast<float>(options.upscaleCustom) / 100.0f;
            if (options.upscaleTargetFps > 0) {
                static float dynamic = 1.0f;
                static double frameMs = 16.0;
                static auto lastChange = std::chrono::steady_clock::now();
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

        params.sky.enabled = dimension == Dimension::Overworld;
        std::copy_n(glm::value_ptr(env.skyColor), 3, params.sky.skyColor);
        params.sky.sunAngle = env.sunAngle;
        params.sky.moonAngle = env.moonAngle;
        params.sky.starAngle = env.starAngle;
        params.sky.rainBrightness = 1.0f - rain;
        params.sky.starBrightness = env.starBrightness;
        std::copy_n(glm::value_ptr(env.sunriseColor), 4, params.sky.sunrise);
        params.sky.moonPhase = env.moonPhase;
        // the sky turns dark below the horizon: sea level in a normal world, the world floor in a flat one
        params.sky.darkDisc =
            eye.y < (session && session->FlatWorld() ? frame::kFlatHorizonHeight : frame::kHorizonHeight);

        const glm::vec4 cloudColor = glm::round(glm::clamp(env.cloudColor, 0.0f, 1.0f) * 255.0f) / 255.0f;
        params.cloud.enabled = graphics.clouds && cloudColor.w > 0.0f;
        params.cloud.fancy = graphics.fancyClouds;
        params.cloud.rangeChunks = graphics.cloudRange;
        std::copy_n(glm::value_ptr(cloudColor), 4, params.cloud.color);
        params.cloud.height = env.cloudHeight;
        params.cloud.gameTicks = std::chrono::duration<double>(frameStart - startedAt).count() * 20.0;

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
        params.portalOverlay = portalTime;
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

        {
            static float waterTile = -1, flowTile = -1, lavaTile = -1;
            static int tileScan = 0;
            if ((tileScan++ % 120) == 0 && levelRenderer->Blocks().Ready()) {
                waterTile = static_cast<float>(levelRenderer->Blocks().TileContaining("water_still"));
                flowTile = static_cast<float>(levelRenderer->Blocks().TileContaining("water_flow"));
                lavaTile = static_cast<float>(levelRenderer->Blocks().TileContaining("lava_still"));
                static std::vector<uint8_t> lastFoliage;
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
                // preview/offline: renderer clock stands in for session time
                params.worldDay = static_cast<int>(worldTicks / 24000.0);
                params.worldTick = static_cast<int>(std::fmod(worldTicks, 24000.0));
            }
        }

        LightmapParams lightmap;
        lightmap.blockFactor = blockLight.Value() + 1.4f;
        lightmap.skyFactor = env.skyLightFactor;
        lightmap.skyLightColor = env.skyLightColor;
        lightmap.ambientColor = env.ambientColor;
        lightmap.blockLightTint = env.blockLightTint;
        lightmap.nightVisionColor = env.nightVisionColor;
        lightmap.brightness = options.gamma;
        BuildLightmap(lightmap, params.lightmap);
    }

    void Perlin::RenderLevel(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& live = f.live;
        auto& frustum = f.frustum;
        auto& params = f.params;
        auto& previewLevel = loop->previewLevel;
        auto& previewShot = loop->previewShot;
        auto& previewWait = loop->previewWait;
        auto& previewShotFrames = loop->previewShotFrames;
        auto& previewReadyAt = loop->previewReadyAt;
        auto& previewLastMeshed = loop->previewLastMeshed;
        auto& frames = loop->frames;
        frame::Profiler::Get().AddToAverage(
            frame::Profiler::Logic,
            std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count());
        fgPacer.PreRenderMs(std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count());
        if (fgPacer.Pending() && !exportJob.active)
            frames += fgPacer.Pump(FrameGenPacer::Stage::BeforeRender, !options.vsync, Clock::now(),
                                   [&](int i, int n) { return renderer->PresentGenerated(i, n); });
        renderer->BeginFrame(params);
        if (live) {
            {
                frame::ProfileScope p(frame::Profiler::LevelUpdate);
                levelRenderer->Update(&live->NetLevel(), camera.x, camera.z, frustum);
            }
            {
                frame::ProfileScope p(frame::Profiler::LevelRender);
                levelRenderer->Render(frustum, camera.x, camera.z);
            }
        } else if (previewLevel) {
            {
                frame::ProfileScope p(frame::Profiler::LevelUpdate);
                levelRenderer->Update(previewLevel.get(), camera.x, camera.z, frustum);
            }
            {
                frame::ProfileScope p(frame::Profiler::LevelRender);
                levelRenderer->Render(frustum, camera.x, camera.z);
            }
            if (!previewShot.empty() && previewShotFrames < 0) {
                const size_t meshed = levelRenderer->MeshedChunks();
                if (meshed != previewLastMeshed) {
                    previewLastMeshed = meshed;
                    previewReadyAt = frameStart;
                }
                if (std::chrono::duration<double>(frameStart - previewReadyAt).count() > previewWait) {
                    renderer->RequestScreenshot(previewShot);
                    previewShotFrames = 0;
                }
            } else if (previewShotFrames >= 0 && ++previewShotFrames > 6) {
                Log("preview", "screenshot saved, meshed chunks {}", levelRenderer->MeshedChunks());
                MeshProfilePrint();
                if (const char* bench = PERLIN_ENV("PERLIN_MESHBENCH"))
                    levelRenderer->BenchmarkMeshing(*previewLevel, std::atoi(bench));
                platform->isRunning = false;
            }
        }
    }

    void Perlin::HandleLiveScreenshot(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& live = f.live;
        auto& startedAt = loop->startedAt;
        if (!live)
            return;
        static const char* shot = PERLIN_ENV("PERLIN_LIVE_SHOT");
        static const char* swingAt = PERLIN_ENV("PERLIN_LIVE_SWING");
        static bool swung = false, shotDone = false;
        static int shotFrames = 0;
        const double t = std::chrono::duration<double>(frameStart - startedAt).count();
        if (swingAt && !swung && t > std::atof(swingAt)) {
            swung = true;
            handRenderer.Attack(options.hand, true);
            live->SendSwingArm("attack");
        }
        if (shot) {
            const std::string spec = shot;
            const size_t at = spec.rfind('@');
            const double when = at == std::string::npos ? 20.0 : std::atof(spec.c_str() + at + 1);
            if (!shotDone && t > when) {
                shotDone = true;
                renderer->RequestScreenshot(spec.substr(0, at));
            } else if (shotDone && ++shotFrames > 8) {
                platform->isRunning = false;
            }
        }
    }

    void Perlin::RenderEntities(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& dt = f.dt;
        auto& live = f.live;
        auto& worldTicks = f.worldTicks;
        auto& frustum = f.frustum;
        auto& dyn = f.dyn;
        auto& params = f.params;
        auto& fpSelf = f.fpSelf;
        auto& startedAt = loop->startedAt;
        auto& preview = loop->preview;
        auto& previewLevel = loop->previewLevel;
        auto& previewReadyAt = loop->previewReadyAt;
        auto& frameEntities = loop->frameEntities;
        if (!(live || previewLevel) || ui::UiAssets::Instance().VanillaRoot().empty())
            return;
        Level& entityLevel = live ? live->NetLevel() : *previewLevel;
        if (!live && !previewEntitiesSpawned && PERLIN_ENV("PERLIN_PREVIEW_ENTITIES")) {
            previewEntitiesSpawned = true;
            SpawnPreviewEntities();
        }
        std::vector<entity::Entity> entities = frameEntities;
        ApplyModSkins(entities);
        if (player.Gliding()) {
            const glm::dvec3 here(camera.x, camera.y, camera.z);
            entities.erase(std::remove_if(entities.begin(), entities.end(),
                                          [&](const entity::Entity& e) {
                                              return e.type == "minecraft:fireworks_rocket" &&
                                                     glm::distance(e.pos, here) < 6.0;
                                          }),
                           entities.end());
        }
        if (!live && PERLIN_ENV("PERLIN_PREVIEW_RIDE") && entities.size() >= 2) {
            std::sort(entities.begin(), entities.end(), [](const entity::Entity& a, const entity::Entity& b) {
                return a.runtimeId < b.runtimeId;
            });
            entities[1].vehicleUid = entities[0].uniqueId;
        }
        entityRenderer.ResolveSeats(entities);
        if (live)
            for (entity::Entity& e : entities)
                if (e.sleeping && e.isPlayer)
                    ApplyBedPose(live->NetLevel(), e);
        if (!live && !previewWalk.empty()) {
            static std::unordered_map<uint64_t, glm::dvec3> base;
            // timed from when the world settles: the screenshot fires at PERLIN_PREVIEW_WAIT on the same clock
            const double t = std::chrono::duration<double>(frameStart - previewReadyAt).count();
            static const bool walking = PERLIN_ENV("PERLIN_PREVIEW_ENTITY_WALK") != nullptr;
            static const double walkTurn = PERLIN_ENV("PERLIN_PREVIEW_ENTITY_WALK_DIR")
                                               ? std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_WALK_DIR"))
                                               : 0.0;
            // head turns by this much between 0.5 s and 1.5 s
            static const float headTurn =
                PERLIN_ENV("PERLIN_PREVIEW_ENTITY_HEAD")
                    ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_HEAD")))
                    : 0.0f;
            for (entity::Entity& pe : entities)
                if (std::find(previewWalk.begin(), previewWalk.end(), pe.runtimeId) != previewWalk.end()) {
                    const glm::dvec3 b = base.emplace(pe.runtimeId, pe.pos).first->second;
                    const double yawRad = glm::radians(static_cast<double>(pe.yaw) + walkTurn);
                    // always from the base: Move takes eye height for players, feeding pos back would sink it
                    pe.pos = b + glm::dvec3(-std::sin(yawRad), 0.0, std::cos(yawRad)) *
                                     (walking ? std::sin(t * 1.5) * 1.5 : 0.0);
                    pe.headYaw = pe.yaw + headTurn * static_cast<float>(std::clamp(t - 0.5, 0.0, 1.0));
                    previewEntities.Move(pe.runtimeId, pe.pos, pe.pitch, pe.yaw, pe.headYaw, true);
                }
        }
        const bool firstPerson = camera.mode == PerspectiveMode::FirstPerson;
        if (live || !firstPerson || PERLIN_ENV("PERLIN_PREVIEW_SELF")) {
            entity::Entity self;
            self.runtimeId = 0xFFFFFFFFFFFF0001ull;
            self.type = "minecraft:player";
            self.isPlayer = self.isSelf = true;
            if (live && live->Invisible())
                self.flags |= entity::flag::Invisible;
            self.name = live ? live->PlayerName() : std::string("Perlin");
            self.pos = {camera.x,
                        camera.y - (live ? static_cast<double>(player.EyeOffset()) : entity::kPlayerEye),
                        camera.z};
            self.yaw = self.headYaw = camera.yaw - 90.0f;
            self.pitch = -camera.pitch;
            if (replayCtl.Playing()) {
                const replay::Pose rp = replayCtl.InterpolatedPose();
                if (rp.known) {
                    self.pos = {rp.x, rp.y - entity::kPlayerEye, rp.z};
                    self.yaw = self.headYaw = rp.yaw;
                    self.pitch = rp.pitch;
                }
            }
            self.moveSerial = 1;
            self.swingSerial = selfSwing;
            self.hurtSerial = live ? live->HurtCount() : 0;
            self.mainHand = hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)].name;
            for (int i = 0; i < 4; i++)
                self.armor[i] = hudState.armorItems[i].name;
            self.offHand = hudState.offhand.name;
            {
                const HandRenderer::RigState rig = handRenderer.Rig(0.0f);
                if (rig.using_ || rig.blocking || handRenderer.EatTicks() > 0)
                    self.flags |= entity::flag::UsingItem;
            }
            if (player.Sneaking() && !player.Crawling())
                self.flags |= entity::flag::Sneaking;
            self.crawling = player.Crawling();
            player.SetElytra(hudState.armorItems[1].name == "minecraft:elytra" &&
                             hudState.armorItems[1].aux < 431);
            if (player.Gliding())
                self.flags |= entity::flag::Gliding;
            if (player.Swimming())
                self.flags |= entity::flag::Swimming;
            if (live && !replayCtl.Playing()) {
                const LoadedSkin& ls = live->Skin();
                const std::string key = ls.skinId + (ls.slimArms ? "/slim" : "/classic") + "/" + ls.capeId;
                if (!selfSkinData || key != selfSkinKey) {
                    auto sd = std::make_shared<entity::SkinData>();
                    sd->serial = std::hash<std::string>{}(key) | 1ull;
                    sd->skinWidth = ls.skinWidth;
                    sd->skinHeight = ls.skinHeight;
                    sd->skinRgba = ls.skinRgba;
                    sd->capeWidth = ls.capeWidth;
                    sd->capeHeight = ls.capeHeight;
                    sd->capeRgba = ls.capeRgba;
                    sd->slim = ls.slimArms;
                    selfSkinData = sd;
                    selfSkinKey = key;
                }
                self.skin = selfSkinData;
            }
            if (replayCtl.Playing() && live) {
                const std::string& rn = replayCtl.Meta().playerName;
                for (const auto& tp : live->PlayerList())
                    if (tp.skin && !tp.skin->skinRgba.empty() &&
                        (tp.name == rn || tp.uid == static_cast<int64_t>(replayCtl.Meta().localRuntimeId))) {
                        self.skin = tp.skin;
                        if (tp.name == rn)
                            break;
                    }
            }
            fpSelf = self;
            if (live && live->RidingUid() != 0) {
                self.vehicleUid = live->RidingUid();
                self.flags |= entity::flag::Riding;
            }
            dollSelf = self;
            if (live && live->Sleeping()) {
                self.sleeping = true;
                ApplyBedPose(live->NetLevel(), self);
            }
            entities.push_back(std::move(self));
        }
        if (replayCtl.Playing() &&
            (!replayUi.showPlayers || !replayUi.showEntities || !replayUi.showNametags)) {
            entities.erase(std::remove_if(entities.begin(), entities.end(),
                                          [&](const entity::Entity& en) {
                                              return en.isPlayer ? !replayUi.showPlayers
                                                                 : !replayUi.showEntities;
                                          }),
                           entities.end());
            if (!replayUi.showNametags)
                for (entity::Entity& en : entities)
                    en.name.clear();
        }
        entity::EntityRenderer::FrameIn ein;
        ein.time = std::chrono::duration<double>(frameStart - startedAt).count();
        ein.dt = dt;
        {
            const glm::vec3 eyePos = camera.EyePosition();
            ein.camera = {eyePos.x, eyePos.y, eyePos.z};
        }
        ein.firstPerson =
            firstPerson && !(replayCtl.Playing() && replayCtl.cameraMode != replay::CameraMode::Follow);
        {
            const bool packShadows = renderer->PackShadowsActive() && options.shadowEntities;
            ein.blobShadows = options.shadows && !packShadows;
            ein.selfShadow = packShadows;
        }
        ein.ownNametag = options.thirdPersonNametag;
        ein.nametagDepth = fgActive > 1;
        ein.ownParts = options.skinParts;
        ein.skins3d = options.skins3d;
        {
            std::lock_guard lock(modAdv.mutex);
            if (std::chrono::steady_clock::now() < modAdv.poseUntil)
                ein.ownPose = glm::vec3(modAdv.pose.pitch, modAdv.pose.yaw, modAdv.pose.roll);
            if (!modAdv.frameLines.empty() && !(replayCtl.Playing() && replayUi.hideHud))
                ein.modLines = &modAdv.frameLines;
        }
        PrepareModRender(ein);
        ein.capePhysics = options.capePhysics == "shoulder" ? 2 : options.capePhysics == "off" ? 0 : 1;
        if (live) {
            int fast = -1, slow = -1;
            for (const auto& fx : live->GetEffects()) {
                if (fx.id == 3 || fx.id == 26)
                    fast = std::max(fast, fx.amplifier);
                else if (fx.id == 4)
                    slow = std::max(slow, fx.amplifier);
            }
            ein.ownSwingTicks = fast >= 0   ? 6.0f - static_cast<float>(1 + fast)
                                : slow >= 0 ? 6.0f + static_cast<float>((1 + slow) * 2)
                                            : 6.0f;
        }
        ein.frustum = &frustum;
        ein.sectionReached = [this](int sx, int sy, int sz) { return renderer->SectionReached(sx, sy, sz); };
        ein.cameraYaw = camera.yaw - 90.0f;
        ein.cameraPitch = -camera.pitch;
        if (camera.mode == PerspectiveMode::ThirdPersonFront) {
            ein.cameraYaw += 180.0f;
            ein.cameraPitch = camera.pitch;
        }
        ein.worldTicks = worldTicks;
        {
            int fw = 0, fh = 0;
            FramebufferSizeEffective(fw, fh);
            ein.pixelScale =
                fh > 0 ? 2.0f * std::tan(glm::radians(camera.fov) * 0.5f) / static_cast<float>(fh) : 0.0f;
            ein.lineWidthPx = std::max(2.5f, static_cast<float>(fw) / 1920.0f * 2.5f);
            ein.chunkBorders = debug.chunkBorders;
            ein.hitboxes = debug.hitboxes;
            ein.playerPos = glm::dvec3(camera.x, camera.y - 1.62, camera.z);
        }
        if (preview && previewLevel && PERLIN_ENV("PERLIN_PREVIEW_AIM")) {
            aimHit = Raycast(
                *previewLevel, &levelRenderer->Blocks(), {},
                [](const entity::Entity&) { return glm::vec2(0.6f, 1.8f); }, {camera.x, camera.y, camera.z},
                glm::dvec3(camera.Direction()), 5.7, 0.0);
        }
        if (aimHit.kind == AimHit::Kind::Block &&
            (options.showHud || (preview && PERLIN_ENV("PERLIN_PREVIEW_AIM")))) {
            ein.outline = true;
            ein.outlineLo = aimHit.boxLo;
            ein.outlineHi = aimHit.boxHi;
            ein.outlineShape = aimHit.shape;
        }
        const auto crackAt = [&](int x, int y, int z, int stage) {
            entity::EntityRenderer::FrameIn::Crack crack{x, y, z, stage, {}};
            if (live && levelRenderer) {
                std::vector<physics::Box> boxes;
                if (LevelCollision(live->NetLevel(), &levelRenderer->Blocks()).Selection(x, y, z, boxes))
                    for (const physics::Box& b : boxes)
                        crack.boxes.push_back({static_cast<float>(b.x0), static_cast<float>(b.y0),
                                               static_cast<float>(b.z0), static_cast<float>(b.x1),
                                               static_cast<float>(b.y1), static_cast<float>(b.z1)});
            }
            return crack;
        };
        if (live)
            for (const auto& c : live->Cracks()) {
                bool present = false;
                const uint16_t cur = live->NetLevel().GetBlock(c.x, c.y, c.z, present);
                if (present && cur == 0)
                    continue;
                if (!(localCrack.active && c.x == localCrack.x && c.y == localCrack.y && c.z == localCrack.z))
                    ein.cracks.push_back(crackAt(c.x, c.y, c.z, c.stage));
            }
        if (localCrack.active)
            ein.cracks.push_back(crackAt(localCrack.x, localCrack.y, localCrack.z, localCrack.stage));
        if (const char* cr = PERLIN_ENV("PERLIN_PREVIEW_CRACK"); cr && aimHit.kind == AimHit::Kind::Block)
            ein.cracks.push_back(crackAt(aimHit.bx, aimHit.by, aimHit.bz, std::atoi(cr)));
        ein.groundY = [&](double x, double y, double z) -> double {
            const int top =
                entityLevel.FindBlockBelow(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(z)),
                                           static_cast<int>(std::floor(y)), [](uint16_t id) {
                                               if (id == 0)
                                                   return false;
                                               const physics::BlockShape& sh = LevelCollision::ShapeOf(id);
                                               return sh.boxes.size() == 1 && sh.boxes[0].x0 <= 0.001 &&
                                                      sh.boxes[0].y0 <= 0.001 && sh.boxes[0].z0 <= 0.001 &&
                                                      sh.boxes[0].x1 >= 0.999 && sh.boxes[0].y1 >= 0.999 &&
                                                      sh.boxes[0].z1 >= 0.999;
                                           });
            return top <= Chunk::MIN_Y - 1 ? -1000.0 : static_cast<double>(top) + 1.0;
        };
        ein.light = [&](const glm::dvec3& p) -> glm::vec3 {
            const int bx = static_cast<int>(std::floor(p.x)), by = static_cast<int>(std::floor(p.y)),
                      bz = static_cast<int>(std::floor(p.z));
            const int64_t key = (static_cast<int64_t>(bx) * 73856093) ^
                                (static_cast<int64_t>(by) * 19349663) ^ (static_cast<int64_t>(bz) * 83492791);
            auto& entry = entityLight[key];
            if (frameStart - entry.at > std::chrono::milliseconds(300)) {
                entry.at = frameStart;
                const uint8_t packed = CachedLight(entityLevel, bx, by, bz, frameStart);
                int sky = std::clamp<int>(ChunkLighting::Result::Sky(packed), 0, 15);
                int blk = std::clamp<int>(ChunkLighting::Result::Block(packed), 0, 15);
                if (sky == 0 && blk == 0) {
                    static const int kN[6][3] = {{0, 1, 0},  {0, -1, 0}, {1, 0, 0},
                                                 {-1, 0, 0}, {0, 0, 1},  {0, 0, -1}};
                    for (const auto& n : kN) {
                        const uint8_t q =
                            CachedLight(entityLevel, bx + n[0], by + n[1], bz + n[2], frameStart);
                        sky = std::max<int>(sky, std::clamp<int>(ChunkLighting::Result::Sky(q), 0, 15));
                        blk = std::max<int>(blk, std::clamp<int>(ChunkLighting::Result::Block(q), 0, 15));
                    }
                }
                const uint8_t* px = params.lightmap + (static_cast<size_t>(sky) * kLightmapSize + blk) * 4;
                entry.colour = {px[0] / 255.0f, px[1] / 255.0f, px[2] / 255.0f};
                entry.sky = sky;
                entry.block = blk;
            }
            if (!dyn.empty()) {
                const int extra = static_cast<int>(dynlight::LevelAt(dyn, p + glm::dvec3(0.0, 0.5, 0.0)));
                if (extra > entry.block) {
                    const uint8_t* px =
                        params.lightmap + (static_cast<size_t>(entry.sky) * kLightmapSize + extra) * 4;
                    return {px[0] / 255.0f, px[1] / 255.0f, px[2] / 255.0f};
                }
            }
            return entry.colour;
        };
        if (entityLight.size() > 4096)
            entityLight.clear();
        TickParticles(f, entityLevel, ein, entities);
        {
            frame::ProfileScope p(frame::Profiler::Entities);
            entityRenderer.Render(entities, ein);
        }
        RenderWeather(f);
        {
            static std::vector<SpecialBlock> specialBlocks;
            static std::vector<entity::BlockEntityDraw> blockEntityDraws;
            specialBlocks.clear();
            blockEntityDraws.clear();
            if (levelRenderer) {
                levelRenderer->CollectSpecials(camera.x, camera.z, 64.0f, specialBlocks);
                static const BlockEntityStore emptyStore;
                entity::BuildBlockEntityDraws(specialBlocks, live ? live->BlockEntities() : emptyStore,
                                              blockEntityDraws);
                entityRenderer.RenderBlockEntities(blockEntityDraws, ein);
            }
        }
    }

    void Perlin::TickParticles(FrameContext& f, Level& entityLevel, entity::EntityRenderer::FrameIn& ein,
                               const std::vector<entity::Entity>& entities) {
        auto& dt = f.dt;
        auto& live = f.live;
        if (!particles.tileFor && levelRenderer) {
            particles.tileFor = [this](uint16_t id, int bx, int by, int bz, particle::TileChoice& out) {
                const BlockAppearance& app = levelRenderer->Blocks().Look(id);
                const std::string pname = BlockPalette::Instance().Name(id);
                const auto pHas = [&](const char* part) { return pname.contains(part); };
                const bool untinted =
                    pHas("cherry_leaves") || pHas("azalea") || pHas("pale_oak_leaves") || pHas("leaf_litter");
                const bool foliageLike = !untinted && (pHas("leaves") || pHas("vine"));
                const bool grassLike = !untinted && (pHas("grass") || pHas("fern") || pHas("sugar_cane"));
                const bool grassBlock = pHas("grass_block");
                int tile =
                    (app.overlayTile > 0 && !grassBlock) ? app.bottom : (app.side ? app.side : app.top);
                if (grassBlock && app.top)
                    tile = app.top;
                if (!tile)
                    tile = app.top ? app.top : app.bottom;
                if (!tile)
                    return false;
                out.tile = tile;
                out.tint =
                    (foliageLike || grassLike || (app.overlayTile <= 0 && (app.tintSide || app.tintTop)))
                        ? glm::vec3(app.tintR, app.tintG, app.tintB)
                        : glm::vec3(1.0f);
                if ((foliageLike || grassLike) && out.tint == glm::vec3(0.0f))
                    out.tint = glm::vec3(0.45f, 0.75f, 0.3f);
                const bool wantTint = foliageLike || grassLike || out.tint != glm::vec3(1.0f);
                if (wantTint && particleLevel && levelRenderer->Tints().Ready()) {
                    const BiomeTints& tints = levelRenderer->Tints();
                    const std::string name = BlockPalette::Instance().Name(id);
                    const auto has = [&](const char* part) { return name.contains(part); };
                    if (has("birch_leaves")) {
                        const auto c = tints.MapAverage("birch");
                        out.tint = {c[0], c[1], c[2]};
                    } else if (has("spruce_leaves")) {
                        const auto c = tints.MapAverage("evergreen");
                        out.tint = {c[0], c[1], c[2]};
                    } else if (has("leaves") || has("vine")) {
                        glm::vec3 sum(0.0f);
                        for (int dz = -2; dz <= 2; dz++)
                            for (int dx = -2; dx <= 2; dx++) {
                                const auto c = tints.Foliage(
                                    tints.NameForId(particleLevel->GetBiome(bx + dx, by, bz + dz)));
                                sum += glm::vec3(c[0], c[1], c[2]);
                            }
                        out.tint = sum / 25.0f;
                    } else if (has("grass") || has("fern") || has("sugar_cane")) {
                        glm::vec3 sum(0.0f);
                        for (int dz = -2; dz <= 2; dz++)
                            for (int dx = -2; dx <= 2; dx++) {
                                const auto c = tints.Grass(
                                    tints.NameForId(particleLevel->GetBiome(bx + dx, by, bz + dz)));
                                sum += glm::vec3(c[0], c[1], c[2]);
                            }
                        out.tint = sum / 25.0f;
                    }
                }
                return true;
            };
        }
        static std::vector<int8_t> solidById;
        particleLevel = &entityLevel;
        particle::WorldAccess pw;
        pw.block = [&](int x, int y, int z) -> uint16_t {
            bool loaded = false;
            const uint16_t id = entityLevel.GetBlock(x, y, z, loaded);
            return loaded ? id : uint16_t(0);
        };
        pw.solid = [&](int x, int y, int z) {
            bool loaded = false;
            const uint16_t id = entityLevel.GetBlock(x, y, z, loaded);
            if (!loaded || id == 0)
                return false;
            if (id >= solidById.size())
                solidById.resize(static_cast<size_t>(id) + 256, -1);
            int8_t& s = solidById[id];
            if (s < 0) {
                const physics::BlockShape& sh = LevelCollision::ShapeOf(id);
                s = (sh.boxes.size() == 1 && sh.boxes[0].x0 <= 0.001 && sh.boxes[0].y0 <= 0.001 &&
                     sh.boxes[0].z0 <= 0.001 && sh.boxes[0].x1 >= 0.999 && sh.boxes[0].y1 >= 0.999 &&
                     sh.boxes[0].z1 >= 0.999)
                        ? 1
                        : 0;
            }
            return s == 1;
        };
        if (live) {
            static double itemAcc = 0.0;
            itemAcc = std::min(itemAcc + static_cast<double>(dt), 0.15);
            const auto itemSolid = [&](int x, int y, int z) {
                bool loaded = false;
                const uint16_t id = entityLevel.GetBlock(x, y, z, loaded);
                return !loaded || (id != 0 && !LevelCollision::ShapeOf(id).Empty());
            };
            while (itemAcc >= 0.05) {
                itemAcc -= 0.05;
                live->Entities().StepItems(itemSolid);
            }
        }
        pw.light = ein.light;
        particle::DataWorld dataWorld;
        dataWorld.solid = pw.solid;
        dataWorld.light = pw.light;
        if (live) {
            for (const BedrockSession::ParticleEvent& pe : live->TakeParticles()) {
                const glm::dvec3 at(pe.x, pe.y, pe.z);
                if (pe.event == 0) {
                    if (player.Gliding() && pe.name.contains("firework"))
                        continue;
                    if (!dataParticles.Spawn(pe.name, at, pe.molang))
                        particles.SpawnNamed(pe.name, at);
                } else if (pe.event == 2001 || pe.event == 2021 || pe.event == 16404) {
                    const int bx = static_cast<int>(std::floor(pe.x)),
                              by = static_cast<int>(std::floor(pe.y)),
                              bz = static_cast<int>(std::floor(pe.z));
                    const bool mine =
                        lastLocalBreak.x == bx && lastLocalBreak.y == by && lastLocalBreak.z == bz &&
                        std::chrono::steady_clock::now() - lastLocalBreak.at < std::chrono::milliseconds(800);
                    if (!mine)
                        particles.Destroy(bx, by, bz, BlockPalette::Instance().InternRuntime(pe.data));
                } else {
                    particles.SpawnLevelEvent(pe.event, at, pe.data);
                }
            }
        }
        if (!live)
            if (const char* pb = PERLIN_ENV("PERLIN_PREVIEW_BREAK")) {
                static double burstClock = 0;
                burstClock += dt;
                int bx = 0, by = 0, bz = 0;
                if (burstClock > 0.6 && std::sscanf(pb, "%d,%d,%d", &bx, &by, &bz) == 3) {
                    burstClock = 0;
                    bool loaded = false;
                    const uint16_t id = entityLevel.GetBlock(bx, by, bz, loaded);
                    if (loaded && id)
                        particles.Destroy(bx, by, bz, id);
                }
            }
        particleClock += std::min(dt, 0.25f);
        frame::ProfileScope particleScope(frame::Profiler::Particles);
        for (int steps = 0; particleClock >= 0.05 && steps < 5; steps++, particleClock -= 0.05)
            particles.Tick(pw, glm::dvec3(camera.x, camera.y, camera.z));
        if (particleClock > 0.2)
            particleClock = 0;
        {
            static auto dataParticleCheck = std::chrono::steady_clock::now() - std::chrono::seconds(10);
            if (std::chrono::steady_clock::now() - dataParticleCheck > std::chrono::seconds(2)) {
                dataParticleCheck = std::chrono::steady_clock::now();
                dataParticles.Configure(ServerPacks::Instance().Directories());
            }
            dataParticles.Update(std::min(dt, 0.1f), dataWorld);
        }
        particleQuads.clear();
        particles.Collect(static_cast<float>(particleClock / 0.05), pw, particleQuads);
        if (const int fireTile = levelRenderer->Blocks().TileContaining("fire_1"); fireTile >= 0) {
            const int soulTile = levelRenderer->Blocks().TileContaining("soul_fire_1");
            for (const entity::Entity& burner : entities) {
                const bool selfBurning = burner.isSelf && hudState.burning;
                if (!selfBurning && !(burner.flags & entity::flag::Onfire))
                    continue;
                if ((burner.flags & entity::flag::FireImmune) || burner.dying || burner.isItem)
                    continue;
                const glm::dvec3 at = burner.pos;
                const float scale = burner.scale;
                // blue flames when standing on soul soil/sand, matches the valley look
                int tile = fireTile;
                {
                    bool loaded = false;
                    const uint16_t below = live->NetLevel().GetBlock(
                        static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y - 0.5)),
                        static_cast<int>(std::floor(at.z)), loaded);
                    if (loaded && soulTile >= 0) {
                        const std::string bn = BlockPalette::Instance().Name(below);
                        if (bn == "soul_sand" || bn == "soul_soil")
                            tile = soulTile;
                    }
                }
                for (int k = 0; k < 6; k++) {
                    const float a = static_cast<float>(k) * 1.0472f + 0.5f;
                    const float h = 0.15f + 0.3f * static_cast<float>(k % 3);
                    particle::Quad q;
                    q.centre = at + glm::dvec3(std::cos(a) * 0.3 * scale, (h + 0.35f * (k / 3)) * scale,
                                               std::sin(a) * 0.3 * scale);
                    q.half = 0.4f * scale;
                    q.terrainTile = tile;
                    particleQuads.push_back(q);
                }
            }
        }
        ein.particles = &particleQuads;
        ein.dataParticles = &dataParticles;
        ein.dataWorld = dataWorld;
        ein.tilePixels = [this](int tile, std::vector<uint8_t>& rgba) {
            return levelRenderer->Blocks().TilePixels(tile, rgba);
        };
    }

    void Perlin::RenderWeather(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& live = f.live;
        auto& rain = f.rain;
        auto& thunder = f.thunder;
        auto& dimension = f.dimension;
        auto& env = f.env;
        auto& startedAt = loop->startedAt;
        auto& previewLevel = loop->previewLevel;
        WeatherRenderer::In wx;
        wx.renderer = renderer.get();
        wx.level = live ? &live->NetLevel() : previewLevel.get();
        wx.tints = &levelRenderer->Tints();
        wx.camera = glm::dvec3(camera.x, camera.y, camera.z);
        wx.rain = rain;
        wx.thunder = thunder;
        wx.seconds = std::chrono::duration<double>(frameStart - startedAt).count();
        wx.light = std::clamp(env.skyLightFactor, 0.0f, 1.0f);
        wx.enabled = live != nullptr && dimension == Dimension::Overworld;
        {
            frame::ProfileScope weatherScope(frame::Profiler::Weather);
            weather.Draw(wx);
        }
        if (wx.enabled && rain > 0.1f) {
            static double nextRainSound = 0.0;
            if (wx.seconds >= nextRainSound) {
                nextRainSound = wx.seconds + 3.2;
                sounds.PlayNamed("ambient.weather.rain", wx.camera, std::min(1.0f, rain) * 0.4f, 1.0f, false);
            }
        }
    }

    void Perlin::RenderHand(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& dt = f.dt;
        auto& live = f.live;
        auto& worldTicks = f.worldTicks;
        auto& partial = f.partial;
        auto& handViewEffect = f.handViewEffect;
        auto& dyn = f.dyn;
        auto& params = f.params;
        auto& fpSelf = f.fpSelf;
        auto& startedAt = loop->startedAt;
        auto& damage = loop->damage;
        auto& preview = loop->preview;
        auto& handSky = loop->handSky;
        auto& handBlock = loop->handBlock;
        if ((live || preview) &&
            (!replayCtl.Playing() || replayCtl.cameraMode == replay::CameraMode::Follow) &&
            camera.mode == PerspectiveMode::FirstPerson && options.showHud &&
            hudState.mode != ui::GameMode::Spectator && !death.shown &&
            ui::UiAssets::Instance().VanillaRoot().size()) {
            HandRenderer::FrameIn handFrame;
            handFrame.partial = partial;
            handFrame.yaw = camera.yaw - 90.0f;
            handFrame.pitch = -camera.pitch;
            {
                const int sky = std::clamp(handSky, 0, 15);
                int blk = std::clamp(handBlock, 0, 15);
                if (!dyn.empty())
                    blk = std::max(blk, static_cast<int>(dynlight::LevelAt(
                                            dyn, glm::dvec3(camera.x, camera.y, camera.z))));
                const uint8_t* px = params.lightmap + (static_cast<size_t>(sky) * kLightmapSize + blk) * 4;
                handFrame.light = {px[0] / 255.0f, px[1] / 255.0f, px[2] / 255.0f};
            }
            handFrame.viewEffect = handViewEffect;
            handFrame.skins3d = options.skins3d;
            handFrame.hurt = damage.HurtFraction(partial);
            static LoadedSkin replayHandSkin;
            static uint64_t replayHandSerial = 0;
            const LoadedSkin* handSkin = session ? &session->Skin() : nullptr;
            if (session && replayCtl.Playing()) {
                for (const auto& tp : session->PlayerList()) {
                    if (!tp.skin || tp.skin->skinRgba.empty() || tp.name != replayCtl.Meta().playerName)
                        continue;
                    if (replayHandSerial != tp.skin->serial) {
                        replayHandSerial = tp.skin->serial;
                        replayHandSkin = LoadedSkin{};
                        replayHandSkin.skinWidth = tp.skin->skinWidth;
                        replayHandSkin.skinHeight = tp.skin->skinHeight;
                        replayHandSkin.skinRgba = tp.skin->skinRgba;
                        replayHandSkin.slimArms = tp.skin->slim;
                        replayHandSkin.skinId = "replay-" + std::to_string(tp.skin->serial);
                    }
                    handSkin = &replayHandSkin;
                    break;
                }
            }
            if (session && handSkin) {
                handFrame.skin = handSkin;
                handFrame.skinSerial =
                    std::hash<std::string>{}(handSkin->skinId) * 2 + (handSkin->slimArms ? 1 : 0);
                handFrame.slimArm = handSkin->slimArms;
                handFrame.leftSleeve = options.skinParts.leftSleeve;
                handFrame.rightSleeve = options.skinParts.rightSleeve;
            }
            {
                frame::ProfileScope p(frame::Profiler::Hand);
                handRenderer.Render(handFrame, options.hand);
            }
            {
                int sw = 0, sh = 0;
                renderer->FramebufferSize(sw, sh);
                handRenderer.RenderScope(sw, sh, partial);
            }
            if (options.hand.bedrockRig && PERLIN_ENV("PERLIN_BEDROCK_RIG") &&
                (fpSelf || previewEntitiesSpawned || true)) {
                if (!fpSelf) {
                    fpSelf.emplace();
                    fpSelf->runtimeId = 0xFFFFFFFFFFFF0001ull;
                    fpSelf->type = "minecraft:player";
                    fpSelf->isPlayer = fpSelf->isSelf = true;
                    fpSelf->pos = {camera.x, camera.y - static_cast<double>(player.EyeOffset()), camera.z};
                    fpSelf->yaw = fpSelf->headYaw = camera.yaw - 90.0f;
                    fpSelf->pitch = -camera.pitch;
                    fpSelf->moveSerial = 1;
                }
                const HandRenderer::RigState rs = handRenderer.Rig(partial);
                fpSelf->mainHand = rs.main;
                fpSelf->offHand = options.hand.showOffhand ? rs.off : std::string();
                if (options.hand.leftHanded)
                    std::swap(fpSelf->mainHand, fpSelf->offHand);
                entity::EntityRenderer::FrameIn fin;
                fin.time = std::chrono::duration<double>(frameStart - startedAt).count();
                fin.dt = dt;
                fin.camera = {camera.x, camera.y, camera.z};
                fin.firstPerson = true;
                fin.cameraYaw = camera.yaw - 90.0f;
                fin.cameraPitch = -camera.pitch;
                fin.worldTicks = worldTicks;
                entity::EntityRenderer::FpState fps;
                fps.attackTime = rs.attackTime;
                fps.armHeight = rs.armHeight;
                fps.offArmHeight = rs.offArmHeight;
                fps.bob = options.hand.viewBobbing && options.hand.bobHand;
                fps.using_ = rs.using_;
                fps.blocking = rs.blocking;
                fps.usedTicks = rs.usedTicks;
                fps.viewEffect = handFrame.viewEffect;
                fps.light = handFrame.light;
                entityRenderer.RenderFirstPerson(*fpSelf, fin, fps);
            }
        }
    }

    void Perlin::UpdateHudAndScreens(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& dt = f.dt;
        auto& live = f.live;
        auto& startedAt = loop->startedAt;
        static double hudRetry = 0;
        const double nowSeconds = std::chrono::duration<double>(frameStart - startedAt).count();
        const std::string vanillaRoot = options.bedrockSamplesDir + "/repo/resource_pack";
        if (!hud && nowSeconds >= hudRetry) {
            hudRetry = nowSeconds + 2.0;
            if (std::filesystem::exists(vanillaRoot + "/ui/hud_screen.json")) {
                ui::UiAssets::Instance().Configure(vanillaRoot, &levelRenderer->Blocks());
                auto candidate = std::make_unique<ui::Hud>();
                if (candidate->Initialize(*renderer, vanillaRoot, paths::DataDir()))
                    hud = std::move(candidate);
                else
                    candidate->Shutdown();
            }
        }
        if (hud) {
            int fbW = 0, fbH = 0;
            renderer->FramebufferSize(fbW, fbH);
            frame::ProfileScope hudScope(frame::Profiler::Hud);
            hud->Update(hudState, fbW, fbH, options.guiScale);
            if (optionsUi.touchEditing) {
                if (!optionsUi.touchEditHidden) {
                    optionsUi.touchEditHidden = true;
                    hud->Overlay().ClearOverlay();
                }
            } else {
                optionsUi.touchEditHidden = false;
                UpdateModLayer(*hud);
                UpdateServerLayer(*hud, live, fbW, fbH);
            }
            const bool titleUp = !optionsUi.touchEditing && UpdateTitleScreen(*hud, fbW, fbH);
            const bool disconnectUp =
                !optionsUi.touchEditing && !titleUp && UpdateDisconnectScreen(*hud, fbW, fbH);
            const bool loadingUp =
                !optionsUi.touchEditing && !titleUp && !disconnectUp && UpdateLoadingScreen(*hud, fbW, fbH);
            if (!optionsUi.touchEditing && !titleUp && !disconnectUp && !loadingUp && replayCtl.Playing() &&
                live && live->IsReplay()) {
                if (exportJob.active)
                    ExportOverlay(*hud, fbW, fbH);
                else
                    UpdateReplayScreen(*hud, live, fbW, fbH);
            } else if (!optionsUi.touchEditing && !titleUp && !disconnectUp && !loadingUp) {
                UpdateCreditsScreen(*hud, live, fbW, fbH);
                if (!credits.shown) {
                    UpdateDeathScreen(*hud, live, hudState, fbW, fbH);
                    UpdateFormScreen(*hud, live, fbW, fbH);
                    UpdateInventoryScreen(*hud, live, hudState, fbW, fbH);
                    UpdatePauseScreen(*hud, live, fbW, fbH);
                    UpdateBedScreen(*hud, live, fbW, fbH);
                    UpdateChatClicks(*hud, live, fbW, fbH);
                }
            }
            {
                {
                    bool keyboard = chat.IsOpen();
                    if (titleUp)
                        keyboard = keyboard || title.page == ui::TitlePage::Edit ||
                                   title.page == ui::TitlePage::Direct ||
                                   title.page == ui::TitlePage::AddRealm ||
                                   title.page == ui::TitlePage::AccountName ||
                                   title.page == ui::TitlePage::ReplayRename;
                    if (form.shown && form.model.focus >= 0 &&
                        static_cast<size_t>(form.model.focus) < form.model.elements.size())
                        keyboard =
                            keyboard || form.model.elements[static_cast<size_t>(form.model.focus)].kind ==
                                            ui::FormElement::Kind::Input;
                    if (inventory.shown && creativeScreen.tab == ui::kCreativeSearchTab)
                        keyboard = true;
                    this->input.SetKeyboard(keyboard);
                }
                const bool pressed = this->input.IsMouseDown(0);
                const bool onButton = hud->Overlay().OverlayHoverIsButton();
                if (pressed && !uiClickWasDown && onButton && !chat.IsOpen()) {
                    std::string clickName = "random.click";
                    float clickVolume = 1.0f, clickPitch = 1.0f;
                    hud->Overlay().OverlayHoverSound(clickName, clickVolume, clickPitch);
                    if (!clickName.empty())
                        sounds.PlayNamed(clickName, {camera.x, camera.y, camera.z}, clickVolume, clickPitch,
                                         false);
                }
                uiClickWasDown = pressed;
            }
            DrawModTint(fbW, fbH);
            hud->Render();
            DrawModGfx(fbW, fbH);
            const bool creativeUi =
                hudState.mode == ui::GameMode::Creative || PERLIN_ENV("PERLIN_PREVIEW_CREATIVE");
            if (inventory.shown && dollSelf && !inventory.workbench && !inventory.horse && !inventory.chest &&
                (!creativeUi || creativeScreen.dollVisible)) {
                const auto [scale, guiW, guiH] = detail::OverlayViewport(*hud, fbW, fbH);
                const bool creativeDoll = creativeUi;
                const float px = static_cast<float>(creativeDoll ? (guiW - ui::kCreativeW) / 2
                                                                 : (guiW - ui::kInvPanelW) / 2 +
                                                                       (inventory.book ? 77 : 0)),
                            py = static_cast<float>(creativeDoll ? (guiH - ui::kCreativeH) / 2
                                                                 : (guiH - ui::kInvPanelH) / 2);
                const float centerX = creativeDoll ? px + 89.0f : px + 51.0f,
                            centerY = creativeDoll ? py + 27.5f : py + 43.0f;
                const float f = std::atan((centerX - inventory.mouseX) / 40.0f),
                            g = std::atan((centerY - inventory.mouseY) / 40.0f);
                entity::Entity doll = *dollSelf;
                doll.runtimeId = 0xFFFFFFFFFFFF0002ull;
                doll.pos = {0.0, 0.0, 0.0};
                doll.yaw = f * 20.0f * 1.0f;
                doll.headYaw = f * 40.0f;
                doll.pitch = -g * 20.0f;
                doll.moveSerial = 1;
                doll.isSelf = true;
                doll.flags &= ~entity::flag::Sneaking;
                entity::EntityRenderer::FrameIn din;
                din.time = std::chrono::duration<double>(frameStart - startedAt).count();
                din.dt = dt;
                din.firstPerson = false;
                din.camera = {0, 0, 0};
                const float dollScale = creativeDoll ? 20.0f : 30.0f;
                const int clip[4] = {static_cast<int>((creativeDoll ? px + 73 : px + 26) * scale),
                                     static_cast<int>((creativeDoll ? py + 6 : py + 8) * scale),
                                     (creativeDoll ? 32 : 49) * scale, (creativeDoll ? 43 : 70) * scale};
                entityRenderer.RenderDoll(doll, din, (creativeDoll ? px + 89.0f : px + 50.5f) * scale,
                                          (centerY + (1.8f / 2.0f + 0.0625f) * dollScale) * scale,
                                          dollScale * scale, clip);
            }
            if (inventory.shown && inventory.horse && live) {
                std::vector<entity::Entity> all;
                live->Entities().Snapshot(all);
                const int64_t mountRuntime = live->GetInventory().horseEntity;
                const entity::Entity* found = nullptr;
                for (const entity::Entity& e : all)
                    if (static_cast<int64_t>(e.runtimeId) == mountRuntime ||
                        (live->RidingUid() != 0 && e.uniqueId == live->RidingUid())) {
                        found = &e;
                        break;
                    }
                if (found) {
                    const auto [scale, guiW, guiH] = detail::OverlayViewport(*hud, fbW, fbH);
                    const float px = static_cast<float>((guiW - ui::kInvPanelW) / 2),
                                py = static_cast<float>((guiH - ui::kInvPanelH) / 2);
                    entity::Entity doll = *found;
                    doll.runtimeId = 0xFFFFFFFFFFFF0003ull;
                    doll.pos = {0.0, 0.0, 0.0};
                    doll.vehicleUid = 0;
                    doll.flags &= ~(entity::flag::Riding | entity::flag::Sneaking);
                    const float f = std::atan((px + 52.0f - inventory.mouseX) / 40.0f),
                                g = std::atan((py + 43.0f - inventory.mouseY) / 40.0f);
                    doll.yaw = 0.0f + f * 20.0f;
                    doll.headYaw = f * 40.0f;
                    doll.pitch = -g * 20.0f;
                    doll.moveSerial = 1;
                    entity::EntityRenderer::FrameIn din;
                    din.time = std::chrono::duration<double>(frameStart - startedAt).count();
                    din.dt = dt;
                    din.firstPerson = false;
                    din.camera = {0, 0, 0};
                    const int clip[4] = {static_cast<int>((px + 26) * scale),
                                         static_cast<int>((py + 17) * scale), 52 * scale, 53 * scale};
                    entityRenderer.RenderDoll(doll, din, (px + 52.0f) * scale, (py + 65.0f) * scale,
                                              22.0f * scale, clip);
                }
            }
            float cx, cy, cw, ch;
            if (options.showHud && !replayCtl.Playing() && !death.shown && !form.shown && !inventory.shown &&
                !pause.shown && !disconnect.shown && !loading.shown && !titleScreen.shown && !credits.shown &&
                (camera.mode == PerspectiveMode::FirstPerson || options.thirdPersonCrosshair) &&
                hudState.mode != ui::GameMode::Spectator && hud->Cursor(cx, cy, cw, ch))
                renderer->DrawHudCursor(cx, cy, cw, ch);
        }
    }

    void Perlin::UpdateTouch(FrameContext& f) {
        auto& width = f.width;
        auto& height = f.height;
        auto& live = f.live;
        auto& preview = loop->preview;
        TouchState touchState;
        touchState.flying = player.Flying();
        touchState.inWater = player.InWater() && !player.State().onGround;
        touchState.riding = live && live->RidingUid() != 0;
        touchState.editing = optionsUi.touchEditing;
        touchState.hudScale = hud ? hud->Scale() : 0;
        touchState.scale = options.touchScale;
        touchState.opacity = options.touchOpacity;
        touchState.sensitivity = options.touchSensitivity;
        touchState.layout = options.touchLayout;
        for (size_t a = 0; a < kActionCount; a++)
            touchState.bindings[a] = options.GetKey(static_cast<Action>(a));
        platform->SetTouchState(touchState);
        std::map<std::string, TouchPlace> edited;
        if (platform->TakeTouchLayout(edited)) {
            options.touchLayout = edited;
            options.Save();
            optionsUi.touchEditing = false;
            optionsUi.signature.clear();
        }
        const bool previewTouch = preview && PERLIN_ENV("PERLIN_PREVIEW_TOUCH");
        if (previewTouch) {
            static TouchControls previewControls;
            previewControls.SetViewport(width, height);
            previewControls.SetState(touchState);
            previewControls.Draw(*renderer, true);
        } else if (optionsUi.touchEditing || (!death.shown && !pause.shown && !inventory.shown &&
                                              !disconnect.shown && !loading.shown && !titleScreen.shown)) {
            platform->DrawTouchOverlay(*renderer);
        }
    }
}
