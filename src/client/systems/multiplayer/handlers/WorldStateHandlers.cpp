#include "client/systems/multiplayer/handlers/HandlerSupport.hpp"

namespace perlin {
    using namespace proto;
    using namespace handlers;

    void BedrockSession::RegisterWorldStateHandlers() {
        On<NetworkChunkPublisherUpdatePacket>([this](const NetworkChunkPublisherUpdatePacket& p) {
            const int cx = static_cast<int>(std::floor(static_cast<double>(p.coordinates.x) / 16.0));
            const int cz = static_cast<int>(std::floor(static_cast<double>(p.coordinates.z) / 16.0));
            const int radius = static_cast<int>((p.radius + 15) / 16);
            viewCenterChunk[0] = cx;
            viewCenterChunk[1] = cz;
            viewRadiusChunks = radius;
            static int logged = 0;
            if (logged++ < 6)
                Log("chunk", "publisher centre ({},{},{}) radius {} blocks, {} server-built chunks listed",
                    p.coordinates.x, p.coordinates.y, p.coordinates.z, p.radius, p.savedChunks.size());
            if (radius > 0)
                PruneChunks(cx, cz, radius + 2);
        });

        On<DimensionDataPacket>([](const DimensionDataPacket& p) {
            for (const DimensionDataPacketDefinitionsEntry& d : p.definitions) {
                const int id = d.id.contains("nether") ? 1 : d.id.contains("end") ? 2 : 0;
                Log("chunk", "dimension {} minY={} height={}", d.id, d.minY, d.heightRange);
                ChunkCodec::SetDimension(id, d.minY, d.heightRange);
            }
        });

        On<ChunkRadiusUpdatePacket>([this](const ChunkRadiusUpdatePacket& p) {
            grantedRadius = p.chunkRadius;
            Log("chunk", "server granted chunk radius {} (asked {})", p.chunkRadius, chunkRadius);
            if (p.chunkRadius < chunkRadius)
                Log("chunk",
                    "the server limits the view distance to {} chunks; LODs can only show terrain the server "
                    "sends",
                    p.chunkRadius);
        });

        On<ItemRegistryPacket>([this](const ItemRegistryPacket& p) {
            if (p.itemstates.empty()) {
                std::lock_guard lock(statusMutex);
                if (!itemNames.empty()) {
                    Log("items", "ignoring an empty item registry");
                    return;
                }
            }
            std::map<std::string, itemcooldown::Definition, std::less<>> cooldowns;
            Log("items", "server defines {} custom items", RegisterCustomItems(p.itemstates, cooldowns));
            {
                std::lock_guard lock(statusMutex);
                itemCooldownDefinitions = std::move(cooldowns);
                itemNames.clear();
                for (const ItemstatesEntry& item : p.itemstates)
                    itemNames[item.runtimeId] = item.name;
                const auto rename = [this](ItemStackInfo& info) {
                    if (info.name != "minecraft:unknown")
                        return;
                    if (const auto it = itemNames.find(info.networkId); it != itemNames.end())
                        info.name = it->second;
                };
                InventoryView& v = inventory;
                for (std::span<ItemStackInfo> group :
                     {std::span<ItemStackInfo>(v.hotbar), std::span<ItemStackInfo>(v.main),
                      std::span<ItemStackInfo>(v.armor), std::span<ItemStackInfo>(v.craft),
                      std::span<ItemStackInfo>(v.work), std::span<ItemStackInfo>(v.horse),
                      std::span<ItemStackInfo>(v.chest)})
                    for (ItemStackInfo& info : group)
                        rename(info);
                rename(v.offhand);
                rename(v.output);
                rename(v.cursor);
            }
            int32_t shield = -1;
            for (const ItemstatesEntry& item : p.itemstates)
                if (item.name == "minecraft:shield") {
                    shield = item.runtimeId;
                    break;
                }
            shieldItemId.store(shield, std::memory_order_relaxed);
            net::context::UpdateBoundShieldItemID(shield);
            net::context::SetShieldItemID(shield);
        });

        On<SetTimePacket>([this](const SetTimePacket& p) {
            Log("bedrock", "SetTime {}", p.time);
            NoteTime(p.time);
        });

        On<SyncWorldClocksPacket>([this](const SyncWorldClocksPacket& p) {
            constexpr uint64_t kOverworldClockId = 0x63d7ede73c38f916ULL;
            if (p.payloadType == SyncWorldClocksPacketPayloadType::InitializeRegistry) {
                for (const WorldClockData& clock : p.clocks)
                    if (clock.id == kOverworldClockId || clock.name == "minecraft:overworld") {
                        overworldClockId = clock.id;
                        Log("bedrock", "clock init {} time={} paused={}", clock.name, clock.time,
                            clock.paused);
                        NoteClock(clock.time, clock.paused);
                    }
            } else if (p.payloadType == SyncWorldClocksPacketPayloadType::SyncState) {
                for (const SyncWorldClockStateData& state : p.syncStates)
                    if (state.clockId == overworldClockId || state.clockId == kOverworldClockId) {
                        Log("bedrock", "clock sync time={} paused={}", state.time, state.paused);
                        NoteClock(state.time, state.paused);
                    }
            }
        });

        On<ChangeDimensionPacket>([this](const ChangeDimensionPacket& p) {
            entities.Clear();
            Log("bedrock", "ChangeDimension {} -> {} pos=({},{},{})", dimensionId.load(), p.dimension,
                p.position.x, p.position.y, p.position.z);
            dimensionId = p.dimension;
            ResetWorldForDimension();
            if (SaneWorldPos(p.position.x, p.position.y, p.position.z)) {
                pose = {p.position.x, p.position.y, p.position.z, pose.yaw,
                        pose.pitch,   pose.headYaw, false,        true};
                haveLastAuthPos = false;
                ServerMotion motion;
                motion.poseChanged = true;
                motion.teleport = true;
                motion.hasVelocity = true;
                QueueServerMotion(motion);
            }
            dimLoadingId = p.loadingScreenId;
            dimAckSince = std::chrono::steady_clock::now();
            dimAckPending = true;
            SendLoadingScreen(true, p.loadingScreenId);
        });

        On<PlaySoundPacket>([this](const PlaySoundPacket& p) {
            SoundRequest r;
            r.kind = SoundRequest::Kind::Named;
            r.name = p.name;
            r.x = static_cast<float>(p.coordinates.x) / 8.0f;
            r.y = static_cast<float>(p.coordinates.y) / 8.0f;
            r.z = static_cast<float>(p.coordinates.z) / 8.0f;
            r.volume = p.volume;
            r.pitch = p.pitch;
            r.bypassListenerRangeCheck = p.bypassListenerRangeCheck;
            r.serverInitiated = true;
            r.handle = p.handle;
            r.loopCount = p.loopCount;
            r.playbackPositionSeconds = p.playbackPositionSeconds;
            PushSound(std::move(r));
        });

        On<StopSoundPacket>([this](const StopSoundPacket& p) {
            SoundRequest r;
            r.kind = SoundRequest::Kind::Stop;
            r.name = p.name;
            r.stopAll = p.stopAll;
            PushSound(std::move(r));
        });

        On<LevelSoundEventPacket>([this](const LevelSoundEventPacket& p) {
            SoundRequest r;
            r.kind = SoundRequest::Kind::Level;
            r.name = p.soundId;
            r.x = p.position.x;
            r.y = p.position.y;
            r.z = p.position.z;
            r.extra = p.extraData;
            r.entityType = p.entityType;
            r.baby = p.isBabyMob;
            r.global = p.isGlobal;
            PushSound(std::move(r));
        });

        On<AvailableCommandsPacket>([this](const AvailableCommandsPacket& p) {
            using VT = AvailableCommandsPacketCommandDataEntryOverloadsEntryParametersEntryValueType;
            std::vector<ui::CommandInfo> list;
            list.reserve(p.commandData.size());
            for (const auto& c : p.commandData) {
                ui::CommandInfo info;
                info.name = c.name;
                info.description = c.description;
                for (const auto& o : c.overloads) {
                    std::vector<ui::CommandParam> params;
                    for (const auto& a : o.parameters) {
                        ui::CommandParam cp;
                        cp.name = a.parameterName;
                        cp.optional = a.optional;
                        const uint16_t flags = static_cast<uint16_t>(a.enumType);
                        const size_t index = static_cast<uint16_t>(a.valueType);
                        if (flags ==
                            static_cast<uint16_t>(
                                AvailableCommandsPacketCommandDataEntryOverloadsEntryParametersEntryEnumType::
                                    SoftEnum)) {
                            if (index < p.dynamicEnums.size()) {
                                cp.values = p.dynamicEnums[index].values;
                                cp.softEnum = p.dynamicEnums[index].name;
                            }
                        } else if (flags & 0x20) {
                            if (index < p.enums.size())
                                for (const uint32_t v : p.enums[index].values)
                                    if (v < p.enumValues.size())
                                        cp.values.push_back(p.enumValues[v]);
                        } else if (a.valueType == VT::Target || a.valueType == VT::WildcardTarget) {
                            cp.target = true;
                        }
                        params.push_back(std::move(cp));
                    }
                    info.overloads.push_back(std::move(params));
                }
                list.push_back(std::move(info));
            }
            std::lock_guard lock(commandMutex);
            commands = std::move(list);
            commandVersion++;
        });
    }
}
