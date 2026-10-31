#pragma once
#include <chrono>
#include <functional>
#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace perlin::entity {
    struct SkinData {
        uint64_t serial = 0;
        std::string skinId;
        int skinWidth = 0, skinHeight = 0;
        std::vector<uint8_t> skinRgba;
        int capeWidth = 0, capeHeight = 0;
        std::vector<uint8_t> capeRgba;
        bool slim = false;
        std::string geometryJson;
        std::string geometryName;
    };

    namespace flag {
        enum : uint64_t {
            Onfire = 1ull << 0, Sneaking = 1ull << 1, Riding = 1ull << 2, Sprinting = 1ull << 3, UsingItem = 1ull << 4,
            Invisible = 1ull << 5, Tempted = 1ull << 6, InLove = 1ull << 7, Saddled = 1ull << 8, Powered = 1ull << 9,
            Ignited = 1ull << 10, Baby = 1ull << 11, Converting = 1ull << 12, Critical = 1ull << 13, CanShowName = 1ull << 14,
            AlwaysShowName = 1ull << 15, Immobile = 1ull << 16, WallClimbing = 1ull << 18,
            Resting = 1ull << 23, Sitting = 1ull << 24, Angry = 1ull << 25, Interested = 1ull << 26, Charged = 1ull << 27,
            Tamed = 1ull << 28, Orphaned = 1ull << 29, Leashed = 1ull << 30, Sheared = 1ull << 31, Gliding = 1ull << 32,
            Elder = 1ull << 33, Moving = 1ull << 34, Breathing = 1ull << 35, Chested = 1ull << 36, Stackable = 1ull << 37,
            // MetadataFlags1 order of protocol/bedrock-1.26.60.json: linger takes bit 47, everything after moves up one
            Rearing = 1ull << 39, Idling = 1ull << 41, Silent = 1ull << 17, Linger = 1ull << 47, HasCollision = 1ull << 48,
            Gravity = 1ull << 49, FireImmune = 1ull << 50, Dancing = 1ull << 51, Enchanted = 1ull << 52,
            Transforming = 1ull << 55, SpinAttack = 1ull << 56, Swimming = 1ull << 57, Bribed = 1ull << 58,
            Pregnant = 1ull << 59, LayingEgg = 1ull << 60, Eating = 1ull << 63, Sleeping = 0,
            Charging = 1ull << 43,
        };
        constexpr uint64_t FacingTargetToRangeAttack = 1ull << 24;

        // MetadataFlags2 bits, read from Entity::flagsExtended
        struct Extended {
            uint64_t bit;
        };
        inline constexpr Extended LayingDown{1ull << 0};
    }

    struct Entity {
        uint64_t runtimeId = 0;
        int64_t uniqueId = 0;
        std::string type;
        bool isPlayer = false;
        bool isSelf = false;
        std::string name, uuid;
        std::string nameTag;
        glm::dvec3 pos{0.0};
        float yaw = 0, pitch = 0, headYaw = 0;
        bool onGround = true;
        uint64_t flags = 0;
        uint64_t flagsExtended = 0;
        bool crawling = false;
        float scale = 1.0f;
        // hitbox the server reported (metadata BOUNDINGBOX_WIDTH/HEIGHT); 0 if never sent
        float boxWidth = 0.0f, boxHeight = 0.0f;
        int variant = 0, markVariant = 0, color = 0;
        float health = -1.0f;
        uint32_t moveSerial = 0;
        uint32_t hurtSerial = 0;
        bool dying = false;
        uint32_t swingSerial = 0;
        bool isItem = false;
        glm::dvec3 vel{0.0};
        bool sleeping = false, sleepExt = false, sleepPlayer = false;
        bool simulated = false;
        bool haveServerPos = false;
        glm::dvec3 lastServerPos{0.0};
        std::chrono::steady_clock::time_point lastServerAt{};
        std::string itemName;
        int itemCount = 0, itemAux = 0;
        uint32_t itemBlock = 0;
        bool itemEnchanted = false;
        double born = -1.0;
        int64_t targetUid = 0;
        int64_t ownerUid = 0;
        int64_t vehicleUid = 0;
        std::string mainHand, offHand;
        std::string armor[4];
        std::shared_ptr<const SkinData> skin;
        std::map<std::string, float> props;
        std::map<std::string, std::string> propText;
        struct Attribute { float min = 0, value = 0, max = 0; };
        std::map<std::string, Attribute> attributes;
        bool Has(uint64_t f) const { return (flags & f) != 0 || (crawling && (f & flag::Swimming) != 0); }
        bool Has(flag::Extended f) const { return (flagsExtended & f.bit) != 0; }
        bool AimingBow() const { return mainHand == "minecraft:bow" && (flagsExtended & flag::FacingTargetToRangeAttack) != 0; }
    };

    class EntityTracker {
    public:
        void Clear();
        void AddPlayer(uint64_t rid, int64_t uid, const std::string& uuid, const std::string& name, glm::dvec3 feetPos, float pitch,
                       float yaw, float headYaw, uint64_t flags, const std::string& mainHand);
        void AddEntity(uint64_t rid, int64_t uid, const std::string& type, glm::dvec3 feetPos, float pitch, float yaw, float headYaw,
                       uint64_t flags, float scale, int variant, int markVariant, int color);
        void AddItem(uint64_t rid, int64_t uid, glm::dvec3 pos, const std::string& name, int count, int aux, uint32_t blockRuntimeId,
                     bool enchanted);
        void Remove(int64_t uid);
        void RemoveRuntime(uint64_t rid);
        void Pickup(uint64_t rid, uint64_t collector);
        void SetViewer(uint64_t rid, glm::dvec3 feet);
        void SetMotion(uint64_t rid, glm::dvec3 velocity);
        void SetSleeping(uint64_t rid, bool sleeping, bool playerFlag = false);
        void StepItems(const std::function<bool(int, int, int)>& solid);
        void Move(uint64_t rid, glm::dvec3 pos, float pitch, float yaw, float headYaw, bool onGround);
        void MoveDelta(uint64_t rid, const double* x, const double* y, const double* z, const float* pitch, const float* yaw,
                       const float* headYaw, bool onGround);
        void SetFlags(uint64_t rid, uint64_t flags);
        void SetFlagsExtended(uint64_t rid, uint64_t flags);
        void SetCrawling(uint64_t rid, bool crawling);
        void SetScale(uint64_t rid, float scale);
        void SetBoundingBox(uint64_t rid, float width, float height);
        void SetNameTag(uint64_t rid, const std::string& nameTag);
        void SetVariant(uint64_t rid, int variant, int markVariant, int color);
        void SetHealth(uint64_t rid, float health);
        void SetAttribute(uint64_t rid, const std::string& name, float min, float value, float max);
        void SetOwner(uint64_t rid, int64_t ownerUid);
        void SetTarget(uint64_t rid, int64_t targetUid);
        void SetHeld(uint64_t rid, const std::string& item);
        void SetOffHand(uint64_t rid, const std::string& item);
        void SetArmor(uint64_t rid, int slot, const std::string& item);
        void SetVehicle(int64_t riderUid, int64_t vehicleUid);
        void Hurt(uint64_t rid);
        void Die(uint64_t rid);
        void Swing(uint64_t rid);
        void SetProperty(uint64_t rid, const std::string& key, float value);
        void SetPropertyText(uint64_t rid, const std::string& key, const std::string& text);
        void SetSkin(const std::string& uuid, std::shared_ptr<const SkinData> skin, bool listed = true);
        void RemoveSkin(const std::string& uuid);
        void SetSelf(uint64_t rid, const std::string& uuid);

        std::vector<Entity> Snapshot() const;
        void Snapshot(std::vector<Entity>& out) const;
        size_t Count() const;
        bool Find(uint64_t rid, Entity& out) const;
        bool FindUnique(int64_t uid, Entity& out) const;

    private:
        void AttachSkinLocked(Entity& e);
        struct PickupState {
            uint64_t collector = 0;
            double startMs = 0;
            glm::dvec3 from{0.0};
        };
        void StepPickupsLocked(std::vector<uint64_t>* finished) const;
        mutable std::mutex mutex;
        mutable std::unordered_map<uint64_t, Entity> entities;
        mutable std::unordered_map<int64_t, uint64_t> byUnique;
        mutable std::unordered_map<uint64_t, PickupState> pickups;
        uint64_t viewerRuntime = 0;
        glm::dvec3 viewerFeet{0.0};
        std::unordered_map<std::string, std::shared_ptr<const SkinData>> skins;
        std::string selfUuid;
        uint64_t selfRuntime = 0;
    };

    inline constexpr double kPlayerEye = 1.62001;
}
