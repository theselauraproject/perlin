#include "client/features/interaction/PlayerInteraction.hpp"
#include "client/systems/blocks/BlockUse.hpp"
#include "client/systems/player/BlockCollision.hpp"
#include "client/systems/player/BlockDestroy.hpp"
#include "client/systems/renderer/chunk/ChunkMesher.hpp"
#include "client/systems/sprites/ItemData.hpp"
#include "client/systems/player/AmmoRules.hpp"
#include "client/systems/player/OffhandRules.hpp"
#include "client/systems/player/InteractionRules.hpp"
#include "util/Clock.hpp"
#include "util/Env.hpp"
#include "util/Log.hpp"

#include <algorithm>
#include <cmath>

namespace perlin::interaction {
    using detail::Clock;

    Player::Player(Deps d)
        : options(d.options), blockPalette(d.blockPalette), serverBlocks(d.serverBlocks), input(d.input),
          inputManager(d.inputManager), camera(d.camera), player(d.player), hudState(d.hudState),
          entityRenderer(d.entityRenderer), handRenderer(d.handRenderer), sounds(d.sounds), audioDirector(d.audioDirector), particles(d.particles),
          levelRenderer(d.levelRenderer), weather(d.weather), rainLevel(d.rainLevel),
          previousRainLevel(d.previousRainLevel), previewSlot(d.previewSlot),
          replayPlaying(std::move(d.replayPlaying)), dispatchModInteract(std::move(d.dispatchModInteract)),
          vibrateBlockBroken(std::move(d.vibrateBlockBroken)) {}

