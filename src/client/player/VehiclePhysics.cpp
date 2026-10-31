#include "client/player/VehiclePhysics.hpp"

#include <bit>
#include <cmath>

namespace perlin::physics::vehicle {
    namespace {
        float F32(uint32_t bits) { return std::bit_cast<float>(bits); }

        // SSE maxss/minss: the first operand only when strictly greater/less, else the second
        float MaxSs(float a, float b) { return a > b ? a : b; }
        float MinSs(float a, float b) { return a < b ? a : b; }

        struct Cell {
            int x, y, z;
        };

        Cell FloorCell(float x, float y, float z) {
            return {static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), static_cast<int>(std::floor(z))};
        }

        // liquid_blocks of the boat: water and flowing_water only
        bool Water(const BlockShape& s) { return s.fluid == 1; }

        // 0x143534d00: liquid top of the cell, from its liquid_depth state (missing state counts as 1)
        float LiquidTop(const BlockShape& s, int y) {
            const int n = s.fluidDepth < 8 ? s.fluidDepth + 1 : 1;
            return static_cast<float>(y + 1) - (static_cast<float>(n) / 9.0f + F32(0xbde38e39));
        }

        // liquid_blocks lists: water only (boat) or water and lava (horse family)
        bool Listed(const BlockShape& s, bool lava) { return s.fluid == 1 || (lava && s.fluid == 2); }

        // 0x1437a1760: a listed liquid at the position below its surface, none above
        bool AtSurface(const World& world, float x, float y, float z, bool lava = false) {
            const Cell c = FloorCell(x, y, z);
            const BlockShape here = world.Query(c.x, c.y, c.z);
            if (!Listed(here, lava) || LiquidTop(here, c.y) <= y)
                return false;
            return !Listed(world.Query(c.x, c.y + 1, c.z), lava);
        }

        // 0x1437a18f0: a listed liquid here and above
        bool Submerged(const World& world, float x, float y, float z, bool lava = false) {
            const Cell c = FloorCell(x, y, z);
            return Listed(world.Query(c.x, c.y, c.z), lava) && Listed(world.Query(c.x, c.y + 1, c.z), lava);
        }

        // 0x1437d7990: the block's own friction when it has a collision box, the one below otherwise
        float FrictionAt(const World& world, const Cell& c) {
            const BlockShape here = world.CollisionQuery(c.x, c.y, c.z);
            if (!here.Empty())
                return here.friction;
            return world.Query(c.x, c.y - 1, c.z).friction;
        }

        // minecraft:buoyant systems, client registrar group 0x14694a720; returns the new posDelta.y
        float Buoyant(float x, float y, float z, float vx, float vy, float vz, double& timer, bool& atSurface,
                      bool& submerged, bool leashed, const Buoyancy& buoyancy, const World& world) {
            // IncreaseBuoyancyTimer 0x146959eb0; the big-wave roll uses the server's random stream, not modelled
            if (buoyancy.movementType == 2) {
                timer = timer + 1.0;
            } else {
                const double speed = static_cast<double>(std::sqrt(vz * vz + vx * vx)) * 30.0 + 1.0;
                timer = static_cast<double>(static_cast<float>(speed * static_cast<double>(0.05f))) + timer;
            }
            // CheckAndAddBuoyancyFloatRequest 0x146949c50
            atSurface = AtSurface(world, x, y, z, buoyancy.lava);
            submerged = Submerged(world, x, y, z, buoyancy.lava);
            // BuoyancyGravity 0x14695c240
            if (buoyancy.applyGravity)
                vy = (vy + F32(0xbd23d70a)) * F32(0x3f7ae148);
            // BuoyancyFloat 0x146959470
            float lift = 0.0f, wave = 0.0f;
            if (atSurface) {
                const float fraction = y - static_cast<float>(static_cast<int>(std::floor(y)));
                const float raw = (buoyancy.base - fraction) * F32(0x3f666666) + F32(0x3dcccccd);
                lift = 1.0f < raw ? 1.0f : MaxSs(raw, 0.0f);
                if (buoyancy.movementType == 1)
                    wave = (static_cast<float>(std::sin(timer)) + 1.0f) * F32(0x3d0f5c29);
                else if (buoyancy.movementType == 2)
                    wave = (static_cast<float>(std::sin(timer * static_cast<double>(F32(0x3ea0d97c)))) + 1.2f) * 0.5f;
            } else if (submerged) {
                lift = 1.0f;
            }
            if ((atSurface || submerged) && lift != 0.0f) {
                const float target = ((lift - wave) + F32(0xbdcccccd)) * F32(0x3e19999a);
                const float damped = vy * F32(0x3f333333);
                // current base game versions: a rising leashed body keeps at least 5% of its lift
                if (leashed && damped > 0.0f)
                    vy = MaxSs(target, damped * F32(0x3d4ccccd));
                else
                    vy = MinSs(target, damped + F32(0x3d4ccccd));
            }
            return vy;
        }

