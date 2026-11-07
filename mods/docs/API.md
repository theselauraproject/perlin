# Perlin mod API reference (schema v3)

Generated from `mods/api/*.papi` by `mods/codegen/generate.py`. Edit the schema, not this file.

## Tags

| Tag | Parameters | Applies to | Meaning |
|---|---|---|---|
| `@doc` | text | any | Human-readable description; every backend copies it into its docs |
| `@hosts` | name... | op, event | Which programs answer the call or raise the event: client, server |
| `@permission` | name... | op | Permission bits the plugin manifest must grant before the host runs the call |
| `@since` | int | any | API version that introduced the declaration |
| `@deprecated` | text? | any | Still served, scheduled for removal; the text names the replacement |
| `@internal` | - | event | Consumed by the SDK runtime instead of becoming an onName method |
| `@cancellable` | - | event | A handler may return cancel; the host then skips the default behaviour |
| `@hot` | - | event, op | Raised or called many times per second; bindings should avoid allocations |
| `@cpp` | text | struct | Extra C++ members appended to the generated struct |
| `@range` | int, int | enum | Valid enumerator range for reflection-based helpers |
| `@unit` | text | field | Physical unit of the value: blocks, ticks, ms, degrees, pixels |
| `@max` | int | field | Largest accepted length or value; hosts reject anything above it |
| `@layout` | name | struct | packed: a binary blob layout with a fixed size, also usable inside bytes fields |
| `@size` | int | struct | Expected sizeof for a packed layout |
| `@category` | name | op, event | Groups declarations in generated documentation and language modules |

## Ops

