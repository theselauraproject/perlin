#pragma once
#include "inventory/Inventory.hpp"
#include "net/Batch.hpp"
#include "net/Connection.hpp"
#include "world/Chunk.hpp"
#include "world/ItemEntity.hpp"
#include "world/Mobs.hpp"

#include <chrono>
#include <memory>
#include <span>
#include <utility>
#include <string>
#include <unordered_set>
#include <vector>

#include "protocol/Packets.hpp"

#include <climits>
#include <map>

namespace perlin::server {

    class Server;

    class Session {
    public:
        enum class State : uint8_t {
            AwaitSettings,
            AwaitLogin,
            ResourcePacks,
            ResourceStack,
            Loading,
            InGame,
            Closing,
            Closed
        };

        Session(Server& server, std::shared_ptr<Connection> connection, uint64_t runtimeId);
        ~Session();

        void OnMessage(std::span<const uint8_t> wire);
        void Tick(uint64_t tick);

        template <class P> void Send(const P& packet);
        void SendText(uint8_t type, const std::string& source, const std::string& message);
        void SendRaw(const std::string& message);
        void Kick(const std::string& reason);
        void Teleport(double x, double y, double z);
        void Teleport(double x, double y, double z, float yawDegrees, float pitchDegrees);
        void Close();

        uint64_t Id() const { return runtimeId; }
        const std::string& Name() const { return name; }
        const std::string& Xuid() const { return xuid; }
        State GetState() const { return state; }
        bool InGame() const { return state == State::InGame; }
        bool IsClosed() const { return state == State::Closed; }
        bool Operator() const { return isOperator; }
        bool Visitor() const { return isVisitor; }
        int DimensionId() const { return dimension; }
        void SwitchDimension(int dimension, double x, double y, double z);
        int portalTicks = 0;
        int portalCooldown = 0;
        double X() const { return posX; }
        double Y() const { return posY; }
        double Z() const { return posZ; }
        const Connection& Conn() const { return *connection; }
        const proto2225::Skin& GetSkin() const;
        const std::array<uint8_t, 16>& UuidBytes() const { return uuid; }

        void ShowPlayer(const Session& other);
        void ShowArmor(const Session& wearer);
        bool EquipHeldArmor();
        void HidePlayer(const Session& other);
        void SendBlockUpdate(int x, int y, int z, int32_t networkId);
        void ShowItem(const ItemEntity& item);
        void HideItem(uint64_t id);
        void PlaySound(const std::string& sound, double x, double y, double z, int extra = -1);
        void SendLevelEvent(int event, double x, double y, double z, int data = 0);
        void MoveItem(const ItemEntity& item);
        void TakeItem(uint64_t itemId, uint64_t takerRuntimeId);
        void ShowMob(const Mob& mob);
        void MoveMob(const Mob& mob);
        void MobData(const Mob& mob);
        void MobMotion(const Mob& mob);
        void MobEvent(uint64_t id, int event, int data);
        enum class DamageCause : uint8_t {
            Generic,
            Mob,
            Player,
            Projectile,
            Explosion,
            Fall,
            Void,
            Lava,
            Fire,
            Burning,
            Drowning,
            Starving,
            Suffocation,
            Cactus,
            Magma,
            BerryBush,
            Poison,
            Wither,
            Magic,
            FallingBlock,
            Kill
        };
        void Hurt(float amount, double fromX, double fromZ);
        bool Damage(float amount, DamageCause cause, double fromX, double fromZ, const std::string& source = {},
                    Session* attacker = nullptr, float knockback = 0.4f);
        void Kill();
        bool Dead() const { return dead; }
        float Health() const { return health; }
        float MaxHealth() const;
        void Heal(float amount);
        int Food() const { return food; }
        float Saturation() const { return saturation; }
        void SetFood(int value, float sat);
        void Exhaust(float amount);
        void OnBlockMined(float hardness);
        int XpLevel() const { return xpLevel; }
        float XpProgress() const { return xpProgress; }
        void AddExperience(int points);
        void AddLevels(int levels);
        void AddEffect(int id, int amplifier, int ticks, bool particles = true, bool ambient = false);
        bool RemoveEffect(int id);
        void ClearEffects();
        int EffectLevel(int id) const;
        bool OnGround() const { return onGround; }
        bool Sprinting() const { return sprinting; }
        float FallDistance() const { return fallDistance; }
        int ArmorValue() const;
        void DamageHeld(int amount);
        bool Shielding(double fromX, double fromZ) const;
        void OpenWindow(int window, proto2225::WindowType type, int x, int y, int z);
        void SendWindowContents(proto2225::ContainerSlotType type, const std::vector<ItemStack>& slots);
        void SendWindowData(int property, int value);
        void CloseWindowFromServer();
        int OpenWindowId() const { return openWindow; }
        void ClearWindow() { openWindow = -1; }
        int64_t openContainer = INT64_MIN;
        bool Sneaking() const { return sneaking; }
        float Yaw() const { return yaw; }
        float Pitch() const { return pitch; }
        void SendMove(const Session& other, uint8_t mode);
        bool WantsChunk(ChunkPos pos) const;
        bool HasSent(ChunkPos pos) const { return sent.contains(pos); }
        PlayerInventory& Inventory() { return inventory; }
        bool Creative() const { return creative; }
        int GameMode() const { return gameMode; }
        bool Spectator() const { return gameMode == 6; }
        int FlySpeedLevel() const { return flySpeedLevel; }
        bool SetFlySpeedLevel(int level);
        void SendActionBar(const std::string& text);
        void SendTime(int32_t ticks);
        void SendWeather(bool rain, bool thunder);
        void SendGameRule(const std::string& lowerName, bool boolean, int value);
        void SendSpawnPosition(int x, int y, int z, bool world);
        void SendDifficulty(int difficulty);
        void SendTitle(int type, const std::string& text, int fadeIn = 10, int stay = 70, int fadeOut = 20);
        void SetGameMode(int mode);
        void SetOperator(bool op);
        void SendInventory();
        void SendSlot(int slot);
        void SendHeldSelection();
        void CorrectBlock(int x, int y, int z);
        bool InReach(int x, int y, int z) const;
        bool TakeMoved() { return std::exchange(moved, false); }

