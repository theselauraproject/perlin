#pragma once
#include <glm/glm.hpp>

#include <chrono>
#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

namespace perlin {

    struct CameraPose {
        glm::vec3 position{0.0f};
        float yaw = 0.0f;
        float pitch = 0.0f;
        float roll = 0.0f;
    };

    struct CameraAimAssist {
        std::optional<std::string> presetId;
        std::optional<int> targetMode;
        std::optional<glm::vec2> angle;
        std::optional<float> distance;
    };

    struct CameraPreset {
        std::string name, parent;
        std::optional<glm::vec3> position;
        std::optional<glm::vec2> rotation;
        std::array<std::optional<float>, 3> positionComponents{};
        std::array<std::optional<float>, 2> rotationComponents{};
        std::optional<glm::vec2> offset;
        std::optional<glm::vec3> entityOffset;
        std::optional<float> rotationSpeed, trackingRadius, radius;
        std::optional<float> yawLimitMin, yawLimitMax;
        std::optional<bool> snapToTarget, continueTargeting;
        std::optional<glm::vec2> horizontalLimit, verticalLimit;
        std::optional<glm::vec2> startingRotation;
        std::optional<int> controlScheme;
        std::optional<int> audioListener;
        std::optional<bool> playerEffects;
        std::optional<CameraAimAssist> aimAssist;
        bool applyStartingRotation = false;
    };

    struct CameraSet {
        int presetIndex = -1;
        std::optional<glm::vec3> position;
        std::optional<glm::vec2> rotation;
        std::optional<glm::vec3> facing;
        std::optional<glm::vec2> offset;
        std::optional<glm::vec3> entityOffset;
        float orbitRadius = 0.0f;
        int controlScheme = -1;
        int audioListener = 0;
        bool useDefault = false;
        bool removeIgnoreStartingValues = false;
        int easeType = 0;
        float easeDuration = 0.0f;
        bool hasEase = false;
    };

    struct CameraSpline {
        struct Progress {
            float value = 0, time = 0;
            int ease = 0;
        };
        struct Rotation {
            glm::vec3 value{0.0f};
            float time = 0;
            int ease = 0;
        };
        std::string name, type;
        float totalTime = 0;
        std::vector<glm::vec3> curve;
        std::vector<Progress> progress;
        std::vector<Rotation> rotation;
    };

    struct TrackedEntity {
        glm::vec3 center{0.0f};
        glm::vec3 eye{0.0f};
        float yaw = 0.0f, pitch = 0.0f;
    };
    using EntityLookup = std::function<bool(int64_t, TrackedEntity&)>;

    class CameraEffects {
    public:
        enum class Kind { None, Free, FirstPerson, ThirdPerson, ThirdPersonFront, FollowOrbit, FixedBoom };
        using TimeSource = std::function<double()>;

        explicit CameraEffects(TimeSource timeSource = {});

        void Shake(float intensity, float duration, int type);
        void StopShake();
        void Fade(float fadeIn, float wait, float fadeOut, glm::vec3 rgb);
        void SetFov(float fov, float easeTime, int easeType, bool clear);
        void SetPresets(std::vector<CameraPreset> presets);
        void SetSplines(std::vector<CameraSpline> splines);
        void Set(const CameraSet& set);
        void StartSpline(const CameraSpline& spline);
        std::optional<CameraPreset> PresetAt(int index) const;
        std::optional<CameraSpline> FindSpline(const std::string& name) const;
        int SplineIndex(const std::string& name) const;
        CameraSpline SplineAt(int index) const;
        void Clear();
        void Target(int64_t entityUid, std::optional<glm::vec3> offset);
        void RemoveTarget();
        void Attach(int64_t entityUid);
        void Detach();
        void SetControlScheme(std::optional<int> scheme);
        bool TakeControlPitchReset();
        bool ApplyLook(float yawDelta, float pitchDelta);
        void SetFog(const std::vector<std::string>& stack);
        std::vector<std::string> FogStack() const;
        void Reset();

