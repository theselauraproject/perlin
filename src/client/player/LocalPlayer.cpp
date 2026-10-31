#include "client/player/LocalPlayer.hpp"

#include "client/player/BlockCollision.hpp"
#include "client/player/PerlinPowderSnow.hpp"
#include "client/player/PerlinScaffolding.hpp"
#include "client/player/PerlinStatusEffect.hpp"
#include "protocol/Packets.hpp"
#include "world/level/block/BlockPalette.hpp"
#include "network/PlayerConstants.hpp"
#include "util/Env.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include "util/Log.hpp"

namespace perlin {
    namespace {
        constexpr int kMaxTicksPerFrame = 5;

        float BedrockYaw(float cameraYaw) {
            float yaw = std::fmod(cameraYaw - 90.0f, 360.0f);
            if (yaw >= 180.0f)
                yaw -= 360.0f;
            if (yaw < -180.0f)
                yaw += 360.0f;
            return yaw;
        }

        float CameraYaw(float bedrockYaw) {
            return bedrockYaw + 90.0f;
        }

        float BedrockPitch(float cameraPitch) {
            return -cameraPitch;
        }

        void ApplyMotion(physics::State& state, const std::optional<std::array<double, 3>>& motion) {
            if (!motion)
                return;
            state.vx = (*motion)[0];
            state.vy = (*motion)[1];
            state.vz = (*motion)[2];
        }

        void ApplyTeleport(physics::State& state, const std::optional<BedrockSession::ServerMotion>& teleport) {
            if (!teleport)
                return;
            state.x = teleport->pos[0];
            state.y = teleport->pos[1];
            state.z = teleport->pos[2];
            state.vx = state.vy = state.vz = 0;
            state.onGround = teleport->onGround;
            state.hasTeleported = teleport->filterTravel;
            state.hasBox = false;
        }

        namespace Flag {
            constexpr uint32_t Ascend = 0;
            constexpr uint32_t Descend = 1;
            constexpr uint32_t JumpDown = 3;
            constexpr uint32_t SprintDown = 4;
            constexpr uint32_t Jumping = 6;
            constexpr uint32_t Sneaking = 8;
            constexpr uint32_t SneakDown = 9;
            constexpr uint32_t UpLeft = 14;
            constexpr uint32_t UpRight = 15;
            constexpr uint32_t WantUp = 16;
            constexpr uint32_t WantDown = 17;
            constexpr uint32_t Sprinting = 20;
            constexpr uint32_t StartSprinting = 25;
            constexpr uint32_t StopSprinting = 26;
            constexpr uint32_t StartSwimming = 29;
            constexpr uint32_t StopSwimming = 30;
            constexpr uint32_t StartSneaking = 27;
            constexpr uint32_t StopSneaking = 28;
            constexpr uint32_t StartJumping = 31;
            constexpr uint32_t StartGliding = 32;
            constexpr uint32_t StopGliding = 33;
            constexpr uint32_t StartCrawling = 40;
            constexpr uint32_t StopCrawling = 41;
            constexpr uint32_t HorizontalCollision = 49;
            constexpr uint32_t VerticalCollision = 50;
            constexpr uint32_t DownLeft = 51;
            constexpr uint32_t DownRight = 52;
            constexpr uint32_t StartSpinAttack = 56;
            constexpr uint32_t StopSpinAttack = 57;
            constexpr uint32_t JumpReleasedRaw = 59;
            constexpr uint32_t JumpPressedRaw = 60;
            constexpr uint32_t JumpCurrentRaw = 61;
            constexpr uint32_t SneakReleasedRaw = 62;
            constexpr uint32_t SneakPressedRaw = 63;
            constexpr uint32_t SneakCurrentRaw = 64;
        }
    }

    void LocalPlayer::ApplyEffectUpdates(BedrockSession& session) {
        const auto updates = session.TakeEffectUpdates();
        size_t first = history.size();
        for (const auto& update : updates) {
            if (update.tick == 0 || history.empty())
                continue;
            const uint64_t boundary = std::max(update.tick, history.front().tick);
            for (size_t i = 0; i < history.size(); ++i) {
                Record& record = history[i];
                if (record.tick <= boundary)
                    continue;
                const bool active = update.active && physics::bds::StatusEffectTicksAt(
                    update.duration, update.tick, record.tick - 1) != 0;
                auto& a = record.attributes;
                bool changed = false;
                switch (update.id) {
                case 8: {
                    const int value = active ? update.amplifier + 1 : 0;
                    changed = a.jumpBoost != value;
                    a.jumpBoost = value;
                    break;
                }
                case 24: {
                    const int value = active ? update.amplifier + 1 : 0;
                    changed = a.levitation != value;
                    a.levitation = value;
                    break;
                }
                case 27:
                    changed = a.slowFalling != active;
                    a.slowFalling = active;
                    break;
                case 33:
                    changed = a.weaving != active;
                    a.weaving = active;
                    break;
                default: break;
                }
                if (changed)
                    first = std::min(first, i);
            }
        }
        ReplayFrom(session, first);
    }

    // MovementEffectPacket 0x146BD3FB0: an event on frame T + 1 (0x142CD9D60), replayed from there; the replay sets
    // the entry outright (0x143A18B90), durations below -1 read as 0. a stamp past our frames lands on the next tick
    void LocalPlayer::ApplyMovementEffectUpdates(BedrockSession& session) {
        size_t first = history.size();
        for (const auto& effect : session.TakeMovementEffects()) {
            const physics::bds::MovementEffect entry{effect.tick, effect.duration < -1 ? 0 : effect.duration,
                                                    static_cast<int32_t>(effect.type), true};
            bool inserted = false;
            if (!history.empty()) {
                const uint64_t boundary = std::max(effect.tick, history.front().tick);
                for (size_t i = 0; i < history.size(); ++i) {
                    if (history[i].tick <= boundary)
                        continue;
                    history[i].input.movementEffects.push_back(entry);
                    first = std::min(first, i);
                    inserted = true;
                    break;
                }
            }
            if (!inserted)
                futureMovementEffects.push_back(entry);
        }
        ReplayFrom(session, first);
    }

    // actor data and attributes are stamped with the ReplayStateComponent tick (+24) of the last input the server
    // took (0x140BA9110, 0x14403EA50): the value after that tick's freeze step, the one frame X + 1 moves with.
    // Vanilla files it on frame X + 1 (0x142CD98E0) and a later packet with the same stamp overwrites it there; a
    // stamp the server skipped (two inputs in one server tick) leaves the frame on the previous value
    void LocalPlayer::SyncFreeze(BedrockSession& session) {
        for (const auto& update : session.TakeFreezeUpdates()) {
            if (PERLIN_ENV("PERLIN_CORRECTION_LOG"))
                std::cout << "[freeze] value stamp=" << update.tick << " server=" << update.strength
                          << " newest=" << (history.empty() ? 0 : history.back().tick) << std::endl;
            if (update.tick == 0 || history.empty() || update.tick < history.front().tick ||
                update.tick > history.back().tick)
                continue;
            bool matched = update.tick == history.back().tick && freezeStrength == update.strength;
            for (const Record& r : history)
                if (r.tick == update.tick + 1)
                    matched = r.freeze == update.strength;
            if (matched)
                continue;
            float strength = update.strength;
            for (const Record& r : history)
                if (r.tick > update.tick)
                    strength = physics::bds::FreezeStep(strength, r.freezeRising);
            if (PERLIN_ENV("PERLIN_CORRECTION_LOG"))
                std::cout << "[freeze] resync stamp=" << update.tick << " server=" << update.strength
                          << " ours=" << freezeStrength << " now=" << strength << std::endl;
            freezeStrength = strength;
        }
    }

    void LocalPlayer::ApplyMotionUpdate(BedrockSession& session, const BedrockSession::MotionUpdate& update) {
        if (teleportMotionBoundary && update.tick <= *teleportMotionBoundary) {
            if (PERLIN_ENV("PERLIN_BLOCK_DEBUG"))
                Log("move", "motion_before_teleport stamp={} boundary={}", update.tick, *teleportMotionBoundary);
            return;
        }
        size_t first = history.size();
        bool inserted = false;
        if (!history.empty()) {
            const uint64_t boundary = std::max(update.tick, history.front().tick);
            for (size_t i = 0; i < history.size(); ++i) {
                if (history[i].tick <= boundary)
                    continue;
                history[i].motion = update.velocity;
                first = i;
                inserted = true;
                if (PERLIN_ENV("PERLIN_BLOCK_DEBUG"))
                    Log("move", "motion_replay stamp={} target={} records={}", update.tick, history[i].tick, history.size() - i);
                break;
            }
        }
        if (!inserted)
            futureMotion.push_back(update);
        ReplayFrom(session, first);
    }

