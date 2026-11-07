#include <perlin/plugin.hpp>

#include <mod_settings.hpp>

#include <algorithm>
#include <cmath>

using namespace perlin;

namespace {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kRad = kPi / 180.0;
    constexpr double kTurn = 0.15;
    constexpr double kButtonSpeed = 1800.0;
    constexpr double kMomentumSpeed = 1200.0;
    constexpr double kMouseMomentumScale = 1.0 / 300.0;
    constexpr uint32_t kLeft = 4, kRight = 8, kForward = 1, kBack = 2, kJump = 16;

    struct Vec {
        double x = 0, y = 0, z = 0;
    };

    Vec operator+(Vec a, Vec b) {
        return {a.x + b.x, a.y + b.y, a.z + b.z};
    }
    Vec operator*(Vec a, double s) {
        return {a.x * s, a.y * s, a.z * s};
    }
    double dot(Vec a, Vec b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    Vec cross(Vec a, Vec b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    Vec unit(Vec a) {
        const double l = std::sqrt(dot(a, a));
        return l > 1e-12 ? a * (1.0 / l) : Vec{0, 0, -1};
    }
    Vec toward(Vec v, Vec t, double angle) {
        return v * std::cos(angle) + t * std::sin(angle);
    }
    Vec rotate_about(Vec v, Vec axis, double angle) {
        const double c = std::cos(angle), s = std::sin(angle);
        return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0 - c));
    }

    struct Smoother {
        double target = 0, remaining = 0, last = 0;

        double next(double delta, double factor) {
            target += delta;
            double f = target - remaining;
            const double blended = last + (f - last) * 0.5;
            const double sign = f > 0 ? 1.0 : f < 0 ? -1.0 : 0.0;
            if (sign * f > sign * last)
                f = blended;
            last = blended;
            remaining += f * factor;
            return f * factor;
        }
        void reset() { target = remaining = last = 0; }
    };

    struct Flight {
        bool rolling = false;
        bool armed = false;
        bool was_gliding = false;
        bool jump_released = false;
        bool prev_jump = false;
        bool toggled_off = false;
        Vec f{0, 0, -1}, u{0, 1, 0}, r{1, 0, 0};
        double roll_deg = 0;
        double mx = 0, my = 0;
        Smoother pitch_s, yaw_s, roll_s;
        bool pitch_up = false, pitch_down = false, yaw_left = false, yaw_right = false, roll_left = false,
             roll_right = false;
        bool enabled_by_key = true;

        void begin(Vec dir) {
            f = unit(dir);
            const double p = std::asin(std::clamp(f.y, -1.0, 1.0));
            const double y = std::atan2(f.z, f.x);
            u = {-std::cos(y) * std::sin(p), std::cos(p), -std::sin(y) * std::sin(p)};
            r = unit(cross(f, u));
            roll_deg = 0;
            mx = my = 0;
            pitch_s.reset();
            yaw_s.reset();
            roll_s.reset();
        }

        void orthonormalise() {
            f = unit(f);
            r = unit(cross(f, u));
            u = unit(cross(r, f));
        }

