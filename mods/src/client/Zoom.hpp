#pragma once
#include "client/Options.hpp"
#include "client/input/InputManager.hpp"

#include <algorithm>
#include <cmath>

namespace perlin {
    class Zoom {
    public:
        explicit Zoom(const ZoomSettings& settings) : cfg(settings), level(settings.level) {}

        void SetConfig(const ZoomSettings& settings) { cfg = settings; }

        LookDelta Update(float dt, bool held, float scroll, LookDelta raw) {
            if (held && !wasHeld && cfg.resetOnPress)
                level = cfg.level;
            wasHeld = held;
            if (held && scroll != 0.0f)
                level =
                    std::clamp(level * std::pow(1.0f - cfg.scrollStep, scroll), cfg.minLevel, cfg.maxLevel);
            shownLevel += (level - shownLevel) * (1.0f - std::exp(-dt * 16.0f));
            if (!active)
                shownLevel = level;

            const float seconds = held ? cfg.inSeconds : cfg.outSeconds;
            const float step = seconds <= 0.0001f ? 1.0f : dt / seconds;
            progress = std::clamp(progress + (held ? step : -step), 0.0f, 1.0f);
            const float eased = Ease(progress);
            factor = std::exp(std::log(std::max(shownLevel, 1e-4f)) * eased);
            active = progress > 0.0f;

            if (!active || !cfg.cinematic || cfg.smoothing <= 0.0f) {
                const float scale = active && cfg.scaleSensitivity ? std::clamp(factor, 0.05f, 1.0f) : 1.0f;
                LookDelta out{raw.yaw * scale + poolYaw, raw.pitch * scale + poolPitch};
                poolYaw = poolPitch = 0.0f;
                return out;
            }
            const float scale = cfg.scaleSensitivity ? std::clamp(factor, 0.05f, 1.0f) : 1.0f;
            poolYaw += raw.yaw * scale;
            poolPitch += raw.pitch * scale;
            const float k = 1.0f - std::exp(-dt * cfg.smoothing);
            LookDelta out{poolYaw * k, poolPitch * k};
            poolYaw -= out.yaw;
            poolPitch -= out.pitch;
            return out;
        }

        float Fov(float baseDegrees) const {
            if (factor >= 0.999f)
                return baseDegrees;
            const float half = baseDegrees * 0.5f * 0.017453292519943295f;
            return 2.0f * std::atan(std::tan(half) * factor) * 57.29577951308232f;
        }
        bool Active() const { return active; }

    private:
        float Ease(float t) const {
            const std::string& e = cfg.easing;
            if (e == "linear")
                return t;
            if (e == "cubic")
                return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2;
            if (e == "quint")
                return t < 0.5f ? 16 * t * t * t * t * t : 1 - std::pow(-2 * t + 2, 5.0f) / 2;
            if (e == "sine")
                return -(std::cos(3.14159265f * t) - 1) / 2;
            return t * t * (3 - 2 * t);
        }

        ZoomSettings cfg;
        float level;
        float shownLevel = 1.0f;
        float progress = 0.0f;
        float factor = 1.0f;
        bool wasHeld = false;
        bool active = false;
        float poolYaw = 0.0f, poolPitch = 0.0f;
    };
}
