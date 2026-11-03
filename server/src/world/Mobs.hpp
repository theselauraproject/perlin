#pragma once
#include "world/MobData.hpp"

#include <cstdint>
#include <list>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

namespace perlin::server {

    class Server;
    class Session;
    struct ChunkPos;

    bool BlockSolid(uint16_t id);
    bool BlockLiquid(uint16_t id);

    struct Mob {
        uint64_t id = 0;
        const MobDef* def = nullptr;
        double x = 0, y = 0, z = 0;
        double vx = 0, vy = 0, vz = 0;
        float yaw = 0, headYaw = 0, pitch = 0;
        float health = 10;
        int hurtTime = 0;
        int deathTime = 0;
        int attackCooldown = 0;
        int panicTicks = 0;
        int goalTicks = 0;
        double goalX = 0, goalY = 0, goalZ = 0;
        bool hasGoal = false;
        uint64_t target = 0;
        bool angry = false;
        bool onGround = false;
        bool inWater = false;
        bool burning = false;
        bool persistent = false;
        bool baby = false;
        int fuse = -1;
        int age = 0;
        int burnTicks = 0;
        int shootCooldown = 0;
        bool projectile = false;
        uint64_t owner = 0;
        float lastX = 0, lastY = 0, lastZ = 0, lastYaw = 0, lastHead = 0;
        uint64_t lastFlags = 0;
    };

    class MobWorld {
    public:
        explicit MobWorld(Server& server);

        void Tick();
        Mob* Spawn(const std::string& identifier, double x, double y, double z, bool persistent = true);
        bool Kill(uint64_t id);
        int KillAll(const std::string& identifier);
        void Show(Session& to) const;
        bool Attack(Session& by, uint64_t entity, float damage);
        bool Interact(Session& by, uint64_t entity);
        Mob* Find(uint64_t id);
        const std::list<Mob>& All() const { return mobs; }
        void OnChunkReady(int chunkX, int chunkZ);
        static uint64_t Flags(const Mob& m);

    private:
        void Physics(Mob& m);
        void Think(Mob& m);
        void Broadcast(Mob& m, bool force);
        void Die(Mob& m, Session* killer);
        void NaturalSpawns();
        void SpawnPack(const std::string& category, int x, int z, int surfaceY, bool chunkGen);
        bool Solid(double x, double y, double z) const;
        bool Collides(const Mob& m, double x, double y, double z) const;
        int SurfaceY(int x, int z) const;
        bool Dark(int x, int y, int z) const;
        bool Day() const;
        Session* Nearest(const Mob& m, double range, bool survivalOnly);
        void Hurt(Mob& m, float damage, double fromX, double fromZ, Session* by);
        void Explode(Mob& m);
        void Shoot(Mob& m, Session& at);

        Server& server;
        std::list<Mob> mobs;
        std::unordered_set<uint64_t> spawnedChunks;
        std::mt19937 rng{0xC0FFEE};
        uint64_t nextId = 5'000'000;
        int spawnTick = 0;
    };
}