        void derive_roll() {
            const double p = std::clamp(std::asin(std::clamp(f.y, -1.0, 1.0)), -89.9 * kRad, 89.9 * kRad);
            const double y = std::atan2(f.z, f.x);
            const Vec f0{std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p)};
            const Vec u0{-std::cos(y) * std::sin(p), std::cos(p), -std::sin(y) * std::sin(p)};
            const Vec r0 = unit(cross(f0, u0));
            roll_deg = std::atan2(dot(u, r0), dot(u, u0)) / kRad;
        }
    };

    Flight flight;

    double smoothed(Smoother& s, double value, double sens, double dt) {
        if (!settings::smoothing_enabled || sens <= 0.0)
            return value;
        return s.next(value, 1.0 / sens * dt);
    }

    void apply_view(double roll, bool gliding) {
        (void)game::camera({.roll = static_cast<float>(roll), .ticks = 2});
        if (gliding)
            (void)game::model_pose({.yaw = static_cast<float>(-roll), .ticks = 2});
    }

    void run(const abi::EventLook& ev) {
        const bool allowed = settings::mod_enabled && flight.enabled_by_key &&
                             !(settings::disable_when_submerged && ev.underwater);
        const bool gliding = ev.gliding && allowed;
        const bool jump = (ev.keys & kJump) != 0;
        const double dt = std::clamp(static_cast<double>(ev.dt), 0.0, 0.1);

        bool want = false;
        const auto behaviour = settings::activation_behaviour.get();
        if (!gliding) {
            flight.armed = false;
            flight.jump_released = false;
            flight.toggled_off = false;
        } else if (!flight.was_gliding) {
            flight.armed = false;
            flight.jump_released = !jump;
            flight.toggled_off = false;
        }
        if (gliding) {
            if (!jump)
                flight.jump_released = true;
            const bool pressed = jump && !flight.prev_jump && flight.jump_released && flight.was_gliding;
            switch (behaviour) {
            case settings::ActivationBehaviour::Vanilla:
                want = true;
                break;
            case settings::ActivationBehaviour::Hybrid:
                if (pressed)
                    flight.armed = true;
                want = flight.armed;
                break;
            case settings::ActivationBehaviour::HybridToggle:
                if (pressed)
                    flight.armed = !flight.armed;
                want = flight.armed;
                break;
            }
        }
        flight.was_gliding = gliding;
        flight.prev_jump = jump;

        if (!want) {
            flight.rolling = false;
            if (!ev.gliding) {
                flight.armed = false;
                flight.toggled_off = false;
            }
            if (std::abs(flight.roll_deg) > 0.4) {
                flight.roll_deg *= std::exp(-dt * (ev.gliding ? 9.0 : 16.0));
                apply_view(flight.roll_deg, ev.gliding);
            } else if (flight.roll_deg != 0.0) {
                flight.roll_deg = 0;
                apply_view(0.0, false);
            }
            flight.mx = flight.my = 0;
            return;
        }

        if (!flight.rolling) {
            flight.begin({ev.dirX, ev.dirY, ev.dirZ});
            flight.rolling = true;
        }

        double dx = ev.dx / kTurn;
        double dy = ev.dy / kTurn;
        double pitch_in, yaw_in, roll_in = 0;
        if (settings::momentum_based_mouse) {
            flight.mx += dx * kMouseMomentumScale;
            flight.my += dy * kMouseMomentumScale;
            const double len2 = flight.mx * flight.mx + flight.my * flight.my;
            if (len2 > 1.0) {
                const double l = std::sqrt(len2);
                flight.mx /= l;
                flight.my /= l;
            }
            double vx = flight.mx, vy = flight.my;
            const double dead = settings::momentum_mouse_deadzone;
            if (vx * vx + vy * vy < dead * dead)
                vx = vy = 0;
            dx = vx * kMomentumSpeed * dt;
            dy = vy * kMomentumSpeed * dt;
        } else {
            flight.mx = flight.my = 0;
        }
        pitch_in = dy;
        yaw_in = dx;
        if (!settings::switch_roll_and_yaw) {
            roll_in = dx;
            yaw_in = 0;
        }
        if (settings::invert_pitch)
            pitch_in = -pitch_in;

        pitch_in = smoothed(flight.pitch_s, pitch_in, settings::smoothing_pitch, dt);
        yaw_in = smoothed(flight.yaw_s, yaw_in, settings::smoothing_yaw, dt);
        roll_in = smoothed(flight.roll_s, roll_in, settings::smoothing_roll, dt);

        const double roll_rad = flight.roll_deg * kRad;
        const double pitch_rad = std::asin(std::clamp(flight.f.y, -1.0, 1.0));
        double bank_yaw = 0, bank_pitch = 0;
        if (settings::enable_banking) {
            const double strength = settings::banking_strength;
            bank_yaw = std::sin(roll_rad) * std::cos(pitch_rad) * 10.0 * strength * dt;
            bank_pitch = (1.0 - std::cos(roll_rad)) * std::cos(pitch_rad) * 10.0 * strength * dt;
        }

        if (settings::automatic_righting) {
            const double cutoff = std::sqrt(10.0 / 3.0);
            double v = 0;
            if (-cutoff < roll_rad && roll_rad < cutoff)
                v = -std::pow(roll_rad, 3.0) / 3.0 + roll_rad;
            roll_in += -v * 10.0 * static_cast<double>(settings::righting_strength) * dt;
        }

        double eff_pitch = 1.0, eff_yaw = 1.0, eff_roll = 1.0;
        if (settings::simulate_control_surface_efficacy) {
            const double e = ev.vx * flight.f.x + ev.vy * flight.f.y + ev.vz * flight.f.z;
            eff_pitch = eff_yaw = eff_roll = e;
        }

        double d_pitch = pitch_in * eff_pitch * settings::sensitivity_pitch;
        double d_yaw = yaw_in * eff_yaw * settings::sensitivity_yaw;
        double d_roll = roll_in * eff_roll * settings::sensitivity_roll;

        const double button = kButtonSpeed * dt;
        if (ev.keys & kLeft)
            d_yaw -= button;
        if (ev.keys & kRight)
            d_yaw += button;
        if (flight.pitch_up)
            d_pitch += button;
        if (flight.pitch_down)
            d_pitch -= button;
        if (flight.yaw_left)
            d_yaw -= button;
        if (flight.yaw_right)
            d_yaw += button;
        if (flight.roll_left)
            d_roll -= button;
        if (flight.roll_right)
            d_roll += button;

        if (std::abs(bank_yaw) > 1e-9 || std::abs(bank_pitch) > 1e-9) {
            const double by = bank_yaw * settings::sensitivity_yaw * kTurn * kRad;
            const double bp = bank_pitch * settings::sensitivity_pitch * kTurn * kRad;
            const Vec world_up{0, 1, 0};
            const auto spin = [&](Vec axis, double angle) {
                flight.f = rotate_about(flight.f, axis, angle);
                flight.u = rotate_about(flight.u, axis, angle);
                flight.r = rotate_about(flight.r, axis, angle);
            };
            spin(world_up, -by);
            Vec flat{flight.f.x, 0, flight.f.z};
            if (dot(flat, flat) > 1e-6) {
                flat = unit(flat);
                spin(unit(cross(flat, world_up)), bp);
            }
        }

        const double a = d_pitch * kTurn * kRad;
        const double b = d_yaw * kTurn * kRad;
        const double c = d_roll * kTurn * kRad;
        {
            const Vec f = flight.f, u = flight.u;
            flight.f = toward(f, u, a);
            flight.u = toward(u, f, -a);
        }
        {
            const Vec f = flight.f, r = flight.r;
            flight.f = toward(f, r, b);
            flight.r = toward(r, f, -b);
        }
        {
            const Vec u = flight.u, r = flight.r;
            flight.u = toward(u, r, c);
            flight.r = toward(r, u, -c);
        }
        flight.orthonormalise();
        flight.derive_roll();

        if (settings::enable_thrust) {
            const double speed = std::sqrt(ev.vx * ev.vx + ev.vy * ev.vy + ev.vz * ev.vz);
            const double accel = settings::thrust_acceleration * dt * 20.0;
            if ((ev.keys & kForward) && speed < settings::max_thrust)
                (void)game::add_velocity(flight.f.x * accel, flight.f.y * accel, flight.f.z * accel);
            if ((ev.keys & kBack) && speed > 0.01) {
                const double k = std::min(accel, speed) / speed;
                (void)game::add_velocity(-ev.vx * k, -ev.vy * k, -ev.vz * k);
            }
        }

        (void)game::set_look(flight.f.x, flight.f.y, flight.f.z);
        apply_view(flight.roll_deg, true);
    }
}

