#include "client/PerlinFrame.hpp"
#include "util/Log.hpp"

namespace perlin {
    void Perlin::UpdateHotbar(FrameContext& f) {
        auto& live = f.live;
        int newSlot = -1;
        for (int i = 0; i < 9; i++)
            if (inputManager.Pressed(static_cast<Action>(static_cast<int>(Action::Hotbar1) + i)))
                newSlot = i;
        static float scrollAccum = 0.0f;
        // in spectator the wheel sets the flying speed (1x, 2x, 5x, 10x, 20x) instead of picking a hotbar slot; the server resets to 1x on entering the mode
        static int spectatorSpeed = 0;
        static const int kSpectatorSpeeds[5] = {1, 2, 5, 10, 20};
        const bool wheelFliesFaster = live && hudState.mode == ui::GameMode::Spectator;
        if (!wheelFliesFaster)
            spectatorSpeed = 0;
        if (!inputManager.IsDown(Action::Zoom)) {
            scrollAccum += inputManager.Scroll();
            while (scrollAccum >= 1.0f || scrollAccum <= -1.0f) {
                const int step = scrollAccum > 0.0f ? 1 : -1;
                scrollAccum -= static_cast<float>(step);
                if (wheelFliesFaster) {
                    const int next = std::clamp(spectatorSpeed + step, 0, 4);
                    if (next != spectatorSpeed) {
                        spectatorSpeed = next;
                        live->SendCommand("flyspeed " + std::to_string(kSpectatorSpeeds[next]));
                    }
                    continue;
                }
                const int from = newSlot >= 0 ? newSlot : hudState.selectedSlot;
                newSlot = ((from - step) % 9 + 9) % 9;
            }
        }
        if (newSlot >= 0 && newSlot != hudState.selectedSlot) {
            hudState.selectedSlot = newSlot;
            if (live)
                live->SelectHotbarSlot(newSlot);
            else
                previewSlot = newSlot;
        }
    }

    void Perlin::UpdateTechnicalBlocks(const ui::HudItem& held, bool creative, BedrockSession* live) {
        int technical = 0;
        if (creative && !held.Empty()) {
            const std::string heldShort =
                held.name.starts_with("minecraft:") ? held.name.substr(10) : held.name;
            if (heldShort == "barrier")
                technical = BlockLibrary::kBarrier;
            else if (heldShort.starts_with("light_block"))
                technical = BlockLibrary::kLightBlock;
            else if (heldShort == "structure_void")
                technical = BlockLibrary::kStructureVoid;
        }
        if (technical != levelRenderer->Blocks().TechnicalVisible()) {
            levelRenderer->Blocks().SetTechnicalVisible(technical);
            MesherInvalidateCaches();
            if (live)
                live->NetLevel().MarkAllDirty();
        }
    }

    void Perlin::UpdateAim(FrameContext& f, bool spectator, bool creative) {
        auto& live = f.live;
        auto& previewLevel = loop->previewLevel;
        auto& frameEntities = loop->frameEntities;
        aimHit = AimHit{};
        if ((live || previewLevel) && !spectator && levelRenderer) {
            const std::vector<entity::Entity>& aimEntities = frameEntities;
            aimHit = Raycast(
                live ? live->NetLevel() : *previewLevel, &levelRenderer->Blocks(), aimEntities,
                [&](const entity::Entity& e) {
                    const auto hb = entityRenderer.HitBox(e);
                    return glm::vec2(hb.first, hb.second);
                },
                {camera.x, camera.y, camera.z}, glm::dvec3(camera.Direction()), PickRange(creative, 1),
                EntityPickRange(creative));
        }
        if (PERLIN_ENV("PERLIN_AIM_DEBUG"))
            Log("aim", "kind {} block {},{},{} face {} dist {}", static_cast<int>(aimHit.kind), aimHit.bx, aimHit.by, aimHit.bz, aimHit.face, aimHit.distance);
    }

    void Perlin::StopBreaking(BedrockSession* live) {
        if (mining.breaking && live) {
            live->SendPlayerAuthInputBlockAction(1, mining.x, mining.y, mining.z, mining.face);
            live->ClearCrack(mining.x, mining.y, mining.z);
        }
        mining.breaking = false;
        mining.progress = 0.0f;
        localCrack.active = false;
    }

    void Perlin::SwingHand() {
        handRenderer.Swing(options.hand);
        selfSwing++;
    }

