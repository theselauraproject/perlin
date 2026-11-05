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

namespace perlin::server {

    class Server;

    // one connected player: the bedrock login handshake, chunk streaming and packet handling
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
        // per tick: stream chunks, then flush whatever was queued
        void Tick(uint64_t tick);

        template <class P> void Send(const P& packet);
        void SendText(uint8_t type, const std::string& source, const std::string& message);
        // one chat line per line of text: the client draws a newline inside a message as a single odd-looking row
        void SendRaw(const std::string& message);
        void Kick(const std::string& reason);
        void Teleport(double x, double y, double z);
        // also turns the player: yaw and pitch in degrees
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

        // other players entering / leaving this player's world
        void ShowPlayer(const Session& other);
        // what another player (or this one) wears, for this client to draw
        void ShowArmor(const Session& wearer);
        // puts the held armor piece on, swapping with what was worn (use in air with armor in hand)
        bool EquipHeldArmor();
        void HidePlayer(const Session& other);
        void SendBlockUpdate(int x, int y, int z, int32_t networkId);
        // items lying in the world
        void ShowItem(const ItemEntity& item);
        void HideItem(uint64_t id);
        void MoveItem(const ItemEntity& item);
        void TakeItem(uint64_t itemId, uint64_t takerRuntimeId);
        void ShowMob(const Mob& mob);
        void MoveMob(const Mob& mob);
        void MobData(const Mob& mob);
        void MobMotion(const Mob& mob);
        void MobEvent(uint64_t id, int event, int data);
        void Hurt(float amount, double fromX, double fromZ);
        bool Dead() const { return dead; }
        float Health() const { return health; }
        void Heal(float amount);
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
        // sends a chunk to the player if inside their radius and not already sent
        bool WantsChunk(ChunkPos pos) const;
        bool HasSent(ChunkPos pos) const { return sent.contains(pos); }
        PlayerInventory& Inventory() { return inventory; }
        bool Creative() const { return creative; }
        int GameMode() const { return gameMode; }
        bool Spectator() const { return gameMode == 6; }
        // spectator flight speed as a multiple of the normal one: 1, 2, 5, 10 or 20
        int FlySpeedLevel() const { return flySpeedLevel; }
        // false when the level is not one of the five or the player is not a spectator; resends the abilities
        bool SetFlySpeedLevel(int level);
        // one line over the hotbar
        void SendActionBar(const std::string& text);
        // world state pushed by commands
        void SendTime(int32_t ticks);
        void SendWeather(bool rain, bool thunder);
        void SendGameRule(const std::string& lowerName, bool boolean, int value);
        void SendSpawnPosition(int x, int y, int z, bool world);
        void SendDifficulty(int difficulty);
        // type: 0 clear, 1 reset, 2 title, 3 subtitle, 4 action bar, 5 times (fade in, stay, fade out in ticks)
        void SendTitle(int type, const std::string& text, int fadeIn = 10, int stay = 70, int fadeOut = 20);
        // 0 survival, 1 creative, 2 adventure; tells the client so its abilities (flying, instant break) follow
        void SetGameMode(int mode);
        // permission changes take effect without rejoining: the client gets its abilities and command list again
        void SetOperator(bool op);
        // whole inventory again, e.g. after the server changed it
        void SendInventory();
        void SendSlot(int slot);
        void SendHeldSelection();
        // the world's real block at a position, to undo a client prediction
        void CorrectBlock(int x, int y, int z);
        bool InReach(int x, int y, int z) const;
        // true once after the position changed
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
        // permissions.json "visitor": no building, breaking, fighting or opening things
        bool isVisitor = false;
        int flySpeedLevel = 1;
        bool creative = false;
        int gameMode = 0;
        PlayerInventory inventory;
        float health = 20.0f;
        bool sneaking = false;
        int openWindow = -1;
        proto2225::WindowType openType = proto2225::WindowType::Inventory;
        bool dead = false;
        int hurtCooldown = 0;
        int regenTicks = 0;

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
        // last time the player moved, looked, acted or chatted (player-idle-timeout)
        std::chrono::steady_clock::time_point lastActive = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point lastHeartbeat = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point stateSince = std::chrono::steady_clock::now();
    };
}
