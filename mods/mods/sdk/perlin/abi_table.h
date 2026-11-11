#ifndef PERLIN_ABI_TABLE_H
#define PERLIN_ABI_TABLE_H

#define PERLIN_ABI_VERSION 3

#define PERLIN_ABI_OPS(X) \
    X(Log, 0x0001, LogRequest, Nothing, None, Both) \
    X(Meta, 0x0002, Nothing, Meta, None, Client) \
    X(Clock, 0x0003, Nothing, int64_t, None, Both) \
    X(PlayerState, 0x0100, Nothing, PlayerState, None, Client) \
    X(PlayerEffects, 0x0101, Nothing, PlayerEffectsResponse, None, Client) \
    X(PlayerLook, 0x0102, Nothing, PlayerLookResponse, None, Client) \
    X(Inventory, 0x0200, Nothing, Inventory, None, Client) \
    X(InventorySelect, 0x0201, InventorySelectRequest, Nothing, Control, Client) \
    X(InventoryDrop, 0x0202, InventoryDropRequest, Nothing, Control, Client) \
    X(InventorySwap, 0x0203, InventorySwapRequest, Nothing, Control, Client) \
    X(InventoryClick, 0x0204, InventoryClickRequest, Nothing, Control, Client) \
    X(InventoryOpen, 0x0205, Nothing, Nothing, Control, Client) \
    X(InventoryClose, 0x0206, Nothing, Nothing, Control, Client) \
    X(WorldState, 0x0300, Nothing, WorldState, None, Client) \
    X(Block, 0x0301, BlockRequest, BlockInfo, None, Client) \
    X(Region, 0x0302, RegionRequest, Region, None, Client) \
    X(Raycast, 0x0303, RaycastRequest, RayHit, None, Client) \
    X(Aim, 0x0304, Nothing, AimState, None, Client) \
    X(FindBlocks, 0x0305, FindBlocksRequest, FindBlocksResponse, None, Client) \
    X(ChunkLoaded, 0x0306, ChunkLoadedRequest, bool, None, Client) \
    X(BlockNames, 0x0307, Nothing, BlockNamesResponse, None, Client) \
    X(BlockProps, 0x0308, BlockPropsRequest, BlockProps, None, Client) \
    X(BlockEntityData, 0x0309, BlockEntityDataRequest, BlockEntityInfo, None, Client) \
    X(DisplayName, 0x030A, DisplayNameRequest, RestText, None, Client) \
    X(BreakProgress, 0x030B, Nothing, BreakState, None, Client) \
    X(MapChunk, 0x030C, MapChunkRequest, RestBytes, None, Client) \
    X(Entities, 0x0400, EntitiesRequest, EntitiesResponse, None, Client) \
    X(EntityById, 0x0401, EntityByIdRequest, Entity, None, Client) \
    X(PlayerList, 0x0402, Nothing, PlayerListResponse, None, Client) \
    X(Options, 0x0500, Nothing, RestText, None, Client) \
    X(OptionGet, 0x0501, OptionGetRequest, RestText, None, Client) \
    X(OptionSet, 0x0502, OptionSetRequest, Nothing, Control, Client) \
    X(Screen, 0x0503, Nothing, ScreenState, None, Client) \
    X(Translate, 0x0504, TranslateRequest, RestText, None, Client) \
    X(ReloadResources, 0x0505, Nothing, Nothing, Control, Client) \
    X(PerspectiveGet, 0x0506, Nothing, Perspective, None, Client) \
    X(PerspectiveSet, 0x0507, PerspectiveSetRequest, Nothing, None, Client) \
    X(ResourcePacks, 0x0508, Nothing, ResourcePacksResponse, None, Client) \
    X(InputDown, 0x0600, InputDownRequest, bool, None, Client) \
    X(InputOverride, 0x0601, InputOverrideState, Nothing, Control, Client) \
    X(KeyRegister, 0x0602, KeyRegisterRequest, Nothing, None, Client) \
    X(InputActions, 0x0603, Nothing, InputActionsResponse, None, Client) \
    X(MousePosition, 0x0604, Nothing, MousePositionResponse, None, Client) \
    X(Chat, 0x0700, ChatRequest, Nothing, Control, Client) \
    X(Command, 0x0701, CommandRequest, Nothing, Control, Client) \
    X(LocalMessage, 0x0702, LocalMessageRequest, Nothing, None, Client) \
    X(CommandRegister, 0x0703, CommandRegisterRequest, Nothing, None, Both) \
    X(CommandUnregister, 0x0704, CommandUnregisterRequest, Nothing, None, Both) \
    X(ServerCommands, 0x0705, Nothing, ServerCommandsResponse, None, Client) \
    X(Toast, 0x0706, ToastRequest, Nothing, None, Client) \
    X(Title, 0x0707, TitleRequest, Nothing, None, Client) \
    X(ChatHistory, 0x0708, ChatHistoryRequest, ChatHistoryResponse, None, Client) \
    X(TitleState, 0x0709, Nothing, TitleStateData, None, Client) \
    X(SwingArm, 0x0800, Nothing, Nothing, Control, Client) \
    X(UseItem, 0x0801, Nothing, Nothing, Control, Client) \
    X(UseOnBlock, 0x0802, UseOnBlockRequest, Nothing, Control, Client) \
    X(AttackEntity, 0x0803, AttackEntityRequest, Nothing, Control, Client) \
    X(InteractEntity, 0x0804, InteractEntityRequest, Nothing, Control, Client) \
    X(BreakBlock, 0x0805, BreakBlockRequest, Nothing, Control, Client) \
    X(Look, 0x0806, LookRequest, Nothing, Control, Client) \
    X(Respawn, 0x0807, Nothing, Nothing, Control, Client) \
    X(Disconnect, 0x0808, Nothing, Nothing, Control, Client) \
    X(PlaySound, 0x0809, PlaySoundRequest, Nothing, None, Client) \
    X(Jump, 0x080B, Nothing, Nothing, Control, Client) \
    X(UiSubmit, 0x0900, UiSubmitRequest, Nothing, None, Client) \
    X(UiClear, 0x0901, UiClearRequest, Nothing, None, Client) \
    X(UiMeasure, 0x0902, UiMeasureRequest, float, None, Client) \
    X(GfxTextureCreate, 0x0910, GfxTextureCreateRequest, uint32_t, None, Client) \
    X(GfxTextureUpdate, 0x0911, GfxTextureUpdateRequest, Nothing, None, Client) \
    X(GfxTextureDestroy, 0x0912, GfxTextureDestroyRequest, Nothing, None, Client) \
    X(GfxSubmit, 0x0913, GfxSubmitRequest, Nothing, None, Client) \
    X(GfxClear, 0x0914, GfxClearRequest, Nothing, None, Client) \
    X(GfxDisplay, 0x0915, Nothing, GfxDisplayInfo, None, Client) \
    X(GfxImageLoad, 0x0916, GfxImageLoadRequest, GfxImageLoadResponse, None, Client) \
    X(InputGrab, 0x0920, InputGrabRequest, Nothing, Control, Client) \
    X(NativeInfo, 0x0930, Nothing, NativeHandles, Native, Client) \
    X(StorageRead, 0x0A00, StorageReadRequest, RestBytes, Storage, Both) \
    X(StorageWrite, 0x0A01, StorageWriteRequest, Nothing, Storage, Both) \
    X(StorageList, 0x0A02, StorageListRequest, StorageListResponse, Storage, Both) \
    X(StorageRemove, 0x0A03, StorageRemoveRequest, Nothing, Storage, Both) \
    X(StorageExists, 0x0A04, StorageExistsRequest, bool, Storage, Both) \
    X(StorageMakeDir, 0x0A05, StorageMakeDirRequest, Nothing, Storage, Both) \
    X(HttpRequest, 0x0B00, HttpRequestRequest, Nothing, Network, Both) \
    X(PacketSend, 0x0B01, PacketSendRequest, Nothing, Packets, Client) \
    X(PacketName, 0x0B02, PacketNameRequest, RestText, None, Client) \
    X(SettingGet, 0x0C01, SettingGetRequest, RestText, None, Both) \
    X(SettingSet, 0x0C02, SettingSetRequest, Nothing, None, Both) \
    X(SettingList, 0x0C03, Nothing, SettingListResponse, None, Both) \
    X(ModList, 0x0C10, Nothing, ModListResponse, None, Both) \
    X(IconSet, 0x0C11, IconSetRequest, Nothing, None, Client) \
    X(OpenScreen, 0x0D00, OpenScreenRequest, Nothing, None, Client) \
    X(OpenModSettings, 0x0D01, OpenModSettingsRequest, Nothing, None, Client) \
    X(TakeScreenshot, 0x0D02, Nothing, Nothing, None, Client) \
    X(CyclePerspective, 0x0D03, Nothing, Nothing, None, Client) \
    X(CopyToClipboard, 0x0D04, CopyToClipboardRequest, Nothing, None, Client) \
    X(OpenUrl, 0x0D05, OpenUrlRequest, Nothing, Network, Client) \
    X(ToggleHud, 0x0D06, Nothing, Nothing, None, Client) \
    X(CopyFile, 0x0E00, CopyFileRequest, Nothing, Files, Client) \
    X(MoveFile, 0x0E01, MoveFileRequest, Nothing, Files, Client) \
    X(DownloadFile, 0x0E02, DownloadFileRequest, Nothing, Network | Files, Client) \
    X(CameraControl, 0x0F00, CameraControlState, Nothing, Control, Client) \
    X(CameraState, 0x0F01, Nothing, CameraInfo, None, Client) \
    X(WorldToScreen, 0x0F02, WorldPoint, ScreenPoint, None, Client) \
    X(PlayerMotion, 0x0F10, MotionRequest, Nothing, Control, Client) \
    X(PlayerTeleport, 0x0F11, TeleportRequest, Nothing, Control, Client) \
    X(PlayerModelPose, 0x0F20, ModelPose, Nothing, Control, Client) \
    X(PlayerSetLook, 0x0F21, LookSet, Nothing, Control, Client) \
    X(WorldLines, 0x0F30, WorldLines, Nothing, None, Client) \
    X(WorldBoxes, 0x0F31, WorldBoxes, Nothing, None, Client) \
    X(ClientBlockSet, 0x0F40, ClientBlockEdits, Nothing, Control, Client) \
    X(ClientBlockGet, 0x0F41, BlockQuery, BlockQueryResult, None, Client) \
    X(PacketDrop, 0x0F50, PacketDropRequest, Nothing, Packets | Control, Client) \
    X(PacketCounters, 0x0F51, Nothing, PacketCountersResponse, None, Client) \
    X(PacketInject, 0x0F52, PacketInjectRequest, Nothing, Packets | Control, Client) \
    X(HudElementSet, 0x1000, HudElementState, Nothing, None, Client) \
    X(DebugLines, 0x1001, DebugLinesData, Nothing, None, Client) \
    X(FogSet, 0x1002, FogOverride, Nothing, None, Client) \
    X(ScreenTint, 0x1003, ScreenTintData, Nothing, None, Client) \
    X(EntityRenderOverride, 0x1010, EntityOverride, Nothing, None, Client) \
    X(WorldMesh, 0x1011, WorldMeshData, Nothing, None, Client) \
    X(WorldMeshClear, 0x1012, WorldMeshClearRequest, Nothing, None, Client) \
    X(FrameLimit, 0x1020, FrameLimitData, Nothing, None, Client) \
    X(WindowState, 0x1021, Nothing, WindowInfo, None, Client) \
    X(ScreenButtonAdd, 0x1030, ScreenButtonDef, Nothing, None, Client) \
    X(ScreenButtonRemove, 0x1031, ScreenButtonRemoveRequest, Nothing, None, Client) \
    X(ScreenOpenCustom, 0x1032, ScreenOpenCustomRequest, Nothing, None, Client) \
    X(TooltipAppend, 0x1033, TooltipAppendRequest, Nothing, None, Client) \
    X(LoadingOverlay, 0x1040, LoadingOverlayData, Nothing, None, Client) \
    X(SkinOverride, 0x1041, SkinOverrideData, Nothing, None, Client) \
    X(AmbientSound, 0x1050, AmbientSoundData, Nothing, None, Client) \
    X(ServerPing, 0x1060, ServerPingRequest, uint32_t, Network, Client) \
    X(ModSend, 0x1070, ModMessageData, Nothing, None, Both) \
    X(PackRegister, 0x1080, PackRegistration, Nothing, Files, Client) \
    X(ShaderPackSet, 0x1081, ShaderPackSetRequest, Nothing, Control, Client) \
    X(ParticleSpawn, 0x1090, ParticleRequest, Nothing, None, Client) \
    X(KeyRegisterEx, 0x10A0, KeyBindEx, Nothing, None, Client) \
    X(Broadcast, 0x7000, TextMessage, Nothing, None, Server) \
    X(PlayerMessage, 0x7001, PlayerText, Nothing, None, Server) \
    X(Kick, 0x7002, PlayerText, Nothing, Admin, Server) \
    X(OnlinePlayers, 0x7003, Nothing, OnlinePlayerList, None, Server) \
    X(Teleport, 0x7004, ServerTeleport, Nothing, Players, Server) \
    X(SetBlock, 0x7005, BlockWrite, Nothing, World, Server) \
    X(BlockName, 0x7006, BlockCoord, TextMessage, None, Server) \
    X(ServerInfo, 0x7007, Nothing, ServerStatus, None, Server) \
    X(GiveItem, 0x7008, GiveItemRequest, GiveItemResult, Players, Server) \
    X(PlayerInfo, 0x7010, PlayerInfoRequest, PlayerDetails, None, Server) \
    X(PlayerSetGameMode, 0x7011, PlayerSetGameModeRequest, Nothing, Players, Server) \
    X(PlayerSetHealth, 0x7012, PlayerSetHealthRequest, Nothing, Players, Server) \
    X(PlayerDamage, 0x7013, PlayerDamageRequest, bool, Players, Server) \
    X(PlayerKill, 0x7014, PlayerKillRequest, Nothing, Players, Server) \
    X(PlayerSetFood, 0x7015, PlayerSetFoodRequest, Nothing, Players, Server) \
    X(PlayerAddExperience, 0x7016, PlayerAddExperienceRequest, Nothing, Players, Server) \
    X(PlayerEffectAdd, 0x7017, PlayerEffectAddRequest, Nothing, Players, Server) \
    X(PlayerEffectRemove, 0x7018, PlayerEffectRemoveRequest, Nothing, Players, Server) \
    X(PlayerInventory, 0x7019, PlayerInventoryRequest, ServerInventory, None, Server) \
    X(PlayerSetSlot, 0x701A, PlayerSetSlotRequest, Nothing, Players, Server) \
    X(PlayerClearInventory, 0x701B, PlayerClearInventoryRequest, Nothing, Players, Server) \
    X(PlayerSetVelocity, 0x701C, PlayerSetVelocityRequest, Nothing, Players, Server) \
    X(PlayerTransferDimension, 0x701D, PlayerTransferDimensionRequest, Nothing, Players, Server) \
    X(PlayerTitle, 0x701E, PlayerTitleRequest, Nothing, None, Server) \
    X(PlayerActionBar, 0x701F, PlayerActionBarRequest, Nothing, None, Server) \
    X(PlayerSound, 0x7020, PlayerSoundRequest, Nothing, None, Server) \
    X(PlayerTransfer, 0x7021, PlayerTransferRequest, Nothing, Admin, Server) \
    X(PlayerSetOperator, 0x7022, PlayerSetOperatorRequest, Nothing, Admin, Server) \
    X(PlayerForm, 0x7023, PlayerFormRequest, uint32_t, None, Server) \
    X(PlayerRunCommand, 0x7024, PlayerRunCommandRequest, Nothing, Admin, Server) \
    X(PlayerSetSpawn, 0x7025, PlayerSetSpawnRequest, Nothing, Players, Server) \
    X(PlayerByName, 0x7026, PlayerByNameRequest, uint64_t, None, Server) \
    X(BlockState, 0x7040, BlockStateRequest, BlockInfo, None, Server) \
    X(BlockStateSet, 0x7041, BlockStateSetRequest, Nothing, World, Server) \
    X(Fill, 0x7042, FillRequest, uint32_t, World, Server) \
    X(TimeGet, 0x7043, Nothing, TimeGetResponse, None, Server) \
    X(TimeSet, 0x7044, TimeSetRequest, Nothing, World, Server) \
    X(WeatherSet, 0x7045, WeatherSetRequest, Nothing, World, Server) \
    X(GameRuleGet, 0x7046, GameRuleGetRequest, int32_t, None, Server) \
    X(GameRuleSet, 0x7047, GameRuleSetRequest, Nothing, Admin, Server) \
    X(DifficultySet, 0x7048, DifficultySetRequest, Nothing, Admin, Server) \
    X(Explode, 0x7049, ExplodeRequest, Nothing, World, Server) \
    X(HighestBlock, 0x704A, HighestBlockRequest, int32_t, None, Server) \
    X(ParticleAt, 0x704B, ParticleAtRequest, Nothing, None, Server) \
    X(DropItem, 0x704C, DropItemRequest, Nothing, World, Server) \
    X(WorldSpawnGet, 0x704D, Nothing, BlockCoord, None, Server) \
    X(WorldSpawnSet, 0x704E, BlockCoord, Nothing, Admin, Server) \
    X(EntitySpawn, 0x7060, EntitySpawnRequest, uint64_t, World, Server) \
    X(EntityList, 0x7061, EntityListRequest, EntityListResponse, None, Server) \
    X(EntityRemove, 0x7062, EntityRemoveRequest, Nothing, World, Server) \
    X(EntityDamage, 0x7063, EntityDamageRequest, bool, World, Server) \
    X(EntityTeleport, 0x7064, EntityTeleportRequest, Nothing, World, Server) \
    X(EntitySetVelocity, 0x7065, EntitySetVelocityRequest, Nothing, World, Server) \
    X(EntityInfo, 0x7066, EntityInfoRequest, ServerEntity, None, Server) \
    X(EntitySetHealth, 0x7067, EntitySetHealthRequest, Nothing, World, Server) \
    X(RunCommand, 0x7080, RunCommandRequest, RestText, Admin, Server) \
    X(MotdSet, 0x7081, MotdSetRequest, Nothing, Admin, Server) \
    X(LootTableOverride, 0x7082, LootTableOverrideRequest, Nothing, World, Server) \
    X(LootTableGet, 0x7083, LootTableGetRequest, RestText, None, Server) \
    X(EventAmend, 0x7084, EventAmendRequest, Nothing, None, Server)

