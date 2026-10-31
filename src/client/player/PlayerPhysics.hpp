#pragma once
#include <cstdint>
#include "client/player/PerlinCollision.hpp"
#include "client/player/PerlinVerticalMotion.hpp"
#include "client/player/PerlinMovementEffects.hpp"
#include "client/player/PerlinRotation.hpp"
#include "client/player/PerlinLanding.hpp"
#include "client/player/PerlinWorldCollision.hpp"
#include "client/player/PerlinBlockOffset.hpp"
#include "client/player/PerlinJump.hpp"
#include "client/player/PerlinScaffolding.hpp"

#include <vector>

namespace perlin::physics {
    constexpr double kTickSeconds = 0.05;
    constexpr double kWidth = 0.6;
    constexpr double kStandHeight = 1.8;
    constexpr double kSneakHeight = 1.49;
    constexpr double kCrawlHeight = 0.6;
    // eye heights from the camera offset: VanillaOffsetSystem 0x142CED370
    constexpr double kCrawlEye = 0.4;
    constexpr double kStandEye = 1.62001f;
    // the sneak drop is 0.35f; older base game versions use 0.125f (Player ctor 0x1402B5670)
    constexpr double kSneakEye = 1.62001f - 0.35f;
    constexpr double kSleepEye = 0.2;
    constexpr double kStepHeight = 0.5625;
    constexpr double kGravity = 0.08;
    constexpr double kAirDrag = 0.98;
    constexpr double kJumpVelocity = 0.42;
    constexpr double kSprintJumpBoost = 0.2;
    constexpr double kDefaultSlipperiness = 0.6;

    struct Box {
        double x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;

        Box Offset(double dx, double dy, double dz) const { return {x0 + dx, y0 + dy, z0 + dz, x1 + dx, y1 + dy, z1 + dz}; }
        Box Expand(double dx, double dy, double dz) const;
        Box Inflate(double d) const { return {x0 - d, y0 - d, z0 - d, x1 + d, y1 + d, z1 + d}; }
        bool Intersects(const Box& o) const;
    };

    struct BlockShape {
        std::vector<Box> boxes;
        std::optional<bds::BlockRandomOffset> randomOffset;
        uint64_t jumpProperties = 0;
        float friction = 0.6f;
        uint8_t surface = 0;
        uint8_t fluid = 0;
        float fluidLevel = 0.0f;
        uint8_t fluidDepth = 255;
        bool climbable = false;
        bool web = false;
        bool powderSnow = false;
        bool berryBush = false;
        bool honey = false;
        float speedFactor = 1.0f;
        bool isAir = true;
        int8_t bubbleColumn = 0;
        int8_t scaffoldingStability = -1;
        // Inside a nether/end portal column: drives the Java-style portal overlay.
        bool portal = false;
        bool Empty() const { return boxes.empty(); }
    };

    class World {
    public:
        virtual ~World() = default;
        virtual BlockShape Query(int x, int y, int z) const = 0;
        virtual bds::ScaffoldingCell ClimberCell(int x, int y, int z) const;
        virtual bool CollisionChunkReady(int x, int z) const { return true; }
        virtual bool CollisionColumnPresent(int x, int z) const { return CollisionChunkReady(x >> 4, z >> 4); }
        virtual bds::TerrainHeightLimits CollisionHeightLimits() const { return {}; }
        virtual bool InsideRegionReady(const bds::CollisionBox& bounds) const;
        // block shapes only: the move query adds entity boxes itself, whole and after the blocks
        virtual BlockShape CollisionQuery(int x, int y, int z) const { return Query(x, y, z); }
        virtual const std::vector<Box>* CollisionEntityBoxes() const { return nullptr; }
    };

    struct Attributes {
        bool currentMovementMath = true;
        float speed = 0.1f;
        float flySpeed = 0.05f;
        float verticalFlySpeed = 1.0f;
        std::optional<float> airDrag;
        std::optional<float> jumpHeight;
        int jumpBoost = 0;
        int levitation = 0;
        bool slowFalling = false;
        bool weaving = false;
        float maxAutoStep = static_cast<float>(kStepHeight);
        int waterWalkSpeedEnchant = 0;
        bool soulSpeed = false;
        std::optional<float> waterDrag;
        std::optional<float> underwaterSpeed = 0.02f;
        // lava_movement attribute; the player registers it at 0.02 (0x142C374F0)
        float lavaSpeed = 0.02f;
        bool dolphin = false;
        // AirSpeedComponent of a non-player mob; unset keeps the player air speed system
        std::optional<float> airSpeed;
    };

