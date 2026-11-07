#pragma once
#include <array>
#include <cstdint>
#include <string_view>

#include "perlin/gen/abi_types.hpp"

namespace perlin::abi {
    enum class Op : uint32_t {
        Log = 0x0001,
        Meta = 0x0002,
        Clock = 0x0003,
        PlayerState = 0x0100,
        PlayerEffects = 0x0101,
        PlayerLook = 0x0102,
        Inventory = 0x0200,
        InventorySelect = 0x0201,
        InventoryDrop = 0x0202,
        InventorySwap = 0x0203,
        InventoryClick = 0x0204,
        InventoryOpen = 0x0205,
        InventoryClose = 0x0206,
        WorldState = 0x0300,
        Block = 0x0301,
        Region = 0x0302,
        Raycast = 0x0303,
        Aim = 0x0304,
        FindBlocks = 0x0305,
        ChunkLoaded = 0x0306,
        BlockNames = 0x0307,
        BlockProps = 0x0308,
        BlockEntityData = 0x0309,
        DisplayName = 0x030A,
        BreakProgress = 0x030B,
        MapChunk = 0x030C,
        Entities = 0x0400,
        EntityById = 0x0401,
        PlayerList = 0x0402,
        Options = 0x0500,
        OptionGet = 0x0501,
        OptionSet = 0x0502,
        Screen = 0x0503,
        Translate = 0x0504,
        ReloadResources = 0x0505,
        PerspectiveGet = 0x0506,
        PerspectiveSet = 0x0507,
        ResourcePacks = 0x0508,
        InputDown = 0x0600,
        InputOverride = 0x0601,
        KeyRegister = 0x0602,
        InputActions = 0x0603,
        MousePosition = 0x0604,
        Chat = 0x0700,
        Command = 0x0701,
        LocalMessage = 0x0702,
        CommandRegister = 0x0703,
        CommandUnregister = 0x0704,
        ServerCommands = 0x0705,
        Toast = 0x0706,
        Title = 0x0707,
        ChatHistory = 0x0708,
        TitleState = 0x0709,
        SwingArm = 0x0800,
        UseItem = 0x0801,
        UseOnBlock = 0x0802,
        AttackEntity = 0x0803,
        InteractEntity = 0x0804,
        BreakBlock = 0x0805,
        Look = 0x0806,
        Respawn = 0x0807,
        Disconnect = 0x0808,
        PlaySound = 0x0809,
        Jump = 0x080B,
        UiSubmit = 0x0900,
        UiClear = 0x0901,
        UiMeasure = 0x0902,
        GfxTextureCreate = 0x0910,
        GfxTextureUpdate = 0x0911,
        GfxTextureDestroy = 0x0912,
        GfxSubmit = 0x0913,
        GfxClear = 0x0914,
        GfxDisplay = 0x0915,
        GfxImageLoad = 0x0916,
        InputGrab = 0x0920,
        NativeInfo = 0x0930,
        StorageRead = 0x0A00,
        StorageWrite = 0x0A01,
        StorageList = 0x0A02,
        StorageRemove = 0x0A03,
        StorageExists = 0x0A04,
        StorageMakeDir = 0x0A05,
        HttpRequest = 0x0B00,
        PacketSend = 0x0B01,
        PacketName = 0x0B02,
        SettingGet = 0x0C01,
        SettingSet = 0x0C02,
        SettingList = 0x0C03,
        ModList = 0x0C10,
        IconSet = 0x0C11,
        OpenScreen = 0x0D00,
        OpenModSettings = 0x0D01,
        TakeScreenshot = 0x0D02,
        CyclePerspective = 0x0D03,
        CopyToClipboard = 0x0D04,
        OpenUrl = 0x0D05,
        ToggleHud = 0x0D06,
        CopyFile = 0x0E00,
        MoveFile = 0x0E01,
        DownloadFile = 0x0E02,
        CameraControl = 0x0F00,
        CameraState = 0x0F01,
        WorldToScreen = 0x0F02,
        PlayerMotion = 0x0F10,
        PlayerTeleport = 0x0F11,
        PlayerModelPose = 0x0F20,
        PlayerSetLook = 0x0F21,
        WorldLines = 0x0F30,
        WorldBoxes = 0x0F31,
        ClientBlockSet = 0x0F40,
        ClientBlockGet = 0x0F41,
        PacketDrop = 0x0F50,
        PacketCounters = 0x0F51,
        PacketInject = 0x0F52,
        HudElementSet = 0x1000,
        DebugLines = 0x1001,
        FogSet = 0x1002,
        ScreenTint = 0x1003,
        EntityRenderOverride = 0x1010,
        WorldMesh = 0x1011,
        WorldMeshClear = 0x1012,
        FrameLimit = 0x1020,
        WindowState = 0x1021,
        ScreenButtonAdd = 0x1030,
        ScreenButtonRemove = 0x1031,
        ScreenOpenCustom = 0x1032,
        TooltipAppend = 0x1033,
        LoadingOverlay = 0x1040,
        SkinOverride = 0x1041,
        AmbientSound = 0x1050,
        ServerPing = 0x1060,
        ModSend = 0x1070,
        PackRegister = 0x1080,
        ShaderPackSet = 0x1081,
        ParticleSpawn = 0x1090,
        KeyRegisterEx = 0x10A0,
        Broadcast = 0x7000,
        PlayerMessage = 0x7001,
        Kick = 0x7002,
        OnlinePlayers = 0x7003,
        Teleport = 0x7004,
        SetBlock = 0x7005,
        BlockName = 0x7006,
        ServerInfo = 0x7007,
        GiveItem = 0x7008,
        PlayerInfo = 0x7010,
        PlayerSetGameMode = 0x7011,
        PlayerSetHealth = 0x7012,
        PlayerDamage = 0x7013,
        PlayerKill = 0x7014,
        PlayerSetFood = 0x7015,
        PlayerAddExperience = 0x7016,
        PlayerEffectAdd = 0x7017,
        PlayerEffectRemove = 0x7018,
        PlayerInventory = 0x7019,
        PlayerSetSlot = 0x701A,
        PlayerClearInventory = 0x701B,
        PlayerSetVelocity = 0x701C,
        PlayerTransferDimension = 0x701D,
        PlayerTitle = 0x701E,
        PlayerActionBar = 0x701F,
        PlayerSound = 0x7020,
        PlayerTransfer = 0x7021,
        PlayerSetOperator = 0x7022,
        PlayerForm = 0x7023,
        PlayerRunCommand = 0x7024,
        PlayerSetSpawn = 0x7025,
        PlayerByName = 0x7026,
        BlockState = 0x7040,
        BlockStateSet = 0x7041,
        Fill = 0x7042,
        TimeGet = 0x7043,
        TimeSet = 0x7044,
        WeatherSet = 0x7045,
        GameRuleGet = 0x7046,
        GameRuleSet = 0x7047,
        DifficultySet = 0x7048,
        Explode = 0x7049,
        HighestBlock = 0x704A,
        ParticleAt = 0x704B,
        DropItem = 0x704C,
        WorldSpawnGet = 0x704D,
        WorldSpawnSet = 0x704E,
        EntitySpawn = 0x7060,
        EntityList = 0x7061,
        EntityRemove = 0x7062,
        EntityDamage = 0x7063,
        EntityTeleport = 0x7064,
        EntitySetVelocity = 0x7065,
        EntityInfo = 0x7066,
        EntitySetHealth = 0x7067,
        RunCommand = 0x7080,
        MotdSet = 0x7081,
        LootTableOverride = 0x7082,
        LootTableGet = 0x7083,
        EventAmend = 0x7084,
    };

    enum class Event : uint32_t {
        Load = 1,
        Unload = 2,
        Tick = 3,
        Frame = 4,
        ChatReceived = 5,
        ChatSending = 6,
        Connected = 7,
        Disconnected = 8,
        Key = 9,
        Command = 10,
        Damage = 11,
        Death = 12,
        Respawned = 13,
        DimensionChanged = 14,
        ScreenChanged = 15,
        SlotChanged = 16,
        EntityAdded = 17,
        EntityRemoved = 18,
        Sound = 19,
        HttpResponse = 20,
        Packet = 21,
        Title = 22,
        SettingChanged = 23,
        Look = 24,
        MouseMove = 25,
        MouseButton = 26,
        MouseWheel = 27,
        KeyRaw = 28,
        TextInput = 29,
        ScreenRender = 30,
        SlotClick = 31,
        Interact = 32,
        ChunkLoaded = 33,
        ChunkUnloaded = 34,
        BlockChanged = 35,
        ResourcesReloaded = 36,
        LevelLoaded = 37,
        LevelUnloaded = 38,
        ModsLoaded = 39,
        ClientStopping = 40,
        Toast = 41,
        ModMessage = 42,
        Tooltip = 43,
        ScreenButton = 44,
        WindowFocus = 45,
        ServerPing = 46,
        PlayerJoin = 47,
        PlayerQuit = 48,
        PlayerChat = 49,
        BlockBreak = 50,
        BlockPlace = 51,
        PlayerDamage = 52,
        PlayerDeath = 53,
        PlayerRespawn = 54,
        PlayerUseBlock = 55,
        PlayerAttack = 56,
        EntityDamage = 57,
        EntityDeath = 58,
        EntitySpawn = 59,
        ItemConsume = 60,
        GameModeChange = 61,
        FormResponse = 62,
        PlayerMove = 63,
        Explosion = 64,
        ItemDrop = 65,
        ItemPickup = 66,
        ContainerOpen = 67,
        DimensionChange = 68,
        ServerCommand = 69,
        PreLogin = 70,
    };

    enum class Hosts : uint8_t { Client = 1, Server = 2, Both = 3 };
    struct Raw {};
    using Nothing = EventNothing;

    inline constexpr uint32_t kEventCount = 71;
    inline constexpr uint32_t kSubscriptionWords = (kEventCount + 31) / 32;