    void Perlin::UpdateBlockBreaking(FrameContext& f, const ui::HudItem& held, bool creative) {
        auto& live = f.live;
        const float eyeF[3] = {camera.x, camera.y, camera.z};
        bool& breaking = mining.breaking;
        int& bkx = mining.x;
        int& bky = mining.y;
        int& bkz = mining.z;
        int& bkFace = mining.face;
        float& breakProgress = mining.progress;
        int& breakDelayTicks = mining.delayTicks;
        uint64_t& creativeBreakReadyAt = mining.creativeReadyAt;
        int& hitTicks = mining.hitTicks;
        double& breakClock = mining.breakClock;
        double& swingClock = mining.swingClock;
        const bool attackHeld = inputManager.IsDown(Action::Attack);
        if (inputManager.Pressed(Action::Attack)) {
            handRenderer.Attack(options.hand, hudState.Survival());
            selfSwing++;
            swingClock = 0;
            if (live) {
                live->SendSwingArm(aimHit.kind == AimHit::Kind::Block ? "mine" : "attack");
                if (aimHit.kind == AimHit::Kind::None)
                    live->QueueInputFlag(InputFlag::MissedSwing);
                entity::Entity attacked;
                const bool mobBlocked = live->Adventure().noPvm &&
                                        live->Entities().Find(aimHit.entity, attacked) && !attacked.isPlayer;
                if (aimHit.kind == AimHit::Kind::Entity && !mobBlocked &&
                    !DispatchModInteract(abi::InteractKind::Attack, 0, 0, 0, 0, aimHit.entity)) {
                    // click position: where the ray meets the target's hitbox, relative to its base coordinate, not world
                    const glm::dvec3 feet((aimHit.boxLo.x + aimHit.boxHi.x) * 0.5, aimHit.boxLo.y,
                                          (aimHit.boxLo.z + aimHit.boxHi.z) * 0.5);
                    const glm::vec3 offset = EntityClickOffset(
                        {camera.x, camera.y, camera.z}, glm::dvec3(camera.Direction()), feet,
                        (aimHit.boxHi.x - aimHit.boxLo.x) * 0.5, aimHit.boxHi.y - aimHit.boxLo.y,
                        (aimHit.boxLo + aimHit.boxHi) * 0.5);
                    float click[3] = {offset.x, offset.y, offset.z};
                    live->SendAttackEntity(aimHit.entity, eyeF, click);
                    // vanilla predicts the crit burst locally: falling, airborne, out of fluids, not flying
                    const physics::State& cst = player.State();
                    if (!cst.onGround && cst.vy < 0.0 && !cst.inWater && !cst.inLava && !player.Flying()) {
                        const glm::dvec3 centre((aimHit.boxLo.x + aimHit.boxHi.x) * 0.5,
                                                aimHit.boxLo.y + (aimHit.boxHi.y - aimHit.boxLo.y) * 0.6,
                                                (aimHit.boxLo.z + aimHit.boxHi.z) * 0.5);
                        for (int ci = 0; ci < 12; ++ci)
                            particles.Spawn(particle::Kind::Crit, centre);
                    }
                }
            }
        }
        const bool creativeSword = creative && held.name.ends_with("_sword");
        if (live && attackHeld && !creativeSword && !live->Adventure().immutableWorld &&
            (!creative || detail::MsNow() >= creativeBreakReadyAt) && aimHit.kind == AimHit::Kind::Block) {
            const bool newTarget = !breaking || aimHit.bx != bkx || aimHit.by != bky || aimHit.bz != bkz;
            if (newTarget && breakDelayTicks > 0 && !breaking) {
            } else if (newTarget && DispatchModInteract(abi::InteractKind::StartBreak, aimHit.bx, aimHit.by,
                                                        aimHit.bz, aimHit.face, 0)) {
            } else if (newTarget) {
                StopBreaking(live);
                static const int faceDx[6] = {0, 0, 0, 0, -1, 1};
                static const int faceDy[6] = {-1, 1, 0, 0, 0, 0};
                static const int faceDz[6] = {0, 0, -1, 1, 0, 0};
                const int fx = aimHit.bx + faceDx[aimHit.face], fy = aimHit.by + faceDy[aimHit.face],
                          fz = aimHit.bz + faceDz[aimHit.face];
                bool fireLoaded = false;
                const uint16_t fireId = live->NetLevel().GetBlock(fx, fy, fz, fireLoaded);
                const std::string fireName =
                    fireLoaded ? BlockPalette::Instance().Name(fireId) : std::string();
                if (fireName == "minecraft:fire" || fireName == "minecraft:soul_fire") {
                    live->SendPlayerAuthInputBlockAction(0, aimHit.bx, aimHit.by, aimHit.bz, aimHit.face);
                    live->SendPlayerAuthInputBlockAction(1, aimHit.bx, aimHit.by, aimHit.bz, aimHit.face);
                    live->NetLevel().SetBlock(fx, fy, fz, 0);
                    sounds.PlayBlockEvent(fireName, "break", glm::dvec3(fx + 0.5, fy + 0.5, fz + 0.5));
                    VibrateBlockBroken();
                    breakDelayTicks = 5;
                    if (creative)
                        creativeBreakReadyAt = detail::MsNow() + 250;
                } else {
                    bkx = aimHit.bx;
                    bky = aimHit.by;
                    bkz = aimHit.bz;
                    bkFace = aimHit.face;
                    breaking = true;
                    breakProgress = 0.0f;
                    breakClock = creative ? 0.05 : 0.0;
                    hitTicks = 0;
                    live->SendPlayerAuthInputBlockAction(0, bkx, bky, bkz, bkFace);
                }
            }
            if (breaking) {
                bool present = false;
                const uint16_t cur = live->NetLevel().GetBlock(bkx, bky, bkz, present);
                if (present && cur == 0)
                    StopBreaking(live);
            }
            while (breaking && breakClock >= 0.05) {
                breakClock -= 0.05;
                if (breakDelayTicks > 0) {
                    breakDelayTicks--;
                    continue;
                }
                SwingHand();
                bool loaded = false;
                const uint16_t id = live->NetLevel().GetBlock(bkx, bky, bkz, loaded);
                const std::string blockName = loaded ? BlockPalette::Instance().Name(id) : std::string();
                float step = 0.0f;
                if (creative) {
                    step = 1.0f;
                } else if (!blockName.empty() && blockName != "minecraft:air") {
                    destroy::Conditions c;
                    c.heldItem = held.Empty() ? std::string() : held.name;
                    c.onGround = player.State().onGround;
                    c.inWater = hudState.inWater;
                    step = destroy::ProgressPerTick(destroy::Lookup(blockName), c);
                }
                if (!live->ServerAuthoritativeBlockBreaking() && !creative)
                    live->SendPlayerAuthInputBlockAction(18, bkx, bky, bkz, bkFace);
                else
                    live->SendPlayerAuthInputBlockAction(27, bkx, bky, bkz, bkFace);
                breakProgress += step;
                if (!creative) {
                    bool loadedHere = false;
                    const uint16_t hitId = live->NetLevel().GetBlock(bkx, bky, bkz, loadedHere);
                    if (loadedHere && hitId != 0)
                        particles.Crack(bkx, bky, bkz, bkFace, hitId);
                }
                if (!creative && ++hitTicks % 4 == 0 && !blockName.empty())
                    sounds.PlayBlockEvent(blockName, "hit", glm::dvec3(bkx + 0.5, bky + 0.5, bkz + 0.5));
                if (breakProgress >= 1.0f) {
                    live->SendPlayerAuthInputBlockAction(26, bkx, bky, bkz, bkFace);
                    if (!live->ServerAuthoritativeBlockBreaking() && !creative)
                        live->SendPlayerAuthInputBlockAction(2, bkx, bky, bkz, bkFace);
                    if (!blockName.empty())
                        sounds.PlayBlockEvent(blockName, "break",
                                              glm::dvec3(bkx + 0.5, bky + 0.5, bkz + 0.5));
                    VibrateBlockBroken();
                    {
                        bool loadedHere = false;
                        const uint16_t oldId = live->NetLevel().GetBlock(bkx, bky, bkz, loadedHere);
                        if (loadedHere && oldId != 0)
                            particles.Destroy(bkx, bky, bkz, oldId);
                        lastLocalBreak = {bkx, bky, bkz, std::chrono::steady_clock::now()};
                    }
                    live->NetLevel().SetBlock(bkx, bky, bkz, 0);
                    std::vector<std::pair<int, uint16_t>> clearedAbove;
                    for (int up = 1; up <= 4; up++) {
                        bool aboveLoaded = false;
                        const uint16_t aboveId = live->NetLevel().GetBlock(bkx, bky + up, bkz, aboveLoaded);
                        const std::string aboveName =
                            aboveLoaded ? BlockPalette::Instance().Name(aboveId) : std::string();
                        if (aboveName.empty() || !destroy::NeedsSupportBelow(aboveName))
                            break;
                        live->NetLevel().SetBlock(bkx, bky + up, bkz, 0);
                        clearedAbove.emplace_back(bky + up, aboveId);
                        if (up == 1 && !destroy::BreaksWholeStack(aboveName))
                            break;
                    }
                    if (!clearedAbove.empty())
                        live->NoteBrokenSupport(bkx, bky, bkz, std::move(clearedAbove));
                    breaking = false;
                    breakProgress = 0.0f;
                    localCrack.active = false;
                    live->ClearCrack(bkx, bky, bkz);
                    SwingHand();
                    breakDelayTicks = 5;
                    if (creative)
                        creativeBreakReadyAt = detail::MsNow() + 250;
                } else {
                    localCrack = {breakProgress > 0.0f,
                                  bkx,
                                  bky,
                                  bkz,
                                  std::clamp(static_cast<int>(breakProgress * 10.0f), 0, 9),
                                  std::clamp(breakProgress, 0.0f, 1.0f)};
                }
            }
            if (swingClock > 0.3) {
                swingClock = 0;
                live->SendSwingArm("mine");
            }
        } else if (breaking) {
            StopBreaking(live);
        } else {
            localCrack.active = false;
            if (!attackHeld)
                breakDelayTicks = 0;
        }
        if (breakDelayTicks > 0 && breakClock >= 0.05 && !breaking) {
            breakClock -= 0.05;
            breakDelayTicks--;
        }
    }