    struct Input {
        float forward = 0;
        float strafe = 0;
        bool jump = false;
        bool sneak = false;
        bool sprint = false;
        bool flying = false;
        // double tap this tick: flying is already the new state, the travel type still the old one
        bool flyToggled = false;
        bool flyUp = false;
        bool flyDown = false;
        bool flyUpSlow = false;
        bool flyDownSlow = false;
        bool noClip = false;
        bool creativeFlight = false;
        bool swimming = false;
        bool headInWater = false;
        float lookY = 0;
        bool gliding = false;
        // GlideInputSystem: +0.1 vertical before the move systems
        bool glideLift = false;
        float pitch = 0;
        bool crawl = false;
        bool forceCrouch = false;
        // riptide level of a trident released this tick, 0 when none
        int riptideLevel = 0;
        // mob boxes a riptide spin can hit; filled only while spinning
        std::vector<Box> mobBoxes;
        bool overDescendableBlock = false;
        bool inAscendableBlock = false;
        bool canStandOnPowderSnow = false;
        bool canClimb = true, wallClimbing = false;
        bool pushTowardsClosestSpace = false;
        uint8_t actorMovementFlags = 0;
        bds::GravityFlags gravity;
        std::vector<bds::MovementEffect> movementEffects;
        // MobTravel intent written by VehicleDesiredMoveDirectionSystem (x right-positive like strafe);
        // replaces the player travel intent and its 0.98 attenuation
        std::optional<bds::Float3> mobIntent;
    };

    struct State {
        double x = 0, y = 0, z = 0;
        double vx = 0, vy = 0, vz = 0;
        float yaw = 0;
        bool onGround = false;
        bool horizontalCollision = false;
        // HorizontalCollisionFlagComponent lanes: no-clip leaves them, and the velocity reset still reads them
        bool horizontalCollisionX = false;
        bool horizontalCollisionZ = false;
        bool verticalCollision = false;
        bool collision = false;
        bool collidableMobNear = false;
        bool inWater = false;
        bool inLava = false;
        std::optional<uint32_t> waterSplashRequest;
        bool onClimbable = false;
        bool crawling = false;
        bool swimming = false;
        bool gliding = false;
        // riptide spin (DAMAGENEARBYMOBS) and its tick counter
        bool spinAttack = false;
        int spinTicks = 0;
        float collisionHeight = static_cast<float>(kStandHeight);
        float swimAmount = 0;
        int jumpDelay = 0;
        float stuckX = 0, stuckY = 0, stuckZ = 0;
        // last move request: horizontal velocity and position before the move (VanillaClientGameplay +28/+36, +40/+48)
        float requestX = 0, requestZ = 0, requestFromX = 0, requestFromZ = 0;
        float fallDistance = 0;
        bds::MovementEffects movementEffects;
        bds::Rotation previousRotation;
        bds::Float3 previousJumpPosition{};
        std::optional<bds::Float3> restitution;

        bds::RecoveryState recovery;
        bool canAlwaysAutoStep = false;

        Box box;
        bool hasBox = false;
        bool hasTeleported = false;

        // horizontal poses (gliding, riptide, swimming, crawling) take a square box: height = width
        bool HorizontalPose() const { return crawling || swimming || gliding || spinAttack; }
        double Height(bool sneaking) const {
            if (mobSize)
                return (*mobSize)[1];
            return HorizontalPose() ? kCrawlHeight : sneaking ? kSneakHeight : kStandHeight;
        }
        // non-player mob (ridden horse): its own collision box (width, height), no poses
        std::optional<std::array<float, 2>> mobSize;
    };

    Box BoundingBox(const State& state, bool sneaking);
    bool PoseFits(const State& state, const World& world, double height);
    bool TouchingWater(const State& state, const World& world);

    struct MoveVector {
        float forward = 0, strafe = 0;
    };

    // sneakScale: last tick's SneakingComponent, 0.3 unless swift sneak
    MoveVector ProcessMoveInput(float forward, float strafe, bool sneaking, bool inWater,
                                float itemUseModifier = 1.0f, float sneakScale = 0.3f);
    // sneaking: the actor SNEAKING flag when SneakingSystem runs (last tick's)
    float SneakSpeed(int swiftSneakLevel, bool sneaking);

    struct AttributeModifierValue {
        float amount = 0;
        int32_t operation = 0, operand = 0;
    };
    // AttributeInstance clamp 0x142bf2520: cap modifiers (operation 3) lower the max
    float ClampAttribute(float value, float min, float max, const std::vector<AttributeModifierValue>& modifiers);
    // AttributeInstance recalculation 0x142bf1680 for the movement attribute; base and limits: min, max, value / min, max
    float RecalculateAttribute(const std::array<float, 3>& base, const std::vector<AttributeModifierValue>& modifiers,
                               float current, std::array<float, 2>& limits);

    void Step(State& state, const Input& input, const Attributes& attributes, const World& world);

    struct BoxMove {
        float x = 0, y = 0, z = 0;
        float vx = 0, vy = 0, vz = 0;
        bool onGround = false, horizontalCollision = false, verticalCollision = false;
    };
    // non-player mover without auto-step: MoveCollision -> ActorMove -> FinalizeMove -> velocity resets;
    // position is the box's bottom centre
    BoxMove MoveBox(const World& world, const Box& start, float dx, float dy, float dz, bool wasOnGround,
                    bds::RecoveryState& recovery);
}
