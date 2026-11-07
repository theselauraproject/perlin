#include "client/player/FlightRoll.hpp"

#include <algorithm>
#include <cmath>

namespace perlin {
    namespace {
        constexpr double kDeg = 3.14159265358979323846 / 180.0;
        constexpr double kTurnScale = 0.15;
        constexpr double kBankingStrength = 20.0;
        constexpr double kButtonSpeed = 1800.0;
        constexpr double kSensitivityPitch = 1.0, kSensitivityYaw = 0.4, kSensitivityRoll = 1.0;
        constexpr double kSmoothPitch = 1.0, kSmoothYaw = 0.4, kSmoothRoll = 1.0;

        glm::dvec3 Rotated(const glm::dvec3& v, const glm::dvec3& toward, double angle) {
            return v * std::cos(angle) + toward * std::sin(angle);
        }

        void Orthonormalise(glm::dvec3& f, glm::dvec3& u, glm::dvec3& r) {
            f = glm::normalize(f);
            r = glm::normalize(glm::cross(f, u));
            u = glm::normalize(glm::cross(r, f));
        }
    }

    double FlightRoll::Smoother::Next(double delta, double factor) {
        target += delta;
        double f = target - remaining;
        const double blended = last + (f - last) * 0.5;
        const double sign = f > 0.0 ? 1.0 : f < 0.0 ? -1.0 : 0.0;
        if (sign * f > sign * last)
            f = blended;
        last = blended;
        remaining += f * factor;
        return f * factor;
    }

    void FlightRoll::Begin(float yaw, float pitch) {
        const double y = yaw * kDeg, p = pitch * kDeg;
        forward = {std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p)};
        up = {-std::cos(y) * std::sin(p), std::cos(p), -std::sin(y) * std::sin(p)};
        right = glm::normalize(glm::cross(forward, up));
        pitchSmooth.Reset();
        yawSmooth.Reset();
        rollSmooth.Reset();
        active = true;
    }

    void FlightRoll::End() {
        active = false;
    }

    void FlightRoll::Derive(Result& out) {
        const double py = std::clamp(forward.y, -1.0, 1.0);
        double pitch = std::asin(py);
        const double limit = 89.9 * kDeg;
        pitch = std::clamp(pitch, -limit, limit);
        const double yaw = std::atan2(forward.z, forward.x);
        const glm::dvec3 f(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch));
        const glm::dvec3 u0(-std::cos(yaw) * std::sin(pitch), std::cos(pitch),
                            -std::sin(yaw) * std::sin(pitch));
        const glm::dvec3 r0 = glm::normalize(glm::cross(f, u0));
        rollDegrees = static_cast<float>(std::atan2(glm::dot(up, r0), glm::dot(up, u0)) / kDeg);
        out.yaw = static_cast<float>(yaw / kDeg);
        out.pitch = static_cast<float>(pitch / kDeg);
    }

    FlightRoll::Result FlightRoll::Step(float dtF, float mouseRight, float mouseUp, float yawKeys,
                                        float rollKeys, float pitchKeys) {
        const double dt = std::clamp(static_cast<double>(dtF), 0.0, 0.1);
        double pitchIn = mouseUp / kTurnScale;
        double yawIn = 0.0;
        double rollIn = mouseRight / kTurnScale;
        pitchIn = pitchSmooth.Next(pitchIn, 1.0 / kSmoothPitch * dt);
        yawIn = yawSmooth.Next(yawIn, 1.0 / kSmoothYaw * dt);
        rollIn = rollSmooth.Next(rollIn, 1.0 / kSmoothRoll * dt);

        const double rollRad = rollDegrees * kDeg;
        const double pitchRad = std::asin(std::clamp(forward.y, -1.0, 1.0));
        const double bankYaw = std::sin(rollRad) * std::cos(pitchRad) * 10.0 * kBankingStrength * dt;
        const double bankPitch =
            (1.0 - std::cos(rollRad)) * std::cos(pitchRad) * 10.0 * kBankingStrength * dt;

        double dPitch = pitchIn * kSensitivityPitch;
        double dYaw = yawIn * kSensitivityYaw;
        double dRoll = rollIn * kSensitivityRoll;

        const double button = kButtonSpeed * dt;
        dPitch += pitchKeys * button;
        dYaw += yawKeys * button;
        dRoll += rollKeys * button;

        const double a = dPitch * kTurnScale * kDeg;
        const double b = dYaw * kTurnScale * kDeg;
        const double c = dRoll * kTurnScale * kDeg;

        if (std::abs(bankYaw) > 1e-9 || std::abs(bankPitch) > 1e-9) {
            const double by = bankYaw * kSensitivityYaw * kTurnScale * kDeg;
            const double bp = bankPitch * kSensitivityPitch * kTurnScale * kDeg;
            const glm::dvec3 worldUp(0.0, 1.0, 0.0);
            const auto rotateAbout = [&](const glm::dvec3& axis, double angle) {
                const double cs = std::cos(angle), sn = std::sin(angle);
                const auto rot = [&](const glm::dvec3& v) {
                    return v * cs + glm::cross(axis, v) * sn + axis * glm::dot(axis, v) * (1.0 - cs);
                };
                forward = rot(forward);
                up = rot(up);
                right = rot(right);
            };
            rotateAbout(worldUp, -by);
            glm::dvec3 flat(forward.x, 0.0, forward.z);
            if (glm::dot(flat, flat) > 1e-6) {
                flat = glm::normalize(flat);
                const glm::dvec3 axis = glm::normalize(glm::cross(flat, worldUp));
                rotateAbout(axis, bp);
            }
        }

        {
            const glm::dvec3 f = forward, u = up, r = right;
            forward = Rotated(f, u, a);
            up = Rotated(u, f, -a);
            right = r;
        }
        {
            const glm::dvec3 f = forward, r = right;
            forward = Rotated(f, r, b);
            right = Rotated(r, f, -b);
        }
        {
            const glm::dvec3 u = up, r = right;
            up = Rotated(u, r, c);
            right = Rotated(r, u, -c);
        }
        Orthonormalise(forward, up, right);
        Result out;
        Derive(out);
        return out;
    }

    void FlightRoll::Settle(float dt, float yaw, float pitch) {
        if (std::abs(rollDegrees) < 0.01f) {
            rollDegrees = 0.0f;
            return;
        }
        const double y = yaw * kDeg, p = pitch * kDeg;
        forward = {std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p)};
        const double next = rollDegrees * std::exp(-static_cast<double>(dt) * 9.0);
        rollDegrees = static_cast<float>(next);
        const glm::dvec3 u0(-std::cos(y) * std::sin(p), std::cos(p), -std::sin(y) * std::sin(p));
        const glm::dvec3 r0 = glm::normalize(glm::cross(forward, u0));
        const double c = rollDegrees * kDeg;
        up = u0 * std::cos(c) + r0 * std::sin(c);
        right = glm::normalize(glm::cross(forward, up));
    }
}
