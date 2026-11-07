#pragma once
#include "perlin/rpc.hpp"

namespace perlin::api {
    inline ::perlin::Result<void> log(::perlin::abi::LogLevel level, std::string text) {
        return ::perlin::call<::perlin::abi::Op::Log>(::perlin::abi::LogRequest{level, std::move(text)});
    }
    inline ::perlin::Result<::perlin::abi::Meta> meta() {
        return ::perlin::call<::perlin::abi::Op::Meta>();
    }
    inline ::perlin::Result<int64_t> clock() {
        return ::perlin::call<::perlin::abi::Op::Clock>();
    }
    inline ::perlin::Result<::perlin::abi::PlayerState> player_state() {
        return ::perlin::call<::perlin::abi::Op::PlayerState>();
    }
    inline ::perlin::Result<::perlin::abi::PlayerEffectsResponse> player_effects() {
        return ::perlin::call<::perlin::abi::Op::PlayerEffects>();
    }
    inline ::perlin::Result<::perlin::abi::PlayerLookResponse> player_look() {
        return ::perlin::call<::perlin::abi::Op::PlayerLook>();
    }
    inline ::perlin::Result<::perlin::abi::Inventory> inventory() {
        return ::perlin::call<::perlin::abi::Op::Inventory>();
    }
    inline ::perlin::Result<void> inventory_select(int32_t slot) {
        return ::perlin::call<::perlin::abi::Op::InventorySelect>(::perlin::abi::InventorySelectRequest{slot});
    }
    inline ::perlin::Result<void> inventory_drop(int32_t count) {
        return ::perlin::call<::perlin::abi::Op::InventoryDrop>(::perlin::abi::InventoryDropRequest{count});
    }
    inline ::perlin::Result<void> inventory_swap(int32_t a, int32_t b) {
        return ::perlin::call<::perlin::abi::Op::InventorySwap>(::perlin::abi::InventorySwapRequest{a, b});
    }
    inline ::perlin::Result<void> inventory_click(int32_t slot, bool right, bool shift) {
        return ::perlin::call<::perlin::abi::Op::InventoryClick>(::perlin::abi::InventoryClickRequest{slot, right, shift});
    }
    inline ::perlin::Result<void> inventory_open() {
        return ::perlin::call<::perlin::abi::Op::InventoryOpen>();
    }
    inline ::perlin::Result<void> inventory_close() {
        return ::perlin::call<::perlin::abi::Op::InventoryClose>();
    }
    inline ::perlin::Result<::perlin::abi::WorldState> world_state() {
        return ::perlin::call<::perlin::abi::Op::WorldState>();
    }
    inline ::perlin::Result<::perlin::abi::BlockInfo> block(int32_t x, int32_t y, int32_t z) {
        return ::perlin::call<::perlin::abi::Op::Block>(::perlin::abi::BlockRequest{x, y, z});
    }
    inline ::perlin::Result<::perlin::abi::Region> region(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1) {
        return ::perlin::call<::perlin::abi::Op::Region>(::perlin::abi::RegionRequest{x0, y0, z0, x1, y1, z1});
    }
    inline ::perlin::Result<::perlin::abi::RayHit> raycast(double ox, double oy, double oz, double dx, double dy, double dz, double maxDistance, bool liquids) {
        return ::perlin::call<::perlin::abi::Op::Raycast>(::perlin::abi::RaycastRequest{ox, oy, oz, dx, dy, dz, maxDistance, liquids});
    }
    inline ::perlin::Result<::perlin::abi::AimState> aim() {
        return ::perlin::call<::perlin::abi::Op::Aim>();
    }
    inline ::perlin::Result<::perlin::abi::FindBlocksResponse> find_blocks(int32_t x, int32_t y, int32_t z, int32_t radius, int32_t limit, bool exact, std::string name) {
        return ::perlin::call<::perlin::abi::Op::FindBlocks>(::perlin::abi::FindBlocksRequest{x, y, z, radius, limit, exact, std::move(name)});
    }
    inline ::perlin::Result<bool> chunk_loaded(int32_t x, int32_t z) {
        return ::perlin::call<::perlin::abi::Op::ChunkLoaded>(::perlin::abi::ChunkLoadedRequest{x, z});
    }
    inline ::perlin::Result<::perlin::abi::BlockNamesResponse> block_names() {
        return ::perlin::call<::perlin::abi::Op::BlockNames>();
    }
    inline ::perlin::Result<::perlin::abi::BlockProps> block_props(std::string name) {
        return ::perlin::call<::perlin::abi::Op::BlockProps>(::perlin::abi::BlockPropsRequest{std::move(name)});
    }
    inline ::perlin::Result<::perlin::abi::BlockEntityInfo> block_entity_data(int32_t x, int32_t y, int32_t z) {
        return ::perlin::call<::perlin::abi::Op::BlockEntityData>(::perlin::abi::BlockEntityDataRequest{x, y, z});
    }
    inline ::perlin::Result<::perlin::abi::RestText> display_name(::perlin::abi::NameKind kind, std::string id, int32_t aux) {
        return ::perlin::call<::perlin::abi::Op::DisplayName>(::perlin::abi::DisplayNameRequest{kind, std::move(id), aux});
    }
    inline ::perlin::Result<::perlin::abi::BreakState> break_progress() {
        return ::perlin::call<::perlin::abi::Op::BreakProgress>();
    }
    inline ::perlin::Result<::perlin::abi::RestBytes> map_chunk(int32_t x, int32_t z, int32_t caveY) {
        return ::perlin::call<::perlin::abi::Op::MapChunk>(::perlin::abi::MapChunkRequest{x, z, caveY});
    }
    inline ::perlin::Result<::perlin::abi::EntitiesResponse> entities(double radius, std::string type, bool playersOnly, bool includeItems) {
        return ::perlin::call<::perlin::abi::Op::Entities>(::perlin::abi::EntitiesRequest{radius, std::move(type), playersOnly, includeItems});
    }
    inline ::perlin::Result<::perlin::abi::Entity> entity_by_id(uint64_t id) {
        return ::perlin::call<::perlin::abi::Op::EntityById>(::perlin::abi::EntityByIdRequest{id});
    }
    inline ::perlin::Result<::perlin::abi::PlayerListResponse> player_list() {
        return ::perlin::call<::perlin::abi::Op::PlayerList>();
    }
    inline ::perlin::Result<::perlin::abi::RestText> options() {
        return ::perlin::call<::perlin::abi::Op::Options>();
    }
    inline ::perlin::Result<::perlin::abi::RestText> option_get(std::string key) {
        return ::perlin::call<::perlin::abi::Op::OptionGet>(::perlin::abi::OptionGetRequest{std::move(key)});
    }
    inline ::perlin::Result<void> option_set(std::string key, std::string value) {
        return ::perlin::call<::perlin::abi::Op::OptionSet>(::perlin::abi::OptionSetRequest{std::move(key), std::move(value)});
    }
    inline ::perlin::Result<::perlin::abi::ScreenState> screen() {
        return ::perlin::call<::perlin::abi::Op::Screen>();
    }
    inline ::perlin::Result<::perlin::abi::RestText> translate(std::string key) {
        return ::perlin::call<::perlin::abi::Op::Translate>(::perlin::abi::TranslateRequest{std::move(key)});
    }
    inline ::perlin::Result<void> reload_resources() {
        return ::perlin::call<::perlin::abi::Op::ReloadResources>();
    }
    inline ::perlin::Result<::perlin::abi::Perspective> perspective_get() {
        return ::perlin::call<::perlin::abi::Op::PerspectiveGet>();
    }
    inline ::perlin::Result<void> perspective_set(::perlin::abi::Perspective perspective) {
        return ::perlin::call<::perlin::abi::Op::PerspectiveSet>(::perlin::abi::PerspectiveSetRequest{perspective});
    }
    inline ::perlin::Result<::perlin::abi::ResourcePacksResponse> resource_packs() {
        return ::perlin::call<::perlin::abi::Op::ResourcePacks>();
    }
    inline ::perlin::Result<bool> input_down(std::string name) {
        return ::perlin::call<::perlin::abi::Op::InputDown>(::perlin::abi::InputDownRequest{std::move(name)});
    }
    inline ::perlin::Result<void> input_override(const ::perlin::abi::InputOverrideState& request) {
        return ::perlin::call<::perlin::abi::Op::InputOverride>(request);
    }
    inline ::perlin::Result<void> key_register(std::string id, std::string label, std::string fallback) {
        return ::perlin::call<::perlin::abi::Op::KeyRegister>(::perlin::abi::KeyRegisterRequest{std::move(id), std::move(label), std::move(fallback)});
    }
    inline ::perlin::Result<::perlin::abi::InputActionsResponse> input_actions() {
        return ::perlin::call<::perlin::abi::Op::InputActions>();
    }
    inline ::perlin::Result<::perlin::abi::MousePositionResponse> mouse_position() {
        return ::perlin::call<::perlin::abi::Op::MousePosition>();
    }
    inline ::perlin::Result<void> chat(std::string text) {
        return ::perlin::call<::perlin::abi::Op::Chat>(::perlin::abi::ChatRequest{std::move(text)});
    }
    inline ::perlin::Result<void> command(std::string line) {
        return ::perlin::call<::perlin::abi::Op::Command>(::perlin::abi::CommandRequest{std::move(line)});
    }
    inline ::perlin::Result<void> local_message(std::string text) {
        return ::perlin::call<::perlin::abi::Op::LocalMessage>(::perlin::abi::LocalMessageRequest{std::move(text)});
    }
    inline ::perlin::Result<void> command_register(std::string name, std::string usage, std::string description) {
        return ::perlin::call<::perlin::abi::Op::CommandRegister>(::perlin::abi::CommandRegisterRequest{std::move(name), std::move(usage), std::move(description)});
    }
    inline ::perlin::Result<void> command_unregister(std::string name) {
        return ::perlin::call<::perlin::abi::Op::CommandUnregister>(::perlin::abi::CommandUnregisterRequest{std::move(name)});
    }
    inline ::perlin::Result<::perlin::abi::ServerCommandsResponse> server_commands() {
        return ::perlin::call<::perlin::abi::Op::ServerCommands>();
    }
    inline ::perlin::Result<void> toast(std::string text, int32_t ms) {
        return ::perlin::call<::perlin::abi::Op::Toast>(::perlin::abi::ToastRequest{std::move(text), ms});
    }
    inline ::perlin::Result<void> title(std::string title, std::string subtitle, int32_t fadeIn, int32_t stay, int32_t fadeOut) {
        return ::perlin::call<::perlin::abi::Op::Title>(::perlin::abi::TitleRequest{std::move(title), std::move(subtitle), fadeIn, stay, fadeOut});
    }
    inline ::perlin::Result<::perlin::abi::ChatHistoryResponse> chat_history(uint32_t count) {
        return ::perlin::call<::perlin::abi::Op::ChatHistory>(::perlin::abi::ChatHistoryRequest{count});
    }
    inline ::perlin::Result<::perlin::abi::TitleStateData> title_state() {
        return ::perlin::call<::perlin::abi::Op::TitleState>();
    }
    inline ::perlin::Result<void> swing_arm() {
        return ::perlin::call<::perlin::abi::Op::SwingArm>();
    }
    inline ::perlin::Result<void> use_item() {
        return ::perlin::call<::perlin::abi::Op::UseItem>();
    }
    inline ::perlin::Result<void> use_on_block(int32_t x, int32_t y, int32_t z, ::perlin::abi::Face face) {
        return ::perlin::call<::perlin::abi::Op::UseOnBlock>(::perlin::abi::UseOnBlockRequest{x, y, z, face});
    }
    inline ::perlin::Result<void> attack_entity(uint64_t entity) {
        return ::perlin::call<::perlin::abi::Op::AttackEntity>(::perlin::abi::AttackEntityRequest{entity});
    }
    inline ::perlin::Result<void> interact_entity(uint64_t entity) {
        return ::perlin::call<::perlin::abi::Op::InteractEntity>(::perlin::abi::InteractEntityRequest{entity});
    }
    inline ::perlin::Result<void> break_block(int32_t x, int32_t y, int32_t z, ::perlin::abi::Face face) {
        return ::perlin::call<::perlin::abi::Op::BreakBlock>(::perlin::abi::BreakBlockRequest{x, y, z, face});
    }
    inline ::perlin::Result<void> look(float yaw, float pitch) {
        return ::perlin::call<::perlin::abi::Op::Look>(::perlin::abi::LookRequest{yaw, pitch});
    }
    inline ::perlin::Result<void> respawn() {
        return ::perlin::call<::perlin::abi::Op::Respawn>();
    }
    inline ::perlin::Result<void> disconnect() {
        return ::perlin::call<::perlin::abi::Op::Disconnect>();
    }
    inline ::perlin::Result<void> play_sound(std::string name, bool positional, float x, float y, float z, float volume, float pitch) {
        return ::perlin::call<::perlin::abi::Op::PlaySound>(::perlin::abi::PlaySoundRequest{std::move(name), positional, x, y, z, volume, pitch});
    }
    inline ::perlin::Result<void> jump() {
        return ::perlin::call<::perlin::abi::Op::Jump>();
    }
    inline ::perlin::Result<void> ui_submit(std::string layer, int32_t z, std::vector<uint8_t> commands) {
        return ::perlin::call<::perlin::abi::Op::UiSubmit>(::perlin::abi::UiSubmitRequest{std::move(layer), z, std::move(commands)});
    }
    inline ::perlin::Result<void> ui_clear(std::string layer) {
        return ::perlin::call<::perlin::abi::Op::UiClear>(::perlin::abi::UiClearRequest{std::move(layer)});
    }
    inline ::perlin::Result<float> ui_measure(std::string text, float scale) {
        return ::perlin::call<::perlin::abi::Op::UiMeasure>(::perlin::abi::UiMeasureRequest{std::move(text), scale});
    }
    inline ::perlin::Result<uint32_t> gfx_texture_create(int32_t width, int32_t height, bool linear, std::vector<uint8_t> rgba) {
        return ::perlin::call<::perlin::abi::Op::GfxTextureCreate>(::perlin::abi::GfxTextureCreateRequest{width, height, linear, std::move(rgba)});
    }
    inline ::perlin::Result<void> gfx_texture_update(uint32_t id, int32_t x, int32_t y, int32_t width, int32_t height, std::vector<uint8_t> rgba) {
        return ::perlin::call<::perlin::abi::Op::GfxTextureUpdate>(::perlin::abi::GfxTextureUpdateRequest{id, x, y, width, height, std::move(rgba)});
    }
    inline ::perlin::Result<void> gfx_texture_destroy(uint32_t id) {
        return ::perlin::call<::perlin::abi::Op::GfxTextureDestroy>(::perlin::abi::GfxTextureDestroyRequest{id});
    }
    inline ::perlin::Result<void> gfx_submit(std::string layer, int32_t z, std::vector<uint8_t> blob) {
        return ::perlin::call<::perlin::abi::Op::GfxSubmit>(::perlin::abi::GfxSubmitRequest{std::move(layer), z, std::move(blob)});
    }
    inline ::perlin::Result<void> gfx_clear(std::string layer) {
        return ::perlin::call<::perlin::abi::Op::GfxClear>(::perlin::abi::GfxClearRequest{std::move(layer)});
    }
    inline ::perlin::Result<::perlin::abi::GfxDisplayInfo> gfx_display() {
        return ::perlin::call<::perlin::abi::Op::GfxDisplay>();
    }
    inline ::perlin::Result<::perlin::abi::GfxImageLoadResponse> gfx_image_load(std::string path) {
        return ::perlin::call<::perlin::abi::Op::GfxImageLoad>(::perlin::abi::GfxImageLoadRequest{std::move(path)});
    }
    inline ::perlin::Result<void> input_grab(bool mouse, bool keyboard) {
        return ::perlin::call<::perlin::abi::Op::InputGrab>(::perlin::abi::InputGrabRequest{mouse, keyboard});
    }
    inline ::perlin::Result<::perlin::abi::NativeHandles> native_info() {
        return ::perlin::call<::perlin::abi::Op::NativeInfo>();
    }
    inline ::perlin::Result<::perlin::abi::RestBytes> storage_read(std::string path) {
        return ::perlin::call<::perlin::abi::Op::StorageRead>(::perlin::abi::StorageReadRequest{std::move(path)});
    }
    inline ::perlin::Result<void> storage_write(std::string path, std::vector<uint8_t> data) {
        return ::perlin::call<::perlin::abi::Op::StorageWrite>(::perlin::abi::StorageWriteRequest{std::move(path), std::move(data)});
    }
    inline ::perlin::Result<::perlin::abi::StorageListResponse> storage_list(std::string path) {
        return ::perlin::call<::perlin::abi::Op::StorageList>(::perlin::abi::StorageListRequest{std::move(path)});
    }
    inline ::perlin::Result<void> storage_remove(std::string path) {
        return ::perlin::call<::perlin::abi::Op::StorageRemove>(::perlin::abi::StorageRemoveRequest{std::move(path)});
    }
    inline ::perlin::Result<bool> storage_exists(std::string path) {
        return ::perlin::call<::perlin::abi::Op::StorageExists>(::perlin::abi::StorageExistsRequest{std::move(path)});
    }
    inline ::perlin::Result<void> storage_make_dir(std::string path) {
        return ::perlin::call<::perlin::abi::Op::StorageMakeDir>(::perlin::abi::StorageMakeDirRequest{std::move(path)});
    }
    inline ::perlin::Result<void> http_request(uint32_t id, std::string method, std::string url, int32_t timeoutMs, std::vector<std::pair<std::string, std::string>> headers, std::vector<uint8_t> body) {
        return ::perlin::call<::perlin::abi::Op::HttpRequest>(::perlin::abi::HttpRequestRequest{id, std::move(method), std::move(url), timeoutMs, std::move(headers), std::move(body)});
    }
    inline ::perlin::Result<void> packet_send(uint32_t id, std::vector<uint8_t> body) {
        return ::perlin::call<::perlin::abi::Op::PacketSend>(::perlin::abi::PacketSendRequest{id, std::move(body)});
    }
    inline ::perlin::Result<::perlin::abi::RestText> packet_name(uint32_t id) {
        return ::perlin::call<::perlin::abi::Op::PacketName>(::perlin::abi::PacketNameRequest{id});
    }
    inline ::perlin::Result<::perlin::abi::RestText> setting_get(std::string id) {
        return ::perlin::call<::perlin::abi::Op::SettingGet>(::perlin::abi::SettingGetRequest{std::move(id)});
    }
    inline ::perlin::Result<void> setting_set(std::string id, std::string value) {
        return ::perlin::call<::perlin::abi::Op::SettingSet>(::perlin::abi::SettingSetRequest{std::move(id), std::move(value)});
    }
    inline ::perlin::Result<::perlin::abi::SettingListResponse> setting_list() {
        return ::perlin::call<::perlin::abi::Op::SettingList>();
    }
    inline ::perlin::Result<::perlin::abi::ModListResponse> mod_list() {
        return ::perlin::call<::perlin::abi::Op::ModList>();
    }
    inline ::perlin::Result<void> icon_set(std::string path) {
        return ::perlin::call<::perlin::abi::Op::IconSet>(::perlin::abi::IconSetRequest{std::move(path)});
    }
    inline ::perlin::Result<void> open_screen(::perlin::abi::Screen screen) {
        return ::perlin::call<::perlin::abi::Op::OpenScreen>(::perlin::abi::OpenScreenRequest{screen});
    }
    inline ::perlin::Result<void> open_mod_settings(std::string mod) {
        return ::perlin::call<::perlin::abi::Op::OpenModSettings>(::perlin::abi::OpenModSettingsRequest{std::move(mod)});
    }
    inline ::perlin::Result<void> take_screenshot() {
        return ::perlin::call<::perlin::abi::Op::TakeScreenshot>();
    }
    inline ::perlin::Result<void> cycle_perspective() {
        return ::perlin::call<::perlin::abi::Op::CyclePerspective>();
    }
    inline ::perlin::Result<void> copy_to_clipboard(std::string text) {
        return ::perlin::call<::perlin::abi::Op::CopyToClipboard>(::perlin::abi::CopyToClipboardRequest{std::move(text)});
    }
    inline ::perlin::Result<void> open_url(std::string url) {
        return ::perlin::call<::perlin::abi::Op::OpenUrl>(::perlin::abi::OpenUrlRequest{std::move(url)});
    }
    inline ::perlin::Result<void> toggle_hud() {
        return ::perlin::call<::perlin::abi::Op::ToggleHud>();
    }
    inline ::perlin::Result<void> copy_file(::perlin::abi::FileLocation from, ::perlin::abi::FileLocation to) {
        return ::perlin::call<::perlin::abi::Op::CopyFile>(::perlin::abi::CopyFileRequest{std::move(from), std::move(to)});
    }
    inline ::perlin::Result<void> move_file(::perlin::abi::FileLocation from, ::perlin::abi::FileLocation to) {
        return ::perlin::call<::perlin::abi::Op::MoveFile>(::perlin::abi::MoveFileRequest{std::move(from), std::move(to)});
    }
    inline ::perlin::Result<void> download_file(uint32_t id, std::string url, ::perlin::abi::FileLocation to) {
        return ::perlin::call<::perlin::abi::Op::DownloadFile>(::perlin::abi::DownloadFileRequest{id, std::move(url), std::move(to)});
    }
    inline ::perlin::Result<void> camera_control(const ::perlin::abi::CameraControlState& request) {
        return ::perlin::call<::perlin::abi::Op::CameraControl>(request);
    }
    inline ::perlin::Result<::perlin::abi::CameraInfo> camera_state() {
        return ::perlin::call<::perlin::abi::Op::CameraState>();
    }
    inline ::perlin::Result<::perlin::abi::ScreenPoint> world_to_screen(const ::perlin::abi::WorldPoint& request) {
        return ::perlin::call<::perlin::abi::Op::WorldToScreen>(request);
    }
    inline ::perlin::Result<void> player_motion(const ::perlin::abi::MotionRequest& request) {
        return ::perlin::call<::perlin::abi::Op::PlayerMotion>(request);
    }
    inline ::perlin::Result<void> player_teleport(const ::perlin::abi::TeleportRequest& request) {
        return ::perlin::call<::perlin::abi::Op::PlayerTeleport>(request);
    }
    inline ::perlin::Result<void> player_model_pose(const ::perlin::abi::ModelPose& request) {
        return ::perlin::call<::perlin::abi::Op::PlayerModelPose>(request);
    }
    inline ::perlin::Result<void> player_set_look(const ::perlin::abi::LookSet& request) {
        return ::perlin::call<::perlin::abi::Op::PlayerSetLook>(request);
    }
    inline ::perlin::Result<void> world_lines(const ::perlin::abi::WorldLines& request) {
        return ::perlin::call<::perlin::abi::Op::WorldLines>(request);
    }
    inline ::perlin::Result<void> world_boxes(const ::perlin::abi::WorldBoxes& request) {
        return ::perlin::call<::perlin::abi::Op::WorldBoxes>(request);
    }
    inline ::perlin::Result<void> client_block_set(const ::perlin::abi::ClientBlockEdits& request) {
        return ::perlin::call<::perlin::abi::Op::ClientBlockSet>(request);
    }
    inline ::perlin::Result<::perlin::abi::BlockQueryResult> client_block_get(const ::perlin::abi::BlockQuery& request) {
        return ::perlin::call<::perlin::abi::Op::ClientBlockGet>(request);
    }
    inline ::perlin::Result<void> packet_drop(const ::perlin::abi::PacketDropRequest& request) {
        return ::perlin::call<::perlin::abi::Op::PacketDrop>(request);
    }
    inline ::perlin::Result<::perlin::abi::PacketCountersResponse> packet_counters() {
        return ::perlin::call<::perlin::abi::Op::PacketCounters>();
    }
    inline ::perlin::Result<void> packet_inject(uint32_t id, std::vector<uint8_t> body) {
        return ::perlin::call<::perlin::abi::Op::PacketInject>(::perlin::abi::PacketInjectRequest{id, std::move(body)});
    }
    inline ::perlin::Result<void> hud_element_set(const ::perlin::abi::HudElementState& request) {
        return ::perlin::call<::perlin::abi::Op::HudElementSet>(request);
    }
    inline ::perlin::Result<void> debug_lines(const ::perlin::abi::DebugLinesData& request) {
        return ::perlin::call<::perlin::abi::Op::DebugLines>(request);
    }
    inline ::perlin::Result<void> fog_set(const ::perlin::abi::FogOverride& request) {
        return ::perlin::call<::perlin::abi::Op::FogSet>(request);
    }
    inline ::perlin::Result<void> screen_tint(const ::perlin::abi::ScreenTintData& request) {
        return ::perlin::call<::perlin::abi::Op::ScreenTint>(request);
    }
    inline ::perlin::Result<void> entity_render_override(const ::perlin::abi::EntityOverride& request) {
        return ::perlin::call<::perlin::abi::Op::EntityRenderOverride>(request);
    }
    inline ::perlin::Result<void> world_mesh(const ::perlin::abi::WorldMeshData& request) {
        return ::perlin::call<::perlin::abi::Op::WorldMesh>(request);
    }
    inline ::perlin::Result<void> world_mesh_clear(std::string id) {
        return ::perlin::call<::perlin::abi::Op::WorldMeshClear>(::perlin::abi::WorldMeshClearRequest{std::move(id)});
    }
    inline ::perlin::Result<void> frame_limit(const ::perlin::abi::FrameLimitData& request) {
        return ::perlin::call<::perlin::abi::Op::FrameLimit>(request);
    }
    inline ::perlin::Result<::perlin::abi::WindowInfo> window_state() {
        return ::perlin::call<::perlin::abi::Op::WindowState>();
    }
    inline ::perlin::Result<void> screen_button_add(const ::perlin::abi::ScreenButtonDef& request) {
        return ::perlin::call<::perlin::abi::Op::ScreenButtonAdd>(request);
    }
    inline ::perlin::Result<void> screen_button_remove(std::string id) {
        return ::perlin::call<::perlin::abi::Op::ScreenButtonRemove>(::perlin::abi::ScreenButtonRemoveRequest{std::move(id)});
    }
    inline ::perlin::Result<void> screen_open_custom(std::string id) {
        return ::perlin::call<::perlin::abi::Op::ScreenOpenCustom>(::perlin::abi::ScreenOpenCustomRequest{std::move(id)});
    }
    inline ::perlin::Result<void> tooltip_append(std::vector<std::string> lines) {
        return ::perlin::call<::perlin::abi::Op::TooltipAppend>(::perlin::abi::TooltipAppendRequest{std::move(lines)});
    }
    inline ::perlin::Result<void> loading_overlay(const ::perlin::abi::LoadingOverlayData& request) {
        return ::perlin::call<::perlin::abi::Op::LoadingOverlay>(request);
    }
    inline ::perlin::Result<void> skin_override(const ::perlin::abi::SkinOverrideData& request) {
        return ::perlin::call<::perlin::abi::Op::SkinOverride>(request);
    }
    inline ::perlin::Result<void> ambient_sound(const ::perlin::abi::AmbientSoundData& request) {
        return ::perlin::call<::perlin::abi::Op::AmbientSound>(request);
    }
    inline ::perlin::Result<uint32_t> server_ping(const ::perlin::abi::ServerPingRequest& request) {
        return ::perlin::call<::perlin::abi::Op::ServerPing>(request);
    }
    inline ::perlin::Result<void> mod_send(const ::perlin::abi::ModMessageData& request) {
        return ::perlin::call<::perlin::abi::Op::ModSend>(request);
    }
    inline ::perlin::Result<void> pack_register(const ::perlin::abi::PackRegistration& request) {
        return ::perlin::call<::perlin::abi::Op::PackRegister>(request);
    }
    inline ::perlin::Result<void> shader_pack_set(std::string name) {
        return ::perlin::call<::perlin::abi::Op::ShaderPackSet>(::perlin::abi::ShaderPackSetRequest{std::move(name)});
    }
    inline ::perlin::Result<void> particle_spawn(const ::perlin::abi::ParticleRequest& request) {
        return ::perlin::call<::perlin::abi::Op::ParticleSpawn>(request);
    }
    inline ::perlin::Result<void> key_register_ex(const ::perlin::abi::KeyBindEx& request) {
        return ::perlin::call<::perlin::abi::Op::KeyRegisterEx>(request);
    }
    inline ::perlin::Result<void> broadcast(const ::perlin::abi::TextMessage& request) {
        return ::perlin::call<::perlin::abi::Op::Broadcast>(request);
    }
    inline ::perlin::Result<void> player_message(const ::perlin::abi::PlayerText& request) {
        return ::perlin::call<::perlin::abi::Op::PlayerMessage>(request);
    }
    inline ::perlin::Result<void> kick(const ::perlin::abi::PlayerText& request) {
        return ::perlin::call<::perlin::abi::Op::Kick>(request);
    }
    inline ::perlin::Result<::perlin::abi::OnlinePlayerList> online_players() {
        return ::perlin::call<::perlin::abi::Op::OnlinePlayers>();
    }
    inline ::perlin::Result<void> teleport(const ::perlin::abi::ServerTeleport& request) {
        return ::perlin::call<::perlin::abi::Op::Teleport>(request);
    }
    inline ::perlin::Result<void> set_block(const ::perlin::abi::BlockWrite& request) {
        return ::perlin::call<::perlin::abi::Op::SetBlock>(request);
    }
    inline ::perlin::Result<::perlin::abi::TextMessage> block_name(const ::perlin::abi::BlockCoord& request) {
        return ::perlin::call<::perlin::abi::Op::BlockName>(request);
    }
    inline ::perlin::Result<::perlin::abi::ServerStatus> server_info() {
        return ::perlin::call<::perlin::abi::Op::ServerInfo>();
    }
    inline ::perlin::Result<::perlin::abi::GiveItemResult> give_item(const ::perlin::abi::GiveItemRequest& request) {
        return ::perlin::call<::perlin::abi::Op::GiveItem>(request);
    }
    inline ::perlin::Result<::perlin::abi::PlayerDetails> player_info(uint64_t player) {
        return ::perlin::call<::perlin::abi::Op::PlayerInfo>(::perlin::abi::PlayerInfoRequest{player});
    }
    inline ::perlin::Result<void> player_set_game_mode(uint64_t player, ::perlin::abi::GameMode mode) {
        return ::perlin::call<::perlin::abi::Op::PlayerSetGameMode>(::perlin::abi::PlayerSetGameModeRequest{player, mode});
    }
    inline ::perlin::Result<void> player_set_health(uint64_t player, float health) {
        return ::perlin::call<::perlin::abi::Op::PlayerSetHealth>(::perlin::abi::PlayerSetHealthRequest{player, health});
    }
    inline ::perlin::Result<bool> player_damage(uint64_t player, float amount, ::perlin::abi::DamageCause cause, bool knockback) {
        return ::perlin::call<::perlin::abi::Op::PlayerDamage>(::perlin::abi::PlayerDamageRequest{player, amount, cause, knockback});
    }
    inline ::perlin::Result<void> player_kill(uint64_t player) {
        return ::perlin::call<::perlin::abi::Op::PlayerKill>(::perlin::abi::PlayerKillRequest{player});
    }
    inline ::perlin::Result<void> player_set_food(uint64_t player, int32_t food, float saturation) {
        return ::perlin::call<::perlin::abi::Op::PlayerSetFood>(::perlin::abi::PlayerSetFoodRequest{player, food, saturation});
    }
    inline ::perlin::Result<void> player_add_experience(uint64_t player, int32_t points, int32_t levels) {
        return ::perlin::call<::perlin::abi::Op::PlayerAddExperience>(::perlin::abi::PlayerAddExperienceRequest{player, points, levels});
    }
    inline ::perlin::Result<void> player_effect_add(uint64_t player, ::perlin::abi::EffectId effect, int32_t amplifier, int32_t ticks, bool particles) {
        return ::perlin::call<::perlin::abi::Op::PlayerEffectAdd>(::perlin::abi::PlayerEffectAddRequest{player, effect, amplifier, ticks, particles});
    }
    inline ::perlin::Result<void> player_effect_remove(uint64_t player, int32_t effect) {
        return ::perlin::call<::perlin::abi::Op::PlayerEffectRemove>(::perlin::abi::PlayerEffectRemoveRequest{player, effect});
    }
    inline ::perlin::Result<::perlin::abi::ServerInventory> player_inventory(uint64_t player) {
        return ::perlin::call<::perlin::abi::Op::PlayerInventory>(::perlin::abi::PlayerInventoryRequest{player});
    }
    inline ::perlin::Result<void> player_set_slot(uint64_t player, int32_t container, int32_t slot, ::perlin::abi::ServerItem item) {
        return ::perlin::call<::perlin::abi::Op::PlayerSetSlot>(::perlin::abi::PlayerSetSlotRequest{player, container, slot, std::move(item)});
    }
    inline ::perlin::Result<void> player_clear_inventory(uint64_t player) {
        return ::perlin::call<::perlin::abi::Op::PlayerClearInventory>(::perlin::abi::PlayerClearInventoryRequest{player});
    }
    inline ::perlin::Result<void> player_set_velocity(uint64_t player, double x, double y, double z) {
        return ::perlin::call<::perlin::abi::Op::PlayerSetVelocity>(::perlin::abi::PlayerSetVelocityRequest{player, x, y, z});
    }
    inline ::perlin::Result<void> player_transfer_dimension(uint64_t player, ::perlin::abi::Dimension dimension, double x, double y, double z) {
        return ::perlin::call<::perlin::abi::Op::PlayerTransferDimension>(::perlin::abi::PlayerTransferDimensionRequest{player, dimension, x, y, z});
    }
    inline ::perlin::Result<void> player_title(uint64_t player, std::string title, std::string subtitle, int32_t fadeIn, int32_t stay, int32_t fadeOut) {
        return ::perlin::call<::perlin::abi::Op::PlayerTitle>(::perlin::abi::PlayerTitleRequest{player, std::move(title), std::move(subtitle), fadeIn, stay, fadeOut});
    }
    inline ::perlin::Result<void> player_action_bar(uint64_t player, std::string text) {
        return ::perlin::call<::perlin::abi::Op::PlayerActionBar>(::perlin::abi::PlayerActionBarRequest{player, std::move(text)});
    }
    inline ::perlin::Result<void> player_sound(uint64_t player, std::string sound, double x, double y, double z, float volume, float pitch) {
        return ::perlin::call<::perlin::abi::Op::PlayerSound>(::perlin::abi::PlayerSoundRequest{player, std::move(sound), x, y, z, volume, pitch});
    }
    inline ::perlin::Result<void> player_transfer(uint64_t player, std::string host, uint16_t port) {
        return ::perlin::call<::perlin::abi::Op::PlayerTransfer>(::perlin::abi::PlayerTransferRequest{player, std::move(host), port});
    }
    inline ::perlin::Result<void> player_set_operator(uint64_t player, bool isOperator) {
        return ::perlin::call<::perlin::abi::Op::PlayerSetOperator>(::perlin::abi::PlayerSetOperatorRequest{player, isOperator});
    }
    inline ::perlin::Result<uint32_t> player_form(uint64_t player, ::perlin::abi::Form form) {
        return ::perlin::call<::perlin::abi::Op::PlayerForm>(::perlin::abi::PlayerFormRequest{player, std::move(form)});
    }
    inline ::perlin::Result<void> player_run_command(uint64_t player, std::string line) {
        return ::perlin::call<::perlin::abi::Op::PlayerRunCommand>(::perlin::abi::PlayerRunCommandRequest{player, std::move(line)});
    }
    inline ::perlin::Result<void> player_set_spawn(uint64_t player, int32_t x, int32_t y, int32_t z) {
        return ::perlin::call<::perlin::abi::Op::PlayerSetSpawn>(::perlin::abi::PlayerSetSpawnRequest{player, x, y, z});
    }
    inline ::perlin::Result<uint64_t> player_by_name(std::string name) {
        return ::perlin::call<::perlin::abi::Op::PlayerByName>(::perlin::abi::PlayerByNameRequest{std::move(name)});
    }
    inline ::perlin::Result<::perlin::abi::BlockInfo> block_state(::perlin::abi::Dimension dimension, int32_t x, int32_t y, int32_t z) {
        return ::perlin::call<::perlin::abi::Op::BlockState>(::perlin::abi::BlockStateRequest{dimension, x, y, z});
    }
    inline ::perlin::Result<void> block_state_set(::perlin::abi::Dimension dimension, int32_t x, int32_t y, int32_t z, std::string name, std::vector<std::pair<std::string, std::string>> states) {
        return ::perlin::call<::perlin::abi::Op::BlockStateSet>(::perlin::abi::BlockStateSetRequest{dimension, x, y, z, std::move(name), std::move(states)});
    }
    inline ::perlin::Result<uint32_t> fill(::perlin::abi::Dimension dimension, int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1, std::string block) {
        return ::perlin::call<::perlin::abi::Op::Fill>(::perlin::abi::FillRequest{dimension, x0, y0, z0, x1, y1, z1, std::move(block)});
    }
    inline ::perlin::Result<::perlin::abi::TimeGetResponse> time_get() {
        return ::perlin::call<::perlin::abi::Op::TimeGet>();
    }
    inline ::perlin::Result<void> time_set(int64_t time) {
        return ::perlin::call<::perlin::abi::Op::TimeSet>(::perlin::abi::TimeSetRequest{time});
    }
    inline ::perlin::Result<void> weather_set(::perlin::abi::Weather weather, int32_t ticks) {
        return ::perlin::call<::perlin::abi::Op::WeatherSet>(::perlin::abi::WeatherSetRequest{weather, ticks});
    }
    inline ::perlin::Result<int32_t> game_rule_get(std::string name) {
        return ::perlin::call<::perlin::abi::Op::GameRuleGet>(::perlin::abi::GameRuleGetRequest{std::move(name)});
    }
    inline ::perlin::Result<void> game_rule_set(std::string name, int32_t value) {
        return ::perlin::call<::perlin::abi::Op::GameRuleSet>(::perlin::abi::GameRuleSetRequest{std::move(name), value});
    }
    inline ::perlin::Result<void> difficulty_set(::perlin::abi::Difficulty difficulty) {
        return ::perlin::call<::perlin::abi::Op::DifficultySet>(::perlin::abi::DifficultySetRequest{difficulty});
    }
    inline ::perlin::Result<void> explode(::perlin::abi::Dimension dimension, double x, double y, double z, float power, bool breakBlocks) {
        return ::perlin::call<::perlin::abi::Op::Explode>(::perlin::abi::ExplodeRequest{dimension, x, y, z, power, breakBlocks});
    }
    inline ::perlin::Result<int32_t> highest_block(::perlin::abi::Dimension dimension, int32_t x, int32_t z) {
        return ::perlin::call<::perlin::abi::Op::HighestBlock>(::perlin::abi::HighestBlockRequest{dimension, x, z});
    }
    inline ::perlin::Result<void> particle_at(::perlin::abi::Dimension dimension, std::string name, double x, double y, double z) {
        return ::perlin::call<::perlin::abi::Op::ParticleAt>(::perlin::abi::ParticleAtRequest{dimension, std::move(name), x, y, z});
    }
    inline ::perlin::Result<void> drop_item(::perlin::abi::Dimension dimension, ::perlin::abi::ServerItem item, double x, double y, double z) {
        return ::perlin::call<::perlin::abi::Op::DropItem>(::perlin::abi::DropItemRequest{dimension, std::move(item), x, y, z});
    }
    inline ::perlin::Result<::perlin::abi::BlockCoord> world_spawn_get() {
        return ::perlin::call<::perlin::abi::Op::WorldSpawnGet>();
    }
    inline ::perlin::Result<void> world_spawn_set(const ::perlin::abi::BlockCoord& request) {
        return ::perlin::call<::perlin::abi::Op::WorldSpawnSet>(request);
    }
    inline ::perlin::Result<uint64_t> entity_spawn(std::string type, double x, double y, double z, bool baby) {
        return ::perlin::call<::perlin::abi::Op::EntitySpawn>(::perlin::abi::EntitySpawnRequest{std::move(type), x, y, z, baby});
    }
    inline ::perlin::Result<::perlin::abi::EntityListResponse> entity_list(double x, double y, double z, double radius, std::string type) {
        return ::perlin::call<::perlin::abi::Op::EntityList>(::perlin::abi::EntityListRequest{x, y, z, radius, std::move(type)});
    }
    inline ::perlin::Result<void> entity_remove(uint64_t entity) {
        return ::perlin::call<::perlin::abi::Op::EntityRemove>(::perlin::abi::EntityRemoveRequest{entity});
    }
    inline ::perlin::Result<bool> entity_damage(uint64_t entity, float amount) {
        return ::perlin::call<::perlin::abi::Op::EntityDamage>(::perlin::abi::EntityDamageRequest{entity, amount});
    }
    inline ::perlin::Result<void> entity_teleport(uint64_t entity, double x, double y, double z) {
        return ::perlin::call<::perlin::abi::Op::EntityTeleport>(::perlin::abi::EntityTeleportRequest{entity, x, y, z});
    }
    inline ::perlin::Result<void> entity_set_velocity(uint64_t entity, double x, double y, double z) {
        return ::perlin::call<::perlin::abi::Op::EntitySetVelocity>(::perlin::abi::EntitySetVelocityRequest{entity, x, y, z});
    }
    inline ::perlin::Result<::perlin::abi::ServerEntity> entity_info(uint64_t entity) {
        return ::perlin::call<::perlin::abi::Op::EntityInfo>(::perlin::abi::EntityInfoRequest{entity});
    }
    inline ::perlin::Result<void> entity_set_health(uint64_t entity, float health) {
        return ::perlin::call<::perlin::abi::Op::EntitySetHealth>(::perlin::abi::EntitySetHealthRequest{entity, health});
    }
    inline ::perlin::Result<::perlin::abi::RestText> run_command(std::string line) {
        return ::perlin::call<::perlin::abi::Op::RunCommand>(::perlin::abi::RunCommandRequest{std::move(line)});
    }
    inline ::perlin::Result<void> motd_set(std::string motd) {
        return ::perlin::call<::perlin::abi::Op::MotdSet>(::perlin::abi::MotdSetRequest{std::move(motd)});
    }
    inline ::perlin::Result<void> loot_table_override(std::string key, std::string json) {
        return ::perlin::call<::perlin::abi::Op::LootTableOverride>(::perlin::abi::LootTableOverrideRequest{std::move(key), std::move(json)});
    }
    inline ::perlin::Result<::perlin::abi::RestText> loot_table_get(std::string key) {
        return ::perlin::call<::perlin::abi::Op::LootTableGet>(::perlin::abi::LootTableGetRequest{std::move(key)});
    }
    inline ::perlin::Result<void> event_amend(std::string field, double number, std::string text) {
        return ::perlin::call<::perlin::abi::Op::EventAmend>(::perlin::abi::EventAmendRequest{std::move(field), number, std::move(text)});
    }
}
