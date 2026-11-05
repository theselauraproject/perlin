#pragma once
#include "core/Access.hpp"
#include "core/Commands.hpp"
#include "core/Config.hpp"
#include "net/Connection.hpp"
#include "net/LoopbackListener.hpp"
#include "plugin/PluginApi.hpp"
#include "plugin/PluginHost.hpp"
#include "session/Session.hpp"
#include "world/ChunkManager.hpp"
#include "world/Mobs.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <set>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace perlin::server {

    struct CatalogItem;

    struct PortalFrame {
        int dimension = 0;
        int x = 0, y = 0, z = 0;
        int axis = 0;
        int width = 2, height = 3;
    };

    struct CommandDef {
        std::string name;
        std::string description;
        std::vector<OverloadSpec> overloads;
        std::string plugin;
        bool operatorOnly = false;
        std::function<void(CommandCtx&)> handler;
    };

    class Server final : public PluginApi {
    public:
        explicit Server(ServerConfig config);
        ~Server() override;

        bool Start(std::string& error);
        void Run();
        void Tick();
        void Stop();
        void RequestStop() { stopRequested = true; }
        bool StopRequested() const { return stopRequested; }

        void RunConsoleCommand(const std::string& line);

        LoopbackListener& Loopback() { return *loopback; }

        const ServerConfig& Config() const { return config; }
        ChunkManager& Chunks() { return *chunks; }
        ChunkManager& World(int dimension);
        ChunkManager* LoadedWorld(int dimension) const;
        std::vector<Session*> PlayersIn(int dimension);
        void ChangeDimension(Session& who, int dimension, double x, double y, double z);
        uint16_t BlockAt(int dimension, int x, int y, int z);
        void SetBlockIn(int dimension, int x, int y, int z, uint16_t block) { ApplyBlock(dimension, x, y, z, block); }
        PluginHost& Plugins() { return *plugins; }
        uint64_t TickCount() const { return tick; }
        std::chrono::nanoseconds TickDelay() const;
        const std::map<std::string, CommandDef>& Commands() const { return commands; }

        void OnPlayerJoined(Session& session);
        void OnPlayerLeft(Session& session);
        void HandleChat(Session& from, const std::string& message);
        void RunPlayerCommand(Session& from, const std::string& line);
        std::vector<Session*> InGamePlayers();
        void BroadcastArmor(const Session& wearer);
        Session* FindSession(uint64_t id);
        Session* FindByName(const std::string& name);
        bool IsOperatorName(const std::string& name) const;
        bool IsOperator(const std::string& name, const std::string& xuid) const;
        bool IsWhitelisted(const std::string& name, const std::string& xuid) const {
            return allowList.Contains(name, xuid);
        }
        std::string PermissionOf(const std::string& name, const std::string& xuid) const;
        AccessList& Permissions() { return permissionList; }
        AccessList& Whitelist() { return allowList; }
        std::string AllowlistPath() const;
        std::string PermissionsPath() const;
        bool IgnoresPlayerLimit(const std::string& name, const std::string& xuid) const;
        AccessList& Ops() { return permissionList; }
        std::string SetOperator(const std::string& name, bool op);
        bool Full() const;
        double SpawnX() const { return static_cast<double>(spawnX); }
        double SpawnY() const { return static_cast<double>(spawnY); }
        double SpawnZ() const { return static_cast<double>(spawnZ); }
        void SetWorldSpawn(int x, int y, int z);

        int64_t DayTime() const { return dayTime; }
        void SetDayTime(int64_t ticks);
        void SetWeather(const std::string& kind, int durationTicks);
        bool Raining() const { return raining; }
        bool Thundering() const { return thundering; }
        void SendWorldState(Session& to);

        struct GameRule {
            std::string display;
            bool boolean = true;
            int value = 0;
        };
        const std::map<std::string, GameRule>& GameRules() const { return gameRules; }
        int RuleValue(const std::string& lowerName) const;
        bool SetRule(const std::string& lowerName, int value);

        bool IsBanned(const std::string& name, const std::string& xuid) const {
            return banList.Contains(name, xuid);
        }
        AccessList& Bans() { return banList; }

        bool PlayerBreak(Session& by, int x, int y, int z);
        bool PlayerPlace(Session& by, int x, int y, int z, int face);
        void SendBlockTo(Session& to, int x, int y, int z);
        void DropFromPlayer(Session& by, const ItemStack& stack);
        void SpawnItem(const ItemStack& stack, double x, double y, double z, double vx, double vy, double vz,
                       int pickupDelay, int dimension = kOverworld);
        const std::vector<ItemEntity>& Items() const { return itemEntities; }
        MobWorld& Mobs() { return *mobWorld; }
        struct BlockContainer {
            int kind = 0;
            int x = 0, y = 0, z = 0;
            std::vector<ItemStack> slots;
            int burn = 0, burnMax = 0, cook = 0;
            bool lit = false;
            int dimension = 0;
        };
        bool UseBlock(Session& by, int x, int y, int z);
        void SyncContainer(Session& by);
        void CloseContainer(Session& by);
        static int ContainerKindOf(const std::string& blockName);
        const std::vector<uint8_t>& ItemRegistryBody() const { return itemRegistryBody; }
        const std::vector<uint8_t>& CreativeContentBody() const { return creativeBody; }

        void Broadcast(const std::string& text) override;
        bool Message(uint64_t player, const std::string& text) override;
        bool Kick(uint64_t player, const std::string& reason) override;
        void RegisterCommand(const std::string& plugin, const std::string& name,
                             const std::string& description) override;
        std::vector<PlayerInfo> Players() override;
        bool Teleport(uint64_t player, double x, double y, double z) override;
        abi::Status SetBlock(int x, int y, int z, const std::string& block) override;
        abi::Status GetBlock(int x, int y, int z, std::string& block) override;
        Status GetStatus() override;
        std::expected<int, abi::Status> GiveItem(uint64_t player, const std::string& item,
                                                 int count) override;

        std::string Describe() const;

    private:
        void RegisterBuiltins();
        void BuildItemPackets();
        void ApplyBlock(int x, int y, int z, uint16_t block) { ApplyBlock(kOverworld, x, y, z, block); }
        void ApplyBlock(int dimension, int x, int y, int z, uint16_t block);
        void Pump();
        void TickItems();
        void ReapClosed();

        ServerConfig config;
        std::unique_ptr<ChunkManager> chunks;
        std::array<std::unique_ptr<ChunkManager>, kDimensionCount> otherWorlds;
        std::unique_ptr<PluginHost> plugins;
        std::unique_ptr<LoopbackListener> loopback;
        std::unique_ptr<Listener> nethernet;
        std::unique_ptr<Listener> raknet;
        EventQueue events;
        int spawnX = 0, spawnY = 0, spawnZ = 0;
        int64_t dayTime = 6000;
        bool raining = false, thundering = false;
        int weatherTicks = 0;
        std::map<std::string, GameRule> gameRules;
        AccessList banList{AccessList::Kind::Allowlist};
        void InitGameRules();
        void DrainLocate();
        std::mutex locateMutex;
        std::vector<std::pair<uint64_t, std::string>> locateDone;
        std::vector<std::thread> locateThreads;
        std::set<uint64_t> locating;
        std::atomic<bool> locateCancel{false};
        AccessList permissionList{AccessList::Kind::Permissions}, allowList{AccessList::Kind::Allowlist};

        std::unordered_map<uint64_t, std::unique_ptr<Session>> sessions;
        uint64_t nextRuntimeId = 1;
        std::map<std::string, CommandDef> commands;
        std::vector<uint8_t> itemRegistryBody, creativeBody;
        std::vector<ItemEntity> itemEntities;
        std::unique_ptr<MobWorld> mobWorld;
        std::unordered_map<int64_t, BlockContainer> containers;
        void TickContainers();
        void DropContainer(int dimension, int x, int y, int z);
        bool UseItemOn(Session& by, int dimension, int x, int y, int z, int face, const CatalogItem& item);
        void OnBlockRemoved(Session& by, int dimension, int x, int y, int z, const std::string& name);
        bool TryLightPortal(int dimension, int x, int y, int z);
        bool FindPortalFrame(int dimension, int x, int y, int z, int axis, PortalFrame& out);
        void FillPortal(const PortalFrame& frame);
        bool PlacePortalNear(int dimension, int x, int y, int z, PortalFrame& out);
        std::vector<PortalFrame> portals;
        void TickPortals();
        void TickGravity();
        std::vector<std::array<int, 4>> gravityQueue;
        void TravelThroughPortal(Session& who, bool endPortal);
        void BuildEndPlatform();
        bool endPlatformBuilt = false;
        void PushContainer(BlockContainer& c, bool slotsChanged);
        uint64_t nextEntityId = 1'000'000;
        uint64_t tick = 0;
        double tickRateTarget = 20.0;
        bool frozen = false;
        int64_t stepRemaining = 0;
        int64_t sprintRemaining = 0;
        int64_t sprintTotal = 0;
        std::chrono::steady_clock::time_point sprintStarted;
        std::deque<double> tickMillis;
        std::string TickQuery() const;
        void RunTickCommand(Session* from, const std::string& args);
        void RunCommand(Session* from, const std::string& line);
        int RunExecute(Session* from, const std::string& args);
        bool executeElevated = false;
        std::atomic<bool> stopRequested{false};
        bool started = false;
    };

    std::unique_ptr<Listener> CreateNetherNetListener(const ServerConfig& config,
                                                      std::function<std::string()> infoJson);
    std::unique_ptr<Listener> CreateRakNetListener(const ServerConfig& config, std::function<std::string()> motd);
}
