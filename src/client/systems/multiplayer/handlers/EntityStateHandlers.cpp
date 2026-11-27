#include "client/systems/multiplayer/handlers/HandlerSupport.hpp"

namespace perlin {
    using namespace proto;
    using namespace handlers;

    void BedrockSession::RegisterEntityStateHandlers() {
        On<PlayerListPacket>([this](const PlayerListPacket& p) {
            for (const PlayerRecord& r : p.records) {
                const std::string key = UuidKey(r.uuid);
                if (r.type == PlayerRecordType::Add) {
                    auto skin = ConvertSkin(r.skinData);
                    entities.SetSkin(key, skin);
                    std::lock_guard<std::mutex> lk(tabMutex);
                    bool found = false;
                    for (TabPlayer& t : tabPlayers)
                        if (t.uuid == key) {
                            t.name = r.username;
                            t.skin = skin;
                            found = true;
                        }
                    if (!found) {
                        TabPlayer tp{key, r.username, skin};
                        tp.uid = r.entityUniqueId;
                        tabPlayers.push_back(std::move(tp));
                    }
                    for (TabPlayer& t : tabPlayers)
                        if (t.uuid == key) {
                            t.uid = r.entityUniqueId;
                            if (const auto pm = permByUid.find(t.uid); pm != permByUid.end())
                                t.perm = pm->second;
                        }
                } else {
                    entities.RemoveSkin(key);
                    std::lock_guard<std::mutex> lk(tabMutex);
                    tabPlayers.erase(std::remove_if(tabPlayers.begin(), tabPlayers.end(),
                                                    [&](const TabPlayer& t) { return t.uuid == key; }),
                                     tabPlayers.end());
                }
            }
        });

        On<PlayerSkinPacket>([this](const PlayerSkinPacket& p) {
            const std::string key = UuidKey(p.uuid);
            auto skin = ConvertSkin(p.skin);
            entities.SetSkin(key, skin, false);
            std::lock_guard<std::mutex> lk(tabMutex);
            for (TabPlayer& t : tabPlayers)
                if (t.uuid == key)
                    t.skin = skin;
        });

        On<MobEquipmentPacket>([this](const MobEquipmentPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId)
                return;
            std::string name;
            if (const auto it = itemNames.find(p.item.networkId);
                p.item.networkId != 0 && it != itemNames.end())
                name = it->second;
            if (static_cast<int>(p.windowId) == 119)
                entities.SetOffHand(p.runtimeEntityId, name);
            else
                entities.SetHeld(p.runtimeEntityId, name);
        });