    void LocalPlayer::ReplayFrom(BedrockSession& session, size_t first) {
        if (first >= history.size())
            return;
        physics::State replay = history[first].before;
        LevelCollision world(session.NetLevel(), blocksPtr);
        for (size_t i = first; i < history.size(); ++i) {
            Record& record = history[i];
            record.before = replay;
            replay.yaw = record.yaw;
            ApplyTeleport(replay, record.teleport);
            ApplyMotion(replay, record.motion);
            if (record.simulated)
                physics::Step(replay, record.input, record.attributes, world);
            else
                replay.vx = replay.vy = replay.vz = 0;
            record.after = replay;
        }
        state = replay;
        previous = history.back().before;
        session.SetPose(static_cast<float>(state.x), static_cast<float>(state.y),
                        static_cast<float>(state.z), state.yaw, session.GetPose().pitch);
    }

    void LocalPlayer::ApplyServerMotion(BedrockSession& session, Camera& camera,
                                       BedrockSession::ServerMotion motion) {
        if (motion.stampedVelocity) {
            ApplyMotionUpdate(session, {motion.tick, {motion.velocity[0], motion.velocity[1], motion.velocity[2]}});
            return;
        }
        bool handled = false;
        if (motion.teleport && motion.filterTravel && motion.tick != 0 && !history.empty() &&
            motion.tick >= history.front().tick) {
            const float dx = static_cast<float>(motion.pos[0]) - static_cast<float>(state.x);
            const float dy = static_cast<float>(motion.pos[1]) - static_cast<float>(state.y);
            const float dz = static_cast<float>(motion.pos[2]) - static_cast<float>(state.z);
            if (std::sqrt(dx * dx + dy * dy + dz * dz) < 16.0f) {
                teleportMotionBoundary = motion.tick;
                std::erase_if(futureMotion, [&](const auto& v) { return v.tick <= motion.tick; });
                size_t first = history.size();
                for (size_t i = 0; i < history.size(); ++i) {
                    if (history[i].tick <= motion.tick)
                        continue;
                    history[i].teleport = motion;
                    if (history[i].tick == motion.tick + 1)
                        history[i].motion.reset();
                    first = i;
                    break;
                }
                if (first == history.size())
                    futureTeleports.push_back(motion);
                else
                    ReplayFrom(session, first);
                if (PERLIN_ENV("PERLIN_BLOCK_DEBUG"))
                    Log("move", "teleport_replay stamp={} records={} queued={}", motion.tick, history.size() - first, (first == history.size()));
                handled = true;
                motion.hasVelocity = false;
            }
        }
        if (motion.correction) {
            if (teleportMotionBoundary && motion.tick > *teleportMotionBoundary)
                state.hasTeleported = false;
            while (!history.empty() && history.front().tick < motion.tick)
                history.pop_front();
            if (PERLIN_ENV("PERLIN_CORRECTION_LOG") && (history.empty() || history.front().tick != motion.tick))
                std::cout << "[correction] unmatched tick=" << motion.tick << " historyFront="
                          << (history.empty() ? -1 : static_cast<long long>(history.front().tick))
                          << " historySize=" << history.size() << " server=(" << motion.pos[0] << "," << motion.pos[1]
                          << "," << motion.pos[2] << ") ours=(" << state.x << "," << state.y << "," << state.z << ")"
                          << " hasTeleported=" << state.hasTeleported << std::endl;
            if (!history.empty() && history.front().tick == motion.tick) {
                Record& r = history.front();
                const double dx = r.after.x - motion.pos[0], dy = r.after.y - motion.pos[1],
                             dz = r.after.z - motion.pos[2];
                const double ex = r.after.vx - motion.velocity[0];
                const double ey = r.after.vy - motion.velocity[1];
                const double ez = r.after.vz - motion.velocity[2];
                const double posErr = dx * dx + dy * dy + dz * dz;
                const double velErr = ex * ex + ey * ey + ez * ez;
                // correction event check 0x1410E8FF0: float squared distances, rewind only past 1e-5 (0x3727C5AC)
                const auto sq = [](double server, double ours) {
                    const float d = static_cast<float>(server) - static_cast<float>(ours);
                    return d * d;
                };
                const float posErrF = (sq(motion.pos[1], r.after.y) + sq(motion.pos[0], r.after.x)) + sq(motion.pos[2], r.after.z);
                const float velErrF = (sq(motion.velocity[1], r.after.vy) + sq(motion.velocity[0], r.after.vx)) +
                                      sq(motion.velocity[2], r.after.vz);
                // strict-threshold servers correct below our replay cutoff: log every correction for parity work
                if (PERLIN_ENV("PERLIN_CORRECTION_LOG")) {
                    std::ostringstream line;
                    line << std::setprecision(std::numeric_limits<float>::max_digits10)
                         << "[correction] tick=" << motion.tick << " dpos=(" << dx << "," << dy << "," << dz << ") dvel=("
                         << ex << "," << ey << "," << ez << ") ours=(" << r.after.x << "," << r.after.y << "," << r.after.z
                         << ") server=(" << motion.pos[0] << "," << motion.pos[1] << "," << motion.pos[2]
                         << ") vel ours=(" << r.after.vx << "," << r.after.vy << "," << r.after.vz << ") server=("
                         << motion.velocity[0] << "," << motion.velocity[1] << "," << motion.velocity[2]
                         << ") ground ours=" << r.after.onGround << " server=" << motion.onGround
                         << " before=(" << r.before.x << "," << r.before.y << "," << r.before.z << ") beforeVel=("
                         << r.before.vx << "," << r.before.vy << "," << r.before.vz << ") fwd=" << r.input.forward
                         << " strafe=" << r.input.strafe << " jump=" << r.input.jump << " sprint=" << r.input.sprint
                         << " sneak=" << r.input.sneak << " yaw=" << r.yaw << " speed=" << r.attributes.speed;
                    std::cout << line.str() << std::endl;
                }
                if (posErrF > 1.0e-5f || velErrF > 1.0e-5f || r.after.onGround != motion.onGround) {
                    const physics::State shown = state;
                    if (PERLIN_ENV("PERLIN_BLOCK_DEBUG") || correctionReplays < 40 || correctionReplays % 100 == 0) {
                        const Level& lv = session.NetLevel();
                        auto name = [&](int bx, int by, int bz) { return BlockPalette::Instance().Name(lv.GetBlock(bx, by, bz)); };
                        const int fx = static_cast<int>(std::floor(r.before.x)), fz = static_cast<int>(std::floor(r.before.z));
                        const int fy = static_cast<int>(std::floor(r.before.y - 0.001));
                        std::ostringstream diagnostic;
                        diagnostic << std::setprecision(std::numeric_limits<double>::max_digits10);
                        diagnostic << "[move] mismatch tick=" << motion.tick << " ours=(" << r.after.x << "," << r.after.y << ","
                                  << r.after.z << ") server=(" << motion.pos[0] << "," << motion.pos[1] << ","
                                  << motion.pos[2] << ") vel ours=(" << r.after.vx << "," << r.after.vy << "," << r.after.vz
                                  << ") server=(" << motion.velocity[0] << "," << motion.velocity[1] << ","
                                  << motion.velocity[2] << ") ground ours=" << r.after.onGround
                                  << " server=" << motion.onGround << " fwd=" << r.input.forward << " strafe="
                                  << r.input.strafe << " jump=" << r.input.jump << " sprint=" << r.input.sprint
                                  << " sneak=" << r.input.sneak << " feet=" << name(fx, fy + 1, fz) << " under="
                                  << name(fx, fy, fz) << " east=" << name(fx + 1, fy + 1, fz) << " west="
                                  << name(fx - 1, fy + 1, fz) << " north=" << name(fx, fy + 1, fz - 1)
                                  << " south=" << name(fx, fy + 1, fz + 1)
                                  << " before=(" << r.before.x << "," << r.before.y << "," << r.before.z
                                  << ") beforeVel=(" << r.before.vx << "," << r.before.vy << "," << r.before.vz
                                  << ") yaw=" << r.yaw << " speed=" << r.attributes.speed;
                        std::cout << diagnostic.str() << std::endl;
                    }
                    r.after.x = motion.pos[0];
                    r.after.y = motion.pos[1];
                    r.after.z = motion.pos[2];
                    r.after.vx = motion.velocity[0];
                    r.after.vy = motion.velocity[1];
                    r.after.vz = motion.velocity[2];
                    r.after.onGround = motion.onGround;
                    physics::State s = r.after;
                    LevelCollision world(session.NetLevel(), blocksPtr);
                    for (size_t i = 1; i < history.size(); i++) {
                        Record& q = history[i];
                        q.before = s;
                        s.yaw = q.yaw;
                        ApplyTeleport(s, q.teleport);
                        ApplyMotion(s, q.motion);
                        if (q.simulated)
                            physics::Step(s, q.input, q.attributes, world);
                        else
                            s.vx = s.vy = s.vz = 0;
                        q.after = s;
                    }
                    state = s;
                    const double ox = shown.x - state.x, oy = shown.y - state.y, oz = shown.z - state.z;
                    if (ox * ox + oy * oy + oz * oz < 4.0) {
                        smoothOffset[0] += ox;
                        smoothOffset[1] += oy;
                        smoothOffset[2] += oz;
                    }
                    previous = state;
                    if (!history.empty())
                        previous = history.back().before;
                    session.SetPose(static_cast<float>(state.x), static_cast<float>(state.y),
                                    static_cast<float>(state.z), state.yaw, session.GetPose().pitch);
                    correctionReplays++;
                    if (correctionReplays <= 5 || correctionReplays % 100 == 0)
                        Log("move", "replayed correction tick={} posErr={} velErr={} replayed={}", motion.tick, std::sqrt(posErr), std::sqrt(velErr), (history.size() - 1));
                }
                history.pop_front();
                handled = true;
                motion.hasVelocity = false;
            }
        }
        if (motion.poseChanged && !handled) {
            state.x = motion.pos[0];
            state.y = motion.pos[1];
            state.z = motion.pos[2];
            previous = state;
            accumulator = 0.0;
            history.clear();
            if (motion.teleport) {
                teleportMotionBoundary = motion.tick ? motion.tick : session.LastAuthTick();
                futureMotion.clear();
                futureTeleports.clear();
                state.hasTeleported = motion.filterTravel;
                state.vx = state.vy = state.vz = 0;
                state.onGround = motion.onGround;
                smoothOffset[0] = smoothOffset[1] = smoothOffset[2] = 0;
            }
            if (motion.correction)
                state.onGround = motion.onGround;
        }
        // unstamped motion: posDelta written straight to the live state (actor vfunc 0x140BB5800), frames kept and not
        // recorded, so a later rewind replays without it
        if (motion.hasVelocity) {
            state.vx = motion.velocity[0];
            state.vy = motion.velocity[1];
            state.vz = motion.velocity[2];
        }
        if (motion.hasRotation) {
            camera.yaw = CameraYaw(motion.yaw);
            camera.pitch = std::clamp(BedrockPitch(motion.pitch), -89.0f, 89.0f);
        }
    }

