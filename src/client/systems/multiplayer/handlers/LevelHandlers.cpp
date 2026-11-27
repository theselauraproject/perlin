#include "client/systems/multiplayer/handlers/HandlerSupport.hpp"

namespace perlin {
    using namespace proto;
    using namespace handlers;

    void BedrockSession::RegisterLevelHandlers() {
        On<LevelEventPacket>([this](const LevelEventPacket& p) {
            if (p.event == LevelEventPacketEvent::BlockStartBreak ||
                p.event == LevelEventPacketEvent::BlockStopBreak ||
                p.event == LevelEventPacketEvent::BlockBreakSpeed) {
                const int bx = static_cast<int>(std::floor(p.position.x)),
                          by = static_cast<int>(std::floor(p.position.y)),
                          bz = static_cast<int>(std::floor(p.position.z));
                std::lock_guard lock(crackMutex);
                const auto now = std::chrono::steady_clock::now();
                const auto it = std::ranges::find_if(
                    crackStates, [&](const CrackState& c) { return c.x == bx && c.y == by && c.z == bz; });
                if (p.event == LevelEventPacketEvent::BlockStopBreak) {
                    if (it != crackStates.end())
                        crackStates.erase(it);
                } else {
                    if (it == crackStates.end()) {
                        if (bx == lastClearedCrack[0] && by == lastClearedCrack[1] &&
                            bz == lastClearedCrack[2] && now - lastClearedAt < std::chrono::milliseconds(750))
                            return;
                        bool present = false;
                        const uint16_t cur = netLevel.GetBlock(bx, by, bz, present);
                        if (present && cur == 0)
                            return;
                    }
                    CrackState* c = it != crackStates.end() ? &*it : &crackStates.emplace_back();
                    if (it == crackStates.end() || c->since == std::chrono::steady_clock::time_point{}) {
                        c->x = bx;
                        c->y = by;
                        c->z = bz;
                        c->progress = 0.0;
                    } else {
                        c->progress = std::min(
                            1.0, c->progress +
                                     c->perSecond * std::chrono::duration<double>(now - c->since).count());
                    }
                    c->since = now;
                    c->perSecond = static_cast<double>(p.data) / 65535.0 * 20.0;
                }
                return;
            }
            {
                const int ev = static_cast<int>(p.event);
                if ((ev >= 2000 && ev <= 2035) || ev >= 16384) {
                    ParticleEvent pe;
                    pe.event = ev;
                    pe.x = p.position.x;
                    pe.y = p.position.y;
                    pe.z = p.position.z;
                    pe.data = static_cast<int32_t>(p.data);
                    PushParticle(std::move(pe));
                }
            }
            const auto named = [&](const char* sound, float volume = 1.0f, float pitch = 1.0f) {
                SoundRequest r;
                r.name = sound;
                r.x = p.position.x;
                r.y = p.position.y;
                r.z = p.position.z;
                r.volume = volume;
                r.pitch = pitch;
                PushSound(std::move(r));
            };
            switch (p.event) {
            case LevelEventPacketEvent::SoundClick:
                named("random.click", 1.0f, 1.0f);
                break;
            case LevelEventPacketEvent::SoundClickFail:
                named("random.click", 1.0f, 0.8f);
                break;
            case LevelEventPacketEvent::SoundShoot:
                named("random.bow");
                break;
            case LevelEventPacketEvent::SoundDoor:
                named("random.door_open");
                break;
            case LevelEventPacketEvent::SoundFizz:
                named("random.fizz", 0.5f);
                break;
            case LevelEventPacketEvent::SoundIgnite:
                named("fire.ignite");
                break;
            case LevelEventPacketEvent::SoundGhast:
                named("mob.ghast.moan");
                break;
            case LevelEventPacketEvent::SoundGhastShoot:
                named("mob.ghast.fireball");
                break;
            case LevelEventPacketEvent::SoundBlazeShoot:
                named("mob.blaze.shoot");
                break;
            case LevelEventPacketEvent::SoundEndermanTeleport:
                named("mob.endermen.portal");
                break;
            case LevelEventPacketEvent::SoundAnvilBreak:
                named("random.anvil_break");
                break;
            case LevelEventPacketEvent::SoundAnvilUse:
                named("random.anvil_use");
                break;
            case LevelEventPacketEvent::SoundAnvilFall:
                named("random.anvil_land");
                break;
            case LevelEventPacketEvent::SoundPop:
                named("random.pop");
                break;
            case LevelEventPacketEvent::SoundPortal:
                named("portal.portal");
                break;
            case LevelEventPacketEvent::SoundOrb: {
                const auto nowMs =
                    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now().time_since_epoch())
                                              .count());
                const float dx = p.position.x - lastOrbX, dy = p.position.y - lastOrbY,
                            dz = p.position.z - lastOrbZ;
                if (nowMs - lastLocalOrbMs < 200u && dx * dx + dy * dy + dz * dz < 9.0f)
                    break;
                named("random.orb");
                break;
            }
            case LevelEventPacketEvent::SoundTotem:
                named("random.totem");
                break;
            case LevelEventPacketEvent::SoundCamera:
                named("camera.take_picture");
                break;
            case LevelEventPacketEvent::ParticleDestroy: {
                SoundRequest r;
                r.kind = SoundRequest::Kind::Level;
                r.name = "break";
                r.x = p.position.x;
                r.y = p.position.y;
                r.z = p.position.z;
                r.extra = p.data;
                PushSound(std::move(r));
                break;
            }
            default:
                break;
            }
            switch (p.event) {
            case LevelEventPacketEvent::StartRain:
                rainTarget = 1.0f;
                break;
            case LevelEventPacketEvent::StopRain:
                rainTarget = 0.0f;
                break;
            case LevelEventPacketEvent::StartThunder:
                thunderTarget = 1.0f;
                break;
            case LevelEventPacketEvent::StopThunder:
                thunderTarget = 0.0f;
                break;
            default:
                break;
            }
        });

        On<SetCommandsEnabledPacket>([this](const SetCommandsEnabledPacket& p) {
            std::lock_guard<std::mutex> lk(worldMutex);
            world.commandsEnabled = p.enabled;
        });
        On<SetDifficultyPacket>([this](const SetDifficultyPacket& p) {
            std::lock_guard<std::mutex> lk(worldMutex);
            world.difficulty = static_cast<int>(p.difficulty);
        });
        On<GameRulesChangedPacket>([this](const GameRulesChangedPacket& p) {
            {
                std::lock_guard<std::mutex> lk(worldMutex);
                for (const GameRuleI32& rule : p.rules) {
                    std::string name = rule.name;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    if (const bool* b = std::get_if<bool>(&rule.value))
                        world.rules[name] = *b ? 1 : 0;
                    else if (const int32_t* i = std::get_if<int32_t>(&rule.value))
                        world.rules[name] = *i;
                }
            }
            for (const GameRuleI32& rule : p.rules)
                if (EqualsIgnoreCase(rule.name, "dodaylightcycle"))
                    if (const bool* enabled = std::get_if<bool>(&rule.value))
                        SetDayCycle(*enabled);
        });

        On<SetSpawnPositionPacket>([this](const SetSpawnPositionPacket& p) {
            const BlockCoordinates& world = p.worldPosition;
            const int32_t worldY = static_cast<int32_t>(world.y);
            Log("bedrock", "SetSpawnPosition type={} pos=({},{},{}) dim={} spawn=({},{},{})",
                static_cast<int>(p.spawnType), p.playerPosition.x, static_cast<int32_t>(p.playerPosition.y),
                p.playerPosition.z, p.dimension, world.x, worldY, world.z);
            if (worldY < 1000) {
                worldSpawn[0] = world.x;
                worldSpawn[1] = worldY;
                worldSpawn[2] = world.z;
                haveWorldSpawn = true;
            }
        });

        On<UpdateBlockPacket>([this](const UpdateBlockPacket& p) {
            std::lock_guard commitLock(chunkCommitMutex);
            if (!ChunkInputCurrent())
                return;
            lastBlockPos[0] = p.position.x;
            lastBlockPos[1] = static_cast<int32_t>(p.position.y);
            lastBlockPos[2] = p.position.z;
            haveBlockPos = true;
            static int blockLogs = 0;
            if (blockLogs < 400 || PERLIN_ENV("PERLIN_BLOCK_DEBUG")) {
                if (blockLogs < 400)
                    ++blockLogs;
                Log("block", "update_block ({},{},{}) layer {} id {} {} afterAuthTick={}", p.position.x,
                    p.position.y, p.position.z, p.layer, static_cast<int32_t>(p.blockRuntimeId),
                    BlockPalette::Instance().Name(
                        BlockPalette::Instance().InternRuntime(static_cast<int32_t>(p.blockRuntimeId))),
                    LastAuthTick());
            }
            NoteUnknownBlock(static_cast<int32_t>(p.blockRuntimeId));
            if (p.layer == 0)
                ConfirmPlace(p.position.x, static_cast<int32_t>(p.position.y), p.position.z,
                             static_cast<int32_t>(p.blockRuntimeId));
            if (p.layer == 0) {
                const uint16_t id =
                    BlockPalette::Instance().InternRuntime(static_cast<int32_t>(p.blockRuntimeId));
                auto& mm = mods::ModManager::Instance();
                if (mm.Wants(abi::Event::BlockChanged)) {
                    const uint16_t was = netLevel.GetBlock(lastBlockPos[0], lastBlockPos[1], lastBlockPos[2]);
                    if (was != id) {
                        abi::Writer w;
                        w.write(abi::EventBlockChanged{lastBlockPos[0], lastBlockPos[1], lastBlockPos[2],
                                                       BlockPalette::Instance().Name(was),
                                                       BlockPalette::Instance().Name(id)});
                        mm.Queue(abi::Event::BlockChanged, std::move(w));
                    }
                }
                netLevel.SetBlock(lastBlockPos[0], lastBlockPos[1], lastBlockPos[2], id);
                RecordChunkCellUpdate(lastBlockPos[0], lastBlockPos[1], lastBlockPos[2], 0, id);
                std::lock_guard lock(supportMutex);
                const auto now = std::chrono::steady_clock::now();
                std::erase_if(brokenSupports, [&](const BrokenSupport& b) {
                    if (now - b.at > std::chrono::seconds(3))
                        return true;
                    if (b.x != lastBlockPos[0] || b.y != lastBlockPos[1] || b.z != lastBlockPos[2])
                        return false;
                    if (id != 0)
                        for (const auto& [y, old] : b.above) {
                            bool loaded = false;
                            if (netLevel.GetBlock(b.x, y, b.z, loaded) == 0 && loaded) {
                                netLevel.SetBlock(b.x, y, b.z, old);
                                RecordChunkCellUpdate(b.x, y, b.z, 0, old);
                            }
                        }
                    return true;
                });
            } else if (p.layer == 1) {
                const uint16_t id = BlockPalette::Instance().InternRuntime(static_cast<int32_t>(p.blockRuntimeId));
                netLevel.SetLiquid(lastBlockPos[0], lastBlockPos[1], lastBlockPos[2], id);
                RecordChunkCellUpdate(lastBlockPos[0], lastBlockPos[1], lastBlockPos[2], 1, id);
            }
        });

        On<UpdateSubchunkBlocksPacket>([this](const UpdateSubchunkBlocksPacket& p) {
            std::lock_guard commitLock(chunkCommitMutex);
            if (!ChunkInputCurrent())
                return;
            const auto sane = [&](const BlockUpdate& b) {
                const int32_t y = static_cast<int32_t>(b.position.y);
                if (y < -64 || y >= 320 || std::abs(b.position.x) > 30000000 ||
                    std::abs(b.position.z) > 30000000)
                    return false;
                return netLevel.HasChunk(b.position.x >> 4, b.position.z >> 4);
            };
            static int logged = 0;
            if (logged < 100 || PERLIN_ENV("PERLIN_BLOCK_DEBUG")) {
                if (logged < 100)
                    ++logged;
                int ok = 0;
                for (const BlockUpdate& b : p.blocks)
                    ok += sane(b) ? 1 : 0;
                std::cout << "[block] update_subchunk_blocks at (" << p.x << "," << p.y << "," << p.z << ") "
                          << p.blocks.size() << " blocks (" << ok << " applied), " << p.extra.size()
                          << " extra";
                if (!p.blocks.empty())
                    std::cout << "; first (" << p.blocks[0].position.x << ","
                              << static_cast<int32_t>(p.blocks[0].position.y) << "," << p.blocks[0].position.z
                              << ") rid " << p.blocks[0].runtimeId;
                std::cout << " afterAuthTick=" << LastAuthTick() << std::endl;
            }
            for (const BlockUpdate& b : p.extra)
                if (sane(b)) {
                    const uint16_t id = BlockPalette::Instance().InternRuntime(static_cast<int32_t>(b.runtimeId));
                    netLevel.SetLiquid(b.position.x, static_cast<int32_t>(b.position.y), b.position.z, id);
                    RecordChunkCellUpdate(b.position.x, static_cast<int32_t>(b.position.y), b.position.z, 1, id);
                }
            for (const BlockUpdate& b : p.blocks) {
                if (!sane(b))
                    continue;
                NoteUnknownBlock(static_cast<int32_t>(b.runtimeId));
                ConfirmPlace(b.position.x, static_cast<int32_t>(b.position.y), b.position.z,
                             static_cast<int32_t>(b.runtimeId));
                const uint16_t id = BlockPalette::Instance().InternRuntime(static_cast<int32_t>(b.runtimeId));
                netLevel.SetBlock(b.position.x, static_cast<int32_t>(b.position.y), b.position.z, id);
                RecordChunkCellUpdate(b.position.x, static_cast<int32_t>(b.position.y), b.position.z, 0, id);
            }
        });

        On<UpdateBlockSyncedPacket>([this](const UpdateBlockSyncedPacket& p) {
            std::lock_guard commitLock(chunkCommitMutex);
            if (!ChunkInputCurrent())
                return;
            const int x = p.position.x, y = static_cast<int32_t>(p.position.y), z = p.position.z;
            const uint16_t id =
                BlockPalette::Instance().InternRuntime(static_cast<int32_t>(p.blockRuntimeId));
            if (p.layer == 0)
                ConfirmPlace(x, y, z, static_cast<int32_t>(p.blockRuntimeId));
            if (p.layer == 0)
                netLevel.SetBlock(x, y, z, id);
            else if (p.layer == 1)
                netLevel.SetLiquid(x, y, z, id);
            if (p.layer <= 1)
                RecordChunkCellUpdate(x, y, z, p.layer, id);
        });
    }
}
