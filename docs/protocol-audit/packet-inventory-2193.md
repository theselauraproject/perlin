# Packet source inventory

This is an exhaustive inventory of the selected schema, not a certificate of complete behavior.

`no receive handler` includes packets sent only by the client; it is not automatically a defect.
`typed` or `partial` means source code is registered. `no-op` means the registered callback does no work.
Every packet's behavior remains unproven as a whole; confirmed fixes are documented separately.

Schema: `protocol/legacy/bedrock-1.26.51.json`. Base revision: `0329b0cc4b1c21482b46cf275bd8a5eafc8aaeab`.

The reference dispatch column is a matched-build virtual entrypoint, not proof of full behavior.

| ID | Perlin name | Executable name | Active in executable | Codec | Receive handler | Reference client dispatch | Source |
| ---: | --- | --- | --- | --- | --- | --- | --- |
| 1 | `login` | LoginPacket | yes | generated | no receive handler | `0x49bac0` | — |
| 2 | `play_status` | PlayStatusPacket | yes | generated | typed | `0x1305650` | `src/client/systems/multiplayer/handlers/SessionHandlers.cpp:55` |
| 3 | `server_to_client_handshake` | ServerToClientHandshakePacket | yes | generated | typed | `0x12fed90` | `src/client/systems/multiplayer/handlers/SessionHandlers.cpp:106` |
| 4 | `client_to_server_handshake` | ClientToServerHandshakePacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 5 | `disconnect` | DisconnectPacket | yes | generated | typed | `0x132f830` | `src/client/systems/multiplayer/handlers/SessionHandlers.cpp:93` |
| 6 | `resource_packs_info` | ResourcePacksInfoPacket | yes | generated | typed | `0x1300f10` | `src/client/systems/multiplayer/handlers/SessionHandlers.cpp:127` |
| 7 | `resource_pack_stack` | ResourcePackStackPacket | yes | generated | typed | `0x1301580` | `src/client/systems/multiplayer/handlers/SessionHandlers.cpp:214` |
| 8 | `resource_pack_client_response` | ResourcePackClientResponsePacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 9 | `text` | TextPacket | yes | generated | partial | `0x1306a90` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:1260`; `src/client/systems/multiplayer/handlers/SessionHandlers.cpp:146` |
| 10 | `set_time` | SetTimePacket | yes | generated | typed | `0x13398b0` | `src/client/systems/multiplayer/handlers/WorldStateHandlers.cpp:86` |
| 11 | `start_game` | StartGamePacket | yes | generated | partial | `0x13323c0` | `src/client/systems/multiplayer/handlers/SessionHandlers.cpp:257` |
| 12 | `add_player` | AddPlayerPacket | yes | generated | typed | `0x133b880` | `src/client/systems/multiplayer/handlers/EntityLifecycleHandlers.cpp:8` |
| 13 | `add_entity` | AddActorPacket | yes | generated | typed | `0x133a660` | `src/client/systems/multiplayer/handlers/EntityLifecycleHandlers.cpp:44` |
| 14 | `remove_entity` | RemoveActorPacket | yes | generated | typed | `0x133d700` | `src/client/systems/multiplayer/handlers/EntityLifecycleHandlers.cpp:115` |
| 15 | `add_item_entity` | AddItemActorPacket | yes | generated | typed | `0x133de60` | `src/client/systems/multiplayer/handlers/EntityLifecycleHandlers.cpp:65` |
| 16 | `server_post_move` | ServerPlayerPostMovePositionPacket | yes | generated | no-op | `0x13216a0` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 17 | `take_item_entity` | TakeItemActorPacket | yes | generated | typed | `0x130e970` | `src/client/systems/multiplayer/handlers/EntityLifecycleHandlers.cpp:81` |
| 18 | `move_entity` | MoveActorAbsolutePacket | yes | generated | typed | `0x133ee10` | `src/client/systems/multiplayer/handlers/EntityLifecycleHandlers.cpp:117` |
| 19 | `move_player` | MovePlayerPacket | yes | generated | typed | `0x133e990` | `src/client/systems/multiplayer/handlers/EntityLifecycleHandlers.cpp:155`; `src/client/systems/multiplayer/handlers/LocalPlayerHandlers.cpp:8` |
| 20 | `rider_jump` | — | no | generated | no receive handler | — | — |
| 21 | `update_block` | UpdateBlockPacket | yes | generated | typed | `0x133f770` | `src/client/systems/multiplayer/handlers/LevelHandlers.cpp:206` |
| 22 | `add_painting` | AddPaintingPacket | yes | generated | typed | `0x1339e20` | `src/client/systems/multiplayer/handlers/EntityLifecycleHandlers.cpp:36` |
| 23 | `tick_sync` | — | no | generated | typed | — | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:270` |
| 24 | `level_sound_event_old` | — | no | generated | typed | — | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1114` |
| 25 | `level_event` | LevelEventPacket | yes | generated | typed | `0x1309790` | `src/client/systems/multiplayer/handlers/LevelHandlers.cpp:8` |
| 26 | `block_event` | BlockEventPacket | yes | generated | typed | `0x1340450` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1169` |
| 27 | `entity_event` | ActorEventPacket | yes | generated | typed | `0x1308500` | `src/client/systems/multiplayer/handlers/EntityStateHandlers.cpp:100`; `src/client/systems/multiplayer/handlers/EntityStateHandlers.cpp:150`; `src/client/systems/multiplayer/handlers/EntityStateHandlers.cpp:163`; `src/client/systems/multiplayer/handlers/EntityStateHandlers.cpp:237` |
| 28 | `mob_effect` | MobEffectPacket | yes | generated | typed | `0x1340700` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:1117` |
| 29 | `update_attributes` | UpdateAttributesPacket | yes | generated | typed | `0x1311d80` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:1082`; `src/client/systems/multiplayer/handlers/EntityStateHandlers.cpp:113` |
| 30 | `inventory_transaction` | InventoryTransactionPacket | yes | generated | typed | `0x1345120` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:627` |
| 31 | `mob_equipment` | MobEquipmentPacket | yes | generated | typed | `0x1340d60` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:1045`; `src/client/systems/multiplayer/handlers/EntityStateHandlers.cpp:53` |
| 32 | `mob_armor_equipment` | MobArmorEquipmentPacket | yes | generated | typed | `0x1341340` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:1057`; `src/client/systems/multiplayer/handlers/EntityStateHandlers.cpp:66` |
| 33 | `interact` | InteractPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 34 | `block_pick_request` | BlockPickRequestPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 35 | `entity_pick_request` | ActorPickRequestPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 36 | `player_action` | PlayerActionPacket | yes | generated | no receive handler | `0x1329a70` | — |
| 38 | `hurt_armor` | HurtArmorPacket | yes | generated | no-op | `0x13421a0` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 39 | `set_entity_data` | SetActorDataPacket | yes | generated | typed | `0x1341480` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:1157`; `src/client/systems/multiplayer/handlers/EntityLifecycleHandlers.cpp:171` |
| 40 | `set_entity_motion` | SetActorMotionPacket | yes | generated | typed | `0x1341750` | `src/client/systems/multiplayer/handlers/EntityLifecycleHandlers.cpp:77`; `src/client/systems/multiplayer/handlers/LocalPlayerHandlers.cpp:144` |
| 41 | `set_entity_link` | SetActorLinkPacket | yes | generated | typed | `0x13456e0` | `src/client/systems/multiplayer/handlers/EntityStateHandlers.cpp:258` |
| 42 | `set_health` | SetHealthPacket | yes | generated | typed | `0x1341f50` | `src/client/systems/multiplayer/BedrockServerUi.cpp:331` |
| 43 | `set_spawn_position` | SetSpawnPositionPacket | yes | generated | typed | `0x1341fd0` | `src/client/systems/multiplayer/handlers/LevelHandlers.cpp:192` |
| 44 | `animate` | AnimatePacket | yes | generated | typed | `0x1341d60` | `src/client/systems/multiplayer/handlers/EntityStateHandlers.cpp:81` |
| 45 | `respawn` | RespawnPacket | yes | generated | typed | `0x1308ef0` | `src/client/systems/multiplayer/handlers/LocalPlayerHandlers.cpp:58` |
| 46 | `container_open` | ContainerOpenPacket | yes | generated | typed | `0x1342340` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:892` |
| 47 | `container_close` | ContainerClosePacket | yes | generated | typed | `0x1342690` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:1008` |
| 48 | `player_hotbar` | PlayerHotbarPacket | yes | generated | typed | `0x13452e0` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:1038` |
| 49 | `inventory_content` | InventoryContentPacket | yes | generated | typed | `0x13441c0` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:619` |
| 50 | `inventory_slot` | InventorySlotPacket | yes | generated | typed | `0x1342cd0` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:622` |
| 51 | `container_set_data` | ContainerSetDataPacket | yes | generated | typed | `0x1345370` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:277` |
| 52 | `crafting_data` | CraftingDataPacket | yes | generated | typed | `0x1312120` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:809` |
| 53 | `crafting_event` | — | no | generated | no receive handler | — | — |
| 54 | `gui_data_pick_item` | GuiDataPickItemPacket | yes | generated | typed | `0x1308820` | `src/client/systems/multiplayer/BedrockServerUi.cpp:166` |
| 55 | `adventure_settings` | — | no | generated | no receive handler | — | — |
| 56 | `block_entity_data` | BlockActorDataPacket | yes | generated | typed | `0x1340560` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:229` |
| 57 | `player_input` | — | no | generated | no receive handler | — | — |
| 58 | `level_chunk` | LevelChunkPacket | yes | generated | typed | `0x13466f0` | `src/client/systems/multiplayer/handlers/HandlerRegistration.cpp:32` |
| 59 | `set_commands_enabled` | SetCommandsEnabledPacket | yes | generated | typed | `0x1339c20` | `src/client/systems/multiplayer/handlers/LevelHandlers.cpp:166` |
| 60 | `set_difficulty` | SetDifficultyPacket | yes | generated | typed | `0x1339d20` | `src/client/systems/multiplayer/handlers/LevelHandlers.cpp:170` |
| 61 | `change_dimension` | ChangeDimensionPacket | yes | generated | typed | `0x1348980` | `src/client/systems/multiplayer/handlers/WorldStateHandlers.cpp:110` |
| 62 | `set_player_game_type` | SetPlayerGameTypePacket | yes | generated | typed | `0x130e3e0` | `src/client/systems/multiplayer/handlers/PlayerRuleHandlers.cpp:78` |
| 63 | `player_list` | PlayerListPacket | yes | generated | typed | `0x1348a10` | `src/client/systems/multiplayer/handlers/EntityStateHandlers.cpp:8` |
| 64 | `simple_event` | SimpleEventPacket | yes | generated | no receive handler | `0x1308e20` | — |
| 65 | `event` | LegacyTelemetryEventPacket | yes | generated | no receive handler | `0x130f7c0` | — |
| 66 | `spawn_experience_orb` | SpawnExperienceOrbPacket | yes | generated | no-op | `0x76bc0` (return-only) | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 67 | `clientbound_map_item_data` | ClientboundMapItemDataPacket | yes | generated | typed | `0x1310e80` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:282` |
| 68 | `map_info_request` | MapInfoRequestPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 69 | `request_chunk_radius` | RequestChunkRadiusPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 70 | `chunk_radius_update` | ChunkRadiusUpdatedPacket | yes | generated | typed | `0x1319040` | `src/client/systems/multiplayer/handlers/WorldStateHandlers.cpp:31` |
| 72 | `game_rules_changed` | GameRulesChangedPacket | yes | generated | typed | `0x1340a10` | `src/client/systems/multiplayer/handlers/LevelHandlers.cpp:174` |
| 73 | `camera` | CameraPacket | yes | generated | no receive handler | `0x131b590` | — |
| 74 | `boss_event` | BossEventPacket | yes | generated | typed | `0x131dd80` | `src/client/systems/multiplayer/BedrockServerUi.cpp:213` |
| 75 | `show_credits` | ShowCreditsPacket | yes | generated | typed | `0x131e480` | `src/client/systems/multiplayer/handlers/PlayerRuleHandlers.cpp:73` |
| 76 | `available_commands` | AvailableCommandsPacket | yes | generated | typed | `0x1306410` | `src/client/systems/multiplayer/handlers/WorldStateHandlers.cpp:171` |
| 77 | `command_request` | CommandRequestPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 78 | `command_block_update` | CommandBlockUpdatePacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 79 | `command_output` | CommandOutputPacket | yes | generated | typed | `0x13067c0` | `src/client/systems/multiplayer/handlers/PlayerRuleHandlers.cpp:93`; `src/client/systems/multiplayer/handlers/SessionHandlers.cpp:157` |
| 80 | `update_trade` | UpdateTradePacket | yes | generated | typed | `0x1321a10` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:318` |
| 81 | `update_equipment` | UpdateEquipPacket | yes | generated | no-op | `0x1321da0` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 82 | `resource_pack_data_info` | ResourcePackDataInfoPacket | yes | generated | typed | `0x131c8a0` | `src/client/systems/multiplayer/handlers/SessionHandlers.cpp:165` |
| 83 | `resource_pack_chunk_data` | ResourcePackChunkDataPacket | yes | generated | typed | `0x131dbe0` | `src/client/systems/multiplayer/handlers/SessionHandlers.cpp:187` |
| 84 | `resource_pack_chunk_request` | ResourcePackChunkRequestPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 85 | `transfer` | TransferPacket | yes | generated | typed | `0x131e730` | `src/client/systems/multiplayer/handlers/SessionHandlers.cpp:207` |
| 86 | `play_sound` | PlaySoundPacket | yes | generated | typed | `0x131f2a0` | `src/client/systems/multiplayer/handlers/WorldStateHandlers.cpp:132` |
| 87 | `stop_sound` | StopSoundPacket | yes | generated | typed | `0x131f5e0` | `src/client/systems/multiplayer/handlers/WorldStateHandlers.cpp:149` |
| 88 | `set_title` | SetTitlePacket | yes | generated | typed | `0x1322160` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:1216` |
| 89 | `add_behavior_tree` | AddBehaviorTreePacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 90 | `structure_block_update` | StructureBlockUpdatePacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 91 | `show_store_offer` | ShowStoreOfferPacket | yes | generated | no receive handler | `0x1320da0` | — |
| 92 | `purchase_receipt` | PurchaseReceiptPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 93 | `player_skin` | PlayerSkinPacket | yes | generated | typed | `0x134a4c0` | `src/client/systems/multiplayer/handlers/EntityStateHandlers.cpp:43` |
| 94 | `sub_client_login` | SubClientLoginPacket | yes | generated | no receive handler | `0x49bac0` | — |
| 95 | `initiate_web_socket_connection` | AutomationClientConnectPacket | yes | generated | no receive handler | `0x1322ae0` | — |
| 96 | `set_last_hurt_by` | SetLastHurtByPacket | yes | generated | no-op | `0x1322a20` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 97 | `book_edit` | BookEditPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 98 | `npc_request` | NpcRequestPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 99 | `photo_transfer` | PhotoTransferPacket | yes | generated | no receive handler | `0x1323870` | — |
| 100 | `modal_form_request` | ModalFormRequestPacket | yes | generated | typed | `0x1320ee0` | `src/client/systems/multiplayer/BedrockServerUi.cpp:193` |
| 101 | `modal_form_response` | ModalFormResponsePacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 102 | `server_settings_request` | ServerSettingsRequestPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 103 | `server_settings_response` | ServerSettingsResponsePacket | yes | generated | typed | `0x1321730` | `src/client/systems/multiplayer/BedrockServerUi.cpp:208` |
| 104 | `show_profile` | ShowProfilePacket | yes | generated | no receive handler | `0x1321940` | — |
| 105 | `set_default_game_type` | SetDefaultGameTypePacket | yes | generated | typed | `0x13454d0` | `src/client/systems/multiplayer/handlers/PlayerRuleHandlers.cpp:58` |
| 106 | `remove_objective` | RemoveObjectivePacket | yes | generated | typed | `0x1323e80` | `src/client/systems/multiplayer/BedrockServerUi.cpp:271` |
| 107 | `set_display_objective` | SetDisplayObjectivePacket | yes | generated | typed | `0x1324010` | `src/client/systems/multiplayer/BedrockServerUi.cpp:261` |
| 108 | `set_score` | SetScorePacket | yes | generated | typed | `0x13242b0` | `src/client/systems/multiplayer/BedrockServerUi.cpp:290` |
| 109 | `lab_table` | LabTablePacket | yes | generated | no receive handler | `0x1323d10` | — |
| 110 | `update_block_synced` | UpdateBlockSyncedPacket | yes | generated | typed | `0x133f5f0` | `src/client/systems/multiplayer/handlers/LevelHandlers.cpp:301` |
| 111 | `move_entity_delta` | MoveActorDeltaPacket | yes | generated | typed | `0x133ef30` | `src/client/systems/multiplayer/handlers/EntityLifecycleHandlers.cpp:130` |
| 112 | `set_scoreboard_identity` | SetScoreboardIdentityPacket | yes | generated | typed | `0x1324810` | `src/client/systems/multiplayer/BedrockServerUi.cpp:280` |
| 113 | `set_local_player_as_initialized` | SetLocalPlayerAsInitializedPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 114 | `update_soft_enum` | UpdateSoftEnumPacket | yes | generated | typed | `0x1324ad0` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1147` |
| 115 | `network_stack_latency` | NetworkStackLatencyPacket | yes | generated | typed | `0x134adb0` | `src/client/systems/multiplayer/handlers/PlayerRuleHandlers.cpp:88` |
| 117 | `script_custom_event` | — | no | generated | no receive handler | — | — |
| 118 | `spawn_particle_effect` | SpawnParticleEffectPacket | yes | generated | typed | `0x133fb50` | `src/client/systems/multiplayer/handlers/EntityStateHandlers.cpp:226` |
| 119 | `available_entity_identifiers` | AvailableActorIdentifiersPacket | yes | generated | no receive handler | `0x1324cc0` | — |
| 120 | `level_sound_event_v2` | — | no | generated | typed | — | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1130` |
| 121 | `network_chunk_publisher_update` | NetworkChunkPublisherUpdatePacket | yes | generated | typed | `0x1324dc0` | `src/client/systems/multiplayer/handlers/WorldStateHandlers.cpp:8` |
| 122 | `biome_definition_list` | BiomeDefinitionListPacket | yes | generated | no receive handler | `0x1325090` | — |
| 123 | `level_sound_event` | LevelSoundEventPacket | yes | generated | typed | `0x1309070` | `src/client/systems/multiplayer/handlers/WorldStateHandlers.cpp:157` |
| 124 | `level_event_generic` | LevelEventGenericPacket | yes | generated | no-op | `0x130a140` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 125 | `lectern_update` | LecternUpdatePacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 126 | `video_stream_connect` | — | no | generated | no receive handler | — | — |
| 129 | `client_cache_status` | ClientCacheStatusPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 130 | `on_screen_texture_animation` | OnScreenTextureAnimationPacket | yes | generated | typed | `0x1321260` | `src/client/systems/multiplayer/handlers/PlayerRuleHandlers.cpp:68` |
| 131 | `map_create_locked_copy` | MapCreateLockedCopyPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 132 | `structure_template_data_export_request` | StructureTemplateDataRequestPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 133 | `structure_template_data_export_response` | StructureTemplateDataResponsePacket | yes | generated | no receive handler | `0x1321b20` | — |
| 134 | `update_block_properties` | — | no | generated | no-op | — | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 135 | `client_cache_blob_status` | ClientCacheBlobStatusPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 136 | `client_cache_miss_response` | ClientCacheMissResponsePacket | yes | generated | typed | `0x1348160` | `src/client/systems/multiplayer/handlers/HandlerRegistration.cpp:34` |
| 137 | `education_settings` | EducationSettingsPacket | yes | generated | no receive handler | `0x1327060` | — |
| 138 | `emote` | EmotePacket | yes | generated | no-op | `0x130ac60` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 139 | `multiplayer_settings` | MultiplayerSettingsPacket | yes | generated | no receive handler | `0x1327450` | — |
| 140 | `settings_command` | SettingsCommandPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 141 | `anvil_damage` | AnvilDamagePacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 142 | `completed_using_item` | CompletedUsingItemPacket | yes | generated | no receive handler | `0x1306470` | — |
| 143 | `network_settings` | NetworkSettingsPacket | yes | generated | typed | `0x1330030` | `src/client/systems/multiplayer/handlers/SessionHandlers.cpp:41` |
| 144 | `player_auth_input` | PlayerAuthInputPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 145 | `creative_content` | CreativeContentPacket | yes | generated | typed | `0x1343ab0` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:768` |
| 146 | `player_enchant_options` | PlayerEnchantOptionsPacket | yes | generated | no receive handler | `0x1327c60` | — |
| 147 | `item_stack_request` | ItemStackRequestPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 148 | `item_stack_response` | ItemStackResponsePacket | yes | generated | typed | `0x1308d20` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:671` |
| 149 | `player_armor_damage` | PlayerArmorDamagePacket | yes | generated | typed | `0x130af90` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:1068` |
| 150 | `code_builder` | CodeBuilderPacket | yes | generated | no receive handler | `0x1319db0` | — |
| 151 | `update_player_game_type` | UpdatePlayerGameTypePacket | yes | generated | typed | `0x130e4e0` | `src/client/systems/multiplayer/handlers/PlayerRuleHandlers.cpp:47` |
| 152 | `emote_list` | EmoteListPacket | yes | generated | no receive handler | `0x130a500` | — |
| 153 | `position_tracking_db_broadcast` | PositionTrackingDBServerBroadcastPacket | yes | generated | no receive handler | `0x1306330` | — |
| 154 | `position_tracking_db_request` | PositionTrackingDBClientRequestPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 155 | `debug_info` | DebugInfoPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 156 | `packet_violation_warning` | PacketViolationWarningPacket | yes | generated | typed | `0x76bc0` (return-only) | `src/client/systems/multiplayer/handlers/SessionHandlers.cpp:238` |
| 157 | `motion_prediction_hints` | MotionPredictionHintsPacket | yes | generated | typed | `0x1341b60` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1173` |
| 158 | `animate_entity` | AnimateEntityPacket | yes | generated | no-op | `0x1327de0` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 159 | `camera_shake` | CameraShakePacket | yes | generated | typed | `0x131c3a0` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:329` |
| 160 | `player_fog` | PlayerFogPacket | yes | generated | typed | `0x1328820` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:336` |
| 161 | `correct_player_move_prediction` | CorrectPlayerMovePredictionPacket | yes | generated | typed | `0x131ab90` | `src/client/systems/multiplayer/handlers/LocalPlayerHandlers.cpp:91` |
| 162 | `item_registry` | ItemRegistryPacket | yes | generated | typed | `0x1344000` | `src/client/systems/multiplayer/handlers/WorldStateHandlers.cpp:41` |
| 163 | `filter_text_packet` | — | no | generated | no receive handler | — | — |
| 164 | `primitive_shapes` | ClientboundDebugRendererPacket | yes | generated | no-op | `0x76bc0` (return-only) | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 165 | `sync_entity_property` | SyncActorPropertyPacket | yes | generated | typed | `0x1328740` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:212` |
| 166 | `add_volume_entity` | AddVolumeEntityPacket | yes | generated | no receive handler | `0x130b1b0` | — |
| 167 | `remove_volume_entity` | RemoveVolumeEntityPacket | yes | generated | no receive handler | `0x130b530` | — |
| 168 | `simulation_type` | SimulationTypePacket | yes | generated | no-op | `0x130b740` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 169 | `npc_dialogue` | NpcDialoguePacket | yes | generated | typed | `0x1319840` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:306` |
| 170 | `edu_uri_resource_packet` | EduUriResourcePacket | yes | generated | no receive handler | `0x1327330` | — |
| 171 | `create_photo` | CreatePhotoPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 172 | `update_subchunk_blocks` | UpdateSubChunkBlocksPacket | yes | generated | typed | `0x1328b70` | `src/client/systems/multiplayer/handlers/LevelHandlers.cpp:261` |
| 173 | `photo_info_request` | — | no | generated | no receive handler | — | — |
| 174 | `subchunk` | SubChunkPacket | yes | generated | typed | `0x131f8c0` | `src/client/systems/multiplayer/handlers/HandlerRegistration.cpp:33` |
| 175 | `subchunk_request` | SubChunkRequestPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 176 | `client_start_item_cooldown` | PlayerStartItemCooldownPacket | yes | generated | typed | `0x130b0c0` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:1031` |
| 177 | `script_message` | ScriptMessagePacket | yes | generated | typed | `0x1321310` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1184` |
| 178 | `code_builder_source` | CodeBuilderSourcePacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 179 | `ticking_areas_load_status` | TickingAreasLoadStatusPacket | yes | generated | no receive handler | `0x12fecc0` | — |
| 180 | `dimension_data` | DimensionDataPacket | yes | generated | typed | `0x134ae70` | `src/client/systems/multiplayer/handlers/WorldStateHandlers.cpp:23` |
| 181 | `agent_action` | AgentActionEventPacket | yes | generated | no receive handler | `0x1328e50` | — |
| 182 | `change_mob_property` | ChangeMobPropertyPacket | yes | generated | typed | `0x76bc0` (return-only) | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:215` |
| 183 | `lesson_progress` | LessonProgressPacket | yes | generated | no receive handler | `0x1310870` | — |
| 184 | `request_ability` | RequestAbilityPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 185 | `request_permissions` | RequestPermissionsPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 186 | `toast_request` | ToastRequestPacket | yes | generated | typed | `0x1321190` | `src/client/systems/multiplayer/BedrockServerUi.cpp:321` |
| 187 | `update_abilities` | UpdateAbilitiesPacket | yes | generated | typed | `0x134b6b0` | `src/client/systems/multiplayer/handlers/PlayerRuleHandlers.cpp:8` |
| 188 | `update_adventure_settings` | UpdateAdventureSettingsPacket | yes | generated | typed | `0x134b950` | `src/client/systems/multiplayer/handlers/PlayerRuleHandlers.cpp:63` |
| 189 | `death_info` | DeathInfoPacket | yes | generated | typed | `0x1329140` | `src/client/systems/multiplayer/handlers/LocalPlayerHandlers.cpp:50` |
| 190 | `editor_network` | EditorNetworkPacket | yes | generated | no receive handler | `0x1329340` | — |
| 191 | `feature_registry` | FeatureRegistryPacket | yes | generated | no receive handler | `0x1310d60` | — |
| 192 | `server_stats` | ServerStatsPacket | yes | generated | no receive handler | `0x134ae20` | — |
| 193 | `request_network_settings` | RequestNetworkSettingsPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 194 | `game_test_request` | GameTestRequestPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 195 | `game_test_results` | GameTestResultsPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 196 | `update_client_input_locks` | UpdateClientInputLocksPacket | yes | generated | typed | `0x130cd00` | `src/client/systems/multiplayer/BedrockServerUi.cpp:328` |
| 197 | `client_cheat_ability` | — | no | generated | no receive handler | — | — |
| 198 | `camera_presets` | CameraPresetsPacket | yes | generated | typed | `0x131bc60` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:338` |
| 199 | `unlocked_recipes` | UnlockedRecipesPacket | yes | generated | typed | `0x130b8c0` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:881` |
| 300 | `camera_instruction` | CameraInstructionPacket | yes | generated | typed | `0x131b260` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:386` |
| 301 | `compressed_biome_definitions` | — | no | generated | no receive handler | — | — |
| 302 | `trim_data` | TrimDataPacket | yes | generated | no receive handler | `0x1329b50` | — |
| 303 | `open_sign` | OpenSignPacket | yes | generated | typed | `0x1329e10` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:235` |
| 304 | `agent_animation` | AgentAnimationPacket | yes | generated | no receive handler | `0x1329770` | — |
| 305 | `refresh_entitlements` | RefreshEntitlementsPacket | yes | generated | no receive handler | `0x130ce00` | — |
| 306 | `toggle_crafter_slot_request` | PlayerToggleCrafterSlotRequestPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 307 | `set_player_inventory_options` | SetPlayerInventoryOptionsPacket | yes | generated | typed | `0x130cc60` | `src/client/systems/multiplayer/BedrockServerUi.cpp:172` |
| 308 | `set_hud` | SetHudPacket | yes | generated | typed | `0x1339a60` | `src/client/systems/multiplayer/BedrockServerUi.cpp:336` |
| 309 | `award_achievement` | AwardAchievementPacket | yes | generated | no receive handler | `0x132a0e0` | — |
| 310 | `clientbound_close_form` | ClientboundCloseFormPacket | yes | generated | typed | `0x132a130` | `src/client/systems/multiplayer/BedrockServerUi.cpp:200` |
| 312 | `serverbound_loading_screen` | ServerboundLoadingScreenPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 313 | `jigsaw_structure_data` | JigsawStructureDataPacket | yes | generated | no receive handler | `0x1326fc0` | — |
| 314 | `current_structure_feature` | CurrentStructureFeaturePacket | yes | generated | no receive handler | `0x132a2c0` | — |
| 315 | `serverbound_diagnostics` | ServerboundDiagnosticsPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 316 | `camera_aim_assist` | CameraAimAssistPacket | yes | generated | no-op | `0x131ac70` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 317 | `container_registry_cleanup` | ContainerRegistryCleanupPacket | yes | generated | no receive handler | `0x1342b10` | — |
| 318 | `movement_effect` | MovementEffectPacket | yes | generated | typed | `0x133f140` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:1106` |
| 319 | `set_movement_authority` | — | no | generated | no-op | — | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 320 | `camera_aim_assist_presets` | CameraAimAssistPresetsPacket | yes | generated | no-op | `0x131bf50` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 321 | `client_camera_aim_assist` | ClientCameraAimAssistPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 322 | `client_movement_prediction_sync` | ClientMovementPredictionSyncPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 323 | `update_client_options` | UpdateClientOptionsPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 324 | `player_video_capture` | PlayerVideoCapturePacket | yes | generated | no receive handler | `0x130cfc0` | — |
| 325 | `player_update_entity_overrides` | PlayerUpdateEntityOverridesPacket | yes | generated | no receive handler | `0x134ba60` | — |
| 326 | `player_location` | PlayerLocationPacket | yes | generated | typed | `0x132a340` | `src/client/systems/multiplayer/BedrockServerUi.cpp:146` |
| 327 | `clientbound_controls_scheme` | ClientboundControlSchemeSetPacket | yes | generated | no-op | `0x1319120` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 328 | `server_script_debug_drawer` | PrimitiveShapesPacket | yes | generated | no-op | `0x130d5d0` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 329 | `serverbound_pack_setting_change` | ServerboundPackSettingChangePacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 330 | `clientbound_data_store` | ClientboundDataStorePacket | yes | generated | no-op | `0x130d830` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 331 | `graphics_override_parameter` | GraphicsOverrideParameterPacket | yes | generated | no-op | `0x130d870` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 332 | `serverbound_data_store` | ServerboundDataStorePacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 333 | `clientbound_data_driven_ui_show_screen` | ClientboundDataDrivenUIShowScreenPacket | yes | generated | no-op | `0x130d8b0` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 334 | `clientbound_data_driven_ui_close_screen` | ClientboundDataDrivenUICloseScreenPacket | yes | generated | no-op | `0x130de90` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 335 | `clientbound_data_driven_ui_reload` | ClientboundDataDrivenUIReloadPacket | yes | generated | no-op | `0x130df60` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 336 | `clientbound_texture_shift` | ClientboundTextureShiftPacket | yes | generated | no-op | `0x132a670` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 337 | `voxel_shapes` | VoxelShapesPacket | yes | generated | typed | `0x130e220` | `src/client/systems/multiplayer/handlers/EntityLifecycleHandlers.cpp:60` |
| 338 | `camera_spline` | CameraSplinePacket | yes | generated | typed | `0x131c750` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:378`; `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 339 | `camera_aim_assist_actor_priority` | CameraAimAssistActorPriorityPacket | yes | generated | no-op | `0x131c180` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 340 | `resource_packs_ready_for_validation` | ResourcePacksReadyForValidationPacket | yes | generated | typed | `0x76bc0` (return-only) | `src/client/systems/multiplayer/handlers/SessionHandlers.cpp:226` |
| 341 | `locator_bar` | LocatorBarPacket | yes | generated | typed | `0x130af50` | `src/client/systems/multiplayer/BedrockPlayerState.cpp:1176` |
| 342 | `party_changed` | PartyChangedPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 343 | `serverbound_data_driven_screen_closed` | ServerboundDataDrivenScreenClosedPacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 344 | `sync_world_clocks` | SyncWorldClocksPacket | yes | generated | typed | `0x132a710` | `src/client/systems/multiplayer/handlers/WorldStateHandlers.cpp:91` |
| 345 | `clientbound_attribute_layer_sync` | ClientboundAttributeLayerSyncPacket | yes | generated | no-op | `0x130e3d0` (mark handled only) | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1192` |
| 346 | `server_store_info` | ServerStoreInfoPacket | yes | generated | no receive handler | `0x132a980` | — |
| 347 | `server_presence_info` | ServerPresenceInfoPacket | yes | generated | no receive handler | `0x132abf0` | — |
| 348 | `clientbound_update_sound_data` | ClientboundUpdateSoundDataPacket | yes | generated | typed | `0x131f6e0` | `src/client/systems/multiplayer/BedrockWorldExtras.cpp:1077` |
| 349 | `send_party_destination_cookie` | SendPartyDestinationCookiePacket | yes | generated | no receive handler | `0x132afd0` | — |
| 350 | `party_destination_cookie_response` | PartyDestinationCookieResponsePacket | yes | generated | no receive handler | `0x76bc0` (return-only) | — |
| 351 | `set_player_furnace_options` | SetPlayerFurnaceOptionsPacket | yes | generated | typed | `0x130cca0` | `src/client/systems/multiplayer/BedrockServerUi.cpp:181` |
| 352 | `record_started` | RecordStartedPacket | yes | generated | no receive handler | `0x131f7b0` | — |