        void Tick(const CameraPose& base, const EntityLookup& lookup,
                  std::optional<CameraPose> renderedBase = {}, std::optional<glm::vec3> playerCenter = {});

        bool PoseActive() const;
        CameraPose Pose() const;
        CameraPose PlayerPose() const;

        Kind PresetKind() const;

        int ControlScheme() const;
        bool AudioAtPlayer() const;
        bool PlayerEffectsEnabled() const;
        glm::mat4 ShakeMatrix() const;
        float Fov(float base) const;
        glm::vec4 Tint() const;
        int64_t TargetUid() const;
        int64_t AttachedUid() const;
        bool Shaking() const;
        bool Fading() const;

    private:
        using Clock = std::chrono::steady_clock;
        double Now() const;
        static float EaseValue(int type, float t);
        static glm::vec3 Sample(const std::vector<glm::vec3>& curve, float p, bool smooth);
        void TickLocked(const CameraPose& base, const EntityLookup& lookup,
                        const std::optional<CameraPose>& renderedBase,
                        const std::optional<glm::vec3>& playerCenter);
        void ClearLocked();
        float FovAt(float base, double now) const;
        int ControlSchemeLocked() const;

        mutable std::mutex mutex;
        TimeSource timeSource;
        float shakeIntensity = 0, shakeDuration = 0;
        double shakeStart = 0;
        int shakeType = 0;
        bool shakeActive = false;

        bool fadeActive = false;
        struct FadeKey { float time, alpha; };
        std::vector<FadeKey> fadeKeys;
        double fadeT0 = 0, fadeEnd = 0;
        glm::vec3 fadeColor{0.0f};
        float FadeAt(double now) const;
        float FadeValue(float time) const;
        void MergeFadeKey(float time, float alpha);

        mutable bool fovActive = false, fovClearing = false, fovFromSet = false;
        mutable float fovFrom = 0, fovTo = 0, fovShown = 0;
        float fovDuration = 0;
        int fovType = 0;
        double fovStart = 0;
        mutable float lastBaseFov = 70.0f;

        std::vector<CameraPreset> presets;
        std::vector<CameraSpline> splines;
        struct PresetOverrides {
            std::optional<glm::vec3> position;
            std::optional<glm::vec2> rotation;
            std::optional<glm::vec2> offset;
            std::optional<glm::vec3> entityOffset;
        };
        std::unordered_map<std::string, PresetOverrides> presetOverrides;

        bool setActive = false, easePending = false;
        Kind kind = Kind::None;
        CameraSet resolved;
        std::string activePresetName;
        float rotationSpeed = 0, trackingRadius = 50;
        bool snapToTarget = false, continueTargeting = false;
        bool playerEffects = true;
        std::optional<int> controlOverride;
        bool controlPitchResetPending = false;
        std::optional<float> yawLimitMin, yawLimitMax;
        bool orbitRotationPending = false;
        CameraPose previousBase;
        bool previousBaseValid = false;
        glm::vec2 orbitRotation{0.0f};
        glm::vec2 hLimit{0.0f, 360.0f}, vLimit{0.0f, 180.0f};
        double poseStart = 0;
        float poseDuration = 0;
        int poseType = 0;
        CameraPose poseFrom, poseNow;
        bool poseValid = false;

        bool splineActive = false;
        CameraSpline spline;
        double splineStart = 0;

        int64_t targetUid = 0;
        glm::vec3 targetOffset{0.0f};
        bool targetActive = false, targetFresh = false;
        glm::vec3 frozenPos{0.0f};
        float targetStartYaw = 0;
        float targetStartPitch = 0;
        bool targetOrientationValid = false;
        double lastTick = 0;

        int64_t attachedUid = 0;
        std::vector<std::string> fogStack;
        glm::mat4 shakeMatrix{1.0f};
    };
}