    private:
        void Dispatch(uint32_t id, std::span<const uint8_t> body);
        void Queue(uint32_t id, const std::vector<uint8_t>& body);
        void Flush();
        void SendImmediate(uint32_t id, const std::vector<uint8_t>& body);

        void OnRequestSettings(std::span<const uint8_t> body);
        void OnLogin(std::span<const uint8_t> body);
        void OnPackResponse(std::span<const uint8_t> body);
        void OnChunkRadius(std::span<const uint8_t> body);
        void OnInitialized();
        void OnText(std::span<const uint8_t> body);
        void OnCommand(std::span<const uint8_t> body);
        void OnAuthInput(std::span<const uint8_t> body);
        void OnMovePlayer(std::span<const uint8_t> body);
        void OnStackRequest(std::span<const uint8_t> body);
        void OnMobEquipment(std::span<const uint8_t> body);
        void OnTransaction(std::span<const uint8_t> body);
        void OnPlayerAction(std::span<const uint8_t> body);
        void OnPickBlock(std::span<const uint8_t> body);
        void OnInteract(std::span<const uint8_t> body);
        void OnContainerClose(std::span<const uint8_t> body);
        void OnAnimate(std::span<const uint8_t> body);
        void OnPlayerSkin(std::span<const uint8_t> body);
        void OnTickSync(std::span<const uint8_t> body);
        void OnLatency(std::span<const uint8_t> body);
        void OnEmote(std::span<const uint8_t> body);
        void OnSound(std::span<const uint8_t> body);
        void OnRespawn(std::span<const uint8_t> body);

        void SendStartGame();
        void SurvivalTick();
        void MovementTick(const proto2225::PlayerAuthInputPacket& in, double previousY);
        void FoodTick();
        void EffectTick();
        void EnvironmentTick();
        void FinishUsing();
        void StartUsing();
        void StopUsing();
        void ReleaseBow();
        void SendEffect(int id, int event);
        void SendSelfData();
        void SendArmorSlot(int slot);
        void DamageArmor(float amount);
        void Die(DamageCause cause, const std::string& source);
        std::string BlockNameAt(double x, double y, double z) const;
        float AttackDamage(const ItemStack& held) const;
        void AttackPlayer(Session& victim, const ItemStack& held);
        void SendAbilities();
        void SendAttributes();
        void SendCommands();
        void SendWorldSetup();
        void StreamChunks();
        void PublishCenter(bool force);
        void SetPosition(double x, double y, double z, float pitchValue, float yawValue);

        Server& server;
        std::shared_ptr<Connection> connection;
        BatchCodec codec;
        State state = State::AwaitSettings;
        std::vector<uint8_t> outgoing;

        uint64_t runtimeId;
        std::string name, xuid;
        std::array<uint8_t, 16> uuid{};
        std::unique_ptr<proto2225::Skin> skin;
        bool isOperator = false;
        bool isVisitor = false;
        int flySpeedLevel = 1;
        bool creative = false;
        int gameMode = 0;
        PlayerInventory inventory;
        float health = 20.0f;
        float absorption = 0.0f;
        int food = 20;
        float saturation = 5.0f;
        float exhaustion = 0.0f;
        int foodTimer = 0;
        int xpLevel = 0;
        float xpProgress = 0.0f;
        int xpTotal = 0;
        struct ActiveEffect {
            int amplifier = 0;
            int ticks = 0;
            bool particles = true;
            bool ambient = false;
        };
        std::map<int, ActiveEffect> effects;
        int invulnerable = 0;
        float lastHurt = 0.0f;
        float fallDistance = 0.0f;
        bool onGround = true;
        bool sprinting = false;
        bool swimming = false;
        bool gliding = false;
        bool flying = false;
        bool jumping = false;
        int air = 300;
        int fireTicks = 0;
        int envTicks = 0;
        int useTicks = -1;
        int useSlot = -1;
        int16_t useItem = 0;
        bool attributesDirty = false;
        bool dataDirty = false;
        int lastAirSent = 300;
        bool lastFireSent = false;
        double lastMoveX = 0, lastMoveZ = 0;
        bool sneaking = false;
        int openWindow = -1;
        proto2225::WindowType openType = proto2225::WindowType::Inventory;
        bool dead = false;

        double posX = 0.5, posY = 0, posZ = 0.5;
        int dimension = kOverworld;
        bool awaitingDimensionAck = false;
        std::chrono::steady_clock::time_point dimensionChangedAt;
        float pitch = 0, yaw = 0;
        int32_t radius = 0;
        ChunkPos center{0, 0};
        bool centerSent = false;
        bool spawnSent = false;
        bool moved = false;
        std::unordered_set<ChunkPos, ChunkPosHash> sent;
        std::chrono::steady_clock::time_point lastPacket = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point lastActive = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point lastHeartbeat = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point stateSince = std::chrono::steady_clock::now();
    };
}