struct BarrelRoll : Plugin {
    void onEnable(PluginCtx& ctx) override {
        ctx.bind<"toggle">("Toggle Do a Barrel Roll", Key::None, this, &BarrelRoll::toggle);
        ctx.bind<"pitch_up">("Pitch up", Key::None, this, &BarrelRoll::pitchUp);
        ctx.bind<"pitch_down">("Pitch down", Key::None, this, &BarrelRoll::pitchDown);
        ctx.bind<"yaw_left">("Yaw left", Key::None, this, &BarrelRoll::yawLeft);
        ctx.bind<"yaw_right">("Yaw right", Key::None, this, &BarrelRoll::yawRight);
        ctx.bind<"roll_left">("Roll left", Key::None, this, &BarrelRoll::rollLeft);
        ctx.bind<"roll_right">("Roll right", Key::None, this, &BarrelRoll::rollRight);
    }

    void onLook(const abi::EventLook& ev) { run(ev); }

    void toggle() {
        flight.enabled_by_key = !flight.enabled_by_key;
        (void)act::toast(flight.enabled_by_key ? "Do a Barrel Roll enabled" : "Do a Barrel Roll disabled");
    }
    void pitchUp(bool down) { flight.pitch_up = down; }
    void pitchDown(bool down) { flight.pitch_down = down; }
    void yawLeft(bool down) { flight.yaw_left = down; }
    void yawRight(bool down) { flight.yaw_right = down; }
    void rollLeft(bool down) { flight.roll_left = down; }
    void rollRight(bool down) { flight.roll_right = down; }
};
PERLIN_REGISTER(BarrelRoll)
