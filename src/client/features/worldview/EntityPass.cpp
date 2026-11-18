#include "client/features/worldview/EntityPass.hpp"
#include "client/systems/diagnostics/FrameProfiler.hpp"
#include "client/systems/entity/BlockEntityView.hpp"

#include <array>
#include "util/Clock.hpp"
#include "util/Log.hpp"
#include "util/Env.hpp"

namespace perlin::worldview {
    using detail::Clock;

    namespace {
        void ApplyBedPose(BlockPalette& pal, Level& level, entity::Entity& e) {
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
    }
    EntityPass::EntityPass(Deps d)
        : blockPalette(d.blockPalette), uiAssets(d.uiAssets), serverPacks(d.serverPacks), debugOverlay(d.debugOverlay), interaction(d.interaction), lightCache(d.lightCache), modHost(d.modHost), replayStudio(d.replayStudio), options(d.options), renderer(d.renderer), levelRenderer(d.levelRenderer), hudState(d.hudState), handRenderer(d.handRenderer), entityRenderer(d.entityRenderer), weather(d.weather), particles(d.particles), dataParticles(d.dataParticles), particleLevel(d.particleLevel), sounds(d.sounds), fgActive(d.fgActive), camera(d.camera), player(d.player), cameraDetached(d.cameraDetached), loop(d.loop), previewEntities(d.previewEntities), dollSelf(d.dollSelf), replayCtl(d.replayCtl) {}

    void EntityPass::RenderEntities(FrameContext& f) {
        auto& live = f.live;
        if (!(live || loop->previewLevel) || uiAssets.VanillaRoot().empty())
            return;
        Level& entityLevel = live ? live->NetLevel() : *loop->previewLevel;
        std::vector<entity::Entity> entities = CollectFrameEntities(f);
        const bool firstPerson = camera.mode == PerspectiveMode::FirstPerson && !cameraDetached;
        if (live || !firstPerson || PERLIN_ENV("PERLIN_PREVIEW_SELF"))
            entities.push_back(BuildSelfEntity(f));
        FilterEntitiesForReplay(entities);
        entity::EntityRenderer::FrameIn ein = BuildEntityFrameIn(f, firstPerson);
        AttachBlockCracks(f, ein);
        AttachEntityLighting(f, entityLevel, ein);
        TickParticles(f, entityLevel, ein, entities);
        {
            frame::ProfileScope p(frame::Profiler::Entities);
            entityRenderer.Render(entities, ein);
        }
        RenderWeather(f);
        RenderBlockEntityPass(f, ein);
    }

    std::vector<entity::Entity> EntityPass::CollectFrameEntities(FrameContext& f) {
        auto& frameStart = f.frameStart;
        auto& live = f.live;
        auto& previewReadyAt = loop->previewReadyAt;
        auto& frameEntities = loop->frameEntities;
        if (!live && !previewEntitiesSpawned && PERLIN_ENV("PERLIN_PREVIEW_ENTITIES")) {
            previewEntitiesSpawned = true;
            SpawnPreviewEntities();
        }
        std::vector<entity::Entity> entities = frameEntities;
        modHost.ApplyModSkins(entities);
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
            std::ranges::sort(entities, [](const entity::Entity& a, const entity::Entity& b) {
                return a.runtimeId < b.runtimeId;
            });
            entities[1].vehicleUid = entities[0].uniqueId;
        }
        entityRenderer.ResolveSeats(entities);
        if (live)
            for (entity::Entity& e : entities)
                if (e.sleeping && e.isPlayer)
                    ApplyBedPose(blockPalette, live->NetLevel(), e);
        if (!live && !previewWalk.empty()) {
            const double t = std::chrono::duration<double>(frameStart - previewReadyAt).count();
            static const bool walking = PERLIN_ENV("PERLIN_PREVIEW_ENTITY_WALK") != nullptr;
            static const double walkTurn = PERLIN_ENV("PERLIN_PREVIEW_ENTITY_WALK_DIR")
                                               ? std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_WALK_DIR"))
                                               : 0.0;
            static const float headTurn =
                PERLIN_ENV("PERLIN_PREVIEW_ENTITY_HEAD")
                    ? static_cast<float>(std::atof(PERLIN_ENV("PERLIN_PREVIEW_ENTITY_HEAD")))
                    : 0.0f;
            for (entity::Entity& pe : entities)
                if (std::ranges::find(previewWalk, pe.runtimeId) != previewWalk.end()) {
                    const glm::dvec3 b = base.emplace(pe.runtimeId, pe.pos).first->second;
                    const double yawRad = glm::radians(static_cast<double>(pe.yaw) + walkTurn);
                    pe.pos = b + glm::dvec3(-std::sin(yawRad), 0.0, std::cos(yawRad)) *
                                     (walking ? std::sin(t * 1.5) * 1.5 : 0.0);
                    pe.headYaw = pe.yaw + headTurn * static_cast<float>(std::clamp(t - 0.5, 0.0, 1.0));
                    previewEntities.Move(pe.runtimeId, pe.pos, pe.pitch, pe.yaw, pe.headYaw, true);
                }
        }
        return entities;
    }

