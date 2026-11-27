#include "client/systems/multiplayer/handlers/HandlerSupport.hpp"

namespace perlin {
    using namespace proto;
    using namespace handlers;

    void BedrockSession::RegisterEntityLifecycleHandlers() {
        On<AddPlayerPacket>([this](const AddPlayerPacket& p) {
            if (runtimeId != 0 && p.runtimeId == runtimeId)
                return;
            {
                std::lock_guard<std::mutex> lk(tabMutex);
                permByUid[p.uniqueId] = static_cast<int>(p.permissionLevel);
                for (TabPlayer& t : tabPlayers)
                    if (t.uid == p.uniqueId)
                        t.perm = static_cast<int>(p.permissionLevel);
            }
            std::string held;
            if (const auto it = itemNames.find(p.heldItem.networkId);
                p.heldItem.networkId != 0 && it != itemNames.end())
                held = it->second;
            entities.AddPlayer(p.runtimeId, p.uniqueId, UuidKey(p.uuid), p.username,
                               {p.position.x, p.position.y, p.position.z}, p.pitch, p.yaw, p.headYaw,
                               MetaFlags(p.metadata), held);
            if (std::getenv("PERLIN_DEBUG_NAMETAGS") != nullptr) {
                std::string flat = p.username;
                for (char& c : flat)
                    if (c == '\n')
                        c = '|';
                Log("npcname", "addplayer rid={} user={}", p.runtimeId, flat);
            }
            ApplyMetadata(entities, p.runtimeId, p.metadata);
            ApplyEntityProperties(p.runtimeId, "minecraft:player", p.properties);
        });

        On<AddPaintingPacket>([this](const AddPaintingPacket& p) {
            entities.AddEntity(p.runtimeEntityId, p.entityIdSelf, "minecraft:painting",
                               {p.coordinates.x, p.coordinates.y, p.coordinates.z}, 0.0f, 0.0f, 0.0f, 0, 1.0f,
                               0, 0, 0);
            entities.SetPropertyText(p.runtimeEntityId, "painting_motive", p.title);
            entities.SetProperty(p.runtimeEntityId, "painting_direction", static_cast<float>(p.direction));
        });

        On<AddEntityPacket>([this](const AddEntityPacket& p) {
            if (runtimeId != 0 && p.runtimeId == runtimeId)
                return;
            entities.AddEntity(p.runtimeId, p.uniqueId, p.entityType,
                               {p.position.x, p.position.y, p.position.z}, p.pitch, p.yaw, p.headYaw,
                               MetaFlags(p.metadata), 1.0f, 0, 0, 0);
            entities.SetMotion(p.runtimeId, {p.velocity.x, p.velocity.y, p.velocity.z});
            ApplyMetadata(entities, p.runtimeId, p.metadata);
            ApplyEntityProperties(p.runtimeId, p.entityType, p.properties);
            for (const EntityAttributesEntry& a : p.attributes) {
                entities.SetAttribute(p.runtimeId, a.name, a.min, a.value, a.max);
                if (a.name == "minecraft:health")
                    entities.SetHealth(p.runtimeId, a.value);
            }
        });

        On<VoxelShapesPacket>([](const VoxelShapesPacket& p) {
            VoxelShapeRegistry::Instance().Set(p);
            Log("bedrock", "VoxelShapes: {} shapes, {} named", p.shapes.size(), p.nameMap.size());
        });

        On<AddItemEntityPacket>([this](const AddItemEntityPacket& p) {
            const auto it = itemNames.find(p.item.networkId);
            if (p.item.networkId == 0 || it == itemNames.end() || p.item.count == 0)
                return;
            if (droppedRaw.size() > 1024)
                droppedRaw.clear();
            droppedRaw[p.runtimeEntityId] = p.item;
            entities.AddItem(p.runtimeEntityId, p.entityIdSelf, {p.position.x, p.position.y, p.position.z},
                             it->second, p.item.count, static_cast<int>(p.item.metadata),
                             p.item.blockRuntimeId, false);
            entities.SetMotion(p.runtimeEntityId, {p.velocity.x, p.velocity.y, p.velocity.z});
        });
        On<SetEntityMotionPacket>([this](const SetEntityMotionPacket& p) {
            entities.SetMotion(p.runtimeEntityId, {p.velocity.x, p.velocity.y, p.velocity.z});
        });

        On<TakeItemEntityPacket>([this](const TakeItemEntityPacket& p) {
            droppedRaw.erase(p.runtimeEntityId);
            entity::Entity taken;
            const bool found = entities.Find(p.runtimeEntityId, taken);
            if (found && taken.type != "minecraft:xp_orb") {
                // Servers send no sound for a pickup: the client plays the pop for every collector.
                SoundRequest r;
                r.kind = SoundRequest::Kind::Named;
                r.name = "pop";
                r.x = static_cast<float>(taken.pos.x);
                r.y = static_cast<float>(taken.pos.y);
                r.z = static_cast<float>(taken.pos.z);
                PushSound(std::move(r));
            } else if (found && runtimeId != 0 && p.target == runtimeId) {
                SoundRequest r;
                r.kind = SoundRequest::Kind::Named;
                r.name = "random.orb";
                r.x = static_cast<float>(taken.pos.x);
                r.y = static_cast<float>(taken.pos.y);
                r.z = static_cast<float>(taken.pos.z);
                r.volume = 0.35f;
                r.pitch = 1.0f + (static_cast<float>(taken.runtimeId % 5) * 0.03f);
                PushSound(std::move(r));
                lastLocalOrbMs =
                    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now().time_since_epoch())
                                              .count());
                lastOrbX = r.x;
                lastOrbY = r.y;
                lastOrbZ = r.z;
            }
            entities.Pickup(p.runtimeEntityId, p.target);
        });

        On<RemoveEntityPacket>([this](const RemoveEntityPacket& p) { entities.Remove(p.entityIdSelf); });

        On<MoveEntityPacket>([this](const MoveEntityPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId)
                return;
            if (p.runtimeEntityId == predictedVehicleRuntime.load())
                return;
            constexpr float kByte = 360.0f / 256.0f;
            entities.Move(p.runtimeEntityId, {p.position.x, p.position.y, p.position.z},
                          p.rotation.pitch * kByte, p.rotation.yaw * kByte, p.rotation.headYaw * kByte,
                          (p.flags & 1) != 0,
                          (p.flags & 2) != 0 ? entity::EntityTracker::MoveKind::Teleport
                                             : entity::EntityTracker::MoveKind::Lerp);
        });

        On<MoveEntityDeltaPacket>([this](const MoveEntityDeltaPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId)
                return;
            if (p.runtimeEntityId == predictedVehicleRuntime.load())
                return;
            constexpr float kByte = 360.0f / 256.0f;
            double x = 0, y = 0, z = 0;
            float pitch = 0, yaw = 0, head = 0;
            if (p.x)
                x = *p.x;
            if (p.y)
                y = *p.y;
            if (p.z)
                z = *p.z;
            if (p.rotX)
                pitch = *p.rotX * kByte;
            if (p.rotY)
                yaw = *p.rotY * kByte;
            if (p.rotZ)
                head = *p.rotZ * kByte;
            entities.MoveDelta(p.runtimeEntityId, p.x ? &x : nullptr, p.y ? &y : nullptr, p.z ? &z : nullptr,
                               p.rotX ? &pitch : nullptr, p.rotY ? &yaw : nullptr, p.rotZ ? &head : nullptr,
                               p.onGround, p.forceMove, p.ticks, p.forceCompletion);
        });

        On<MovePlayerPacket>([this](const MovePlayerPacket& p) {
            if (runtimeId != 0 && p.runtimeId == runtimeId)
                return;
            if (!SanePlayerMove(p))
                return;
            if (p.mode == MovePlayerPacketMode::Rotation) {
                entities.RotateHead(p.runtimeId, p.pitch, p.headYaw, p.onGround);
                return;
            }
            entities.Move(p.runtimeId, {p.position.x, p.position.y, p.position.z}, p.pitch, p.yaw, p.headYaw,
                          p.onGround,
                          p.mode == MovePlayerPacketMode::Normal
                              ? entity::EntityTracker::MoveKind::Lerp
                              : entity::EntityTracker::MoveKind::Teleport);
        });

        On<SetEntityDataPacket>([this](const SetEntityDataPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId) {
                std::lock_guard lk(attrMutex);
                for (const MetadataDictionaryEntry& m : p.metadata) {
                    int64_t integer = 0;
                    if (m.key == MetadataDictionaryEntryKey::Flags ||
                        m.key == MetadataDictionaryEntryKey::FlagsExtended) {
                        if (MetaInt(m, integer)) {
                            const size_t word = m.key == MetadataDictionaryEntryKey::Flags ? 0 : 1;
                            ownFlagWords[word] = static_cast<uint64_t>(integer);
                            ownFlagWordsKnown[word] = true;
                        }
                        continue;
                    }
                    if (m.key == MetadataDictionaryEntryKey::RiderSeatPosition) {
                        if (const auto* inner = std::get_if<4>(&m.value))
                            if (const auto* seat = std::get_if<9>(inner))
                                ownSeatOffset = std::array<float, 3>{seat->x, seat->y, seat->z};
                        continue;
                    }
                    if (m.key == MetadataDictionaryEntryKey::Nametag) {
                        if (const auto* inner = std::get_if<4>(&m.value))
                            if (const auto* str = std::get_if<5>(inner))
                                ownNameTag = *str;
                        continue;
                    }
                    double n = 0;
                    if (!MetaNumber(m, n))
                        continue;
                    if (m.key == MetadataDictionaryEntryKey::PlayerFlags)
                        ownPlayerFlags = static_cast<int64_t>(n);
                    else if (m.key == MetadataDictionaryEntryKey::FreezingEffectStrength)
                        freezeUpdates.push_back({p.tick, static_cast<float>(n)});
                    else if (m.key == MetadataDictionaryEntryKey::Scale)
                        ownScale = static_cast<float>(n);
                    else if (m.key == MetadataDictionaryEntryKey::BoundingboxWidth)
                        ownWidth = static_cast<float>(n);
                    else if (m.key == MetadataDictionaryEntryKey::BoundingboxHeight)
                        ownHeight = static_cast<float>(n);
                }
                return;
            }
            ApplyMetadata(entities, p.runtimeEntityId, p.metadata);
            entity::Entity target;
            if (entities.Find(p.runtimeEntityId, target))
                ApplyEntityProperties(p.runtimeEntityId, target.type, p.properties);
        });
    }
}
