#pragma once
#include "client/attachable/Geometry.hpp"
#include <algorithm>
#include <cmath>

namespace perlin::entity::java {
    inline float Wrap(float a) {
        return a - std::floor((a + 180.0f) / 360.0f) * 360.0f;
    }
    inline float Turn(float a, float b, float t) {
        return a + Wrap(b - a) * t;
    }
    struct BodyRotation {
        float previous = 0, current = 0, stableHead = 0;
        int stableTicks = 0;
        void Reset(float yaw) {
            previous = current = stableHead = yaw;
            stableTicks = 0;
        }
        void PlayerTick(double dx, double dz, float facing, bool swinging) {
            previous = current;
            float target = current;
            if (dx * dx + dz * dz > 0.0025000002) {
                float direction = static_cast<float>(std::atan2(dz, dx) * 57.295779513 - 90.0);
                const float difference = std::abs(Wrap(facing - direction));
                target = difference > 95.0f ? direction - 180.0f : direction;
            }
            if (swinging)
                target = facing;
            current = Turn(current, target, 0.3f);
            const float difference = Wrap(facing - current);
            current += difference - std::clamp(difference, -50.0f, 50.0f);
            previous = current - Wrap(current - previous);
        }
        void MobTick(double dx, double dz, float facing, float head, float limit = 75.0f) {
            previous = current;
            if (dx * dx + dz * dz > 2.500000277905201e-7) {
                current = facing;
                stableHead = head;
                stableTicks = 0;
            } else {
                if (std::abs(Wrap(head - stableHead)) > 15.0f) {
                    stableTicks = 0;
                    stableHead = head;
                } else if ((stableTicks = std::min(stableTicks + 1, 20)) > 10) {
                    limit *= 1.0f - std::clamp((stableTicks - 10) / 10.0f, 0.0f, 1.0f);
                }
                current = head - std::clamp(Wrap(head - current), -limit, limit);
            }
            previous = current - Wrap(current - previous);
        }
        float Sample(float partial) const { return Turn(previous, current, partial); }
    };
    // BodyRotation driven by render frames: each 20 Hz tick sees the position and head at its own time
    struct PlayerBodyClock {
        BodyRotation rotation;
        double remainder = 0, lastX = 0, lastZ = 0, tickX = 0, tickZ = 0;
        float lastHead = 0;
        bool init = false;
        void Reset(double x, double z, float head) {
            rotation.Reset(head);
            remainder = 0;
            lastX = tickX = x;
            lastZ = tickZ = z;
            lastHead = head;
            init = true;
        }
        float Frame(double x, double z, float head, double dt, bool swinging, bool riding, float facing) {
            // teleports and first sight: no turn toward a jump of several blocks
            if (!init || (x - lastX) * (x - lastX) + (z - lastZ) * (z - lastZ) > 64.0)
                Reset(x, z, head);
            remainder += dt;
            int ticks = 0;
            while (remainder + 1e-10 >= 0.05 && ticks < 8) {
                remainder = std::max(0.0, remainder - 0.05);
                const double f = dt > 0 ? std::clamp(1.0 - remainder / dt, 0.0, 1.0) : 1.0;
                const double tx = lastX + (x - lastX) * f, tz = lastZ + (z - lastZ) * f;
                if (riding) {
                    rotation.previous = rotation.current;
                    rotation.current = Turn(rotation.current, facing, 0.3f);
                } else
                    rotation.PlayerTick(tx - tickX, tz - tickZ, Turn(lastHead, head, static_cast<float>(f)), swinging);
                tickX = tx;
                tickZ = tz;
                ++ticks;
            }
            // long stall: drop the backlog instead of replaying it
            remainder = std::min(remainder, 0.05);
            lastX = x;
            lastZ = z;
            lastHead = head;
            const float body = rotation.Sample(static_cast<float>(remainder / 0.05));
            if (riding)
                return body;
            // the sampled body trails the head by one tick: keep the 50 degree cap on screen too
            const float offset = Wrap(head - body);
            return body + offset - std::clamp(offset, -50.0f, 50.0f);
        }
    };
    struct WalkAnimation {
        float previousSpeed = 0, speed = 0, position = 0;
        void Reset() { previousSpeed = speed = position = 0; }
        void Tick(float distance, bool stopped) {
            if (stopped) {
                Reset();
                return;
            }
            previousSpeed = speed;
            speed += (std::min(distance * 4.0f, 1.0f) - speed) * 0.4f;
            position += speed;
        }
        float Speed(float p) const { return previousSpeed + (speed - previousSpeed) * p; }
        float Position(float p, bool baby = false) const {
            return (position - speed * (1.0f - p)) * (baby ? 3.0f : 1.0f);
        }
    };
    inline double TickFraction(double remainder, double dt) {
        return std::clamp(1.0 - remainder / dt, 0.0, 1.0);
    }
    struct PoseBlend {
        float previous = 0, current = 0;
        void Tick(bool active, float step = 0.09f) {
            previous = current;
            current = std::clamp(current + (active ? step : -step), 0.0f, 1.0f);
        }
        float Sample(float partial) const { return previous + (current - previous) * partial; }
    };
    inline float AdvanceAttack(float progress, float dt, float duration, bool started) {
        if (started)
            return 0.0001f;
        if (progress <= 0)
            return 0;
        progress += dt / std::max(duration, 0.05f);
        return progress >= 1 ? 0 : progress;
    }
    struct Part {
        glm::vec3 rotation{0}, offset{0};
    };
    struct PlayerPose {
        Part head, body, rightArm, leftArm, rightLeg, leftLeg;
    };
    enum class ArmPose {
        Empty,
        Item,
        Block,
        Bow,
        Trident,
        CrossbowCharge,
        CrossbowHold,
        Spyglass,
        Horn,
        Brush
    };
    struct PlayerState {
        float headYaw = 0, pitch = 0, walk = 0, speed = 0, age = 0, attack = 0, swim = 0, speedScale = 1;
        float useTicks = 0, chargeTicks = 25;
        bool crouching = false, riding = false, gliding = false, usingItem = false;
        bool useLeft = false, mainLeft = false, attackLeft = false, swinging = false;
        ArmPose right = ArmPose::Empty, left = ArmPose::Empty;
    };
    PlayerPose AnimatePlayer(const PlayerState& state);
    struct RootState {
        float bodyYaw = 0, pitch = 0, age = 0, deathTicks = 0, swim = 0, glideTicks = 0, flightYaw = 0,
              height = 1.8f;
        bool gliding = false, spin = false, sleeping = false, inWater = false, visuallySwimming = false;
        bool flyingYaw = false, upsideDown = false, shaking = false;
        int bedDirection = -1;
    };
    glm::mat4 PlayerRootTransform(const RootState& state, float modelScale);
    float FlightYaw(float yaw, float pitch, const glm::vec3& velocityPerTick);
    bool ApplyPlayerPose(const PlayerPose& player, const attachable::Geometry* geometry,
                         attachable::Pose& pose);
}