| Id | Op | C | Request | Response | Hosts | Permission | Notes |
|---|---|---|---|---|---|---|---|
| 0x0001 | Log | `perlin_log` | [LogRequest](#logrequest) | - | client, server | - | `@category(core)` |
| 0x0002 | Meta | `perlin_meta` | - | [Meta](#meta) | client | - | `@category(core)` |
| 0x0003 | Clock | `perlin_clock` | - | `i64` | client, server | - | `@category(core)` |
| 0x0100 | PlayerState | `perlin_player_state` | - | [PlayerState](#playerstate) | client | - | `@category(player)` `@hot` |
| 0x0101 | PlayerEffects | `perlin_player_effects` | - | [PlayerEffectsResponse](#playereffectsresponse) | client | - | `@category(player)` |
| 0x0102 | PlayerLook | `perlin_player_look` | - | [PlayerLookResponse](#playerlookresponse) | client | - | `@category(player)` |
| 0x0200 | Inventory | `perlin_inventory` | - | [Inventory](#inventory) | client | - | `@category(inventory)` |
| 0x0201 | InventorySelect | `perlin_inventory_select` | [InventorySelectRequest](#inventoryselectrequest) | - | client | Control | `@category(inventory)` |
| 0x0202 | InventoryDrop | `perlin_inventory_drop` | [InventoryDropRequest](#inventorydroprequest) | - | client | Control | `@category(inventory)` |
| 0x0203 | InventorySwap | `perlin_inventory_swap` | [InventorySwapRequest](#inventoryswaprequest) | - | client | Control | `@category(inventory)` |
| 0x0204 | InventoryClick | `perlin_inventory_click` | [InventoryClickRequest](#inventoryclickrequest) | - | client | Control | `@category(inventory)` |
| 0x0205 | InventoryOpen | `perlin_inventory_open` | - | - | client | Control | `@category(inventory)` |
| 0x0206 | InventoryClose | `perlin_inventory_close` | - | - | client | Control | `@category(inventory)` |
| 0x0300 | WorldState | `perlin_world_state` | - | [WorldState](#worldstate) | client | - | `@category(world)` |
| 0x0301 | Block | `perlin_block` | [BlockRequest](#blockrequest) | [BlockInfo](#blockinfo) | client | - | `@category(world)` `@hot` |
| 0x0302 | Region | `perlin_region` | [RegionRequest](#regionrequest) | [Region](#region) | client | - | `@category(world)` |
| 0x0303 | Raycast | `perlin_raycast` | [RaycastRequest](#raycastrequest) | [RayHit](#rayhit) | client | - | `@category(world)` |
| 0x0304 | Aim | `perlin_aim` | - | [AimState](#aimstate) | client | - | `@category(world)` |
| 0x0305 | FindBlocks | `perlin_find_blocks` | [FindBlocksRequest](#findblocksrequest) | [FindBlocksResponse](#findblocksresponse) | client | - | `@category(world)` |
| 0x0306 | ChunkLoaded | `perlin_chunk_loaded` | [ChunkLoadedRequest](#chunkloadedrequest) | `bool` | client | - | `@category(world)` |
| 0x0307 | BlockNames | `perlin_block_names` | - | [BlockNamesResponse](#blocknamesresponse) | client | - | `@category(world)` |
| 0x0308 | BlockProps | `perlin_block_props` | [BlockPropsRequest](#blockpropsrequest) | [BlockProps](#blockprops) | client | - | `@category(world)` |
| 0x0309 | BlockEntityData | `perlin_block_entity_data` | [BlockEntityDataRequest](#blockentitydatarequest) | [BlockEntityInfo](#blockentityinfo) | client | - | `@category(world)` |
| 0x030A | DisplayName | `perlin_display_name` | [DisplayNameRequest](#displaynamerequest) | `rest_text` | client | - | `@category(world)` |
| 0x030B | BreakProgress | `perlin_break_progress` | - | [BreakState](#breakstate) | client | - | `@category(world)` |
| 0x030C | MapChunk | `perlin_map_chunk` | [MapChunkRequest](#mapchunkrequest) | `rest_bytes` | client | - | bool fresh, u64 version, then the column grid: see mods/docs/map-chunk.md `@category(world)` |
| 0x0400 | Entities | `perlin_entities` | [EntitiesRequest](#entitiesrequest) | [EntitiesResponse](#entitiesresponse) | client | - | `@category(entity)` |
| 0x0401 | EntityById | `perlin_entity_by_id` | [EntityByIdRequest](#entitybyidrequest) | [Entity](#entity) | client | - | `@category(entity)` |
| 0x0402 | PlayerList | `perlin_player_list` | - | [PlayerListResponse](#playerlistresponse) | client | - | `@category(entity)` |
| 0x0500 | Options | `perlin_options` | - | `rest_text` | client | - | every option as one JSON object `@category(options)` |
| 0x0501 | OptionGet | `perlin_option_get` | [OptionGetRequest](#optiongetrequest) | `rest_text` | client | - | `@category(options)` |
| 0x0502 | OptionSet | `perlin_option_set` | [OptionSetRequest](#optionsetrequest) | - | client | Control | `@category(options)` |
| 0x0503 | Screen | `perlin_screen` | - | [ScreenState](#screenstate) | client | - | `@category(ui)` |
| 0x0504 | Translate | `perlin_translate` | [TranslateRequest](#translaterequest) | `rest_text` | client | - | `@category(ui)` |
| 0x0505 | ReloadResources | `perlin_reload_resources` | - | - | client | Control | `@category(resources)` |
| 0x0506 | PerspectiveGet | `perlin_perspective_get` | - | [Perspective](#perspective) | client | - | `@category(camera)` |
| 0x0507 | PerspectiveSet | `perlin_perspective_set` | [PerspectiveSetRequest](#perspectivesetrequest) | - | client | - | `@category(camera)` |
| 0x0508 | ResourcePacks | `perlin_resource_packs` | - | [ResourcePacksResponse](#resourcepacksresponse) | client | - | `@category(resources)` |
| 0x0600 | InputDown | `perlin_input_down` | [InputDownRequest](#inputdownrequest) | `bool` | client | - | `@category(input)` |
| 0x0601 | InputOverride | `perlin_input_override` | [InputOverrideState](#inputoverridestate) | - | client | Control | `@category(input)` |
| 0x0602 | KeyRegister | `perlin_key_register` | [KeyRegisterRequest](#keyregisterrequest) | - | client | - | `@category(input)` |
| 0x0603 | InputActions | `perlin_input_actions` | - | [InputActionsResponse](#inputactionsresponse) | client | - | `@category(input)` |
| 0x0604 | MousePosition | `perlin_mouse_position` | - | [MousePositionResponse](#mousepositionresponse) | client | - | `@category(input)` |
| 0x0700 | Chat | `perlin_chat` | [ChatRequest](#chatrequest) | - | client | Control | `@category(chat)` |
| 0x0701 | Command | `perlin_command` | [CommandRequest](#commandrequest) | - | client | Control | `@category(chat)` |
| 0x0702 | LocalMessage | `perlin_local_message` | [LocalMessageRequest](#localmessagerequest) | - | client | - | `@category(chat)` |
| 0x0703 | CommandRegister | `perlin_command_register` | [CommandRegisterRequest](#commandregisterrequest) | - | client, server | - | `@category(chat)` |
| 0x0704 | CommandUnregister | `perlin_command_unregister` | [CommandUnregisterRequest](#commandunregisterrequest) | - | client, server | - | `@category(chat)` |
| 0x0705 | ServerCommands | `perlin_server_commands` | - | [ServerCommandsResponse](#servercommandsresponse) | client | - | `@category(chat)` |
| 0x0706 | Toast | `perlin_toast` | [ToastRequest](#toastrequest) | - | client | - | `@category(ui)` |
| 0x0707 | Title | `perlin_title` | [TitleRequest](#titlerequest) | - | client | - | `@category(ui)` |
| 0x0708 | ChatHistory | `perlin_chat_history` | [ChatHistoryRequest](#chathistoryrequest) | [ChatHistoryResponse](#chathistoryresponse) | client | - | `@category(chat)` |
| 0x0709 | TitleState | `perlin_title_state` | - | [TitleStateData](#titlestatedata) | client | - | `@category(ui)` |
| 0x0800 | SwingArm | `perlin_swing_arm` | - | - | client | Control | `@category(action)` |
| 0x0801 | UseItem | `perlin_use_item` | - | - | client | Control | `@category(action)` |
| 0x0802 | UseOnBlock | `perlin_use_on_block` | [UseOnBlockRequest](#useonblockrequest) | - | client | Control | `@category(action)` |
| 0x0803 | AttackEntity | `perlin_attack_entity` | [AttackEntityRequest](#attackentityrequest) | - | client | Control | `@category(action)` |
| 0x0804 | InteractEntity | `perlin_interact_entity` | [InteractEntityRequest](#interactentityrequest) | - | client | Control | `@category(action)` |
| 0x0805 | BreakBlock | `perlin_break_block` | [BreakBlockRequest](#breakblockrequest) | - | client | Control | `@category(action)` |
| 0x0806 | Look | `perlin_look` | [LookRequest](#lookrequest) | - | client | Control | `@category(action)` |
| 0x0807 | Respawn | `perlin_respawn` | - | - | client | Control | `@category(action)` |
| 0x0808 | Disconnect | `perlin_disconnect` | - | - | client | Control | `@category(action)` |
| 0x0809 | PlaySound | `perlin_play_sound` | [PlaySoundRequest](#playsoundrequest) | - | client | - | `@category(audio)` |
| 0x080B | Jump | `perlin_jump` | - | - | client | Control | `@category(action)` |
| 0x0900 | UiSubmit | `perlin_ui_submit` | [UiSubmitRequest](#uisubmitrequest) | - | client | - | `@category(ui)` `@hot` |
| 0x0901 | UiClear | `perlin_ui_clear` | [UiClearRequest](#uiclearrequest) | - | client | - | `@category(ui)` |
| 0x0902 | UiMeasure | `perlin_ui_measure` | [UiMeasureRequest](#uimeasurerequest) | `f32` | client | - | `@category(ui)` |
| 0x0910 | GfxTextureCreate | `perlin_gfx_texture_create` | [GfxTextureCreateRequest](#gfxtexturecreaterequest) | `u32` | client | - | `@category(gfx)` |
| 0x0911 | GfxTextureUpdate | `perlin_gfx_texture_update` | [GfxTextureUpdateRequest](#gfxtextureupdaterequest) | - | client | - | `@category(gfx)` `@hot` |
| 0x0912 | GfxTextureDestroy | `perlin_gfx_texture_destroy` | [GfxTextureDestroyRequest](#gfxtexturedestroyrequest) | - | client | - | `@category(gfx)` |
| 0x0913 | GfxSubmit | `perlin_gfx_submit` | [GfxSubmitRequest](#gfxsubmitrequest) | - | client | - | `@category(gfx)` `@hot` |
| 0x0914 | GfxClear | `perlin_gfx_clear` | [GfxClearRequest](#gfxclearrequest) | - | client | - | `@category(gfx)` |
| 0x0915 | GfxDisplay | `perlin_gfx_display` | - | [GfxDisplayInfo](#gfxdisplayinfo) | client | - | `@category(gfx)` |
| 0x0916 | GfxImageLoad | `perlin_gfx_image_load` | [GfxImageLoadRequest](#gfximageloadrequest) | [GfxImageLoadResponse](#gfximageloadresponse) | client | - | `@category(gfx)` |
| 0x0920 | InputGrab | `perlin_input_grab` | [InputGrabRequest](#inputgrabrequest) | - | client | Control | `@category(input)` |
| 0x0930 | NativeInfo | `perlin_native_info` | - | [NativeHandles](#nativehandles) | client | Native | `@category(gfx)` |
| 0x0A00 | StorageRead | `perlin_storage_read` | [StorageReadRequest](#storagereadrequest) | `rest_bytes` | client, server | Storage | `@category(storage)` |
| 0x0A01 | StorageWrite | `perlin_storage_write` | [StorageWriteRequest](#storagewriterequest) | - | client, server | Storage | `@category(storage)` |
| 0x0A02 | StorageList | `perlin_storage_list` | [StorageListRequest](#storagelistrequest) | [StorageListResponse](#storagelistresponse) | client, server | Storage | `@category(storage)` |
| 0x0A03 | StorageRemove | `perlin_storage_remove` | [StorageRemoveRequest](#storageremoverequest) | - | client, server | Storage | `@category(storage)` |
| 0x0A04 | StorageExists | `perlin_storage_exists` | [StorageExistsRequest](#storageexistsrequest) | `bool` | client, server | Storage | `@category(storage)` |
| 0x0A05 | StorageMakeDir | `perlin_storage_make_dir` | [StorageMakeDirRequest](#storagemakedirrequest) | - | client, server | Storage | `@category(storage)` |
| 0x0B00 | HttpRequest | `perlin_http_request` | [HttpRequestRequest](#httprequestrequest) | - | client, server | Network | the answer arrives as the HttpResponse event with the same id `@category(network)` |
| 0x0B01 | PacketSend | `perlin_packet_send` | [PacketSendRequest](#packetsendrequest) | - | client | Packets | `@category(network)` |
| 0x0B02 | PacketName | `perlin_packet_name` | [PacketNameRequest](#packetnamerequest) | `rest_text` | client | - | `@category(network)` |
| 0x0C01 | SettingGet | `perlin_setting_get` | [SettingGetRequest](#settinggetrequest) | `rest_text` | client, server | - | `@category(settings)` |
| 0x0C02 | SettingSet | `perlin_setting_set` | [SettingSetRequest](#settingsetrequest) | - | client, server | - | `@category(settings)` |
| 0x0C03 | SettingList | `perlin_setting_list` | - | [SettingListResponse](#settinglistresponse) | client, server | - | `@category(settings)` |
| 0x0C10 | ModList | `perlin_mod_list` | - | [ModListResponse](#modlistresponse) | client, server | - | `@category(mods)` |
| 0x0C11 | IconSet | `perlin_icon_set` | [IconSetRequest](#iconsetrequest) | - | client | - | `@category(mods)` |
| 0x0D00 | OpenScreen | `perlin_open_screen` | [OpenScreenRequest](#openscreenrequest) | - | client | - | `@category(ui)` |
| 0x0D01 | OpenModSettings | `perlin_open_mod_settings` | [OpenModSettingsRequest](#openmodsettingsrequest) | - | client | - | `@category(ui)` |
| 0x0D02 | TakeScreenshot | `perlin_take_screenshot` | - | - | client | - | `@category(ui)` |
| 0x0D03 | CyclePerspective | `perlin_cycle_perspective` | - | - | client | - | `@category(camera)` |
| 0x0D04 | CopyToClipboard | `perlin_copy_to_clipboard` | [CopyToClipboardRequest](#copytoclipboardrequest) | - | client | - | `@category(ui)` |
| 0x0D05 | OpenUrl | `perlin_open_url` | [OpenUrlRequest](#openurlrequest) | - | client | Network | `@category(ui)` |
| 0x0D06 | ToggleHud | `perlin_toggle_hud` | - | - | client | - | `@category(ui)` |
| 0x0E00 | CopyFile | `perlin_copy_file` | [CopyFileRequest](#copyfilerequest) | - | client | Files | `@category(storage)` |
| 0x0E01 | MoveFile | `perlin_move_file` | [MoveFileRequest](#movefilerequest) | - | client | Files | `@category(storage)` |
| 0x0E02 | DownloadFile | `perlin_download_file` | [DownloadFileRequest](#downloadfilerequest) | - | client | Network, Files | `@category(storage)` |
| 0x0F00 | CameraControl | `perlin_camera_control` | [CameraControlState](#cameracontrolstate) | - | client | Control | `@category(camera)` |
| 0x0F01 | CameraState | `perlin_camera_state` | - | [CameraInfo](#camerainfo) | client | - | `@category(camera)` |
| 0x0F02 | WorldToScreen | `perlin_world_to_screen` | [WorldPoint](#worldpoint) | [ScreenPoint](#screenpoint) | client | - | `@category(camera)` |
| 0x0F10 | PlayerMotion | `perlin_player_motion` | [MotionRequest](#motionrequest) | - | client | Control | `@category(player)` |
| 0x0F11 | PlayerTeleport | `perlin_player_teleport` | [TeleportRequest](#teleportrequest) | - | client | Control | `@category(player)` |
| 0x0F20 | PlayerModelPose | `perlin_player_model_pose` | [ModelPose](#modelpose) | - | client | Control | `@category(player)` |
| 0x0F21 | PlayerSetLook | `perlin_player_set_look` | [LookSet](#lookset) | - | client | Control | `@category(player)` |
| 0x0F30 | WorldLines | `perlin_world_lines` | [WorldLines](#worldlines) | - | client | - | `@category(render)` |
| 0x0F31 | WorldBoxes | `perlin_world_boxes` | [WorldBoxes](#worldboxes) | - | client | - | `@category(render)` |
| 0x0F40 | ClientBlockSet | `perlin_client_block_set` | [ClientBlockEdits](#clientblockedits) | - | client | Control | `@category(world)` |
| 0x0F41 | ClientBlockGet | `perlin_client_block_get` | [BlockQuery](#blockquery) | [BlockQueryResult](#blockqueryresult) | client | - | `@category(world)` |
| 0x0F50 | PacketDrop | `perlin_packet_drop` | [PacketDropRequest](#packetdroprequest) | - | client | Packets, Control | `@category(network)` |
| 0x0F51 | PacketCounters | `perlin_packet_counters` | - | [PacketCountersResponse](#packetcountersresponse) | client | - | `@category(network)` |
| 0x0F52 | PacketInject | `perlin_packet_inject` | [PacketInjectRequest](#packetinjectrequest) | - | client | Packets, Control | `@category(network)` |
| 0x1000 | HudElementSet | `perlin_hud_element_set` | [HudElementState](#hudelementstate) | - | client | - | `@category(ui)` |
| 0x1001 | DebugLines | `perlin_debug_lines` | [DebugLinesData](#debuglinesdata) | - | client | - | `@category(ui)` |
| 0x1002 | FogSet | `perlin_fog_set` | [FogOverride](#fogoverride) | - | client | - | `@category(render)` |
| 0x1003 | ScreenTint | `perlin_screen_tint` | [ScreenTintData](#screentintdata) | - | client | - | `@category(render)` |
| 0x1010 | EntityRenderOverride | `perlin_entity_render_override` | [EntityOverride](#entityoverride) | - | client | - | `@category(render)` |
| 0x1011 | WorldMesh | `perlin_world_mesh` | [WorldMeshData](#worldmeshdata) | - | client | - | `@category(render)` |
| 0x1012 | WorldMeshClear | `perlin_world_mesh_clear` | [WorldMeshClearRequest](#worldmeshclearrequest) | - | client | - | `@category(render)` |
| 0x1020 | FrameLimit | `perlin_frame_limit` | [FrameLimitData](#framelimitdata) | - | client | - | `@category(render)` |
| 0x1021 | WindowState | `perlin_window_state` | - | [WindowInfo](#windowinfo) | client | - | `@category(ui)` |
| 0x1030 | ScreenButtonAdd | `perlin_screen_button_add` | [ScreenButtonDef](#screenbuttondef) | - | client | - | `@category(ui)` |
| 0x1031 | ScreenButtonRemove | `perlin_screen_button_remove` | [ScreenButtonRemoveRequest](#screenbuttonremoverequest) | - | client | - | `@category(ui)` |
| 0x1032 | ScreenOpenCustom | `perlin_screen_open_custom` | [ScreenOpenCustomRequest](#screenopencustomrequest) | - | client | - | `@category(ui)` |
| 0x1033 | TooltipAppend | `perlin_tooltip_append` | [TooltipAppendRequest](#tooltipappendrequest) | - | client | - | `@category(ui)` |
| 0x1040 | LoadingOverlay | `perlin_loading_overlay` | [LoadingOverlayData](#loadingoverlaydata) | - | client | - | `@category(ui)` |
| 0x1041 | SkinOverride | `perlin_skin_override` | [SkinOverrideData](#skinoverridedata) | - | client | - | `@category(render)` |
| 0x1050 | AmbientSound | `perlin_ambient_sound` | [AmbientSoundData](#ambientsounddata) | - | client | - | `@category(audio)` |
| 0x1060 | ServerPing | `perlin_server_ping` | [ServerPingRequest](#serverpingrequest) | `u32` | client | Network | request id; the answer arrives as the ServerPing event `@category(network)` |
| 0x1070 | ModSend | `perlin_mod_send` | [ModMessageData](#modmessagedata) | - | client, server | - | `@category(mods)` |
| 0x1080 | PackRegister | `perlin_pack_register` | [PackRegistration](#packregistration) | - | client | Files | `@category(resources)` |
| 0x1081 | ShaderPackSet | `perlin_shader_pack_set` | [ShaderPackSetRequest](#shaderpacksetrequest) | - | client | Control | `@category(resources)` |
| 0x1090 | ParticleSpawn | `perlin_particle_spawn` | [ParticleRequest](#particlerequest) | - | client | - | `@category(render)` |
| 0x10A0 | KeyRegisterEx | `perlin_key_register_ex` | [KeyBindEx](#keybindex) | - | client | - | `@category(input)` |
| 0x7000 | Broadcast | `perlin_broadcast` | [TextMessage](#textmessage) | - | server | - | `@category(server)` |
| 0x7001 | PlayerMessage | `perlin_player_message` | [PlayerText](#playertext) | - | server | - | `@category(server)` |
| 0x7002 | Kick | `perlin_kick` | [PlayerText](#playertext) | - | server | Admin | `@category(server)` |
| 0x7003 | OnlinePlayers | `perlin_online_players` | - | [OnlinePlayerList](#onlineplayerlist) | server | - | `@category(server)` |
| 0x7004 | Teleport | `perlin_teleport` | [ServerTeleport](#serverteleport) | - | server | Players | `@category(players)` |
| 0x7005 | SetBlock | `perlin_set_block` | [BlockWrite](#blockwrite) | - | server | World | `@category(world)` |
| 0x7006 | BlockName | `perlin_block_name` | [BlockCoord](#blockcoord) | [TextMessage](#textmessage) | server | - | `@category(world)` |
| 0x7007 | ServerInfo | `perlin_server_info` | - | [ServerStatus](#serverstatus) | server | - | `@category(server)` |
| 0x7008 | GiveItem | `perlin_give_item` | [GiveItemRequest](#giveitemrequest) | [GiveItemResult](#giveitemresult) | server | Players | `@category(inventory)` |
| 0x7010 | PlayerInfo | `perlin_player_info` | [PlayerInfoRequest](#playerinforequest) | [PlayerDetails](#playerdetails) | server | - | `@since(3)` `@category(players)` |
| 0x7011 | PlayerSetGameMode | `perlin_player_set_game_mode` | [PlayerSetGameModeRequest](#playersetgamemoderequest) | - | server | Players | `@since(3)` `@category(players)` |
| 0x7012 | PlayerSetHealth | `perlin_player_set_health` | [PlayerSetHealthRequest](#playersethealthrequest) | - | server | Players | `@since(3)` `@category(players)` |
| 0x7013 | PlayerDamage | `perlin_player_damage` | [PlayerDamageRequest](#playerdamagerequest) | `bool` | server | Players | `@since(3)` `@category(players)` |
| 0x7014 | PlayerKill | `perlin_player_kill` | [PlayerKillRequest](#playerkillrequest) | - | server | Players | `@since(3)` `@category(players)` |
| 0x7015 | PlayerSetFood | `perlin_player_set_food` | [PlayerSetFoodRequest](#playersetfoodrequest) | - | server | Players | `@since(3)` `@category(players)` |
| 0x7016 | PlayerAddExperience | `perlin_player_add_experience` | [PlayerAddExperienceRequest](#playeraddexperiencerequest) | - | server | Players | `@since(3)` `@category(players)` |
| 0x7017 | PlayerEffectAdd | `perlin_player_effect_add` | [PlayerEffectAddRequest](#playereffectaddrequest) | - | server | Players | `@since(3)` `@category(players)` |
| 0x7018 | PlayerEffectRemove | `perlin_player_effect_remove` | [PlayerEffectRemoveRequest](#playereffectremoverequest) | - | server | Players | `@since(3)` `@category(players)` |
| 0x7019 | PlayerInventory | `perlin_player_inventory` | [PlayerInventoryRequest](#playerinventoryrequest) | [ServerInventory](#serverinventory) | server | - | `@since(3)` `@category(inventory)` |
| 0x701A | PlayerSetSlot | `perlin_player_set_slot` | [PlayerSetSlotRequest](#playersetslotrequest) | - | server | Players | `@since(3)` `@category(inventory)` |
| 0x701B | PlayerClearInventory | `perlin_player_clear_inventory` | [PlayerClearInventoryRequest](#playerclearinventoryrequest) | - | server | Players | `@since(3)` `@category(inventory)` |
| 0x701C | PlayerSetVelocity | `perlin_player_set_velocity` | [PlayerSetVelocityRequest](#playersetvelocityrequest) | - | server | Players | `@since(3)` `@category(players)` |
| 0x701D | PlayerTransferDimension | `perlin_player_transfer_dimension` | [PlayerTransferDimensionRequest](#playertransferdimensionrequest) | - | server | Players | `@since(3)` `@category(players)` |
| 0x701E | PlayerTitle | `perlin_player_title` | [PlayerTitleRequest](#playertitlerequest) | - | server | - | `@since(3)` `@category(players)` |
| 0x701F | PlayerActionBar | `perlin_player_action_bar` | [PlayerActionBarRequest](#playeractionbarrequest) | - | server | - | `@since(3)` `@category(players)` |
| 0x7020 | PlayerSound | `perlin_player_sound` | [PlayerSoundRequest](#playersoundrequest) | - | server | - | `@since(3)` `@category(players)` |
| 0x7021 | PlayerTransfer | `perlin_player_transfer` | [PlayerTransferRequest](#playertransferrequest) | - | server | Admin | `@since(3)` `@category(players)` |
| 0x7022 | PlayerSetOperator | `perlin_player_set_operator` | [PlayerSetOperatorRequest](#playersetoperatorrequest) | - | server | Admin | `@since(3)` `@category(players)` |
| 0x7023 | PlayerForm | `perlin_player_form` | [PlayerFormRequest](#playerformrequest) | `u32` | server | - | form id; the answer arrives as FormResponse `@since(3)` `@category(ui)` |
| 0x7024 | PlayerRunCommand | `perlin_player_run_command` | [PlayerRunCommandRequest](#playerruncommandrequest) | - | server | Admin | `@since(3)` `@category(players)` |
| 0x7025 | PlayerSetSpawn | `perlin_player_set_spawn` | [PlayerSetSpawnRequest](#playersetspawnrequest) | - | server | Players | `@since(3)` `@category(players)` |
| 0x7026 | PlayerByName | `perlin_player_by_name` | [PlayerByNameRequest](#playerbynamerequest) | `u64` | server | - | `@since(3)` `@category(players)` |
| 0x7040 | BlockState | `perlin_block_state` | [BlockStateRequest](#blockstaterequest) | [BlockInfo](#blockinfo) | server | - | `@since(3)` `@category(world)` |
| 0x7041 | BlockStateSet | `perlin_block_state_set` | [BlockStateSetRequest](#blockstatesetrequest) | - | server | World | `@since(3)` `@category(world)` |
| 0x7042 | Fill | `perlin_fill` | [FillRequest](#fillrequest) | `u32` | server | World | `@since(3)` `@category(world)` |
| 0x7043 | TimeGet | `perlin_time_get` | - | [TimeGetResponse](#timegetresponse) | server | - | `@since(3)` `@category(world)` |
| 0x7044 | TimeSet | `perlin_time_set` | [TimeSetRequest](#timesetrequest) | - | server | World | `@since(3)` `@category(world)` |
| 0x7045 | WeatherSet | `perlin_weather_set` | [WeatherSetRequest](#weathersetrequest) | - | server | World | `@since(3)` `@category(world)` |
| 0x7046 | GameRuleGet | `perlin_game_rule_get` | [GameRuleGetRequest](#gamerulegetrequest) | `i32` | server | - | `@since(3)` `@category(world)` |
| 0x7047 | GameRuleSet | `perlin_game_rule_set` | [GameRuleSetRequest](#gamerulesetrequest) | - | server | Admin | `@since(3)` `@category(world)` |
| 0x7048 | DifficultySet | `perlin_difficulty_set` | [DifficultySetRequest](#difficultysetrequest) | - | server | Admin | `@since(3)` `@category(world)` |
| 0x7049 | Explode | `perlin_explode` | [ExplodeRequest](#exploderequest) | - | server | World | `@since(3)` `@category(world)` |
| 0x704A | HighestBlock | `perlin_highest_block` | [HighestBlockRequest](#highestblockrequest) | `i32` | server | - | `@since(3)` `@category(world)` |
| 0x704B | ParticleAt | `perlin_particle_at` | [ParticleAtRequest](#particleatrequest) | - | server | - | `@since(3)` `@category(world)` |
| 0x704C | DropItem | `perlin_drop_item` | [DropItemRequest](#dropitemrequest) | - | server | World | `@since(3)` `@category(world)` |
| 0x704D | WorldSpawnGet | `perlin_world_spawn_get` | - | [BlockCoord](#blockcoord) | server | - | `@since(3)` `@category(world)` |
| 0x704E | WorldSpawnSet | `perlin_world_spawn_set` | [BlockCoord](#blockcoord) | - | server | Admin | `@since(3)` `@category(world)` |
| 0x7060 | EntitySpawn | `perlin_entity_spawn` | [EntitySpawnRequest](#entityspawnrequest) | `u64` | server | World | `@since(3)` `@category(entity)` |
| 0x7061 | EntityList | `perlin_entity_list` | [EntityListRequest](#entitylistrequest) | [EntityListResponse](#entitylistresponse) | server | - | `@since(3)` `@category(entity)` |
| 0x7062 | EntityRemove | `perlin_entity_remove` | [EntityRemoveRequest](#entityremoverequest) | - | server | World | `@since(3)` `@category(entity)` |
| 0x7063 | EntityDamage | `perlin_entity_damage` | [EntityDamageRequest](#entitydamagerequest) | `bool` | server | World | `@since(3)` `@category(entity)` |
| 0x7064 | EntityTeleport | `perlin_entity_teleport` | [EntityTeleportRequest](#entityteleportrequest) | - | server | World | `@since(3)` `@category(entity)` |
| 0x7065 | EntitySetVelocity | `perlin_entity_set_velocity` | [EntitySetVelocityRequest](#entitysetvelocityrequest) | - | server | World | `@since(3)` `@category(entity)` |
| 0x7066 | EntityInfo | `perlin_entity_info` | [EntityInfoRequest](#entityinforequest) | [ServerEntity](#serverentity) | server | - | `@since(3)` `@category(entity)` |
| 0x7067 | EntitySetHealth | `perlin_entity_set_health` | [EntitySetHealthRequest](#entitysethealthrequest) | - | server | World | `@since(3)` `@category(entity)` |
| 0x7080 | RunCommand | `perlin_run_command` | [RunCommandRequest](#runcommandrequest) | `rest_text` | server | Admin | `@since(3)` `@category(server)` |
| 0x7081 | MotdSet | `perlin_motd_set` | [MotdSetRequest](#motdsetrequest) | - | server | Admin | `@since(3)` `@category(server)` |
| 0x7082 | LootTableOverride | `perlin_loot_table_override` | [LootTableOverrideRequest](#loottableoverriderequest) | - | server | World | `@since(3)` `@category(loot)` |
| 0x7083 | LootTableGet | `perlin_loot_table_get` | [LootTableGetRequest](#loottablegetrequest) | `rest_text` | server | - | `@since(3)` `@category(loot)` |
| 0x7084 | EventAmend | `perlin_event_amend` | [EventAmendRequest](#eventamendrequest) | - | server | - | changes the event being handled: PlayerDamage amount, EntityDamage amount, PlayerChat message, Explosion power, ServerCommand line, PreLogin reason `@since(3)` `@category(events)` |

## Events

| Id | Event | Payload | Hosts | Notes |
|---|---|---|---|---|
| 1 | Load | - | client, server | `@internal` |
| 2 | Unload | - | client, server | `@internal` |
| 3 | Tick | [EventTick](#eventtick) | client, server | `@hot` |
| 4 | Frame | [EventFrame](#eventframe) | client | `@hot` |
| 5 | ChatReceived | [EventChat](#eventchat) | client | `@cancellable` |
| 6 | ChatSending | [EventText](#eventtext) | client | `@cancellable` |
| 7 | Connected | [EventConnection](#eventconnection) | client |  |
| 8 | Disconnected | [EventConnection](#eventconnection) | client |  |
| 9 | Key | [EventKey](#eventkey) | client |  |
| 10 | Command | [EventCommand](#eventcommand) | client, server | `@internal` |
| 11 | Damage | [EventDamage](#eventdamage) | client |  |
| 12 | Death | [EventText](#eventtext) | client |  |
| 13 | Respawned | - | client |  |
| 14 | DimensionChanged | [EventDimension](#eventdimension) | client |  |
| 15 | ScreenChanged | [EventScreen](#eventscreen) | client |  |
| 16 | SlotChanged | [EventSlot](#eventslot) | client |  |
| 17 | EntityAdded | [EventEntity](#evententity) | client |  |
| 18 | EntityRemoved | [EventEntity](#evententity) | client |  |
| 19 | Sound | [EventSound](#eventsound) | client | `@cancellable` |
| 20 | HttpResponse | [EventHttp](#eventhttp) | client, server | `@internal` |
| 21 | Packet | [EventPacket](#eventpacket) | client | `@cancellable` `@hot` |
| 22 | Title | [EventTitle](#eventtitle) | client | `@cancellable` |
| 23 | SettingChanged | [EventSettingChanged](#eventsettingchanged) | client |  |
| 24 | Look | [EventLook](#eventlook) | client | `@hot` `@cancellable` |
| 25 | MouseMove | [EventMouseMove](#eventmousemove) | client | `@hot` |
| 26 | MouseButton | [EventMouseButton](#eventmousebutton) | client | `@cancellable` |
| 27 | MouseWheel | [EventMouseWheel](#eventmousewheel) | client | `@cancellable` |
| 28 | KeyRaw | [EventKeyRaw](#eventkeyraw) | client | `@cancellable` |
| 29 | TextInput | [EventText](#eventtext) | client | `@cancellable` |
| 30 | ScreenRender | [EventScreenRender](#eventscreenrender) | client | `@hot` |
| 31 | SlotClick | [EventSlotClick](#eventslotclick) | client | `@cancellable` |
| 32 | Interact | [EventInteract](#eventinteract) | client | `@cancellable` |
| 33 | ChunkLoaded | [EventChunk](#eventchunk) | client |  |
| 34 | ChunkUnloaded | [EventChunk](#eventchunk) | client |  |
| 35 | BlockChanged | [EventBlockChanged](#eventblockchanged) | client, server |  |
| 36 | ResourcesReloaded | - | client |  |
| 37 | LevelLoaded | [EventLevel](#eventlevel) | client |  |
| 38 | LevelUnloaded | - | client |  |
| 39 | ModsLoaded | - | client, server |  |
| 40 | ClientStopping | - | client |  |
| 41 | Toast | [EventToast](#eventtoast) | client | `@cancellable` |
| 42 | ModMessage | [EventModMessage](#eventmodmessage) | client, server |  |
| 43 | Tooltip | [EventTooltip](#eventtooltip) | client |  |
| 44 | ScreenButton | [EventScreenButton](#eventscreenbutton) | client |  |
| 45 | WindowFocus | [EventWindow](#eventwindow) | client |  |
| 46 | ServerPing | [EventServerPing](#eventserverping) | client |  |
| 47 | PlayerJoin | [EventPlayer](#eventplayer) | server |  |
| 48 | PlayerQuit | [EventPlayer](#eventplayer) | server |  |
| 49 | PlayerChat | [EventPlayerChat](#eventplayerchat) | server | `@cancellable` |
| 50 | BlockBreak | [EventPlayerBlock](#eventplayerblock) | server | `@cancellable` |
| 51 | BlockPlace | [EventPlayerBlock](#eventplayerblock) | server | `@cancellable` |
| 52 | PlayerDamage | [EventServerDamage](#eventserverdamage) | server | `@since(3)` `@cancellable` |
| 53 | PlayerDeath | [EventServerDeath](#eventserverdeath) | server | `@since(3)` |
| 54 | PlayerRespawn | [EventPlayer](#eventplayer) | server | `@since(3)` |
| 55 | PlayerUseBlock | [EventPlayerUse](#eventplayeruse) | server | `@since(3)` `@cancellable` |
| 56 | PlayerAttack | [EventPlayerAttack](#eventplayerattack) | server | `@since(3)` `@cancellable` |
| 57 | EntityDamage | [EventEntityDamage](#evententitydamage) | server | `@since(3)` `@cancellable` |
| 58 | EntityDeath | [EventEntityDeath](#evententitydeath) | server | `@since(3)` |
| 59 | EntitySpawn | [EventEntitySpawn](#evententityspawn) | server | `@since(3)` `@cancellable` |
| 60 | ItemConsume | [EventItem](#eventitem) | server | `@since(3)` `@cancellable` |
| 61 | GameModeChange | [EventGameModeChange](#eventgamemodechange) | server | `@since(3)` |
| 62 | FormResponse | [EventFormResponse](#eventformresponse) | server | `@since(3)` |
| 63 | PlayerMove | [EventPlayerMove](#eventplayermove) | server | `@since(3)` `@hot` |
| 64 | Explosion | [EventExplosion](#eventexplosion) | server | `@since(3)` `@cancellable` |
| 65 | ItemDrop | [EventItem](#eventitem) | server | `@since(3)` `@cancellable` |
| 66 | ItemPickup | [EventItem](#eventitem) | server | `@since(3)` `@cancellable` |
| 67 | ContainerOpen | [EventContainer](#eventcontainer) | server | `@since(3)` `@cancellable` |
| 68 | DimensionChange | [EventPlayerDimension](#eventplayerdimension) | server | `@since(3)` |
| 69 | ServerCommand | [EventServerCommand](#eventservercommand) | server | `@since(3)` `@cancellable` |
| 70 | PreLogin | [EventPreLogin](#eventprelogin) | server | `@since(3)` `@cancellable` |

## Enums

### Status

Result of every call; negative values are returned by perlin.call (enum over `i32`)

`Ok`=0, `UnknownOp`=-1, `Denied`=-2, `BadArguments`=-3, `Unavailable`=-4, `NotFound`=-5, `Failed`=-6

### Permission

Capabilities a plugin manifest requests (flags over `u32`)

`Network`=1<<0, `Packets`=1<<1, `Control`=1<<2, `Storage`=1<<3, `Files`=1<<4, `Native`=1<<5, `World`=1<<6, `Players`=1<<7, `Admin`=1<<8

### Draw

(enum over `u8`)

`Rect`=1, `Text`=2, `Item`=3, `Image`=4, `Line`=5, `Gradient`=6, `Border`=7

### Screen

(enum over `i32`)

`None`=0, `Pause`=1, `Chat`=2, `Inventory`=3, `Options`=4, `Mods`=5, `Wardrobe`=6, `Death`=7, `Workbench`=8, `Menu`=9, `Other`=10, `Custom`=11

### HudElement

(enum over `i32`)

`Hearts`=0, `Armor`=1, `Hunger`=2, `Air`=3, `Hotbar`=4, `Experience`=5, `Debug`=6, `Chat`=7, `Effects`=8, `Locator`=9, `Fire`=10, `Subtitles`=11, `Crosshair`=12

### InteractKind

(enum over `i32`)

`StartBreak`=0, `Attack`=1, `InteractEntity`=2, `UseOnBlock`=3, `UseItem`=4

### RenderStage

(enum over `i32`)

`AfterTerrain`=0, `AfterEntities`=1, `Overlay`=2

### KeyContext

(enum over `i32`)

`InGame`=0, `InScreen`=1, `Any`=2

### GameMode

(enum over `i32`)

`Survival`=0, `Creative`=1, `Adventure`=2, `SurvivalSpectator`=3, `CreativeSpectator`=4, `Default`=5, `Spectator`=6

### Dimension

(enum over `i32`)

`Overworld`=0, `Nether`=1, `End`=2

### Difficulty

(enum over `i32`)

`Peaceful`=0, `Easy`=1, `Normal`=2, `Hard`=3

### PermissionLevel

(enum over `i32`)

`Unknown`=-1, `Visitor`=0, `Member`=1, `Operator`=2, `Custom`=3

### Face

(enum over `i32`)

`Down`=0, `Up`=1, `North`=2, `South`=3, `West`=4, `East`=5

### AimKind

(enum over `i32`)

`None`=0, `Block`=1, `Entity`=2

### ChatType

(enum over `i32`)

`Raw`=0, `Chat`=1, `Translation`=2, `Popup`=3, `JukeboxPopup`=4, `Tip`=5, `System`=6, `Whisper`=7, `Announcement`=8, `ObjectWhisper`=9, `Object`=10, `ObjectAnnouncement`=11

### DisconnectReason

(enum over `i32`)

`Unknown`=0, `CantConnectNoInternet`=1, `NoPermissions`=2, `UnrecoverableError`=3, `ThirdPartyBlocked`=4, `ThirdPartyNoInternet`=5, `ThirdPartyBadIp`=6, `ThirdPartyNoServerOrServerLocked`=7, `VersionMismatch`=8, `SkinIssue`=9, `InviteSessionNotFound`=10

### EffectId

(enum over `i32`)

`Speed`=1, `Slowness`=2, `Haste`=3, `MiningFatigue`=4, `Strength`=5, `InstantHealth`=6, `InstantDamage`=7, `JumpBoost`=8, `Nausea`=9, `Regeneration`=10, `Resistance`=11, `FireResistance`=12, `WaterBreathing`=13, `Invisibility`=14, `Blindness`=15, `NightVision`=16, `Hunger`=17, `Weakness`=18, `Poison`=19, `Wither`=20, `HealthBoost`=21, `Absorption`=22, `Saturation`=23, `Levitation`=24, `FatalPoison`=25, `ConduitPower`=26, `SlowFalling`=27, `BadOmen`=28, `VillageHero`=29, `Darkness`=30, `TrialOmen`=31, `RaidOmen`=32, `WindCharged`=33, `Weaving`=34, `Oozing`=35, `Infested`=36

### Perspective

(enum over `i32`)

`FirstPerson`=0, `ThirdPersonBack`=1, `ThirdPersonFront`=2

### SettingKind

(enum over `u8`)

`Toggle`=0, `Integer`=1, `Number`=2, `Choice`=3, `Button`=4, `Label`=5, `Header`=6

### Area

(enum over `u8`)

`Mod`=0, `Screenshots`=1, `ResourcePacks`=2, `ShaderPacks`=3

### EntityFlag

(enum over `u8`)

`OnFire`=0, `Sneaking`=1, `Riding`=2, `Sprinting`=3, `UsingItem`=4, `Invisible`=5, `Tempted`=6, `InLove`=7, `Saddled`=8, `Powered`=9, `Ignited`=10, `Baby`=11, `Converting`=12, `Critical`=13, `CanShowName`=14, `AlwaysShowName`=15, `NoAi`=16, `Silent`=17, `WallClimbing`=18, `CanClimb`=19, `CanSwim`=20, `CanFly`=21, `CanWalk`=22, `Resting`=23, `Sitting`=24, `Angry`=25, `Interested`=26, `Charged`=27, `Tamed`=28, `Orphaned`=29, `Leashed`=30, `Sheared`=31, `Gliding`=32, `Elder`=33, `Moving`=34, `Breathing`=35, `Chested`=36, `Stackable`=37, `ShowBase`=38, `Rearing`=39, `Vibrating`=40, `Idling`=41, `EvokerSpell`=42, `ChargeAttack`=43, `WasdControlled`=44, `CanPowerJump`=45, `Lingering`=46, `HasCollision`=47, `HasGravity`=48, `FireImmune`=49, `Dancing`=50, `Enchanted`=51, `ReturnTrident`=52, `ContainerPrivate`=53, `Transforming`=54, `DamageNearbyMobs`=55, `Swimming`=56, `Bribed`=57, `Pregnant`=58, `LayingEgg`=59, `PassengerCanPick`=60, `TransitionSitting`=61, `Eating`=62, `LayingDown`=63

### Tool

(enum over `u8`)

`None`=0, `Pickaxe`=1, `Axe`=2, `Shovel`=3, `Hoe`=4, `Shears`=5, `Sword`=6

### NameKind

(enum over `u8`)

`Item`=0, `Block`=1, `Entity`=2

### DamageCause

Why a player or mob took damage (enum over `i32`)

`Generic`=0, `Mob`=1, `Player`=2, `Projectile`=3, `Explosion`=4, `Fall`=5, `Void`=6, `Lava`=7, `Fire`=8, `Burning`=9, `Drowning`=10, `Starving`=11, `Suffocation`=12, `Cactus`=13, `Magma`=14, `BerryBush`=15, `Poison`=16, `Wither`=17, `Magic`=18, `FallingBlock`=19, `Kill`=20, `Plugin`=21

### Weather

(enum over `i32`)

`Clear`=0, `Rain`=1, `Thunder`=2

### FormKind

(enum over `i32`)

`Message`=0, `Menu`=1, `Custom`=2

### FormElement

(enum over `i32`)

`Label`=0, `Toggle`=1, `Slider`=2, `StepSlider`=3, `Dropdown`=4, `Input`=5, `Header`=6, `Divider`=7

### LogLevel

(enum over `i32`)

`Debug`=0, `Info`=1, `Warn`=2, `Error`=3

## Aliases

- **Permissions** = flagset&lt;[Permission](#permission)&gt; 
- **EntityFlags** = indexset&lt;[EntityFlag](#entityflag), `u64`&gt; 

## Structs

### Item

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |
| count | `i32` | 0 |  |
| aux | `i32` | 0 |  |
| damage | `i32` | 0 |  |
| enchanted | `bool` | false |  |
| customName | `string` |  |  |
| enchants | list&lt;pair&lt;`i32`, `i32`&gt;&gt; |  |  |
| lore | list&lt;`string`&gt; |  |  |

### PlayerState

| Field | Type | Default | Notes |
|---|---|---|---|
| runtimeId | `u64` | 0 |  |
| uniqueId | `i64` | 0 |  |
| name | `string` |  |  |
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | 0 |  |
| vx | `f64` | 0 |  |
| vy | `f64` | 0 |  |
| vz | `f64` | 0 |  |
| yaw | `f32` | 0 |  |
| pitch | `f32` | 0 |  |
| headYaw | `f32` | 0 |  |
| eyeHeight | `f32` | 1.62 |  |
| onGround | `bool` | false |  |
| sprinting | `bool` | false |  |
| sneaking | `bool` | false |  |
| swimming | `bool` | false |  |
| gliding | `bool` | false |  |
| flying | `bool` | false |  |
| mayFly | `bool` | false |  |
| spectator | `bool` | false |  |
| onFire | `bool` | false |  |
| riding | `bool` | false |  |
| connected | `bool` | false |  |
| gameMode | [GameMode](#gamemode) | Survival |  |
| dimension | [Dimension](#dimension) | Overworld |  |
| selectedSlot | `i32` | 0 |  |
| permission | [PermissionLevel](#permissionlevel) | Unknown |  |
| health | `f32` | 20 |  |
| maxHealth | `f32` | 20 |  |
| absorption | `f32` | 0 |  |
| hunger | `f32` | 20 |  |
| saturation | `f32` | 5 |  |
| xpProgress | `f32` | 0 |  |
| walkSpeed | `f32` | 0.1 |  |
| xpLevel | `i32` | 0 |  |
| air | `i32` | 300 |  |
| maxAir | `i32` | 300 |  |

### Effect

| Field | Type | Default | Notes |
|---|---|---|---|
| id | [EffectId](#effectid) |  |  |
| amplifier | `i32` | 0 |  |
| ticks | `i32` | 0 |  |
| ambient | `bool` | false |  |
| particles | `bool` | true |  |

### Inventory

| Field | Type | Default | Notes |
|---|---|---|---|
| slots | array&lt;[Item](#item), 36&gt; |  |  |
| armor | array&lt;[Item](#item), 4&gt; |  |  |
| offhand | [Item](#item) |  |  |
| cursor | [Item](#item) |  |  |
| selected | `i32` | 0 |  |
| open | `bool` | false |  |

### Attribute

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |
| min | `f32` | 0 |  |
| value | `f32` | 0 |  |
| max | `f32` | 0 |  |

### Entity

| Field | Type | Default | Notes |
|---|---|---|---|
| runtimeId | `u64` | 0 |  |
| uniqueId | `i64` | 0 |  |
| type | `string` |  |  |
| name | `string` |  |  |
| nameTag | `string` |  |  |
| uuid | `string` |  |  |
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | 0 |  |
| yaw | `f32` | 0 |  |
| pitch | `f32` | 0 |  |
| headYaw | `f32` | 0 |  |
| health | `f32` | -1 |  |
| scale | `f32` | 1 |  |
| onGround | `bool` | false |  |
| isPlayer | `bool` | false |  |
| isItem | `bool` | false |  |
| isSelf | `bool` | false |  |
| flags | [EntityFlags](#entityflags) |  |  |
| itemName | `string` |  |  |
| itemCount | `i32` | 0 |  |
| mainHand | `string` |  |  |
| offHand | `string` |  |  |
| armor | array&lt;`string`, 4&gt; |  |  |
| vehicleUid | `i64` | 0 |  |
| ownerUid | `i64` | 0 |  |
| targetUid | `i64` | 0 |  |
| variant | `i32` | 0 |  |
| markVariant | `i32` | 0 |  |
| color | `i32` | 0 |  |
| maxHealth | `f32` | -1 |  |
| dying | `bool` | false |  |
| attributes | list&lt;[Attribute](#attribute)&gt; |  |  |
| properties | list&lt;pair&lt;`string`, `f32`&gt;&gt; |  |  |
| propertyText | list&lt;pair&lt;`string`, `string`&gt;&gt; |  |  |

### PlayerEntry

| Field | Type | Default | Notes |
|---|---|---|---|
| uuid | `string` |  |  |
| name | `string` |  |  |
| uid | `i64` | 0 |  |
| permission | [PermissionLevel](#permissionlevel) | Unknown |  |

### WorldState

| Field | Type | Default | Notes |
|---|---|---|---|
| dimension | [Dimension](#dimension) | Overworld |  |
| difficulty | [Difficulty](#difficulty) | Normal |  |
| worldGameMode | [GameMode](#gamemode) | Survival |  |
| day | `i64` | 0 |  |
| timeOfDay | `i32` | 0 |  |
| totalTicks | `f64` | 0 |  |
| rain | `f32` | 0 |  |
| thunder | `f32` | 0 |  |
| commandsEnabled | `bool` | true |  |
| haveSpawn | `bool` | false |  |
| spawnX | `i32` | 0 |  |
| spawnY | `i32` | 0 |  |
| spawnZ | `i32` | 0 |  |
| rules | list&lt;pair&lt;`string`, `i32`&gt;&gt; |  |  |

### BlockInfo

| Field | Type | Default | Notes |
|---|---|---|---|
| loaded | `bool` | false |  |
| runtimeId | `u32` | 0 |  |
| name | `string` |  |  |
| biome | `i32` | 0 |  |
| liquid | `u32` | 0 |  |
| states | list&lt;pair&lt;`string`, `string`&gt;&gt; |  |  |

### BlockProps

| Field | Type | Default | Notes |
|---|---|---|---|
| known | `bool` | false |  |
| hardness | `f32` | 0 |  |
| tool | [Tool](#tool) | None |  |
| harvestLevel | `i32` | 0 |  |
| needsSupportBelow | `bool` | false |  |
| breaksWholeStack | `bool` | false |  |
| displayName | `string` |  |  |
| properties | list&lt;`string`&gt; |  |  |

### BlockEntityInfo

| Field | Type | Default | Notes |
|---|---|---|---|
| present | `bool` | false |  |
| id | `string` |  |  |
| color | `i32` | -1 |  |
| waxed | `bool` | false |  |
| rotation | `f32` | 0 |  |
| frontText | `string` |  |  |
| backText | `string` |  |  |
| frontGlow | `bool` | false |  |
| backGlow | `bool` | false |  |
| base | `i32` | -1 |  |
| bannerType | `i32` | 0 |  |
| patterns | list&lt;pair&lt;`string`, `i32`&gt;&gt; |  |  |
| nbt | `string` |  |  |

### BreakState

| Field | Type | Default | Notes |
|---|---|---|---|
| active | `bool` | false |  |
| x | `i32` | 0 |  |
| y | `i32` | 0 |  |
| z | `i32` | 0 |  |
| progress | `f32` | 0 |  |

### RayHit

| Field | Type | Default | Notes |
|---|---|---|---|
| hit | `bool` | false |  |
| x | `i32` | 0 |  |
| y | `i32` | 0 |  |
| z | `i32` | 0 |  |
| face | [Face](#face) | Up |  |
| hx | `f64` | 0 |  |
| hy | `f64` | 0 |  |
| hz | `f64` | 0 |  |
| distance | `f64` | 0 |  |
| block | `string` |  |  |

### AimState

| Field | Type | Default | Notes |
|---|---|---|---|
| kind | [AimKind](#aimkind) | None |  |
| block | [RayHit](#rayhit) |  |  |
| entity | `u64` | 0 |  |
| entityType | `string` |  |  |
| distance | `f64` | 0 |  |

### ScreenState

| Field | Type | Default | Notes |
|---|---|---|---|
| guiWidth | `i32` | 0 |  |
| guiHeight | `i32` | 0 |  |
| guiScale | `i32` | 1 |  |
| framebufferWidth | `i32` | 0 |  |
| framebufferHeight | `i32` | 0 |  |
| fps | `f32` | 0 |  |
| frameMs | `f32` | 0 |  |
| screen | [Screen](#screen) | None |  |
| paused | `bool` | false |  |
| connected | `bool` | false |  |
| hudHidden | `bool` | false |  |
| debug | `bool` | false |  |
| server | `string` |  |  |
| language | `string` |  |  |
| platform | `string` |  |  |
| gameVersion | `string` |  |  |

### Meta

| Field | Type | Default | Notes |
|---|---|---|---|
| apiVersion | `u32` | ApiVersion |  |
| gameVersion | `string` |  |  |
| plugin | `string` |  |  |
| platform | `string` |  |  |
| permissions | [Permissions](#permissions) |  |  |
| nowMs | `i64` | 0 |  |
| tick | `u64` | 0 |  |

### InputOverrideState

| Field | Type | Default | Notes |
|---|---|---|---|
| forward | `f32` | 0 |  |
| strafe | `f32` | 0 |  |
| jump | `bool` | false |  |
| sneak | `bool` | false |  |
| sprint | `bool` | false |  |
| attack | `bool` | false |  |
| use | `bool` | false |  |
| ticks | `i32` | 1 | `@unit(ticks)` |

### CameraControlState

| Field | Type | Default | Notes |
|---|---|---|---|
| roll | `f32` | 0 | `@unit(degrees)` |
| fovAdd | `f32` | 0 |  |
| fovSet | `f32` | 0 |  |
| offsetX | `f32` | 0 |  |
| offsetY | `f32` | 0 |  |
| offsetZ | `f32` | 0 |  |
| yawAdd | `f32` | 0 |  |
| pitchAdd | `f32` | 0 |  |
| ticks | `i32` | 2 | `@unit(ticks)` |

### CameraInfo

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | 0 |  |
| yaw | `f32` | 0 |  |
| pitch | `f32` | 0 |  |
| fov | `f32` | 70 |  |
| aspect | `f32` | 1.78 |  |
| nearPlane | `f32` | 0.1 |  |
| farPlane | `f32` | 600 |  |
| perspective | `i32` | 0 |  |
| viewProjection | array&lt;`f32`, 16&gt; |  |  |

### WorldPoint

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | 0 |  |

### ScreenPoint

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `f32` | 0 |  |
| y | `f32` | 0 |  |
| depth | `f32` | 0 |  |
| visible | `bool` | false |  |

### MotionRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| add | `bool` | false |  |
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | 0 |  |
| axisX | `bool` | true |  |
| axisY | `bool` | true |  |
| axisZ | `bool` | true |  |

### TeleportRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | 0 |  |
| keepVelocity | `bool` | false |  |

### ModelPose

| Field | Type | Default | Notes |
|---|---|---|---|
| roll | `f32` | 0 |  |
| pitch | `f32` | 0 |  |
| yaw | `f32` | 0 |  |
| ticks | `i32` | 2 |  |

### LookSet

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | -1 |  |

### WorldLine

| Field | Type | Default | Notes |
|---|---|---|---|
| x1 | `f64` | 0 |  |
| y1 | `f64` | 0 |  |
| z1 | `f64` | 0 |  |
| x2 | `f64` | 0 |  |
| y2 | `f64` | 0 |  |
| z2 | `f64` | 0 |  |
| color | `u32` | 0xFFFFFFFF |  |
| width | `f32` | 1.0 |  |

### WorldLines

| Field | Type | Default | Notes |
|---|---|---|---|
| ticks | `i32` | 2 |  |
| lines | list&lt;[WorldLine](#worldline)&gt; |  |  |

### WorldBox

| Field | Type | Default | Notes |
|---|---|---|---|
| x0 | `f64` | 0 |  |
| y0 | `f64` | 0 |  |
| z0 | `f64` | 0 |  |
| x1 | `f64` | 1 |  |
| y1 | `f64` | 1 |  |
| z1 | `f64` | 1 |  |
| color | `u32` | 0xFFFFFFFF |  |
| width | `f32` | 1.0 |  |

### WorldBoxes

| Field | Type | Default | Notes |
|---|---|---|---|
| ticks | `i32` | 2 |  |
| boxes | list&lt;[WorldBox](#worldbox)&gt; |  |  |

### ClientBlockEdit

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `i32` | 0 |  |
| y | `i32` | 0 |  |
| z | `i32` | 0 |  |
| runtimeId | `u32` | 0 |  |

### ClientBlockEdits

| Field | Type | Default | Notes |
|---|---|---|---|
| edits | list&lt;[ClientBlockEdit](#clientblockedit)&gt; |  |  |

### BlockQuery

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |
| states | list&lt;pair&lt;`string`, `string`&gt;&gt; |  |  |

### BlockQueryResult

| Field | Type | Default | Notes |
|---|---|---|---|
| found | `bool` | false |  |
| runtimeId | `u32` | 0 |  |

### PacketDropRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| ticks | `i32` | 20 |  |
| ids | list&lt;`u32`&gt; |  |  |
| outgoing | `bool` | false |  |

### PacketCount

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `u32` | 0 |  |
| count | `u32` | 0 |  |

### Region

| Field | Type | Default | Notes |
|---|---|---|---|
| x0 | `i32` | 0 |  |
| y0 | `i32` | 0 |  |
| z0 | `i32` | 0 |  |
| sx | `i32` | 0 |  |
| sy | `i32` | 0 |  |
| sz | `i32` | 0 |  |
| palette | list&lt;`string`&gt; |  |  |
| cells | list&lt;`u16`&gt; |  |  |

### FoundBlock

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `i32` | 0 |  |
| y | `i32` | 0 |  |
| z | `i32` | 0 |  |
| name | `string` |  |  |

### SettingDef

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `string` |  |  |
| kind | [SettingKind](#settingkind) | Toggle |  |
| label | `string` |  |  |
| description | `string` |  |  |
| icon | `string` |  |  |
| value | `string` |  |  |
| fallback | `string` |  |  |
| min | `f64` | 0 |  |
| max | `f64` | 1 |  |
| step | `f64` | 0 |  |
| choices | list&lt;`string`&gt; |  |  |
| choiceLabels | list&lt;`string`&gt; |  |  |
| unit | `string` |  |  |
| restart | `bool` | false |  |

### SettingEntry

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `string` |  |  |
| value | `string` |  |  |

### FileLocation

| Field | Type | Default | Notes |
|---|---|---|---|
| area | [Area](#area) | Mod |  |
| path | `string` |  |  |

### ModEntry

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |
| displayName | `string` |  |  |
| description | `string` |  |  |
| version | `string` |  |  |
| author | `string` |  |  |
| state | `string` |  |  |
| enabled | `bool` | true |  |
| self | `bool` | false |  |
| hasSettings | `bool` | false |  |
| website | `string` |  |  |
| arch | `string` |  |  |
| hooks | list&lt;`string`&gt; |  |  |
| priority | `i32` | 0 |  |
| permissions | [Permissions](#permissions) |  |  |

### HudElementState

| Field | Type | Default | Notes |
|---|---|---|---|
| element | [HudElement](#hudelement) | Hearts |  |
| visible | `bool` | true |  |
| dx | `f32` | 0 |  |
| dy | `f32` | 0 |  |

### DebugLinesData

| Field | Type | Default | Notes |
|---|---|---|---|
| left | list&lt;`string`&gt; |  |  |
| right | list&lt;`string`&gt; |  |  |

### FogOverride

| Field | Type | Default | Notes |
|---|---|---|---|
| enabled | `bool` | true |  |
| color | `u32` | 0 | 0 keeps the vanilla colour; else 0xAARRGGBB |
| start | `f32` | -1 | `@unit(blocks)` negative keeps vanilla |
| end | `f32` | -1 | `@unit(blocks)` |
| ticks | `i32` | 2 |  |

### ScreenTintData

| Field | Type | Default | Notes |
|---|---|---|---|
| color | `u32` | 0 | 0xAARRGGBB drawn over the world |
| vignette | `f32` | 0 | 0..1 darkening at the edges |
| ticks | `i32` | 2 |  |

### EntityOverride

| Field | Type | Default | Notes |
|---|---|---|---|
| entity | `u64` | 0 | runtime id, or 0 to match every entity of type |
| type | `string` |  |  |
| hidden | `bool` | false |  |
| scaleX | `f32` | 1 |  |
| scaleY | `f32` | 1 |  |
| scaleZ | `f32` | 1 |  |
| tint | `u32` | 0xFFFFFFFF |  |
| texture | `string` |  | resource-pack texture path; empty keeps the vanilla one |
| nameTag | `i32` | 0 | 0 keep, 1 hide, 2 replace with nameText |
| nameText | `string` |  |  |
| ticks | `i32` | 2 |  |

### MeshVertex

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | 0 |  |
| u | `f32` | 0 |  |
| v | `f32` | 0 |  |
| color | `u32` | 0xFFFFFFFF |  |

### WorldMeshData

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `string` |  |  |
| stage | [RenderStage](#renderstage) | AfterEntities |  |
| texture | `u32` | 0 | gfx texture id, 0 for plain colour |
| depthTest | `bool` | true |  |
| ticks | `i32` | 2 |  |
| triangles | list&lt;[MeshVertex](#meshvertex)&gt; |  | three vertices per triangle |

### FrameLimitData

| Field | Type | Default | Notes |
|---|---|---|---|
| fps | `i32` | 0 | 0 keeps the player's setting |
| unfocusedFps | `i32` | 0 | while unfocused or minimized; 0 keeps fps |

### WindowInfo

| Field | Type | Default | Notes |
|---|---|---|---|
| focused | `bool` | true |  |
| minimized | `bool` | false |  |
| width | `i32` | 0 |  |
| height | `i32` | 0 |  |

### ScreenButtonDef

| Field | Type | Default | Notes |
|---|---|---|---|
| screen | [Screen](#screen) | Pause |  |
| id | `string` |  |  |
| label | `string` |  |  |
| x | `i32` | 0 |  |
| y | `i32` | 0 |  |
| width | `i32` | 98 |  |
| height | `i32` | 20 | `@unit(gui pixels)` |

### LoadingOverlayData

| Field | Type | Default | Notes |
|---|---|---|---|
| hidden | `bool` | false |  |

### SkinOverrideData

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `string` |  | player name, empty for yourself |
| slim | `bool` | false |  |
| png | `bytes` |  | empty removes the override |

### AmbientSoundData

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `string` |  |  |
| sound | `string` |  |  |
| volume | `f32` | 1 |  |
| pitch | `f32` | 1 |  |
| play | `bool` | true |  |

### ServerPingRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| host | `string` |  |  |
| port | `u16` | 19132 |  |

### ModMessageData

| Field | Type | Default | Notes |
|---|---|---|---|
| to | `string` |  | empty sends to every other mod |
| channel | `string` |  |  |
| body | `bytes` |  |  |

### PackRegistration

| Field | Type | Default | Notes |
|---|---|---|---|
| path | `string` |  | folder or .zip inside the mod's area |
| enabled | `bool` | true |  |

### ParticleRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | 0 |  |
| count | `i32` | 1 |  |
| spread | `f32` | 0 |  |

### KeyBindEx

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `string` |  |  |
| label | `string` |  |  |
| key | `string` |  |  |
| ctrl | `bool` | false |  |
| shift | `bool` | false |  |
| alt | `bool` | false |  |
| context | [KeyContext](#keycontext) | InGame |  |

### ChatLine

| Field | Type | Default | Notes |
|---|---|---|---|
| text | `string` |  |  |
| alpha | `f32` | 1 |  |

### PackEntry

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |
| server | `bool` | false |  |

### TitleStateData

| Field | Type | Default | Notes |
|---|---|---|---|
| title | `string` |  |  |
| subtitle | `string` |  |  |
| actionBar | `string` |  |  |
| titleAlpha | `f32` | 0 |  |
| actionBarAlpha | `f32` | 0 |  |

### GfxVertex

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `f32` | 0 |  |
| y | `f32` | 0 |  |
| u | `f32` | 0 |  |
| v | `f32` | 0 |  |
| color | `u32` | 0xFFFFFFFF |  |

### GfxCmd

| Field | Type | Default | Notes |
|---|---|---|---|
| clip | array&lt;`f32`, 4&gt; |  |  |
| texture | `u32` | 0 |  |
| indexOffset | `u32` | 0 |  |
| indexCount | `u32` | 0 |  |
| vertexOffset | `u32` | 0 |  |

### GfxHeader

| Field | Type | Default | Notes |
|---|---|---|---|
| vertexCount | `u32` | 0 |  |
| indexCount | `u32` | 0 |  |
| commandCount | `u32` | 0 |  |
| flags | `u32` | 0 |  |

### NativeHandles

| Field | Type | Default | Notes |
|---|---|---|---|
| api | `string` |  |  |
| adapter | `string` |  |  |
| window | `u64` | 0 |  |
| display | `u64` | 0 |  |
| device | `u64` | 0 |  |

### GfxDisplayInfo

| Field | Type | Default | Notes |
|---|---|---|---|
| width | `i32` | 0 |  |
| height | `i32` | 0 |  |
| guiScale | `i32` | 1 |  |
| mouseX | `f32` | 0 |  |
| mouseY | `f32` | 0 |  |
| mouseFree | `bool` | false |  |
| inGame | `bool` | false |  |

### TextMessage

| Field | Type | Default | Notes |
|---|---|---|---|
| text | `string` |  |  |

### PlayerText

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` | 0 |  |
| text | `string` |  |  |

### OnlinePlayer

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `u64` | 0 |  |
| name | `string` |  |  |
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | 0 |  |

### OnlinePlayerList

| Field | Type | Default | Notes |
|---|---|---|---|
| players | list&lt;[OnlinePlayer](#onlineplayer)&gt; |  |  |

### ServerTeleport

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` | 0 |  |
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | 0 |  |

### BlockCoord

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `i32` | 0 |  |
| y | `i32` | 0 |  |
| z | `i32` | 0 |  |

### BlockWrite

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `i32` | 0 |  |
| y | `i32` | 0 |  |
| z | `i32` | 0 |  |
| block | `string` |  |  |

### ServerStatus

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |
| players | `u32` | 0 |  |
| maxPlayers | `u32` | 0 |  |
| tick | `u64` | 0 |  |
| backend | `string` |  |  |

### GiveItemRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` | 0 |  |
| item | `string` |  |  |
| count | `u32` | 1 |  |

### GiveItemResult

| Field | Type | Default | Notes |
|---|---|---|---|
| leftover | `u32` | 0 | what did not fit in the inventory |

### EventNothing


### EventTick

| Field | Type | Default | Notes |
|---|---|---|---|
| dt | `f32` | 0.05 |  |
| tick | `u64` | 0 |  |

### EventFrame

| Field | Type | Default | Notes |
|---|---|---|---|
| dt | `f32` | 0 |  |
| fps | `f32` | 0 |  |
| guiWidth | `i32` | 0 |  |
| guiHeight | `i32` | 0 |  |

### EventChat

| Field | Type | Default | Notes |
|---|---|---|---|
| type | [ChatType](#chattype) | Raw |  |
| sender | `string` |  |  |
| message | `string` |  |  |
| raw | `string` |  |  |

### EventKey

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `string` |  |  |
| down | `bool` | false |  |

### EventCommand

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |
| args | `string` |  |  |
| player | `u64` | 0 | the player who ran it on a server; 0 on the client |

### EventConnection

| Field | Type | Default | Notes |
|---|---|---|---|
| server | `string` |  |  |
| message | `string` |  |  |
| reason | [DisconnectReason](#disconnectreason) | Unknown |  |

### EventDamage

| Field | Type | Default | Notes |
|---|---|---|---|
| known | `bool` | false |  |
| dx | `f32` | 0 |  |
| dz | `f32` | 0 |  |
| health | `f32` | 0 |  |

### EventText

| Field | Type | Default | Notes |
|---|---|---|---|
| text | `string` |  |  |

### EventDimension

| Field | Type | Default | Notes |
|---|---|---|---|
| from | [Dimension](#dimension) |  |  |
| to | [Dimension](#dimension) |  |  |

### EventScreen

| Field | Type | Default | Notes |
|---|---|---|---|
| from | [Screen](#screen) |  |  |
| to | [Screen](#screen) |  |  |

### EventSlot

| Field | Type | Default | Notes |
|---|---|---|---|
| slot | `i32` | 0 |  |
| item | [Item](#item) |  |  |

### EventEntity

| Field | Type | Default | Notes |
|---|---|---|---|
| entity | [Entity](#entity) |  |  |

### EventSound

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | 0 |  |
| volume | `f32` | 1 |  |
| pitch | `f32` | 1 |  |

### EventHttp

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `u32` | 0 |  |
| status | `i32` | 0 |  |
| error | `string` |  |  |
| headers | list&lt;pair&lt;`string`, `string`&gt;&gt; |  |  |
| body | `bytes` |  |  |

### EventPacket

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `u32` | 0 |  |
| outgoing | `bool` | false |  |
| body | `bytes` |  |  |

### EventTitle

| Field | Type | Default | Notes |
|---|---|---|---|
| title | `string` |  |  |
| subtitle | `string` |  |  |
| actionBar | `string` |  |  |

### EventLook

| Field | Type | Default | Notes |
|---|---|---|---|
| dx | `f32` | 0 |  |
| dy | `f32` | 0 |  |
| dt | `f32` | 0 |  |
| dirX | `f64` | 0 |  |
| dirY | `f64` | 0 |  |
| dirZ | `f64` | -1 |  |
| vx | `f64` | 0 |  |
| vy | `f64` | 0 |  |
| vz | `f64` | 0 |  |
| keys | `u32` | 0 |  |
| gliding | `bool` | false |  |
| underwater | `bool` | false |  |
| thirdPerson | `bool` | false |  |
| onGround | `bool` | false |  |

### EventSettingChanged

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `string` |  |  |
| value | `string` |  |  |
| kind | [SettingKind](#settingkind) | Toggle |  |

### EventMouseMove

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `f32` | 0 |  |
| y | `f32` | 0 |  |

### EventMouseButton

| Field | Type | Default | Notes |
|---|---|---|---|
| button | `i32` | 0 |  |
| down | `bool` | false |  |
| x | `f32` | 0 |  |
| y | `f32` | 0 |  |

### EventMouseWheel

| Field | Type | Default | Notes |
|---|---|---|---|
| dx | `f32` | 0 |  |
| dy | `f32` | 0 |  |

### EventKeyRaw

| Field | Type | Default | Notes |
|---|---|---|---|
| key | `i32` | 0 |  |
| down | `bool` | false |  |
| repeat | `bool` | false |  |
| ctrl | `bool` | false |  |
| shift | `bool` | false |  |
| alt | `bool` | false |  |

### EventScreenRender

| Field | Type | Default | Notes |
|---|---|---|---|
| screen | [Screen](#screen) | None |  |
| guiWidth | `i32` | 0 |  |
| guiHeight | `i32` | 0 |  |
| mouseX | `f32` | 0 |  |
| mouseY | `f32` | 0 |  |

### EventSlotClick

| Field | Type | Default | Notes |
|---|---|---|---|
| slot | `i32` | 0 | same slot address as InventoryClick |
| right | `bool` | false |  |
| shift | `bool` | false |  |

### EventInteract

| Field | Type | Default | Notes |
|---|---|---|---|
| kind | [InteractKind](#interactkind) | StartBreak |  |
| x | `i32` | 0 |  |
| y | `i32` | 0 |  |
| z | `i32` | 0 |  |
| face | [Face](#face) | Up |  |
| entity | `u64` | 0 |  |

### EventChunk

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `i32` | 0 |  |
| z | `i32` | 0 |  |
| dimension | [Dimension](#dimension) | Overworld |  |

### EventBlockChanged

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `i32` | 0 |  |
| y | `i32` | 0 |  |
| z | `i32` | 0 |  |
| from | `string` |  |  |
| to | `string` |  |  |

### EventLevel

| Field | Type | Default | Notes |
|---|---|---|---|
| dimension | [Dimension](#dimension) | Overworld |  |
| server | `string` |  |  |

### EventToast

| Field | Type | Default | Notes |
|---|---|---|---|
| title | `string` |  |  |
| body | `string` |  |  |

### EventModMessage

| Field | Type | Default | Notes |
|---|---|---|---|
| from | `string` |  |  |
| channel | `string` |  |  |
| body | `bytes` |  |  |

### EventTooltip

| Field | Type | Default | Notes |
|---|---|---|---|
| item | [Item](#item) |  |  |
| lines | list&lt;`string`&gt; |  |  |

### EventScreenButton

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `string` |  |  |

### EventWindow

| Field | Type | Default | Notes |
|---|---|---|---|
| focused | `bool` | true |  |
| minimized | `bool` | false |  |

### EventServerPing

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `u32` | 0 |  |
| ok | `bool` | false |  |
| motd | `string` |  |  |
| version | `string` |  |  |
| players | `i32` | 0 |  |
| maxPlayers | `i32` | 0 |  |
| protocol | `i32` | 0 |  |
| latencyMs | `f32` | 0 |  |

### EventPlayer

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` | 0 |  |
| name | `string` |  |  |

### EventPlayerChat

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` | 0 |  |
| name | `string` |  |  |
| message | `string` |  |  |

### EventPlayerBlock

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` | 0 |  |
| x | `i32` | 0 |  |
| y | `i32` | 0 |  |
| z | `i32` | 0 |  |
| block | `string` |  |  |

### LogRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| level | [LogLevel](#loglevel) |  |  |
| text | `string` |  |  |

### PlayerEffectsResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| effects | list&lt;[Effect](#effect)&gt; |  |  |

### PlayerLookResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| yaw | `f32` |  |  |
| pitch | `f32` |  |  |

### InventorySelectRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| slot | `i32` |  |  |

### InventoryDropRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| count | `i32` |  | 1 to 64 items from the selected slot |

### InventorySwapRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| a | `i32` |  |  |
| b | `i32` |  |  |

### InventoryClickRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| slot | `i32` |  |  |
| right | `bool` |  |  |
| shift | `bool` |  |  |

### BlockRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `i32` |  |  |
| y | `i32` |  |  |
| z | `i32` |  |  |

### RegionRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| x0 | `i32` |  |  |
| y0 | `i32` |  |  |
| z0 | `i32` |  |  |
| x1 | `i32` |  |  |
| y1 | `i32` |  |  |
| z1 | `i32` |  |  |

### RaycastRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| ox | `f64` |  |  |
| oy | `f64` |  |  |
| oz | `f64` |  |  |
| dx | `f64` |  |  |
| dy | `f64` |  |  |
| dz | `f64` |  |  |
| maxDistance | `f64` |  |  |
| liquids | `bool` |  |  |

### FindBlocksRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `i32` |  |  |
| y | `i32` |  |  |
| z | `i32` |  |  |
| radius | `i32` |  |  |
| limit | `i32` |  |  |
| exact | `bool` |  |  |
| name | `string` |  |  |

### FindBlocksResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| blocks | list&lt;[FoundBlock](#foundblock)&gt; |  |  |

### ChunkLoadedRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `i32` |  |  |
| z | `i32` |  |  |

### BlockNamesResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| names | list&lt;`string`&gt; |  |  |

### BlockPropsRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |

### BlockEntityDataRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `i32` |  |  |
| y | `i32` |  |  |
| z | `i32` |  |  |

### DisplayNameRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| kind | [NameKind](#namekind) |  |  |
| id | `string` |  |  |
| aux | `i32` |  |  |

### MapChunkRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `i32` |  |  |
| z | `i32` |  |  |
| caveY | `i32` |  |  |

### EntitiesRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| radius | `f64` |  |  |
| type | `string` |  |  |
| playersOnly | `bool` |  |  |
| includeItems | `bool` |  |  |

### EntitiesResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| entities | list&lt;[Entity](#entity)&gt; |  |  |

### EntityByIdRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `u64` |  |  |

### PlayerListResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| players | list&lt;[PlayerEntry](#playerentry)&gt; |  |  |

### OptionGetRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| key | `string` |  |  |

### OptionSetRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| key | `string` |  |  |
| value | `string` |  |  |

### TranslateRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| key | `string` |  |  |

### PerspectiveSetRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| perspective | [Perspective](#perspective) |  |  |

### ResourcePacksResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| packs | list&lt;[PackEntry](#packentry)&gt; |  |  |

### InputDownRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |

### KeyRegisterRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `string` |  |  |
| label | `string` |  |  |
| fallback | `string` |  |  |

### InputActionsResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| actions | list&lt;`string`&gt; |  |  |

### MousePositionResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `f32` |  |  |
| y | `f32` |  |  |
| captured | `bool` |  |  |

### ChatRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| text | `string` |  |  |

### CommandRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| line | `string` |  |  |

### LocalMessageRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| text | `string` |  |  |

### CommandRegisterRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |
| usage | `string` |  |  |
| description | `string` |  |  |

### CommandUnregisterRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |

### ServerCommandsResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| names | list&lt;`string`&gt; |  |  |

### ToastRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| text | `string` |  |  |
| ms | `i32` |  | `@unit(ms)` |

### TitleRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| title | `string` |  |  |
| subtitle | `string` |  |  |
| fadeIn | `i32` |  |  |
| stay | `i32` |  |  |
| fadeOut | `i32` |  |  |

### ChatHistoryRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| count | `u32` |  | `@max(100)` |

### ChatHistoryResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| lines | list&lt;[ChatLine](#chatline)&gt; |  |  |

### UseOnBlockRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `i32` |  |  |
| y | `i32` |  |  |
| z | `i32` |  |  |
| face | [Face](#face) |  |  |

### AttackEntityRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| entity | `u64` |  |  |

### InteractEntityRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| entity | `u64` |  |  |

### BreakBlockRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `i32` |  |  |
| y | `i32` |  |  |
| z | `i32` |  |  |
| face | [Face](#face) |  |  |

### LookRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| yaw | `f32` |  |  |
| pitch | `f32` |  |  |

### PlaySoundRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |
| positional | `bool` |  |  |
| x | `f32` |  |  |
| y | `f32` |  |  |
| z | `f32` |  |  |
| volume | `f32` |  |  |
| pitch | `f32` |  |  |

### UiSubmitRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| layer | `string` |  |  |
| z | `i32` |  |  |
| commands | `bytes` |  |  |

### UiClearRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| layer | `string` |  |  |

### UiMeasureRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| text | `string` |  |  |
| scale | `f32` |  |  |

### GfxTextureCreateRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| width | `i32` |  |  |
| height | `i32` |  |  |
| linear | `bool` |  |  |
| rgba | `bytes` |  |  |

### GfxTextureUpdateRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `u32` |  |  |
| x | `i32` |  |  |
| y | `i32` |  |  |
| width | `i32` |  |  |
| height | `i32` |  |  |
| rgba | `bytes` |  |  |

### GfxTextureDestroyRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `u32` |  |  |

### GfxSubmitRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| layer | `string` |  |  |
| z | `i32` |  |  |
| blob | `bytes` |  | GfxHeader, GfxVertex[], indices, GfxCmd[] |

### GfxClearRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| layer | `string` |  |  |

### GfxImageLoadRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| path | `string` |  |  |

### GfxImageLoadResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| width | `i32` |  |  |
| height | `i32` |  |  |
| rgba | `bytes` |  |  |

### InputGrabRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| mouse | `bool` |  |  |
| keyboard | `bool` |  |  |

### StorageReadRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| path | `string` |  |  |

### StorageWriteRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| path | `string` |  |  |
| data | `bytes` |  |  |

### StorageListRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| path | `string` |  |  |

### StorageListResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| names | list&lt;`string`&gt; |  |  |

### StorageRemoveRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| path | `string` |  |  |

### StorageExistsRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| path | `string` |  |  |

### StorageMakeDirRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| path | `string` |  |  |

### HttpRequestRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `u32` |  |  |
| method | `string` |  |  |
| url | `string` |  |  |
| timeoutMs | `i32` |  |  |
| headers | list&lt;pair&lt;`string`, `string`&gt;&gt; |  |  |
| body | `bytes` |  |  |

### PacketSendRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `u32` |  |  |
| body | `bytes` |  |  |

### PacketNameRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `u32` |  |  |

### SettingGetRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `string` |  |  |

### SettingSetRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `string` |  |  |
| value | `string` |  |  |

### SettingListResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| settings | list&lt;[SettingEntry](#settingentry)&gt; |  |  |

### ModListResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| mods | list&lt;[ModEntry](#modentry)&gt; |  |  |

### IconSetRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| path | `string` |  |  |

### OpenScreenRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| screen | [Screen](#screen) |  |  |

### OpenModSettingsRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| mod | `string` |  |  |

### CopyToClipboardRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| text | `string` |  |  |

### OpenUrlRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| url | `string` |  |  |

### CopyFileRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| from | [FileLocation](#filelocation) |  |  |
| to | [FileLocation](#filelocation) |  |  |

### MoveFileRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| from | [FileLocation](#filelocation) |  |  |
| to | [FileLocation](#filelocation) |  |  |

### DownloadFileRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `u32` |  |  |
| url | `string` |  |  |
| to | [FileLocation](#filelocation) |  |  |

### PacketCountersResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| counters | list&lt;[PacketCount](#packetcount)&gt; |  |  |

### PacketInjectRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `u32` |  |  |
| body | `bytes` |  |  |

### WorldMeshClearRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `string` |  |  |

### ScreenButtonRemoveRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `string` |  |  |

### ScreenOpenCustomRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `string` |  |  |

### TooltipAppendRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| lines | list&lt;`string`&gt; |  |  |

### ShaderPackSetRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |

### ServerItem

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |
| count | `u32` | 0 |  |
| damage | `u32` | 0 |  |

### ServerInventory

| Field | Type | Default | Notes |
|---|---|---|---|
| main | list&lt;[ServerItem](#serveritem)&gt; |  | 36 slots: hotbar first |
| armor | list&lt;[ServerItem](#serveritem)&gt; |  | helmet, chestplate, leggings, boots |
| offhand | [ServerItem](#serveritem) |  |  |
| selected | `i32` | 0 |  |

### PlayerDetails

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `u64` | 0 |  |
| name | `string` |  |  |
| xuid | `string` |  |  |
| uuid | `string` |  |  |
| dimension | [Dimension](#dimension) | Overworld |  |
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | 0 |  |
| yaw | `f32` | 0 |  |
| pitch | `f32` | 0 |  |
| gameMode | [GameMode](#gamemode) | Survival |  |
| health | `f32` | 20 |  |
| maxHealth | `f32` | 20 |  |
| absorption | `f32` | 0 |  |
| food | `i32` | 20 |  |
| saturation | `f32` | 5 |  |
| xpLevel | `i32` | 0 |  |
| xpProgress | `f32` | 0 |  |
| onGround | `bool` | true |  |
| sneaking | `bool` | false |  |
| sprinting | `bool` | false |  |
| dead | `bool` | false |  |
| isOperator | `bool` | false |  |
| effects | list&lt;[Effect](#effect)&gt; |  |  |

### ServerEntity

| Field | Type | Default | Notes |
|---|---|---|---|
| id | `u64` | 0 |  |
| type | `string` |  |  |
| dimension | [Dimension](#dimension) | Overworld |  |
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | 0 |  |
| yaw | `f32` | 0 |  |
| health | `f32` | 0 |  |
| maxHealth | `f32` | 0 |  |
| baby | `bool` | false |  |

### FormButton

| Field | Type | Default | Notes |
|---|---|---|---|
| text | `string` |  |  |
| image | `string` |  | texture path or https url; empty for none |

### FormControl

| Field | Type | Default | Notes |
|---|---|---|---|
| kind | [FormElement](#formelement) | Label |  |
| text | `string` |  |  |
| options | list&lt;`string`&gt; |  | dropdown and step slider choices |
| min | `f32` | 0 |  |
| max | `f32` | 100 |  |
| step | `f32` | 1 |  |
| defaultNumber | `f32` | 0 |  |
| defaultText | `string` |  | input default, or the toggle default as true or false |
| placeholder | `string` |  |  |

### Form

| Field | Type | Default | Notes |
|---|---|---|---|
| kind | [FormKind](#formkind) | Message |  |
| title | `string` |  |  |
| body | `string` |  |  |
| buttons | list&lt;[FormButton](#formbutton)&gt; |  | menu buttons; a message form uses the first two |
| controls | list&lt;[FormControl](#formcontrol)&gt; |  | custom form controls |

### EventServerDamage

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` | 0 |  |
| amount | `f32` | 0 |  |
| cause | [DamageCause](#damagecause) | Generic |  |
| attacker | `u64` | 0 | player or entity id; 0 when there is none |
| attackerType | `string` |  |  |

### EventServerDeath

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` | 0 |  |
| cause | [DamageCause](#damagecause) | Generic |  |
| attacker | `u64` | 0 |  |
| message | `string` |  |  |

### EventPlayerUse

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` | 0 |  |
| x | `i32` | 0 |  |
| y | `i32` | 0 |  |
| z | `i32` | 0 |  |
| face | [Face](#face) | Up |  |
| block | `string` |  |  |
| item | `string` |  |  |

### EventPlayerAttack

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` | 0 |  |
| target | `u64` | 0 |  |
| targetType | `string` |  |  |
| damage | `f32` | 0 |  |

### EventEntityDamage

| Field | Type | Default | Notes |
|---|---|---|---|
| entity | `u64` | 0 |  |
| type | `string` |  |  |
| amount | `f32` | 0 |  |
| cause | [DamageCause](#damagecause) | Generic |  |
| attacker | `u64` | 0 |  |

### EventEntityDeath

| Field | Type | Default | Notes |
|---|---|---|---|
| entity | `u64` | 0 |  |
| type | `string` |  |  |
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | 0 |  |
| killer | `u64` | 0 | player id when a player got the kill |

### EventEntitySpawn

| Field | Type | Default | Notes |
|---|---|---|---|
| entity | `u64` | 0 |  |
| type | `string` |  |  |
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | 0 |  |
| natural | `bool` | false | spawned by the world rather than a command, egg or plugin |

### EventItem

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` | 0 |  |
| item | `string` |  |  |
| count | `u32` | 0 |  |

### EventGameModeChange

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` | 0 |  |
| from | [GameMode](#gamemode) |  |  |
| to | [GameMode](#gamemode) |  |  |

### EventFormResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` | 0 |  |
| form | `u32` | 0 |  |
| closed | `bool` | false | the player closed the form without answering |
| button | `i32` | -1 | menu: index of the pressed button; message: 0 or 1 |
| values | list&lt;`string`&gt; |  | custom form: one value per control, as text |

### EventPlayerMove

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` | 0 |  |
| dimension | [Dimension](#dimension) | Overworld |  |
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | 0 |  |
| yaw | `f32` | 0 |  |
| pitch | `f32` | 0 |  |
| onGround | `bool` | true |  |

### EventExplosion

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `f64` | 0 |  |
| y | `f64` | 0 |  |
| z | `f64` | 0 |  |
| power | `f32` | 0 |  |
| source | `u64` | 0 |  |
| sourceType | `string` |  |  |

### EventContainer

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` | 0 |  |
| x | `i32` | 0 |  |
| y | `i32` | 0 |  |
| z | `i32` | 0 |  |
| block | `string` |  |  |

### EventPlayerDimension

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` | 0 |  |
| from | [Dimension](#dimension) |  |  |
| to | [Dimension](#dimension) |  |  |

### EventServerCommand

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` | 0 | 0 for the console |
| line | `string` |  |  |

### EventPreLogin

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |
| xuid | `string` |  |  |
| address | `string` |  |  |

### PlayerInfoRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |

### PlayerSetGameModeRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |
| mode | [GameMode](#gamemode) |  |  |

### PlayerSetHealthRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |
| health | `f32` |  |  |

### PlayerDamageRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |
| amount | `f32` |  |  |
| cause | [DamageCause](#damagecause) |  |  |
| knockback | `bool` |  |  |

### PlayerKillRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |

### PlayerSetFoodRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |
| food | `i32` |  |  |
| saturation | `f32` |  |  |

### PlayerAddExperienceRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |
| points | `i32` |  |  |
| levels | `i32` |  |  |

### PlayerEffectAddRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |
| effect | [EffectId](#effectid) |  |  |
| amplifier | `i32` |  |  |
| ticks | `i32` |  |  |
| particles | `bool` |  |  |

### PlayerEffectRemoveRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |
| effect | `i32` |  | 0 removes every effect |

### PlayerInventoryRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |

### PlayerSetSlotRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |
| container | `i32` |  | 0 main, 1 armor, 2 offhand |
| slot | `i32` |  |  |
| item | [ServerItem](#serveritem) |  |  |

### PlayerClearInventoryRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |

### PlayerSetVelocityRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |
| x | `f64` |  |  |
| y | `f64` |  |  |
| z | `f64` |  |  |

### PlayerTransferDimensionRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |
| dimension | [Dimension](#dimension) |  |  |
| x | `f64` |  |  |
| y | `f64` |  |  |
| z | `f64` |  |  |

### PlayerTitleRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |
| title | `string` |  |  |
| subtitle | `string` |  |  |
| fadeIn | `i32` |  |  |
| stay | `i32` |  |  |
| fadeOut | `i32` |  |  |

### PlayerActionBarRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |
| text | `string` |  |  |

### PlayerSoundRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  | 0 plays to everyone nearby |
| sound | `string` |  |  |
| x | `f64` |  |  |
| y | `f64` |  |  |
| z | `f64` |  |  |
| volume | `f32` |  |  |
| pitch | `f32` |  |  |

### PlayerTransferRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |
| host | `string` |  |  |
| port | `u16` |  |  |

### PlayerSetOperatorRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |
| isOperator | `bool` |  |  |

### PlayerFormRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |
| form | [Form](#form) |  |  |

### PlayerRunCommandRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |
| line | `string` |  |  |

### PlayerSetSpawnRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| player | `u64` |  |  |
| x | `i32` |  |  |
| y | `i32` |  |  |
| z | `i32` |  |  |

### PlayerByNameRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |

### BlockStateRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| dimension | [Dimension](#dimension) |  |  |
| x | `i32` |  |  |
| y | `i32` |  |  |
| z | `i32` |  |  |

### BlockStateSetRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| dimension | [Dimension](#dimension) |  |  |
| x | `i32` |  |  |
| y | `i32` |  |  |
| z | `i32` |  |  |
| name | `string` |  |  |
| states | list&lt;pair&lt;`string`, `string`&gt;&gt; |  |  |

### FillRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| dimension | [Dimension](#dimension) |  |  |
| x0 | `i32` |  |  |
| y0 | `i32` |  |  |
| z0 | `i32` |  |  |
| x1 | `i32` |  |  |
| y1 | `i32` |  |  |
| z1 | `i32` |  |  |
| block | `string` |  |  |

### TimeGetResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| time | `i64` |  |  |
| day | `i64` |  |  |
| timeOfDay | `i32` |  |  |

### TimeSetRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| time | `i64` |  |  |

### WeatherSetRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| weather | [Weather](#weather) |  |  |
| ticks | `i32` |  |  |

### GameRuleGetRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |

### GameRuleSetRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| name | `string` |  |  |
| value | `i32` |  |  |

### DifficultySetRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| difficulty | [Difficulty](#difficulty) |  |  |

### ExplodeRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| dimension | [Dimension](#dimension) |  |  |
| x | `f64` |  |  |
| y | `f64` |  |  |
| z | `f64` |  |  |
| power | `f32` |  |  |
| breakBlocks | `bool` |  |  |

### HighestBlockRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| dimension | [Dimension](#dimension) |  |  |
| x | `i32` |  |  |
| z | `i32` |  |  |

### ParticleAtRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| dimension | [Dimension](#dimension) |  |  |
| name | `string` |  |  |
| x | `f64` |  |  |
| y | `f64` |  |  |
| z | `f64` |  |  |

### DropItemRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| dimension | [Dimension](#dimension) |  |  |
| item | [ServerItem](#serveritem) |  |  |
| x | `f64` |  |  |
| y | `f64` |  |  |
| z | `f64` |  |  |

### EntitySpawnRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| type | `string` |  |  |
| x | `f64` |  |  |
| y | `f64` |  |  |
| z | `f64` |  |  |
| baby | `bool` |  |  |

### EntityListRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| x | `f64` |  |  |
| y | `f64` |  |  |
| z | `f64` |  |  |
| radius | `f64` |  | 0 lists every entity |
| type | `string` |  |  |

### EntityListResponse

| Field | Type | Default | Notes |
|---|---|---|---|
| entities | list&lt;[ServerEntity](#serverentity)&gt; |  |  |

### EntityRemoveRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| entity | `u64` |  |  |

### EntityDamageRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| entity | `u64` |  |  |
| amount | `f32` |  |  |

### EntityTeleportRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| entity | `u64` |  |  |
| x | `f64` |  |  |
| y | `f64` |  |  |
| z | `f64` |  |  |

### EntitySetVelocityRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| entity | `u64` |  |  |
| x | `f64` |  |  |
| y | `f64` |  |  |
| z | `f64` |  |  |

### EntityInfoRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| entity | `u64` |  |  |

### EntitySetHealthRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| entity | `u64` |  |  |
| health | `f32` |  |  |

### RunCommandRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| line | `string` |  |  |

### MotdSetRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| motd | `string` |  |  |

### LootTableOverrideRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| key | `string` |  | blocks/stone, entities/zombie |
| json | `string` |  | a Java loot table; empty restores the built-in one |

### LootTableGetRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| key | `string` |  |  |

### EventAmendRequest

| Field | Type | Default | Notes |
|---|---|---|---|
| field | `string` |  |  |
| number | `f64` |  |  |
| text | `string` |  |  |

