#include "client/CameraEffects.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace perlin {
    namespace {
        constexpr float kPi = 3.14159265358979f;

        float Bounce(float t) {
            constexpr float n = 7.5625f, d = 2.75f;
            if (t < 1.0f / d)
                return n * t * t;
            if (t < 2.0f / d) {
                t -= 1.5f / d;
                return n * t * t + 0.75f;
            }
            if (t < 2.5f / d) {
                t -= 2.25f / d;
                return n * t * t + 0.9375f;
            }
            t -= 2.625f / d;
            return n * t * t + 0.984375f;
        }

        float Hash(float x) {
            const float s = std::sin(x * 12.9898f) * 43758.5453f;
            return s - std::floor(s);
        }

        float Noise(float x) {
            const float i = std::floor(x), f = x - i;
            const float u = f * f * (3.0f - 2.0f * f);
            return (Hash(i) * (1.0f - u) + Hash(i + 1.0f) * u) * 2.0f - 1.0f;
        }
    }

    double CameraEffects::Now() {
        return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    }

    float CameraEffects::EaseValue(int type, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        const float c1 = 1.70158f, c2 = c1 * 1.525f, c3 = c1 + 1.0f, c4 = 2.0f * kPi / 3.0f,
                    c5 = 2.0f * kPi / 4.5f;
        switch (type) {
        case 0:
        case 32:
            return t;
        case 1:
            return 1.0f - std::cos(t * kPi * 4.5f) * std::exp(-t * 6.0f);
        case 2:
            return t * t;
        case 3:
            return 1.0f - (1.0f - t) * (1.0f - t);
        case 4:
            return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
        case 5:
            return t * t * t;
        case 6:
            return 1.0f - std::pow(1.0f - t, 3.0f);
        case 7:
            return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
        case 8:
            return t * t * t * t;
        case 9:
            return 1.0f - std::pow(1.0f - t, 4.0f);
        case 10:
            return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) / 2.0f;
        case 11:
            return t * t * t * t * t;
        case 12:
            return 1.0f - std::pow(1.0f - t, 5.0f);
        case 13:
            return t < 0.5f ? 16.0f * std::pow(t, 5.0f) : 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) / 2.0f;
        case 14:
            return 1.0f - std::cos(t * kPi / 2.0f);
        case 15:
            return std::sin(t * kPi / 2.0f);
        case 16:
            return -(std::cos(kPi * t) - 1.0f) / 2.0f;
        case 17:
            return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
        case 18:
            return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
        case 19:
            return t == 0.0f   ? 0.0f
                   : t == 1.0f ? 1.0f
                   : t < 0.5f  ? std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f
                               : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;
        case 20:
            return 1.0f - std::sqrt(1.0f - t * t);
        case 21:
            return std::sqrt(1.0f - (t - 1.0f) * (t - 1.0f));
        case 22:
            return t < 0.5f ? (1.0f - std::sqrt(1.0f - std::pow(2.0f * t, 2.0f))) / 2.0f
                            : (std::sqrt(1.0f - std::pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;
        case 23:
            return 1.0f - Bounce(1.0f - t);
        case 24:
            return Bounce(t);
        case 25:
            return t < 0.5f ? (1.0f - Bounce(1.0f - 2.0f * t)) / 2.0f
                            : (1.0f + Bounce(2.0f * t - 1.0f)) / 2.0f;
        case 26:
            return c3 * t * t * t - c1 * t * t;
        case 27:
            return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
        case 28:
            return t < 0.5f
                       ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) / 2.0f
                       : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) /
                             2.0f;
        case 29:
            return t == 0.0f   ? 0.0f
                   : t == 1.0f ? 1.0f
                               : -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
        case 30:
            return t == 0.0f   ? 0.0f
                   : t == 1.0f ? 1.0f
                               : std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
        case 31:
            return t == 0.0f   ? 0.0f
                   : t == 1.0f ? 1.0f
                   : t < 0.5f
                       ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f
                       : (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f +
                             1.0f;
        default:
            return t;
        }
    }

    void CameraEffects::Shake(float intensity, float duration, int type) {
        std::lock_guard lock(mutex);
        shakeIntensity = std::clamp(intensity, 0.0f, 4.0f);
        shakeDuration = std::max(duration, 0.0f);
        shakeType = type;
        shakeStart = Now();
        shakeActive = shakeDuration > 0.0f && shakeIntensity > 0.0f;
    }

    void CameraEffects::StopShake() {
        std::lock_guard lock(mutex);
        shakeActive = false;
        shakeMatrix = glm::mat4(1.0f);
    }

    float CameraEffects::FadeAt(double now) const {
        if (!fadeActive)
            return 0.0f;
        if (now >= fadeEnd)
            return 0.0f;
        if (now >= fadeOutAt)
            return static_cast<float>((fadeEnd - now) / std::max(fadeEnd - fadeOutAt, 1e-4));
        if (now >= fadeFull)
            return 1.0f;
        const double span = std::max(fadeFull - fadeT0, 1e-4);
        return fadeFrom + (1.0f - fadeFrom) * static_cast<float>((now - fadeT0) / span);
    }

    void CameraEffects::Fade(float in, float wait, float out, glm::vec3 rgb) {
        std::lock_guard lock(mutex);
        const double now = Now();

        in = std::clamp(in, 0.0f, 10.0f);
        wait = std::clamp(wait, 0.0f, 10.0f);
        out = std::clamp(out, 0.0f, 10.0f);
        if (in + wait + out < 0.5f)
            wait = 0.5f - in - out;
        if (rgb.x > 1.0f || rgb.y > 1.0f || rgb.z > 1.0f)
            rgb /= 255.0f;
        const float current = FadeAt(now);
        if (fadeActive && now < fadeEnd) {

            const double remainingIn = std::max(0.0, fadeFull - now);
            const double newFull = now + std::min(remainingIn > 0.0 ? remainingIn : 0.0, static_cast<double>(in));
            const double newOutAt = std::max(fadeOutAt, now + in + wait);
            const double newEnd = std::max(fadeEnd, now + in + wait + out);
            fadeFrom = current;
            fadeT0 = now;
            fadeFull = current >= 1.0f ? now : (remainingIn > 0.0 ? newFull : now + in * (1.0f - current));
            fadeOutAt = std::max(newOutAt, fadeFull);
            fadeEnd = std::max(newEnd, fadeOutAt);
            return;
        }
        fadeColor = rgb;
        fadeFrom = 0.0f;
        fadeT0 = now;
        fadeFull = now + in;
        fadeOutAt = fadeFull + wait;
        fadeEnd = fadeOutAt + out;
        fadeActive = true;
    }

    void CameraEffects::SetFov(float fov, float easeTime, int easeType, bool clear) {
        std::lock_guard lock(mutex);
        fovFromSet = false;
        fovStart = Now();
        fovDuration = std::max(easeTime, 0.0f);
        fovType = easeType;
        fovClearing = clear;
        fovTo = fov;
        fovActive = true;
    }

    void CameraEffects::SetPresets(std::vector<CameraPreset> list) {
        std::lock_guard lock(mutex);
        presets = std::move(list);
    }

    void CameraEffects::SetSplines(std::vector<CameraSpline> list) {
        std::lock_guard lock(mutex);
        splines = std::move(list);
    }

    const CameraPreset* CameraEffects::PresetAt(int index) const {
        std::lock_guard lock(mutex);
        return index >= 0 && static_cast<size_t>(index) < presets.size() ? &presets[static_cast<size_t>(index)]
                                                                           : nullptr;
    }

    int CameraEffects::SplineIndex(const std::string& name) const {
        std::lock_guard lock(mutex);
        for (size_t i = 0; i < splines.size(); i++)
            if (splines[i].name == name)
                return static_cast<int>(i);
        return -1;
    }

    CameraSpline CameraEffects::SplineAt(int index) const {
        std::lock_guard lock(mutex);
        return index >= 0 && static_cast<size_t>(index) < splines.size() ? splines[static_cast<size_t>(index)]
                                                                           : CameraSpline{};
    }

    void CameraEffects::Set(const CameraSet& set) {
        std::lock_guard lock(mutex);
        CameraSet r = set;
        Kind k = Kind::Free;
        float speed = 0.0f, radius = 50.0f;
        bool snap = false, cont = false;
        glm::vec2 hl{0.0f, 360.0f}, vl{0.0f, 180.0f};

        bool haveOrbit = false, haveScheme = false, haveAudio = false;
        bool havePos = false, haveRot = false, haveOff = false, haveEnt = false, haveSpeed = false,
             haveSnap = false, haveCont = false, haveH = false, haveV = false, haveRadius = false;
        std::string name;
        if (set.presetIndex >= 0 && static_cast<size_t>(set.presetIndex) < presets.size()) {
            const CameraPreset* cur = &presets[static_cast<size_t>(set.presetIndex)];
            name = cur->name;
            for (int guard = 0; cur && guard < 16; guard++) {
                if (cur->name == "minecraft:first_person")
                    k = Kind::FirstPerson;
                else if (cur->name == "minecraft:third_person")
                    k = Kind::ThirdPerson;
                else if (cur->name == "minecraft:third_person_front")
                    k = Kind::ThirdPersonFront;
                if (!havePos && cur->position)
                    r.position = set.useDefault || !set.position ? cur->position : set.position, havePos = true;
                if (!haveRot && cur->rotation)
                    r.rotation = set.useDefault || !set.rotation ? cur->rotation : set.rotation, haveRot = true;
                if (!haveOff && cur->offset)
                    r.offset = set.useDefault || !set.offset ? cur->offset : set.offset, haveOff = true;
                if (!haveEnt && cur->entityOffset)
                    r.entityOffset = set.useDefault || !set.entityOffset ? cur->entityOffset : set.entityOffset,
                    haveEnt = true;
                if (!haveSpeed && cur->rotationSpeed)
                    speed = *cur->rotationSpeed, haveSpeed = true;
                if (!haveSnap && cur->snapToTarget)
                    snap = *cur->snapToTarget, haveSnap = true;
                if (!haveCont && cur->continueTargeting)
                    cont = *cur->continueTargeting, haveCont = true;
                if (!haveH && cur->horizontalLimit)
                    hl = *cur->horizontalLimit, haveH = true;
                if (!haveV && cur->verticalLimit)
                    vl = *cur->verticalLimit, haveV = true;
                if (!haveScheme && cur->controlScheme)
                    r.controlScheme = *cur->controlScheme, haveScheme = true;
                if (!haveAudio && cur->audioListener)
                    r.audioListener = *cur->audioListener, haveAudio = true;
                if (!haveOrbit && cur->radius && *cur->radius > 0.0f)
                    r.orbitRadius = *cur->radius, haveOrbit = true;

                if (!haveRot && !cur->rotation && cur->startingRotation && cur->applyStartingRotation &&
                    !set.rotation)
                    r.rotation = cur->startingRotation, haveRot = true;
                if (!haveRadius && cur->trackingRadius)
                    radius = *cur->trackingRadius, haveRadius = true;
                const CameraPreset* next = nullptr;
                if (!cur->parent.empty())
                    for (const auto& c : presets)
                        if (c.name == cur->parent) {
                            next = &c;
                            break;
                        }
                cur = next;
            }
        }
        if (set.useDefault) {
            if (!havePos)
                r.position.reset();
            if (!haveRot)
                r.rotation.reset();
        }

        if (r.facing)
            r.rotation.reset();
        const bool wasShown = setActive || splineActive || targetActive;
        resolved = r;
        kind = k;
        rotationSpeed = speed;
        trackingRadius = radius;
        snapToTarget = snap;
        continueTargeting = cont;
        hLimit = hl;
        vLimit = vl;
        setActive = true;
        splineActive = false;

        easePending = true;
        poseValid = wasShown && poseValid;
        poseStart = Now();
        poseDuration = set.hasEase && set.easeDuration > 0.0f ? set.easeDuration : 0.0f;
        poseType = set.easeType;

        if (k != Kind::Free)
            poseDuration = 0.0f;
        (void)name;
    }

    void CameraEffects::StartSpline(const CameraSpline& sp) {
        std::lock_guard lock(mutex);
        spline = sp;
        splineStart = Now();
        splineActive = sp.curve.size() >= 2 && sp.totalTime > 0.0f;
        if (splineActive) {
            setActive = false;
            kind = Kind::Free;
        }
    }

    void CameraEffects::Clear() {
        std::lock_guard lock(mutex);
        setActive = false;
        easePending = false;
        poseValid = false;
        splineActive = false;
        kind = Kind::None;
        targetActive = false;
        targetUid = 0;
        attachedUid = 0;
    }

    void CameraEffects::Target(int64_t entityUid, std::optional<glm::vec3> offset) {
        std::lock_guard lock(mutex);
        targetUid = entityUid;
        targetOffset = offset ? *offset : glm::vec3(0.0f);
        targetActive = true;
        targetFresh = true;
    }

    void CameraEffects::RemoveTarget() {
        std::lock_guard lock(mutex);
        targetActive = false;
        targetUid = 0;
    }

    void CameraEffects::Attach(int64_t entityUid) {
        std::lock_guard lock(mutex);
        attachedUid = entityUid;
    }

    void CameraEffects::Detach() {
        std::lock_guard lock(mutex);
        attachedUid = 0;
    }

    void CameraEffects::SetFog(const std::vector<std::string>& stack) {
        std::lock_guard lock(mutex);
        fogStack = stack;
    }

    std::vector<std::string> CameraEffects::FogStack() const {
        std::lock_guard lock(mutex);
        return fogStack;
    }

    void CameraEffects::Reset() {
        {
            std::lock_guard lock(mutex);
            fadeActive = false;
            fovActive = false;
            fovFromSet = false;
            shakeActive = false;
            shakeMatrix = glm::mat4(1.0f);
            fogStack.clear();
            presets.clear();
            splines.clear();
        }
        Clear();
    }

    glm::vec3 CameraEffects::Sample(const std::vector<glm::vec3>& c, float p, bool smooth) {
        const int n = static_cast<int>(c.size());
        if (n == 0)
            return glm::vec3(0.0f);
        if (n == 1)
            return c[0];
        const float f = std::clamp(p, 0.0f, 1.0f) * static_cast<float>(n - 1);
        const int i = std::min(static_cast<int>(f), n - 2);
        const float u = f - static_cast<float>(i);
        if (!smooth)
            return c[static_cast<size_t>(i)] + (c[static_cast<size_t>(i) + 1] - c[static_cast<size_t>(i)]) * u;
        const glm::vec3 p0 = c[static_cast<size_t>(std::max(i - 1, 0))], p1 = c[static_cast<size_t>(i)],
                        p2 = c[static_cast<size_t>(i) + 1], p3 = c[static_cast<size_t>(std::min(i + 2, n - 1))];
        const float u2 = u * u, u3 = u2 * u;
        return 0.5f * ((2.0f * p1) + (-p0 + p2) * u + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u2 +
                       (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * u3);
    }

    void CameraEffects::Tick(const CameraPose& base, const EntityLookup& lookup) {
        std::lock_guard lock(mutex);
        TickLocked(base, lookup);
    }

    namespace {
        float WrapDeg(float a) {
            while (a > 180.0f)
                a -= 360.0f;
            while (a < -180.0f)
                a += 360.0f;
            return a;
        }

        CameraPose LookAt(const glm::vec3& from, const glm::vec3& to) {
            const glm::vec3 d = to - from;
            CameraPose p;
            p.position = from;
            p.yaw = glm::degrees(std::atan2(d.z, d.x));
            p.pitch = glm::degrees(std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)));
            return p;
        }
    }

    void CameraEffects::TickLocked(const CameraPose& baseIn, const EntityLookup& lookup) {
        const double now = Now();
        const float dt = lastTick > 0.0 ? static_cast<float>(std::min(now - lastTick, 0.25)) : 0.0f;
        lastTick = now;
        shakeMatrix = glm::mat4(1.0f);
        if (shakeActive) {
            const float elapsed = static_cast<float>(now - shakeStart);
            if (elapsed >= shakeDuration)
                shakeActive = false;
            else {
                const float fade =
                    std::min(1.0f, (shakeDuration - elapsed) / std::max(0.25f, shakeDuration * 0.35f));
                const float amp = shakeIntensity * fade;
                const float t = elapsed * 22.0f;
                const float nx = Noise(t), ny = Noise(t + 31.7f), nz = Noise(t + 77.3f);
                if (shakeType == 1) {
                    const float deg = amp * 2.2f;
                    shakeMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(nz * deg), glm::vec3(0, 0, 1)) *
                                  glm::rotate(glm::mat4(1.0f), glm::radians(ny * deg), glm::vec3(1, 0, 0)) *
                                  glm::rotate(glm::mat4(1.0f), glm::radians(nx * deg), glm::vec3(0, 1, 0));
                } else {
                    const float d = amp * 0.12f;
                    shakeMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(nx * d, ny * d, nz * d * 0.5f));
                }
            }
        }
        fadeAlpha = FadeAt(now);
        if (fadeActive && now >= fadeEnd)
            fadeActive = false;

        CameraPose base = baseIn;
        TrackedEntity attached;
        const bool isAttached = attachedUid != 0 && lookup && lookup(attachedUid, attached);
        if (isAttached) {
            base.position = attached.eye;
            base.yaw = attached.yaw;
            base.pitch = attached.pitch;
        }

        const bool wasValid = poseValid;
        bool have = false;
        CameraPose goal = base;
        if (splineActive) {
            const float el = static_cast<float>(now - splineStart);
            const float t = std::clamp(el / spline.totalTime, 0.0f, 1.0f);
            float p = t;
            if (!spline.progress.empty()) {
                const auto& k = spline.progress;
                const float secs = std::min(el, spline.totalTime);
                p = k.back().value;
                if (secs <= k.front().time)
                    p = k.front().value;
                else
                    for (size_t i = 1; i < k.size(); i++)
                        if (secs <= k[i].time) {
                            const float span = std::max(k[i].time - k[i - 1].time, 1e-4f);
                            p = k[i - 1].value +
                                (k[i].value - k[i - 1].value) *
                                    EaseValue(k[i].ease, (secs - k[i - 1].time) / span);
                            break;
                        }
            }
            const bool smooth = spline.type != "linear";
            goal.position = Sample(spline.curve, p, smooth);
            if (!spline.rotation.empty()) {
                const auto& k = spline.rotation;
                const float secs = std::min(el, spline.totalTime);
                glm::vec3 r = k.back().value;
                if (secs <= k.front().time)
                    r = k.front().value;
                else
                    for (size_t i = 1; i < k.size(); i++)
                        if (secs <= k[i].time) {
                            const float span = std::max(k[i].time - k[i - 1].time, 1e-4f);
                            const float e = EaseValue(k[i].ease, (secs - k[i - 1].time) / span);
                            const glm::vec3 a = k[i - 1].value, b = k[i].value;
                            r = {a.x + (b.x - a.x) * e, a.y + WrapDeg(b.y - a.y) * e, 0.0f};
                            break;
                        }
                goal.pitch = -r.x;
                goal.yaw = r.y + 90.0f;
            } else {
                const glm::vec3 ahead = Sample(spline.curve, std::min(p + 0.01f, 1.0f), smooth);
                if (glm::length(ahead - goal.position) > 1e-4f) {
                    const CameraPose l = LookAt(goal.position, ahead);
                    goal.yaw = l.yaw;
                    goal.pitch = l.pitch;
                }
            }
            poseNow = goal;
            poseValid = have = true;
        } else if (setActive && kind == Kind::Free) {
            const CameraSet& r = resolved;
            goal.position = r.position ? *r.position : base.position;
            if (r.entityOffset)
                goal.position += *r.entityOffset;
            if (r.facing) {
                const CameraPose l = LookAt(goal.position, *r.facing);
                goal.yaw = l.yaw;
                goal.pitch = l.pitch;
            } else if (r.rotation) {
                goal.pitch = -r.rotation->x;
                goal.yaw = r.rotation->y + 90.0f;
            }
            if (r.orbitRadius > 0.0f && !r.position) {

                const float yr0 = glm::radians(goal.yaw), pr0 = glm::radians(goal.pitch);
                const glm::vec3 dir(std::cos(yr0) * std::cos(pr0), std::sin(pr0), std::sin(yr0) * std::cos(pr0));
                goal.position -= dir * r.orbitRadius;
            }
            if (r.offset) {
                const float yr = glm::radians(goal.yaw);
                const glm::vec3 right(-std::sin(yr), 0.0f, std::cos(yr));
                goal.position += right * r.offset->x + glm::vec3(0.0f, r.offset->y, 0.0f);
            }
            if (easePending) {
                easePending = false;
                poseFrom = poseValid ? poseNow : base;
                poseStart = now;
            }
            const float t = poseDuration > 0.0f ? static_cast<float>((now - poseStart) / poseDuration) : 1.0f;
            if (t >= 1.0f)
                poseNow = goal;
            else {
                const float e = EaseValue(poseType, t);
                poseNow.position = poseFrom.position + (goal.position - poseFrom.position) * e;
                poseNow.yaw = poseFrom.yaw + WrapDeg(goal.yaw - poseFrom.yaw) * e;
                poseNow.pitch = poseFrom.pitch + (goal.pitch - poseFrom.pitch) * e;
            }
            poseValid = have = true;
        } else if (setActive) {

            poseValid = false;
        } else if (isAttached) {
            poseNow = base;
            poseValid = have = true;
        } else if (!targetActive) {
            poseValid = false;
        }

        if (targetActive) {
            TrackedEntity victim;
            if (!lookup || !lookup(targetUid, victim)) {

                if (!continueTargeting && targetUid != 0 && lookup)
                    targetActive = false;
            } else {
                if (!have) {

                    if (targetFresh || !poseValid) {
                        frozenPos = base.position;
                        poseNow = base;
                    }
                    poseValid = have = true;
                }
                const glm::vec3 aim = victim.center + targetOffset;
                const float dist = glm::length(aim - poseNow.position);
                if (targetFresh) {
                    targetFresh = false;
                    targetStartYaw = poseNow.yaw;
                }
                const CameraPose want = LookAt(poseNow.position, aim);
                const float dyaw = WrapDeg(want.yaw - targetStartYaw);
                const float fromDown = 90.0f - want.pitch;
                const float lo = std::min(vLimit.x, vLimit.y), hi = std::max(vLimit.x, vLimit.y);
                const bool inLimits = dyaw >= -hLimit.x - 0.01f && dyaw <= hLimit.y + 0.01f &&
                                      fromDown >= lo - 0.01f && fromDown <= hi + 0.01f;
                if (dist > 1024.0f) {
                    targetActive = false;
                } else if (dist <= trackingRadius && inLimits) {
                    if (rotationSpeed <= 0.0f || (snapToTarget && dt == 0.0f)) {
                        poseNow.yaw = want.yaw;
                        poseNow.pitch = want.pitch;
                    } else {
                        const float step = rotationSpeed * dt;
                        const float ay = WrapDeg(want.yaw - poseNow.yaw), ap = want.pitch - poseNow.pitch;
                        const float len = std::sqrt(ay * ay + ap * ap);
                        const float k = len > step && len > 0.0f ? step / len : 1.0f;
                        poseNow.yaw += ay * k;
                        poseNow.pitch += ap * k;
                    }
                } else if (!continueTargeting) {
                    targetActive = false;
                }
            }
        }
        if (!have)
            poseValid = false;
        (void)wasValid;
    }

    bool CameraEffects::PoseActive() const {
        std::lock_guard lock(mutex);
        return poseValid;
    }

    CameraPose CameraEffects::Pose() const {
        std::lock_guard lock(mutex);
        return poseNow;
    }

    CameraEffects::Kind CameraEffects::PresetKind() const {
        std::lock_guard lock(mutex);
        return setActive && !splineActive ? kind : Kind::None;
    }

    int CameraEffects::ControlScheme() const {
        std::lock_guard lock(mutex);
        return setActive && poseValid && kind == Kind::Free ? resolved.controlScheme : -1;
    }

    bool CameraEffects::AudioAtPlayer() const {
        std::lock_guard lock(mutex);
        return setActive && resolved.audioListener == 1;
    }

    glm::mat4 CameraEffects::ShakeMatrix() const {
        std::lock_guard lock(mutex);
        return shakeMatrix;
    }

    float CameraEffects::Fov(float base) const {
        std::lock_guard lock(mutex);
        if (!fovActive && fovShown <= 0.0f)
            return base;
        const double now = Now();
        if (!fovFromSet) {
            fovFrom = fovShown > 1.0f ? fovShown : base;
            fovFromSet = true;
        }
        const float t = fovDuration > 0.0f ? static_cast<float>((now - fovStart) / fovDuration) : 1.0f;
        const float goal = fovClearing ? base : fovTo;
        const float v = fovFrom + (goal - fovFrom) * EaseValue(fovType, t);
        if (t >= 1.0f && fovActive) {
            fovActive = false;
            fovFromSet = false;
            fovShown = fovClearing ? 0.0f : fovTo;
            return fovClearing ? base : fovTo;
        }
        if (!fovActive)
            return fovShown > 1.0f ? fovShown : base;
        fovShown = v;
        return v;
    }

    glm::vec4 CameraEffects::Tint() const {
        std::lock_guard lock(mutex);
        return glm::vec4(fadeColor, std::clamp(fadeAlpha, 0.0f, 1.0f));
    }

    int64_t CameraEffects::TargetUid() const {
        std::lock_guard lock(mutex);
        return targetActive ? targetUid : 0;
    }

    int64_t CameraEffects::AttachedUid() const {
        std::lock_guard lock(mutex);
        return attachedUid;
    }

    bool CameraEffects::Shaking() const {
        std::lock_guard lock(mutex);
        return shakeActive;
    }
}
