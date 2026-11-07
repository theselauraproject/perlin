#pragma once
#include <glm/glm.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace perlin {
    // Camera convention throughout (same as Camera): yaw = atan2(z, x) in degrees, pitch positive looking up.
    // Bedrock's own convention (yaw 0 = +z, pitch positive down) is converted where the packets are read.
    struct CameraPose {
        glm::vec3 position{0.0f};
        float yaw = 0.0f;
        float pitch = 0.0f;
    };

    // a camera preset as the server defines it (CameraPresetsPacket); built-ins arrive in the same list
    struct CameraPreset {
        std::string name, parent;
        std::optional<glm::vec3> position;
        std::optional<glm::vec2> rotation; // bedrock: x pitch (down +), y yaw
        std::optional<glm::vec2> offset;   // view offset: x right, y up, in camera space
        std::optional<glm::vec3> entityOffset;
        std::optional<float> rotationSpeed, trackingRadius, radius; // radius: orbit distance behind the player
        std::optional<bool> snapToTarget, continueTargeting;
        std::optional<glm::vec2> horizontalLimit, verticalLimit;
        std::optional<glm::vec2> startingRotation;
        std::optional<int> controlScheme;  // 0 locked_player_relative_strafe .. 4 player_relative_strafe
        std::optional<int> audioListener;  // 0 camera, 1 player
        bool applyStartingRotation = false;
    };

    // one `set` instruction (CameraInstructionPacket::instructionSet)
    struct CameraSet {
        int presetIndex = -1;
        std::optional<glm::vec3> position;
        std::optional<glm::vec2> rotation; // bedrock: x pitch (down +), y yaw
        std::optional<glm::vec3> facing;
        std::optional<glm::vec2> offset;
        std::optional<glm::vec3> entityOffset;
        float orbitRadius = 0.0f;
        int controlScheme = -1;
        int audioListener = 0;
        bool useDefault = false;
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
            glm::vec3 value{0.0f}; // bedrock pitch, yaw, roll
            float time = 0;
            int ease = 0;
        };
        std::string name, type;
        float totalTime = 0;
        std::vector<glm::vec3> curve;
        std::vector<Progress> progress;
        std::vector<Rotation> rotation;
    };

    // what the frame knows about an entity the camera follows or tracks
    struct TrackedEntity {
        glm::vec3 center{0.0f};
        glm::vec3 eye{0.0f};
        float yaw = 0.0f, pitch = 0.0f; // camera convention
    };
    using EntityLookup = std::function<bool(int64_t, TrackedEntity&)>;

    class CameraEffects {
    public:
        enum class Kind { None, Free, FirstPerson, ThirdPerson, ThirdPersonFront };

        void Shake(float intensity, float duration, int type);
        void StopShake();
        void Fade(float fadeIn, float wait, float fadeOut, glm::vec3 rgb);
        void SetFov(float fov, float easeTime, int easeType, bool clear);
        void SetPresets(std::vector<CameraPreset> presets);
        void SetSplines(std::vector<CameraSpline> splines);
        void Set(const CameraSet& set);
        void StartSpline(const CameraSpline& spline);
        const CameraPreset* PresetAt(int index) const;
        int SplineIndex(const std::string& name) const;
        CameraSpline SplineAt(int index) const;
        void Clear();
        void Target(int64_t entityUid, std::optional<glm::vec3> offset);
        void RemoveTarget();
        void Attach(int64_t entityUid);
        void Detach();
        void SetFog(const std::vector<std::string>& stack);
        std::vector<std::string> FogStack() const;
        void Reset();

        // base: the player's own view this frame
        void Tick(const CameraPose& base, const EntityLookup& lookup);

        bool PoseActive() const;
        CameraPose Pose() const;
        // first_person / third_person / third_person_front presets pick the perspective instead of a pose
        Kind PresetKind() const;
        // movement scheme of the running preset (-1: none) and the yaw movement is relative to
        int ControlScheme() const;
        bool AudioAtPlayer() const;
        glm::mat4 ShakeMatrix() const;
        float Fov(float base) const;
        glm::vec4 Tint() const;
        int64_t TargetUid() const;
        int64_t AttachedUid() const;
        bool Shaking() const;

    private:
        using Clock = std::chrono::steady_clock;
        static double Now();
        static float EaseValue(int type, float t);
        static glm::vec3 Sample(const std::vector<glm::vec3>& curve, float p, bool smooth);
        void TickLocked(const CameraPose& base, const EntityLookup& lookup);

        mutable std::mutex mutex;
        float shakeIntensity = 0, shakeDuration = 0;
        double shakeStart = 0;
        int shakeType = 0;
        bool shakeActive = false;

        // fade: opacity rises from fadeFrom at fadeT0 to 1 at fadeFull, holds, then falls to 0 between
        // fadeOutAt and fadeEnd; a fade issued during another merges into this timeline
        bool fadeActive = false;
        float fadeFrom = 0, fadeAlpha = 0;
        double fadeT0 = 0, fadeFull = 0, fadeOutAt = 0, fadeEnd = 0;
        glm::vec3 fadeColor{0.0f};
        float FadeAt(double now) const;

        mutable bool fovActive = false, fovClearing = false, fovFromSet = false;
        mutable float fovFrom = 0, fovTo = 0, fovShown = 0;
        float fovDuration = 0;
        int fovType = 0;
        double fovStart = 0;

        std::vector<CameraPreset> presets;
        std::vector<CameraSpline> splines;

        // the active set instruction, resolved against its preset chain
        bool setActive = false, easePending = false;
        Kind kind = Kind::None;
        CameraSet resolved;
        float rotationSpeed = 0, trackingRadius = 50;
        bool snapToTarget = false, continueTargeting = false;
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
        double lastTick = 0;

        int64_t attachedUid = 0;
        std::vector<std::string> fogStack;
        glm::mat4 shakeMatrix{1.0f};
    };
}