#define PERLIN_ABI_EVENTS(X) \
    X(Load, 1, EventNothing, Both, Internal) \
    X(Unload, 2, EventNothing, Both, Internal) \
    X(Tick, 3, EventTick, Both, Method) \
    X(Frame, 4, EventFrame, Client, Method) \
    X(ChatReceived, 5, EventChat, Client, Method) \
    X(ChatSending, 6, EventText, Client, Method) \
    X(Connected, 7, EventConnection, Client, Method) \
    X(Disconnected, 8, EventConnection, Client, Method) \
    X(Key, 9, EventKey, Client, Method) \
    X(Command, 10, EventCommand, Both, Internal) \
    X(Damage, 11, EventDamage, Client, Method) \
    X(Death, 12, EventText, Client, Method) \
    X(Respawned, 13, EventNothing, Client, Method) \
    X(DimensionChanged, 14, EventDimension, Client, Method) \
    X(ScreenChanged, 15, EventScreen, Client, Method) \
    X(SlotChanged, 16, EventSlot, Client, Method) \
    X(EntityAdded, 17, EventEntity, Client, Method) \
    X(EntityRemoved, 18, EventEntity, Client, Method) \
    X(Sound, 19, EventSound, Client, Method) \
    X(HttpResponse, 20, EventHttp, Both, Internal) \
    X(Packet, 21, EventPacket, Client, Method) \
    X(Title, 22, EventTitle, Client, Method) \
    X(SettingChanged, 23, EventSettingChanged, Client, Method) \
    X(Look, 24, EventLook, Client, Method) \
    X(MouseMove, 25, EventMouseMove, Client, Method) \
    X(MouseButton, 26, EventMouseButton, Client, Method) \
    X(MouseWheel, 27, EventMouseWheel, Client, Method) \
    X(KeyRaw, 28, EventKeyRaw, Client, Method) \
    X(TextInput, 29, EventText, Client, Method) \
    X(ScreenRender, 30, EventScreenRender, Client, Method) \
    X(SlotClick, 31, EventSlotClick, Client, Method) \
    X(Interact, 32, EventInteract, Client, Method) \
    X(ChunkLoaded, 33, EventChunk, Client, Method) \
    X(ChunkUnloaded, 34, EventChunk, Client, Method) \
    X(BlockChanged, 35, EventBlockChanged, Both, Method) \
    X(ResourcesReloaded, 36, EventNothing, Client, Method) \
    X(LevelLoaded, 37, EventLevel, Client, Method) \
    X(LevelUnloaded, 38, EventNothing, Client, Method) \
    X(ModsLoaded, 39, EventNothing, Both, Method) \
    X(ClientStopping, 40, EventNothing, Client, Method) \
    X(Toast, 41, EventToast, Client, Method) \
    X(ModMessage, 42, EventModMessage, Both, Method) \
    X(Tooltip, 43, EventTooltip, Client, Method) \
    X(ScreenButton, 44, EventScreenButton, Client, Method) \
    X(WindowFocus, 45, EventWindow, Client, Method) \
    X(ServerPing, 46, EventServerPing, Client, Method) \
    X(PlayerJoin, 47, EventPlayer, Server, Method) \
    X(PlayerQuit, 48, EventPlayer, Server, Method) \
    X(PlayerChat, 49, EventPlayerChat, Server, Method) \
    X(BlockBreak, 50, EventPlayerBlock, Server, Method) \
    X(BlockPlace, 51, EventPlayerBlock, Server, Method) \
    X(PlayerDamage, 52, EventServerDamage, Server, Method) \
    X(PlayerDeath, 53, EventServerDeath, Server, Method) \
    X(PlayerRespawn, 54, EventPlayer, Server, Method) \
    X(PlayerUseBlock, 55, EventPlayerUse, Server, Method) \
    X(PlayerAttack, 56, EventPlayerAttack, Server, Method) \
    X(EntityDamage, 57, EventEntityDamage, Server, Method) \
    X(EntityDeath, 58, EventEntityDeath, Server, Method) \
    X(EntitySpawn, 59, EventEntitySpawn, Server, Method) \
    X(ItemConsume, 60, EventItem, Server, Method) \
    X(GameModeChange, 61, EventGameModeChange, Server, Method) \
    X(FormResponse, 62, EventFormResponse, Server, Method) \
    X(PlayerMove, 63, EventPlayerMove, Server, Method) \
    X(Explosion, 64, EventExplosion, Server, Method) \
    X(ItemDrop, 65, EventItem, Server, Method) \
    X(ItemPickup, 66, EventItem, Server, Method) \
    X(ContainerOpen, 67, EventContainer, Server, Method) \
    X(DimensionChange, 68, EventPlayerDimension, Server, Method) \
    X(ServerCommand, 69, EventServerCommand, Server, Method) \
    X(PreLogin, 70, EventPreLogin, Server, Method)

#endif
