#include "client/systems/camera/CameraEffects.hpp"
#include "util/Angles.hpp"
#include "world/movement/PerlinSineTable.hpp"

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

    CameraEffects::CameraEffects(TimeSource source) : timeSource(std::move(source)) {}

    double CameraEffects::Now() const {
        if (timeSource)
            return timeSource();
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
        {
            const float angle = kPi * t * (0.2f + 2.5f * t * t * t);
            const float sine = physics::bds::CurrentSineEntry(physics::bds::SineIndex(angle * 10430.378f));
            return (sine * std::pow(1.0f - t, 2.2f) + t) * (1.0f + 1.2f * (1.0f - t));
        }
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

    float CameraEffects::FadeValue(float time) const {
        if (fadeKeys.empty())
            return 0.0f;
        for (const auto& key : fadeKeys)
            if (std::abs(key.time - time) <= 0.0001f)
                return key.alpha;
        if (time <= fadeKeys.front().time) {
            const auto& first = fadeKeys.front();
            return first.time > 0.0f ? first.alpha * std::max(time, 0.0f) / first.time : first.alpha;
        }
        for (size_t i = 1; i < fadeKeys.size(); ++i) {
            const auto& a = fadeKeys[i - 1];
            const auto& b = fadeKeys[i];
            if (time <= b.time) {
                const float span = b.time - a.time;
                return span > 0.0f ? a.alpha + (b.alpha - a.alpha) * ((time - a.time) / span) : b.alpha;
            }
        }
        return fadeKeys.back().alpha;
    }

    float CameraEffects::FadeAt(double now) const {
        return fadeActive && now < fadeEnd ? FadeValue(static_cast<float>(now - fadeT0)) : 0.0f;
    }

    void CameraEffects::MergeFadeKey(float time, float alpha) {
        constexpr float epsilon = 0.0001f;
        if (!fadeKeys.empty() && FadeValue(time) + epsilon >= alpha && fadeKeys.back().time + epsilon >= time)
            return;
        auto at = fadeKeys.begin();
        for (; at != fadeKeys.end(); ++at) {
            if (at->time > time)
                break;
            if (at->time + epsilon >= time) {
                at->alpha = alpha;
                break;
            }
        }
        if (at == fadeKeys.end() || at->time > time)
            fadeKeys.insert(at, FadeKey{time, alpha});
        for (size_t i = 1; i + 1 < fadeKeys.size();) {
            if (fadeKeys[i - 1].alpha >= fadeKeys[i].alpha && fadeKeys[i + 1].alpha >= fadeKeys[i].alpha)
                fadeKeys.erase(fadeKeys.begin() + static_cast<std::ptrdiff_t>(i));
            else
                ++i;
        }
    }

    void CameraEffects::Fade(float in, float wait, float out, glm::vec3 rgb) {
        std::lock_guard lock(mutex);
        const double now = Now();
        const float current = FadeAt(now);
        if (!fadeActive || now >= fadeEnd) {
            fadeKeys.clear();
            fadeColor = rgb;
        } else {
            const float elapsed = static_cast<float>(now - fadeT0);
            for (auto& key : fadeKeys)
                key.time -= elapsed;
            std::erase_if(fadeKeys, [](const FadeKey& key) { return key.time < 0.0f; });
        }
        if (!fadeKeys.empty())
            fadeKeys.insert(fadeKeys.begin(), FadeKey{0.0f, current});
        in = std::clamp(in, 0.0f, 10.0f);
        wait = std::clamp(wait, 0.0f, 10.0f);
        out = std::clamp(out, 0.0f, 10.0f);
        wait = std::max(wait, 0.5f - in - out);
        MergeFadeKey(in, 1.0f);
        MergeFadeKey(in + wait, 1.0f);
        MergeFadeKey(in + wait + out, 0.0f);
        fadeT0 = now;
        fadeEnd = now + fadeKeys.back().time;
        fadeActive = true;
    }

    void CameraEffects::SetFov(float fov, float easeTime, int easeType, bool clear) {
        std::lock_guard lock(mutex);
        const double now = Now();
        const bool hadFov = fovActive || fovShown > 0.0f;
        const float current = FovAt(lastBaseFov, now);
        fovFrom = current;
        fovFromSet = hadFov;
        fovStart = now;
        fovDuration = std::max(easeTime, 0.0f);
        fovType = easeType;
        fovClearing = clear;
        fovTo = std::clamp(fov, 30.0f, 110.0f);
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

    std::optional<CameraPreset> CameraEffects::PresetAt(int index) const {
        std::lock_guard lock(mutex);
        if (index >= 0 && static_cast<size_t>(index) < presets.size())
            return presets[static_cast<size_t>(index)];
        return std::nullopt;
    }

    std::optional<CameraSpline> CameraEffects::FindSpline(const std::string& name) const {
        std::lock_guard lock(mutex);
        for (const auto& candidate : splines)
            if (candidate.name == name)
                return candidate;
        return std::nullopt;
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
        if (set.presetIndex < 0 || static_cast<size_t>(set.presetIndex) >= presets.size())
            return;

        const auto builtinKind = [](const std::string& name) {
            if (name == "minecraft:first_person") return Kind::FirstPerson;
            if (name == "minecraft:third_person") return Kind::ThirdPerson;
            if (name == "minecraft:third_person_front") return Kind::ThirdPersonFront;
            if (name == "minecraft:follow_orbit") return Kind::FollowOrbit;
            if (name == "minecraft:fixed_boom" || name == "minecraft:third_person_boom") return Kind::FixedBoom;
            if (name == "minecraft:free") return Kind::Free;
            return Kind::None;
        };
        CameraSet r = set;
        Kind k = Kind::None;
        std::array<std::optional<float>, 3> pos{};
        std::array<std::optional<float>, 2> rot{};
        std::optional<glm::vec2> off, starting;
        std::optional<glm::vec3> ent;
        std::optional<float> speed, radius, orbit, yawMin, yawMax;
        std::optional<bool> snap, cont, effects;
        std::optional<glm::vec2> horizontal, vertical;
        std::optional<int> scheme, listener;
        bool applyStarting = false;
        std::vector<bool> seen(presets.size(), false);
        size_t index = static_cast<size_t>(set.presetIndex);
        const std::string name = presets[index].name;
        const bool applyInheritedStarting = presets[index].applyStartingRotation;
        for (;;) {
            if (seen[index])
                return; // A cyclic inheritance chain is not a usable preset.
            seen[index] = true;
            const CameraPreset& cur = presets[index];
            if (k == Kind::None)
                k = builtinKind(cur.name);
            for (size_t i = 0; i < pos.size(); ++i)
                if (!pos[i]) {
                    if (cur.positionComponents[i]) pos[i] = cur.positionComponents[i];
                    else if (cur.position) pos[i] = (*cur.position)[static_cast<int>(i)];
                }
            for (size_t i = 0; i < rot.size(); ++i)
                if (!rot[i]) {
                    if (cur.rotationComponents[i]) rot[i] = cur.rotationComponents[i];
                    else if (cur.rotation) rot[i] = (*cur.rotation)[static_cast<int>(i)];
                }
            if (!off && cur.offset) off = cur.offset;
            if (!ent && cur.entityOffset) ent = cur.entityOffset;
            if (!speed && cur.rotationSpeed) speed = cur.rotationSpeed;
            if (!radius && cur.trackingRadius) radius = cur.trackingRadius;
            if (!orbit && cur.radius) orbit = cur.radius;
            if (!snap && cur.snapToTarget) snap = cur.snapToTarget;
            if (!cont && cur.continueTargeting) cont = cur.continueTargeting;
            if (!horizontal && cur.horizontalLimit) horizontal = cur.horizontalLimit;
            if (!vertical && cur.verticalLimit) vertical = cur.verticalLimit;
            if (!scheme && cur.controlScheme) scheme = cur.controlScheme;
            if (!listener && cur.audioListener) listener = cur.audioListener;
            if (!effects && cur.playerEffects) effects = cur.playerEffects;
            if (!yawMin && cur.yawLimitMin) yawMin = cur.yawLimitMin;
            if (!yawMax && cur.yawLimitMax) yawMax = cur.yawLimitMax;
            if (!starting && cur.startingRotation) {
                starting = cur.startingRotation;
                applyStarting = applyInheritedStarting || index == static_cast<size_t>(set.presetIndex);
            }
            if (cur.parent.empty())
                break;
            size_t parent = 0;
            for (; parent < presets.size(); ++parent)
                if (presets[parent].name == cur.parent)
                    break;
            if (parent == presets.size()) {
                if (k == Kind::None)
                    k = builtinKind(cur.parent);
                break;
            }
            index = parent;
        }
        if (k == Kind::None)
            k = Kind::Free;
        const bool orbitCamera = k == Kind::FollowOrbit || k == Kind::FixedBoom;
        auto& saved = presetOverrides[name];
        if (set.position) saved.position = set.position;
        if (set.rotation) saved.rotation = glm::vec2(std::clamp(set.rotation->x, -90.0f, 90.0f), set.rotation->y);
        if (set.offset) saved.offset = glm::clamp(*set.offset, glm::vec2(-64.0f), glm::vec2(64.0f));
        if (set.entityOffset) saved.entityOffset = glm::clamp(*set.entityOffset, glm::vec3(-64.0f), glm::vec3(64.0f));
        if (set.useDefault) {
            saved.position.reset();
            saved.rotation.reset();
            if (set.removeIgnoreStartingValues) {
                saved.offset.reset();
                saved.entityOffset.reset();
            }
        }

        r.position = saved.position;
        if (!r.position && !orbitCamera)
            r.position = glm::vec3(pos[0].value_or(0.0f), pos[1].value_or(0.0f), pos[2].value_or(0.0f));
        r.rotation = saved.rotation;
        if (!r.rotation && (rot[0] || rot[1] || !orbitCamera))
            r.rotation = glm::vec2(rot[0].value_or(0.0f), rot[1].value_or(0.0f));
        if (!r.rotation && starting && applyStarting)
            r.rotation = starting;
        r.offset = saved.offset ? saved.offset : off;
        r.entityOffset = saved.entityOffset ? saved.entityOffset : ent;
        r.orbitRadius = orbitCamera ? orbit.value_or(10.0f) : 0.0f;
        r.controlScheme = scheme.value_or(0);
        r.audioListener = listener.value_or(0);
        if (set.useDefault)
            r.facing.reset();
        if (r.facing && r.position) {
            const glm::vec3 d = *r.facing - *r.position;
            const float pitch = -glm::degrees(std::atan2(d.y, std::hypot(d.x, d.z)));
            const float yaw = glm::degrees(std::atan2(d.z, d.x)) - 90.0f;
            saved.rotation = r.rotation = glm::vec2(pitch, yaw);
            r.facing.reset();
        }
        const bool wasShown = setActive || splineActive || targetActive;
        const bool samePreset = setActive && resolved.presetIndex == set.presetIndex &&
                                kind == k && activePresetName == name;
        orbitRotationPending = orbitCamera && (!samePreset || (set.rotation && !set.useDefault) ||
                                               (set.useDefault && set.removeIgnoreStartingValues));
        resolved = r;
        activePresetName = name;
        kind = k;
        rotationSpeed = speed.value_or(0.0f);
        trackingRadius = radius.value_or(50.0f);
        snapToTarget = snap.value_or(false);
        continueTargeting = cont.value_or(false);
        hLimit = horizontal.value_or(glm::vec2(0.0f, 360.0f));
        vLimit = vertical.value_or(glm::vec2(0.0f, 180.0f));
        yawLimitMin = yawMin;
        yawLimitMax = yawMax;
        playerEffects = effects.value_or(k != Kind::Free);
        setActive = true;
        splineActive = false;
        targetOrientationValid = false;
        targetFresh = targetActive;
        easePending = true;
        poseValid = wasShown && poseValid;
        poseStart = Now();
        poseDuration = set.hasEase && set.easeDuration > 0.0f ? set.easeDuration : 0.0f;
        poseType = set.easeType;
        if (k == Kind::FirstPerson || k == Kind::ThirdPerson || k == Kind::ThirdPersonFront)
            poseDuration = 0.0f;
    }

    void CameraEffects::StartSpline(const CameraSpline& sp) {
        std::lock_guard lock(mutex);
        if (sp.curve.size() < 2 || !(sp.totalTime > 0.0f))
            return;
        spline = sp;
        splineStart = Now();
        splineActive = sp.curve.size() >= 2 && sp.totalTime > 0.0f;
        if (splineActive) {
            setActive = false;
            kind = Kind::Free;
            playerEffects = false;
            easePending = false;
        }
    }

    void CameraEffects::Clear() {
        std::lock_guard lock(mutex);
        ClearLocked();
    }

    void CameraEffects::ClearLocked() {
        setActive = false;
        easePending = false;
        poseValid = false;
        splineActive = false;
        kind = Kind::None;
        targetActive = false;
        targetUid = 0;
        attachedUid = 0;
        targetFresh = false;
        targetOrientationValid = false;
        previousBaseValid = false;
        orbitRotationPending = false;
        playerEffects = true;
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
        targetFresh = false;
    }

    void CameraEffects::Attach(int64_t entityUid) {
        std::lock_guard lock(mutex);
        attachedUid = entityUid;
    }

    void CameraEffects::Detach() {
        std::lock_guard lock(mutex);
        attachedUid = 0;
    }

    void CameraEffects::SetControlScheme(std::optional<int> scheme) {
        std::lock_guard lock(mutex);
        controlOverride = scheme;
        if (scheme == 2 || scheme == 4)
            controlPitchResetPending = true;
    }

    bool CameraEffects::TakeControlPitchReset() {
        std::lock_guard lock(mutex);
        const bool pending = controlPitchResetPending;
        controlPitchResetPending = false;
        return pending;
    }

    bool CameraEffects::ApplyLook(float yawDelta, float pitchDelta) {
        std::lock_guard lock(mutex);
        if (!setActive)
            return false;
        if (kind == Kind::FollowOrbit && !orbitRotationPending) {
            orbitRotation.x = std::clamp(orbitRotation.x + pitchDelta, -90.0f, 90.0f);
            orbitRotation.y += yawDelta;
        }
        const int scheme = ControlSchemeLocked();
        return scheme == 1 || scheme == 3;
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
        std::lock_guard lock(mutex);
        ClearLocked();
        fadeActive = false;
        fadeKeys.clear();
        fadeColor = glm::vec3(0.0f);
        fovActive = fovClearing = fovFromSet = false;
        fovShown = fovFrom = fovTo = 0.0f;
        lastBaseFov = 70.0f;
        shakeActive = false;
        shakeMatrix = glm::mat4(1.0f);
        lastTick = 0.0;
        controlOverride.reset();
        controlPitchResetPending = false;
        fogStack.clear();
        presets.clear();
        presetOverrides.clear();
        splines.clear();
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

    void CameraEffects::Tick(const CameraPose& base, const EntityLookup& lookup,
                             std::optional<CameraPose> renderedBase, std::optional<glm::vec3> playerCenter) {
        std::lock_guard lock(mutex);
        TickLocked(base, lookup, renderedBase, playerCenter);
    }

    namespace {
        CameraPose LookAt(const glm::vec3& from, const glm::vec3& to) {
            const glm::vec3 d = to - from;
            CameraPose p;
            p.position = from;
            p.yaw = glm::degrees(std::atan2(d.z, d.x));
            p.pitch = glm::degrees(std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)));
            return p;
        }
    }

    void CameraEffects::TickLocked(const CameraPose& baseIn, const EntityLookup& lookup,
                                   const std::optional<CameraPose>& renderedBase,
                                   const std::optional<glm::vec3>& playerCenter) {
        const double now = Now();
        const float dt = previousBaseValid ? static_cast<float>(std::clamp(now - lastTick, 0.0, 0.25)) : 0.0f;
        lastTick = now;
        previousBase = baseIn;
        previousBaseValid = true;
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
        if (fadeActive && now >= fadeEnd)
            fadeActive = false;

        CameraPose base = baseIn;
        TrackedEntity attached;
        const bool boom = setActive && (kind == Kind::FollowOrbit || kind == Kind::FixedBoom);
        const bool isAttached = boom && attachedUid != 0 && lookup && lookup(attachedUid, attached);
        if (isAttached) {
            base.position = attached.eye;
            base.yaw = attached.yaw;
            base.pitch = attached.pitch;
        }

        const CameraPose previousPose = poseNow;
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
                                    EaseValue(k[i - 1].ease, (secs - k[i - 1].time) / span);
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
                            const float e = EaseValue(k[i - 1].ease, (secs - k[i - 1].time) / span);
                            const glm::vec3 a = k[i - 1].value, b = k[i].value;
                            r = a + (b - a) * e;
                            break;
                        }
                goal.pitch = -r.x;
                goal.yaw = r.y + 90.0f;
                goal.roll = r.z;
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
        } else if (setActive && (kind == Kind::Free || kind == Kind::FollowOrbit || kind == Kind::FixedBoom)) {
            const CameraSet& r = resolved;
            const bool orbit = kind == Kind::FollowOrbit || kind == Kind::FixedBoom;
            if (orbit) {
                if (orbitRotationPending) {
                    orbitRotation = r.rotation ? glm::vec2(-r.rotation->x, r.rotation->y + 90.0f)
                                               : glm::vec2(base.pitch, base.yaw);
                    orbitRotationPending = false;
                }
                if (yawLimitMin && yawLimitMax) {
                    const float yaw = math::WrapDegrees(orbitRotation.y - 90.0f);
                    orbitRotation.y = std::clamp(yaw, std::min(*yawLimitMin, *yawLimitMax),
                                                 std::max(*yawLimitMin, *yawLimitMax)) + 90.0f;
                }
                goal.pitch = orbitRotation.x;
                goal.yaw = orbitRotation.y;
                goal.position = isAttached ? attached.center : playerCenter.value_or(base.position);
                if (r.entityOffset)
                    goal.position += *r.entityOffset;
                const float yr = glm::radians(goal.yaw), pr = glm::radians(goal.pitch);
                const glm::vec3 dir(std::cos(yr) * std::cos(pr), std::sin(pr), std::sin(yr) * std::cos(pr));
                goal.position -= dir * r.orbitRadius;
            } else {
                goal.position = isAttached ? base.position : r.position.value_or(glm::vec3(0.0f));
                if (r.facing) {
                    const CameraPose l = LookAt(goal.position, *r.facing);
                    goal.yaw = l.yaw;
                    goal.pitch = l.pitch;
                } else if (r.rotation && !isAttached) {
                    goal.pitch = -r.rotation->x;
                    goal.yaw = r.rotation->y + 90.0f;
                }
            }
            if (orbit && r.offset) {
                const float yr = glm::radians(goal.yaw), pr = glm::radians(goal.pitch);
                const glm::vec3 right(-std::sin(yr), 0.0f, std::cos(yr));
                const glm::vec3 up(-std::cos(yr) * std::sin(pr), std::cos(pr), -std::sin(yr) * std::sin(pr));
                goal.position += right * r.offset->x + up * r.offset->y;
            }
            if (easePending) {
                easePending = false;
                poseFrom = poseValid ? poseNow : renderedBase.value_or(base);
            }
            const float t = poseDuration > 0.0f ? static_cast<float>((now - poseStart) / poseDuration) : 1.0f;
            if (t >= 1.0f)
                poseNow = goal;
            else {
                const float e = EaseValue(poseType, t);
                poseNow.position = poseFrom.position + (goal.position - poseFrom.position) * e;
                poseNow.yaw = poseFrom.yaw + math::WrapDegrees(goal.yaw - poseFrom.yaw) * e;
                poseNow.pitch = poseFrom.pitch + (goal.pitch - poseFrom.pitch) * e;
                poseNow.roll = poseFrom.roll + math::WrapDegrees(goal.roll - poseFrom.roll) * e;
            }
            poseValid = have = true;
        } else if (setActive) {

            poseValid = false;
        } else if (!targetActive) {
            poseValid = false;
        }

        if (targetActive && setActive && kind == Kind::Free) {
            TrackedEntity victim;
            if (lookup && lookup(targetUid, victim)) {
                const glm::vec3 aim = victim.center + targetOffset;
                const float dist = glm::length(aim - poseNow.position);
                const bool first = targetFresh;
                if (!targetOrientationValid) {
                    targetStartYaw = poseNow.yaw;
                    targetStartPitch = poseNow.pitch;
                    targetOrientationValid = true;
                }
                targetFresh = false;
                CameraPose want = LookAt(poseNow.position, aim);
                const float dyaw = math::WrapDegrees(want.yaw - targetStartYaw);
                const float fromDown = 90.0f + want.pitch;
                const float lo = std::min(vLimit.x, vLimit.y), hi = std::max(vLimit.x, vLimit.y);
                const bool fullHorizontal = hLimit.x + hLimit.y >= 360.0f;
                const bool inLimits = (fullHorizontal || (dyaw >= -hLimit.x && dyaw <= hLimit.y)) &&
                                      fromDown >= lo && fromDown <= hi;
                if (dist > 1024.0f) {
                    targetActive = false;
                    targetUid = 0;
                } else {
                    if (wasValid && !first) {
                        poseNow.yaw = previousPose.yaw;
                        poseNow.pitch = previousPose.pitch;
                    }
                    const bool inRange = dist <= trackingRadius && inLimits;
                    if (!inRange) {
                        if (continueTargeting)
                            want = poseNow;
                        else {
                            want.yaw = targetStartYaw;
                            want.pitch = targetStartPitch;
                        }
                    }
                    if (rotationSpeed <= 0.0f || (snapToTarget && first && inRange)) {
                        poseNow.yaw = want.yaw;
                        poseNow.pitch = want.pitch;
                    } else {
                        const float step = rotationSpeed * dt;
                        const float ay = math::WrapDegrees(want.yaw - poseNow.yaw), ap = want.pitch - poseNow.pitch;
                        const float length = std::hypot(ay, ap);
                        const float fraction = length > step && length > 0.0f ? step / length : 1.0f;
                        poseNow.yaw += ay * fraction;
                        poseNow.pitch += ap * fraction;
                    }
                }
            }
        }
        if (!have)
            poseValid = false;
    }

    bool CameraEffects::PoseActive() const {
        std::lock_guard lock(mutex);
        return poseValid;
    }

    CameraPose CameraEffects::Pose() const {
        std::lock_guard lock(mutex);
        return poseNow;
    }

    CameraPose CameraEffects::PlayerPose() const {
        std::lock_guard lock(mutex);
        return previousBase;
    }

    CameraEffects::Kind CameraEffects::PresetKind() const {
        std::lock_guard lock(mutex);
        return setActive && !splineActive ? kind : Kind::None;
    }

    int CameraEffects::ControlScheme() const {
        std::lock_guard lock(mutex);
        return ControlSchemeLocked();
    }

    int CameraEffects::ControlSchemeLocked() const {
        if (!setActive && !splineActive)
            return controlOverride ? 0 : -1;
        int scheme = controlOverride.value_or(resolved.controlScheme);
        if (scheme < 0 || scheme > 4)
            scheme = 0;
        if (kind == Kind::FollowOrbit)
            return scheme == 0 || scheme == 1 || scheme == 3 ? scheme : 0;
        if (kind != Kind::Free && kind != Kind::FixedBoom)
            return 0;
        return scheme;
    }

    bool CameraEffects::AudioAtPlayer() const {
        std::lock_guard lock(mutex);
        return setActive && resolved.audioListener == 1;
    }

    bool CameraEffects::PlayerEffectsEnabled() const {
        std::lock_guard lock(mutex);
        return !splineActive && (!setActive || playerEffects);
    }

    glm::mat4 CameraEffects::ShakeMatrix() const {
        std::lock_guard lock(mutex);
        return shakeMatrix;
    }

    float CameraEffects::Fov(float base) const {
        std::lock_guard lock(mutex);
        lastBaseFov = base;
        return FovAt(base, Now());
    }

    float CameraEffects::FovAt(float base, double now) const {
        if (!fovActive)
            return fovShown > 0.0f ? fovShown : base;
        if (!fovFromSet) {
            fovFrom = fovShown > 0.0f ? fovShown : base;
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
        fovShown = v;
        return v;
    }

    glm::vec4 CameraEffects::Tint() const {
        std::lock_guard lock(mutex);
        return glm::vec4(fadeColor, std::clamp(FadeAt(Now()), 0.0f, 1.0f));
    }

    bool CameraEffects::Fading() const {
        std::lock_guard lock(mutex);
        return fadeActive && Now() < fadeEnd;
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