    void LocalPlayer::Tick(BedrockSession& session, const InputManager& input, const Camera& camera,
                           const physics::World& world) {
        const uint64_t nextTick = session.LastAuthTick() + 1;
        std::optional<BedrockSession::ServerMotion> tickTeleport;
        for (auto it = futureTeleports.begin(); it != futureTeleports.end();) {
            if (it->tick < nextTick) {
                tickTeleport = *it;
                it = futureTeleports.erase(it);
            } else {
                ++it;
            }
        }
        ApplyTeleport(state, tickTeleport);
        previous = state;
        const physics::State before = state;
        const bool currentWater = physics::TouchingWater(state, world);
        const bool itemSlow = this->itemSlow && input.IsDown(Action::Use)
                           && (!itemSlowUntil || tickCounter < itemSlowUntil);

        const bool asleep = session.Sleeping();
        const int dimension = session.DimensionId();
        const bool dimensionChanged = replayDimension && *replayDimension != dimension;
        replayDimension = dimension;
        const bool dimensionChanging = session.DimensionChanging();
        const bool savePrediction = !asleep && !rideLock && !dimensionChanged && !dimensionChanging;
        if (!savePrediction)
            history.clear();
        const float forwardKeys = asleep ? 0.0f : input.Axis(Action::Forward, Action::Back);
        const float strafeKeys = asleep ? 0.0f : input.Axis(Action::Right, Action::Left);
        const float forward = forwardKeys;
        const float strafe = strafeKeys;
        const bool jump = !asleep && input.IsDown(Action::Jump);
        const bool sneakHeldKey = input.IsDown(Action::Sneak);
        const bool sneakKey = sneakToggle ? sneakLatch : sneakHeldKey;
        if (rideLock && sneakKey && !sneakHeld && !(session.InputLocks() & 256u))
            session.SendLeaveVehicle();
        const bool sprintHeldKey = input.IsDown(Action::Sprint);
        const bool sprintKey = sprintToggle ? sprintLatch : sprintHeldKey;

        const bool wasSprinting = sprinting;
        // ClientInputUpdateSystem 0x140493140: the SNEAKING and SWIMMING flags are still last tick's here, so a
        // released sneak scales one more tick and the swim pose cancels it
        const bool sneakScaled = (sneakKey || sneaking) && !flying && !swimming;
        const physics::MoveVector move = physics::ProcessMoveInput(forward, strafe, sneakScaled, currentWater,
                                                                  itemSlow ? 0.35f : 1.0f, sneakSpeed);
        // SneakingSystem runs after the input stage: its value is next tick's multiplier
        sneakSpeed = physics::SneakSpeed(session.SwiftSneakEnchant(), sneaking);

        // SetRequestSprintTriggerSystem 0x14b3405d0: food <= 6 blocks sprinting unless the player may fly
        const BedrockSession::Vitals vitals = session.GetVitals();
        tooHungryToSprint = vitals.known && vitals.hunger <= 6.0f && !session.MayFly();
        bool blind = false;
        for (const auto& effect : session.GetEffects())
            if (effect.id == 15)
                blind = true;
        // item-use slowdown 0x14ce4c200 clears the double-tap window, SprintTimerSystem 0x14b33f6f0 counts it down
        if (itemSlow)
            sprintTapTicks = 0;
        if (sprintTapTicks > 0)
            --sprintTapTicks;
        // IntentSprintTriggerSystem 0x14b341890, on the scaled move vector
        constexpr float kSprintAxis = 0.70710677f;
        bool sprintIntent = sprinting, sprintStarted = false, moveStop = false;
        if (!itemSlow && move.forward >= kSprintAxis && !tooHungryToSprint && !sprinting && !blind) {
            if (sprintKey) {
                sprintStarted = true;
            } else if ((currentWater || state.onGround || flying) && !prevSneakInput && !prevForwardHigh) {
                // double-tap forward: the second fresh press within 6 ticks
                if (sprintTapTicks > 0)
                    sprintStarted = true;
                else
                    sprintTapTicks = 7;
            }
            sprintIntent = sprintStarted;
        }
        if (sprintIntent) {
            moveStop = true;
            // separate statements: no fused multiply-add, the game squares then adds
            const float forwardSq = move.forward * move.forward;
            const float strafeSq = move.strafe * move.strafe;
            const float length = std::sqrt(forwardSq + strafeSq);
            if (move.forward > 0.0f && length >= kSprintAxis && std::fabs(move.strafe) <= kSprintAxis) {
                // last move made no progress (< 0.00005) along its main requested horizontal axis
                const float requestX = std::fabs(state.requestX), requestZ = std::fabs(state.requestZ);
                const bool stuckZ = requestZ > requestX &&
                    std::fabs(static_cast<float>(state.z) - state.requestFromZ) < 0.00005f;
                const bool stuckX = requestX > requestZ &&
                    std::fabs(static_cast<float>(state.x) - state.requestFromX) < 0.00005f;
                moveStop = stuckZ || stuckX;
            }
            bool stop;
            if (swimming)
                stop = !currentWater;
            else if (currentWater && jumpHeld)
                stop = true;
            else
                stop = moveStop || tooHungryToSprint;
            // the action applies start then stop: both in one tick leaves it off
            if (stop)
                sprinting = false;
            else if (sprintStarted)
                sprinting = true;
        }

        // countdown 0x14b33f0a0 then FlyTriggerIntentSystem 0x145e0ddb0: tick-sampled jump edges, the second
        // within 6 ticks of the first toggles flight
        if (flyTapTicks > 0)
            --flyTapTicks;
        // the toggle flips PlayerInputRequest flying now, but FlyTriggerActionSystem 0x145e0fb40 only queues the
        // ability: MovementAbilities (TravelTypeSensingSystem 0x147cce890) keeps the old travel for this tick
        const bool flyingTravel = flying;
        if (session.MayFly() && !session.NoClip() && !session.Spectator() && !rideLock && jump && !jumpHeld) {
            if (flyTapTicks > 0) {
                flying = !flying;
                (flying ? sendStartFlying : sendStopFlying) = true;
                flyPending = true;
                flyPendingAt = std::chrono::steady_clock::now();
                flyTapTicks = 0;
            } else {
                flyTapTicks = 7;
            }
        }

        const bool wasSneaking = sneaking;
        const auto headInWaterAt = [&](float eyeHeight) {
            const int x = static_cast<int>(std::floor(state.x)), z = static_cast<int>(std::floor(state.z));
            const float eyeY = static_cast<float>(state.y) + eyeHeight;
            const int y = static_cast<int>(std::floor(eyeY));
            const auto cell = world.Query(x, y, z);
            if (cell.fluid != 1) return false;
            // liquid height 0x143534D00: (y + 1) - (d/9 - 1/9) from this cell's depth alone, water above doesn't fill it
            const int depth = cell.fluidDepth < 8 ? cell.fluidDepth + 1 : 1;
            const float drop = static_cast<float>(depth) / 9.0f + std::bit_cast<float>(0xbde38e39u);
            const float height = static_cast<float>(y + 1) - drop;
            return height > eyeY;
        };
        const bool headInWaterBeforePose = headInWaterAt(static_cast<float>(
            (swimming || crawling || gliding) ? physics::kCrawlEye : sneaking ? physics::kSneakEye : physics::kStandEye));
        bool forcedSneak = false;
        bool wantCrawl = false;
        // a riptide spin counts as standing room: no forced pose
        if (!flying && !rideLock && !gliding && !swimming && !state.spinAttack) {
            if (!physics::PoseFits(state, world, physics::kStandHeight)) {
                if (physics::PoseFits(state, world, physics::kSneakHeight))
                    forcedSneak = true;
                // SneakTriggerIntent 0x14B313690: a 0.6 gap starts crawling; in water last tick it starts swimming
                // instead (0x200000) and stops a crawl; no gap at all keeps the current pose
                else if (physics::PoseFits(state, world, physics::kCrawlHeight)) {
                    wantCrawl = !state.inWater;
                    swimming = state.inWater;
                } else
                    wantCrawl = crawling || state.crawling;
            }
        }
        crawling = wantCrawl;
        sneaking = (sneakKey || forcedSneak) && !flying;
        if (crawling) {
            sprinting = false;
            sneaking = true;
        }

        const bool column = session.ColumnSettled(static_cast<int>(std::floor(state.x)) >> 4,
                                                  static_cast<int>(std::floor(state.z)) >> 4);
        const bool simulate = column && !asleep && !dimensionChanging;

        const auto swimBox = physics::BoundingBox(state, wasSneaking);
        // SwimTrigger 0x147CAE950: centre as (max - min) * 0.5f + min in float
        const auto boxCentre = [](double lo, double hi) {
            const float half = (static_cast<float>(hi) - static_cast<float>(lo)) * 0.5f;
            return static_cast<int>(std::floor(half + static_cast<float>(lo)));
        };
        const auto aboveSwimBox = world.Query(boxCentre(swimBox.x0, swimBox.x1),
                                              boxCentre(swimBox.y0, swimBox.y1) + 1,
                                              boxCentre(swimBox.z0, swimBox.z1));
        const float lookPitch = physics::bds::InterpolateAngle(state.previousRotation.pitch,
                                                               BedrockPitch(camera.pitch), 1.0f);
        const float lookCoordinate = physics::bds::detail::Round(
            physics::bds::detail::Round(lookPitch * -0.017453292f) * 10430.378f);
        const float lookY = physics::bds::CurrentSineEntry(physics::bds::SineIndex(lookCoordinate));
        const bool canStartSwimUp = lookY < 0.15f || !aboveSwimBox.isAir
                                || aboveSwimBox.fluid != 0 || !aboveSwimBox.Empty();
        // SwimTriggerSystem 0x147cae950 on the sprint intent (input request +8/+10), the scaled move and the
        // hunger/riding gates; the input request's +32 branch is taken as clear (writer not identified)
        if (!swimming) {
            if (!flying && headInWaterBeforePose && canStartSwimUp && sprintIntent && !moveStop)
                swimming = true;
        } else if (physics::PoseFits(state, world, physics::kStandHeight)) {
            const float forwardSq = move.forward * move.forward;
            const float strafeSq = move.strafe * move.strafe;
            const float length = std::sqrt(forwardSq + strafeSq);
            if (length < kSprintAxis || tooHungryToSprint || rideLock || !currentWater)
                swimming = false;
        }
        // SneakTriggerIntent 0x14B313690 under a low ceiling: a sneak gap with no move input stops the swim into a
        // sneak (0x400000 + start sneak, no crawl); a crawl gap out of water (last tick) turns it into a crawl
        if (swimming && !physics::PoseFits(state, world, physics::kStandHeight)) {
            const float moveSq = forwardKeys * forwardKeys + strafeKeys * strafeKeys;
            const bool sneakGap = physics::PoseFits(state, world, physics::kSneakHeight);
            const bool leaveSwim = sneakGap
                ? moveSq < 0.49999997f
                : physics::PoseFits(state, world, physics::kCrawlHeight) && !state.inWater;
            if (leaveSwim) {
                swimming = false;
                sprinting = false;
                sneaking = true;
                if (sneakGap)
                    forcedSneak = true;
                else
                    crawling = true;
            }
        }
        // StartGlidingIntentSystem 0x14b34d5e0: no lava, levitation or pose test; lava only picks the lava
        // travel system (TravelTypeSensingSystem 0x147cce890) while the glide state stays
        if (!gliding && hasElytra && jump && !jumpHeld && !state.onGround && !flying && !rideLock)
            gliding = true;
        // StopGlidingIntentSystem 0x14b3533b0, same tick as the start: water, climbing, or a fresh jump press
        // after 10 glide ticks (instabuild keeps gliding and lifts instead)
        if (gliding) {
            const auto glideFlags = session.OwnActorFlags();
            const auto glideFeet = world.Query(static_cast<int>(std::floor(state.x)),
                                               static_cast<int>(std::floor(static_cast<float>(state.y))),
                                               static_cast<int>(std::floor(state.z)));
            const auto glideBoots = session.GetInventory().armor[3];
            const bool leatherBoots = !glideBoots.Empty() &&
                (glideBoots.name == "minecraft:leather_boots" || glideBoots.name == "leather_boots");
            const bool climbing = (glideFlags[0] & proto::FlagsMetadataFlags1::Wallclimbing) != 0 ||
                ((glideFlags[0] & proto::FlagsMetadataFlags1::CanClimb) != 0 &&
                 (glideFeet.climbable || (glideFeet.powderSnow && leatherBoots)));
            if (!hasElytra || state.onGround || state.inWater || flying || rideLock || climbing ||
                (jump && !jumpHeld && !session.Instabuild() && fallFlyTicks > 10))
                gliding = false;
        }
        {
            const auto glideNow = std::chrono::steady_clock::now();
            const bool serverGlide = session.ServerGliding();
            if (serverGlide)
                glideReported = true;
            if (gliding != glideLocal) {
                glideLocal = gliding;
                glideChangedAt = glideNow;
            }
            if (glideReported && glideNow - glideChangedAt > std::chrono::milliseconds(600)) {
                if (gliding && !serverGlide) {
                    gliding = false;
                    glideLocal = false;
                    glideChangedAt = glideNow;
                } else if (!gliding && serverGlide && hasElytra && !state.onGround && !state.inWater && !flying && !rideLock) {
                    gliding = true;
                    glideLocal = true;
                    glideChangedAt = glideNow;
                }
            }
        }
        // GlideInputSystem 0x146d09030 (FallFlyTicksComponent), after the stop check above; the lift reads
        // the count before it advances and needs jump held, not a fresh press
        const bool glideLift = gliding && jump && session.Instabuild() && fallFlyTicks >= 11;
        fallFlyTicks = gliding ? fallFlyTicks + 1 : 0;
        physics::Input in;
        in.gliding = gliding;
        in.glideLift = glideLift;
        in.crawl = crawling;
        in.forceCrouch = forcedSneak;
        if (pendingRiptide > 0 && simulate)
            in.riptideLevel = pendingRiptide;
        if (in.riptideLevel > 0 || state.spinAttack)
            in.mobBoxes = mobBoxes;
        pendingRiptide = 0;
        in.pitch = BedrockPitch(camera.pitch);
        in.swimming = swimming && !gliding;
        in.headInWater = headInWaterAt(static_cast<float>((swimming || crawling || gliding) ? physics::kCrawlEye
                                                         : sneaking ? physics::kSneakEye : physics::kStandEye));
        in.lookY = lookY;
        in.forward = move.forward;
        in.strafe = move.strafe;
        in.jump = jump;
        in.sneak = sneakKey;
        in.sprint = sprinting;
        in.flying = flying;
        in.flyToggled = flying != flyingTravel;
        in.flyUp = flying && jump;
        in.flyDown = flying && sneakKey;
        in.noClip = flying && (session.NoClip() || session.Spectator());
        in.creativeFlight = session.GameType() == 1 ||
            (session.GameType() == 5 && session.World().worldGameMode == 1);
        const auto actorFlags = session.OwnActorFlags();
        in.canClimb = (actorFlags[0] & proto::FlagsMetadataFlags1::CanClimb) != 0;
        in.wallClimbing = (actorFlags[0] & proto::FlagsMetadataFlags1::Wallclimbing) != 0;
        in.actorMovementFlags = static_cast<uint8_t>(actorFlags[0]);
        in.pushTowardsClosestSpace = (actorFlags[1] & proto::FlagsMetadataFlags2::PushTowardsClosestSpace) != 0;
        in.gravity.hasGravity = (actorFlags[0] & proto::FlagsMetadataFlags1::AffectedByGravity) != 0;
        in.gravity.inScaffolding = (actorFlags[1] & proto::FlagsMetadataFlags2::InScaffolding) != 0;
        in.gravity.overScaffolding = (actorFlags[1] & proto::FlagsMetadataFlags2::OverScaffolding) != 0;
        in.gravity.descendThroughBlock = (actorFlags[1] & proto::FlagsMetadataFlags2::FallThroughScaffolding) != 0;
        const auto boots = session.GetInventory().armor[3];
        in.canStandOnPowderSnow = !boots.Empty() &&
            (boots.name == "minecraft:leather_boots" || boots.name == "leather_boots");
        const auto contactBox = physics::BoundingBox(state, wasSneaking);
        const auto scaffold = physics::bds::SenseScaffolding(
            physics::bds::CollisionBox{static_cast<float>(contactBox.x0), static_cast<float>(contactBox.y0),
                static_cast<float>(contactBox.z0), static_cast<float>(contactBox.x1),
                static_cast<float>(contactBox.y1), static_cast<float>(contactBox.z1)},
            [&](int32_t x, int32_t y, int32_t z) {
                return world.ClimberCell(x, y, z);
            }, in.canStandOnPowderSnow);
        in.gravity.inScaffolding = scaffold.inside;
        in.gravity.overScaffolding = scaffold.over;
        in.inAscendableBlock = scaffold.ascendable;
        in.overDescendableBlock = scaffold.descendable;
        in.gravity.descendThroughBlock = in.sneak && in.overDescendableBlock;

        // FreezingSystem 0x145E14260 runs after the server's move (0x146FF8110, after the movement group): this tick
        // moves with the strength the last step left, the step reads the box after this move; any leather armor
        // piece (0x140BC68B0) or spectator mode only thaws
        bool leatherArmor = false;
        const auto inventory = session.GetInventory();
        for (const auto& piece : inventory.armor) {
            std::string_view name = piece.name;
            if (name.starts_with("minecraft:"))
                name.remove_prefix(10);
            leatherArmor |= !piece.Empty() && (name == "leather_helmet" || name == "leather_chestplate" ||
                                               name == "leather_leggings" || name == "leather_boots");
        }
        const bool spectatorMode = session.GameType() == 6 ||
            (session.GameType() == 5 && session.World().worldGameMode == 6);

        physics::Attributes attributes;
        attributes.speed = session.PredictedMovementSpeed(sprinting, freezeStrength);
        attributes.flySpeed = session.FlySpeed();
        attributes.verticalFlySpeed = session.VFlySpeed();
        attributes.airDrag = session.AirDragAttribute();
        attributes.waterWalkSpeedEnchant = session.WaterWalkSpeedEnchant();
        attributes.soulSpeed = session.SoulSpeedEnchant() > 0;
        attributes.underwaterSpeed = session.UnderwaterMovementSpeedAttribute();
        attributes.lavaSpeed = session.LavaMovementSpeedAttribute();
        for (const auto& effect : session.GetEffects()) {
            if (effect.id == 8)
                attributes.jumpBoost = effect.amplifier + 1;
            else if (effect.id == 24)
                attributes.levitation = effect.amplifier + 1;
            else if (effect.id == 27)
                attributes.slowFalling = true;
            else if (effect.id == 33)
                attributes.weaving = true;
        }

        // the prediction keeps the shorter-duration check (0x14355F1D0), the server's entry after it does not
        if (predictGlideBoost && simulate)
            in.movementEffects.push_back(
                {nextTick, 20, static_cast<int32_t>(physics::bds::MovementEffectType::GlideBoost)});
        predictGlideBoost = false;
        if (simulate) {
            in.movementEffects.insert(in.movementEffects.end(), futureMovementEffects.begin(),
                                      futureMovementEffects.end());
            futureMovementEffects.clear();
        }

        state.yaw = BedrockYaw(camera.yaw);
        Record rec;
        rec.input = in;
        rec.attributes = attributes;
        rec.yaw = state.yaw;
        rec.before = state;
        rec.simulated = simulate;
        rec.freeze = freezeStrength;
        rec.teleport = tickTeleport;
        // a stamp past the newest frame still lands on the live state at once (0x142CD98E0): next tick, whatever the stamp
        for (const auto& pending : futureMotion) {
            rec.motion = pending.velocity;
            if (PERLIN_ENV("PERLIN_BLOCK_DEBUG"))
                Log("move", "motion_apply stamp={} target={}", pending.tick, nextTick);
        }
        futureMotion.clear();
        ApplyMotion(state, rec.motion);
        if (simulate) {
            physics::Step(state, in, attributes, world);
        } else {
            state.vx = state.vy = state.vz = 0;
        }

        // the vehicle moves in the movement tick; the rider is placed on it after (passenger positioning)
        const bool drivingVehicle = TickVehicle(session, forward, strafe, jump, in.pitch, world);
        if (rideLock) {
            state.x = rideX;
            state.y = rideY;
            state.z = rideZ;
            state.vx = state.vy = state.vz = 0;
            state.onGround = true;
            flying = false;
        }
        const auto frozenBox = physics::BoundingBox(state, state.collisionHeight == static_cast<float>(physics::kSneakHeight));
        rec.freezeRising = !leatherArmor && !spectatorMode && physics::bds::TouchesPowderSnow(
            static_cast<float>(frozenBox.x0), static_cast<float>(frozenBox.y0), static_cast<float>(frozenBox.z0),
            static_cast<float>(frozenBox.x1), static_cast<float>(frozenBox.y1), static_cast<float>(frozenBox.z1),
            [&](int x, int y, int z) { return world.Query(x, y, z).powderSnow; });
        freezeStrength = physics::bds::FreezeStep(freezeStrength, rec.freezeRising);
        walkBob.Tick(std::hypot(state.x - before.x, state.z - before.z), state.onGround && !flying,
                     state.inWater && sprinting);
        ++tickCounter;


        int slownessAmplifier = -1;
        for (const auto& effect : session.GetEffects())
            if (effect.id == 2)
                slownessAmplifier = effect.amplifier;
        fov.Tick(MovementFovTarget(session.MovementSpeedAttribute(), session.WalkSpeed(),
                                   flying, slownessAmplifier));

        BedrockSession::MoveInput packet;
        std::vector<uint32_t>& f = packet.flags;
        const bool goLeft = strafe < 0.0f, goRight = strafe > 0.0f;
        const bool goForward = forward > 0.0f, goBack = forward < 0.0f;
        if (goForward) f.push_back(InputFlag::Up);
        if (goBack) f.push_back(InputFlag::Down);
        if (goLeft) f.push_back(InputFlag::Left);
        if (goRight) f.push_back(InputFlag::Right);
        if (goForward && goLeft) f.push_back(Flag::UpLeft);
        if (goForward && goRight) f.push_back(Flag::UpRight);
        if (goBack && goLeft) f.push_back(Flag::DownLeft);
        if (goBack && goRight) f.push_back(Flag::DownRight);

        const bool jumpInitiated = !flying && before.jumpDelay == 0 && state.jumpDelay == 9;
        if (jump) {
            f.push_back(Flag::JumpDown);
            f.push_back(Flag::Jumping);
            f.push_back(Flag::JumpCurrentRaw);
            f.push_back(Flag::WantUp);
        }
        if (jumpInitiated)
            f.push_back(Flag::StartJumping);
        if (jump && !jumpHeld)
            f.push_back(Flag::JumpPressedRaw);
        if (!jump && jumpHeld)
            f.push_back(Flag::JumpReleasedRaw);
        jumpHeld = jump;
        // StorePreviousClientInputSystem 0x147cb9d90: what the next double tap compares against
        prevSneakInput = sneakKey;
        prevForwardHigh = move.forward >= kSprintAxis;

        if (sneakKey) {
            f.push_back(Flag::Sneaking);
            f.push_back(Flag::SneakDown);
            f.push_back(Flag::SneakCurrentRaw);
            f.push_back(Flag::WantDown);
        }
        if (sneaking && !wasSneaking)
            f.push_back(Flag::StartSneaking);
        if (!sneaking && wasSneaking)
            f.push_back(Flag::StopSneaking);
        if (sneakKey && !sneakHeld)
            f.push_back(Flag::SneakPressedRaw);
        if (!sneakKey && sneakHeld)
            f.push_back(Flag::SneakReleasedRaw);
        sneakHeld = sneakKey;

        if (sprintKey)
            f.push_back(Flag::SprintDown);
        if (sprinting)
            f.push_back(Flag::Sprinting);
        if (sprinting && !wasSprinting)
            f.push_back(Flag::StartSprinting);
        if (!sprinting && wasSprinting)
            f.push_back(Flag::StopSprinting);
        sprintHeld = sprintKey;
        if (swimming && !sentSwimming)
            f.push_back(Flag::StartSwimming);
        if (!swimming && sentSwimming)
            f.push_back(Flag::StopSwimming);
        sentSwimming = swimming;
        if (crawling && !sentCrawling)
            f.push_back(Flag::StartCrawling);
        if (!crawling && sentCrawling)
            f.push_back(Flag::StopCrawling);
        sentCrawling = crawling;
        if (gliding && !sentGliding)
            f.push_back(Flag::StartGliding);
        if (!gliding && sentGliding)
            f.push_back(Flag::StopGliding);
        if (in.riptideLevel > 0)
            f.push_back(Flag::StartSpinAttack);
        if ((before.spinAttack || in.riptideLevel > 0) && !state.spinAttack)
            f.push_back(Flag::StopSpinAttack);
        sentGliding = gliding;

        if (flying && jump)
            f.push_back(Flag::Ascend);
        if (flying && sneakKey)
            f.push_back(Flag::Descend);
        if (sendStartFlying) {
            f.push_back(InputFlag::StartFlying);
            sendStartFlying = false;
        }
        if (sendStopFlying) {
            f.push_back(InputFlag::StopFlying);
            sendStopFlying = false;
        }
        if (state.horizontalCollision)
            f.push_back(Flag::HorizontalCollision);
        if (state.verticalCollision)
            f.push_back(Flag::VerticalCollision);

        packet.moveX = -move.strafe;
        packet.moveZ = move.forward;
        packet.rawX = -strafeKeys;
        packet.rawZ = forwardKeys;
        packet.yaw = BedrockYaw(camera.yaw);
        packet.pitch = BedrockPitch(camera.pitch);
        packet.eyeHeight = kPlayerEyeHeight;
        packet.position[0] = static_cast<float>(state.x);
        packet.position[1] = static_cast<float>(state.y);
        packet.position[2] = static_cast<float>(state.z);
        packet.delta[0] = static_cast<float>(state.vx);
        packet.delta[1] = static_cast<float>(state.vy);
        packet.delta[2] = static_cast<float>(state.vz);
        packet.hasDelta = true;

        session.SetPose(packet.position[0], packet.position[1], packet.position[2], packet.yaw, packet.pitch);
        if (drivingVehicle && boatActive) {
            // SendPlayerInputPacketSystem 0x146dab080: position and posDelta of the vehicle, not the rider
            packet.eyeHeight = 0.0f;
            packet.position[0] = boat.x;
            packet.position[1] = boat.y;
            packet.position[2] = boat.z;
            packet.delta[0] = boat.vx;
            packet.delta[1] = boat.vy;
            packet.delta[2] = boat.vz;
            packet.vehicleRotation = std::array<float, 2>{boat.pitch, boat.yaw};
            packet.predictedVehicle = boatUid;
        } else if (drivingVehicle && horseActive) {
            const auto& h = horse.state;
            packet.eyeHeight = 0.0f;
            packet.position[0] = static_cast<float>(h.x);
            packet.position[1] = static_cast<float>(h.y);
            packet.position[2] = static_cast<float>(h.z);
            packet.delta[0] = static_cast<float>(h.vx);
            packet.delta[1] = static_cast<float>(h.vy);
            packet.delta[2] = static_cast<float>(h.vz);
            packet.vehicleRotation = std::array<float, 2>{horse.pitch, horse.yaw};
            packet.predictedVehicle = horseUid;
        }
        if (session.SendMoveInput(packet)) {
            if (drivingVehicle && boatActive && !boatHistory.empty())
                boatHistory.back().tick = session.LastAuthTick();
            if (drivingVehicle && horseActive && !horseHistory.empty())
                horseHistory.back().tick = session.LastAuthTick();
            rec.tick = session.LastAuthTick();
            rec.after = state;
            if (tickObserver) tickObserver(rec.tick, state, rec.input);
            if (savePrediction)
                history.push_back(rec);
            while (history.size() > 200)
                history.pop_front();
        }
    }