    void Player::UpdateHotbar(FrameContext& f) {
        auto& live = f.live;
        int newSlot = -1;
        for (int i = 0; i < 9; i++)
            if (inputManager.Pressed(static_cast<Action>(static_cast<int>(Action::Hotbar1) + i)))
                newSlot = i;
        static constexpr int kSpectatorSpeeds[5] = {1, 2, 5, 10, 20};
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
            // A selection change cancels ItemInUse; it is not a manual release
            // of a charged weapon and must not trigger a Riptide launch.
            StopHeldItemUse();
            hudState.selectedSlot = newSlot;
            if (live)
                live->SelectHotbarSlot(newSlot);
            else
                previewSlot = newSlot;
        }
    }

    void Player::UpdateTechnicalBlocks(const ui::HudItem& held, bool creative, BedrockSession* live) {
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

    glm::dvec3 Player::AimDirection(const FrameContext& f) const {
        glm::dvec3 forward(targeting::ViewVector(camera.yaw, camera.pitch));
        float nx = 0.5f, ny = 0.5f;
        const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count();
        if (f.width <= 0 || f.height <= 0 || !input.TouchAim(now, nx, ny))
            return forward;
        const double len = glm::length(forward);
        if (len < 1e-9)
            return forward;
        forward /= len;
        glm::dvec3 right = glm::cross(forward, glm::dvec3(0.0, 1.0, 0.0));
        if (glm::length(right) < 1e-6) {
            const double yaw = glm::radians(static_cast<double>(camera.yaw));
            right = glm::dvec3(-std::cos(yaw), 0.0, -std::sin(yaw));
        }
        right = glm::normalize(right);
        const glm::dvec3 up = glm::cross(right, forward);
        const double th = std::tan(glm::radians(static_cast<double>(camera.fov)) * 0.5);
        const double aspect = static_cast<double>(f.width) / static_cast<double>(f.height);
        const double sx = (static_cast<double>(nx) * 2.0 - 1.0) * th * aspect;
        const double sy = (1.0 - static_cast<double>(ny) * 2.0) * th;
        return glm::normalize(forward + right * sx + up * sy);
    }

    void Player::ApplyTouchActions() {
        const auto now = std::chrono::steady_clock::now();
        if (input.TakeTouchTaps() > 0) {
            const Action a = aimHit.kind == AimHit::Kind::Entity ? Action::Attack : Action::Use;
            inputManager.Inject(a);
            inputManager.Hold(a, now + std::chrono::milliseconds(60));
        }
        if (input.TouchHold()) {
            if (touchHoldAction == Action::Count) {
                touchHoldAction = aimHit.kind == AimHit::Kind::None ? Action::Use : Action::Attack;
                inputManager.Inject(touchHoldAction);
            }
            inputManager.Hold(touchHoldAction, now + std::chrono::milliseconds(120));
        } else {
            touchHoldAction = Action::Count;
        }
    }

    void Player::UpdateAim(FrameContext& f, bool spectator, bool creative) {
        auto& live = f.live;
        auto& previewLevel = *view.previewLevel;
        auto& frameEntities = *view.entities;
        aimHit = AimHit{};
        float touchX = 0.5f, touchY = 0.5f;
        const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count();
        const bool touchAim = input.TouchAim(nowMs, touchX, touchY);
        if (input.TouchInteract() && !touchAim)
            return;
        if ((live || previewLevel) && !spectator && levelRenderer) {
            const int64_t selfUid = live ? live->SelfUid() : 0;
            const auto ridesSelf = [&](const entity::Entity& e) {
                int64_t vehicle = e.vehicleUid;
                for (int depth = 0; vehicle != 0 && depth < 8; depth++) {
                    if (vehicle == selfUid || (live && vehicle == live->RidingUid()))
                        return true;
                    const auto it = std::ranges::find_if(
                        frameEntities, [&](const entity::Entity& o) { return o.uniqueId == vehicle; });
                    if (it == frameEntities.end())
                        return false;
                    vehicle = it->vehicleUid;
                }
                return false;
            };
            std::vector<entity::Entity> unridden;
            const bool anyRider = selfUid != 0 && std::ranges::any_of(frameEntities, ridesSelf);
            if (anyRider)
                for (const entity::Entity& e : frameEntities)
                    if (!ridesSelf(e))
                        unridden.push_back(e);
            const std::vector<entity::Entity>& aimEntities = anyRider ? unridden : frameEntities;
            aimHit = Raycast(
                live ? live->NetLevel() : *previewLevel, &levelRenderer->Blocks(), aimEntities,
                [&](const entity::Entity& e) {
                    const auto hb = entityRenderer.HitBox(e);
                    return glm::vec2(hb.first, hb.second);
                },
                {camera.x, camera.y, camera.z}, AimDirection(f), PickRange(creative, 1),
                EntityPickRange(creative),
                FluidAimFor(hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)].name));

            const glm::dvec3 look(targeting::ViewVector(camera.yaw, camera.pitch));
            if (aimHit.kind == AimHit::Kind::None && look.y < -0.7 && !touchAim) {
                const std::vector<entity::Entity> noEntities;
                AimHit below = Raycast(
                    live ? live->NetLevel() : *previewLevel, &levelRenderer->Blocks(), noEntities,
                    [](const entity::Entity&) { return glm::vec2(0.0f); }, {camera.x, camera.y, camera.z},
                    glm::dvec3(0.0, -1.0, 0.0), 2.0, 0.0);
                if (below.kind == AimHit::Kind::Block) {
                    below.face = std::fabs(look.x) > std::fabs(look.z) ? (look.x < 0.0 ? 4 : 5) : (look.z < 0.0 ? 2 : 3);
                    below.buildAssist = true;
                    aimHit = below;
                }
            }
        }
        if (PERLIN_ENV("PERLIN_AIM_DEBUG"))
            Log("aim", "kind {} block {},{},{} face {} dist {}", static_cast<int>(aimHit.kind), aimHit.bx,
                aimHit.by, aimHit.bz, aimHit.face, aimHit.distance);
    }

    namespace {

        bool MayStartBreaking(BedrockSession& live, const ui::HudItem& held, bool creative,
                              const std::string& blockName) {
            if (live.Spectator() || !live.MayMine())
                return false;
            const int type = live.GameType() == 5 ? live.World().worldGameMode : live.GameType();
            if (type == 2) {
                const std::string bare = blockName.starts_with("minecraft:") ? blockName.substr(10) : blockName;
                const bool listed = std::ranges::any_of(held.canDestroy, [&](std::string n) {
                    if (n.starts_with("minecraft:"))
                        n.erase(0, 10);
                    return n == bare;
                });
                if (held.Empty() || !listed)
                    return false;
            }
            return creative || destroy::Lookup(blockName).hardness >= 0.0f;
        }
    }

    void Player::StopBreaking(BedrockSession* live) {
        if (mining.breaking && live) {
            live->SendPlayerAuthInputBlockAction(1, mining.x, mining.y, mining.z, mining.face);
            live->ClearCrack(mining.x, mining.y, mining.z);
        }
        mining.breaking = false;
        mining.progress = 0.0f;
        localCrack.active = false;
    }

    void Player::SwingHand() {
        handRenderer.Swing(options.hand);
        selfSwing++;
    }

    void Player::DropSelected(BedrockSession& live, bool wholeStack) {
        if (!live.SendDropSelected(wholeStack ? 0 : 1))
            return;
        SwingHand();
        live.SendSwingArm("dropitem");
    }

    void Player::UpdateBlockBreaking(FrameContext& f, const ui::HudItem& held, bool creative) {
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
            const bool offAttack =
                offhand::AttackOffhand(held, hudState.offhand) && aimHit.kind != AimHit::Kind::Block;
            if (offAttack)
                handRenderer.SwingOffhand();
            else
                handRenderer.Attack(options.hand, hudState.Survival());
            selfSwing++;
            swingClock = 0;
            if (live)
                live->SetAttackOffhand(offAttack);
            if (live) {
                live->SendSwingArm(aimHit.kind == AimHit::Kind::Block ? "mine" : "attack");
                if (aimHit.kind == AimHit::Kind::None)
                    live->QueueInputFlag(InputFlag::MissedSwing);
                entity::Entity attacked;
                const bool mobBlocked = live->Adventure().noPvm &&
                                        live->Entities().Find(aimHit.entity, attacked) && !attacked.isPlayer;
                if (aimHit.kind == AimHit::Kind::Entity && !mobBlocked &&
                    !dispatchModInteract(abi::InteractKind::Attack, 0, 0, 0, 0, aimHit.entity)) {

                    const float click[3] = {static_cast<float>(aimHit.point.x), static_cast<float>(aimHit.point.y),
                                            static_cast<float>(aimHit.point.z)};
                    live->SendAttackEntity(aimHit.entity, eyeF, click);
                }
                live->SetAttackOffhand(false);
            }
        }
        const bool creativeSword = creative && held.name.ends_with("_sword");
        if (live && attackHeld && !creativeSword && !live->Adventure().immutableWorld &&
            (!creative || detail::MsNow() >= creativeBreakReadyAt) && aimHit.kind == AimHit::Kind::Block) {
            const bool newTarget = !breaking || aimHit.bx != bkx || aimHit.by != bky || aimHit.bz != bkz;
            if (newTarget && breakDelayTicks > 0 && !breaking) {
            } else if (newTarget && dispatchModInteract(abi::InteractKind::StartBreak, aimHit.bx, aimHit.by,
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
                    fireLoaded ? blockPalette.Name(fireId) : std::string();
                bool targetLoaded = false;
                const uint16_t targetId = live->NetLevel().GetBlock(aimHit.bx, aimHit.by, aimHit.bz, targetLoaded);
                const std::string targetName =
                    targetLoaded ? blockPalette.Name(targetId) : std::string();
                if (fireName == "minecraft:fire" || fireName == "minecraft:soul_fire") {
                    live->SendPlayerAuthInputBlockAction(0, aimHit.bx, aimHit.by, aimHit.bz, aimHit.face);
                    live->SendPlayerAuthInputBlockAction(1, aimHit.bx, aimHit.by, aimHit.bz, aimHit.face);
                    live->EndBlockActionTick();
                    live->NetLevel().SetBlock(fx, fy, fz, 0);
                    sounds.PlayBlockEvent(fireName, "break", glm::dvec3(fx + 0.5, fy + 0.5, fz + 0.5));
                    vibrateBlockBroken();
                    breakDelayTicks = 5;
                    if (creative)
                        creativeBreakReadyAt = detail::MsNow() + 250;
                } else if (!MayStartBreaking(*live, held, creative, targetName)) {

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
                const std::string blockName = loaded ? blockPalette.Name(id) : std::string();
                float step = 0.0f;
                if (creative) {
                    step = 1.0f;
                } else if (!blockName.empty() && blockName != "minecraft:air") {
                    destroy::Conditions c;
                    c.heldItem = held.Empty() ? std::string() : held.name;
                    c.onGround = player.State().onGround;
                    c.inWater = hudState.inWater;
                    for (const auto& fx : live->GetEffects()) {
                        if (fx.id == 3 || fx.id == 26)
                            c.haste = std::max(c.haste, fx.amplifier + 1);
                        else if (fx.id == 4)
                            c.miningFatigue = fx.amplifier + 1;
                    }
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
                    vibrateBlockBroken();
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
                            aboveLoaded ? blockPalette.Name(aboveId) : std::string();
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
                live->EndBlockActionTick();
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

    void Player::UpdateBlockUse(FrameContext& f, const ui::HudItem& held, bool creative) {
        auto& live = f.live;
        auto& earlyItemUse = f.earlyItemUse;
        auto& blockedFoodUse = f.blockedFoodUse;
        auto& previewLevel = *view.previewLevel;
        ReconcileHeldItemUse(held);
        const float eyeF[3] = {camera.x, camera.y, camera.z};
        const auto clickOf = [](const AimHit& aim, float out[3]) {
            out[0] = static_cast<float>(aim.point.x) - static_cast<float>(aim.bx);
            out[1] = static_cast<float>(aim.point.y) - static_cast<float>(aim.by);
            out[2] = static_cast<float>(aim.point.z) - static_cast<float>(aim.bz);
        };
        double& useClock = mining.useClock;
        AimHit useAim = aimHit;
        const bool bedrockBuild = !held.Empty() && held.blockRuntimeId != 0;
        const glm::vec3 buildEye(camera.x, camera.y, camera.z);
        const int64_t buildNow =
            std::chrono::duration_cast<std::chrono::nanoseconds>(detail::Clock::now().time_since_epoch()).count();
        const auto aimAt = [&](glm::ivec3 support, int face, glm::dvec3 point) {
            useAim.kind = AimHit::Kind::Block;
            useAim.bx = support.x;
            useAim.by = support.y;
            useAim.bz = support.z;
            useAim.face = face;
            useAim.point = point;
        };
        if (bedrockBuild && inputManager.Pressed(Action::Use))
            heldBuild.Start(player.TickCount(), useAim.kind == AimHit::Kind::Block);
        bool heldRepeat = false;
        if (bedrockBuild && inputManager.IsDown(Action::Use) && !inputManager.Pressed(Action::Use)) {
            heldbuild::Hit hit;
            hit.block = aimHit.kind == AimHit::Kind::Block;
            hit.assist = aimHit.buildAssist;
            hit.target = {{aimHit.bx, aimHit.by, aimHit.bz}, aimHit.face};
            hit.point = aimHit.kind != AimHit::Kind::None && !aimHit.buildAssist
                            ? glm::vec3(aimHit.point)
                            : buildEye + glm::normalize(camera.Direction()) * static_cast<float>(PickRange(creative, 1));
            const glm::vec3 velocity(static_cast<float>(player.State().vx), static_cast<float>(player.State().vy),
                                     static_cast<float>(player.State().vz));
            const auto chosen = heldBuild.Next(
                hit, buildEye, velocity, player.TickDisplacement(), player.Sneaking(), buildNow, player.TickCount(),
                [&](glm::ivec3 p) {
                    if (!live)
                        return false;
                    bool loaded = false;
                    const uint16_t id = live->NetLevel().GetBlock(p.x, p.y, p.z, loaded);
                    return blockuse::IsReplaceableCell(loaded, id, blockPalette.Name(id));
                });
            if (!chosen) {
                useAim.kind = AimHit::Kind::None;
            } else {
                heldRepeat = true;
                if (!hit.block || chosen->support != hit.target.support || chosen->face != hit.target.face)
                    aimAt(chosen->support, chosen->face,
                          hit.block ? glm::dvec3(aimHit.point) : glm::dvec3(chosen->support));
            }
        }
        const bool ammoBlocked = !detail::HasAmmo(hudState, live, held.name);
        // A block or entity handles interaction before the held item's use
        // method. Missing ammunition must not prevent opening a chest.
        const bool useHeld = inputManager.IsDown(Action::Use);
        const bool usePressed = inputManager.Pressed(Action::Use);
        const bool holdsItem = !held.Empty();
        const bool holdsBlock = holdsItem && held.blockRuntimeId != 0;
        blockuse::Use use = blockuse::Use::Nothing;
        int dest[3] = {0, 0, 0};
        if (useAim.kind == AimHit::Kind::Block && (live || previewLevel)) {
            Level& lv = live ? live->NetLevel() : *previewLevel;
            const auto nameAt = [&](int x, int y, int z) {
                bool loaded = false;
                const uint16_t id = lv.GetBlock(x, y, z, loaded);
                return loaded ? blockPalette.Name(id) : std::string();
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
        if (!inputManager.IsDown(Action::Use))
            heldBuild.Stop();
        const glm::ivec3 destination{dest[0], dest[1], dest[2]};
        const uint64_t useTime = detail::MsNow();
        const bool tooSoon = Clock::now() - lastUseSent < std::chrono::milliseconds(60);
        const bool repeatReady =
            bedrockBuild && use == blockuse::Use::Place ? heldRepeat : useClock >= 0.2 && !tooSoon;
        const bool sameCell = !bedrockBuild && destination == lastBuildCell && useTime - lastBuildAt < 200;
        if (use == blockuse::Use::Place && holdsBlock && sameCell && useHeld && !usePressed)
            use = blockuse::Use::Nothing;
        bool intoBody = false;
        if (use == blockuse::Use::Place && holdsBlock) {
            const uint16_t placedId =
                blockPalette.InternRuntime(static_cast<int32_t>(held.blockRuntimeId));
            const auto& placed = LevelCollision::ShapeOf(placedId);
            std::vector<physics::Box> shape(placed.boxes.begin(), placed.boxes.end());
            if (placedId == 0)
                shape = {physics::Box{0, 0, 0, 1, 1, 1}};
            if (heldbuild::OverlapsBody(destination, shape,
                                        physics::BoundingBox(player.State(), player.Sneaking()))) {
                use = blockuse::Use::Nothing;
                intoBody = true;
            }
        }
        if (!usingItem && (usePressed || (useHeld && repeatReady))) {
            lastUseSent = Clock::now();
            useClock = 0;
            bool interactedEntity = false;
            if (usePressed && live && useAim.kind == AimHit::Kind::Entity) {
                entity::Entity target;
                if (live->Entities().Find(useAim.entity, target) && frame::IsInteractable(target.type) &&
                    !(live->InputLocks() & 128u) &&
                    !dispatchModInteract(abi::InteractKind::InteractEntity, 0, 0, 0, 0, useAim.entity)) {

                    const float click[3] = {static_cast<float>(useAim.point.x), static_cast<float>(useAim.point.y),
                                            static_cast<float>(useAim.point.z)};
                    SwingHand();
                    live->SendSwingArm("interact");
                    live->SendInteractEntity(useAim.entity, eyeF, click);
                    interactedEntity = frame::InteractionTakesUse(target.type, ui::IsFood(held.name));
                }
            }
            const bool allowed = usePressed || (use == blockuse::Use::Place);
            const bool consumableInAir =
                holdsItem && !holdsBlock && use == blockuse::Use::Nothing && ui::IsFood(held.name);
            if (useAim.kind == AimHit::Kind::Block && allowed && !consumableInAir && !intoBody &&
                !dispatchModInteract(abi::InteractKind::UseOnBlock, useAim.bx, useAim.by, useAim.bz,
                                     useAim.face, 0)) {
                bool sent = !live;
                if (live) {
                    float click[3];
                    clickOf(useAim, click);
                    sent = live->SendUseItemOnBlock(useAim.bx, useAim.by, useAim.bz, useAim.face, click, eyeF,
                                                    static_cast<int>(use), dest, !usePressed);
                }
                if (use == blockuse::Use::Place && holdsBlock) {
                    lastBuildAt = useTime;
                    lastBuildCell = destination;
                    if (sent && bedrockBuild) {
                        const auto& placedShape =
                            LevelCollision::ShapeOf(blockPalette.InternRuntime(
                                static_cast<int32_t>(held.blockRuntimeId)));
                        heldBuild.Built({{useAim.bx, useAim.by, useAim.bz}, useAim.face}, destination,
                                        heldbuild::IsSnappable(
                                            held.name, std::vector<physics::Box>(placedShape.boxes.begin(),
                                                                                 placedShape.boxes.end())),
                                        false, buildNow, !usePressed);
                    }
                }
                bool overlapsPadded = false;
                if (sent && live && use == blockuse::Use::Place && holdsBlock && !bedrockBuild && !player.Flying()) {
                    const physics::State& pst = player.State();
                    const double pad = pst.onGround ? 0.0 : 0.6;
                    const double lo = pst.y - pad + std::min(0.0, pst.vy * 2.0);
                    const double hi = pst.y + 1.85 + pad + std::max(0.0, pst.vy * 2.0);
                    overlapsPadded =
                        pst.x + 0.35 + pad * 0.3 > dest[0] && pst.x - 0.35 - pad * 0.3 < dest[0] + 1 &&
                        pst.z + 0.35 + pad * 0.3 > dest[2] && pst.z - 0.35 - pad * 0.3 < dest[2] + 1 &&
                        hi > dest[1] && lo < dest[1] + 1;
                }
                if (sent && !overlapsPadded && use != blockuse::Use::Nothing &&
                    (usePressed || use == blockuse::Use::Place)) {
                    if (offhandUse)
                        handRenderer.SwingOffhand();
                    else
                        handRenderer.Swing(options.hand);
                    selfSwing++;
                    if (live)
                        live->SendSwingArm(use == blockuse::Use::Place ? "build" : "interact");
                }
                if (sent && !overlapsPadded && use == blockuse::Use::Place && holdsBlock) {
                    if (live && bedrockBuild) {
                        float click[3];
                        clickOf(useAim, click);
                        live->PredictBridgePlace(dest[0], dest[1], dest[2],
                                                 static_cast<int32_t>(held.blockRuntimeId), useAim.face,
                                                 click);
                    } else if (live) {
                        live->PredictPlace(dest[0], dest[1], dest[2],
                                           static_cast<int32_t>(held.blockRuntimeId));
                    }
                    sounds.PlayBlockEvent(held.name, "place",
                                          glm::dvec3(dest[0] + 0.5, dest[1] + 0.5, dest[2] + 0.5));
                    audioDirector.NoteLocalPlace(dest[0], dest[1], dest[2], std::chrono::steady_clock::now());
                }
            }
            const bool continueAirUse = useHeld && repeatReady && (useConsumed || repeatAirUse);
            if ((usePressed || continueAirUse) && !useRestartBlocked &&
                !earlyItemUse && !blockedFoodUse && live && holdsItem &&
                use == blockuse::Use::Nothing && !interactedEntity && !intoBody &&
                !dispatchModInteract(abi::InteractKind::UseItem, 0, 0, 0, 0, 0)) {
                const bool wet = held.name == "minecraft:trident" &&
                                 (player.State().inWater || RainingOnPlayer(*live));
                const bool heldUse =
                    !ammoBlocked && live->CanStartUsingHeldItem(wet) &&
                    ((OffhandExperiment() && held.name == "minecraft:shield") ||
                     held.name == "minecraft:crossbow" || HeldUseDuration(held, *live) > 0);
                const bool onCooldown = live->HeldItemUseOnCooldown();
                const bool sent = live->SendUseItemInAir(eyeF, heldUse);
                if (sent && !onCooldown && held.name == "minecraft:firework_rocket")
                    player.UseFirework();
                usingItem = sent && heldUse && !onCooldown;
                if (usingItem)
                    BeginHeldItemUse(held, *live);
                if (sent && (held.name == "minecraft:ender_pearl" || held.name == "minecraft:wind_charge")) {
                    const auto inventory = live->GetInventory();
                    const auto& remaining = offhandUse
                                                ? inventory.offhand
                                                : inventory.hotbar[std::clamp(inventory.selected, 0, 8)];
                    repeatAirUse = !remaining.Empty() && remaining.name == held.name;
                    useItemName = held.name;
                    useItemSlot = hudState.selectedSlot;
                    useItemOffhand = offhandUse;
                }
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

        if (live && inputManager.Pressed(Action::SwapOffhand))
            live->SwapOffhand();

        if (live && inputManager.Pressed(Action::Drop) && !held.Empty())
            DropSelected(*live, inputManager.IsDown(Action::Sprint));
    }

    void Player::UpdateGameInput(FrameContext& f, const View& frameView) {
        view = frameView;
        auto& dt = f.dt;
        auto& live = f.live;
        const bool preview = view.preview;
        if (inputManager.Pressed(Action::ToggleHud))
            options.showHud = !options.showHud;
        if (!(live || preview) || !input.Captured() || input.TextMode() || replayPlaying()) {
            if (!live)
                StopHeldItemUse();
            if (live)
                live->SetUseOffhand(false);
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
        ApplyTouchActions();
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
        offhandUse = offhand::UseOffhand(held, hudState.offhand);
        if (live)
            live->SetUseOffhand(offhandUse);
        UpdateBlockUse(f, offhandUse ? hudState.offhand : held, creative);
    }

    void Player::UpdateEatingAndDrinking(FrameContext& f, BedrockSession& live) {
        auto& earlyItemUse = f.earlyItemUse;
        auto& blockedFoodUse = f.blockedFoodUse;
        const auto& mainHeld = hudState.hotbar[std::clamp(hudState.selectedSlot, 0, 8)];
        offhandUse = offhand::UseOffhand(mainHeld, hudState.offhand);
        live.SetUseOffhand(offhandUse);
        const auto& held = offhandUse ? hudState.offhand : mainHeld;
        ReconcileHeldItemUse(held);
        const auto& name = held.name;
        const bool use = input.Captured() && inputManager.IsDown(Action::Use);
        if (!use) {
            useRestartBlocked = false;
            repeatAirUse = false;
        }
        // FinishedChargingItem can precede the chargedItem inventory update.
        // The native callback retains its reset flag until the held stack is
        // charged, then clears the retained use intention. It does not release
        // the weapon or stop an independently active item-use timer.
        if (live.HeldItemIsChargedCrossbow() && live.TakeChargedItemUseReset())
            useRestartBlocked = use;
        const bool drink = name == "minecraft:potion" || name == "minecraft:milk_bucket" ||
                           name == "minecraft:honey_bottle" || name == "minecraft:ominous_bottle";
        const bool always = drink || name == "minecraft:golden_apple" ||
                            name == "minecraft:enchanted_golden_apple" || name == "minecraft:chorus_fruit" ||
                            name == "minecraft:suspicious_stew";
        const bool eating = hudState.Survival() && ui::IsFood(name) && (always || ui::CanEatNow(name, hudState.food));
        blockedFoodUse = ui::IsFood(name) && !eating;
        bool aimInteracts = false;
        if (aimHit.kind == AimHit::Kind::Block) {
            bool loaded = false;
            const uint16_t id = live.NetLevel().GetBlock(aimHit.bx, aimHit.by, aimHit.bz, loaded);
            aimInteracts =
                !loaded || (blockuse::IsInteractive(blockPalette.Name(id)) && !player.Sneaking());
        } else if (aimHit.kind == AimHit::Kind::Entity) {
            entity::Entity target;
            aimInteracts = !live.Entities().Find(aimHit.entity, target) || frame::TakesItemUse(target.type);
        }
        if (use && !usingItem && eating && !aimInteracts && inputManager.Pressed(Action::Use) &&
            !dispatchModInteract(abi::InteractKind::UseItem, 0, 0, 0, 0, 0)) {
            const float eye[3] = {static_cast<float>(camera.x), static_cast<float>(camera.y),
                                  static_cast<float>(camera.z)};
            earlyItemUse = live.SendUseItemInAir(eye, true);
            usingItem = earlyItemUse && !live.HeldItemUseOnCooldown();
            if (usingItem)
                BeginHeldItemUse(held, live);
        }
        if (!use && usingItem) {
            const float eye[3] = {static_cast<float>(camera.x), static_cast<float>(camera.y),
                                  static_cast<float>(camera.z)};
            ReleaseHeldItem(&live, held, eye);
        }
        {
            const uint64_t now = player.TickCount();
            const bool held_ = use && usingItem;
            if (held_ && !useWasHeld) {
                Log("item", "started using {}", name);
                useStartTick = now;
                useConsumed = false;
            }
            if (held_ && !useConsumed) {
                const int need = useDuration;
                // Native positive durations count down; zero completes and a
                // negative duration is retained. Quick Charge can produce all
                // three, so do not cast a negative duration to unsigned ticks.
                const bool finite = need > 0 || (need == 0 && name == "minecraft:crossbow");
                if (finite && now >= useStartTick && now - useStartTick >= static_cast<uint64_t>(need)) {
                    Log("item", "finished using {} after {} ticks", name, now - useStartTick);
                    useConsumed = true;
                    usingItem = false;
                }
            }
            useWasHeld = held_ || (use && useConsumed);
            if (!use) {
                useWasHeld = false;
                useConsumed = false;
            }
        }
        const bool shieldSlow = OffhandExperiment() && live.previewOffhand && name == "minecraft:shield";
        CustomItemDef custom;
        const bool customUse = serverBlocks.Item(name, custom) && custom.useTicks > 0;
        // Use state owns the slowdown lifetime. A timer tied only to the first
        // physical press expires incorrectly when held food starts another use.
        player.SetItemSlow(use && usingItem &&
                               (customUse || ui::IsFood(name) || drink || name == "minecraft:bow" ||
                                name == "minecraft:crossbow" || name == "minecraft:trident" ||
                                name == "minecraft:spyglass" || shieldSlow),
                           0, false, customUse ? custom.useMovementModifier : 0.35f);
    }

    int Player::HeldUseDuration(const ui::HudItem& held, BedrockSession& live) const {
        if (held.name == "minecraft:crossbow")
            return blockuse::VanillaUseTicks(held.name, false, live.HeldItemEnchantLevel(35));
        CustomItemDef custom;
        if (serverBlocks.Item(held.name, custom) && custom.useTicks > 0)
            return custom.useTicks;
        if (held.name == "minecraft:dried_kelp")
            return 16;
        return blockuse::VanillaUseTicks(held.name, ui::IsFood(held.name));
    }

    void Player::BeginHeldItemUse(const ui::HudItem& held, BedrockSession& live) {
        usingItem = true;
        useConsumed = false;
        useWasHeld = false;
        useRestartBlocked = false;
        repeatAirUse = false;
        useDuration = HeldUseDuration(held, live);
        useItemName = held.name;
        useItemSlot = hudState.selectedSlot;
        useItemOffhand = offhandUse;
        player.BeginItemUse();
    }

    void Player::StopHeldItemUse() {
        usingItem = false;
        useConsumed = false;
        useWasHeld = false;
        useRestartBlocked = false;
        repeatAirUse = false;
        useStartTick = 0;
        useDuration = 0;
        useItemName.clear();
        player.SetItemSlow(false);
    }

    void Player::ReconcileHeldItemUse(const ui::HudItem& held) {
        // For the ordinary supported use-item classes, the matched client's
        // relevant-user-data comparison returns true and its continuity check
        // explicitly excludes aux/durability. Count changes refresh the copied
        // stack; only an empty stack cancels it.
        if ((usingItem || useConsumed || repeatAirUse) &&
            (held.Empty() || held.name != useItemName || hudState.selectedSlot != useItemSlot ||
             offhandUse != useItemOffhand))
            StopHeldItemUse();
    }

    bool Player::RainingOnPlayer(BedrockSession& live) const {
        if (live.DimensionId() != 0)
            return false;
        const float delta = rainLevel - previousRainLevel;
        if (!(delta + previousRainLevel > 0.2f) || !levelRenderer)
            return false;
        const physics::State& pst = player.State();
        const float eyeY = static_cast<float>(pst.y) + static_cast<float>(physics::kStandEye);
        const int x = static_cast<int>(std::floor(static_cast<float>(pst.x)));
        const int y = static_cast<int>(std::floor(eyeY + pst.collisionHeight));
        const int z = static_cast<int>(std::floor(static_cast<float>(pst.z)));
        auto& level = live.NetLevel();
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
        return downfall > 0.0f && temperature > 0.15f;
    }

    void Player::ReleaseHeldItem(BedrockSession* live, const ui::HudItem& held, const float eye[3]) {
        StopHeldItemUse();
        if (!live)
            return;
        live->SendReleaseItem(eye);
        if (held.name == "minecraft:trident")
            player.ReleaseTrident(live->HeldItemEnchantLevel(30), RainingOnPlayer(*live));
    }
}