    void Perlin::UpdateBlockUse(FrameContext& f, const ui::HudItem& held, bool creative) {
        auto& live = f.live;
        auto& earlyItemUse = f.earlyItemUse;
        auto& blockedFoodUse = f.blockedFoodUse;
        auto& previewLevel = loop->previewLevel;
        const float eyeF[3] = {camera.x, camera.y, camera.z};
        const auto clickOf = [](const AimHit& aim, float out[3]) {
            out[0] = static_cast<float>(aim.point.x - aim.bx);
            out[1] = static_cast<float>(aim.point.y - aim.by);
            out[2] = static_cast<float>(aim.point.z - aim.bz);
        };
        double& useClock = mining.useClock;
        // attack/pick ray unchanged; with bedrock bridging, air beside your footing places against the block under you
        // (reach-around), held use keeps building from the last placed block (heldbuild::HeldBuild); a real crosshair hit wins
        AimHit useAim = aimHit;
        const bool bedrockBuild = options.bedrockBridging && !held.Empty() && held.blockRuntimeId != 0;
        const glm::vec3 buildEye(camera.x, camera.y, camera.z);
        const glm::vec3 buildVelocity =
            glm::vec3(static_cast<float>(player.State().vx), static_cast<float>(player.State().vy),
                      static_cast<float>(player.State().vz)) *
            20.0f;
        static const int kFaceNormal[6][3] = {{0, -1, 0}, {0, 1, 0},  {0, 0, -1},
                                              {0, 0, 1},  {-1, 0, 0}, {1, 0, 0}};
        const auto aimAt = [&](glm::ivec3 support, int face, glm::dvec3 point) {
            useAim.kind = AimHit::Kind::Block;
            useAim.bx = support.x;
            useAim.by = support.y;
            useAim.bz = support.z;
            useAim.face = face;
            useAim.point = point;
        };
        if (bedrockBuild && live && levelRenderer && aimHit.kind == AimHit::Kind::None) {
            Level& level = live->NetLevel();
            const LevelCollision collision(level, &levelRenderer->Blocks());
            auto& palette = BlockPalette::Instance();
            const auto& shape =
                LevelCollision::ShapeOf(palette.InternRuntime(static_cast<int32_t>(held.blockRuntimeId)));
            const std::vector<physics::Box> heldShape(shape.boxes.begin(), shape.boxes.end());
            bridging::Request request;
            request.eye = {camera.x, camera.y, camera.z};
            request.direction = glm::dvec3(camera.Direction());
            request.body = physics::BoundingBox(player.State(), player.Sneaking());
            request.sneaking = player.Sneaking();
            request.reach = PickRange(creative, 1);
            request.placedShape = heldShape;
            request.horizontalSlab = held.name.contains("slab") && heldShape.size() == 1 &&
                                     std::abs(heldShape[0].y1 - heldShape[0].y0 - 0.5) < 1e-5;
            const auto around = bridging::FindTarget(request, [&](glm::ivec3 position) {
                bridging::Cell cell;
                bool loaded = false;
                const uint16_t id = level.GetBlock(position.x, position.y, position.z, loaded);
                const std::string& name = palette.Name(id);
                cell.loaded = loaded;
                cell.replaceable = blockuse::IsReplaceableCell(loaded, id, name);
                cell.interactive = blockuse::IsInteractive(name);
                if (loaded && !cell.replaceable) {
                    collision.Selection(position.x, position.y, position.z, cell.collision);
                }
                return cell;
            });
            if (around)
                aimAt(around->anchor, around->face, around->point);
        }
        if (bedrockBuild && inputManager.Pressed(Action::Use))
            heldBuild.Start(detail::MsNow(), player.TickCount());
        // held repeats follow the game's build rules (heldbuild::HeldBuild): direct crosshair hits only, the
        // repeat delay and the build line; reach-around is for fresh presses
        bool heldRepeat = false;
        if (bedrockBuild && inputManager.IsDown(Action::Use) && !inputManager.Pressed(Action::Use)) {
            heldbuild::Hit hit;
            hit.block = aimHit.kind == AimHit::Kind::Block;
            hit.target = {{aimHit.bx, aimHit.by, aimHit.bz}, aimHit.face};
            hit.point = hit.block ? glm::vec3(aimHit.point)
                                  : buildEye + glm::normalize(camera.Direction()) *
                                                   static_cast<float>(PickRange(creative, 1));
            const glm::vec3 posDelta = buildVelocity / 20.0f;
            const auto chosen = heldBuild.Next(
                hit, buildEye, posDelta, player.Sneaking(), detail::MsNow(), player.TickCount(),
                [&](glm::ivec3 p) {
                    if (!live)
                        return false;
                    bool loaded = false;
                    const uint16_t id = live->NetLevel().GetBlock(p.x, p.y, p.z, loaded);
                    return blockuse::IsReplaceableCell(loaded, id, BlockPalette::Instance().Name(id));
                });
            if (!chosen) {
                useAim.kind = AimHit::Kind::None;
            } else {
                heldRepeat = true;
                if (!hit.block || chosen->support != hit.target.support || chosen->face != hit.target.face)
                    aimAt(chosen->support, chosen->face,
                          glm::dvec3(chosen->support) + glm::dvec3(0.5) +
                              glm::dvec3(kFaceNormal[chosen->face][0], kFaceNormal[chosen->face][1],
                                         kFaceNormal[chosen->face][2]) *
                                  0.5);
            }
        }
        const bool ammoBlocked = !detail::HasAmmo(hudState, live, held.name);
        const bool useHeld = inputManager.IsDown(Action::Use) && !ammoBlocked;
        const bool usePressed = inputManager.Pressed(Action::Use) && !ammoBlocked;
        const bool holdsItem = !held.Empty();
        const bool holdsBlock = holdsItem && held.blockRuntimeId != 0;
        blockuse::Use use = blockuse::Use::Nothing;
        int dest[3] = {0, 0, 0};
        if (useAim.kind == AimHit::Kind::Block && (live || previewLevel)) {
            Level& lv = live ? live->NetLevel() : *previewLevel;
            const auto nameAt = [&](int x, int y, int z) {
                bool loaded = false;
                const uint16_t id = lv.GetBlock(x, y, z, loaded);
                return loaded ? BlockPalette::Instance().Name(id) : std::string();
            };
            int clicked[3] = {useAim.bx, useAim.by, useAim.bz};
            static const int kOff[6][3] = {{0, -1, 0}, {0, 1, 0},  {0, 0, -1},
                                           {0, 0, 1},  {-1, 0, 0}, {1, 0, 0}};
            const int f = std::clamp(useAim.face, 0, 5);
            const blockuse::Result r = blockuse::Classify(
                nameAt(clicked[0], clicked[1], clicked[2]),
                nameAt(clicked[0] + kOff[f][0], clicked[1] + kOff[f][1], clicked[2] + kOff[f][2]), clicked,
                useAim.face, holdsItem, holdsBlock, player.Sneaking());
            use = r.use;
            if (use == blockuse::Use::Place && live && live->Adventure().immutableWorld)
                use = blockuse::Use::Nothing;
            std::copy(r.dest, r.dest + 3, dest);
        }
        if (!useHeld || !holdsBlock)
            heldBuild.Stop();
        const glm::ivec3 destination{dest[0], dest[1], dest[2]};
        const uint64_t useTime = detail::MsNow();
        static auto lastUseSent = Clock::time_point{};
        static uint64_t lastBuildAt = 0;
        static glm::ivec3 lastBuildCell{0, -1000, 0};
        const bool tooSoon = Clock::now() - lastUseSent < std::chrono::milliseconds(60);
        // held building repeats only when build rules allow it this tick; other held use keeps the 200 ms repeat
        const bool repeatReady =
            bedrockBuild && use == blockuse::Use::Place ? heldRepeat : useClock >= 0.2 && !tooSoon;
        // press and held repeat can fire on consecutive ticks for the same cell: drop the duplicate
        const bool sameCell = destination == lastBuildCell && useTime - lastBuildAt < 200;
        if (use == blockuse::Use::Place && holdsBlock && sameCell && useHeld && !usePressed)
            use = blockuse::Use::Nothing;
        // never place a block into the player's own body, and send nothing: servers that trust the
        // click, or see the player a tick behind mid-jump, place it and push the player out
        bool intoBody = false;
        if (use == blockuse::Use::Place && holdsBlock) {
            const uint16_t placedId =
                BlockPalette::Instance().InternRuntime(static_cast<int32_t>(held.blockRuntimeId));
            const auto& placed = LevelCollision::ShapeOf(placedId);
            std::vector<physics::Box> shape(placed.boxes.begin(), placed.boxes.end());
            // an id the palette can't resolve reads as air, with no shape: assume a full cube
            if (placedId == 0)
                shape = {physics::Box{0, 0, 0, 1, 1, 1}};
            if (heldbuild::OverlapsBody(destination, shape,
                                        physics::BoundingBox(player.State(), player.Sneaking()))) {
                use = blockuse::Use::Nothing;
                intoBody = true;
            }
        }
        // fresh clicks start item use immediately, food included
        if (usePressed || (useHeld && repeatReady)) {
            lastUseSent = Clock::now();
            useClock = 0;
            bool interactedEntity = false;
            if (usePressed && live && useAim.kind == AimHit::Kind::Entity) {
                entity::Entity target;
                if (live->Entities().Find(useAim.entity, target) && frame::IsInteractable(target.type) &&
                    !(live->InputLocks() & 128u) &&
                    !DispatchModInteract(abi::InteractKind::InteractEntity, 0, 0, 0, 0, useAim.entity)) {
                    const glm::vec3 offset = EntityClickOffset(
                        {camera.x, camera.y, camera.z}, glm::dvec3(camera.Direction()), target.pos,
                        (useAim.boxHi.x - useAim.boxLo.x) * 0.5, useAim.boxHi.y - useAim.boxLo.y,
                        (useAim.boxLo + useAim.boxHi) * 0.5);
                    float click[3] = {offset.x, offset.y, offset.z};
                    SwingHand();
                    live->SendSwingArm("interact");
                    live->SendInteractEntity(useAim.entity, eyeF, click);
                    interactedEntity = true;
                }
            }
            const bool allowed = usePressed || (use == blockuse::Use::Place);
            const bool consumableInAir =
                holdsItem && !holdsBlock && use == blockuse::Use::Nothing && HandRenderer::IsFood(held.name);
            if (useAim.kind == AimHit::Kind::Block && allowed && !consumableInAir && !intoBody &&
                !DispatchModInteract(abi::InteractKind::UseOnBlock, useAim.bx, useAim.by, useAim.bz,
                                     useAim.face, 0)) {
                bool sent = !live;
                if (live) {
                    float click[3];
                    clickOf(useAim, click);
                    sent = live->SendUseItemOnBlock(useAim.bx, useAim.by, useAim.bz, useAim.face, click, eyeF,
                                                    static_cast<int>(use), dest);
                }
                if (use == blockuse::Use::Place && holdsBlock) {
                    lastBuildAt = useTime;
                    lastBuildCell = destination;
                    if (sent && bedrockBuild) {
                        const auto& placedShape =
                            LevelCollision::ShapeOf(BlockPalette::Instance().InternRuntime(
                                static_cast<int32_t>(held.blockRuntimeId)));
                        heldBuild.Built({{useAim.bx, useAim.by, useAim.bz}, useAim.face}, destination,
                                        heldbuild::IsSnappable(
                                            held.name, std::vector<physics::Box>(placedShape.boxes.begin(),
                                                                                 placedShape.boxes.end())),
                                        false, useTime, !usePressed);
                    }
                }
                if (sent && use != blockuse::Use::Nothing && (usePressed || use == blockuse::Use::Place)) {
                    handRenderer.Swing(options.hand);
                    selfSwing++;
                    if (live)
                        live->SendSwingArm(use == blockuse::Use::Place ? "build" : "interact");
                }
                if (sent && use == blockuse::Use::Place && holdsBlock) {
                    if (live && bedrockBuild) {
                        // bridge placements checked against the actual body: the airborne padding below would block jump bridging
                        float click[3];
                        clickOf(useAim, click);
                        live->PredictBridgePlace(dest[0], dest[1], dest[2],
                                                 static_cast<int32_t>(held.blockRuntimeId), useAim.face,
                                                 click);
                    } else if (live && !player.Flying()) {
                        const physics::State& pst = player.State();
                        const double pad = pst.onGround ? 0.0 : 0.6;
                        const double lo = pst.y - pad + std::min(0.0, pst.vy * 2.0);
                        const double hi = pst.y + 1.85 + pad + std::max(0.0, pst.vy * 2.0);
                        const bool overlaps =
                            pst.x + 0.35 + pad * 0.3 > dest[0] && pst.x - 0.35 - pad * 0.3 < dest[0] + 1 &&
                            pst.z + 0.35 + pad * 0.3 > dest[2] && pst.z - 0.35 - pad * 0.3 < dest[2] + 1 &&
                            hi > dest[1] && lo < dest[1] + 1;
                        if (!overlaps)
                            live->PredictPlace(dest[0], dest[1], dest[2],
                                               static_cast<int32_t>(held.blockRuntimeId));
                    } else if (live) {
                        live->PredictPlace(dest[0], dest[1], dest[2],
                                           static_cast<int32_t>(held.blockRuntimeId));
                    }
                    sounds.PlayBlockEvent(held.name, "place",
                                          glm::dvec3(dest[0] + 0.5, dest[1] + 0.5, dest[2] + 0.5));
                }
            }
            if (usePressed && !earlyItemUse && !blockedFoodUse && live && holdsItem &&
                use == blockuse::Use::Nothing && !interactedEntity && !intoBody &&
                !DispatchModInteract(abi::InteractKind::UseItem, 0, 0, 0, 0, 0)) {
                CustomItemDef custom;
                const bool heldUse =
                    ServerBlocks::Instance().Item(held.name, custom)
                        ? custom.useTicks > 0
                        : blockuse::VanillaUseTicks(held.name, HandRenderer::IsFood(held.name)) > 0;
                live->SendUseItemInAir(eyeF, heldUse);
                if (held.name == "minecraft:firework_rocket")
                    player.UseFirework();
                usingItem = heldUse;
                if (heldUse)
                    player.BeginItemUse();
            }
        }
        if (!useHeld && live)
            live->EndItemUseOn();
        if (usingItem && !useHeld)
            ReleaseHeldItem(live, held, eyeF);

        if (live && inputManager.Pressed(Action::PickBlock)) {
            entity::Entity picked;
            if (aimHit.kind == AimHit::Kind::Block)
                live->SendPickBlock(aimHit.bx, aimHit.by, aimHit.bz);
            else if (aimHit.kind == AimHit::Kind::Entity && live->Entities().Find(aimHit.entity, picked))
                live->SendPickEntity(picked.runtimeId);
        }

        if (live && inputManager.Pressed(Action::Drop) && !held.Empty())
            live->SendDropSelected(inputManager.IsDown(Action::Sprint) ? 0 : 1);
    }