    namespace {
        // HorseFlagComponent actors (SetIsClientPredictedHorseSystem 0x14b31b900 query)
        bool HorseFamily(const std::string& type) {
            return type == "minecraft:horse" || type == "minecraft:donkey" || type == "minecraft:mule" ||
                   type == "minecraft:skeleton_horse" || type == "minecraft:zombie_horse";
        }

        std::optional<float> EntityAttribute(const entity::Entity& e, const char* name) {
            const auto it = e.attributes.find(name);
            if (it == e.attributes.end())
                return std::nullopt;
            return it->second.value;
        }
    }

    bool LocalPlayer::TickVehicle(BedrockSession& session, float forward, float strafe, bool jump, float riderPitch,
                                  const physics::World& world) {
        rideSeatKnown = false;
        const int64_t uid = session.RidingUid();
        entity::Entity vehicle;
        const bool riding = rideLock && uid != 0 && session.Entities().FindUnique(uid, vehicle);
        const bool driver = riding && session.RidingAsDriver();
        // SetIsClientPredictedBoatSystem 0x14b31ac80: every boat whose controlling seat holds the local player
        const bool drivingBoat = driver && vehicle.type.find("boat") != std::string::npos;
        // SetIsClientPredictedHorseSystem 0x14b31b900: horse family, only TAMED (flag 28) and SADDLED (flag 8)
        const bool drivingHorse = driver && HorseFamily(vehicle.type) &&
                                  (vehicle.flags & proto::FlagsMetadataFlags1::Tamed) != 0 &&
                                  (vehicle.flags & proto::FlagsMetadataFlags1::Saddled) != 0;
        if (!drivingBoat && boatActive) {
            boatActive = false;
            boatHistory.clear();
            session.SetPredictedVehicle(0);
        }
        if (!drivingHorse && horseActive) {
            horseActive = false;
            horseHistory.clear();
            session.SetPredictedVehicle(0);
        }
        const physics::MoveVector move = physics::ProcessMoveInput(forward, strafe, false, false);
        if (drivingBoat) {
            if (!boatActive || boatUid != uid) {
                boat = {};
                boat.x = static_cast<float>(vehicle.pos.x);
                boat.y = static_cast<float>(vehicle.pos.y);
                boat.z = static_cast<float>(vehicle.pos.z);
                boat.pitch = vehicle.pitch;
                boat.yaw = vehicle.yaw;
                if (vehicle.boxWidth > 0.0f && vehicle.boxHeight > 0.0f) {
                    boat.width = vehicle.boxWidth;
                    boat.height = vehicle.boxHeight;
                }
                boat.onGround = vehicle.onGround;
                boatActive = true;
                boatUid = uid;
                boatRuntime = vehicle.runtimeId;
                boatHistory.clear();
                session.SetPredictedVehicle(boatRuntime);
            }
            physics::vehicle::BoatInput in;
            in.controlled = true;
            in.moveX = -move.strafe;
            in.moveZ = move.forward;
            boat.leashed = (vehicle.flags & proto::FlagsMetadataFlags1::Leashed) != 0;
            physics::vehicle::TickBoat(boat, in, physics::vehicle::Buoyancy{}, world);
            boatHistory.push_back({0, in, boat});
            while (boatHistory.size() > 200)
                boatHistory.pop_front();
            session.Entities().Move(boatRuntime, {boat.x, boat.y, boat.z}, boat.pitch, boat.yaw, boat.yaw, boat.onGround);
            vehicle.pos = {boat.x, boat.y, boat.z};
            vehicle.yaw = boat.yaw;
        }
        if (drivingHorse) {
            if (!horseActive || horseUid != uid) {
                horse = {};
                // collision box: the server's BOUNDINGBOX metadata, else horse.json's 1.4 x 1.6
                const float width = vehicle.boxWidth > 0.0f && vehicle.boxHeight > 0.0f ? vehicle.boxWidth : 1.4f;
                const float height = vehicle.boxWidth > 0.0f && vehicle.boxHeight > 0.0f ? vehicle.boxHeight : 1.6f;
                horse.state.mobSize = std::array<float, 2>{width, height};
                horse.state.collisionHeight = height;
                horse.state.x = static_cast<float>(vehicle.pos.x);
                horse.state.y = static_cast<float>(vehicle.pos.y);
                horse.state.z = static_cast<float>(vehicle.pos.z);
                horse.state.onGround = vehicle.onGround;
                horse.pitch = horse.previousPitch = vehicle.pitch;
                horse.yaw = horse.previousYaw = vehicle.yaw;
                horse.state.yaw = vehicle.yaw;
                horseActive = true;
                horseUid = uid;
                horseRuntime = vehicle.runtimeId;
                horseHistory.clear();
                session.SetPredictedVehicle(horseRuntime);
            }
            physics::vehicle::HorseInput in;
            in.forward = move.forward;
            in.strafe = move.strafe;
            in.riderYaw = state.yaw;
            in.riderPitch = riderPitch;
            in.jumpHeld = jump;
            in.movement = EntityAttribute(vehicle, "minecraft:movement").value_or(0.1f);
            in.jumpStrength = EntityAttribute(vehicle, "minecraft:horse.jump_strength");
            in.underwaterMovement = EntityAttribute(vehicle, "minecraft:underwater_movement").value_or(0.02f);
            in.lavaMovement = EntityAttribute(vehicle, "minecraft:lava_movement").value_or(0.02f);
            in.canPowerJump = (vehicle.flags & proto::FlagsMetadataFlags1::CanPowerJump) != 0;
            in.hasGravity = (vehicle.flags & proto::FlagsMetadataFlags1::AffectedByGravity) != 0;
            in.canClimb = (vehicle.flags & proto::FlagsMetadataFlags1::CanClimb) != 0;
            in.wallClimbing = (vehicle.flags & proto::FlagsMetadataFlags1::Wallclimbing) != 0;
            physics::vehicle::TickHorse(horse, in, world);
            horseHistory.push_back({0, in, horse});
            while (horseHistory.size() > 200)
                horseHistory.pop_front();
            const auto& h = horse.state;
            session.Entities().Move(horseRuntime, {h.x, h.y, h.z}, horse.pitch, horse.yaw, horse.yaw, h.onGround);
            vehicle.pos = {h.x, h.y, h.z};
            vehicle.yaw = horse.yaw;
        }
        if (!riding)
            return false;
        if (const auto seat = session.OwnSeatOffset()) {
            // VehicleClientPositionPassengerSystem::setPosition 0x142cee0d0: seat turned by the vehicle yaw,
            // added to the vehicle position; the result is the rider's eye-level position
            const float angle = vehicle.yaw * -0.017453292f;
            const float cosine = std::cos(angle), sine = std::sin(angle);
            const float sx = (*seat)[0], sy = (*seat)[1], sz = (*seat)[2];
            rideX = (sz * sine + sx * cosine) + static_cast<float>(vehicle.pos.x);
            rideY = static_cast<double>(sy + static_cast<float>(vehicle.pos.y)) - physics::kStandEye;
            rideZ = (sz * cosine - sx * sine) + static_cast<float>(vehicle.pos.z);
            rideSeatKnown = true;
        }
        return drivingBoat || drivingHorse;
    }

