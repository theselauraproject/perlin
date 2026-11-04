/* perlin plugin abi: every host function and event, client and server, in one place.
 *
 * This file is the single source of truth. The C++ SDK, the C header, the client host (src/client/mods)
 * and the server host (server/src/plugin) all expand these lists; adding a line here is all it takes for a
 * new call or event to appear, typed, in every plugin. Plain preprocessor so C plugins can include it too.
 *
 * PERLIN_ABI_OPS(X):    X(Name, id, Request, Response, Permission, Hosts)
 *   Permission: what the plugin's manifest must grant, combined with | (Network | Files)
 *   Request/Response: an abi:: aggregate (encoded field by field), Nothing, or Raw (hand-encoded bytes)
 *   Hosts: Client, Server or Both; calling an op the running host lacks answers Status::UnknownOp
 * PERLIN_ABI_EVENTS(X): X(Name, id, Payload, Hosts, Wiring)
 *   Wiring: Method when a plugin class gets it by defining on<Name>(), Internal when the runtime consumes it
 *   ids stay below 64: plugins report subscriptions as one 64-bit mask
 */
#ifndef PERLIN_ABI_TABLE_H
#define PERLIN_ABI_TABLE_H

// clang-format off
#define PERLIN_ABI_VERSION 2

#define PERLIN_ABI_OPS(X) \
    X(Log, 0x0001, Raw, Nothing, None, Both) \
    X(Meta, 0x0002, Nothing, Meta, None, Client) \
    X(Clock, 0x0003, Nothing, int64_t, None, Both) \
    X(PlayerState, 0x0100, Nothing, PlayerState, None, Client) \
    X(PlayerEffects, 0x0101, Raw, Raw, None, Client) \
    X(PlayerLook, 0x0102, Nothing, Raw, None, Client) \
    X(Inventory, 0x0200, Nothing, Inventory, None, Client) \
    X(InventorySelect, 0x0201, Raw, Nothing, Control, Client) \
    X(InventoryDrop, 0x0202, Raw, Nothing, Control, Client) \
    X(InventorySwap, 0x0203, Raw, Nothing, Control, Client) \
    X(InventoryClick, 0x0204, Raw, Nothing, Control, Client) \
    X(InventoryOpen, 0x0205, Nothing, Nothing, Control, Client) \
    X(InventoryClose, 0x0206, Nothing, Nothing, Control, Client) \
    X(WorldState, 0x0300, Nothing, WorldState, None, Client) \
    X(Block, 0x0301, Raw, BlockInfo, None, Client) \
    X(Region, 0x0302, Raw, Region, None, Client) \
    X(Raycast, 0x0303, Raw, RayHit, None, Client) \
    X(Aim, 0x0304, Nothing, AimState, None, Client) \
    X(FindBlocks, 0x0305, Raw, Raw, None, Client) \
    X(ChunkLoaded, 0x0306, Raw, Raw, None, Client) \
    X(BlockNames, 0x0307, Raw, Raw, None, Client) \
    X(BlockProps, 0x0308, Raw, BlockProps, None, Client) \
    X(BlockEntityData, 0x0309, Raw, BlockEntityInfo, None, Client) \
    X(DisplayName, 0x030A, Raw, Raw, None, Client) \
    X(BreakProgress, 0x030B, Nothing, BreakState, None, Client) \
    X(MapChunk, 0x030C, Raw, Raw, None, Client) \
    X(Entities, 0x0400, Raw, Raw, None, Client) \
    X(EntityById, 0x0401, Raw, Entity, None, Client) \
    X(PlayerList, 0x0402, Raw, Raw, None, Client) \
    X(Options, 0x0500, Raw, Raw, None, Client) \
    X(OptionGet, 0x0501, Raw, Raw, None, Client) \
    X(OptionSet, 0x0502, Raw, Nothing, Control, Client) \
    X(Screen, 0x0503, Nothing, ScreenState, None, Client) \
    X(Translate, 0x0504, Raw, Raw, None, Client) \
    X(ReloadResources, 0x0505, Nothing, Nothing, Control, Client) \
    X(PerspectiveGet, 0x0506, Nothing, Perspective, None, Client) \
    X(PerspectiveSet, 0x0507, Raw, Nothing, None, Client) \
    X(ResourcePacks, 0x0508, Raw, Raw, None, Client) \
    X(InputDown, 0x0600, Raw, Raw, None, Client) \
    X(InputOverride, 0x0601, Raw, Nothing, Control, Client) \
    X(KeyRegister, 0x0602, Raw, Nothing, None, Client) \
    X(InputActions, 0x0603, Raw, Raw, None, Client) \
    X(MousePosition, 0x0604, Nothing, Raw, None, Client) \
    X(Chat, 0x0700, Raw, Nothing, Control, Client) \
    X(Command, 0x0701, Raw, Nothing, Control, Client) \
    X(LocalMessage, 0x0702, Raw, Nothing, None, Client) \
    X(CommandRegister, 0x0703, Raw, Nothing, None, Both) \
    X(CommandUnregister, 0x0704, Raw, Nothing, None, Both) \
    X(ServerCommands, 0x0705, Raw, Raw, None, Client) \
    X(Toast, 0x0706, Raw, Nothing, None, Client) \
    X(Title, 0x0707, Raw, Nothing, None, Client) \
    X(ChatHistory, 0x0708, Raw, Raw, None, Client) \
    X(TitleState, 0x0709, Nothing, TitleStateData, None, Client) \
    X(SwingArm, 0x0800, Nothing, Nothing, Control, Client) \
    X(UseItem, 0x0801, Nothing, Nothing, Control, Client) \
    X(UseOnBlock, 0x0802, Raw, Nothing, Control, Client) \
    X(AttackEntity, 0x0803, Raw, Nothing, Control, Client) \
    X(InteractEntity, 0x0804, Raw, Nothing, Control, Client) \
    X(BreakBlock, 0x0805, Raw, Nothing, Control, Client) \
    X(Look, 0x0806, Raw, Nothing, Control, Client) \
    X(Respawn, 0x0807, Nothing, Nothing, Control, Client) \
    X(Disconnect, 0x0808, Nothing, Nothing, Control, Client) \
    X(PlaySound, 0x0809, Raw, Nothing, None, Client) \
    X(Jump, 0x080B, Nothing, Nothing, Control, Client) \
    X(UiSubmit, 0x0900, Raw, Nothing, None, Client) \
    X(UiClear, 0x0901, Raw, Nothing, None, Client) \
    X(UiMeasure, 0x0902, Raw, Raw, None, Client) \
    X(GfxTextureCreate, 0x0910, Raw, Raw, None, Client) \
    X(GfxTextureUpdate, 0x0911, Raw, Nothing, None, Client) \
    X(GfxTextureDestroy, 0x0912, Raw, Nothing, None, Client) \
    X(GfxSubmit, 0x0913, Raw, Nothing, None, Client) \
    X(GfxClear, 0x0914, Raw, Nothing, None, Client) \
    X(GfxDisplay, 0x0915, Nothing, Raw, None, Client) \
    X(GfxImageLoad, 0x0916, Raw, Raw, None, Client) \
    X(InputGrab, 0x0920, Raw, Nothing, Control, Client) \
    X(NativeInfo, 0x0930, Nothing, NativeHandles, Native, Client) \
    X(StorageRead, 0x0A00, Raw, Raw, Storage, Client) \
    X(StorageWrite, 0x0A01, Raw, Nothing, Storage, Client) \
    X(StorageList, 0x0A02, Raw, Raw, Storage, Client) \
    X(StorageRemove, 0x0A03, Raw, Nothing, Storage, Client) \
    X(StorageExists, 0x0A04, Raw, Raw, Storage, Client) \
    X(StorageMakeDir, 0x0A05, Raw, Nothing, Storage, Client) \
    X(HttpRequest, 0x0B00, Raw, Nothing, Network, Client) \
    X(PacketSend, 0x0B01, Raw, Nothing, Packets, Client) \
    X(PacketName, 0x0B02, Raw, Raw, None, Client) \
    X(SettingGet, 0x0C01, Raw, Raw, None, Client) \
    X(SettingSet, 0x0C02, Raw, Nothing, None, Client) \
    X(SettingList, 0x0C03, Raw, Raw, None, Client) \
    X(ModList, 0x0C10, Raw, Raw, None, Client) \
    X(IconSet, 0x0C11, Raw, Nothing, None, Client) \
    X(OpenScreen, 0x0D00, Raw, Nothing, None, Client) \
    X(OpenModSettings, 0x0D01, Raw, Nothing, None, Client) \
    X(TakeScreenshot, 0x0D02, Nothing, Nothing, None, Client) \
    X(CyclePerspective, 0x0D03, Nothing, Nothing, None, Client) \
    X(CopyToClipboard, 0x0D04, Raw, Nothing, None, Client) \
    X(OpenUrl, 0x0D05, Raw, Nothing, Network, Client) \
    X(ToggleHud, 0x0D06, Nothing, Nothing, None, Client) \
    X(CopyFile, 0x0E00, Raw, Nothing, Files, Client) \
    X(MoveFile, 0x0E01, Raw, Nothing, Files, Client) \
    X(DownloadFile, 0x0E02, Raw, Nothing, Network | Files, Client) \
    X(CameraControl, 0x0F00, Raw, Nothing, Control, Client) \
    X(CameraState, 0x0F01, Nothing, CameraInfo, None, Client) \
    X(WorldToScreen, 0x0F02, Raw, ScreenPoint, None, Client) \
    X(PlayerMotion, 0x0F10, Raw, Nothing, Control, Client) \
    X(PlayerTeleport, 0x0F11, Raw, Nothing, Control, Client) \
    X(PlayerModelPose, 0x0F20, Raw, Nothing, Control, Client) \
    X(PlayerSetLook, 0x0F21, Raw, Nothing, Control, Client) \
    X(WorldLines, 0x0F30, Raw, Nothing, None, Client) \
    X(WorldBoxes, 0x0F31, Raw, Nothing, None, Client) \
    X(ClientBlockSet, 0x0F40, Raw, Nothing, Control, Client) \
    X(ClientBlockGet, 0x0F41, Raw, BlockQueryResult, None, Client) \
    X(PacketDrop, 0x0F50, Raw, Nothing, Packets | Control, Client) \
    X(PacketCounters, 0x0F51, Raw, Raw, None, Client) \
    X(PacketInject, 0x0F52, Raw, Nothing, Packets | Control, Client) \
    X(HudElementSet, 0x1000, Raw, Raw, None, Client) \
    X(DebugLines, 0x1001, Raw, Raw, None, Client) \
    X(FogSet, 0x1002, Raw, Raw, None, Client) \
    X(ScreenTint, 0x1003, Raw, Raw, None, Client) \
    X(EntityRenderOverride, 0x1010, Raw, Raw, None, Client) \
    X(WorldMesh, 0x1011, Raw, Raw, None, Client) \
    X(WorldMeshClear, 0x1012, Raw, Nothing, None, Client) \
    X(FrameLimit, 0x1020, Raw, Raw, None, Client) \
    X(WindowState, 0x1021, Nothing, WindowInfo, None, Client) \
    X(ScreenButtonAdd, 0x1030, Raw, Raw, None, Client) \
    X(ScreenButtonRemove, 0x1031, Raw, Nothing, None, Client) \
    X(ScreenOpenCustom, 0x1032, Raw, Nothing, None, Client) \
    X(TooltipAppend, 0x1033, Raw, Nothing, None, Client) \
    X(LoadingOverlay, 0x1040, Raw, Raw, None, Client) \
    X(SkinOverride, 0x1041, Raw, Raw, None, Client) \
    X(AmbientSound, 0x1050, Raw, Raw, None, Client) \
    X(ServerPing, 0x1060, Raw, uint32_t, Network, Client) \
    X(ModSend, 0x1070, Raw, Raw, None, Client) \
    X(PackRegister, 0x1080, Raw, Raw, Files, Client) \
    X(ShaderPackSet, 0x1081, Raw, Nothing, Control, Client) \
    X(ParticleSpawn, 0x1090, Raw, Raw, None, Client) \
    X(KeyRegisterEx, 0x10A0, Raw, Raw, None, Client) \
    X(Broadcast, 0x7000, TextMessage, Nothing, None, Server) \
    X(PlayerMessage, 0x7001, PlayerText, Nothing, None, Server) \
    X(Kick, 0x7002, PlayerText, Nothing, None, Server) \
    X(OnlinePlayers, 0x7003, Nothing, OnlinePlayerList, None, Server) \
    X(Teleport, 0x7004, ServerTeleport, Nothing, None, Server) \
    X(SetBlock, 0x7005, BlockWrite, Nothing, None, Server) \
    X(BlockName, 0x7006, BlockCoord, TextMessage, None, Server) \
    X(ServerInfo, 0x7007, Nothing, ServerStatus, None, Server) \
    X(GiveItem, 0x7008, GiveItemRequest, GiveItemResult, None, Server)

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
    X(HttpResponse, 20, EventHttp, Client, Internal) \
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
    X(ModsLoaded, 39, EventNothing, Client, Method) \
    X(ClientStopping, 40, EventNothing, Client, Method) \
    X(Toast, 41, EventToast, Client, Method) \
    X(ModMessage, 42, EventModMessage, Client, Method) \
    X(Tooltip, 43, EventTooltip, Client, Method) \
    X(ScreenButton, 44, EventScreenButton, Client, Method) \
    X(WindowFocus, 45, EventWindow, Client, Method) \
    X(ServerPing, 46, EventServerPing, Client, Method) \
    X(PlayerJoin, 47, EventPlayer, Server, Method) \
    X(PlayerQuit, 48, EventPlayer, Server, Method) \
    X(PlayerChat, 49, EventPlayerChat, Server, Method) \
    X(BlockBreak, 50, EventPlayerBlock, Server, Method) \
    X(BlockPlace, 51, EventPlayerBlock, Server, Method)

// clang-format on
#endif