        On<MobArmorEquipmentPacket>([this](const MobArmorEquipmentPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId)
                return;
            const auto nameOf = [&](const auto& item) -> std::string {
                if (const auto it = itemNames.find(item.networkId);
                    item.networkId != 0 && it != itemNames.end())
                    return it->second;
                return {};
            };
            entities.SetArmor(p.runtimeEntityId, 0, nameOf(p.helmet));
            entities.SetArmor(p.runtimeEntityId, 1, nameOf(p.chestplate));
            entities.SetArmor(p.runtimeEntityId, 2, nameOf(p.leggings));
            entities.SetArmor(p.runtimeEntityId, 3, nameOf(p.boots));
        });

        On<AnimatePacket>([this](const AnimatePacket& p) {
            if (p.actionId == AnimatePacketActionId::SwingArm &&
                !(runtimeId != 0 && p.runtimeEntityId == runtimeId))
                entities.Swing(p.runtimeEntityId);
            if (p.actionId == AnimatePacketActionId::CriticalHit ||
                p.actionId == AnimatePacketActionId::MagicCriticalHit) {
                entity::Entity e;
                if (!entities.Find(p.runtimeEntityId, e))
                    return;
                ParticleEvent pe;
                pe.name = p.actionId == AnimatePacketActionId::CriticalHit ? "minecraft:critical_hit_emitter"
                                                                           : "minecraft:magic_critical_hit_emitter";
                pe.x = e.pos.x;
                pe.y = e.pos.y + 1.0;
                pe.z = e.pos.z;
                PushParticle(std::move(pe));
            }
        });

        On<EntityEventPacket>([this](const EntityEventPacket& p) {
            if (p.eventId == EntityEventPacketEventId::HurtAnimation &&
                !(runtimeId != 0 && p.runtimeEntityId == runtimeId))
                entities.Hurt(p.runtimeEntityId);
            if (p.eventId == EntityEventPacketEventId::DeathAnimation &&
                !(runtimeId != 0 && p.runtimeEntityId == runtimeId))
                entities.Die(p.runtimeEntityId);

            if (p.eventId == EntityEventPacketEventId::ArmSwing &&
                !(runtimeId != 0 && p.runtimeEntityId == runtimeId))
                entities.Swing(p.runtimeEntityId);
        });

        On<UpdateAttributesPacket>([this](const UpdateAttributesPacket& p) {
            if (runtimeId == 0 || p.runtimeEntityId != runtimeId) {
                for (const PlayerAttributesEntry& a : p.attributes) {
                    entities.SetAttribute(p.runtimeEntityId, a.name, a.min, a.current, a.max);
                    if (a.name == "minecraft:health")
                        entities.SetHealth(p.runtimeEntityId, a.current);
                }
                return;
            }
            {
                std::lock_guard lk(attrMutex);
                if (!p.attributes.empty())
                    pendingAttributeUpdates.push_back({p.tick, p.attributes});
                for (const PlayerAttributesEntry& a : p.attributes) {
                    // The movement consumer compares stamped values with its
                    // saved frame before deciding whether live prediction
                    // should be replaced. Health and other actor data remain
                    // immediately available to their existing consumers.
                    if (!IsMovementAttribute(a.name))
                        ownAttributes[a.name] = a.current;
                }
            }
            for (const PlayerAttributesEntry& a : p.attributes) {
                if (a.name == "minecraft:health") {
                    const bool recent =
                        std::chrono::steady_clock::now() - lastHurtAt < std::chrono::milliseconds(250);
                    if (lastHealth >= 0.0f && a.current < lastHealth - 0.01f && a.current > 0.0f && !recent)
                        NoteHurt();
                    lastHealth = a.current;
                    std::lock_guard<std::mutex> lk(deathMutex);
                    if (a.current > 0.0f && deathPending) {
                        deathPending = false;
                        deathCause.clear();
                    } else if (a.current <= 0.0f) {
                        deathPending = true;
                    }
                }
            }
        });

        On<EntityEventPacket>([this](const EntityEventPacket& p) {
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId &&
                p.eventId == EntityEventPacketEventId::ChargedItem)
                chargedItemUseReset = true;
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId &&
                p.eventId == EntityEventPacketEventId::HurtAnimation)
                NoteHurt();
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId &&
                p.eventId == EntityEventPacketEventId::DeathAnimation) {
                Log("bedrock", "own death animation");
                std::lock_guard<std::mutex> lk(deathMutex);
                if (lastHealth <= 0.01f)
                    deathPending = true;
            }
        });

        On<EntityEventPacket>([this](const EntityEventPacket& p) {
            const char* event = nullptr;
            const char* direct = nullptr;
            switch (p.eventId) {
            case EntityEventPacketEventId::HurtAnimation:
                event = "hurt";
                break;
            case EntityEventPacketEventId::DeathAnimation:
                event = "death";
                break;
            case EntityEventPacketEventId::ElderGuardianCurse:
                event = "elderguardian.curse";
                break;
            case EntityEventPacketEventId::EatingItem:
                event = "eat";
                break;
            case EntityEventPacketEventId::WitchDrinkPotion:
                event = "drink";
                break;
            case EntityEventPacketEventId::PlayerAddXpLevels:
                direct = "random.levelup";
                break;
            case EntityEventPacketEventId::CreeperPrimeFuse:
                direct = "random.fuse";
                break;
            case EntityEventPacketEventId::ConsumeTotem:
                direct = "random.totem";
                break;
            default:
                break;
            }
            if (!event && !direct)
                return;
            SoundRequest r;
            const bool self = runtimeId != 0 && p.runtimeEntityId == runtimeId;
            if (self && (p.eventId == EntityEventPacketEventId::EatingItem))
                return;
            if (self) {
                r.entityType = "minecraft:player";
                r.x = pose.x;
                r.y = pose.y;
                r.z = pose.z;
            } else {
                entity::Entity e;
                if (!entities.Find(p.runtimeEntityId, e))
                    return;
                r.entityType = e.type;
                r.x = static_cast<float>(e.pos.x);
                r.y = static_cast<float>(e.pos.y);
                r.z = static_cast<float>(e.pos.z);
                r.baby = e.Has(entity::flag::Baby);
            }
            if (direct) {
                r.kind = SoundRequest::Kind::Named;
                r.name = direct;
                r.entityType.clear();
            } else {
                r.kind = SoundRequest::Kind::Level;
                r.name = event;
            }
            PushSound(std::move(r));
        });

        On<SpawnParticleEffectPacket>([this](const SpawnParticleEffectPacket& p) {
            ParticleEvent pe;
            pe.name = p.particleName;
            if (p.molangVariables)
                pe.molang = *p.molangVariables;
            pe.x = p.position.x;
            pe.y = p.position.y;
            pe.z = p.position.z;
            PushParticle(std::move(pe));
        });

        On<EntityEventPacket>([this](const EntityEventPacket& p) {
            if (p.eventId != EntityEventPacketEventId::ConsumeTotem)
                return;
            glm::dvec3 at{pose.x, pose.y + 0.9, pose.z};
            if (runtimeId != 0 && p.runtimeEntityId == runtimeId) {
                std::lock_guard<std::mutex> lk(worldMutex);
                screenAnimation = {kTotemScreenAnimation, std::chrono::steady_clock::now()};
            } else {
                entity::Entity e;
                if (!entities.Find(p.runtimeEntityId, e))
                    return;
                at = {e.pos.x, e.pos.y + 0.9, e.pos.z};
            }
            ParticleEvent pe;
            pe.event = 20065;
            pe.x = at.x;
            pe.y = at.y;
            pe.z = at.z;
            PushParticle(std::move(pe));
        });

        On<SetEntityLinkPacket>([this](const SetEntityLinkPacket& p) {
            const int64_t rider =
                p.link.riderEntityId == 0 ? static_cast<int64_t>(uniqueId) : p.link.riderEntityId;
            entities.SetVehicle(rider, p.link.type == 0 ? 0 : p.link.riddenEntityId);
            if (rider == static_cast<int64_t>(uniqueId)) {
                ridingUid = p.link.type == 0 ? 0 : p.link.riddenEntityId;
                ridingDriver = p.link.type == 1;
            }
            Log("bedrock", "link rider={} vehicle={} type={}", rider, p.link.riddenEntityId,
                int(p.link.type));
        });
    }
}