    void Perlin::UpdateGameInput(FrameContext& f) {
        auto& dt = f.dt;
        auto& live = f.live;
        auto& preview = loop->preview;
        if (inputManager.Pressed(Action::ToggleHud))
            options.showHud = !options.showHud;
        if (!(live || preview) || !input.Captured() || replayCtl.Playing()) {
            aimHit = AimHit{};
            heldBuild.Stop();
            if (live)
                live->EndItemUseOn();
            StopBreaking(live);
            return;
        }
        UpdateHotbar(f);
        const bool spectator = hudState.mode == ui::GameMode::Spectator;
        const bool creative = hudState.mode == ui::GameMode::Creative;
        const ui::HudItem& held = hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)];
        if (levelRenderer)
            UpdateTechnicalBlocks(held, creative, live);
        UpdateAim(f, spectator, creative);
        if (live && live->TakeBlockNamesLearned()) {
            MesherInvalidateCaches();
            live->NetLevel().MarkAllDirty();
        }
        mining.breakClock += dt;
        mining.useClock += dt;
        mining.swingClock += dt;
        if (spectator) {
            heldBuild.Stop();
            if (live)
                live->EndItemUseOn();
            StopBreaking(live);
            return;
        }
        UpdateBlockBreaking(f, held, creative);
        UpdateBlockUse(f, held, creative);
    }

    void Perlin::HandleDebugKeys(FrameContext& f) {
        auto& live = f.live;
        auto& previewLevel = loop->previewLevel;
        static bool f3Was = false, combo = false;
        static bool keyWas[256] = {};
        static std::chrono::steady_clock::time_point cHeldSince{};
        static bool cHeld = false;
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
            chat.AddMessage(i18n::Tr("perlin.debug.prefix") + " " + text, hudAnim.ticks);
        };
        const auto onOff = [&](bool on) { return i18n::Tr(on ? "options.on" : "options.off"); };
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
            say(i18n::Tr("perlin.debug.reload_chunks"));
        }
        {
            const bool cDown = f3 && input.IsKeyDown('C');
            if (edge('C')) {
                const std::string tp = "/tp @s " + num(camera.x) + " " + num(feetY) + " " + num(camera.z) +
                                       " " + num(camera.yaw - 90.0f) + " " + num(-camera.pitch);
                platform->SetClipboard(tp);
                say(i18n::Tr("perlin.debug.copied_location", {tp}));
                cHeldSince = std::chrono::steady_clock::now();
                cHeld = true;
            }
            if (!cDown)
                cHeld = false;
            else if (cHeld && std::chrono::steady_clock::now() - cHeldSince > std::chrono::seconds(10))
                throw std::runtime_error(i18n::Tr("perlin.debug.manual_crash"));
        }
        if (edge('B')) {
            debug.hitboxes = !debug.hitboxes;
            say(i18n::Tr(debug.hitboxes ? "perlin.debug.hitboxes_on" : "perlin.debug.hitboxes_off"));
        }
        if (edge('D')) {
            chat.Clear();
            say(i18n::Tr("perlin.debug.chat_cleared"));
        }
        if (edge('G')) {
            debug.chunkBorders = !debug.chunkBorders;
            say(i18n::Tr(debug.chunkBorders ? "perlin.debug.chunk_borders_on"
                                            : "perlin.debug.chunk_borders_off"));
        }
        if (edge('H')) {
            debug.advancedTooltips = !debug.advancedTooltips;
            say(i18n::Tr(debug.advancedTooltips ? "perlin.debug.advanced_tooltips_on"
                                                : "perlin.debug.advanced_tooltips_off"));
        }
        if (edge('I')) {
            if (aimHit.kind == AimHit::Kind::Block && (live || previewLevel)) {
                bool loaded = false;
                Level& l = live ? live->NetLevel() : *previewLevel;
                const std::string name =
                    BlockPalette::Instance().Name(l.GetBlock(aimHit.bx, aimHit.by, aimHit.bz, loaded));
                const std::string cmd = "/setblock " + std::to_string(aimHit.bx) + " " +
                                        std::to_string(aimHit.by) + " " + std::to_string(aimHit.bz) + " " +
                                        name;
                platform->SetClipboard(cmd);
                say(i18n::Tr("perlin.debug.copied_block", {cmd}));
            } else if (aimHit.kind == AimHit::Kind::Entity && live) {
                entity::Entity target;
                if (live->Entities().Find(aimHit.entity, target)) {
                    const std::string cmd = "/summon " + target.type + " " + num(target.pos.x) + " " +
                                            num(target.pos.y) + " " + num(target.pos.z);
                    platform->SetClipboard(cmd);
                    say(i18n::Tr("perlin.debug.copied_entity", {cmd}));
                }
            } else {
                say(i18n::Tr("perlin.debug.nothing_targeted"));
            }
        }
        if (edge('N')) {
            static int previousMode = static_cast<int>(ui::GameMode::Creative);
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
            say(i18n::Tr("perlin.debug.gamemode_toggle"));
        }
        if (edge('P')) {
            input.pauseOnLostFocus = !input.pauseOnLostFocus;
            say(i18n::Tr(input.pauseOnLostFocus ? "perlin.debug.pause_on_lost_focus_on"
                                                : "perlin.debug.pause_on_lost_focus_off"));
        }
        if (edge('T')) {
            if (levelRenderer)
                levelRenderer->ReloadResources();
            entityRenderer.ClearCaches();
            ui::UiAssets::Instance().ClearItemCache();
            i18n::Lang::Get().Invalidate();
            sounds.Reload();
            if (hud)
                hud->Reload();
            say(i18n::Tr("perlin.debug.reload_resources"));
        }
        if (edge(Key::F4)) {
            if (debug.switcherIndex < 0) {
                const int cur = static_cast<int>(hudState.mode);
                debug.switcherIndex = cur == 1 ? 0 : cur == 0 ? 1 : cur == 2 ? 2 : 3;
            }
            debug.switcherIndex = (debug.switcherIndex + 1) % 4;
        }
        if (edge(Key::Escape)) {
            debug.softPaused = !debug.softPaused;
            inputManager.SetBlocked(debug.softPaused);
            say(i18n::Tr(debug.softPaused ? "perlin.debug.paused" : "perlin.debug.unpaused"));
        }
        if (f3Was && !f3) {
            if (debug.switcherIndex >= 0) {
                static const char* kModes[4] = {"creative", "survival", "adventure", "spectator"};
                if (live)
                    live->SendCommand(std::string("gamemode ") + kModes[debug.switcherIndex]);
                debug.switcherIndex = -1;
            } else if (!combo) {
                debug.on = !debug.on;
            }
            combo = false;
        }
        f3Was = f3;
        if (!f3)
            std::fill(std::begin(keyWas), std::end(keyWas), false);
        if (debug.switcherIndex >= 0) {
            static const char* kKeys[4] = {"selectWorld.gameMode.creative", "selectWorld.gameMode.survival",
                                           "selectWorld.gameMode.adventure",
                                           "selectWorld.gameMode.spectator"};
            std::string line;
            for (int i = 0; i < 4; i++) {
                const std::string name = i18n::Tr(kKeys[i]);
                line += (i == debug.switcherIndex ? "\u00a7"
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
