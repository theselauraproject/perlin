#pragma once

template <class T> void wire_events(T& self, Context& ctx) {
    if constexpr (requires(T& t, const typename event::Tick::Payload& p) { t.onTick(p); })
        ctx.on<event::Tick>([&self](const typename event::Tick::Payload& p) { return self.onTick(p); });
    else if constexpr (requires(T& t) { t.onTick(); })
        ctx.on<event::Tick>([&self]() { return self.onTick(); });
    if constexpr (requires(T& t, const typename event::Frame::Payload& p) { t.onFrame(p); })
        ctx.on<event::Frame>([&self](const typename event::Frame::Payload& p) { return self.onFrame(p); });
    else if constexpr (requires(T& t) { t.onFrame(); })
        ctx.on<event::Frame>([&self]() { return self.onFrame(); });
    if constexpr (requires(T& t, const typename event::ChatReceived::Payload& p) { t.onChatReceived(p); })
        ctx.on<event::ChatReceived>([&self](const typename event::ChatReceived::Payload& p) { return self.onChatReceived(p); });
    else if constexpr (requires(T& t) { t.onChatReceived(); })
        ctx.on<event::ChatReceived>([&self]() { return self.onChatReceived(); });
    if constexpr (requires(T& t, const typename event::ChatSending::Payload& p) { t.onChatSending(p); })
        ctx.on<event::ChatSending>([&self](const typename event::ChatSending::Payload& p) { return self.onChatSending(p); });
    else if constexpr (requires(T& t) { t.onChatSending(); })
        ctx.on<event::ChatSending>([&self]() { return self.onChatSending(); });
    if constexpr (requires(T& t, const typename event::Connected::Payload& p) { t.onConnected(p); })
        ctx.on<event::Connected>([&self](const typename event::Connected::Payload& p) { return self.onConnected(p); });
    else if constexpr (requires(T& t) { t.onConnected(); })
        ctx.on<event::Connected>([&self]() { return self.onConnected(); });
    if constexpr (requires(T& t, const typename event::Disconnected::Payload& p) { t.onDisconnected(p); })
        ctx.on<event::Disconnected>([&self](const typename event::Disconnected::Payload& p) { return self.onDisconnected(p); });
    else if constexpr (requires(T& t) { t.onDisconnected(); })
        ctx.on<event::Disconnected>([&self]() { return self.onDisconnected(); });
    if constexpr (requires(T& t, const typename event::Key::Payload& p) { t.onKey(p); })
        ctx.on<event::Key>([&self](const typename event::Key::Payload& p) { return self.onKey(p); });
    else if constexpr (requires(T& t) { t.onKey(); })
        ctx.on<event::Key>([&self]() { return self.onKey(); });
    if constexpr (requires(T& t, const typename event::Damage::Payload& p) { t.onDamage(p); })
        ctx.on<event::Damage>([&self](const typename event::Damage::Payload& p) { return self.onDamage(p); });
    else if constexpr (requires(T& t) { t.onDamage(); })
        ctx.on<event::Damage>([&self]() { return self.onDamage(); });
    if constexpr (requires(T& t, const typename event::Death::Payload& p) { t.onDeath(p); })
        ctx.on<event::Death>([&self](const typename event::Death::Payload& p) { return self.onDeath(p); });
    else if constexpr (requires(T& t) { t.onDeath(); })
        ctx.on<event::Death>([&self]() { return self.onDeath(); });
    if constexpr (requires(T& t, const typename event::Respawned::Payload& p) { t.onRespawned(p); })
        ctx.on<event::Respawned>([&self](const typename event::Respawned::Payload& p) { return self.onRespawned(p); });
    else if constexpr (requires(T& t) { t.onRespawned(); })
        ctx.on<event::Respawned>([&self]() { return self.onRespawned(); });
    if constexpr (requires(T& t, const typename event::DimensionChanged::Payload& p) { t.onDimensionChanged(p); })
        ctx.on<event::DimensionChanged>([&self](const typename event::DimensionChanged::Payload& p) { return self.onDimensionChanged(p); });
    else if constexpr (requires(T& t) { t.onDimensionChanged(); })
        ctx.on<event::DimensionChanged>([&self]() { return self.onDimensionChanged(); });
    if constexpr (requires(T& t, const typename event::ScreenChanged::Payload& p) { t.onScreenChanged(p); })
        ctx.on<event::ScreenChanged>([&self](const typename event::ScreenChanged::Payload& p) { return self.onScreenChanged(p); });
    else if constexpr (requires(T& t) { t.onScreenChanged(); })
        ctx.on<event::ScreenChanged>([&self]() { return self.onScreenChanged(); });
    if constexpr (requires(T& t, const typename event::SlotChanged::Payload& p) { t.onSlotChanged(p); })
        ctx.on<event::SlotChanged>([&self](const typename event::SlotChanged::Payload& p) { return self.onSlotChanged(p); });
    else if constexpr (requires(T& t) { t.onSlotChanged(); })
        ctx.on<event::SlotChanged>([&self]() { return self.onSlotChanged(); });
    if constexpr (requires(T& t, const typename event::EntityAdded::Payload& p) { t.onEntityAdded(p); })
        ctx.on<event::EntityAdded>([&self](const typename event::EntityAdded::Payload& p) { return self.onEntityAdded(p); });
    else if constexpr (requires(T& t) { t.onEntityAdded(); })
        ctx.on<event::EntityAdded>([&self]() { return self.onEntityAdded(); });
    if constexpr (requires(T& t, const typename event::EntityRemoved::Payload& p) { t.onEntityRemoved(p); })
        ctx.on<event::EntityRemoved>([&self](const typename event::EntityRemoved::Payload& p) { return self.onEntityRemoved(p); });
    else if constexpr (requires(T& t) { t.onEntityRemoved(); })
        ctx.on<event::EntityRemoved>([&self]() { return self.onEntityRemoved(); });
    if constexpr (requires(T& t, const typename event::Sound::Payload& p) { t.onSound(p); })
        ctx.on<event::Sound>([&self](const typename event::Sound::Payload& p) { return self.onSound(p); });
    else if constexpr (requires(T& t) { t.onSound(); })
        ctx.on<event::Sound>([&self]() { return self.onSound(); });
    if constexpr (requires(T& t, const typename event::Packet::Payload& p) { t.onPacket(p); })
        ctx.on<event::Packet>([&self](const typename event::Packet::Payload& p) { return self.onPacket(p); });
    else if constexpr (requires(T& t) { t.onPacket(); })
        ctx.on<event::Packet>([&self]() { return self.onPacket(); });
    if constexpr (requires(T& t, const typename event::Title::Payload& p) { t.onTitle(p); })
        ctx.on<event::Title>([&self](const typename event::Title::Payload& p) { return self.onTitle(p); });
    else if constexpr (requires(T& t) { t.onTitle(); })
        ctx.on<event::Title>([&self]() { return self.onTitle(); });
    if constexpr (requires(T& t, const typename event::SettingChanged::Payload& p) { t.onSettingChanged(p); })
        ctx.on<event::SettingChanged>([&self](const typename event::SettingChanged::Payload& p) { return self.onSettingChanged(p); });
    else if constexpr (requires(T& t) { t.onSettingChanged(); })
        ctx.on<event::SettingChanged>([&self]() { return self.onSettingChanged(); });
    if constexpr (requires(T& t, const typename event::Look::Payload& p) { t.onLook(p); })
        ctx.on<event::Look>([&self](const typename event::Look::Payload& p) { return self.onLook(p); });
    else if constexpr (requires(T& t) { t.onLook(); })
        ctx.on<event::Look>([&self]() { return self.onLook(); });
    if constexpr (requires(T& t, const typename event::MouseMove::Payload& p) { t.onMouseMove(p); })
        ctx.on<event::MouseMove>([&self](const typename event::MouseMove::Payload& p) { return self.onMouseMove(p); });
    else if constexpr (requires(T& t) { t.onMouseMove(); })
        ctx.on<event::MouseMove>([&self]() { return self.onMouseMove(); });
    if constexpr (requires(T& t, const typename event::MouseButton::Payload& p) { t.onMouseButton(p); })
        ctx.on<event::MouseButton>([&self](const typename event::MouseButton::Payload& p) { return self.onMouseButton(p); });
    else if constexpr (requires(T& t) { t.onMouseButton(); })
        ctx.on<event::MouseButton>([&self]() { return self.onMouseButton(); });
    if constexpr (requires(T& t, const typename event::MouseWheel::Payload& p) { t.onMouseWheel(p); })
        ctx.on<event::MouseWheel>([&self](const typename event::MouseWheel::Payload& p) { return self.onMouseWheel(p); });
    else if constexpr (requires(T& t) { t.onMouseWheel(); })
        ctx.on<event::MouseWheel>([&self]() { return self.onMouseWheel(); });
    if constexpr (requires(T& t, const typename event::KeyRaw::Payload& p) { t.onKeyRaw(p); })
        ctx.on<event::KeyRaw>([&self](const typename event::KeyRaw::Payload& p) { return self.onKeyRaw(p); });
    else if constexpr (requires(T& t) { t.onKeyRaw(); })
        ctx.on<event::KeyRaw>([&self]() { return self.onKeyRaw(); });
    if constexpr (requires(T& t, const typename event::TextInput::Payload& p) { t.onTextInput(p); })
        ctx.on<event::TextInput>([&self](const typename event::TextInput::Payload& p) { return self.onTextInput(p); });
    else if constexpr (requires(T& t) { t.onTextInput(); })
        ctx.on<event::TextInput>([&self]() { return self.onTextInput(); });
    if constexpr (requires(T& t, const typename event::ScreenRender::Payload& p) { t.onScreenRender(p); })
        ctx.on<event::ScreenRender>([&self](const typename event::ScreenRender::Payload& p) { return self.onScreenRender(p); });
    else if constexpr (requires(T& t) { t.onScreenRender(); })
        ctx.on<event::ScreenRender>([&self]() { return self.onScreenRender(); });
    if constexpr (requires(T& t, const typename event::SlotClick::Payload& p) { t.onSlotClick(p); })
        ctx.on<event::SlotClick>([&self](const typename event::SlotClick::Payload& p) { return self.onSlotClick(p); });
    else if constexpr (requires(T& t) { t.onSlotClick(); })
        ctx.on<event::SlotClick>([&self]() { return self.onSlotClick(); });
    if constexpr (requires(T& t, const typename event::Interact::Payload& p) { t.onInteract(p); })
        ctx.on<event::Interact>([&self](const typename event::Interact::Payload& p) { return self.onInteract(p); });
    else if constexpr (requires(T& t) { t.onInteract(); })
        ctx.on<event::Interact>([&self]() { return self.onInteract(); });
    if constexpr (requires(T& t, const typename event::ChunkLoaded::Payload& p) { t.onChunkLoaded(p); })
        ctx.on<event::ChunkLoaded>([&self](const typename event::ChunkLoaded::Payload& p) { return self.onChunkLoaded(p); });
    else if constexpr (requires(T& t) { t.onChunkLoaded(); })
        ctx.on<event::ChunkLoaded>([&self]() { return self.onChunkLoaded(); });
    if constexpr (requires(T& t, const typename event::ChunkUnloaded::Payload& p) { t.onChunkUnloaded(p); })
        ctx.on<event::ChunkUnloaded>([&self](const typename event::ChunkUnloaded::Payload& p) { return self.onChunkUnloaded(p); });
    else if constexpr (requires(T& t) { t.onChunkUnloaded(); })
        ctx.on<event::ChunkUnloaded>([&self]() { return self.onChunkUnloaded(); });
    if constexpr (requires(T& t, const typename event::BlockChanged::Payload& p) { t.onBlockChanged(p); })
        ctx.on<event::BlockChanged>([&self](const typename event::BlockChanged::Payload& p) { return self.onBlockChanged(p); });
    else if constexpr (requires(T& t) { t.onBlockChanged(); })
        ctx.on<event::BlockChanged>([&self]() { return self.onBlockChanged(); });
    if constexpr (requires(T& t, const typename event::ResourcesReloaded::Payload& p) { t.onResourcesReloaded(p); })
        ctx.on<event::ResourcesReloaded>([&self](const typename event::ResourcesReloaded::Payload& p) { return self.onResourcesReloaded(p); });
    else if constexpr (requires(T& t) { t.onResourcesReloaded(); })
        ctx.on<event::ResourcesReloaded>([&self]() { return self.onResourcesReloaded(); });
    if constexpr (requires(T& t, const typename event::LevelLoaded::Payload& p) { t.onLevelLoaded(p); })
        ctx.on<event::LevelLoaded>([&self](const typename event::LevelLoaded::Payload& p) { return self.onLevelLoaded(p); });
    else if constexpr (requires(T& t) { t.onLevelLoaded(); })
        ctx.on<event::LevelLoaded>([&self]() { return self.onLevelLoaded(); });
    if constexpr (requires(T& t, const typename event::LevelUnloaded::Payload& p) { t.onLevelUnloaded(p); })
        ctx.on<event::LevelUnloaded>([&self](const typename event::LevelUnloaded::Payload& p) { return self.onLevelUnloaded(p); });
    else if constexpr (requires(T& t) { t.onLevelUnloaded(); })
        ctx.on<event::LevelUnloaded>([&self]() { return self.onLevelUnloaded(); });
    if constexpr (requires(T& t, const typename event::ModsLoaded::Payload& p) { t.onModsLoaded(p); })
        ctx.on<event::ModsLoaded>([&self](const typename event::ModsLoaded::Payload& p) { return self.onModsLoaded(p); });
    else if constexpr (requires(T& t) { t.onModsLoaded(); })
        ctx.on<event::ModsLoaded>([&self]() { return self.onModsLoaded(); });
    if constexpr (requires(T& t, const typename event::ClientStopping::Payload& p) { t.onClientStopping(p); })
        ctx.on<event::ClientStopping>([&self](const typename event::ClientStopping::Payload& p) { return self.onClientStopping(p); });
    else if constexpr (requires(T& t) { t.onClientStopping(); })
        ctx.on<event::ClientStopping>([&self]() { return self.onClientStopping(); });
    if constexpr (requires(T& t, const typename event::Toast::Payload& p) { t.onToast(p); })
        ctx.on<event::Toast>([&self](const typename event::Toast::Payload& p) { return self.onToast(p); });
    else if constexpr (requires(T& t) { t.onToast(); })
        ctx.on<event::Toast>([&self]() { return self.onToast(); });
    if constexpr (requires(T& t, const typename event::ModMessage::Payload& p) { t.onModMessage(p); })
        ctx.on<event::ModMessage>([&self](const typename event::ModMessage::Payload& p) { return self.onModMessage(p); });
    else if constexpr (requires(T& t) { t.onModMessage(); })
        ctx.on<event::ModMessage>([&self]() { return self.onModMessage(); });
    if constexpr (requires(T& t, const typename event::Tooltip::Payload& p) { t.onTooltip(p); })
        ctx.on<event::Tooltip>([&self](const typename event::Tooltip::Payload& p) { return self.onTooltip(p); });
    else if constexpr (requires(T& t) { t.onTooltip(); })
        ctx.on<event::Tooltip>([&self]() { return self.onTooltip(); });
    if constexpr (requires(T& t, const typename event::ScreenButton::Payload& p) { t.onScreenButton(p); })
        ctx.on<event::ScreenButton>([&self](const typename event::ScreenButton::Payload& p) { return self.onScreenButton(p); });
    else if constexpr (requires(T& t) { t.onScreenButton(); })
        ctx.on<event::ScreenButton>([&self]() { return self.onScreenButton(); });
    if constexpr (requires(T& t, const typename event::WindowFocus::Payload& p) { t.onWindowFocus(p); })
        ctx.on<event::WindowFocus>([&self](const typename event::WindowFocus::Payload& p) { return self.onWindowFocus(p); });
    else if constexpr (requires(T& t) { t.onWindowFocus(); })
        ctx.on<event::WindowFocus>([&self]() { return self.onWindowFocus(); });
    if constexpr (requires(T& t, const typename event::ServerPing::Payload& p) { t.onServerPing(p); })
        ctx.on<event::ServerPing>([&self](const typename event::ServerPing::Payload& p) { return self.onServerPing(p); });
    else if constexpr (requires(T& t) { t.onServerPing(); })
        ctx.on<event::ServerPing>([&self]() { return self.onServerPing(); });
    if constexpr (requires(T& t, const typename event::PlayerJoin::Payload& p) { t.onPlayerJoin(p); })
        ctx.on<event::PlayerJoin>([&self](const typename event::PlayerJoin::Payload& p) { return self.onPlayerJoin(p); });
    else if constexpr (requires(T& t) { t.onPlayerJoin(); })
        ctx.on<event::PlayerJoin>([&self]() { return self.onPlayerJoin(); });
    if constexpr (requires(T& t, const typename event::PlayerQuit::Payload& p) { t.onPlayerQuit(p); })
        ctx.on<event::PlayerQuit>([&self](const typename event::PlayerQuit::Payload& p) { return self.onPlayerQuit(p); });
    else if constexpr (requires(T& t) { t.onPlayerQuit(); })
        ctx.on<event::PlayerQuit>([&self]() { return self.onPlayerQuit(); });
    if constexpr (requires(T& t, const typename event::PlayerChat::Payload& p) { t.onPlayerChat(p); })
        ctx.on<event::PlayerChat>([&self](const typename event::PlayerChat::Payload& p) { return self.onPlayerChat(p); });
    else if constexpr (requires(T& t) { t.onPlayerChat(); })
        ctx.on<event::PlayerChat>([&self]() { return self.onPlayerChat(); });
    if constexpr (requires(T& t, const typename event::BlockBreak::Payload& p) { t.onBlockBreak(p); })
        ctx.on<event::BlockBreak>([&self](const typename event::BlockBreak::Payload& p) { return self.onBlockBreak(p); });
    else if constexpr (requires(T& t) { t.onBlockBreak(); })
        ctx.on<event::BlockBreak>([&self]() { return self.onBlockBreak(); });
    if constexpr (requires(T& t, const typename event::BlockPlace::Payload& p) { t.onBlockPlace(p); })
        ctx.on<event::BlockPlace>([&self](const typename event::BlockPlace::Payload& p) { return self.onBlockPlace(p); });
    else if constexpr (requires(T& t) { t.onBlockPlace(); })
        ctx.on<event::BlockPlace>([&self]() { return self.onBlockPlace(); });
    if constexpr (requires(T& t, const typename event::PlayerDamage::Payload& p) { t.onPlayerDamage(p); })
        ctx.on<event::PlayerDamage>([&self](const typename event::PlayerDamage::Payload& p) { return self.onPlayerDamage(p); });
    else if constexpr (requires(T& t) { t.onPlayerDamage(); })
        ctx.on<event::PlayerDamage>([&self]() { return self.onPlayerDamage(); });
    if constexpr (requires(T& t, const typename event::PlayerDeath::Payload& p) { t.onPlayerDeath(p); })
        ctx.on<event::PlayerDeath>([&self](const typename event::PlayerDeath::Payload& p) { return self.onPlayerDeath(p); });
    else if constexpr (requires(T& t) { t.onPlayerDeath(); })
        ctx.on<event::PlayerDeath>([&self]() { return self.onPlayerDeath(); });
    if constexpr (requires(T& t, const typename event::PlayerRespawn::Payload& p) { t.onPlayerRespawn(p); })
        ctx.on<event::PlayerRespawn>([&self](const typename event::PlayerRespawn::Payload& p) { return self.onPlayerRespawn(p); });
    else if constexpr (requires(T& t) { t.onPlayerRespawn(); })
        ctx.on<event::PlayerRespawn>([&self]() { return self.onPlayerRespawn(); });
    if constexpr (requires(T& t, const typename event::PlayerUseBlock::Payload& p) { t.onPlayerUseBlock(p); })
        ctx.on<event::PlayerUseBlock>([&self](const typename event::PlayerUseBlock::Payload& p) { return self.onPlayerUseBlock(p); });
    else if constexpr (requires(T& t) { t.onPlayerUseBlock(); })
        ctx.on<event::PlayerUseBlock>([&self]() { return self.onPlayerUseBlock(); });
    if constexpr (requires(T& t, const typename event::PlayerAttack::Payload& p) { t.onPlayerAttack(p); })
        ctx.on<event::PlayerAttack>([&self](const typename event::PlayerAttack::Payload& p) { return self.onPlayerAttack(p); });
    else if constexpr (requires(T& t) { t.onPlayerAttack(); })
        ctx.on<event::PlayerAttack>([&self]() { return self.onPlayerAttack(); });
    if constexpr (requires(T& t, const typename event::EntityDamage::Payload& p) { t.onEntityDamage(p); })
        ctx.on<event::EntityDamage>([&self](const typename event::EntityDamage::Payload& p) { return self.onEntityDamage(p); });
    else if constexpr (requires(T& t) { t.onEntityDamage(); })
        ctx.on<event::EntityDamage>([&self]() { return self.onEntityDamage(); });
    if constexpr (requires(T& t, const typename event::EntityDeath::Payload& p) { t.onEntityDeath(p); })
        ctx.on<event::EntityDeath>([&self](const typename event::EntityDeath::Payload& p) { return self.onEntityDeath(p); });
    else if constexpr (requires(T& t) { t.onEntityDeath(); })
        ctx.on<event::EntityDeath>([&self]() { return self.onEntityDeath(); });
    if constexpr (requires(T& t, const typename event::EntitySpawn::Payload& p) { t.onEntitySpawn(p); })
        ctx.on<event::EntitySpawn>([&self](const typename event::EntitySpawn::Payload& p) { return self.onEntitySpawn(p); });
    else if constexpr (requires(T& t) { t.onEntitySpawn(); })
        ctx.on<event::EntitySpawn>([&self]() { return self.onEntitySpawn(); });
    if constexpr (requires(T& t, const typename event::ItemConsume::Payload& p) { t.onItemConsume(p); })
        ctx.on<event::ItemConsume>([&self](const typename event::ItemConsume::Payload& p) { return self.onItemConsume(p); });
    else if constexpr (requires(T& t) { t.onItemConsume(); })
        ctx.on<event::ItemConsume>([&self]() { return self.onItemConsume(); });
    if constexpr (requires(T& t, const typename event::GameModeChange::Payload& p) { t.onGameModeChange(p); })
        ctx.on<event::GameModeChange>([&self](const typename event::GameModeChange::Payload& p) { return self.onGameModeChange(p); });
    else if constexpr (requires(T& t) { t.onGameModeChange(); })
        ctx.on<event::GameModeChange>([&self]() { return self.onGameModeChange(); });
    if constexpr (requires(T& t, const typename event::FormResponse::Payload& p) { t.onFormResponse(p); })
        ctx.on<event::FormResponse>([&self](const typename event::FormResponse::Payload& p) { return self.onFormResponse(p); });
    else if constexpr (requires(T& t) { t.onFormResponse(); })
        ctx.on<event::FormResponse>([&self]() { return self.onFormResponse(); });
    if constexpr (requires(T& t, const typename event::PlayerMove::Payload& p) { t.onPlayerMove(p); })
        ctx.on<event::PlayerMove>([&self](const typename event::PlayerMove::Payload& p) { return self.onPlayerMove(p); });
    else if constexpr (requires(T& t) { t.onPlayerMove(); })
        ctx.on<event::PlayerMove>([&self]() { return self.onPlayerMove(); });
    if constexpr (requires(T& t, const typename event::Explosion::Payload& p) { t.onExplosion(p); })
        ctx.on<event::Explosion>([&self](const typename event::Explosion::Payload& p) { return self.onExplosion(p); });
    else if constexpr (requires(T& t) { t.onExplosion(); })
        ctx.on<event::Explosion>([&self]() { return self.onExplosion(); });
    if constexpr (requires(T& t, const typename event::ItemDrop::Payload& p) { t.onItemDrop(p); })
        ctx.on<event::ItemDrop>([&self](const typename event::ItemDrop::Payload& p) { return self.onItemDrop(p); });
    else if constexpr (requires(T& t) { t.onItemDrop(); })
        ctx.on<event::ItemDrop>([&self]() { return self.onItemDrop(); });
    if constexpr (requires(T& t, const typename event::ItemPickup::Payload& p) { t.onItemPickup(p); })
        ctx.on<event::ItemPickup>([&self](const typename event::ItemPickup::Payload& p) { return self.onItemPickup(p); });
    else if constexpr (requires(T& t) { t.onItemPickup(); })
        ctx.on<event::ItemPickup>([&self]() { return self.onItemPickup(); });
    if constexpr (requires(T& t, const typename event::ContainerOpen::Payload& p) { t.onContainerOpen(p); })
        ctx.on<event::ContainerOpen>([&self](const typename event::ContainerOpen::Payload& p) { return self.onContainerOpen(p); });
    else if constexpr (requires(T& t) { t.onContainerOpen(); })
        ctx.on<event::ContainerOpen>([&self]() { return self.onContainerOpen(); });
    if constexpr (requires(T& t, const typename event::DimensionChange::Payload& p) { t.onDimensionChange(p); })
        ctx.on<event::DimensionChange>([&self](const typename event::DimensionChange::Payload& p) { return self.onDimensionChange(p); });
    else if constexpr (requires(T& t) { t.onDimensionChange(); })
        ctx.on<event::DimensionChange>([&self]() { return self.onDimensionChange(); });
    if constexpr (requires(T& t, const typename event::ServerCommand::Payload& p) { t.onServerCommand(p); })
        ctx.on<event::ServerCommand>([&self](const typename event::ServerCommand::Payload& p) { return self.onServerCommand(p); });
    else if constexpr (requires(T& t) { t.onServerCommand(); })
        ctx.on<event::ServerCommand>([&self]() { return self.onServerCommand(); });
    if constexpr (requires(T& t, const typename event::PreLogin::Payload& p) { t.onPreLogin(p); })
        ctx.on<event::PreLogin>([&self](const typename event::PreLogin::Payload& p) { return self.onPreLogin(p); });
    else if constexpr (requires(T& t) { t.onPreLogin(); })
        ctx.on<event::PreLogin>([&self]() { return self.onPreLogin(); });
}