    void LocalPlayer::ApplyVehicleCorrections(BedrockSession& session) {
        BedrockSession::VehicleCorrection c;
        const auto sq = [](float a, float b) { const float d = a - b; return d * d; };
        // correction event check 0x1410E8FF0: rewind on a ground change, position, delta or rotation past
        // 1e-5 (0x3727C5AC) squared, or an angular velocity off by more than 1e-4 (0x38D1B717); without a
        // recorded state for that tick nothing is compared and the prediction stays
        const auto mismatch = [&](float x, float y, float z, float vx, float vy, float vz, float pitch, float yaw,
                                  bool onGround, std::optional<float> angularVelocity) {
            const float posErr = sq(c.pos[2], z) + (sq(c.pos[1], y) + sq(c.pos[0], x));
            const float velErr = sq(c.delta[2], vz) + (sq(c.delta[1], vy) + sq(c.delta[0], vx));
            const float rotErr = sq(c.rotation[1], yaw) + sq(c.rotation[0], pitch);
            const bool spin = c.angularVelocity && angularVelocity &&
                              !(1.0e-4f >= std::fabs(*angularVelocity - *c.angularVelocity));
            return c.onGround != onGround || posErr > 1.0e-5f || velErr > 1.0e-5f || rotErr > 1.0e-5f || spin;
        };
        while (session.TakeVehicleCorrection(c)) {
            // the player's replay state is lent to the vehicle (LendReplayStateSystem 0x14ad2afc0): rewind it
            if (boatActive) {
                while (!boatHistory.empty() && boatHistory.front().tick != 0 && boatHistory.front().tick < c.tick)
                    boatHistory.pop_front();
                if (boatHistory.empty() || boatHistory.front().tick != c.tick)
                    continue;
                auto& ours = boatHistory.front().after;
                if (!mismatch(ours.x, ours.y, ours.z, ours.vx, ours.vy, ours.vz, ours.pitch, ours.yaw, ours.onGround,
                              ours.angularVelocity))
                    continue;
                ours.x = c.pos[0];
                ours.y = c.pos[1];
                ours.z = c.pos[2];
                ours.vx = c.delta[0];
                ours.vy = c.delta[1];
                ours.vz = c.delta[2];
                ours.pitch = c.rotation[0];
                ours.yaw = c.rotation[1];
                if (c.angularVelocity)
                    ours.angularVelocity = *c.angularVelocity;
                ours.onGround = c.onGround;
                physics::vehicle::Boat replay = ours;
                LevelCollision world(session.NetLevel(), blocksPtr);
                for (size_t i = 1; i < boatHistory.size(); ++i) {
                    physics::vehicle::TickBoat(replay, boatHistory[i].input, physics::vehicle::Buoyancy{}, world);
                    boatHistory[i].after = replay;
                }
                boat = replay;
                session.Entities().Move(boatRuntime, {boat.x, boat.y, boat.z}, boat.pitch, boat.yaw, boat.yaw,
                                        boat.onGround);
            } else if (horseActive) {
                while (!horseHistory.empty() && horseHistory.front().tick != 0 && horseHistory.front().tick < c.tick)
                    horseHistory.pop_front();
                if (horseHistory.empty() || horseHistory.front().tick != c.tick)
                    continue;
                auto& ours = horseHistory.front().after;
                auto& h = ours.state;
                // a horse has no angular velocity component: that lane is skipped
                if (!mismatch(static_cast<float>(h.x), static_cast<float>(h.y), static_cast<float>(h.z),
                              static_cast<float>(h.vx), static_cast<float>(h.vy), static_cast<float>(h.vz), ours.pitch,
                              ours.yaw, h.onGround, std::nullopt))
                    continue;
                h.x = c.pos[0];
                h.y = c.pos[1];
                h.z = c.pos[2];
                h.vx = c.delta[0];
                h.vy = c.delta[1];
                h.vz = c.delta[2];
                ours.pitch = c.rotation[0];
                ours.yaw = c.rotation[1];
                h.yaw = ours.yaw;
                h.onGround = c.onGround;
                h.hasBox = false;
                physics::vehicle::Horse replay = ours;
                LevelCollision world(session.NetLevel(), blocksPtr);
                for (size_t i = 1; i < horseHistory.size(); ++i) {
                    physics::vehicle::TickHorse(replay, horseHistory[i].input, world);
                    horseHistory[i].after = replay;
                }
                horse = replay;
                const auto& r = horse.state;
                session.Entities().Move(horseRuntime, {r.x, r.y, r.z}, horse.pitch, horse.yaw, horse.yaw, r.onGround);
            }
        }
    }