    entity::Entity EntityPass::BuildSelfEntity(FrameContext& f) {
        auto& live = f.live;
        auto& fpSelf = f.fpSelf;
        entity::Entity self;
        self.runtimeId = 0xFFFFFFFFFFFF0001ull;
        self.type = "minecraft:player";
        self.isPlayer = self.isSelf = true;
        if (live && live->Invisible())
            self.flags |= entity::flag::Invisible;
        self.name = live ? live->PlayerName() : std::string("Perlin");
        if (live)
            self.nameTag = live->OwnNameTag();
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
        self.swingSerial = interaction.selfSwing;
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
            ApplyBedPose(blockPalette, live->NetLevel(), self);
        }
        return self;
    }

    void EntityPass::FilterEntitiesForReplay(std::vector<entity::Entity>& entities) {
        if (replayCtl.Playing() &&
            replayStudio.FiltersEntities()) {
            entities.erase(std::remove_if(entities.begin(), entities.end(),
                                          [&](const entity::Entity& en) {
                                              return en.isPlayer ? !replayStudio.ShowsPlayers()
                                                                 : !replayStudio.ShowsEntities();
                                          }),
                           entities.end());
            if (!replayStudio.ShowsNametags())
                for (entity::Entity& en : entities)
                    en.name.clear();
        }
    }

    entity::EntityRenderer::FrameIn EntityPass::BuildEntityFrameIn(FrameContext& f, bool firstPerson) {
        auto& frameStart = f.frameStart;
        auto& dt = f.dt;
        auto& worldTicks = f.worldTicks;
        auto& frustum = f.frustum;
        auto& startedAt = loop->startedAt;
        auto& preview = loop->preview;
        auto& previewLevel = loop->previewLevel;
        entity::EntityRenderer::FrameIn ein;
        ein.time = std::chrono::duration<double>(frameStart - startedAt).count();
        ein.dt = dt;
        ein.partialTick = replayCtl.Playing()
                              ? static_cast<float>(std::clamp(loop->replayActorClock / physics::kTickSeconds, 0.0, 1.0))
                              : player.PartialTick();
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
        ein.ownLeftHanded = options.hand.leftHanded;
        ein.nametags = options.showHud || replayCtl.Playing();
        ein.nametagBackground = options.nametagBackground;
        ein.nametagShadow = options.nametagShadow;
        ein.perlinBadges = options.perlinBadges;
        ein.nametagDepth = fgActive > 1;
        ein.ownParts = options.skinParts;
        ein.skins3d = options.skins3d;
        {
            std::lock_guard lock(modHost.modAdv.mutex);
            if (std::chrono::steady_clock::now() < modHost.modAdv.poseUntil)
                ein.ownPose = glm::vec3(modHost.modAdv.pose.pitch, modHost.modAdv.pose.yaw, modHost.modAdv.pose.roll);
            if (!modHost.modAdv.frameLines.empty() && !(replayStudio.HidesHud()))
                ein.modLines = &modHost.modAdv.frameLines;
        }
        modHost.PrepareModRender(ein);
        ein.capePhysics = options.capePhysics == "shoulder" ? 2 : options.capePhysics == "off" ? 0 : 1;
        ein.ownAttackTime = handRenderer.SwingProgress(f.partial);
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
            replayStudio.FramebufferSizeEffective(fw, fh);
            ein.pixelScale =
                fh > 0 ? 2.0f * std::tan(glm::radians(camera.fov) * 0.5f) / static_cast<float>(fh) : 0.0f;
            ein.lineWidthPx = std::max(2.5f, static_cast<float>(fw) / 1920.0f * 2.5f);
            ein.chunkBorders = debugOverlay.state.chunkBorders;
            ein.hitboxes = debugOverlay.state.hitboxes;
            ein.playerPos = glm::dvec3(camera.x, camera.y - 1.62, camera.z);
        }
        if (preview && previewLevel && PERLIN_ENV("PERLIN_PREVIEW_AIM")) {
            interaction.aimHit = Raycast(
                *previewLevel, &levelRenderer->Blocks(), {},
                [](const entity::Entity&) { return glm::vec2(0.6f, 1.8f); }, {camera.x, camera.y, camera.z},
                glm::dvec3(camera.Direction()), 5.7, 0.0);
        }
        if (interaction.aimHit.kind == AimHit::Kind::Block &&
            (options.showHud || (preview && PERLIN_ENV("PERLIN_PREVIEW_AIM")))) {
            ein.outline = true;
            if (const auto color = modHost.ModOutline()) {
                ein.outlineColor = *color;
                ein.outline = (*color >> 24) != 0;
            }
            ein.outlineLo = interaction.aimHit.boxLo;
            ein.outlineHi = interaction.aimHit.boxHi;
            ein.outlineShape = interaction.aimHit.shape;
        }
        return ein;
    }

    void EntityPass::AttachBlockCracks(FrameContext& f, entity::EntityRenderer::FrameIn& ein) {
        auto& live = f.live;
        const auto crackAt = [&](int x, int y, int z, int stage) {
            entity::EntityRenderer::FrameIn::Crack crack{x, y, z, stage, {}};
            if (live) {
                bool loaded = false;
                const std::string_view name = blockPalette.Name(live->NetLevel().GetBlock(x, y, z, loaded));
                if (loaded && name.starts_with("minecraft:") && name.ends_with("chest")) {
                    std::array<float, 6> box{1.0f / 16, 0.0f, 1.0f / 16, 15.0f / 16, 14.0f / 16, 15.0f / 16};
                    BlockEntity be;
                    if (live->BlockEntities().Get(x, y, z, be) && be.paired && be.pairX - x >= -1 &&
                        be.pairX - x <= 1 && be.pairZ - z >= -1 && be.pairZ - z <= 1) {
                        if (be.pairX == x + 1)
                            box[3] = 1.0f;
                        else if (be.pairX == x - 1)
                            box[0] = 0.0f;
                        if (be.pairZ == z + 1)
                            box[5] = 1.0f;
                        else if (be.pairZ == z - 1)
                            box[2] = 0.0f;
                    }
                    crack.boxes.push_back(box);
                    return crack;
                }
            }
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
                if (!(interaction.localCrack.active && c.x == interaction.localCrack.x && c.y == interaction.localCrack.y && c.z == interaction.localCrack.z))
                    ein.cracks.push_back(crackAt(c.x, c.y, c.z, c.stage));
            }
        if (interaction.localCrack.active)
            ein.cracks.push_back(crackAt(interaction.localCrack.x, interaction.localCrack.y, interaction.localCrack.z, interaction.localCrack.stage));
        if (const char* cr = PERLIN_ENV("PERLIN_PREVIEW_CRACK"); cr && interaction.aimHit.kind == AimHit::Kind::Block)
            ein.cracks.push_back(crackAt(interaction.aimHit.bx, interaction.aimHit.by, interaction.aimHit.bz, std::atoi(cr)));
    }

    void EntityPass::AttachEntityLighting(FrameContext& f, Level& entityLevel, entity::EntityRenderer::FrameIn& ein) {
        auto& frameStart = f.frameStart;
        auto& dyn = f.dyn;
        auto& params = f.params;
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
                const uint8_t packed = lightCache.At(entityLevel, bx, by, bz, frameStart);
                int sky = std::clamp<int>(ChunkLighting::Result::Sky(packed), 0, 15);
                int blk = std::clamp<int>(ChunkLighting::Result::Block(packed), 0, 15);
                if (sky == 0 && blk == 0) {
                    static const int kN[6][3] = {{0, 1, 0},  {0, -1, 0}, {1, 0, 0},
                                                 {-1, 0, 0}, {0, 0, 1},  {0, 0, -1}};
                    for (const auto& n : kN) {
                        const uint8_t q =
                            lightCache.At(entityLevel, bx + n[0], by + n[1], bz + n[2], frameStart);
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
        ein.lightLevels = [&, lightFn = ein.light](const glm::dvec3& p) -> glm::ivec2 {
            lightFn(p);
            const int bx = static_cast<int>(std::floor(p.x)), by = static_cast<int>(std::floor(p.y)),
                      bz = static_cast<int>(std::floor(p.z));
            const int64_t key = (static_cast<int64_t>(bx) * 73856093) ^
                                (static_cast<int64_t>(by) * 19349663) ^ (static_cast<int64_t>(bz) * 83492791);
            const auto& entry = entityLight[key];
            int block = entry.block;
            if (!dyn.empty())
                block =
                    std::max(block, static_cast<int>(dynlight::LevelAt(dyn, p + glm::dvec3(0.0, 0.5, 0.0))));
            return {std::clamp(block, 0, 15), std::clamp(entry.sky, 0, 15)};
        };
        if (entityLight.size() > 4096)
            entityLight.clear();
    }

    void EntityPass::RenderBlockEntityPass(FrameContext& f, const entity::EntityRenderer::FrameIn& ein) {
        auto& live = f.live;
        {
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

    void EntityPass::TickParticles(FrameContext& f, Level& entityLevel, entity::EntityRenderer::FrameIn& ein,
                               const std::vector<entity::Entity>& entities) {
        auto& dt = f.dt;
        auto& live = f.live;
        if (!particles.tileFor && levelRenderer) {
            particles.tileFor = [this](uint16_t id, int bx, int by, int bz, particle::TileChoice& out) {
                const BlockAppearance& app = levelRenderer->Blocks().Look(id);
                const std::string pname = blockPalette.Name(id);
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
                    const std::string name = blockPalette.Name(id);
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
            Level* itemLevel = &entityLevel;
            const auto itemHit = [itemLevel](double x0, double y0, double z0, double x1, double y1, double z1) {
                for (int bx = static_cast<int>(std::floor(x0)); bx <= static_cast<int>(std::floor(x1)); bx++)
                    for (int by = static_cast<int>(std::floor(y0)); by <= static_cast<int>(std::floor(y1)); by++)
                        for (int bz = static_cast<int>(std::floor(z0)); bz <= static_cast<int>(std::floor(z1));
                             bz++) {
                            bool loaded = false;
                            const uint16_t id = itemLevel->GetBlock(bx, by, bz, loaded);
                            if (!loaded)
                                return true;
                            if (id == 0)
                                continue;
                            for (const auto& b : LevelCollision::ShapeOf(id).boxes)
                                if (x0 < bx + b.x1 && x1 > bx + b.x0 && y0 < by + b.y1 && y1 > by + b.y0 &&
                                    z0 < bz + b.z1 && z1 > bz + b.z0)
                                    return true;
                        }
                return false;
            };
            live->Entities().SetItemCollider(itemHit);
        }
        pw.light = ein.light;
        if (renderer && renderer->JavaPackDrawing())
            pw.lightLevels = ein.lightLevels;
        particle::DataWorld dataWorld;
        dataWorld.solid = pw.solid;
        dataWorld.light = pw.light;
        dataWorld.lightLevels = pw.lightLevels;
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
                        interaction.lastLocalBreak.x == bx && interaction.lastLocalBreak.y == by && interaction.lastLocalBreak.z == bz &&
                        std::chrono::steady_clock::now() - interaction.lastLocalBreak.at < std::chrono::milliseconds(800);
                    if (!mine)
                        particles.Destroy(bx, by, bz, blockPalette.InternRuntime(pe.data));
                } else {
                    particles.SpawnLevelEvent(pe.event, at, pe.data);
                }
            }
        }

        for (const auto& request : entityRenderer.TakeParticleRequests())
            if (!dataParticles.Spawn(request.effect, request.at, {}, request.preScript))
                particles.SpawnNamed(request.effect, request.at);
        if (!live)
            if (const char* pb = PERLIN_ENV("PERLIN_PREVIEW_BREAK")) {
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
            if (std::chrono::steady_clock::now() - dataParticleCheck > std::chrono::seconds(2)) {
                dataParticleCheck = std::chrono::steady_clock::now();
                dataParticles.Configure(serverPacks.Directories());
            }
            dataParticles.Update(std::min(dt, 0.1f), dataWorld);
        }
        particleQuads.clear();
        particles.Collect(static_cast<float>(particleClock / 0.05), pw, particleQuads);
        fireSprites.clear();
        for (const entity::Entity& burner : entities) {
            const bool selfBurning = burner.isSelf && hudState.burning;
            if (!selfBurning && !(burner.flags & entity::flag::Onfire))
                continue;
            if (burner.flags & entity::flag::FireImmune)
                continue;

            if (burner.isSelf && ein.firstPerson)
                continue;
            entity::EntityRenderer::FrameIn::FireSprite sprite;
            sprite.pos = burner.pos;
            sprite.runtimeId = burner.runtimeId;
            sprite.width = (burner.boxWidth > 0.0f ? burner.boxWidth : 0.6f * burner.scale);
            sprite.height = (burner.boxHeight > 0.0f ? burner.boxHeight : 1.8f * burner.scale);
            bool loaded = false;
            const uint16_t below = live->NetLevel().GetBlock(static_cast<int>(std::floor(burner.pos.x)),
                                                             static_cast<int>(std::floor(burner.pos.y - 0.5)),
                                                             static_cast<int>(std::floor(burner.pos.z)), loaded);
            if (loaded) {
                const std::string blockName = blockPalette.Name(below);
                sprite.soul = blockName == "minecraft:soul_sand" || blockName == "minecraft:soul_soil";
            }
            fireSprites.push_back(sprite);
        }
        ein.fire = &fireSprites;
        ein.screenFire = hudState.burning && ein.firstPerson && hudState.mode != ui::GameMode::Spectator;
        ein.particles = &particleQuads;
        ein.dataParticles = &dataParticles;
        ein.dataWorld = dataWorld;
        ein.tilePixels = [this](int tile, std::vector<uint8_t>& rgba) {
            return levelRenderer->Blocks().TilePixels(tile, rgba);
        };
    }

    void EntityPass::RenderWeather(FrameContext& f) {
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
            if (wx.seconds >= nextRainSound) {
                nextRainSound = wx.seconds + 3.2;
                sounds.PlayNamed("ambient.weather.rain", wx.camera, std::min(1.0f, rain) * 0.4f, 1.0f, false);
            }
        }
    }

    void EntityPass::SpawnPreviewEntities() {
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
                if (uiAssets.Image(slim ? "textures/entity/alex" : "textures/entity/steve",
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
