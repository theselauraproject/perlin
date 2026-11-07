#pragma once
#include "perlin/host.hpp"

namespace perlin::abi::host {
    template <class Ctx> class ClientService {
    public:
        virtual ~ClientService() = default;

        virtual Result<void> OpLog(Ctx&, const LogRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<Meta> OpMeta(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<int64_t> OpClock(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<PlayerState> OpPlayerState(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<PlayerEffectsResponse> OpPlayerEffects(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<PlayerLookResponse> OpPlayerLook(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<Inventory> OpInventory(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpInventorySelect(Ctx&, const InventorySelectRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpInventoryDrop(Ctx&, const InventoryDropRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpInventorySwap(Ctx&, const InventorySwapRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpInventoryClick(Ctx&, const InventoryClickRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpInventoryOpen(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpInventoryClose(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<WorldState> OpWorldState(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<BlockInfo> OpBlock(Ctx&, const BlockRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<Region> OpRegion(Ctx&, const RegionRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<RayHit> OpRaycast(Ctx&, const RaycastRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<AimState> OpAim(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<FindBlocksResponse> OpFindBlocks(Ctx&, const FindBlocksRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<bool> OpChunkLoaded(Ctx&, const ChunkLoadedRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<BlockNamesResponse> OpBlockNames(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<BlockProps> OpBlockProps(Ctx&, const BlockPropsRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<BlockEntityInfo> OpBlockEntityData(Ctx&, const BlockEntityDataRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<RestText> OpDisplayName(Ctx&, const DisplayNameRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<BreakState> OpBreakProgress(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<RestBytes> OpMapChunk(Ctx&, const MapChunkRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<EntitiesResponse> OpEntities(Ctx&, const EntitiesRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<Entity> OpEntityById(Ctx&, const EntityByIdRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<PlayerListResponse> OpPlayerList(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<RestText> OpOptions(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<RestText> OpOptionGet(Ctx&, const OptionGetRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpOptionSet(Ctx&, const OptionSetRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<ScreenState> OpScreen(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<RestText> OpTranslate(Ctx&, const TranslateRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpReloadResources(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<Perspective> OpPerspectiveGet(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPerspectiveSet(Ctx&, const PerspectiveSetRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<ResourcePacksResponse> OpResourcePacks(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<bool> OpInputDown(Ctx&, const InputDownRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpInputOverride(Ctx&, const InputOverrideState&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpKeyRegister(Ctx&, const KeyRegisterRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<InputActionsResponse> OpInputActions(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<MousePositionResponse> OpMousePosition(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpChat(Ctx&, const ChatRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpCommand(Ctx&, const CommandRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpLocalMessage(Ctx&, const LocalMessageRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpCommandRegister(Ctx&, const CommandRegisterRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpCommandUnregister(Ctx&, const CommandUnregisterRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<ServerCommandsResponse> OpServerCommands(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpToast(Ctx&, const ToastRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpTitle(Ctx&, const TitleRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<ChatHistoryResponse> OpChatHistory(Ctx&, const ChatHistoryRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<TitleStateData> OpTitleState(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpSwingArm(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpUseItem(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpUseOnBlock(Ctx&, const UseOnBlockRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpAttackEntity(Ctx&, const AttackEntityRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpInteractEntity(Ctx&, const InteractEntityRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpBreakBlock(Ctx&, const BreakBlockRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpLook(Ctx&, const LookRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpRespawn(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpDisconnect(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlaySound(Ctx&, const PlaySoundRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpJump(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpUiSubmit(Ctx&, const UiSubmitRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpUiClear(Ctx&, const UiClearRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<float> OpUiMeasure(Ctx&, const UiMeasureRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<uint32_t> OpGfxTextureCreate(Ctx&, const GfxTextureCreateRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpGfxTextureUpdate(Ctx&, const GfxTextureUpdateRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpGfxTextureDestroy(Ctx&, const GfxTextureDestroyRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpGfxSubmit(Ctx&, const GfxSubmitRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpGfxClear(Ctx&, const GfxClearRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<GfxDisplayInfo> OpGfxDisplay(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<GfxImageLoadResponse> OpGfxImageLoad(Ctx&, const GfxImageLoadRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpInputGrab(Ctx&, const InputGrabRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<NativeHandles> OpNativeInfo(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<RestBytes> OpStorageRead(Ctx&, const StorageReadRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpStorageWrite(Ctx&, const StorageWriteRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<StorageListResponse> OpStorageList(Ctx&, const StorageListRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpStorageRemove(Ctx&, const StorageRemoveRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<bool> OpStorageExists(Ctx&, const StorageExistsRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpStorageMakeDir(Ctx&, const StorageMakeDirRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpHttpRequest(Ctx&, const HttpRequestRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPacketSend(Ctx&, const PacketSendRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<RestText> OpPacketName(Ctx&, const PacketNameRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<RestText> OpSettingGet(Ctx&, const SettingGetRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpSettingSet(Ctx&, const SettingSetRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<SettingListResponse> OpSettingList(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<ModListResponse> OpModList(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpIconSet(Ctx&, const IconSetRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpOpenScreen(Ctx&, const OpenScreenRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpOpenModSettings(Ctx&, const OpenModSettingsRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpTakeScreenshot(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpCyclePerspective(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpCopyToClipboard(Ctx&, const CopyToClipboardRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpOpenUrl(Ctx&, const OpenUrlRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpToggleHud(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpCopyFile(Ctx&, const CopyFileRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpMoveFile(Ctx&, const MoveFileRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpDownloadFile(Ctx&, const DownloadFileRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpCameraControl(Ctx&, const CameraControlState&) { return std::unexpected(Status::Unavailable); }
        virtual Result<CameraInfo> OpCameraState(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<ScreenPoint> OpWorldToScreen(Ctx&, const WorldPoint&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerMotion(Ctx&, const MotionRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerTeleport(Ctx&, const TeleportRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerModelPose(Ctx&, const ModelPose&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPlayerSetLook(Ctx&, const LookSet&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpWorldLines(Ctx&, const WorldLines&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpWorldBoxes(Ctx&, const WorldBoxes&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpClientBlockSet(Ctx&, const ClientBlockEdits&) { return std::unexpected(Status::Unavailable); }
        virtual Result<BlockQueryResult> OpClientBlockGet(Ctx&, const BlockQuery&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPacketDrop(Ctx&, const PacketDropRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<PacketCountersResponse> OpPacketCounters(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPacketInject(Ctx&, const PacketInjectRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpHudElementSet(Ctx&, const HudElementState&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpDebugLines(Ctx&, const DebugLinesData&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpFogSet(Ctx&, const FogOverride&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpScreenTint(Ctx&, const ScreenTintData&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpEntityRenderOverride(Ctx&, const EntityOverride&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpWorldMesh(Ctx&, const WorldMeshData&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpWorldMeshClear(Ctx&, const WorldMeshClearRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpFrameLimit(Ctx&, const FrameLimitData&) { return std::unexpected(Status::Unavailable); }
        virtual Result<WindowInfo> OpWindowState(Ctx&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpScreenButtonAdd(Ctx&, const ScreenButtonDef&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpScreenButtonRemove(Ctx&, const ScreenButtonRemoveRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpScreenOpenCustom(Ctx&, const ScreenOpenCustomRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpTooltipAppend(Ctx&, const TooltipAppendRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpLoadingOverlay(Ctx&, const LoadingOverlayData&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpSkinOverride(Ctx&, const SkinOverrideData&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpAmbientSound(Ctx&, const AmbientSoundData&) { return std::unexpected(Status::Unavailable); }
        virtual Result<uint32_t> OpServerPing(Ctx&, const ServerPingRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpModSend(Ctx&, const ModMessageData&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpPackRegister(Ctx&, const PackRegistration&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpShaderPackSet(Ctx&, const ShaderPackSetRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpParticleSpawn(Ctx&, const ParticleRequest&) { return std::unexpected(Status::Unavailable); }
        virtual Result<void> OpKeyRegisterEx(Ctx&, const KeyBindEx&) { return std::unexpected(Status::Unavailable); }

        void Bind(OpTable<Ctx>& table) {
            table.template Handle<Op::Log>([this](Ctx& c, const LogRequest& r) { return OpLog(c, r); });
            table.template Handle<Op::Meta>([this](Ctx& c) { return OpMeta(c); });
            table.template Handle<Op::Clock>([this](Ctx& c) { return OpClock(c); });
            table.template Handle<Op::PlayerState>([this](Ctx& c) { return OpPlayerState(c); });
            table.template Handle<Op::PlayerEffects>([this](Ctx& c) { return OpPlayerEffects(c); });
            table.template Handle<Op::PlayerLook>([this](Ctx& c) { return OpPlayerLook(c); });
            table.template Handle<Op::Inventory>([this](Ctx& c) { return OpInventory(c); });
            table.template Handle<Op::InventorySelect>([this](Ctx& c, const InventorySelectRequest& r) { return OpInventorySelect(c, r); });
            table.template Handle<Op::InventoryDrop>([this](Ctx& c, const InventoryDropRequest& r) { return OpInventoryDrop(c, r); });
            table.template Handle<Op::InventorySwap>([this](Ctx& c, const InventorySwapRequest& r) { return OpInventorySwap(c, r); });
            table.template Handle<Op::InventoryClick>([this](Ctx& c, const InventoryClickRequest& r) { return OpInventoryClick(c, r); });
            table.template Handle<Op::InventoryOpen>([this](Ctx& c) { return OpInventoryOpen(c); });
            table.template Handle<Op::InventoryClose>([this](Ctx& c) { return OpInventoryClose(c); });
            table.template Handle<Op::WorldState>([this](Ctx& c) { return OpWorldState(c); });
            table.template Handle<Op::Block>([this](Ctx& c, const BlockRequest& r) { return OpBlock(c, r); });
            table.template Handle<Op::Region>([this](Ctx& c, const RegionRequest& r) { return OpRegion(c, r); });
            table.template Handle<Op::Raycast>([this](Ctx& c, const RaycastRequest& r) { return OpRaycast(c, r); });
            table.template Handle<Op::Aim>([this](Ctx& c) { return OpAim(c); });
            table.template Handle<Op::FindBlocks>([this](Ctx& c, const FindBlocksRequest& r) { return OpFindBlocks(c, r); });
            table.template Handle<Op::ChunkLoaded>([this](Ctx& c, const ChunkLoadedRequest& r) { return OpChunkLoaded(c, r); });
            table.template Handle<Op::BlockNames>([this](Ctx& c) { return OpBlockNames(c); });
            table.template Handle<Op::BlockProps>([this](Ctx& c, const BlockPropsRequest& r) { return OpBlockProps(c, r); });
            table.template Handle<Op::BlockEntityData>([this](Ctx& c, const BlockEntityDataRequest& r) { return OpBlockEntityData(c, r); });
            table.template Handle<Op::DisplayName>([this](Ctx& c, const DisplayNameRequest& r) { return OpDisplayName(c, r); });
            table.template Handle<Op::BreakProgress>([this](Ctx& c) { return OpBreakProgress(c); });
            table.template Handle<Op::MapChunk>([this](Ctx& c, const MapChunkRequest& r) { return OpMapChunk(c, r); });
            table.template Handle<Op::Entities>([this](Ctx& c, const EntitiesRequest& r) { return OpEntities(c, r); });
            table.template Handle<Op::EntityById>([this](Ctx& c, const EntityByIdRequest& r) { return OpEntityById(c, r); });
            table.template Handle<Op::PlayerList>([this](Ctx& c) { return OpPlayerList(c); });
            table.template Handle<Op::Options>([this](Ctx& c) { return OpOptions(c); });
            table.template Handle<Op::OptionGet>([this](Ctx& c, const OptionGetRequest& r) { return OpOptionGet(c, r); });
            table.template Handle<Op::OptionSet>([this](Ctx& c, const OptionSetRequest& r) { return OpOptionSet(c, r); });
            table.template Handle<Op::Screen>([this](Ctx& c) { return OpScreen(c); });
            table.template Handle<Op::Translate>([this](Ctx& c, const TranslateRequest& r) { return OpTranslate(c, r); });
            table.template Handle<Op::ReloadResources>([this](Ctx& c) { return OpReloadResources(c); });
            table.template Handle<Op::PerspectiveGet>([this](Ctx& c) { return OpPerspectiveGet(c); });
            table.template Handle<Op::PerspectiveSet>([this](Ctx& c, const PerspectiveSetRequest& r) { return OpPerspectiveSet(c, r); });
            table.template Handle<Op::ResourcePacks>([this](Ctx& c) { return OpResourcePacks(c); });
            table.template Handle<Op::InputDown>([this](Ctx& c, const InputDownRequest& r) { return OpInputDown(c, r); });
            table.template Handle<Op::InputOverride>([this](Ctx& c, const InputOverrideState& r) { return OpInputOverride(c, r); });
            table.template Handle<Op::KeyRegister>([this](Ctx& c, const KeyRegisterRequest& r) { return OpKeyRegister(c, r); });
            table.template Handle<Op::InputActions>([this](Ctx& c) { return OpInputActions(c); });
            table.template Handle<Op::MousePosition>([this](Ctx& c) { return OpMousePosition(c); });
            table.template Handle<Op::Chat>([this](Ctx& c, const ChatRequest& r) { return OpChat(c, r); });
            table.template Handle<Op::Command>([this](Ctx& c, const CommandRequest& r) { return OpCommand(c, r); });
            table.template Handle<Op::LocalMessage>([this](Ctx& c, const LocalMessageRequest& r) { return OpLocalMessage(c, r); });
            table.template Handle<Op::CommandRegister>([this](Ctx& c, const CommandRegisterRequest& r) { return OpCommandRegister(c, r); });
            table.template Handle<Op::CommandUnregister>([this](Ctx& c, const CommandUnregisterRequest& r) { return OpCommandUnregister(c, r); });
            table.template Handle<Op::ServerCommands>([this](Ctx& c) { return OpServerCommands(c); });
            table.template Handle<Op::Toast>([this](Ctx& c, const ToastRequest& r) { return OpToast(c, r); });
            table.template Handle<Op::Title>([this](Ctx& c, const TitleRequest& r) { return OpTitle(c, r); });
            table.template Handle<Op::ChatHistory>([this](Ctx& c, const ChatHistoryRequest& r) { return OpChatHistory(c, r); });
            table.template Handle<Op::TitleState>([this](Ctx& c) { return OpTitleState(c); });
            table.template Handle<Op::SwingArm>([this](Ctx& c) { return OpSwingArm(c); });
            table.template Handle<Op::UseItem>([this](Ctx& c) { return OpUseItem(c); });
            table.template Handle<Op::UseOnBlock>([this](Ctx& c, const UseOnBlockRequest& r) { return OpUseOnBlock(c, r); });
            table.template Handle<Op::AttackEntity>([this](Ctx& c, const AttackEntityRequest& r) { return OpAttackEntity(c, r); });
            table.template Handle<Op::InteractEntity>([this](Ctx& c, const InteractEntityRequest& r) { return OpInteractEntity(c, r); });
            table.template Handle<Op::BreakBlock>([this](Ctx& c, const BreakBlockRequest& r) { return OpBreakBlock(c, r); });
            table.template Handle<Op::Look>([this](Ctx& c, const LookRequest& r) { return OpLook(c, r); });
            table.template Handle<Op::Respawn>([this](Ctx& c) { return OpRespawn(c); });
            table.template Handle<Op::Disconnect>([this](Ctx& c) { return OpDisconnect(c); });
            table.template Handle<Op::PlaySound>([this](Ctx& c, const PlaySoundRequest& r) { return OpPlaySound(c, r); });
            table.template Handle<Op::Jump>([this](Ctx& c) { return OpJump(c); });
            table.template Handle<Op::UiSubmit>([this](Ctx& c, const UiSubmitRequest& r) { return OpUiSubmit(c, r); });
            table.template Handle<Op::UiClear>([this](Ctx& c, const UiClearRequest& r) { return OpUiClear(c, r); });
            table.template Handle<Op::UiMeasure>([this](Ctx& c, const UiMeasureRequest& r) { return OpUiMeasure(c, r); });
            table.template Handle<Op::GfxTextureCreate>([this](Ctx& c, const GfxTextureCreateRequest& r) { return OpGfxTextureCreate(c, r); });
            table.template Handle<Op::GfxTextureUpdate>([this](Ctx& c, const GfxTextureUpdateRequest& r) { return OpGfxTextureUpdate(c, r); });
            table.template Handle<Op::GfxTextureDestroy>([this](Ctx& c, const GfxTextureDestroyRequest& r) { return OpGfxTextureDestroy(c, r); });
            table.template Handle<Op::GfxSubmit>([this](Ctx& c, const GfxSubmitRequest& r) { return OpGfxSubmit(c, r); });
            table.template Handle<Op::GfxClear>([this](Ctx& c, const GfxClearRequest& r) { return OpGfxClear(c, r); });
            table.template Handle<Op::GfxDisplay>([this](Ctx& c) { return OpGfxDisplay(c); });
            table.template Handle<Op::GfxImageLoad>([this](Ctx& c, const GfxImageLoadRequest& r) { return OpGfxImageLoad(c, r); });
            table.template Handle<Op::InputGrab>([this](Ctx& c, const InputGrabRequest& r) { return OpInputGrab(c, r); });
            table.template Handle<Op::NativeInfo>([this](Ctx& c) { return OpNativeInfo(c); });
            table.template Handle<Op::StorageRead>([this](Ctx& c, const StorageReadRequest& r) { return OpStorageRead(c, r); });
            table.template Handle<Op::StorageWrite>([this](Ctx& c, const StorageWriteRequest& r) { return OpStorageWrite(c, r); });
            table.template Handle<Op::StorageList>([this](Ctx& c, const StorageListRequest& r) { return OpStorageList(c, r); });
            table.template Handle<Op::StorageRemove>([this](Ctx& c, const StorageRemoveRequest& r) { return OpStorageRemove(c, r); });
            table.template Handle<Op::StorageExists>([this](Ctx& c, const StorageExistsRequest& r) { return OpStorageExists(c, r); });
            table.template Handle<Op::StorageMakeDir>([this](Ctx& c, const StorageMakeDirRequest& r) { return OpStorageMakeDir(c, r); });
            table.template Handle<Op::HttpRequest>([this](Ctx& c, const HttpRequestRequest& r) { return OpHttpRequest(c, r); });
            table.template Handle<Op::PacketSend>([this](Ctx& c, const PacketSendRequest& r) { return OpPacketSend(c, r); });
            table.template Handle<Op::PacketName>([this](Ctx& c, const PacketNameRequest& r) { return OpPacketName(c, r); });
            table.template Handle<Op::SettingGet>([this](Ctx& c, const SettingGetRequest& r) { return OpSettingGet(c, r); });
            table.template Handle<Op::SettingSet>([this](Ctx& c, const SettingSetRequest& r) { return OpSettingSet(c, r); });
            table.template Handle<Op::SettingList>([this](Ctx& c) { return OpSettingList(c); });
            table.template Handle<Op::ModList>([this](Ctx& c) { return OpModList(c); });
            table.template Handle<Op::IconSet>([this](Ctx& c, const IconSetRequest& r) { return OpIconSet(c, r); });
            table.template Handle<Op::OpenScreen>([this](Ctx& c, const OpenScreenRequest& r) { return OpOpenScreen(c, r); });
            table.template Handle<Op::OpenModSettings>([this](Ctx& c, const OpenModSettingsRequest& r) { return OpOpenModSettings(c, r); });
            table.template Handle<Op::TakeScreenshot>([this](Ctx& c) { return OpTakeScreenshot(c); });
            table.template Handle<Op::CyclePerspective>([this](Ctx& c) { return OpCyclePerspective(c); });
            table.template Handle<Op::CopyToClipboard>([this](Ctx& c, const CopyToClipboardRequest& r) { return OpCopyToClipboard(c, r); });
            table.template Handle<Op::OpenUrl>([this](Ctx& c, const OpenUrlRequest& r) { return OpOpenUrl(c, r); });
            table.template Handle<Op::ToggleHud>([this](Ctx& c) { return OpToggleHud(c); });
            table.template Handle<Op::CopyFile>([this](Ctx& c, const CopyFileRequest& r) { return OpCopyFile(c, r); });
            table.template Handle<Op::MoveFile>([this](Ctx& c, const MoveFileRequest& r) { return OpMoveFile(c, r); });
            table.template Handle<Op::DownloadFile>([this](Ctx& c, const DownloadFileRequest& r) { return OpDownloadFile(c, r); });
            table.template Handle<Op::CameraControl>([this](Ctx& c, const CameraControlState& r) { return OpCameraControl(c, r); });
            table.template Handle<Op::CameraState>([this](Ctx& c) { return OpCameraState(c); });
            table.template Handle<Op::WorldToScreen>([this](Ctx& c, const WorldPoint& r) { return OpWorldToScreen(c, r); });
            table.template Handle<Op::PlayerMotion>([this](Ctx& c, const MotionRequest& r) { return OpPlayerMotion(c, r); });
            table.template Handle<Op::PlayerTeleport>([this](Ctx& c, const TeleportRequest& r) { return OpPlayerTeleport(c, r); });
            table.template Handle<Op::PlayerModelPose>([this](Ctx& c, const ModelPose& r) { return OpPlayerModelPose(c, r); });
            table.template Handle<Op::PlayerSetLook>([this](Ctx& c, const LookSet& r) { return OpPlayerSetLook(c, r); });
            table.template Handle<Op::WorldLines>([this](Ctx& c, const WorldLines& r) { return OpWorldLines(c, r); });
            table.template Handle<Op::WorldBoxes>([this](Ctx& c, const WorldBoxes& r) { return OpWorldBoxes(c, r); });
            table.template Handle<Op::ClientBlockSet>([this](Ctx& c, const ClientBlockEdits& r) { return OpClientBlockSet(c, r); });
            table.template Handle<Op::ClientBlockGet>([this](Ctx& c, const BlockQuery& r) { return OpClientBlockGet(c, r); });
            table.template Handle<Op::PacketDrop>([this](Ctx& c, const PacketDropRequest& r) { return OpPacketDrop(c, r); });
            table.template Handle<Op::PacketCounters>([this](Ctx& c) { return OpPacketCounters(c); });
            table.template Handle<Op::PacketInject>([this](Ctx& c, const PacketInjectRequest& r) { return OpPacketInject(c, r); });
            table.template Handle<Op::HudElementSet>([this](Ctx& c, const HudElementState& r) { return OpHudElementSet(c, r); });
            table.template Handle<Op::DebugLines>([this](Ctx& c, const DebugLinesData& r) { return OpDebugLines(c, r); });
            table.template Handle<Op::FogSet>([this](Ctx& c, const FogOverride& r) { return OpFogSet(c, r); });
            table.template Handle<Op::ScreenTint>([this](Ctx& c, const ScreenTintData& r) { return OpScreenTint(c, r); });
            table.template Handle<Op::EntityRenderOverride>([this](Ctx& c, const EntityOverride& r) { return OpEntityRenderOverride(c, r); });
            table.template Handle<Op::WorldMesh>([this](Ctx& c, const WorldMeshData& r) { return OpWorldMesh(c, r); });
            table.template Handle<Op::WorldMeshClear>([this](Ctx& c, const WorldMeshClearRequest& r) { return OpWorldMeshClear(c, r); });
            table.template Handle<Op::FrameLimit>([this](Ctx& c, const FrameLimitData& r) { return OpFrameLimit(c, r); });
            table.template Handle<Op::WindowState>([this](Ctx& c) { return OpWindowState(c); });
            table.template Handle<Op::ScreenButtonAdd>([this](Ctx& c, const ScreenButtonDef& r) { return OpScreenButtonAdd(c, r); });
            table.template Handle<Op::ScreenButtonRemove>([this](Ctx& c, const ScreenButtonRemoveRequest& r) { return OpScreenButtonRemove(c, r); });
            table.template Handle<Op::ScreenOpenCustom>([this](Ctx& c, const ScreenOpenCustomRequest& r) { return OpScreenOpenCustom(c, r); });
            table.template Handle<Op::TooltipAppend>([this](Ctx& c, const TooltipAppendRequest& r) { return OpTooltipAppend(c, r); });
            table.template Handle<Op::LoadingOverlay>([this](Ctx& c, const LoadingOverlayData& r) { return OpLoadingOverlay(c, r); });
            table.template Handle<Op::SkinOverride>([this](Ctx& c, const SkinOverrideData& r) { return OpSkinOverride(c, r); });
            table.template Handle<Op::AmbientSound>([this](Ctx& c, const AmbientSoundData& r) { return OpAmbientSound(c, r); });
            table.template Handle<Op::ServerPing>([this](Ctx& c, const ServerPingRequest& r) { return OpServerPing(c, r); });
            table.template Handle<Op::ModSend>([this](Ctx& c, const ModMessageData& r) { return OpModSend(c, r); });
            table.template Handle<Op::PackRegister>([this](Ctx& c, const PackRegistration& r) { return OpPackRegister(c, r); });
            table.template Handle<Op::ShaderPackSet>([this](Ctx& c, const ShaderPackSetRequest& r) { return OpShaderPackSet(c, r); });
            table.template Handle<Op::ParticleSpawn>([this](Ctx& c, const ParticleRequest& r) { return OpParticleSpawn(c, r); });
            table.template Handle<Op::KeyRegisterEx>([this](Ctx& c, const KeyBindEx& r) { return OpKeyRegisterEx(c, r); });
        }
    };
}