    void LocalPlayer::ModMotion(bool add, double x, double y, double z, bool ax, bool ay, bool az) {
        std::lock_guard lock(modMutex);
        if (modQueue.size() < 256)
            modQueue.push_back({false, add, false, ax, ay, az, x, y, z});
    }

    void LocalPlayer::ModTeleport(double x, double y, double z, bool keepVelocity) {
        std::lock_guard lock(modMutex);
        if (modQueue.size() < 256)
            modQueue.push_back({true, false, keepVelocity, true, true, true, x, y, z});
    }

    void LocalPlayer::ApplyModCommands() {
        std::vector<ModCommand> list;
        {
            std::lock_guard lock(modMutex);
            list.swap(modQueue);
        }
        for (const ModCommand& c : list) {
            if (c.teleport) {
                state.x = c.x;
                state.y = c.y;
                state.z = c.z;
                state.hasBox = false;
                if (!c.keep)
                    state.vx = state.vy = state.vz = 0.0;
                previous = state;
                history.clear();
                continue;
            }
            if (c.ax)
                state.vx = c.add ? state.vx + c.x : c.x;
            if (c.ay)
                state.vy = c.add ? state.vy + c.y : c.y;
            if (c.az)
                state.vz = c.add ? state.vz + c.z : c.z;
        }
    }