        // BoatMoveFrictionSystem 0x14ace0140
        void Friction(Boat& b, const World& world) {
            const Cell c = FloorCell(b.x, b.y, b.z);
            // leashed: BoatMovementComponent+16, 0.91f on current base game versions (written at 0x1438bf055)
            b.friction = b.leashed ? F32(0x3f68f5c3) : 1.0f;
            b.uncontrolled = true;
            const float water = F32(0x3f666666);
            if (AtSurface(world, b.x, b.y, b.z)) {
                b.friction = water;
                b.uncontrolled = false;
                b.submergedTicks = 0;
            } else if (Submerged(world, b.x, b.y, b.z)) {
                b.friction = water;
                b.uncontrolled = false;
                ++b.submergedTicks;
            } else {
                const BlockShape here = world.Query(c.x, c.y, c.z);
                const bool air = here.isAir && here.fluid == 0;
                if (!air) {
                    if (Water(here)) {
                        b.friction = water;
                        b.uncontrolled = false;
                    } else if (b.onGround) {
                        b.friction = FrictionAt(world, c);
                        b.uncontrolled = false;
                    }
                } else if (b.onGround) {
                    b.friction = world.Query(c.x, c.y - 1, c.z).friction;
                    b.uncontrolled = false;
                } else if (Water(world.Query(c.x, c.y - 1, c.z))) {
                    b.friction = water;
                    b.uncontrolled = false;
                }
            }
            b.vx = b.friction * b.vx;
            b.vz = b.friction * b.vz;
            b.angularVelocity = b.friction * b.angularVelocity;
        }

        // VehicleBoatPaddleInputSystem 0x14b375070, analog lane: 3x the intent, eased for the first 10 ticks of a stroke
        void PaddleForce(BoatPaddles& p, int lane, float intent) {
            if (intent == 0.0f) {
                p.force[lane] = intent;
                return;
            }
            const float full = 3.0f * intent;
            const int32_t now = static_cast<int32_t>(p.tick);
            if (now - p.rowStart[lane] >= 10) {
                p.force[lane] = full;
                p.rowStart[lane] = now;
                return;
            }
            const float eased = MaxSs(0.0f, std::fabs(full) + F32(0xbdcccccd));
            p.force[lane] = 0.0f > intent ? -eased : eased;
        }

