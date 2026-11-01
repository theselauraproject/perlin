#pragma once
#include "client/Options.hpp"
#include "client/hand/HandAnim.hpp"
#include "client/attachable/Attachable.hpp"
#include "client/multiplayer/SkinLoader.hpp"
#include "client/renderer/RenderContext.hpp"
#include "client/ui/HudState.hpp"

#include <glm/glm.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin {
    struct WalkBob {
        float walkDist = 0, walkDistO = 0;
        float bob = 0, oBob = 0;
        void Tick(double moved, bool onGround, bool swimming);
    };

    glm::mat4 BobViewMatrix(const WalkBob& bob, float partial, float strength);

    class HandRenderer {
    public:
        void UnloadLevelResources() { ClearItems(); }
        void Initialize(RenderContext& renderer);
        void Shutdown();

        struct TickIn {
            float yaw = 0;
            float pitch = 0;
            ui::HudItem main, off;
            bool survival = true;
            bool sneaking = false;
            bool useHeld = false;
            bool eating = false;
            int slot = 0;
            // Mob::getCurrentSwingDuration ticks, before the swing speed option
            int swingTicks = 6;
        };
        static bool IsFood(const std::string& name);
        static bool IsThrowable(const std::string& name);
        int EatTicks() const { return eatTicks; }
        bool Scoping() const { return scoping; }
        void RenderScope(int width, int height, float partial);
        void Tick(const TickIn& in, const HandSettings& settings);
        void Swing(const HandSettings& settings);
        void Attack(const HandSettings& settings, bool resetsCooldown);
        float SwingProgress(float partial) const;
        void ForceSwingProgress(float progress) { forcedSwing = progress; }

        struct FrameIn {
            float partial = 0;
            float yaw = 0, pitch = 0;
            glm::vec3 light{1.0f};
            bool slimArm = false;
            bool leftSleeve = true, rightSleeve = true;
            glm::mat4 viewEffect{1.0f};
            const LoadedSkin* skin = nullptr;
            uint64_t skinSerial = 0;
            bool skins3d = false;
            float hurt = 0.0f;
        };
        void Render(const FrameIn& in, const HandSettings& settings);

        void BeginWorldItems();
        void DrawWorldItem(const ui::HudItem& item, const glm::dvec3& feet, float age, uint64_t seed,
                           const glm::vec3& light);

        void DrawHeldItem(const ui::HudItem& item, const glm::mat4& arm, bool left, const glm::vec3& light,
                          float bowUseTicks = -1.0f);
        void SetWorldShadowOnly(bool on) { worldShadowOnly = on; }
        void SetDollClip(const int* clip) { dollClip = clip; }

        bool ThirdPersonAttachable(const std::string& item, bool left, bool firstPerson, bool sneaking,
                                   bool using_, float usedTicks, bool blocking, double life,
                                   const std::string& mainItem, const std::string& offItem,
                                   attachable::Frame& out);

        struct RigState {
            float attackTime = 0, armHeight = 1, offArmHeight = 1;
            std::string main, off;
            bool using_ = false, blocking = false;
            float usedTicks = 0;
        };
        RigState Rig(float partial) const;
        bool RigItem(const std::string& name);

        bool ArmorAttachable(const std::string& identifier, std::string& geometry, std::string& texture);

        static glm::mat4 Projection(float fovDegrees, float aspect, float farPlane);

    private:
        using Quad = attachable::Quad;
        struct Model {
            std::vector<Quad> quads;
            uint32_t texture = 0;
            bool block = false;
            bool valid = false;
        };
        struct ArmModel {
            std::vector<Quad> base, sleeve;
        };

        bool DrawGeometryItem(const ui::HudItem& item, const glm::mat4& base, bool leftHand, bool firstPerson,
                              const glm::vec3& light);
        void DrawModel(const std::vector<Quad>& quads, uint32_t texture, const glm::mat4& model,
                       const glm::vec3& light, float alphaCutoff, bool glint = false, float redTint = 0.0f);
        void EnsureSleeves3d(const FrameIn& in);
        std::vector<Quad> sleeve3d[2];
        uint64_t sleeve3dSerial = ~0ull;
        bool sleeve3dFlags = false, sleeve3dValid = false, sleeve3dFallback = true;
        const Model& ItemModel(const ui::HudItem& item);
        void EnsureSkin(const FrameIn& in);
        void BuildArms(bool slim);
        void RenderArm(const FrameIn& in, const HandSettings& s, bool rightArm, float swing, float equip);
        void RenderItem(const FrameIn& in, const HandSettings& s, const ui::HudItem& item, bool rightArm,
                        float swing, float equip);
        bool RenderAttachable(const FrameIn& in, const HandSettings& s, const ui::HudItem& item,
                              bool rightArm, const glm::mat4& hand);
        uint32_t AttachableTexture(const std::string& path);
        void RenderShield(const FrameIn& in, const HandSettings& s, const ui::HudItem& item, bool rightArm,
                          const glm::mat4& hand);
        uint32_t shieldTexture = 0;
        std::vector<Quad> shieldQuads;
        void ClearItems();
        float Unwrap(float yaw);

        RenderContext* renderer = nullptr;

        hand::Swing armSwing;
        bool showSwing = true;
        int attackStrengthTicker = 100;
        float forcedSwing = -1.0f;

        float mainHandHeight = 0, oMainHandHeight = 0, offHandHeight = 0, oOffHandHeight = 0;
        ui::HudItem mainShown, offShown;
        int mainSlot = 0;
        bool firstTick = true;

        float xBob = 0, xBobO = 0, yBob = 0, yBobO = 0;
        float yawUnwrapped = 0, lastYawRaw = 0;
        bool haveYaw = false;
        float xRotNow = 0, yRotNow = 0;

        std::unordered_map<std::string, Model> items;
        uint32_t itemGeneration = 0;
        uint32_t skinTexture = 0;
        uint32_t glintTexture = 0;
        attachable::Attachables attachables;
        std::unordered_map<std::string, uint32_t> attachableTextures;
        std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
        bool useHeld = false;
        int eatTicks = 0;
        bool eatLatched = false;
        int usedTicks = 0;
        bool blocking = false;
        bool scoping = false;
        std::vector<uint32_t> scopeMeshes;
        float scopeScale = 0.5f, oScopeScale = 0.5f;
        uint32_t scopeTexture = 0, spyglassTexture = 0;
        Model spyglassModel;
        const Model& SpyglassModel();
        int glintWidth = 64;
        uint64_t skinSerialUploaded = ~0ull;
        bool skinFallback = true;
        ArmModel armClassic[2], armSlim[2];
        bool armsBuilt = false;
        std::vector<uint32_t> pendingMeshes;
        std::vector<uint32_t> worldMeshes;
        bool worldPass = false;
        bool worldShadowOnly = false;
        const int* dollClip = nullptr;
    };
}