    void LocalPlayer::Update(BedrockSession& session, InputManager& input, Camera& camera,
                             const BlockLibrary& blocks, float dt) {
        BedrockSession::PlayerPose pose = session.GetPose();
        if (!pose.known)
            return;

        if (!initialised) {
            state = {};
            state.x = pose.x;
            state.y = pose.y;
            state.z = pose.z;
            previous = state;
            initialised = true;
            flying = session.IsFlying();
        }
        blocksPtr = &blocks;
        if (sneakToggle) {
            if (input.Pressed(Action::Sneak))
                sneakLatch = !sneakLatch;
        } else {
            sneakLatch = false;
        }
        if (sprintToggle) {
            if (input.Pressed(Action::Sprint))
                sprintLatch = !sprintLatch;
        } else {
            sprintLatch = false;
        }
        if (session.Sleeping() || (replayDimension && *replayDimension != session.DimensionId()))
            history.clear();
        if (rideLock) {
            session.TakeEffectUpdates();
            futureMotion.clear();
            futureTeleports.clear();
            BedrockSession::ServerMotion ignored;
            while (session.TakeServerMotion(ignored)) {}
            history.clear();
            ApplyVehicleCorrections(session);
        } else {
            ApplyEffectUpdates(session);
            ApplyMovementEffectUpdates(session);
            BedrockSession::ServerMotion motion;
            while (session.TakeServerMotion(motion))
                ApplyServerMotion(session, camera, motion);
            ApplyVehicleCorrections(session);
        }
        SyncFreeze(session);
        ApplyModCommands();

        const auto now = std::chrono::steady_clock::now();
        const bool spectator = session.NoClip() || session.Spectator();
        if (spectator) {
            if (!flying) {
                flying = true;
                sendStartFlying = true;
            }
        } else if (flying && !session.MayFly()) {
            flying = false;
            sendStopFlying = true;
        }
        if (flyPending && (session.IsFlying() == flying || now - flyPendingAt > std::chrono::milliseconds(750)))
            flyPending = false;
        // an abilities update overwrites flying even when it repeats the last value: our double tap may differ
        const bool abilitiesSent = session.AbilitiesSeq() != abilitiesSeq;
        abilitiesSeq = session.AbilitiesSeq();
        if (!flyPending && (abilitiesSent || session.IsFlying() != serverFlying)) {
            serverFlying = session.IsFlying();
            if (serverFlying && session.MayFly() && !flying) {
                flying = true;
            } else if (!serverFlying && flying && !spectator) {
                flying = false;
            }
        }
        LevelCollision world(session.NetLevel(), &blocks);
        if (!session.Initialized()) {
            accumulator = 0.0;
            previous = state;
        } else {
            // Timer::advanceTime 0x145A3B180 (20 tps, 0x41A00000): at most 0.1 s of real time per frame, the rest dropped
            accumulator += std::min(dt, 0.1f);
        }
        int ticks = 0;
        while (accumulator >= physics::kTickSeconds) {
            accumulator -= physics::kTickSeconds;
            if (++ticks > kMaxTicksPerFrame) {
                accumulator = 0.0;
                break;
            }
            input.SetControlTick(tickCounter);
            Tick(session, input, camera, world);
            // VanillaOffsetSystem::ClientSide 0x142CED480: the drop below eye level moves halfway each tick
            const float standEye = static_cast<float>(physics::kStandEye);
            // no riding case in 0x142CED480: a rider placed from RiderSeatPosition keeps the standing eye;
            // 1.2 only for the renderer seat fallback
            const float targetEye = rideLock ? (rideSeatKnown ? standEye : 1.2f)
                : (crawling || gliding || swimming || state.spinAttack) ? static_cast<float>(physics::kCrawlEye)
                : sneaking ? static_cast<float>(physics::kSneakEye)
                : session.Sleeping() ? static_cast<float>(physics::kSleepEye) : standEye;
            eyeDropPrevious = eyeDrop;
            eyeDrop = (standEye - targetEye - eyeDrop) * 0.5f + eyeDrop;
        }

        const double t = std::clamp(accumulator / physics::kTickSeconds, 0.0, 1.0);
        const double x = previous.x + (state.x - previous.x) * t;
        const double y = previous.y + (state.y - previous.y) * t;
        const double z = previous.z + (state.z - previous.z) * t;
        eyeOffset = static_cast<float>(physics::kStandEye) -
                    (eyeDropPrevious + (eyeDrop - eyeDropPrevious) * static_cast<float>(t));
        const double offsetSize = std::sqrt(smoothOffset[0] * smoothOffset[0] + smoothOffset[1] * smoothOffset[1] +
                                            smoothOffset[2] * smoothOffset[2]);
        const double decay = std::exp(-dt * (offsetSize < 0.4 ? 7.0 : 14.0));
        for (double& o : smoothOffset)
            o = std::fabs(o) < 1e-4 ? 0.0 : o * decay;
        camera.x = static_cast<float>(x + smoothOffset[0]);
        camera.y = static_cast<float>(y + smoothOffset[1]) + eyeOffset;
        camera.z = static_cast<float>(z + smoothOffset[2]);
    }
}