    template <Op O> struct OpSpec;
    template <> struct OpSpec<Op::Log> {
        using Request = LogRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Log";
        static constexpr std::string_view kCategory = "core";
    };
    template <> struct OpSpec<Op::Meta> {
        using Request = Nothing;
        using Response = Meta;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Meta";
        static constexpr std::string_view kCategory = "core";
    };
    template <> struct OpSpec<Op::Clock> {
        using Request = Nothing;
        using Response = int64_t;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Clock";
        static constexpr std::string_view kCategory = "core";
    };
    template <> struct OpSpec<Op::PlayerState> {
        using Request = Nothing;
        using Response = PlayerState;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = true;
        static constexpr std::string_view kName = "PlayerState";
        static constexpr std::string_view kCategory = "player";
    };
    template <> struct OpSpec<Op::PlayerEffects> {
        using Request = Nothing;
        using Response = PlayerEffectsResponse;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerEffects";
        static constexpr std::string_view kCategory = "player";
    };
    template <> struct OpSpec<Op::PlayerLook> {
        using Request = Nothing;
        using Response = PlayerLookResponse;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerLook";
        static constexpr std::string_view kCategory = "player";
    };
    template <> struct OpSpec<Op::Inventory> {
        using Request = Nothing;
        using Response = Inventory;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Inventory";
        static constexpr std::string_view kCategory = "inventory";
    };
    template <> struct OpSpec<Op::InventorySelect> {
        using Request = InventorySelectRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "InventorySelect";
        static constexpr std::string_view kCategory = "inventory";
    };
    template <> struct OpSpec<Op::InventoryDrop> {
        using Request = InventoryDropRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "InventoryDrop";
        static constexpr std::string_view kCategory = "inventory";
    };
    template <> struct OpSpec<Op::InventorySwap> {
        using Request = InventorySwapRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "InventorySwap";
        static constexpr std::string_view kCategory = "inventory";
    };
    template <> struct OpSpec<Op::InventoryClick> {
        using Request = InventoryClickRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "InventoryClick";
        static constexpr std::string_view kCategory = "inventory";
    };
    template <> struct OpSpec<Op::InventoryOpen> {
        using Request = Nothing;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "InventoryOpen";
        static constexpr std::string_view kCategory = "inventory";
    };
    template <> struct OpSpec<Op::InventoryClose> {
        using Request = Nothing;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "InventoryClose";
        static constexpr std::string_view kCategory = "inventory";
    };
    template <> struct OpSpec<Op::WorldState> {
        using Request = Nothing;
        using Response = WorldState;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "WorldState";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::Block> {
        using Request = BlockRequest;
        using Response = BlockInfo;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = true;
        static constexpr std::string_view kName = "Block";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::Region> {
        using Request = RegionRequest;
        using Response = Region;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Region";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::Raycast> {
        using Request = RaycastRequest;
        using Response = RayHit;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Raycast";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::Aim> {
        using Request = Nothing;
        using Response = AimState;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Aim";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::FindBlocks> {
        using Request = FindBlocksRequest;
        using Response = FindBlocksResponse;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "FindBlocks";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::ChunkLoaded> {
        using Request = ChunkLoadedRequest;
        using Response = bool;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ChunkLoaded";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::BlockNames> {
        using Request = Nothing;
        using Response = BlockNamesResponse;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "BlockNames";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::BlockProps> {
        using Request = BlockPropsRequest;
        using Response = BlockProps;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "BlockProps";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::BlockEntityData> {
        using Request = BlockEntityDataRequest;
        using Response = BlockEntityInfo;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "BlockEntityData";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::DisplayName> {
        using Request = DisplayNameRequest;
        using Response = RestText;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "DisplayName";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::BreakProgress> {
        using Request = Nothing;
        using Response = BreakState;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "BreakProgress";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::MapChunk> {
        using Request = MapChunkRequest;
        using Response = RestBytes;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "MapChunk";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::Entities> {
        using Request = EntitiesRequest;
        using Response = EntitiesResponse;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Entities";
        static constexpr std::string_view kCategory = "entity";
    };
    template <> struct OpSpec<Op::EntityById> {
        using Request = EntityByIdRequest;
        using Response = Entity;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "EntityById";
        static constexpr std::string_view kCategory = "entity";
    };
    template <> struct OpSpec<Op::PlayerList> {
        using Request = Nothing;
        using Response = PlayerListResponse;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerList";
        static constexpr std::string_view kCategory = "entity";
    };
    template <> struct OpSpec<Op::Options> {
        using Request = Nothing;
        using Response = RestText;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Options";
        static constexpr std::string_view kCategory = "options";
    };
    template <> struct OpSpec<Op::OptionGet> {
        using Request = OptionGetRequest;
        using Response = RestText;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "OptionGet";
        static constexpr std::string_view kCategory = "options";
    };
    template <> struct OpSpec<Op::OptionSet> {
        using Request = OptionSetRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "OptionSet";
        static constexpr std::string_view kCategory = "options";
    };
    template <> struct OpSpec<Op::Screen> {
        using Request = Nothing;
        using Response = ScreenState;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Screen";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::Translate> {
        using Request = TranslateRequest;
        using Response = RestText;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Translate";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::ReloadResources> {
        using Request = Nothing;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ReloadResources";
        static constexpr std::string_view kCategory = "resources";
    };
    template <> struct OpSpec<Op::PerspectiveGet> {
        using Request = Nothing;
        using Response = Perspective;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PerspectiveGet";
        static constexpr std::string_view kCategory = "camera";
    };
    template <> struct OpSpec<Op::PerspectiveSet> {
        using Request = PerspectiveSetRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PerspectiveSet";
        static constexpr std::string_view kCategory = "camera";
    };
    template <> struct OpSpec<Op::ResourcePacks> {
        using Request = Nothing;
        using Response = ResourcePacksResponse;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ResourcePacks";
        static constexpr std::string_view kCategory = "resources";
    };
    template <> struct OpSpec<Op::InputDown> {
        using Request = InputDownRequest;
        using Response = bool;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "InputDown";
        static constexpr std::string_view kCategory = "input";
    };
    template <> struct OpSpec<Op::InputOverride> {
        using Request = InputOverrideState;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "InputOverride";
        static constexpr std::string_view kCategory = "input";
    };
    template <> struct OpSpec<Op::KeyRegister> {
        using Request = KeyRegisterRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "KeyRegister";
        static constexpr std::string_view kCategory = "input";
    };
    template <> struct OpSpec<Op::InputActions> {
        using Request = Nothing;
        using Response = InputActionsResponse;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "InputActions";
        static constexpr std::string_view kCategory = "input";
    };
    template <> struct OpSpec<Op::MousePosition> {
        using Request = Nothing;
        using Response = MousePositionResponse;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "MousePosition";
        static constexpr std::string_view kCategory = "input";
    };
    template <> struct OpSpec<Op::Chat> {
        using Request = ChatRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Chat";
        static constexpr std::string_view kCategory = "chat";
    };
    template <> struct OpSpec<Op::Command> {
        using Request = CommandRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Command";
        static constexpr std::string_view kCategory = "chat";
    };
    template <> struct OpSpec<Op::LocalMessage> {
        using Request = LocalMessageRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "LocalMessage";
        static constexpr std::string_view kCategory = "chat";
    };
    template <> struct OpSpec<Op::CommandRegister> {
        using Request = CommandRegisterRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "CommandRegister";
        static constexpr std::string_view kCategory = "chat";
    };
    template <> struct OpSpec<Op::CommandUnregister> {
        using Request = CommandUnregisterRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "CommandUnregister";
        static constexpr std::string_view kCategory = "chat";
    };
    template <> struct OpSpec<Op::ServerCommands> {
        using Request = Nothing;
        using Response = ServerCommandsResponse;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ServerCommands";
        static constexpr std::string_view kCategory = "chat";
    };
    template <> struct OpSpec<Op::Toast> {
        using Request = ToastRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Toast";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::Title> {
        using Request = TitleRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Title";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::ChatHistory> {
        using Request = ChatHistoryRequest;
        using Response = ChatHistoryResponse;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ChatHistory";
        static constexpr std::string_view kCategory = "chat";
    };
    template <> struct OpSpec<Op::TitleState> {
        using Request = Nothing;
        using Response = TitleStateData;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "TitleState";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::SwingArm> {
        using Request = Nothing;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "SwingArm";
        static constexpr std::string_view kCategory = "action";
    };
    template <> struct OpSpec<Op::UseItem> {
        using Request = Nothing;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "UseItem";
        static constexpr std::string_view kCategory = "action";
    };
    template <> struct OpSpec<Op::UseOnBlock> {
        using Request = UseOnBlockRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "UseOnBlock";
        static constexpr std::string_view kCategory = "action";
    };
    template <> struct OpSpec<Op::AttackEntity> {
        using Request = AttackEntityRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "AttackEntity";
        static constexpr std::string_view kCategory = "action";
    };
    template <> struct OpSpec<Op::InteractEntity> {
        using Request = InteractEntityRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "InteractEntity";
        static constexpr std::string_view kCategory = "action";
    };
    template <> struct OpSpec<Op::BreakBlock> {
        using Request = BreakBlockRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "BreakBlock";
        static constexpr std::string_view kCategory = "action";
    };
    template <> struct OpSpec<Op::Look> {
        using Request = LookRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Look";
        static constexpr std::string_view kCategory = "action";
    };
    template <> struct OpSpec<Op::Respawn> {
        using Request = Nothing;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Respawn";
        static constexpr std::string_view kCategory = "action";
    };
    template <> struct OpSpec<Op::Disconnect> {
        using Request = Nothing;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Disconnect";
        static constexpr std::string_view kCategory = "action";
    };
    template <> struct OpSpec<Op::PlaySound> {
        using Request = PlaySoundRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlaySound";
        static constexpr std::string_view kCategory = "audio";
    };
    template <> struct OpSpec<Op::Jump> {
        using Request = Nothing;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Jump";
        static constexpr std::string_view kCategory = "action";
    };
    template <> struct OpSpec<Op::UiSubmit> {
        using Request = UiSubmitRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = true;
        static constexpr std::string_view kName = "UiSubmit";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::UiClear> {
        using Request = UiClearRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "UiClear";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::UiMeasure> {
        using Request = UiMeasureRequest;
        using Response = float;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "UiMeasure";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::GfxTextureCreate> {
        using Request = GfxTextureCreateRequest;
        using Response = uint32_t;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "GfxTextureCreate";
        static constexpr std::string_view kCategory = "gfx";
    };
    template <> struct OpSpec<Op::GfxTextureUpdate> {
        using Request = GfxTextureUpdateRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = true;
        static constexpr std::string_view kName = "GfxTextureUpdate";
        static constexpr std::string_view kCategory = "gfx";
    };
    template <> struct OpSpec<Op::GfxTextureDestroy> {
        using Request = GfxTextureDestroyRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "GfxTextureDestroy";
        static constexpr std::string_view kCategory = "gfx";
    };
    template <> struct OpSpec<Op::GfxSubmit> {
        using Request = GfxSubmitRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = true;
        static constexpr std::string_view kName = "GfxSubmit";
        static constexpr std::string_view kCategory = "gfx";
    };
    template <> struct OpSpec<Op::GfxClear> {
        using Request = GfxClearRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "GfxClear";
        static constexpr std::string_view kCategory = "gfx";
    };
    template <> struct OpSpec<Op::GfxDisplay> {
        using Request = Nothing;
        using Response = GfxDisplayInfo;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "GfxDisplay";
        static constexpr std::string_view kCategory = "gfx";
    };
    template <> struct OpSpec<Op::GfxImageLoad> {
        using Request = GfxImageLoadRequest;
        using Response = GfxImageLoadResponse;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "GfxImageLoad";
        static constexpr std::string_view kCategory = "gfx";
    };
    template <> struct OpSpec<Op::InputGrab> {
        using Request = InputGrabRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "InputGrab";
        static constexpr std::string_view kCategory = "input";
    };
    template <> struct OpSpec<Op::NativeInfo> {
        using Request = Nothing;
        using Response = NativeHandles;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Native);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "NativeInfo";
        static constexpr std::string_view kCategory = "gfx";
    };
    template <> struct OpSpec<Op::StorageRead> {
        using Request = StorageReadRequest;
        using Response = RestBytes;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Storage);
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "StorageRead";
        static constexpr std::string_view kCategory = "storage";
    };
    template <> struct OpSpec<Op::StorageWrite> {
        using Request = StorageWriteRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Storage);
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "StorageWrite";
        static constexpr std::string_view kCategory = "storage";
    };
    template <> struct OpSpec<Op::StorageList> {
        using Request = StorageListRequest;
        using Response = StorageListResponse;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Storage);
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "StorageList";
        static constexpr std::string_view kCategory = "storage";
    };
    template <> struct OpSpec<Op::StorageRemove> {
        using Request = StorageRemoveRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Storage);
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "StorageRemove";
        static constexpr std::string_view kCategory = "storage";
    };
    template <> struct OpSpec<Op::StorageExists> {
        using Request = StorageExistsRequest;
        using Response = bool;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Storage);
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "StorageExists";
        static constexpr std::string_view kCategory = "storage";
    };
    template <> struct OpSpec<Op::StorageMakeDir> {
        using Request = StorageMakeDirRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Storage);
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "StorageMakeDir";
        static constexpr std::string_view kCategory = "storage";
    };
    template <> struct OpSpec<Op::HttpRequest> {
        using Request = HttpRequestRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Network);
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "HttpRequest";
        static constexpr std::string_view kCategory = "network";
    };
    template <> struct OpSpec<Op::PacketSend> {
        using Request = PacketSendRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Packets);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PacketSend";
        static constexpr std::string_view kCategory = "network";
    };
    template <> struct OpSpec<Op::PacketName> {
        using Request = PacketNameRequest;
        using Response = RestText;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PacketName";
        static constexpr std::string_view kCategory = "network";
    };
    template <> struct OpSpec<Op::SettingGet> {
        using Request = SettingGetRequest;
        using Response = RestText;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "SettingGet";
        static constexpr std::string_view kCategory = "settings";
    };
    template <> struct OpSpec<Op::SettingSet> {
        using Request = SettingSetRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "SettingSet";
        static constexpr std::string_view kCategory = "settings";
    };
    template <> struct OpSpec<Op::SettingList> {
        using Request = Nothing;
        using Response = SettingListResponse;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "SettingList";
        static constexpr std::string_view kCategory = "settings";
    };
    template <> struct OpSpec<Op::ModList> {
        using Request = Nothing;
        using Response = ModListResponse;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ModList";
        static constexpr std::string_view kCategory = "mods";
    };
    template <> struct OpSpec<Op::IconSet> {
        using Request = IconSetRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "IconSet";
        static constexpr std::string_view kCategory = "mods";
    };
    template <> struct OpSpec<Op::OpenScreen> {
        using Request = OpenScreenRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "OpenScreen";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::OpenModSettings> {
        using Request = OpenModSettingsRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "OpenModSettings";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::TakeScreenshot> {
        using Request = Nothing;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "TakeScreenshot";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::CyclePerspective> {
        using Request = Nothing;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "CyclePerspective";
        static constexpr std::string_view kCategory = "camera";
    };
    template <> struct OpSpec<Op::CopyToClipboard> {
        using Request = CopyToClipboardRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "CopyToClipboard";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::OpenUrl> {
        using Request = OpenUrlRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Network);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "OpenUrl";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::ToggleHud> {
        using Request = Nothing;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ToggleHud";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::CopyFile> {
        using Request = CopyFileRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Files);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "CopyFile";
        static constexpr std::string_view kCategory = "storage";
    };
    template <> struct OpSpec<Op::MoveFile> {
        using Request = MoveFileRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Files);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "MoveFile";
        static constexpr std::string_view kCategory = "storage";
    };
    template <> struct OpSpec<Op::DownloadFile> {
        using Request = DownloadFileRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Network) | static_cast<uint32_t>(Permission::Files);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "DownloadFile";
        static constexpr std::string_view kCategory = "storage";
    };
    template <> struct OpSpec<Op::CameraControl> {
        using Request = CameraControlState;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "CameraControl";
        static constexpr std::string_view kCategory = "camera";
    };
    template <> struct OpSpec<Op::CameraState> {
        using Request = Nothing;
        using Response = CameraInfo;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "CameraState";
        static constexpr std::string_view kCategory = "camera";
    };
    template <> struct OpSpec<Op::WorldToScreen> {
        using Request = WorldPoint;
        using Response = ScreenPoint;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "WorldToScreen";
        static constexpr std::string_view kCategory = "camera";
    };
    template <> struct OpSpec<Op::PlayerMotion> {
        using Request = MotionRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerMotion";
        static constexpr std::string_view kCategory = "player";
    };
    template <> struct OpSpec<Op::PlayerTeleport> {
        using Request = TeleportRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerTeleport";
        static constexpr std::string_view kCategory = "player";
    };
    template <> struct OpSpec<Op::PlayerModelPose> {
        using Request = ModelPose;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerModelPose";
        static constexpr std::string_view kCategory = "player";
    };
    template <> struct OpSpec<Op::PlayerSetLook> {
        using Request = LookSet;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerSetLook";
        static constexpr std::string_view kCategory = "player";
    };
    template <> struct OpSpec<Op::WorldLines> {
        using Request = WorldLines;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "WorldLines";
        static constexpr std::string_view kCategory = "render";
    };
    template <> struct OpSpec<Op::WorldBoxes> {
        using Request = WorldBoxes;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "WorldBoxes";
        static constexpr std::string_view kCategory = "render";
    };
    template <> struct OpSpec<Op::ClientBlockSet> {
        using Request = ClientBlockEdits;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ClientBlockSet";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::ClientBlockGet> {
        using Request = BlockQuery;
        using Response = BlockQueryResult;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ClientBlockGet";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::PacketDrop> {
        using Request = PacketDropRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Packets) | static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PacketDrop";
        static constexpr std::string_view kCategory = "network";
    };
    template <> struct OpSpec<Op::PacketCounters> {
        using Request = Nothing;
        using Response = PacketCountersResponse;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PacketCounters";
        static constexpr std::string_view kCategory = "network";
    };
    template <> struct OpSpec<Op::PacketInject> {
        using Request = PacketInjectRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Packets) | static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PacketInject";
        static constexpr std::string_view kCategory = "network";
    };
    template <> struct OpSpec<Op::HudElementSet> {
        using Request = HudElementState;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "HudElementSet";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::DebugLines> {
        using Request = DebugLinesData;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "DebugLines";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::FogSet> {
        using Request = FogOverride;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "FogSet";
        static constexpr std::string_view kCategory = "render";
    };
    template <> struct OpSpec<Op::ScreenTint> {
        using Request = ScreenTintData;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ScreenTint";
        static constexpr std::string_view kCategory = "render";
    };
    template <> struct OpSpec<Op::EntityRenderOverride> {
        using Request = EntityOverride;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "EntityRenderOverride";
        static constexpr std::string_view kCategory = "render";
    };
    template <> struct OpSpec<Op::WorldMesh> {
        using Request = WorldMeshData;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "WorldMesh";
        static constexpr std::string_view kCategory = "render";
    };
    template <> struct OpSpec<Op::WorldMeshClear> {
        using Request = WorldMeshClearRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "WorldMeshClear";
        static constexpr std::string_view kCategory = "render";
    };
    template <> struct OpSpec<Op::FrameLimit> {
        using Request = FrameLimitData;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "FrameLimit";
        static constexpr std::string_view kCategory = "render";
    };
    template <> struct OpSpec<Op::WindowState> {
        using Request = Nothing;
        using Response = WindowInfo;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "WindowState";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::ScreenButtonAdd> {
        using Request = ScreenButtonDef;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ScreenButtonAdd";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::ScreenButtonRemove> {
        using Request = ScreenButtonRemoveRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ScreenButtonRemove";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::ScreenOpenCustom> {
        using Request = ScreenOpenCustomRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ScreenOpenCustom";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::TooltipAppend> {
        using Request = TooltipAppendRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "TooltipAppend";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::LoadingOverlay> {
        using Request = LoadingOverlayData;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "LoadingOverlay";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::SkinOverride> {
        using Request = SkinOverrideData;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "SkinOverride";
        static constexpr std::string_view kCategory = "render";
    };
    template <> struct OpSpec<Op::AmbientSound> {
        using Request = AmbientSoundData;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "AmbientSound";
        static constexpr std::string_view kCategory = "audio";
    };
    template <> struct OpSpec<Op::ServerPing> {
        using Request = ServerPingRequest;
        using Response = uint32_t;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Network);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ServerPing";
        static constexpr std::string_view kCategory = "network";
    };
    template <> struct OpSpec<Op::ModSend> {
        using Request = ModMessageData;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ModSend";
        static constexpr std::string_view kCategory = "mods";
    };
    template <> struct OpSpec<Op::PackRegister> {
        using Request = PackRegistration;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Files);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PackRegister";
        static constexpr std::string_view kCategory = "resources";
    };
    template <> struct OpSpec<Op::ShaderPackSet> {
        using Request = ShaderPackSetRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Control);
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ShaderPackSet";
        static constexpr std::string_view kCategory = "resources";
    };
    template <> struct OpSpec<Op::ParticleSpawn> {
        using Request = ParticleRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ParticleSpawn";
        static constexpr std::string_view kCategory = "render";
    };
    template <> struct OpSpec<Op::KeyRegisterEx> {
        using Request = KeyBindEx;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "KeyRegisterEx";
        static constexpr std::string_view kCategory = "input";
    };
    template <> struct OpSpec<Op::Broadcast> {
        using Request = TextMessage;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Broadcast";
        static constexpr std::string_view kCategory = "server";
    };
    template <> struct OpSpec<Op::PlayerMessage> {
        using Request = PlayerText;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerMessage";
        static constexpr std::string_view kCategory = "server";
    };
    template <> struct OpSpec<Op::Kick> {
        using Request = PlayerText;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Admin);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Kick";
        static constexpr std::string_view kCategory = "server";
    };
    template <> struct OpSpec<Op::OnlinePlayers> {
        using Request = Nothing;
        using Response = OnlinePlayerList;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "OnlinePlayers";
        static constexpr std::string_view kCategory = "server";
    };
    template <> struct OpSpec<Op::Teleport> {
        using Request = ServerTeleport;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Players);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Teleport";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::SetBlock> {
        using Request = BlockWrite;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::World);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "SetBlock";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::BlockName> {
        using Request = BlockCoord;
        using Response = TextMessage;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "BlockName";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::ServerInfo> {
        using Request = Nothing;
        using Response = ServerStatus;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ServerInfo";
        static constexpr std::string_view kCategory = "server";
    };
    template <> struct OpSpec<Op::GiveItem> {
        using Request = GiveItemRequest;
        using Response = GiveItemResult;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Players);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 1;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "GiveItem";
        static constexpr std::string_view kCategory = "inventory";
    };
    template <> struct OpSpec<Op::PlayerInfo> {
        using Request = PlayerInfoRequest;
        using Response = PlayerDetails;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerInfo";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerSetGameMode> {
        using Request = PlayerSetGameModeRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Players);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerSetGameMode";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerSetHealth> {
        using Request = PlayerSetHealthRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Players);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerSetHealth";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerDamage> {
        using Request = PlayerDamageRequest;
        using Response = bool;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Players);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerDamage";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerKill> {
        using Request = PlayerKillRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Players);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerKill";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerSetFood> {
        using Request = PlayerSetFoodRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Players);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerSetFood";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerAddExperience> {
        using Request = PlayerAddExperienceRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Players);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerAddExperience";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerEffectAdd> {
        using Request = PlayerEffectAddRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Players);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerEffectAdd";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerEffectRemove> {
        using Request = PlayerEffectRemoveRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Players);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerEffectRemove";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerInventory> {
        using Request = PlayerInventoryRequest;
        using Response = ServerInventory;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerInventory";
        static constexpr std::string_view kCategory = "inventory";
    };
    template <> struct OpSpec<Op::PlayerSetSlot> {
        using Request = PlayerSetSlotRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Players);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerSetSlot";
        static constexpr std::string_view kCategory = "inventory";
    };
    template <> struct OpSpec<Op::PlayerClearInventory> {
        using Request = PlayerClearInventoryRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Players);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerClearInventory";
        static constexpr std::string_view kCategory = "inventory";
    };
    template <> struct OpSpec<Op::PlayerSetVelocity> {
        using Request = PlayerSetVelocityRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Players);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerSetVelocity";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerTransferDimension> {
        using Request = PlayerTransferDimensionRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Players);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerTransferDimension";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerTitle> {
        using Request = PlayerTitleRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerTitle";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerActionBar> {
        using Request = PlayerActionBarRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerActionBar";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerSound> {
        using Request = PlayerSoundRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerSound";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerTransfer> {
        using Request = PlayerTransferRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Admin);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerTransfer";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerSetOperator> {
        using Request = PlayerSetOperatorRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Admin);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerSetOperator";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerForm> {
        using Request = PlayerFormRequest;
        using Response = uint32_t;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerForm";
        static constexpr std::string_view kCategory = "ui";
    };
    template <> struct OpSpec<Op::PlayerRunCommand> {
        using Request = PlayerRunCommandRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Admin);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerRunCommand";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerSetSpawn> {
        using Request = PlayerSetSpawnRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Players);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerSetSpawn";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::PlayerByName> {
        using Request = PlayerByNameRequest;
        using Response = uint64_t;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "PlayerByName";
        static constexpr std::string_view kCategory = "players";
    };
    template <> struct OpSpec<Op::BlockState> {
        using Request = BlockStateRequest;
        using Response = BlockInfo;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "BlockState";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::BlockStateSet> {
        using Request = BlockStateSetRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::World);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "BlockStateSet";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::Fill> {
        using Request = FillRequest;
        using Response = uint32_t;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::World);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Fill";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::TimeGet> {
        using Request = Nothing;
        using Response = TimeGetResponse;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "TimeGet";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::TimeSet> {
        using Request = TimeSetRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::World);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "TimeSet";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::WeatherSet> {
        using Request = WeatherSetRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::World);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "WeatherSet";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::GameRuleGet> {
        using Request = GameRuleGetRequest;
        using Response = int32_t;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "GameRuleGet";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::GameRuleSet> {
        using Request = GameRuleSetRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Admin);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "GameRuleSet";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::DifficultySet> {
        using Request = DifficultySetRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Admin);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "DifficultySet";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::Explode> {
        using Request = ExplodeRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::World);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "Explode";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::HighestBlock> {
        using Request = HighestBlockRequest;
        using Response = int32_t;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "HighestBlock";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::ParticleAt> {
        using Request = ParticleAtRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "ParticleAt";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::DropItem> {
        using Request = DropItemRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::World);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "DropItem";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::WorldSpawnGet> {
        using Request = Nothing;
        using Response = BlockCoord;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "WorldSpawnGet";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::WorldSpawnSet> {
        using Request = BlockCoord;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Admin);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "WorldSpawnSet";
        static constexpr std::string_view kCategory = "world";
    };
    template <> struct OpSpec<Op::EntitySpawn> {
        using Request = EntitySpawnRequest;
        using Response = uint64_t;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::World);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "EntitySpawn";
        static constexpr std::string_view kCategory = "entity";
    };
    template <> struct OpSpec<Op::EntityList> {
        using Request = EntityListRequest;
        using Response = EntityListResponse;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "EntityList";
        static constexpr std::string_view kCategory = "entity";
    };
    template <> struct OpSpec<Op::EntityRemove> {
        using Request = EntityRemoveRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::World);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "EntityRemove";
        static constexpr std::string_view kCategory = "entity";
    };
    template <> struct OpSpec<Op::EntityDamage> {
        using Request = EntityDamageRequest;
        using Response = bool;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::World);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "EntityDamage";
        static constexpr std::string_view kCategory = "entity";
    };
    template <> struct OpSpec<Op::EntityTeleport> {
        using Request = EntityTeleportRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::World);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "EntityTeleport";
        static constexpr std::string_view kCategory = "entity";
    };
    template <> struct OpSpec<Op::EntitySetVelocity> {
        using Request = EntitySetVelocityRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::World);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "EntitySetVelocity";
        static constexpr std::string_view kCategory = "entity";
    };
    template <> struct OpSpec<Op::EntityInfo> {
        using Request = EntityInfoRequest;
        using Response = ServerEntity;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "EntityInfo";
        static constexpr std::string_view kCategory = "entity";
    };
    template <> struct OpSpec<Op::EntitySetHealth> {
        using Request = EntitySetHealthRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::World);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "EntitySetHealth";
        static constexpr std::string_view kCategory = "entity";
    };
    template <> struct OpSpec<Op::RunCommand> {
        using Request = RunCommandRequest;
        using Response = RestText;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Admin);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "RunCommand";
        static constexpr std::string_view kCategory = "server";
    };
    template <> struct OpSpec<Op::MotdSet> {
        using Request = MotdSetRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::Admin);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "MotdSet";
        static constexpr std::string_view kCategory = "server";
    };
    template <> struct OpSpec<Op::LootTableOverride> {
        using Request = LootTableOverrideRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = static_cast<uint32_t>(Permission::World);
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "LootTableOverride";
        static constexpr std::string_view kCategory = "loot";
    };
    template <> struct OpSpec<Op::LootTableGet> {
        using Request = LootTableGetRequest;
        using Response = RestText;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "LootTableGet";
        static constexpr std::string_view kCategory = "loot";
    };
    template <> struct OpSpec<Op::EventAmend> {
        using Request = EventAmendRequest;
        using Response = Nothing;
        static constexpr uint32_t kPermission = 0u;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr uint32_t kSince = 3;
        static constexpr bool kHot = false;
        static constexpr std::string_view kName = "EventAmend";
        static constexpr std::string_view kCategory = "events";
    };

    template <Event E> struct EventSpec;
    template <> struct EventSpec<Event::Load> {
        using Payload = EventNothing;
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr bool kMethod = false;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Load";
    };
    template <> struct EventSpec<Event::Unload> {
        using Payload = EventNothing;
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr bool kMethod = false;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Unload";
    };
    template <> struct EventSpec<Event::Tick> {
        using Payload = EventTick;
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = true;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Tick";
    };
    template <> struct EventSpec<Event::Frame> {
        using Payload = EventFrame;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = true;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Frame";
    };
    template <> struct EventSpec<Event::ChatReceived> {
        using Payload = EventChat;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "ChatReceived";
    };
    template <> struct EventSpec<Event::ChatSending> {
        using Payload = EventText;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "ChatSending";
    };
    template <> struct EventSpec<Event::Connected> {
        using Payload = EventConnection;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Connected";
    };
    template <> struct EventSpec<Event::Disconnected> {
        using Payload = EventConnection;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Disconnected";
    };
    template <> struct EventSpec<Event::Key> {
        using Payload = EventKey;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Key";
    };
    template <> struct EventSpec<Event::Command> {
        using Payload = EventCommand;
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr bool kMethod = false;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Command";
    };
    template <> struct EventSpec<Event::Damage> {
        using Payload = EventDamage;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Damage";
    };
    template <> struct EventSpec<Event::Death> {
        using Payload = EventText;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Death";
    };
    template <> struct EventSpec<Event::Respawned> {
        using Payload = EventNothing;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Respawned";
    };
    template <> struct EventSpec<Event::DimensionChanged> {
        using Payload = EventDimension;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "DimensionChanged";
    };
    template <> struct EventSpec<Event::ScreenChanged> {
        using Payload = EventScreen;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "ScreenChanged";
    };
    template <> struct EventSpec<Event::SlotChanged> {
        using Payload = EventSlot;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "SlotChanged";
    };
    template <> struct EventSpec<Event::EntityAdded> {
        using Payload = EventEntity;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "EntityAdded";
    };
    template <> struct EventSpec<Event::EntityRemoved> {
        using Payload = EventEntity;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "EntityRemoved";
    };
    template <> struct EventSpec<Event::Sound> {
        using Payload = EventSound;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Sound";
    };
    template <> struct EventSpec<Event::HttpResponse> {
        using Payload = EventHttp;
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr bool kMethod = false;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "HttpResponse";
    };
    template <> struct EventSpec<Event::Packet> {
        using Payload = EventPacket;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = true;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Packet";
    };
    template <> struct EventSpec<Event::Title> {
        using Payload = EventTitle;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Title";
    };
    template <> struct EventSpec<Event::SettingChanged> {
        using Payload = EventSettingChanged;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "SettingChanged";
    };
    template <> struct EventSpec<Event::Look> {
        using Payload = EventLook;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = true;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Look";
    };
    template <> struct EventSpec<Event::MouseMove> {
        using Payload = EventMouseMove;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = true;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "MouseMove";
    };
    template <> struct EventSpec<Event::MouseButton> {
        using Payload = EventMouseButton;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "MouseButton";
    };
    template <> struct EventSpec<Event::MouseWheel> {
        using Payload = EventMouseWheel;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "MouseWheel";
    };
    template <> struct EventSpec<Event::KeyRaw> {
        using Payload = EventKeyRaw;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "KeyRaw";
    };
    template <> struct EventSpec<Event::TextInput> {
        using Payload = EventText;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "TextInput";
    };
    template <> struct EventSpec<Event::ScreenRender> {
        using Payload = EventScreenRender;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = true;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "ScreenRender";
    };
    template <> struct EventSpec<Event::SlotClick> {
        using Payload = EventSlotClick;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "SlotClick";
    };
    template <> struct EventSpec<Event::Interact> {
        using Payload = EventInteract;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Interact";
    };
    template <> struct EventSpec<Event::ChunkLoaded> {
        using Payload = EventChunk;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "ChunkLoaded";
    };
    template <> struct EventSpec<Event::ChunkUnloaded> {
        using Payload = EventChunk;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "ChunkUnloaded";
    };
    template <> struct EventSpec<Event::BlockChanged> {
        using Payload = EventBlockChanged;
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "BlockChanged";
    };
    template <> struct EventSpec<Event::ResourcesReloaded> {
        using Payload = EventNothing;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "ResourcesReloaded";
    };
    template <> struct EventSpec<Event::LevelLoaded> {
        using Payload = EventLevel;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "LevelLoaded";
    };
    template <> struct EventSpec<Event::LevelUnloaded> {
        using Payload = EventNothing;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "LevelUnloaded";
    };
    template <> struct EventSpec<Event::ModsLoaded> {
        using Payload = EventNothing;
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "ModsLoaded";
    };
    template <> struct EventSpec<Event::ClientStopping> {
        using Payload = EventNothing;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "ClientStopping";
    };
    template <> struct EventSpec<Event::Toast> {
        using Payload = EventToast;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Toast";
    };
    template <> struct EventSpec<Event::ModMessage> {
        using Payload = EventModMessage;
        static constexpr Hosts kHosts = Hosts::Both;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "ModMessage";
    };
    template <> struct EventSpec<Event::Tooltip> {
        using Payload = EventTooltip;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "Tooltip";
    };
    template <> struct EventSpec<Event::ScreenButton> {
        using Payload = EventScreenButton;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "ScreenButton";
    };
    template <> struct EventSpec<Event::WindowFocus> {
        using Payload = EventWindow;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "WindowFocus";
    };
    template <> struct EventSpec<Event::ServerPing> {
        using Payload = EventServerPing;
        static constexpr Hosts kHosts = Hosts::Client;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "ServerPing";
    };
    template <> struct EventSpec<Event::PlayerJoin> {
        using Payload = EventPlayer;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "PlayerJoin";
    };
    template <> struct EventSpec<Event::PlayerQuit> {
        using Payload = EventPlayer;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "PlayerQuit";
    };
    template <> struct EventSpec<Event::PlayerChat> {
        using Payload = EventPlayerChat;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "PlayerChat";
    };
    template <> struct EventSpec<Event::BlockBreak> {
        using Payload = EventPlayerBlock;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "BlockBreak";
    };
    template <> struct EventSpec<Event::BlockPlace> {
        using Payload = EventPlayerBlock;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 1;
        static constexpr std::string_view kName = "BlockPlace";
    };
    template <> struct EventSpec<Event::PlayerDamage> {
        using Payload = EventServerDamage;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "PlayerDamage";
    };
    template <> struct EventSpec<Event::PlayerDeath> {
        using Payload = EventServerDeath;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "PlayerDeath";
    };
    template <> struct EventSpec<Event::PlayerRespawn> {
        using Payload = EventPlayer;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "PlayerRespawn";
    };
    template <> struct EventSpec<Event::PlayerUseBlock> {
        using Payload = EventPlayerUse;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "PlayerUseBlock";
    };
    template <> struct EventSpec<Event::PlayerAttack> {
        using Payload = EventPlayerAttack;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "PlayerAttack";
    };
    template <> struct EventSpec<Event::EntityDamage> {
        using Payload = EventEntityDamage;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "EntityDamage";
    };
    template <> struct EventSpec<Event::EntityDeath> {
        using Payload = EventEntityDeath;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "EntityDeath";
    };
    template <> struct EventSpec<Event::EntitySpawn> {
        using Payload = EventEntitySpawn;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "EntitySpawn";
    };
    template <> struct EventSpec<Event::ItemConsume> {
        using Payload = EventItem;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "ItemConsume";
    };
    template <> struct EventSpec<Event::GameModeChange> {
        using Payload = EventGameModeChange;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "GameModeChange";
    };
    template <> struct EventSpec<Event::FormResponse> {
        using Payload = EventFormResponse;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "FormResponse";
    };
    template <> struct EventSpec<Event::PlayerMove> {
        using Payload = EventPlayerMove;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = true;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "PlayerMove";
    };
    template <> struct EventSpec<Event::Explosion> {
        using Payload = EventExplosion;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "Explosion";
    };
    template <> struct EventSpec<Event::ItemDrop> {
        using Payload = EventItem;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "ItemDrop";
    };
    template <> struct EventSpec<Event::ItemPickup> {
        using Payload = EventItem;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "ItemPickup";
    };
    template <> struct EventSpec<Event::ContainerOpen> {
        using Payload = EventContainer;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "ContainerOpen";
    };
    template <> struct EventSpec<Event::DimensionChange> {
        using Payload = EventPlayerDimension;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = false;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "DimensionChange";
    };
    template <> struct EventSpec<Event::ServerCommand> {
        using Payload = EventServerCommand;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "ServerCommand";
    };
    template <> struct EventSpec<Event::PreLogin> {
        using Payload = EventPreLogin;
        static constexpr Hosts kHosts = Hosts::Server;
        static constexpr bool kMethod = true;
        static constexpr bool kCancellable = true;
        static constexpr bool kHot = false;
        static constexpr uint32_t kSince = 3;
        static constexpr std::string_view kName = "PreLogin";
    };

    template <Event E> using payload_t = typename EventSpec<E>::Payload;

    constexpr uint32_t PermissionOf(Op op) {
        switch (op) {
        case Op::Log: return OpSpec<Op::Log>::kPermission;
        case Op::Meta: return OpSpec<Op::Meta>::kPermission;
        case Op::Clock: return OpSpec<Op::Clock>::kPermission;
        case Op::PlayerState: return OpSpec<Op::PlayerState>::kPermission;
        case Op::PlayerEffects: return OpSpec<Op::PlayerEffects>::kPermission;
        case Op::PlayerLook: return OpSpec<Op::PlayerLook>::kPermission;
        case Op::Inventory: return OpSpec<Op::Inventory>::kPermission;
        case Op::InventorySelect: return OpSpec<Op::InventorySelect>::kPermission;
        case Op::InventoryDrop: return OpSpec<Op::InventoryDrop>::kPermission;
        case Op::InventorySwap: return OpSpec<Op::InventorySwap>::kPermission;
        case Op::InventoryClick: return OpSpec<Op::InventoryClick>::kPermission;
        case Op::InventoryOpen: return OpSpec<Op::InventoryOpen>::kPermission;
        case Op::InventoryClose: return OpSpec<Op::InventoryClose>::kPermission;
        case Op::WorldState: return OpSpec<Op::WorldState>::kPermission;
        case Op::Block: return OpSpec<Op::Block>::kPermission;
        case Op::Region: return OpSpec<Op::Region>::kPermission;
        case Op::Raycast: return OpSpec<Op::Raycast>::kPermission;
        case Op::Aim: return OpSpec<Op::Aim>::kPermission;
        case Op::FindBlocks: return OpSpec<Op::FindBlocks>::kPermission;
        case Op::ChunkLoaded: return OpSpec<Op::ChunkLoaded>::kPermission;
        case Op::BlockNames: return OpSpec<Op::BlockNames>::kPermission;
        case Op::BlockProps: return OpSpec<Op::BlockProps>::kPermission;
        case Op::BlockEntityData: return OpSpec<Op::BlockEntityData>::kPermission;
        case Op::DisplayName: return OpSpec<Op::DisplayName>::kPermission;
        case Op::BreakProgress: return OpSpec<Op::BreakProgress>::kPermission;
        case Op::MapChunk: return OpSpec<Op::MapChunk>::kPermission;
        case Op::Entities: return OpSpec<Op::Entities>::kPermission;
        case Op::EntityById: return OpSpec<Op::EntityById>::kPermission;
        case Op::PlayerList: return OpSpec<Op::PlayerList>::kPermission;
        case Op::Options: return OpSpec<Op::Options>::kPermission;
        case Op::OptionGet: return OpSpec<Op::OptionGet>::kPermission;
        case Op::OptionSet: return OpSpec<Op::OptionSet>::kPermission;
        case Op::Screen: return OpSpec<Op::Screen>::kPermission;
        case Op::Translate: return OpSpec<Op::Translate>::kPermission;
        case Op::ReloadResources: return OpSpec<Op::ReloadResources>::kPermission;
        case Op::PerspectiveGet: return OpSpec<Op::PerspectiveGet>::kPermission;
        case Op::PerspectiveSet: return OpSpec<Op::PerspectiveSet>::kPermission;
        case Op::ResourcePacks: return OpSpec<Op::ResourcePacks>::kPermission;
        case Op::InputDown: return OpSpec<Op::InputDown>::kPermission;
        case Op::InputOverride: return OpSpec<Op::InputOverride>::kPermission;
        case Op::KeyRegister: return OpSpec<Op::KeyRegister>::kPermission;
        case Op::InputActions: return OpSpec<Op::InputActions>::kPermission;
        case Op::MousePosition: return OpSpec<Op::MousePosition>::kPermission;
        case Op::Chat: return OpSpec<Op::Chat>::kPermission;
        case Op::Command: return OpSpec<Op::Command>::kPermission;
        case Op::LocalMessage: return OpSpec<Op::LocalMessage>::kPermission;
        case Op::CommandRegister: return OpSpec<Op::CommandRegister>::kPermission;
        case Op::CommandUnregister: return OpSpec<Op::CommandUnregister>::kPermission;
        case Op::ServerCommands: return OpSpec<Op::ServerCommands>::kPermission;
        case Op::Toast: return OpSpec<Op::Toast>::kPermission;
        case Op::Title: return OpSpec<Op::Title>::kPermission;
        case Op::ChatHistory: return OpSpec<Op::ChatHistory>::kPermission;
        case Op::TitleState: return OpSpec<Op::TitleState>::kPermission;
        case Op::SwingArm: return OpSpec<Op::SwingArm>::kPermission;
        case Op::UseItem: return OpSpec<Op::UseItem>::kPermission;
        case Op::UseOnBlock: return OpSpec<Op::UseOnBlock>::kPermission;
        case Op::AttackEntity: return OpSpec<Op::AttackEntity>::kPermission;
        case Op::InteractEntity: return OpSpec<Op::InteractEntity>::kPermission;
        case Op::BreakBlock: return OpSpec<Op::BreakBlock>::kPermission;
        case Op::Look: return OpSpec<Op::Look>::kPermission;
        case Op::Respawn: return OpSpec<Op::Respawn>::kPermission;
        case Op::Disconnect: return OpSpec<Op::Disconnect>::kPermission;
        case Op::PlaySound: return OpSpec<Op::PlaySound>::kPermission;
        case Op::Jump: return OpSpec<Op::Jump>::kPermission;
        case Op::UiSubmit: return OpSpec<Op::UiSubmit>::kPermission;
        case Op::UiClear: return OpSpec<Op::UiClear>::kPermission;
        case Op::UiMeasure: return OpSpec<Op::UiMeasure>::kPermission;
        case Op::GfxTextureCreate: return OpSpec<Op::GfxTextureCreate>::kPermission;
        case Op::GfxTextureUpdate: return OpSpec<Op::GfxTextureUpdate>::kPermission;
        case Op::GfxTextureDestroy: return OpSpec<Op::GfxTextureDestroy>::kPermission;
        case Op::GfxSubmit: return OpSpec<Op::GfxSubmit>::kPermission;
        case Op::GfxClear: return OpSpec<Op::GfxClear>::kPermission;
        case Op::GfxDisplay: return OpSpec<Op::GfxDisplay>::kPermission;
        case Op::GfxImageLoad: return OpSpec<Op::GfxImageLoad>::kPermission;
        case Op::InputGrab: return OpSpec<Op::InputGrab>::kPermission;
        case Op::NativeInfo: return OpSpec<Op::NativeInfo>::kPermission;
        case Op::StorageRead: return OpSpec<Op::StorageRead>::kPermission;
        case Op::StorageWrite: return OpSpec<Op::StorageWrite>::kPermission;
        case Op::StorageList: return OpSpec<Op::StorageList>::kPermission;
        case Op::StorageRemove: return OpSpec<Op::StorageRemove>::kPermission;
        case Op::StorageExists: return OpSpec<Op::StorageExists>::kPermission;
        case Op::StorageMakeDir: return OpSpec<Op::StorageMakeDir>::kPermission;
        case Op::HttpRequest: return OpSpec<Op::HttpRequest>::kPermission;
        case Op::PacketSend: return OpSpec<Op::PacketSend>::kPermission;
        case Op::PacketName: return OpSpec<Op::PacketName>::kPermission;
        case Op::SettingGet: return OpSpec<Op::SettingGet>::kPermission;
        case Op::SettingSet: return OpSpec<Op::SettingSet>::kPermission;
        case Op::SettingList: return OpSpec<Op::SettingList>::kPermission;
        case Op::ModList: return OpSpec<Op::ModList>::kPermission;
        case Op::IconSet: return OpSpec<Op::IconSet>::kPermission;
        case Op::OpenScreen: return OpSpec<Op::OpenScreen>::kPermission;
        case Op::OpenModSettings: return OpSpec<Op::OpenModSettings>::kPermission;
        case Op::TakeScreenshot: return OpSpec<Op::TakeScreenshot>::kPermission;
        case Op::CyclePerspective: return OpSpec<Op::CyclePerspective>::kPermission;
        case Op::CopyToClipboard: return OpSpec<Op::CopyToClipboard>::kPermission;
        case Op::OpenUrl: return OpSpec<Op::OpenUrl>::kPermission;
        case Op::ToggleHud: return OpSpec<Op::ToggleHud>::kPermission;
        case Op::CopyFile: return OpSpec<Op::CopyFile>::kPermission;
        case Op::MoveFile: return OpSpec<Op::MoveFile>::kPermission;
        case Op::DownloadFile: return OpSpec<Op::DownloadFile>::kPermission;
        case Op::CameraControl: return OpSpec<Op::CameraControl>::kPermission;
        case Op::CameraState: return OpSpec<Op::CameraState>::kPermission;
        case Op::WorldToScreen: return OpSpec<Op::WorldToScreen>::kPermission;
        case Op::PlayerMotion: return OpSpec<Op::PlayerMotion>::kPermission;
        case Op::PlayerTeleport: return OpSpec<Op::PlayerTeleport>::kPermission;
        case Op::PlayerModelPose: return OpSpec<Op::PlayerModelPose>::kPermission;
        case Op::PlayerSetLook: return OpSpec<Op::PlayerSetLook>::kPermission;
        case Op::WorldLines: return OpSpec<Op::WorldLines>::kPermission;
        case Op::WorldBoxes: return OpSpec<Op::WorldBoxes>::kPermission;
        case Op::ClientBlockSet: return OpSpec<Op::ClientBlockSet>::kPermission;
        case Op::ClientBlockGet: return OpSpec<Op::ClientBlockGet>::kPermission;
        case Op::PacketDrop: return OpSpec<Op::PacketDrop>::kPermission;
        case Op::PacketCounters: return OpSpec<Op::PacketCounters>::kPermission;
        case Op::PacketInject: return OpSpec<Op::PacketInject>::kPermission;
        case Op::HudElementSet: return OpSpec<Op::HudElementSet>::kPermission;
        case Op::DebugLines: return OpSpec<Op::DebugLines>::kPermission;
        case Op::FogSet: return OpSpec<Op::FogSet>::kPermission;
        case Op::ScreenTint: return OpSpec<Op::ScreenTint>::kPermission;
        case Op::EntityRenderOverride: return OpSpec<Op::EntityRenderOverride>::kPermission;
        case Op::WorldMesh: return OpSpec<Op::WorldMesh>::kPermission;
        case Op::WorldMeshClear: return OpSpec<Op::WorldMeshClear>::kPermission;
        case Op::FrameLimit: return OpSpec<Op::FrameLimit>::kPermission;
        case Op::WindowState: return OpSpec<Op::WindowState>::kPermission;
        case Op::ScreenButtonAdd: return OpSpec<Op::ScreenButtonAdd>::kPermission;
        case Op::ScreenButtonRemove: return OpSpec<Op::ScreenButtonRemove>::kPermission;
        case Op::ScreenOpenCustom: return OpSpec<Op::ScreenOpenCustom>::kPermission;
        case Op::TooltipAppend: return OpSpec<Op::TooltipAppend>::kPermission;
        case Op::LoadingOverlay: return OpSpec<Op::LoadingOverlay>::kPermission;
        case Op::SkinOverride: return OpSpec<Op::SkinOverride>::kPermission;
        case Op::AmbientSound: return OpSpec<Op::AmbientSound>::kPermission;
        case Op::ServerPing: return OpSpec<Op::ServerPing>::kPermission;
        case Op::ModSend: return OpSpec<Op::ModSend>::kPermission;
        case Op::PackRegister: return OpSpec<Op::PackRegister>::kPermission;
        case Op::ShaderPackSet: return OpSpec<Op::ShaderPackSet>::kPermission;
        case Op::ParticleSpawn: return OpSpec<Op::ParticleSpawn>::kPermission;
        case Op::KeyRegisterEx: return OpSpec<Op::KeyRegisterEx>::kPermission;
        case Op::Broadcast: return OpSpec<Op::Broadcast>::kPermission;
        case Op::PlayerMessage: return OpSpec<Op::PlayerMessage>::kPermission;
        case Op::Kick: return OpSpec<Op::Kick>::kPermission;
        case Op::OnlinePlayers: return OpSpec<Op::OnlinePlayers>::kPermission;
        case Op::Teleport: return OpSpec<Op::Teleport>::kPermission;
        case Op::SetBlock: return OpSpec<Op::SetBlock>::kPermission;
        case Op::BlockName: return OpSpec<Op::BlockName>::kPermission;
        case Op::ServerInfo: return OpSpec<Op::ServerInfo>::kPermission;
        case Op::GiveItem: return OpSpec<Op::GiveItem>::kPermission;
        case Op::PlayerInfo: return OpSpec<Op::PlayerInfo>::kPermission;
        case Op::PlayerSetGameMode: return OpSpec<Op::PlayerSetGameMode>::kPermission;
        case Op::PlayerSetHealth: return OpSpec<Op::PlayerSetHealth>::kPermission;
        case Op::PlayerDamage: return OpSpec<Op::PlayerDamage>::kPermission;
        case Op::PlayerKill: return OpSpec<Op::PlayerKill>::kPermission;
        case Op::PlayerSetFood: return OpSpec<Op::PlayerSetFood>::kPermission;
        case Op::PlayerAddExperience: return OpSpec<Op::PlayerAddExperience>::kPermission;
        case Op::PlayerEffectAdd: return OpSpec<Op::PlayerEffectAdd>::kPermission;
        case Op::PlayerEffectRemove: return OpSpec<Op::PlayerEffectRemove>::kPermission;
        case Op::PlayerInventory: return OpSpec<Op::PlayerInventory>::kPermission;
        case Op::PlayerSetSlot: return OpSpec<Op::PlayerSetSlot>::kPermission;
        case Op::PlayerClearInventory: return OpSpec<Op::PlayerClearInventory>::kPermission;
        case Op::PlayerSetVelocity: return OpSpec<Op::PlayerSetVelocity>::kPermission;
        case Op::PlayerTransferDimension: return OpSpec<Op::PlayerTransferDimension>::kPermission;
        case Op::PlayerTitle: return OpSpec<Op::PlayerTitle>::kPermission;
        case Op::PlayerActionBar: return OpSpec<Op::PlayerActionBar>::kPermission;
        case Op::PlayerSound: return OpSpec<Op::PlayerSound>::kPermission;
        case Op::PlayerTransfer: return OpSpec<Op::PlayerTransfer>::kPermission;
        case Op::PlayerSetOperator: return OpSpec<Op::PlayerSetOperator>::kPermission;
        case Op::PlayerForm: return OpSpec<Op::PlayerForm>::kPermission;
        case Op::PlayerRunCommand: return OpSpec<Op::PlayerRunCommand>::kPermission;
        case Op::PlayerSetSpawn: return OpSpec<Op::PlayerSetSpawn>::kPermission;
        case Op::PlayerByName: return OpSpec<Op::PlayerByName>::kPermission;
        case Op::BlockState: return OpSpec<Op::BlockState>::kPermission;
        case Op::BlockStateSet: return OpSpec<Op::BlockStateSet>::kPermission;
        case Op::Fill: return OpSpec<Op::Fill>::kPermission;
        case Op::TimeGet: return OpSpec<Op::TimeGet>::kPermission;
        case Op::TimeSet: return OpSpec<Op::TimeSet>::kPermission;
        case Op::WeatherSet: return OpSpec<Op::WeatherSet>::kPermission;
        case Op::GameRuleGet: return OpSpec<Op::GameRuleGet>::kPermission;
        case Op::GameRuleSet: return OpSpec<Op::GameRuleSet>::kPermission;
        case Op::DifficultySet: return OpSpec<Op::DifficultySet>::kPermission;
        case Op::Explode: return OpSpec<Op::Explode>::kPermission;
        case Op::HighestBlock: return OpSpec<Op::HighestBlock>::kPermission;
        case Op::ParticleAt: return OpSpec<Op::ParticleAt>::kPermission;
        case Op::DropItem: return OpSpec<Op::DropItem>::kPermission;
        case Op::WorldSpawnGet: return OpSpec<Op::WorldSpawnGet>::kPermission;
        case Op::WorldSpawnSet: return OpSpec<Op::WorldSpawnSet>::kPermission;
        case Op::EntitySpawn: return OpSpec<Op::EntitySpawn>::kPermission;
        case Op::EntityList: return OpSpec<Op::EntityList>::kPermission;
        case Op::EntityRemove: return OpSpec<Op::EntityRemove>::kPermission;
        case Op::EntityDamage: return OpSpec<Op::EntityDamage>::kPermission;
        case Op::EntityTeleport: return OpSpec<Op::EntityTeleport>::kPermission;
        case Op::EntitySetVelocity: return OpSpec<Op::EntitySetVelocity>::kPermission;
        case Op::EntityInfo: return OpSpec<Op::EntityInfo>::kPermission;
        case Op::EntitySetHealth: return OpSpec<Op::EntitySetHealth>::kPermission;
        case Op::RunCommand: return OpSpec<Op::RunCommand>::kPermission;
        case Op::MotdSet: return OpSpec<Op::MotdSet>::kPermission;
        case Op::LootTableOverride: return OpSpec<Op::LootTableOverride>::kPermission;
        case Op::LootTableGet: return OpSpec<Op::LootTableGet>::kPermission;
        case Op::EventAmend: return OpSpec<Op::EventAmend>::kPermission;
        }
        return 0;
    }

    constexpr Hosts HostsOf(Op op) {
        switch (op) {
        case Op::Log: return Hosts::Both;
        case Op::Meta: return Hosts::Client;
        case Op::Clock: return Hosts::Both;
        case Op::PlayerState: return Hosts::Client;
        case Op::PlayerEffects: return Hosts::Client;
        case Op::PlayerLook: return Hosts::Client;
        case Op::Inventory: return Hosts::Client;
        case Op::InventorySelect: return Hosts::Client;
        case Op::InventoryDrop: return Hosts::Client;
        case Op::InventorySwap: return Hosts::Client;
        case Op::InventoryClick: return Hosts::Client;
        case Op::InventoryOpen: return Hosts::Client;
        case Op::InventoryClose: return Hosts::Client;
        case Op::WorldState: return Hosts::Client;
        case Op::Block: return Hosts::Client;
        case Op::Region: return Hosts::Client;
        case Op::Raycast: return Hosts::Client;
        case Op::Aim: return Hosts::Client;
        case Op::FindBlocks: return Hosts::Client;
        case Op::ChunkLoaded: return Hosts::Client;
        case Op::BlockNames: return Hosts::Client;
        case Op::BlockProps: return Hosts::Client;
        case Op::BlockEntityData: return Hosts::Client;
        case Op::DisplayName: return Hosts::Client;
        case Op::BreakProgress: return Hosts::Client;
        case Op::MapChunk: return Hosts::Client;
        case Op::Entities: return Hosts::Client;
        case Op::EntityById: return Hosts::Client;
        case Op::PlayerList: return Hosts::Client;
        case Op::Options: return Hosts::Client;
        case Op::OptionGet: return Hosts::Client;
        case Op::OptionSet: return Hosts::Client;
        case Op::Screen: return Hosts::Client;
        case Op::Translate: return Hosts::Client;
        case Op::ReloadResources: return Hosts::Client;
        case Op::PerspectiveGet: return Hosts::Client;
        case Op::PerspectiveSet: return Hosts::Client;
        case Op::ResourcePacks: return Hosts::Client;
        case Op::InputDown: return Hosts::Client;
        case Op::InputOverride: return Hosts::Client;
        case Op::KeyRegister: return Hosts::Client;
        case Op::InputActions: return Hosts::Client;
        case Op::MousePosition: return Hosts::Client;
        case Op::Chat: return Hosts::Client;
        case Op::Command: return Hosts::Client;
        case Op::LocalMessage: return Hosts::Client;
        case Op::CommandRegister: return Hosts::Both;
        case Op::CommandUnregister: return Hosts::Both;
        case Op::ServerCommands: return Hosts::Client;
        case Op::Toast: return Hosts::Client;
        case Op::Title: return Hosts::Client;
        case Op::ChatHistory: return Hosts::Client;
        case Op::TitleState: return Hosts::Client;
        case Op::SwingArm: return Hosts::Client;
        case Op::UseItem: return Hosts::Client;
        case Op::UseOnBlock: return Hosts::Client;
        case Op::AttackEntity: return Hosts::Client;
        case Op::InteractEntity: return Hosts::Client;
        case Op::BreakBlock: return Hosts::Client;
        case Op::Look: return Hosts::Client;
        case Op::Respawn: return Hosts::Client;
        case Op::Disconnect: return Hosts::Client;
        case Op::PlaySound: return Hosts::Client;
        case Op::Jump: return Hosts::Client;
        case Op::UiSubmit: return Hosts::Client;
        case Op::UiClear: return Hosts::Client;
        case Op::UiMeasure: return Hosts::Client;
        case Op::GfxTextureCreate: return Hosts::Client;
        case Op::GfxTextureUpdate: return Hosts::Client;
        case Op::GfxTextureDestroy: return Hosts::Client;
        case Op::GfxSubmit: return Hosts::Client;
        case Op::GfxClear: return Hosts::Client;
        case Op::GfxDisplay: return Hosts::Client;
        case Op::GfxImageLoad: return Hosts::Client;
        case Op::InputGrab: return Hosts::Client;
        case Op::NativeInfo: return Hosts::Client;
        case Op::StorageRead: return Hosts::Both;
        case Op::StorageWrite: return Hosts::Both;
        case Op::StorageList: return Hosts::Both;
        case Op::StorageRemove: return Hosts::Both;
        case Op::StorageExists: return Hosts::Both;
        case Op::StorageMakeDir: return Hosts::Both;
        case Op::HttpRequest: return Hosts::Both;
        case Op::PacketSend: return Hosts::Client;
        case Op::PacketName: return Hosts::Client;
        case Op::SettingGet: return Hosts::Both;
        case Op::SettingSet: return Hosts::Both;
        case Op::SettingList: return Hosts::Both;
        case Op::ModList: return Hosts::Both;
        case Op::IconSet: return Hosts::Client;
        case Op::OpenScreen: return Hosts::Client;
        case Op::OpenModSettings: return Hosts::Client;
        case Op::TakeScreenshot: return Hosts::Client;
        case Op::CyclePerspective: return Hosts::Client;
        case Op::CopyToClipboard: return Hosts::Client;
        case Op::OpenUrl: return Hosts::Client;
        case Op::ToggleHud: return Hosts::Client;
        case Op::CopyFile: return Hosts::Client;
        case Op::MoveFile: return Hosts::Client;
        case Op::DownloadFile: return Hosts::Client;
        case Op::CameraControl: return Hosts::Client;
        case Op::CameraState: return Hosts::Client;
        case Op::WorldToScreen: return Hosts::Client;
        case Op::PlayerMotion: return Hosts::Client;
        case Op::PlayerTeleport: return Hosts::Client;
        case Op::PlayerModelPose: return Hosts::Client;
        case Op::PlayerSetLook: return Hosts::Client;
        case Op::WorldLines: return Hosts::Client;
        case Op::WorldBoxes: return Hosts::Client;
        case Op::ClientBlockSet: return Hosts::Client;
        case Op::ClientBlockGet: return Hosts::Client;
        case Op::PacketDrop: return Hosts::Client;
        case Op::PacketCounters: return Hosts::Client;
        case Op::PacketInject: return Hosts::Client;
        case Op::HudElementSet: return Hosts::Client;
        case Op::DebugLines: return Hosts::Client;
        case Op::FogSet: return Hosts::Client;
        case Op::ScreenTint: return Hosts::Client;
        case Op::EntityRenderOverride: return Hosts::Client;
        case Op::WorldMesh: return Hosts::Client;
        case Op::WorldMeshClear: return Hosts::Client;
        case Op::FrameLimit: return Hosts::Client;
        case Op::WindowState: return Hosts::Client;
        case Op::ScreenButtonAdd: return Hosts::Client;
        case Op::ScreenButtonRemove: return Hosts::Client;
        case Op::ScreenOpenCustom: return Hosts::Client;
        case Op::TooltipAppend: return Hosts::Client;
        case Op::LoadingOverlay: return Hosts::Client;
        case Op::SkinOverride: return Hosts::Client;
        case Op::AmbientSound: return Hosts::Client;
        case Op::ServerPing: return Hosts::Client;
        case Op::ModSend: return Hosts::Both;
        case Op::PackRegister: return Hosts::Client;
        case Op::ShaderPackSet: return Hosts::Client;
        case Op::ParticleSpawn: return Hosts::Client;
        case Op::KeyRegisterEx: return Hosts::Client;
        case Op::Broadcast: return Hosts::Server;
        case Op::PlayerMessage: return Hosts::Server;
        case Op::Kick: return Hosts::Server;
        case Op::OnlinePlayers: return Hosts::Server;
        case Op::Teleport: return Hosts::Server;
        case Op::SetBlock: return Hosts::Server;
        case Op::BlockName: return Hosts::Server;
        case Op::ServerInfo: return Hosts::Server;
        case Op::GiveItem: return Hosts::Server;
        case Op::PlayerInfo: return Hosts::Server;
        case Op::PlayerSetGameMode: return Hosts::Server;
        case Op::PlayerSetHealth: return Hosts::Server;
        case Op::PlayerDamage: return Hosts::Server;
        case Op::PlayerKill: return Hosts::Server;
        case Op::PlayerSetFood: return Hosts::Server;
        case Op::PlayerAddExperience: return Hosts::Server;
        case Op::PlayerEffectAdd: return Hosts::Server;
        case Op::PlayerEffectRemove: return Hosts::Server;
        case Op::PlayerInventory: return Hosts::Server;
        case Op::PlayerSetSlot: return Hosts::Server;
        case Op::PlayerClearInventory: return Hosts::Server;
        case Op::PlayerSetVelocity: return Hosts::Server;
        case Op::PlayerTransferDimension: return Hosts::Server;
        case Op::PlayerTitle: return Hosts::Server;
        case Op::PlayerActionBar: return Hosts::Server;
        case Op::PlayerSound: return Hosts::Server;
        case Op::PlayerTransfer: return Hosts::Server;
        case Op::PlayerSetOperator: return Hosts::Server;
        case Op::PlayerForm: return Hosts::Server;
        case Op::PlayerRunCommand: return Hosts::Server;
        case Op::PlayerSetSpawn: return Hosts::Server;
        case Op::PlayerByName: return Hosts::Server;
        case Op::BlockState: return Hosts::Server;
        case Op::BlockStateSet: return Hosts::Server;
        case Op::Fill: return Hosts::Server;
        case Op::TimeGet: return Hosts::Server;
        case Op::TimeSet: return Hosts::Server;
        case Op::WeatherSet: return Hosts::Server;
        case Op::GameRuleGet: return Hosts::Server;
        case Op::GameRuleSet: return Hosts::Server;
        case Op::DifficultySet: return Hosts::Server;
        case Op::Explode: return Hosts::Server;
        case Op::HighestBlock: return Hosts::Server;
        case Op::ParticleAt: return Hosts::Server;
        case Op::DropItem: return Hosts::Server;
        case Op::WorldSpawnGet: return Hosts::Server;
        case Op::WorldSpawnSet: return Hosts::Server;
        case Op::EntitySpawn: return Hosts::Server;
        case Op::EntityList: return Hosts::Server;
        case Op::EntityRemove: return Hosts::Server;
        case Op::EntityDamage: return Hosts::Server;
        case Op::EntityTeleport: return Hosts::Server;
        case Op::EntitySetVelocity: return Hosts::Server;
        case Op::EntityInfo: return Hosts::Server;
        case Op::EntitySetHealth: return Hosts::Server;
        case Op::RunCommand: return Hosts::Server;
        case Op::MotdSet: return Hosts::Server;
        case Op::LootTableOverride: return Hosts::Server;
        case Op::LootTableGet: return Hosts::Server;
        case Op::EventAmend: return Hosts::Server;
        }
        return Hosts::Both;
    }

    constexpr Hosts HostsOf(Event event) {
        switch (event) {
        case Event::Load: return Hosts::Both;
        case Event::Unload: return Hosts::Both;
        case Event::Tick: return Hosts::Both;
        case Event::Frame: return Hosts::Client;
        case Event::ChatReceived: return Hosts::Client;
        case Event::ChatSending: return Hosts::Client;
        case Event::Connected: return Hosts::Client;
        case Event::Disconnected: return Hosts::Client;
        case Event::Key: return Hosts::Client;
        case Event::Command: return Hosts::Both;
        case Event::Damage: return Hosts::Client;
        case Event::Death: return Hosts::Client;
        case Event::Respawned: return Hosts::Client;
        case Event::DimensionChanged: return Hosts::Client;
        case Event::ScreenChanged: return Hosts::Client;
        case Event::SlotChanged: return Hosts::Client;
        case Event::EntityAdded: return Hosts::Client;
        case Event::EntityRemoved: return Hosts::Client;
        case Event::Sound: return Hosts::Client;
        case Event::HttpResponse: return Hosts::Both;
        case Event::Packet: return Hosts::Client;
        case Event::Title: return Hosts::Client;
        case Event::SettingChanged: return Hosts::Client;
        case Event::Look: return Hosts::Client;
        case Event::MouseMove: return Hosts::Client;
        case Event::MouseButton: return Hosts::Client;
        case Event::MouseWheel: return Hosts::Client;
        case Event::KeyRaw: return Hosts::Client;
        case Event::TextInput: return Hosts::Client;
        case Event::ScreenRender: return Hosts::Client;
        case Event::SlotClick: return Hosts::Client;
        case Event::Interact: return Hosts::Client;
        case Event::ChunkLoaded: return Hosts::Client;
        case Event::ChunkUnloaded: return Hosts::Client;
        case Event::BlockChanged: return Hosts::Both;
        case Event::ResourcesReloaded: return Hosts::Client;
        case Event::LevelLoaded: return Hosts::Client;
        case Event::LevelUnloaded: return Hosts::Client;
        case Event::ModsLoaded: return Hosts::Both;
        case Event::ClientStopping: return Hosts::Client;
        case Event::Toast: return Hosts::Client;
        case Event::ModMessage: return Hosts::Both;
        case Event::Tooltip: return Hosts::Client;
        case Event::ScreenButton: return Hosts::Client;
        case Event::WindowFocus: return Hosts::Client;
        case Event::ServerPing: return Hosts::Client;
        case Event::PlayerJoin: return Hosts::Server;
        case Event::PlayerQuit: return Hosts::Server;
        case Event::PlayerChat: return Hosts::Server;
        case Event::BlockBreak: return Hosts::Server;
        case Event::BlockPlace: return Hosts::Server;
        case Event::PlayerDamage: return Hosts::Server;
        case Event::PlayerDeath: return Hosts::Server;
        case Event::PlayerRespawn: return Hosts::Server;
        case Event::PlayerUseBlock: return Hosts::Server;
        case Event::PlayerAttack: return Hosts::Server;
        case Event::EntityDamage: return Hosts::Server;
        case Event::EntityDeath: return Hosts::Server;
        case Event::EntitySpawn: return Hosts::Server;
        case Event::ItemConsume: return Hosts::Server;
        case Event::GameModeChange: return Hosts::Server;
        case Event::FormResponse: return Hosts::Server;
        case Event::PlayerMove: return Hosts::Server;
        case Event::Explosion: return Hosts::Server;
        case Event::ItemDrop: return Hosts::Server;
        case Event::ItemPickup: return Hosts::Server;
        case Event::ContainerOpen: return Hosts::Server;
        case Event::DimensionChange: return Hosts::Server;
        case Event::ServerCommand: return Hosts::Server;
        case Event::PreLogin: return Hosts::Server;
        }
        return Hosts::Both;
    }

    constexpr std::string_view NameOf(Op op) {
        switch (op) {
        case Op::Log: return "Log";
        case Op::Meta: return "Meta";
        case Op::Clock: return "Clock";
        case Op::PlayerState: return "PlayerState";
        case Op::PlayerEffects: return "PlayerEffects";
        case Op::PlayerLook: return "PlayerLook";
        case Op::Inventory: return "Inventory";
        case Op::InventorySelect: return "InventorySelect";
        case Op::InventoryDrop: return "InventoryDrop";
        case Op::InventorySwap: return "InventorySwap";
        case Op::InventoryClick: return "InventoryClick";
        case Op::InventoryOpen: return "InventoryOpen";
        case Op::InventoryClose: return "InventoryClose";
        case Op::WorldState: return "WorldState";
        case Op::Block: return "Block";
        case Op::Region: return "Region";
        case Op::Raycast: return "Raycast";
        case Op::Aim: return "Aim";
        case Op::FindBlocks: return "FindBlocks";
        case Op::ChunkLoaded: return "ChunkLoaded";
        case Op::BlockNames: return "BlockNames";
        case Op::BlockProps: return "BlockProps";
        case Op::BlockEntityData: return "BlockEntityData";
        case Op::DisplayName: return "DisplayName";
        case Op::BreakProgress: return "BreakProgress";
        case Op::MapChunk: return "MapChunk";
        case Op::Entities: return "Entities";
        case Op::EntityById: return "EntityById";
        case Op::PlayerList: return "PlayerList";
        case Op::Options: return "Options";
        case Op::OptionGet: return "OptionGet";
        case Op::OptionSet: return "OptionSet";
        case Op::Screen: return "Screen";
        case Op::Translate: return "Translate";
        case Op::ReloadResources: return "ReloadResources";
        case Op::PerspectiveGet: return "PerspectiveGet";
        case Op::PerspectiveSet: return "PerspectiveSet";
        case Op::ResourcePacks: return "ResourcePacks";
        case Op::InputDown: return "InputDown";
        case Op::InputOverride: return "InputOverride";
        case Op::KeyRegister: return "KeyRegister";
        case Op::InputActions: return "InputActions";
        case Op::MousePosition: return "MousePosition";
        case Op::Chat: return "Chat";
        case Op::Command: return "Command";
        case Op::LocalMessage: return "LocalMessage";
        case Op::CommandRegister: return "CommandRegister";
        case Op::CommandUnregister: return "CommandUnregister";
        case Op::ServerCommands: return "ServerCommands";
        case Op::Toast: return "Toast";
        case Op::Title: return "Title";
        case Op::ChatHistory: return "ChatHistory";
        case Op::TitleState: return "TitleState";
        case Op::SwingArm: return "SwingArm";
        case Op::UseItem: return "UseItem";
        case Op::UseOnBlock: return "UseOnBlock";
        case Op::AttackEntity: return "AttackEntity";
        case Op::InteractEntity: return "InteractEntity";
        case Op::BreakBlock: return "BreakBlock";
        case Op::Look: return "Look";
        case Op::Respawn: return "Respawn";
        case Op::Disconnect: return "Disconnect";
        case Op::PlaySound: return "PlaySound";
        case Op::Jump: return "Jump";
        case Op::UiSubmit: return "UiSubmit";
        case Op::UiClear: return "UiClear";
        case Op::UiMeasure: return "UiMeasure";
        case Op::GfxTextureCreate: return "GfxTextureCreate";
        case Op::GfxTextureUpdate: return "GfxTextureUpdate";
        case Op::GfxTextureDestroy: return "GfxTextureDestroy";
        case Op::GfxSubmit: return "GfxSubmit";
        case Op::GfxClear: return "GfxClear";
        case Op::GfxDisplay: return "GfxDisplay";
        case Op::GfxImageLoad: return "GfxImageLoad";
        case Op::InputGrab: return "InputGrab";
        case Op::NativeInfo: return "NativeInfo";
        case Op::StorageRead: return "StorageRead";
        case Op::StorageWrite: return "StorageWrite";
        case Op::StorageList: return "StorageList";
        case Op::StorageRemove: return "StorageRemove";
        case Op::StorageExists: return "StorageExists";
        case Op::StorageMakeDir: return "StorageMakeDir";
        case Op::HttpRequest: return "HttpRequest";
        case Op::PacketSend: return "PacketSend";
        case Op::PacketName: return "PacketName";
        case Op::SettingGet: return "SettingGet";
        case Op::SettingSet: return "SettingSet";
        case Op::SettingList: return "SettingList";
        case Op::ModList: return "ModList";
        case Op::IconSet: return "IconSet";
        case Op::OpenScreen: return "OpenScreen";
        case Op::OpenModSettings: return "OpenModSettings";
        case Op::TakeScreenshot: return "TakeScreenshot";
        case Op::CyclePerspective: return "CyclePerspective";
        case Op::CopyToClipboard: return "CopyToClipboard";
        case Op::OpenUrl: return "OpenUrl";
        case Op::ToggleHud: return "ToggleHud";
        case Op::CopyFile: return "CopyFile";
        case Op::MoveFile: return "MoveFile";
        case Op::DownloadFile: return "DownloadFile";
        case Op::CameraControl: return "CameraControl";
        case Op::CameraState: return "CameraState";
        case Op::WorldToScreen: return "WorldToScreen";
        case Op::PlayerMotion: return "PlayerMotion";
        case Op::PlayerTeleport: return "PlayerTeleport";
        case Op::PlayerModelPose: return "PlayerModelPose";
        case Op::PlayerSetLook: return "PlayerSetLook";
        case Op::WorldLines: return "WorldLines";
        case Op::WorldBoxes: return "WorldBoxes";
        case Op::ClientBlockSet: return "ClientBlockSet";
        case Op::ClientBlockGet: return "ClientBlockGet";
        case Op::PacketDrop: return "PacketDrop";
        case Op::PacketCounters: return "PacketCounters";
        case Op::PacketInject: return "PacketInject";
        case Op::HudElementSet: return "HudElementSet";
        case Op::DebugLines: return "DebugLines";
        case Op::FogSet: return "FogSet";
        case Op::ScreenTint: return "ScreenTint";
        case Op::EntityRenderOverride: return "EntityRenderOverride";
        case Op::WorldMesh: return "WorldMesh";
        case Op::WorldMeshClear: return "WorldMeshClear";
        case Op::FrameLimit: return "FrameLimit";
        case Op::WindowState: return "WindowState";
        case Op::ScreenButtonAdd: return "ScreenButtonAdd";
        case Op::ScreenButtonRemove: return "ScreenButtonRemove";
        case Op::ScreenOpenCustom: return "ScreenOpenCustom";
        case Op::TooltipAppend: return "TooltipAppend";
        case Op::LoadingOverlay: return "LoadingOverlay";
        case Op::SkinOverride: return "SkinOverride";
        case Op::AmbientSound: return "AmbientSound";
        case Op::ServerPing: return "ServerPing";
        case Op::ModSend: return "ModSend";
        case Op::PackRegister: return "PackRegister";
        case Op::ShaderPackSet: return "ShaderPackSet";
        case Op::ParticleSpawn: return "ParticleSpawn";
        case Op::KeyRegisterEx: return "KeyRegisterEx";
        case Op::Broadcast: return "Broadcast";
        case Op::PlayerMessage: return "PlayerMessage";
        case Op::Kick: return "Kick";
        case Op::OnlinePlayers: return "OnlinePlayers";
        case Op::Teleport: return "Teleport";
        case Op::SetBlock: return "SetBlock";
        case Op::BlockName: return "BlockName";
        case Op::ServerInfo: return "ServerInfo";
        case Op::GiveItem: return "GiveItem";
        case Op::PlayerInfo: return "PlayerInfo";
        case Op::PlayerSetGameMode: return "PlayerSetGameMode";
        case Op::PlayerSetHealth: return "PlayerSetHealth";
        case Op::PlayerDamage: return "PlayerDamage";
        case Op::PlayerKill: return "PlayerKill";
        case Op::PlayerSetFood: return "PlayerSetFood";
        case Op::PlayerAddExperience: return "PlayerAddExperience";
        case Op::PlayerEffectAdd: return "PlayerEffectAdd";
        case Op::PlayerEffectRemove: return "PlayerEffectRemove";
        case Op::PlayerInventory: return "PlayerInventory";
        case Op::PlayerSetSlot: return "PlayerSetSlot";
        case Op::PlayerClearInventory: return "PlayerClearInventory";
        case Op::PlayerSetVelocity: return "PlayerSetVelocity";
        case Op::PlayerTransferDimension: return "PlayerTransferDimension";
        case Op::PlayerTitle: return "PlayerTitle";
        case Op::PlayerActionBar: return "PlayerActionBar";
        case Op::PlayerSound: return "PlayerSound";
        case Op::PlayerTransfer: return "PlayerTransfer";
        case Op::PlayerSetOperator: return "PlayerSetOperator";
        case Op::PlayerForm: return "PlayerForm";
        case Op::PlayerRunCommand: return "PlayerRunCommand";
        case Op::PlayerSetSpawn: return "PlayerSetSpawn";
        case Op::PlayerByName: return "PlayerByName";
        case Op::BlockState: return "BlockState";
        case Op::BlockStateSet: return "BlockStateSet";
        case Op::Fill: return "Fill";
        case Op::TimeGet: return "TimeGet";
        case Op::TimeSet: return "TimeSet";
        case Op::WeatherSet: return "WeatherSet";
        case Op::GameRuleGet: return "GameRuleGet";
        case Op::GameRuleSet: return "GameRuleSet";
        case Op::DifficultySet: return "DifficultySet";
        case Op::Explode: return "Explode";
        case Op::HighestBlock: return "HighestBlock";
        case Op::ParticleAt: return "ParticleAt";
        case Op::DropItem: return "DropItem";
        case Op::WorldSpawnGet: return "WorldSpawnGet";
        case Op::WorldSpawnSet: return "WorldSpawnSet";
        case Op::EntitySpawn: return "EntitySpawn";
        case Op::EntityList: return "EntityList";
        case Op::EntityRemove: return "EntityRemove";
        case Op::EntityDamage: return "EntityDamage";
        case Op::EntityTeleport: return "EntityTeleport";
        case Op::EntitySetVelocity: return "EntitySetVelocity";
        case Op::EntityInfo: return "EntityInfo";
        case Op::EntitySetHealth: return "EntitySetHealth";
        case Op::RunCommand: return "RunCommand";
        case Op::MotdSet: return "MotdSet";
        case Op::LootTableOverride: return "LootTableOverride";
        case Op::LootTableGet: return "LootTableGet";
        case Op::EventAmend: return "EventAmend";
        }
        return "?";
    }

    constexpr std::string_view NameOf(Event event) {
        switch (event) {
        case Event::Load: return "Load";
        case Event::Unload: return "Unload";
        case Event::Tick: return "Tick";
        case Event::Frame: return "Frame";
        case Event::ChatReceived: return "ChatReceived";
        case Event::ChatSending: return "ChatSending";
        case Event::Connected: return "Connected";
        case Event::Disconnected: return "Disconnected";
        case Event::Key: return "Key";
        case Event::Command: return "Command";
        case Event::Damage: return "Damage";
        case Event::Death: return "Death";
        case Event::Respawned: return "Respawned";
        case Event::DimensionChanged: return "DimensionChanged";
        case Event::ScreenChanged: return "ScreenChanged";
        case Event::SlotChanged: return "SlotChanged";
        case Event::EntityAdded: return "EntityAdded";
        case Event::EntityRemoved: return "EntityRemoved";
        case Event::Sound: return "Sound";
        case Event::HttpResponse: return "HttpResponse";
        case Event::Packet: return "Packet";
        case Event::Title: return "Title";
        case Event::SettingChanged: return "SettingChanged";
        case Event::Look: return "Look";
        case Event::MouseMove: return "MouseMove";
        case Event::MouseButton: return "MouseButton";
        case Event::MouseWheel: return "MouseWheel";
        case Event::KeyRaw: return "KeyRaw";
        case Event::TextInput: return "TextInput";
        case Event::ScreenRender: return "ScreenRender";
        case Event::SlotClick: return "SlotClick";
        case Event::Interact: return "Interact";
        case Event::ChunkLoaded: return "ChunkLoaded";
        case Event::ChunkUnloaded: return "ChunkUnloaded";
        case Event::BlockChanged: return "BlockChanged";
        case Event::ResourcesReloaded: return "ResourcesReloaded";
        case Event::LevelLoaded: return "LevelLoaded";
        case Event::LevelUnloaded: return "LevelUnloaded";
        case Event::ModsLoaded: return "ModsLoaded";
        case Event::ClientStopping: return "ClientStopping";
        case Event::Toast: return "Toast";
        case Event::ModMessage: return "ModMessage";
        case Event::Tooltip: return "Tooltip";
        case Event::ScreenButton: return "ScreenButton";
        case Event::WindowFocus: return "WindowFocus";
        case Event::ServerPing: return "ServerPing";
        case Event::PlayerJoin: return "PlayerJoin";
        case Event::PlayerQuit: return "PlayerQuit";
        case Event::PlayerChat: return "PlayerChat";
        case Event::BlockBreak: return "BlockBreak";
        case Event::BlockPlace: return "BlockPlace";
        case Event::PlayerDamage: return "PlayerDamage";
        case Event::PlayerDeath: return "PlayerDeath";
        case Event::PlayerRespawn: return "PlayerRespawn";
        case Event::PlayerUseBlock: return "PlayerUseBlock";
        case Event::PlayerAttack: return "PlayerAttack";
        case Event::EntityDamage: return "EntityDamage";
        case Event::EntityDeath: return "EntityDeath";
        case Event::EntitySpawn: return "EntitySpawn";
        case Event::ItemConsume: return "ItemConsume";
        case Event::GameModeChange: return "GameModeChange";
        case Event::FormResponse: return "FormResponse";
        case Event::PlayerMove: return "PlayerMove";
        case Event::Explosion: return "Explosion";
        case Event::ItemDrop: return "ItemDrop";
        case Event::ItemPickup: return "ItemPickup";
        case Event::ContainerOpen: return "ContainerOpen";
        case Event::DimensionChange: return "DimensionChange";
        case Event::ServerCommand: return "ServerCommand";
        case Event::PreLogin: return "PreLogin";
        }
        return "?";
    }

    constexpr std::string_view SnakeName(Event event) {
        switch (event) {
        case Event::Load: return "load";
        case Event::Unload: return "unload";
        case Event::Tick: return "tick";
        case Event::Frame: return "frame";
        case Event::ChatReceived: return "chat_received";
        case Event::ChatSending: return "chat_sending";
        case Event::Connected: return "connected";
        case Event::Disconnected: return "disconnected";
        case Event::Key: return "key";
        case Event::Command: return "command";
        case Event::Damage: return "damage";
        case Event::Death: return "death";
        case Event::Respawned: return "respawned";
        case Event::DimensionChanged: return "dimension_changed";
        case Event::ScreenChanged: return "screen_changed";
        case Event::SlotChanged: return "slot_changed";
        case Event::EntityAdded: return "entity_added";
        case Event::EntityRemoved: return "entity_removed";
        case Event::Sound: return "sound";
        case Event::HttpResponse: return "http_response";
        case Event::Packet: return "packet";
        case Event::Title: return "title";
        case Event::SettingChanged: return "setting_changed";
        case Event::Look: return "look";
        case Event::MouseMove: return "mouse_move";
        case Event::MouseButton: return "mouse_button";
        case Event::MouseWheel: return "mouse_wheel";
        case Event::KeyRaw: return "key_raw";
        case Event::TextInput: return "text_input";
        case Event::ScreenRender: return "screen_render";
        case Event::SlotClick: return "slot_click";
        case Event::Interact: return "interact";
        case Event::ChunkLoaded: return "chunk_loaded";
        case Event::ChunkUnloaded: return "chunk_unloaded";
        case Event::BlockChanged: return "block_changed";
        case Event::ResourcesReloaded: return "resources_reloaded";
        case Event::LevelLoaded: return "level_loaded";
        case Event::LevelUnloaded: return "level_unloaded";
        case Event::ModsLoaded: return "mods_loaded";
        case Event::ClientStopping: return "client_stopping";
        case Event::Toast: return "toast";
        case Event::ModMessage: return "mod_message";
        case Event::Tooltip: return "tooltip";
        case Event::ScreenButton: return "screen_button";
        case Event::WindowFocus: return "window_focus";
        case Event::ServerPing: return "server_ping";
        case Event::PlayerJoin: return "player_join";
        case Event::PlayerQuit: return "player_quit";
        case Event::PlayerChat: return "player_chat";
        case Event::BlockBreak: return "block_break";
        case Event::BlockPlace: return "block_place";
        case Event::PlayerDamage: return "player_damage";
        case Event::PlayerDeath: return "player_death";
        case Event::PlayerRespawn: return "player_respawn";
        case Event::PlayerUseBlock: return "player_use_block";
        case Event::PlayerAttack: return "player_attack";
        case Event::EntityDamage: return "entity_damage";
        case Event::EntityDeath: return "entity_death";
        case Event::EntitySpawn: return "entity_spawn";
        case Event::ItemConsume: return "item_consume";
        case Event::GameModeChange: return "game_mode_change";
        case Event::FormResponse: return "form_response";
        case Event::PlayerMove: return "player_move";
        case Event::Explosion: return "explosion";
        case Event::ItemDrop: return "item_drop";
        case Event::ItemPickup: return "item_pickup";
        case Event::ContainerOpen: return "container_open";
        case Event::DimensionChange: return "dimension_change";
        case Event::ServerCommand: return "server_command";
        case Event::PreLogin: return "pre_login";
        }
        return {};
    }

    constexpr std::string_view SnakeName(Op op) {
        switch (op) {
        case Op::Log: return "log";
        case Op::Meta: return "meta";
        case Op::Clock: return "clock";
        case Op::PlayerState: return "player_state";
        case Op::PlayerEffects: return "player_effects";
        case Op::PlayerLook: return "player_look";
        case Op::Inventory: return "inventory";
        case Op::InventorySelect: return "inventory_select";
        case Op::InventoryDrop: return "inventory_drop";
        case Op::InventorySwap: return "inventory_swap";
        case Op::InventoryClick: return "inventory_click";
        case Op::InventoryOpen: return "inventory_open";
        case Op::InventoryClose: return "inventory_close";
        case Op::WorldState: return "world_state";
        case Op::Block: return "block";
        case Op::Region: return "region";
        case Op::Raycast: return "raycast";
        case Op::Aim: return "aim";
        case Op::FindBlocks: return "find_blocks";
        case Op::ChunkLoaded: return "chunk_loaded";
        case Op::BlockNames: return "block_names";
        case Op::BlockProps: return "block_props";
        case Op::BlockEntityData: return "block_entity_data";
        case Op::DisplayName: return "display_name";
        case Op::BreakProgress: return "break_progress";
        case Op::MapChunk: return "map_chunk";
        case Op::Entities: return "entities";
        case Op::EntityById: return "entity_by_id";
        case Op::PlayerList: return "player_list";
        case Op::Options: return "options";
        case Op::OptionGet: return "option_get";
        case Op::OptionSet: return "option_set";
        case Op::Screen: return "screen";
        case Op::Translate: return "translate";
        case Op::ReloadResources: return "reload_resources";
        case Op::PerspectiveGet: return "perspective_get";
        case Op::PerspectiveSet: return "perspective_set";
        case Op::ResourcePacks: return "resource_packs";
        case Op::InputDown: return "input_down";
        case Op::InputOverride: return "input_override";
        case Op::KeyRegister: return "key_register";
        case Op::InputActions: return "input_actions";
        case Op::MousePosition: return "mouse_position";
        case Op::Chat: return "chat";
        case Op::Command: return "command";
        case Op::LocalMessage: return "local_message";
        case Op::CommandRegister: return "command_register";
        case Op::CommandUnregister: return "command_unregister";
        case Op::ServerCommands: return "server_commands";
        case Op::Toast: return "toast";
        case Op::Title: return "title";
        case Op::ChatHistory: return "chat_history";
        case Op::TitleState: return "title_state";
        case Op::SwingArm: return "swing_arm";
        case Op::UseItem: return "use_item";
        case Op::UseOnBlock: return "use_on_block";
        case Op::AttackEntity: return "attack_entity";
        case Op::InteractEntity: return "interact_entity";
        case Op::BreakBlock: return "break_block";
        case Op::Look: return "look";
        case Op::Respawn: return "respawn";
        case Op::Disconnect: return "disconnect";
        case Op::PlaySound: return "play_sound";
        case Op::Jump: return "jump";
        case Op::UiSubmit: return "ui_submit";
        case Op::UiClear: return "ui_clear";
        case Op::UiMeasure: return "ui_measure";
        case Op::GfxTextureCreate: return "gfx_texture_create";
        case Op::GfxTextureUpdate: return "gfx_texture_update";
        case Op::GfxTextureDestroy: return "gfx_texture_destroy";
        case Op::GfxSubmit: return "gfx_submit";
        case Op::GfxClear: return "gfx_clear";
        case Op::GfxDisplay: return "gfx_display";
        case Op::GfxImageLoad: return "gfx_image_load";
        case Op::InputGrab: return "input_grab";
        case Op::NativeInfo: return "native_info";
        case Op::StorageRead: return "storage_read";
        case Op::StorageWrite: return "storage_write";
        case Op::StorageList: return "storage_list";
        case Op::StorageRemove: return "storage_remove";
        case Op::StorageExists: return "storage_exists";
        case Op::StorageMakeDir: return "storage_make_dir";
        case Op::HttpRequest: return "http_request";
        case Op::PacketSend: return "packet_send";
        case Op::PacketName: return "packet_name";
        case Op::SettingGet: return "setting_get";
        case Op::SettingSet: return "setting_set";
        case Op::SettingList: return "setting_list";
        case Op::ModList: return "mod_list";
        case Op::IconSet: return "icon_set";
        case Op::OpenScreen: return "open_screen";
        case Op::OpenModSettings: return "open_mod_settings";
        case Op::TakeScreenshot: return "take_screenshot";
        case Op::CyclePerspective: return "cycle_perspective";
        case Op::CopyToClipboard: return "copy_to_clipboard";
        case Op::OpenUrl: return "open_url";
        case Op::ToggleHud: return "toggle_hud";
        case Op::CopyFile: return "copy_file";
        case Op::MoveFile: return "move_file";
        case Op::DownloadFile: return "download_file";
        case Op::CameraControl: return "camera_control";
        case Op::CameraState: return "camera_state";
        case Op::WorldToScreen: return "world_to_screen";
        case Op::PlayerMotion: return "player_motion";
        case Op::PlayerTeleport: return "player_teleport";
        case Op::PlayerModelPose: return "player_model_pose";
        case Op::PlayerSetLook: return "player_set_look";
        case Op::WorldLines: return "world_lines";
        case Op::WorldBoxes: return "world_boxes";
        case Op::ClientBlockSet: return "client_block_set";
        case Op::ClientBlockGet: return "client_block_get";
        case Op::PacketDrop: return "packet_drop";
        case Op::PacketCounters: return "packet_counters";
        case Op::PacketInject: return "packet_inject";
        case Op::HudElementSet: return "hud_element_set";
        case Op::DebugLines: return "debug_lines";
        case Op::FogSet: return "fog_set";
        case Op::ScreenTint: return "screen_tint";
        case Op::EntityRenderOverride: return "entity_render_override";
        case Op::WorldMesh: return "world_mesh";
        case Op::WorldMeshClear: return "world_mesh_clear";
        case Op::FrameLimit: return "frame_limit";
        case Op::WindowState: return "window_state";
        case Op::ScreenButtonAdd: return "screen_button_add";
        case Op::ScreenButtonRemove: return "screen_button_remove";
        case Op::ScreenOpenCustom: return "screen_open_custom";
        case Op::TooltipAppend: return "tooltip_append";
        case Op::LoadingOverlay: return "loading_overlay";
        case Op::SkinOverride: return "skin_override";
        case Op::AmbientSound: return "ambient_sound";
        case Op::ServerPing: return "server_ping";
        case Op::ModSend: return "mod_send";
        case Op::PackRegister: return "pack_register";
        case Op::ShaderPackSet: return "shader_pack_set";
        case Op::ParticleSpawn: return "particle_spawn";
        case Op::KeyRegisterEx: return "key_register_ex";
        case Op::Broadcast: return "broadcast";
        case Op::PlayerMessage: return "player_message";
        case Op::Kick: return "kick";
        case Op::OnlinePlayers: return "online_players";
        case Op::Teleport: return "teleport";
        case Op::SetBlock: return "set_block";
        case Op::BlockName: return "block_name";
        case Op::ServerInfo: return "server_info";
        case Op::GiveItem: return "give_item";
        case Op::PlayerInfo: return "player_info";
        case Op::PlayerSetGameMode: return "player_set_game_mode";
        case Op::PlayerSetHealth: return "player_set_health";
        case Op::PlayerDamage: return "player_damage";
        case Op::PlayerKill: return "player_kill";
        case Op::PlayerSetFood: return "player_set_food";
        case Op::PlayerAddExperience: return "player_add_experience";
        case Op::PlayerEffectAdd: return "player_effect_add";
        case Op::PlayerEffectRemove: return "player_effect_remove";
        case Op::PlayerInventory: return "player_inventory";
        case Op::PlayerSetSlot: return "player_set_slot";
        case Op::PlayerClearInventory: return "player_clear_inventory";
        case Op::PlayerSetVelocity: return "player_set_velocity";
        case Op::PlayerTransferDimension: return "player_transfer_dimension";
        case Op::PlayerTitle: return "player_title";
        case Op::PlayerActionBar: return "player_action_bar";
        case Op::PlayerSound: return "player_sound";
        case Op::PlayerTransfer: return "player_transfer";
        case Op::PlayerSetOperator: return "player_set_operator";
        case Op::PlayerForm: return "player_form";
        case Op::PlayerRunCommand: return "player_run_command";
        case Op::PlayerSetSpawn: return "player_set_spawn";
        case Op::PlayerByName: return "player_by_name";
        case Op::BlockState: return "block_state";
        case Op::BlockStateSet: return "block_state_set";
        case Op::Fill: return "fill";
        case Op::TimeGet: return "time_get";
        case Op::TimeSet: return "time_set";
        case Op::WeatherSet: return "weather_set";
        case Op::GameRuleGet: return "game_rule_get";
        case Op::GameRuleSet: return "game_rule_set";
        case Op::DifficultySet: return "difficulty_set";
        case Op::Explode: return "explode";
        case Op::HighestBlock: return "highest_block";
        case Op::ParticleAt: return "particle_at";
        case Op::DropItem: return "drop_item";
        case Op::WorldSpawnGet: return "world_spawn_get";
        case Op::WorldSpawnSet: return "world_spawn_set";
        case Op::EntitySpawn: return "entity_spawn";
        case Op::EntityList: return "entity_list";
        case Op::EntityRemove: return "entity_remove";
        case Op::EntityDamage: return "entity_damage";
        case Op::EntityTeleport: return "entity_teleport";
        case Op::EntitySetVelocity: return "entity_set_velocity";
        case Op::EntityInfo: return "entity_info";
        case Op::EntitySetHealth: return "entity_set_health";
        case Op::RunCommand: return "run_command";
        case Op::MotdSet: return "motd_set";
        case Op::LootTableOverride: return "loot_table_override";
        case Op::LootTableGet: return "loot_table_get";
        case Op::EventAmend: return "event_amend";
        }
        return {};
    }

    constexpr bool Cancellable(Event event) {
        switch (event) {
        case Event::Load: return false;
        case Event::Unload: return false;
        case Event::Tick: return false;
        case Event::Frame: return false;
        case Event::ChatReceived: return true;
        case Event::ChatSending: return true;
        case Event::Connected: return false;
        case Event::Disconnected: return false;
        case Event::Key: return false;
        case Event::Command: return false;
        case Event::Damage: return false;
        case Event::Death: return false;
        case Event::Respawned: return false;
        case Event::DimensionChanged: return false;
        case Event::ScreenChanged: return false;
        case Event::SlotChanged: return false;
        case Event::EntityAdded: return false;
        case Event::EntityRemoved: return false;
        case Event::Sound: return true;
        case Event::HttpResponse: return false;
        case Event::Packet: return true;
        case Event::Title: return true;
        case Event::SettingChanged: return false;
        case Event::Look: return true;
        case Event::MouseMove: return false;
        case Event::MouseButton: return true;
        case Event::MouseWheel: return true;
        case Event::KeyRaw: return true;
        case Event::TextInput: return true;
        case Event::ScreenRender: return false;
        case Event::SlotClick: return true;
        case Event::Interact: return true;
        case Event::ChunkLoaded: return false;
        case Event::ChunkUnloaded: return false;
        case Event::BlockChanged: return false;
        case Event::ResourcesReloaded: return false;
        case Event::LevelLoaded: return false;
        case Event::LevelUnloaded: return false;
        case Event::ModsLoaded: return false;
        case Event::ClientStopping: return false;
        case Event::Toast: return true;
        case Event::ModMessage: return false;
        case Event::Tooltip: return false;
        case Event::ScreenButton: return false;
        case Event::WindowFocus: return false;
        case Event::ServerPing: return false;
        case Event::PlayerJoin: return false;
        case Event::PlayerQuit: return false;
        case Event::PlayerChat: return true;
        case Event::BlockBreak: return true;
        case Event::BlockPlace: return true;
        case Event::PlayerDamage: return true;
        case Event::PlayerDeath: return false;
        case Event::PlayerRespawn: return false;
        case Event::PlayerUseBlock: return true;
        case Event::PlayerAttack: return true;
        case Event::EntityDamage: return true;
        case Event::EntityDeath: return false;
        case Event::EntitySpawn: return true;
        case Event::ItemConsume: return true;
        case Event::GameModeChange: return false;
        case Event::FormResponse: return false;
        case Event::PlayerMove: return false;
        case Event::Explosion: return true;
        case Event::ItemDrop: return true;
        case Event::ItemPickup: return true;
        case Event::ContainerOpen: return true;
        case Event::DimensionChange: return false;
        case Event::ServerCommand: return true;
        case Event::PreLogin: return true;
        }
        return false;
    }

    inline constexpr std::array<Op, 196> kAllOps = {
        Op::Log,
        Op::Meta,
        Op::Clock,
        Op::PlayerState,
        Op::PlayerEffects,
        Op::PlayerLook,
        Op::Inventory,
        Op::InventorySelect,
        Op::InventoryDrop,
        Op::InventorySwap,
        Op::InventoryClick,
        Op::InventoryOpen,
        Op::InventoryClose,
        Op::WorldState,
        Op::Block,
        Op::Region,
        Op::Raycast,
        Op::Aim,
        Op::FindBlocks,
        Op::ChunkLoaded,
        Op::BlockNames,
        Op::BlockProps,
        Op::BlockEntityData,
        Op::DisplayName,
        Op::BreakProgress,
        Op::MapChunk,
        Op::Entities,
        Op::EntityById,
        Op::PlayerList,
        Op::Options,
        Op::OptionGet,
        Op::OptionSet,
        Op::Screen,
        Op::Translate,
        Op::ReloadResources,
        Op::PerspectiveGet,
        Op::PerspectiveSet,
        Op::ResourcePacks,
        Op::InputDown,
        Op::InputOverride,
        Op::KeyRegister,
        Op::InputActions,
        Op::MousePosition,
        Op::Chat,
        Op::Command,
        Op::LocalMessage,
        Op::CommandRegister,
        Op::CommandUnregister,
        Op::ServerCommands,
        Op::Toast,
        Op::Title,
        Op::ChatHistory,
        Op::TitleState,
        Op::SwingArm,
        Op::UseItem,
        Op::UseOnBlock,
        Op::AttackEntity,
        Op::InteractEntity,
        Op::BreakBlock,
        Op::Look,
        Op::Respawn,
        Op::Disconnect,
        Op::PlaySound,
        Op::Jump,
        Op::UiSubmit,
        Op::UiClear,
        Op::UiMeasure,
        Op::GfxTextureCreate,
        Op::GfxTextureUpdate,
        Op::GfxTextureDestroy,
        Op::GfxSubmit,
        Op::GfxClear,
        Op::GfxDisplay,
        Op::GfxImageLoad,
        Op::InputGrab,
        Op::NativeInfo,
        Op::StorageRead,
        Op::StorageWrite,
        Op::StorageList,
        Op::StorageRemove,
        Op::StorageExists,
        Op::StorageMakeDir,
        Op::HttpRequest,
        Op::PacketSend,
        Op::PacketName,
        Op::SettingGet,
        Op::SettingSet,
        Op::SettingList,
        Op::ModList,
        Op::IconSet,
        Op::OpenScreen,
        Op::OpenModSettings,
        Op::TakeScreenshot,
        Op::CyclePerspective,
        Op::CopyToClipboard,
        Op::OpenUrl,
        Op::ToggleHud,
        Op::CopyFile,
        Op::MoveFile,
        Op::DownloadFile,
        Op::CameraControl,
        Op::CameraState,
        Op::WorldToScreen,
        Op::PlayerMotion,
        Op::PlayerTeleport,
        Op::PlayerModelPose,
        Op::PlayerSetLook,
        Op::WorldLines,
        Op::WorldBoxes,
        Op::ClientBlockSet,
        Op::ClientBlockGet,
        Op::PacketDrop,
        Op::PacketCounters,
        Op::PacketInject,
        Op::HudElementSet,
        Op::DebugLines,
        Op::FogSet,
        Op::ScreenTint,
        Op::EntityRenderOverride,
        Op::WorldMesh,
        Op::WorldMeshClear,
        Op::FrameLimit,
        Op::WindowState,
        Op::ScreenButtonAdd,
        Op::ScreenButtonRemove,
        Op::ScreenOpenCustom,
        Op::TooltipAppend,
        Op::LoadingOverlay,
        Op::SkinOverride,
        Op::AmbientSound,
        Op::ServerPing,
        Op::ModSend,
        Op::PackRegister,
        Op::ShaderPackSet,
        Op::ParticleSpawn,
        Op::KeyRegisterEx,
        Op::Broadcast,
        Op::PlayerMessage,
        Op::Kick,
        Op::OnlinePlayers,
        Op::Teleport,
        Op::SetBlock,
        Op::BlockName,
        Op::ServerInfo,
        Op::GiveItem,
        Op::PlayerInfo,
        Op::PlayerSetGameMode,
        Op::PlayerSetHealth,
        Op::PlayerDamage,
        Op::PlayerKill,
        Op::PlayerSetFood,
        Op::PlayerAddExperience,
        Op::PlayerEffectAdd,
        Op::PlayerEffectRemove,
        Op::PlayerInventory,
        Op::PlayerSetSlot,
        Op::PlayerClearInventory,
        Op::PlayerSetVelocity,
        Op::PlayerTransferDimension,
        Op::PlayerTitle,
        Op::PlayerActionBar,
        Op::PlayerSound,
        Op::PlayerTransfer,
        Op::PlayerSetOperator,
        Op::PlayerForm,
        Op::PlayerRunCommand,
        Op::PlayerSetSpawn,
        Op::PlayerByName,
        Op::BlockState,
        Op::BlockStateSet,
        Op::Fill,
        Op::TimeGet,
        Op::TimeSet,
        Op::WeatherSet,
        Op::GameRuleGet,
        Op::GameRuleSet,
        Op::DifficultySet,
        Op::Explode,
        Op::HighestBlock,
        Op::ParticleAt,
        Op::DropItem,
        Op::WorldSpawnGet,
        Op::WorldSpawnSet,
        Op::EntitySpawn,
        Op::EntityList,
        Op::EntityRemove,
        Op::EntityDamage,
        Op::EntityTeleport,
        Op::EntitySetVelocity,
        Op::EntityInfo,
        Op::EntitySetHealth,
        Op::RunCommand,
        Op::MotdSet,
        Op::LootTableOverride,
        Op::LootTableGet,
        Op::EventAmend,
    };
    inline constexpr std::array<Event, 70> kAllEvents = {
        Event::Load,
        Event::Unload,
        Event::Tick,
        Event::Frame,
        Event::ChatReceived,
        Event::ChatSending,
        Event::Connected,
        Event::Disconnected,
        Event::Key,
        Event::Command,
        Event::Damage,
        Event::Death,
        Event::Respawned,
        Event::DimensionChanged,
        Event::ScreenChanged,
        Event::SlotChanged,
        Event::EntityAdded,
        Event::EntityRemoved,
        Event::Sound,
        Event::HttpResponse,
        Event::Packet,
        Event::Title,
        Event::SettingChanged,
        Event::Look,
        Event::MouseMove,
        Event::MouseButton,
        Event::MouseWheel,
        Event::KeyRaw,
        Event::TextInput,
        Event::ScreenRender,
        Event::SlotClick,
        Event::Interact,
        Event::ChunkLoaded,
        Event::ChunkUnloaded,
        Event::BlockChanged,
        Event::ResourcesReloaded,
        Event::LevelLoaded,
        Event::LevelUnloaded,
        Event::ModsLoaded,
        Event::ClientStopping,
        Event::Toast,
        Event::ModMessage,
        Event::Tooltip,
        Event::ScreenButton,
        Event::WindowFocus,
        Event::ServerPing,
        Event::PlayerJoin,
        Event::PlayerQuit,
        Event::PlayerChat,
        Event::BlockBreak,
        Event::BlockPlace,
        Event::PlayerDamage,
        Event::PlayerDeath,
        Event::PlayerRespawn,
        Event::PlayerUseBlock,
        Event::PlayerAttack,
        Event::EntityDamage,
        Event::EntityDeath,
        Event::EntitySpawn,
        Event::ItemConsume,
        Event::GameModeChange,
        Event::FormResponse,
        Event::PlayerMove,
        Event::Explosion,
        Event::ItemDrop,
        Event::ItemPickup,
        Event::ContainerOpen,
        Event::DimensionChange,
        Event::ServerCommand,
        Event::PreLogin,
    };
}