        // BoatMoveControlClientSystem 0x14ad15310: paddle thrust and torque, turn, then the friction factor again
        void Control(Boat& b, bool controlled) {
            if (b.submergedTicks > 24)
                return;
            float side = 0.0f, forward = 0.0f, torqueX = 0.0f, torqueY = 0.0f, torqueZ = 0.0f;
            if (controlled) {
                float strength = b.paddles.force[0] * F32(0x3c6147ae);
                if (strength == 0.0f) {
                    b.paddles.rowTime[0] = 0.0f;
                    b.paddles.rowStart[0] = -1;
                } else {
                    if (!b.uncontrolled) {
                        // (0, s, 0) crossed with the (3, 0, 0) lever, done lane-wise like the game
                        const float lane0 = 0.0f * strength;
                        torqueX = 0.0f + (0.0f * lane0 - lane0);
                        torqueY = 0.0f + (3.0f * strength - 0.0f * lane0);
                        torqueZ = (0.0f * lane0 - 3.0f * lane0) + 0.0f;
                        side = lane0 + 0.0f;
                        forward = strength;
                    }
                    const float time = strength + b.paddles.rowTime[0];
                    b.paddles.rowTime[0] = 1000.0f < time ? time + -1000.0f : time;
                }
                strength = b.paddles.force[1] * F32(0x3c6147ae);
                if (strength == 0.0f) {
                    b.paddles.rowTime[1] = 0.0f;
                    b.paddles.rowStart[1] = -1;
                } else {
                    if (!b.uncontrolled) {
                        const float lane0 = 0.0f * strength;
                        const float negativeZero = -0.0f * lane0;
                        const float lever = 3.0f * lane0 + negativeZero;
                        side = side + lane0;
                        torqueX = torqueX + (lane0 + negativeZero);
                        torqueY = torqueY + (-3.0f * strength - negativeZero);
                        torqueZ = torqueZ + lever;
                        forward = forward + strength;
                    }
                    const float time = strength + b.paddles.rowTime[1];
                    b.paddles.rowTime[1] = 1000.0f < time ? time + -1000.0f : time;
                }
            }
            float turn = torqueY;
            const float speedSquared = b.vz * b.vz + b.vx * b.vx;
            if (F32(0x3c23d70b) > speedSquared && (torqueX != 0.0f || torqueY != 0.0f || torqueZ != 0.0f)) {
                side = side * b.friction;
                forward = forward * b.friction;
                turn = turn * F32(0x3fcccccd);
            }
            turn = (turn * 10.0f + b.angularVelocity) * b.friction;
            b.angularVelocity = turn;
            b.yaw = turn + b.yaw;
            const float angle = (90.0f - b.yaw) * F32(0x3c8efa35);
            // CRT cosf/sinf imports, host libm is the nearest match
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            const float dx = forward * sine + side * cosine;
            const float dz = forward * cosine - sine * side;
            const float vx = dx + b.vx;
            b.vy = side + b.vy;
            const float vz = dz + b.vz;
            b.vx = vx * b.friction;
            b.vz = vz * b.friction;
        }
    }

    Box Boat::Bounds() const {
        const float half = width * 0.5f;
        return {static_cast<double>(x - half), static_cast<double>(y), static_cast<double>(z - half),
                static_cast<double>(x + half), static_cast<double>(y + height), static_cast<double>(z + half)};
    }

    void PaddleIntent(float moveX, float moveZ, float& left, float& right) {
        const float z = 1.0f < moveZ ? 1.0f : MaxSs(moveZ, -1.0f);
        float length = std::sqrt(z * z + moveX * moveX);
        if (0.0f > z)
            length = length * F32(0xbe19999a);
        const float turn = z < 0.0f ? -moveX : moveX;
        const float rest = 1.0f - turn;
        float lane0 = 0.0f < turn ? rest + -0.0f : 1.0f;
        float lane1 = -1.0f < turn ? turn + 1.0f : 1.0f;
        if (1.0f < turn)
            lane0 = 0.0f;
        if (0.0f < turn)
            lane1 = 1.0f;
        left = length * lane0;
        right = length * lane1;
    }

    void TickBoat(Boat& b, const BoatInput& input, const Buoyancy& buoyancy, const World& world) {
        if (input.controlled) {
            float left = 0, right = 0;
            PaddleIntent(input.moveX, input.moveZ, left, right);
            PaddleForce(b.paddles, 0, left);
            PaddleForce(b.paddles, 1, right);
            ++b.paddles.tick;
        }
        Friction(b, world);
        Control(b, input.controlled);

        // BoatMoveRequestSystem 0x14ad19570: the move request is the posDelta
        const BoxMove moved = MoveBox(world, b.Bounds(), b.vx, b.vy, b.vz, b.onGround, b.recovery);
        b.x = moved.x;
        b.y = moved.y;
        b.z = moved.z;
        b.vx = moved.vx;
        b.vy = moved.vy;
        b.vz = moved.vz;
        b.onGround = moved.onGround;
        b.horizontalCollision = moved.horizontalCollision;
        b.verticalCollision = moved.verticalCollision;

        b.vy = Buoyant(b.x, b.y, b.z, b.vx, b.vy, b.vz, b.waveTimer, b.atSurface, b.submerged, b.leashed, buoyancy,
                       world);
    }

    namespace {
        // VehicleDesiredMoveDirectionSystem wrap: fmodf(a + 180, 360), +360 when negative, then -180
        float WrapDegrees(float angle) {
            const float m = std::fmod(angle + 180.0f, 360.0f);
            return (m < 0.0f ? m + 360.0f : m) + -180.0f;
        }
    }

    void TickHorse(Horse& h, const HorseInput& in, const World& world) {
        State& s = h.state;
        // ResetJumpRidingScaleSystem 0x147b6ea80: ten ticks after a release the charge is gone
        if (h.jumpCounter < 0) {
            ++h.jumpCounter;
            if (h.jumpCounter == 0)
                h.jumpRidingScale = 0.0f;
        }
        // PassengerJumpTriggerSystem 0x147b8e130; VanillaClientGameplay+21 is last tick's jump key
        std::optional<int32_t> percent;
        if (h.jumpHeldBefore) {
            if (in.jumpHeld) {
                const int32_t n = h.jumpCounter++;
                h.jumpRidingScale = n > 8 ? (2.0f / static_cast<float>(n - 8)) * 0.1f + 0.8f
                                          : static_cast<float>(n + 1) * 0.1f;
            } else {
                h.jumpCounter = -10;
                const float scaled = h.jumpRidingScale * 100.0f;
                const int32_t truncated = static_cast<int32_t>(scaled);
                percent = static_cast<float>(truncated) > scaled ? truncated - 1 : truncated;
            }
        }
        h.jumpHeldBefore = in.jumpHeld;
        // VehicleJumpTriggerSystem 0x147b908e0 -> 0x14ad6a840: percentage to JumpPendingScale, horse rears
        if (percent && *percent >= 0) {
            const uint32_t p = static_cast<uint32_t>(*percent);
            h.jumpPendingScale = p <= 89u ? (static_cast<float>(p) * 0.4f) / 90.0f + 0.4f : 1.0f;
            h.standing = true;
        }

        // PassengerDesiredMoveDirectionSystem 0x1484c1f10 copies the rider look and LocalMoveVelocity, the travel
        // intent after MobTravelIntent's 0.98 (0x143606ab0)
        float nf = in.forward, ns = in.strafe;
        const float lengthSq = nf * nf + ns * ns;
        if (lengthSq > 1.0f) {
            const float length = std::sqrt(lengthSq);
            nf /= length;
            ns /= length;
        }
        const bds::Float3 rider = bds::AttenuateTravelIntent({ns, 0.0f, nf}, 0.0f).movement;
        // VehicleDesiredMoveDirectionSystem 0x1484edbc0
        float yaw = in.riderYaw;
        if (in.localController) {
            const float limit = MinSs(45.0f, std::fabs(WrapDegrees(in.riderYaw - h.yaw)));
            const float ease = MaxSs(F32(0x3e3851ec), (45.0f - limit) / 90.0f) * F32(0x3f333333);
            yaw = WrapDegrees(in.riderYaw - h.yaw) * ease + h.yaw;
        }
        const float pitch = in.riderPitch * 0.5f;
        const float wrappedYaw = WrapDegrees(yaw);
        h.previousYaw = WrapDegrees((wrappedYaw - yaw) + h.previousYaw);
        h.yaw = wrappedYaw;
        const float wrappedPitch = WrapDegrees(pitch);
        h.previousPitch = WrapDegrees((wrappedPitch - pitch) + h.previousPitch);
        h.pitch = wrappedPitch;
        // travel intent: half sideways, a quarter backwards (x here is right-positive, the game's is left-positive)
        const bds::Float3 intent{0.5f * rider.x, rider.y, !(rider.z <= 0.0f) ? rider.z : 0.25f * rider.z};
        s.yaw = h.yaw;

        // TriggerJumpSystem 0x147cd1140 -> ApplyJumpModifierSystem 0x147b3dec0: power jump from the ground
        if (h.jumpPendingScale > 0.0f && in.canPowerJump && in.jumpStrength && s.onGround) {
            bds::JumpParameters probe;
            probe.bodyMinimumY = static_cast<float>(BoundingBox(s, false).y0);
            probe.position = {static_cast<float>(s.x), static_cast<float>(s.y), static_cast<float>(s.z)};
            const auto trigger = bds::GroundJump(probe, [&](bds::BlockPosition position) {
                return world.Query(position[0], position[1], position[2]).jumpProperties;
            }).trigger;
            const float multiplier = trigger.reducedSurface ? 0.6f : 1.0f;
            const float scale = h.jumpPendingScale;
            // jump boost on the horse itself: not tracked, adds (amplifier + 1) * 0.1f
            const float effect = 0.0f;
            s.vy = (*in.jumpStrength * scale) * multiplier + effect * multiplier;
            if (intent.z > 0.0f) {
                const float coordinate = (h.yaw * 0.017453292f) * 10430.378f;
                s.vx = static_cast<float>(s.vx) -
                       (bds::CurrentSineEntry(bds::SineIndex(coordinate)) * 0.4f) * scale;
                s.vz = (0.4f * bds::CurrentSineEntry(bds::SineIndex(coordinate + 16384.0f))) * scale +
                       static_cast<float>(s.vz);
            }
            h.jumpPendingScale = 0.0f;
        }

        // MobTravelUpdateSpeedsSystem 0x143606ee0, local controller: ground speed = movement, air = movement * 0.1f
        Attributes attributes;
        attributes.speed = in.movement;
        attributes.airSpeed = in.movement * 0.1f;
        attributes.underwaterSpeed = in.underwaterMovement;
        attributes.lavaSpeed = in.lavaMovement;
        attributes.maxAutoStep = in.maxAutoStep;
        Input step;
        step.mobIntent = intent;
        step.pitch = h.pitch;
        step.canClimb = in.canClimb;
        step.wallClimbing = in.wallClimbing;
        step.gravity.hasGravity = in.hasGravity;
        Step(s, step, attributes, world);

        const float vy = Buoyant(static_cast<float>(s.x), static_cast<float>(s.y), static_cast<float>(s.z),
                                 static_cast<float>(s.vx), static_cast<float>(s.vy), static_cast<float>(s.vz),
                                 h.waveTimer, h.atSurface, h.submerged, false, HorseBuoyancy(), world);
        // can_auto_step_from_liquid: at the surface, not under, adds CanAlwaysAutoStepFlagComponent
        if (h.atSurface && !h.submerged)
            s.canAlwaysAutoStep = true;
        s.vy = vy;
    }
}
