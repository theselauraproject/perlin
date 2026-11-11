#pragma once
#include "perlin/host.hpp"

namespace perlin::abi::host {
    template <class Ctx> class ServerService {
    public:
        virtual ~ServerService() = default;

        virtual Result<void> OpLog(Ctx&, const LogRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<int64_t> OpClock(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpCommandRegister(Ctx&, const CommandRegisterRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpCommandUnregister(Ctx&, const CommandUnregisterRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<RestBytes> OpStorageRead(Ctx&, const StorageReadRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpStorageWrite(Ctx&, const StorageWriteRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<StorageListResponse> OpStorageList(Ctx&, const StorageListRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpStorageRemove(Ctx&, const StorageRemoveRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<bool> OpStorageExists(Ctx&, const StorageExistsRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpStorageMakeDir(Ctx&, const StorageMakeDirRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpHttpRequest(Ctx&, const HttpRequestRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<RestText> OpSettingGet(Ctx&, const SettingGetRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpSettingSet(Ctx&, const SettingSetRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<SettingListResponse> OpSettingList(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<ModListResponse> OpModList(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpModSend(Ctx&, const ModMessageData&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpBroadcast(Ctx&, const TextMessage&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerMessage(Ctx&, const PlayerText&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpKick(Ctx&, const PlayerText&) { return std::unexpected(Status::Unavailable); }
        virtual Result<OnlinePlayerList> OpOnlinePlayers(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpTeleport(Ctx&, const ServerTeleport&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpSetBlock(Ctx&, const BlockWrite&) { return std::unexpected(Status::Unavailable); }
        virtual Result<TextMessage> OpBlockName(Ctx&, const BlockCoord&) { return std::unexpected(Status::Unavailable); }
        virtual Result<ServerStatus> OpServerInfo(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<GiveItemResult> OpGiveItem(Ctx&, const GiveItemRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<PlayerDetails> OpPlayerInfo(Ctx&, const PlayerInfoRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerSetGameMode(Ctx&, const PlayerSetGameModeRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerSetHealth(Ctx&, const PlayerSetHealthRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<bool> OpPlayerDamage(Ctx&, const PlayerDamageRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerKill(Ctx&, const PlayerKillRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerSetFood(Ctx&, const PlayerSetFoodRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerAddExperience(Ctx&, const PlayerAddExperienceRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerEffectAdd(Ctx&, const PlayerEffectAddRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerEffectRemove(Ctx&, const PlayerEffectRemoveRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<ServerInventory> OpPlayerInventory(Ctx&, const PlayerInventoryRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerSetSlot(Ctx&, const PlayerSetSlotRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerClearInventory(Ctx&, const PlayerClearInventoryRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerSetVelocity(Ctx&, const PlayerSetVelocityRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerTransferDimension(Ctx&, const PlayerTransferDimensionRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerTitle(Ctx&, const PlayerTitleRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerActionBar(Ctx&, const PlayerActionBarRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerSound(Ctx&, const PlayerSoundRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerTransfer(Ctx&, const PlayerTransferRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerSetOperator(Ctx&, const PlayerSetOperatorRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<uint32_t> OpPlayerForm(Ctx&, const PlayerFormRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerRunCommand(Ctx&, const PlayerRunCommandRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerSetSpawn(Ctx&, const PlayerSetSpawnRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<uint64_t> OpPlayerByName(Ctx&, const PlayerByNameRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<BlockInfo> OpBlockState(Ctx&, const BlockStateRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpBlockStateSet(Ctx&, const BlockStateSetRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<uint32_t> OpFill(Ctx&, const FillRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<TimeGetResponse> OpTimeGet(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpTimeSet(Ctx&, const TimeSetRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpWeatherSet(Ctx&, const WeatherSetRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<int32_t> OpGameRuleGet(Ctx&, const GameRuleGetRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpGameRuleSet(Ctx&, const GameRuleSetRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpDifficultySet(Ctx&, const DifficultySetRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpExplode(Ctx&, const ExplodeRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<int32_t> OpHighestBlock(Ctx&, const HighestBlockRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpParticleAt(Ctx&, const ParticleAtRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpDropItem(Ctx&, const DropItemRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<BlockCoord> OpWorldSpawnGet(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpWorldSpawnSet(Ctx&, const BlockCoord&) { return std::unexpected(Status::Unavailable); }
        virtual Result<uint64_t> OpEntitySpawn(Ctx&, const EntitySpawnRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<EntityListResponse> OpEntityList(Ctx&, const EntityListRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpEntityRemove(Ctx&, const EntityRemoveRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<bool> OpEntityDamage(Ctx&, const EntityDamageRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpEntityTeleport(Ctx&, const EntityTeleportRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpEntitySetVelocity(Ctx&, const EntitySetVelocityRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<ServerEntity> OpEntityInfo(Ctx&, const EntityInfoRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpEntitySetHealth(Ctx&, const EntitySetHealthRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<RestText> OpRunCommand(Ctx&, const RunCommandRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpMotdSet(Ctx&, const MotdSetRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpLootTableOverride(Ctx&, const LootTableOverrideRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<RestText> OpLootTableGet(Ctx&, const LootTableGetRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpEventAmend(Ctx&, const EventAmendRequest&) { return std::unexpected(Status::Unavailable); }

        void Bind(OpTable<Ctx>& table) {
            table.template Handle<Op::Log>([this](Ctx& c, const LogRequest& r) { return OpLog(c, r); });
            table.template Handle<Op::Clock>([this](Ctx& c) { return OpClock(c); });
            table.template Handle<Op::CommandRegister>([this](Ctx& c, const CommandRegisterRequest& r) { return OpCommandRegister(c, r); });
            table.template Handle<Op::CommandUnregister>([this](Ctx& c, const CommandUnregisterRequest& r) { return OpCommandUnregister(c, r); });
            table.template Handle<Op::StorageRead>([this](Ctx& c, const StorageReadRequest& r) { return OpStorageRead(c, r); });
            table.template Handle<Op::StorageWrite>([this](Ctx& c, const StorageWriteRequest& r) { return OpStorageWrite(c, r); });
            table.template Handle<Op::StorageList>([this](Ctx& c, const StorageListRequest& r) { return OpStorageList(c, r); });
            table.template Handle<Op::StorageRemove>([this](Ctx& c, const StorageRemoveRequest& r) { return OpStorageRemove(c, r); });
            table.template Handle<Op::StorageExists>([this](Ctx& c, const StorageExistsRequest& r) { return OpStorageExists(c, r); });
            table.template Handle<Op::StorageMakeDir>([this](Ctx& c, const StorageMakeDirRequest& r) { return OpStorageMakeDir(c, r); });
            table.template Handle<Op::HttpRequest>([this](Ctx& c, const HttpRequestRequest& r) { return OpHttpRequest(c, r); });
            table.template Handle<Op::SettingGet>([this](Ctx& c, const SettingGetRequest& r) { return OpSettingGet(c, r); });
            table.template Handle<Op::SettingSet>([this](Ctx& c, const SettingSetRequest& r) { return OpSettingSet(c, r); });
            table.template Handle<Op::SettingList>([this](Ctx& c) { return OpSettingList(c); });
            table.template Handle<Op::ModList>([this](Ctx& c) { return OpModList(c); });
            table.template Handle<Op::ModSend>([this](Ctx& c, const ModMessageData& r) { return OpModSend(c, r); });
            table.template Handle<Op::Broadcast>([this](Ctx& c, const TextMessage& r) { return OpBroadcast(c, r); });
            table.template Handle<Op::PlayerMessage>([this](Ctx& c, const PlayerText& r) { return OpPlayerMessage(c, r); });
            table.template Handle<Op::Kick>([this](Ctx& c, const PlayerText& r) { return OpKick(c, r); });
            table.template Handle<Op::OnlinePlayers>([this](Ctx& c) { return OpOnlinePlayers(c); });
            table.template Handle<Op::Teleport>([this](Ctx& c, const ServerTeleport& r) { return OpTeleport(c, r); });
            table.template Handle<Op::SetBlock>([this](Ctx& c, const BlockWrite& r) { return OpSetBlock(c, r); });
            table.template Handle<Op::BlockName>([this](Ctx& c, const BlockCoord& r) { return OpBlockName(c, r); });
            table.template Handle<Op::ServerInfo>([this](Ctx& c) { return OpServerInfo(c); });
            table.template Handle<Op::GiveItem>([this](Ctx& c, const GiveItemRequest& r) { return OpGiveItem(c, r); });
            table.template Handle<Op::PlayerInfo>([this](Ctx& c, const PlayerInfoRequest& r) { return OpPlayerInfo(c, r); });
            table.template Handle<Op::PlayerSetGameMode>([this](Ctx& c, const PlayerSetGameModeRequest& r) { return OpPlayerSetGameMode(c, r); });
            table.template Handle<Op::PlayerSetHealth>([this](Ctx& c, const PlayerSetHealthRequest& r) { return OpPlayerSetHealth(c, r); });
            table.template Handle<Op::PlayerDamage>([this](Ctx& c, const PlayerDamageRequest& r) { return OpPlayerDamage(c, r); });
            table.template Handle<Op::PlayerKill>([this](Ctx& c, const PlayerKillRequest& r) { return OpPlayerKill(c, r); });
            table.template Handle<Op::PlayerSetFood>([this](Ctx& c, const PlayerSetFoodRequest& r) { return OpPlayerSetFood(c, r); });
            table.template Handle<Op::PlayerAddExperience>([this](Ctx& c, const PlayerAddExperienceRequest& r) { return OpPlayerAddExperience(c, r); });
            table.template Handle<Op::PlayerEffectAdd>([this](Ctx& c, const PlayerEffectAddRequest& r) { return OpPlayerEffectAdd(c, r); });
            table.template Handle<Op::PlayerEffectRemove>([this](Ctx& c, const PlayerEffectRemoveRequest& r) { return OpPlayerEffectRemove(c, r); });
            table.template Handle<Op::PlayerInventory>([this](Ctx& c, const PlayerInventoryRequest& r) { return OpPlayerInventory(c, r); });
            table.template Handle<Op::PlayerSetSlot>([this](Ctx& c, const PlayerSetSlotRequest& r) { return OpPlayerSetSlot(c, r); });
            table.template Handle<Op::PlayerClearInventory>([this](Ctx& c, const PlayerClearInventoryRequest& r) { return OpPlayerClearInventory(c, r); });
            table.template Handle<Op::PlayerSetVelocity>([this](Ctx& c, const PlayerSetVelocityRequest& r) { return OpPlayerSetVelocity(c, r); });
            table.template Handle<Op::PlayerTransferDimension>([this](Ctx& c, const PlayerTransferDimensionRequest& r) { return OpPlayerTransferDimension(c, r); });
            table.template Handle<Op::PlayerTitle>([this](Ctx& c, const PlayerTitleRequest& r) { return OpPlayerTitle(c, r); });
            table.template Handle<Op::PlayerActionBar>([this](Ctx& c, const PlayerActionBarRequest& r) { return OpPlayerActionBar(c, r); });
            table.template Handle<Op::PlayerSound>([this](Ctx& c, const PlayerSoundRequest& r) { return OpPlayerSound(c, r); });
            table.template Handle<Op::PlayerTransfer>([this](Ctx& c, const PlayerTransferRequest& r) { return OpPlayerTransfer(c, r); });
            table.template Handle<Op::PlayerSetOperator>([this](Ctx& c, const PlayerSetOperatorRequest& r) { return OpPlayerSetOperator(c, r); });
            table.template Handle<Op::PlayerForm>([this](Ctx& c, const PlayerFormRequest& r) { return OpPlayerForm(c, r); });
            table.template Handle<Op::PlayerRunCommand>([this](Ctx& c, const PlayerRunCommandRequest& r) { return OpPlayerRunCommand(c, r); });
            table.template Handle<Op::PlayerSetSpawn>([this](Ctx& c, const PlayerSetSpawnRequest& r) { return OpPlayerSetSpawn(c, r); });
            table.template Handle<Op::PlayerByName>([this](Ctx& c, const PlayerByNameRequest& r) { return OpPlayerByName(c, r); });
            table.template Handle<Op::BlockState>([this](Ctx& c, const BlockStateRequest& r) { return OpBlockState(c, r); });
            table.template Handle<Op::BlockStateSet>([this](Ctx& c, const BlockStateSetRequest& r) { return OpBlockStateSet(c, r); });
            table.template Handle<Op::Fill>([this](Ctx& c, const FillRequest& r) { return OpFill(c, r); });
            table.template Handle<Op::TimeGet>([this](Ctx& c) { return OpTimeGet(c); });
            table.template Handle<Op::TimeSet>([this](Ctx& c, const TimeSetRequest& r) { return OpTimeSet(c, r); });
            table.template Handle<Op::WeatherSet>([this](Ctx& c, const WeatherSetRequest& r) { return OpWeatherSet(c, r); });
            table.template Handle<Op::GameRuleGet>([this](Ctx& c, const GameRuleGetRequest& r) { return OpGameRuleGet(c, r); });
            table.template Handle<Op::GameRuleSet>([this](Ctx& c, const GameRuleSetRequest& r) { return OpGameRuleSet(c, r); });
            table.template Handle<Op::DifficultySet>([this](Ctx& c, const DifficultySetRequest& r) { return OpDifficultySet(c, r); });
            table.template Handle<Op::Explode>([this](Ctx& c, const ExplodeRequest& r) { return OpExplode(c, r); });
            table.template Handle<Op::HighestBlock>([this](Ctx& c, const HighestBlockRequest& r) { return OpHighestBlock(c, r); });
            table.template Handle<Op::ParticleAt>([this](Ctx& c, const ParticleAtRequest& r) { return OpParticleAt(c, r); });
            table.template Handle<Op::DropItem>([this](Ctx& c, const DropItemRequest& r) { return OpDropItem(c, r); });
            table.template Handle<Op::WorldSpawnGet>([this](Ctx& c) { return OpWorldSpawnGet(c); });
            table.template Handle<Op::WorldSpawnSet>([this](Ctx& c, const BlockCoord& r) { return OpWorldSpawnSet(c, r); });
            table.template Handle<Op::EntitySpawn>([this](Ctx& c, const EntitySpawnRequest& r) { return OpEntitySpawn(c, r); });
            table.template Handle<Op::EntityList>([this](Ctx& c, const EntityListRequest& r) { return OpEntityList(c, r); });
            table.template Handle<Op::EntityRemove>([this](Ctx& c, const EntityRemoveRequest& r) { return OpEntityRemove(c, r); });
            table.template Handle<Op::EntityDamage>([this](Ctx& c, const EntityDamageRequest& r) { return OpEntityDamage(c, r); });
            table.template Handle<Op::EntityTeleport>([this](Ctx& c, const EntityTeleportRequest& r) { return OpEntityTeleport(c, r); });
            table.template Handle<Op::EntitySetVelocity>([this](Ctx& c, const EntitySetVelocityRequest& r) { return OpEntitySetVelocity(c, r); });
            table.template Handle<Op::EntityInfo>([this](Ctx& c, const EntityInfoRequest& r) { return OpEntityInfo(c, r); });
            table.template Handle<Op::EntitySetHealth>([this](Ctx& c, const EntitySetHealthRequest& r) { return OpEntitySetHealth(c, r); });
            table.template Handle<Op::RunCommand>([this](Ctx& c, const RunCommandRequest& r) { return OpRunCommand(c, r); });
            table.template Handle<Op::MotdSet>([this](Ctx& c, const MotdSetRequest& r) { return OpMotdSet(c, r); });
            table.template Handle<Op::LootTableOverride>([this](Ctx& c, const LootTableOverrideRequest& r) { return OpLootTableOverride(c, r); });
            table.template Handle<Op::LootTableGet>([this](Ctx& c, const LootTableGetRequest& r) { return OpLootTableGet(c, r); });
            table.template Handle<Op::EventAmend>([this](Ctx& c, const EventAmendRequest& r) { return OpEventAmend(c, r); });
        }
    };
}
