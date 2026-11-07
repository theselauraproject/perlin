#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perlin/meta.hpp"

namespace perlin::abi {
    inline constexpr uint32_t kSchemaVersion = 3;
    inline constexpr uint32_t kApiVersion = 1;
    inline constexpr int32_t kNeedMoreBase = 16;
    inline constexpr uint32_t kEventCancel = 1;
    inline constexpr uint32_t kGfxIndex32 = 1;
    inline constexpr uint32_t kGfxMaxBytes = 8388608;
    inline constexpr uint32_t kGfxMaxVertices = 1048576;
    inline constexpr uint32_t kGfxMaxLayers = 8;
    inline constexpr int32_t kGfxScreenZ = 100;
    inline constexpr uint32_t kGfxMaxTextures = 64;
    inline constexpr uint32_t kGfxMaxTextureSide = 4096;
    inline constexpr uint64_t kGfxMaxTextureBytes = 134217728;

    enum class Status : int32_t {
        Ok = 0,
        UnknownOp = -1,
        Denied = -2,
        BadArguments = -3,
        Unavailable = -4,
        NotFound = -5,
        Failed = -6,
    };

    enum class Permission : uint32_t {
        None = 0,
        Network = uint32_t{1} << 0,
        Packets = uint32_t{1} << 1,
        Control = uint32_t{1} << 2,
        Storage = uint32_t{1} << 3,
        Files = uint32_t{1} << 4,
        Native = uint32_t{1} << 5,
        World = uint32_t{1} << 6,
        Players = uint32_t{1} << 7,
        Admin = uint32_t{1} << 8,
    };

    enum class Draw : uint8_t {
        Rect = 1,
        Text = 2,
        Item = 3,
        Image = 4,
        Line = 5,
        Gradient = 6,
        Border = 7,
    };

    enum class Screen : int32_t {
        None = 0,
        Pause = 1,
        Chat = 2,
        Inventory = 3,
        Options = 4,
        Mods = 5,
        Wardrobe = 6,
        Death = 7,
        Workbench = 8,
        Menu = 9,
        Other = 10,
        Custom = 11,
    };

    enum class HudElement : int32_t {
        Hearts = 0,
        Armor = 1,
        Hunger = 2,
        Air = 3,
        Hotbar = 4,
        Experience = 5,
        Debug = 6,
        Chat = 7,
        Effects = 8,
        Locator = 9,
        Fire = 10,
        Subtitles = 11,
        Crosshair = 12,
    };

    enum class InteractKind : int32_t {
        StartBreak = 0,
        Attack = 1,
        InteractEntity = 2,
        UseOnBlock = 3,
        UseItem = 4,
    };

    enum class RenderStage : int32_t {
        AfterTerrain = 0,
        AfterEntities = 1,
        Overlay = 2,
    };

    enum class KeyContext : int32_t {
        InGame = 0,
        InScreen = 1,
        Any = 2,
    };

    enum class GameMode : int32_t {
        Survival = 0,
        Creative = 1,
        Adventure = 2,
        SurvivalSpectator = 3,
        CreativeSpectator = 4,
        Default = 5,
        Spectator = 6,
    };

    enum class Dimension : int32_t {
        Overworld = 0,
        Nether = 1,
        End = 2,
    };

    enum class Difficulty : int32_t {
        Peaceful = 0,
        Easy = 1,
        Normal = 2,
        Hard = 3,
    };

    enum class PermissionLevel : int32_t {
        Unknown = -1,
        Visitor = 0,
        Member = 1,
        Operator = 2,
        Custom = 3,
    };

    enum class Face : int32_t {
        Down = 0,
        Up = 1,
        North = 2,
        South = 3,
        West = 4,
        East = 5,
    };

    enum class AimKind : int32_t {
        None = 0,
        Block = 1,
        Entity = 2,
    };

    enum class ChatType : int32_t {
        Raw = 0,
        Chat = 1,
        Translation = 2,
        Popup = 3,
        JukeboxPopup = 4,
        Tip = 5,
        System = 6,
        Whisper = 7,
        Announcement = 8,
        ObjectWhisper = 9,
        Object = 10,
        ObjectAnnouncement = 11,
    };

    enum class DisconnectReason : int32_t {
        Unknown = 0,
        CantConnectNoInternet = 1,
        NoPermissions = 2,
        UnrecoverableError = 3,
        ThirdPartyBlocked = 4,
        ThirdPartyNoInternet = 5,
        ThirdPartyBadIp = 6,
        ThirdPartyNoServerOrServerLocked = 7,
        VersionMismatch = 8,
        SkinIssue = 9,
        InviteSessionNotFound = 10,
    };

    enum class EffectId : int32_t {
        Speed = 1,
        Slowness = 2,
        Haste = 3,
        MiningFatigue = 4,
        Strength = 5,
        InstantHealth = 6,
        InstantDamage = 7,
        JumpBoost = 8,
        Nausea = 9,
        Regeneration = 10,
        Resistance = 11,
        FireResistance = 12,
        WaterBreathing = 13,
        Invisibility = 14,
        Blindness = 15,
        NightVision = 16,
        Hunger = 17,
        Weakness = 18,
        Poison = 19,
        Wither = 20,
        HealthBoost = 21,
        Absorption = 22,
        Saturation = 23,
        Levitation = 24,
        FatalPoison = 25,
        ConduitPower = 26,
        SlowFalling = 27,
        BadOmen = 28,
        VillageHero = 29,
        Darkness = 30,
        TrialOmen = 31,
        RaidOmen = 32,
        WindCharged = 33,
        Weaving = 34,
        Oozing = 35,
        Infested = 36,
    };

    enum class Perspective : int32_t {
        FirstPerson = 0,
        ThirdPersonBack = 1,
        ThirdPersonFront = 2,
    };

    enum class SettingKind : uint8_t {
        Toggle = 0,
        Integer = 1,
        Number = 2,
        Choice = 3,
        Button = 4,
        Label = 5,
        Header = 6,
    };

    enum class Area : uint8_t {
        Mod = 0,
        Screenshots = 1,
        ResourcePacks = 2,
        ShaderPacks = 3,
    };

    enum class EntityFlag : uint8_t {
        OnFire = 0,
        Sneaking = 1,
        Riding = 2,
        Sprinting = 3,
        UsingItem = 4,
        Invisible = 5,
        Tempted = 6,
        InLove = 7,
        Saddled = 8,
        Powered = 9,
        Ignited = 10,
        Baby = 11,
        Converting = 12,
        Critical = 13,
        CanShowName = 14,
        AlwaysShowName = 15,
        NoAi = 16,
        Silent = 17,
        WallClimbing = 18,
        CanClimb = 19,
        CanSwim = 20,
        CanFly = 21,
        CanWalk = 22,
        Resting = 23,
        Sitting = 24,
        Angry = 25,
        Interested = 26,
        Charged = 27,
        Tamed = 28,
        Orphaned = 29,
        Leashed = 30,
        Sheared = 31,
        Gliding = 32,
        Elder = 33,
        Moving = 34,
        Breathing = 35,
        Chested = 36,
        Stackable = 37,
        ShowBase = 38,
        Rearing = 39,
        Vibrating = 40,
        Idling = 41,
        EvokerSpell = 42,
        ChargeAttack = 43,
        WasdControlled = 44,
        CanPowerJump = 45,
        Lingering = 46,
        HasCollision = 47,
        HasGravity = 48,
        FireImmune = 49,
        Dancing = 50,
        Enchanted = 51,
        ReturnTrident = 52,
        ContainerPrivate = 53,
        Transforming = 54,
        DamageNearbyMobs = 55,
        Swimming = 56,
        Bribed = 57,
        Pregnant = 58,
        LayingEgg = 59,
        PassengerCanPick = 60,
        TransitionSitting = 61,
        Eating = 62,
        LayingDown = 63,
    };

    enum class Tool : uint8_t {
        None = 0,
        Pickaxe = 1,
        Axe = 2,
        Shovel = 3,
        Hoe = 4,
        Shears = 5,
        Sword = 6,
    };

    enum class NameKind : uint8_t {
        Item = 0,
        Block = 1,
        Entity = 2,
    };

    enum class DamageCause : int32_t {
        Generic = 0,
        Mob = 1,
        Player = 2,
        Projectile = 3,
        Explosion = 4,
        Fall = 5,
        Void = 6,
        Lava = 7,
        Fire = 8,
        Burning = 9,
        Drowning = 10,
        Starving = 11,
        Suffocation = 12,
        Cactus = 13,
        Magma = 14,
        BerryBush = 15,
        Poison = 16,
        Wither = 17,
        Magic = 18,
        FallingBlock = 19,
        Kill = 20,
        Plugin = 21,
    };

    enum class Weather : int32_t {
        Clear = 0,
        Rain = 1,
        Thunder = 2,
    };

    enum class FormKind : int32_t {
        Message = 0,
        Menu = 1,
        Custom = 2,
    };

    enum class FormElement : int32_t {
        Label = 0,
        Toggle = 1,
        Slider = 2,
        StepSlider = 3,
        Dropdown = 4,
        Input = 5,
        Header = 6,
        Divider = 7,
    };

    enum class LogLevel : int32_t {
        Debug = 0,
        Info = 1,
        Warn = 2,
        Error = 3,
    };

}

template <> struct perlin::meta::enable_flags<perlin::abi::Permission> : std::true_type {};
template <> struct perlin::meta::enum_range<perlin::abi::EffectId> {
    static constexpr int min = 1;
    static constexpr int max = 63;
};
template <> struct perlin::meta::enum_range<perlin::abi::EntityFlag> {
    static constexpr int min = 0;
    static constexpr int max = 63;
};

namespace perlin::abi {
    template <class E>
        requires std::is_enum_v<E>
    constexpr bool abi_enum_tag(E) {
        return true;
    }
}

namespace perlin::meta {
    template <class E>
        requires std::is_enum_v<E>
    struct enable_format<E> : std::bool_constant < requires(E e) {
        abi_enum_tag(e);
    } > {};
}

namespace perlin::abi {
    struct RestText {
        std::string value;
        static constexpr bool wire_rest_tag = true;
        operator const std::string&() const { return value; }
    };
    struct RestBytes {
        std::vector<uint8_t> value;
        static constexpr bool wire_rest_tag = true;
    };

    using Permissions = meta::Flags<Permission>;
    using EntityFlags = meta::IndexSet<EntityFlag, uint64_t>;

    struct Item {
        std::string name;
        int32_t count = 0;
        int32_t aux = 0;
        int32_t damage = 0;
        bool enchanted = false;
        std::string customName;
        std::vector<std::pair<int32_t, int32_t>> enchants;
        std::vector<std::string> lore;
        bool Empty() const { return name.empty() || count <= 0; }
    };

    struct PlayerState {
        uint64_t runtimeId = 0;
        int64_t uniqueId = 0;
        std::string name;
        double x = 0;
        double y = 0;
        double z = 0;
        double vx = 0;
        double vy = 0;
        double vz = 0;
        float yaw = 0.0f;
        float pitch = 0.0f;
        float headYaw = 0.0f;
        float eyeHeight = 1.62f;
        bool onGround = false;
        bool sprinting = false;
        bool sneaking = false;
        bool swimming = false;
        bool gliding = false;
        bool flying = false;
        bool mayFly = false;
        bool spectator = false;
        bool onFire = false;
        bool riding = false;
        bool connected = false;
        GameMode gameMode = GameMode::Survival;
        Dimension dimension = Dimension::Overworld;
        int32_t selectedSlot = 0;
        PermissionLevel permission = PermissionLevel::Unknown;
        float health = 20.0f;
        float maxHealth = 20.0f;
        float absorption = 0.0f;
        float hunger = 20.0f;
        float saturation = 5.0f;
        float xpProgress = 0.0f;
        float walkSpeed = 0.1f;
        int32_t xpLevel = 0;
        int32_t air = 300;
        int32_t maxAir = 300;
    };

    struct Effect {
        EffectId id{};
        int32_t amplifier = 0;
        int32_t ticks = 0;
        bool ambient = false;
        bool particles = true;
    };

    struct Inventory {
        std::array<Item, 36> slots{};
        std::array<Item, 4> armor{};
        Item offhand;
        Item cursor;
        int32_t selected = 0;
        bool open = false;
    };

    struct Attribute {
        std::string name;
        float min = 0.0f;
        float value = 0.0f;
        float max = 0.0f;
    };

    struct Entity {
        uint64_t runtimeId = 0;
        int64_t uniqueId = 0;
        std::string type;
        std::string name;
        std::string nameTag;
        std::string uuid;
        double x = 0;
        double y = 0;
        double z = 0;
        float yaw = 0.0f;
        float pitch = 0.0f;
        float headYaw = 0.0f;
        float health = -1.0f;
        float scale = 1.0f;
        bool onGround = false;
        bool isPlayer = false;
        bool isItem = false;
        bool isSelf = false;
        EntityFlags flags;
        std::string itemName;
        int32_t itemCount = 0;
        std::string mainHand;
        std::string offHand;
        std::array<std::string, 4> armor{};
        int64_t vehicleUid = 0;
        int64_t ownerUid = 0;
        int64_t targetUid = 0;
        int32_t variant = 0;
        int32_t markVariant = 0;
        int32_t color = 0;
        float maxHealth = -1.0f;
        bool dying = false;
        std::vector<Attribute> attributes;
        std::vector<std::pair<std::string, float>> properties;
        std::vector<std::pair<std::string, std::string>> propertyText;
    };

    struct PlayerEntry {
        std::string uuid;
        std::string name;
        int64_t uid = 0;
        PermissionLevel permission = PermissionLevel::Unknown;
    };

    struct WorldState {
        Dimension dimension = Dimension::Overworld;
        Difficulty difficulty = Difficulty::Normal;
        GameMode worldGameMode = GameMode::Survival;
        int64_t day = 0;
        int32_t timeOfDay = 0;
        double totalTicks = 0;
        float rain = 0.0f;
        float thunder = 0.0f;
        bool commandsEnabled = true;
        bool haveSpawn = false;
        int32_t spawnX = 0;
        int32_t spawnY = 0;
        int32_t spawnZ = 0;
        std::vector<std::pair<std::string, int32_t>> rules;
    };

    struct BlockInfo {
        bool loaded = false;
        uint32_t runtimeId = 0;
        std::string name;
        int32_t biome = 0;
        uint32_t liquid = 0;
        std::vector<std::pair<std::string, std::string>> states;
    };

    struct BlockProps {
        bool known = false;
        float hardness = 0.0f;
        Tool tool = Tool::None;
        int32_t harvestLevel = 0;
        bool needsSupportBelow = false;
        bool breaksWholeStack = false;
        std::string displayName;
        std::vector<std::string> properties;
    };

    struct BlockEntityInfo {
        bool present = false;
        std::string id;
        int32_t color = -1;
        bool waxed = false;
        float rotation = 0.0f;
        std::string frontText;
        std::string backText;
        bool frontGlow = false;
        bool backGlow = false;
        int32_t base = -1;
        int32_t bannerType = 0;
        std::vector<std::pair<std::string, int32_t>> patterns;
        std::string nbt;
    };

    struct BreakState {
        bool active = false;
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        float progress = 0.0f;
    };

    struct RayHit {
        bool hit = false;
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        Face face = Face::Up;
        double hx = 0;
        double hy = 0;
        double hz = 0;
        double distance = 0;
        std::string block;
    };

    struct AimState {
        AimKind kind = AimKind::None;
        RayHit block;
        uint64_t entity = 0;
        std::string entityType;
        double distance = 0;
    };

    struct ScreenState {
        int32_t guiWidth = 0;
        int32_t guiHeight = 0;
        int32_t guiScale = 1;
        int32_t framebufferWidth = 0;
        int32_t framebufferHeight = 0;
        float fps = 0.0f;
        float frameMs = 0.0f;
        Screen screen = Screen::None;
        bool paused = false;
        bool connected = false;
        bool hudHidden = false;
        bool debug = false;
        std::string server;
        std::string language;
        std::string platform;
        std::string gameVersion;
    };

    struct Meta {
        uint32_t apiVersion = kApiVersion;
        std::string gameVersion;
        std::string plugin;
        std::string platform;
        Permissions permissions;
        int64_t nowMs = 0;
        uint64_t tick = 0;
    };

    struct InputOverrideState {
        float forward = 0.0f;
        float strafe = 0.0f;
        bool jump = false;
        bool sneak = false;
        bool sprint = false;
        bool attack = false;
        bool use = false;
        int32_t ticks = 1;
    };

    struct CameraControlState {
        float roll = 0.0f;
        float fovAdd = 0.0f;
        float fovSet = 0.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float offsetZ = 0.0f;
        float yawAdd = 0.0f;
        float pitchAdd = 0.0f;
        int32_t ticks = 2;
    };

    struct CameraInfo {
        double x = 0;
        double y = 0;
        double z = 0;
        float yaw = 0.0f;
        float pitch = 0.0f;
        float fov = 70.0f;
        float aspect = 1.78f;
        float nearPlane = 0.1f;
        float farPlane = 600.0f;
        int32_t perspective = 0;
        std::array<float, 16> viewProjection{};
    };

    struct WorldPoint {
        double x = 0;
        double y = 0;
        double z = 0;
    };

    struct ScreenPoint {
        float x = 0.0f;
        float y = 0.0f;
        float depth = 0.0f;
        bool visible = false;
    };

    struct MotionRequest {
        bool add = false;
        double x = 0;
        double y = 0;
        double z = 0;
        bool axisX = true;
        bool axisY = true;
        bool axisZ = true;
    };

    struct TeleportRequest {
        double x = 0;
        double y = 0;
        double z = 0;
        bool keepVelocity = false;
    };

    struct ModelPose {
        float roll = 0.0f;
        float pitch = 0.0f;
        float yaw = 0.0f;
        int32_t ticks = 2;
    };

    struct LookSet {
        double x = 0;
        double y = 0;
        double z = -1;
    };

    struct WorldLine {
        double x1 = 0;
        double y1 = 0;
        double z1 = 0;
        double x2 = 0;
        double y2 = 0;
        double z2 = 0;
        uint32_t color = 0xFFFFFFFFu;
        float width = 1.0f;
    };

    struct WorldLines {
        int32_t ticks = 2;
        std::vector<WorldLine> lines;
    };

    struct WorldBox {
        double x0 = 0;
        double y0 = 0;
        double z0 = 0;
        double x1 = 1;
        double y1 = 1;
        double z1 = 1;
        uint32_t color = 0xFFFFFFFFu;
        float width = 1.0f;
    };

    struct WorldBoxes {
        int32_t ticks = 2;
        std::vector<WorldBox> boxes;
    };

    struct ClientBlockEdit {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        uint32_t runtimeId = 0;
    };

    struct ClientBlockEdits {
        std::vector<ClientBlockEdit> edits;
    };

    struct BlockQuery {
        std::string name;
        std::vector<std::pair<std::string, std::string>> states;
    };

    struct BlockQueryResult {
        bool found = false;
        uint32_t runtimeId = 0;
    };

    struct PacketDropRequest {
        int32_t ticks = 20;
        std::vector<uint32_t> ids;
        bool outgoing = false;
    };

    struct PacketCount {
        uint32_t id = 0;
        uint32_t count = 0;
    };

    struct Region {
        int32_t x0 = 0;
        int32_t y0 = 0;
        int32_t z0 = 0;
        int32_t sx = 0;
        int32_t sy = 0;
        int32_t sz = 0;
        std::vector<std::string> palette;
        std::vector<uint16_t> cells;
        const std::string& at(int x, int y, int z) const { static const std::string empty; if (x < x0 || y < y0 || z < z0 || x >= x0 + sx || y >= y0 + sy || z >= z0 + sz) return empty; const size_t i = (static_cast<size_t>(y - y0) * static_cast<size_t>(sz) + static_cast<size_t>(z - z0)) * static_cast<size_t>(sx) + static_cast<size_t>(x - x0); return i < cells.size() && cells[i] < palette.size() ? palette[cells[i]] : empty; }
    };

    struct FoundBlock {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        std::string name;
    };

    struct SettingDef {
        std::string id;
        SettingKind kind = SettingKind::Toggle;
        std::string label;
        std::string description;
        std::string icon;
        std::string value;
        std::string fallback;
        double min = 0;
        double max = 1;
        double step = 0;
        std::vector<std::string> choices;
        std::vector<std::string> choiceLabels;
        std::string unit;
        bool restart = false;
    };

    struct SettingEntry {
        std::string id;
        std::string value;
    };

    struct FileLocation {
        Area area = Area::Mod;
        std::string path;
    };

    struct ModEntry {
        std::string name;
        std::string displayName;
        std::string description;
        std::string version;
        std::string author;
        std::string state;
        bool enabled = true;
        bool self = false;
        bool hasSettings = false;
        std::string website;
        std::string arch;
        std::vector<std::string> hooks;
        int32_t priority = 0;
        Permissions permissions;
    };

    struct HudElementState {
        HudElement element = HudElement::Hearts;
        bool visible = true;
        float dx = 0.0f;
        float dy = 0.0f;
    };

    struct DebugLinesData {
        std::vector<std::string> left;
        std::vector<std::string> right;
    };

    struct FogOverride {
        bool enabled = true;
        uint32_t color = 0;
        float start = -1.0f;
        float end = -1.0f;
        int32_t ticks = 2;
    };

    struct ScreenTintData {
        uint32_t color = 0;
        float vignette = 0.0f;
        int32_t ticks = 2;
    };

    struct EntityOverride {
        uint64_t entity = 0;
        std::string type;
        bool hidden = false;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        float scaleZ = 1.0f;
        uint32_t tint = 0xFFFFFFFFu;
        std::string texture;
        int32_t nameTag = 0;
        std::string nameText;
        int32_t ticks = 2;
    };

    struct MeshVertex {
        double x = 0;
        double y = 0;
        double z = 0;
        float u = 0.0f;
        float v = 0.0f;
        uint32_t color = 0xFFFFFFFFu;
    };

    struct WorldMeshData {
        std::string id;
        RenderStage stage = RenderStage::AfterEntities;
        uint32_t texture = 0;
        bool depthTest = true;
        int32_t ticks = 2;
        std::vector<MeshVertex> triangles;
    };

    struct FrameLimitData {
        int32_t fps = 0;
        int32_t unfocusedFps = 0;
    };

    struct WindowInfo {
        bool focused = true;
        bool minimized = false;
        int32_t width = 0;
        int32_t height = 0;
    };

    struct ScreenButtonDef {
        Screen screen = Screen::Pause;
        std::string id;
        std::string label;
        int32_t x = 0;
        int32_t y = 0;
        int32_t width = 98;
        int32_t height = 20;
    };

    struct LoadingOverlayData {
        bool hidden = false;
    };

    struct SkinOverrideData {
        std::string player;
        bool slim = false;
        std::vector<uint8_t> png;
    };

    struct AmbientSoundData {
        std::string id;
        std::string sound;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool play = true;
    };

    struct ServerPingRequest {
        std::string host;
        uint16_t port = 19132;
    };

    struct ModMessageData {
        std::string to;
        std::string channel;
        std::vector<uint8_t> body;
    };

    struct PackRegistration {
        std::string path;
        bool enabled = true;
    };

    struct ParticleRequest {
        std::string name;
        double x = 0;
        double y = 0;
        double z = 0;
        int32_t count = 1;
        float spread = 0.0f;
    };

    struct KeyBindEx {
        std::string id;
        std::string label;
        std::string key;
        bool ctrl = false;
        bool shift = false;
        bool alt = false;
        KeyContext context = KeyContext::InGame;
    };

    struct ChatLine {
        std::string text;
        float alpha = 1.0f;
    };

    struct PackEntry {
        std::string name;
        bool server = false;
    };

    struct TitleStateData {
        std::string title;
        std::string subtitle;
        std::string actionBar;
        float titleAlpha = 0.0f;
        float actionBarAlpha = 0.0f;
    };

    struct GfxVertex {
        float x = 0.0f;
        float y = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        uint32_t color = 0xFFFFFFFFu;
    };
    static_assert(sizeof(GfxVertex) == 20);

    struct GfxCmd {
        std::array<float, 4> clip{};
        uint32_t texture = 0;
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;
        uint32_t vertexOffset = 0;
    };
    static_assert(sizeof(GfxCmd) == 32);

    struct GfxHeader {
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        uint32_t commandCount = 0;
        uint32_t flags = 0;
    };
    static_assert(sizeof(GfxHeader) == 16);

    struct NativeHandles {
        std::string api;
        std::string adapter;
        uint64_t window = 0;
        uint64_t display = 0;
        uint64_t device = 0;
    };

    struct GfxDisplayInfo {
        int32_t width = 0;
        int32_t height = 0;
        int32_t guiScale = 1;
        float mouseX = 0.0f;
        float mouseY = 0.0f;
        bool mouseFree = false;
        bool inGame = false;
    };

    struct TextMessage {
        std::string text;
    };

    struct PlayerText {
        uint64_t player = 0;
        std::string text;
    };

    struct OnlinePlayer {
        uint64_t id = 0;
        std::string name;
        double x = 0;
        double y = 0;
        double z = 0;
    };

    struct OnlinePlayerList {
        std::vector<OnlinePlayer> players;
    };

    struct ServerTeleport {
        uint64_t player = 0;
        double x = 0;
        double y = 0;
        double z = 0;
    };

    struct BlockCoord {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
    };

    struct BlockWrite {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        std::string block;
    };

    struct ServerStatus {
        std::string name;
        uint32_t players = 0;
        uint32_t maxPlayers = 0;
        uint64_t tick = 0;
        std::string backend;
    };

    struct GiveItemRequest {
        uint64_t player = 0;
        std::string item;
        uint32_t count = 1;
    };

    struct GiveItemResult {
        uint32_t leftover = 0;
    };

    struct EventNothing {
    };

    struct EventTick {
        float dt = 0.05f;
        uint64_t tick = 0;
    };

    struct EventFrame {
        float dt = 0.0f;
        float fps = 0.0f;
        int32_t guiWidth = 0;
        int32_t guiHeight = 0;
    };

    struct EventChat {
        ChatType type = ChatType::Raw;
        std::string sender;
        std::string message;
        std::string raw;
    };

    struct EventKey {
        std::string id;
        bool down = false;
    };

    struct EventCommand {
        std::string name;
        std::string args;
        uint64_t player = 0;
    };

    struct EventConnection {
        std::string server;
        std::string message;
        DisconnectReason reason = DisconnectReason::Unknown;
    };

    struct EventDamage {
        bool known = false;
        float dx = 0.0f;
        float dz = 0.0f;
        float health = 0.0f;
    };

    struct EventText {
        std::string text;
    };

    struct EventDimension {
        Dimension from{};
        Dimension to{};
    };

    struct EventScreen {
        Screen from{};
        Screen to{};
    };

    struct EventSlot {
        int32_t slot = 0;
        Item item;
    };

    struct EventEntity {
        Entity entity;
    };

    struct EventSound {
        std::string name;
        double x = 0;
        double y = 0;
        double z = 0;
        float volume = 1.0f;
        float pitch = 1.0f;
    };

    struct EventHttp {
        uint32_t id = 0;
        int32_t status = 0;
        std::string error;
        std::vector<std::pair<std::string, std::string>> headers;
        std::vector<uint8_t> body;
    };

    struct EventPacket {
        uint32_t id = 0;
        bool outgoing = false;
        std::vector<uint8_t> body;
    };

    struct EventTitle {
        std::string title;
        std::string subtitle;
        std::string actionBar;
    };

    struct EventLook {
        float dx = 0.0f;
        float dy = 0.0f;
        float dt = 0.0f;
        double dirX = 0;
        double dirY = 0;
        double dirZ = -1;
        double vx = 0;
        double vy = 0;
        double vz = 0;
        uint32_t keys = 0;
        bool gliding = false;
        bool underwater = false;
        bool thirdPerson = false;
        bool onGround = false;
    };

    struct EventSettingChanged {
        std::string id;
        std::string value;
        SettingKind kind = SettingKind::Toggle;
    };

    struct EventMouseMove {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct EventMouseButton {
        int32_t button = 0;
        bool down = false;
        float x = 0.0f;
        float y = 0.0f;
    };

    struct EventMouseWheel {
        float dx = 0.0f;
        float dy = 0.0f;
    };

    struct EventKeyRaw {
        int32_t key = 0;
        bool down = false;
        bool repeat = false;
        bool ctrl = false;
        bool shift = false;
        bool alt = false;
    };

    struct EventScreenRender {
        Screen screen = Screen::None;
        int32_t guiWidth = 0;
        int32_t guiHeight = 0;
        float mouseX = 0.0f;
        float mouseY = 0.0f;
    };

    struct EventSlotClick {
        int32_t slot = 0;
        bool right = false;
        bool shift = false;
    };

    struct EventInteract {
        InteractKind kind = InteractKind::StartBreak;
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        Face face = Face::Up;
        uint64_t entity = 0;
    };

    struct EventChunk {
        int32_t x = 0;
        int32_t z = 0;
        Dimension dimension = Dimension::Overworld;
    };

    struct EventBlockChanged {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        std::string from;
        std::string to;
    };

    struct EventLevel {
        Dimension dimension = Dimension::Overworld;
        std::string server;
    };

    struct EventToast {
        std::string title;
        std::string body;
    };

    struct EventModMessage {
        std::string from;
        std::string channel;
        std::vector<uint8_t> body;
    };

    struct EventTooltip {
        Item item;
        std::vector<std::string> lines;
    };

    struct EventScreenButton {
        std::string id;
    };

    struct EventWindow {
        bool focused = true;
        bool minimized = false;
    };

    struct EventServerPing {
        uint32_t id = 0;
        bool ok = false;
        std::string motd;
        std::string version;
        int32_t players = 0;
        int32_t maxPlayers = 0;
        int32_t protocol = 0;
        float latencyMs = 0.0f;
    };

    struct EventPlayer {
        uint64_t player = 0;
        std::string name;
    };

    struct EventPlayerChat {
        uint64_t player = 0;
        std::string name;
        std::string message;
    };

    struct EventPlayerBlock {
        uint64_t player = 0;
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        std::string block;
    };

    struct LogRequest {
        LogLevel level{};
        std::string text;
    };

    struct PlayerEffectsResponse {
        std::vector<Effect> effects;
    };

    struct PlayerLookResponse {
        float yaw{};
        float pitch{};
    };

    struct InventorySelectRequest {
        int32_t slot{};
    };

    struct InventoryDropRequest {
        int32_t count{};
    };

    struct InventorySwapRequest {
        int32_t a{};
        int32_t b{};
    };

    struct InventoryClickRequest {
        int32_t slot{};
        bool right{};
        bool shift{};
    };

    struct BlockRequest {
        int32_t x{};
        int32_t y{};
        int32_t z{};
    };

    struct RegionRequest {
        int32_t x0{};
        int32_t y0{};
        int32_t z0{};
        int32_t x1{};
        int32_t y1{};
        int32_t z1{};
    };

    struct RaycastRequest {
        double ox{};
        double oy{};
        double oz{};
        double dx{};
        double dy{};
        double dz{};
        double maxDistance{};
        bool liquids{};
    };

    struct FindBlocksRequest {
        int32_t x{};
        int32_t y{};
        int32_t z{};
        int32_t radius{};
        int32_t limit{};
        bool exact{};
        std::string name;
    };

    struct FindBlocksResponse {
        std::vector<FoundBlock> blocks;
    };

    struct ChunkLoadedRequest {
        int32_t x{};
        int32_t z{};
    };

    struct BlockNamesResponse {
        std::vector<std::string> names;
    };

    struct BlockPropsRequest {
        std::string name;
    };

    struct BlockEntityDataRequest {
        int32_t x{};
        int32_t y{};
        int32_t z{};
    };

    struct DisplayNameRequest {
        NameKind kind{};
        std::string id;
        int32_t aux{};
    };

    struct MapChunkRequest {
        int32_t x{};
        int32_t z{};
        int32_t caveY{};
    };

    struct EntitiesRequest {
        double radius{};
        std::string type;
        bool playersOnly{};
        bool includeItems{};
    };

    struct EntitiesResponse {
        std::vector<Entity> entities;
    };

    struct EntityByIdRequest {
        uint64_t id{};
    };

    struct PlayerListResponse {
        std::vector<PlayerEntry> players;
    };

    struct OptionGetRequest {
        std::string key;
    };

    struct OptionSetRequest {
        std::string key;
        std::string value;
    };

    struct TranslateRequest {
        std::string key;
    };

    struct PerspectiveSetRequest {
        Perspective perspective{};
    };

    struct ResourcePacksResponse {
        std::vector<PackEntry> packs;
    };

    struct InputDownRequest {
        std::string name;
    };

    struct KeyRegisterRequest {
        std::string id;
        std::string label;
        std::string fallback;
    };

    struct InputActionsResponse {
        std::vector<std::string> actions;
    };

    struct MousePositionResponse {
        float x{};
        float y{};
        bool captured{};
    };

    struct ChatRequest {
        std::string text;
    };

    struct CommandRequest {
        std::string line;
    };

    struct LocalMessageRequest {
        std::string text;
    };

    struct CommandRegisterRequest {
        std::string name;
        std::string usage;
        std::string description;
    };

    struct CommandUnregisterRequest {
        std::string name;
    };

    struct ServerCommandsResponse {
        std::vector<std::string> names;
    };

    struct ToastRequest {
        std::string text;
        int32_t ms{};
    };

    struct TitleRequest {
        std::string title;
        std::string subtitle;
        int32_t fadeIn{};
        int32_t stay{};
        int32_t fadeOut{};
    };

    struct ChatHistoryRequest {
        uint32_t count{};
    };

    struct ChatHistoryResponse {
        std::vector<ChatLine> lines;
    };

    struct UseOnBlockRequest {
        int32_t x{};
        int32_t y{};
        int32_t z{};
        Face face{};
    };

    struct AttackEntityRequest {
        uint64_t entity{};
    };

    struct InteractEntityRequest {
        uint64_t entity{};
    };

    struct BreakBlockRequest {
        int32_t x{};
        int32_t y{};
        int32_t z{};
        Face face{};
    };

    struct LookRequest {
        float yaw{};
        float pitch{};
    };

    struct PlaySoundRequest {
        std::string name;
        bool positional{};
        float x{};
        float y{};
        float z{};
        float volume{};
        float pitch{};
    };

    struct UiSubmitRequest {
        std::string layer;
        int32_t z{};
        std::vector<uint8_t> commands;
    };

    struct UiClearRequest {
        std::string layer;
    };

    struct UiMeasureRequest {
        std::string text;
        float scale{};
    };

    struct GfxTextureCreateRequest {
        int32_t width{};
        int32_t height{};
        bool linear{};
        std::vector<uint8_t> rgba;
    };

    struct GfxTextureUpdateRequest {
        uint32_t id{};
        int32_t x{};
        int32_t y{};
        int32_t width{};
        int32_t height{};
        std::vector<uint8_t> rgba;
    };

    struct GfxTextureDestroyRequest {
        uint32_t id{};
    };

    struct GfxSubmitRequest {
        std::string layer;
        int32_t z{};
        std::vector<uint8_t> blob;
    };

    struct GfxClearRequest {
        std::string layer;
    };

    struct GfxImageLoadRequest {
        std::string path;
    };

    struct GfxImageLoadResponse {
        int32_t width{};
        int32_t height{};
        std::vector<uint8_t> rgba;
    };

    struct InputGrabRequest {
        bool mouse{};
        bool keyboard{};
    };

    struct StorageReadRequest {
        std::string path;
    };

    struct StorageWriteRequest {
        std::string path;
        std::vector<uint8_t> data;
    };

    struct StorageListRequest {
        std::string path;
    };

    struct StorageListResponse {
        std::vector<std::string> names;
    };

    struct StorageRemoveRequest {
        std::string path;
    };

    struct StorageExistsRequest {
        std::string path;
    };

    struct StorageMakeDirRequest {
        std::string path;
    };

    struct HttpRequestRequest {
        uint32_t id{};
        std::string method;
        std::string url;
        int32_t timeoutMs{};
        std::vector<std::pair<std::string, std::string>> headers;
        std::vector<uint8_t> body;
    };

    struct PacketSendRequest {
        uint32_t id{};
        std::vector<uint8_t> body;
    };

    struct PacketNameRequest {
        uint32_t id{};
    };

    struct SettingGetRequest {
        std::string id;
    };

    struct SettingSetRequest {
        std::string id;
        std::string value;
    };

    struct SettingListResponse {
        std::vector<SettingEntry> settings;
    };

    struct ModListResponse {
        std::vector<ModEntry> mods;
    };

    struct IconSetRequest {
        std::string path;
    };

    struct OpenScreenRequest {
        Screen screen{};
    };

    struct OpenModSettingsRequest {
        std::string mod;
    };

    struct CopyToClipboardRequest {
        std::string text;
    };

    struct OpenUrlRequest {
        std::string url;
    };

    struct CopyFileRequest {
        FileLocation from;
        FileLocation to;
    };

    struct MoveFileRequest {
        FileLocation from;
        FileLocation to;
    };

    struct DownloadFileRequest {
        uint32_t id{};
        std::string url;
        FileLocation to;
    };

    struct PacketCountersResponse {
        std::vector<PacketCount> counters;
    };

    struct PacketInjectRequest {
        uint32_t id{};
        std::vector<uint8_t> body;
    };

    struct WorldMeshClearRequest {
        std::string id;
    };

    struct ScreenButtonRemoveRequest {
        std::string id;
    };

    struct ScreenOpenCustomRequest {
        std::string id;
    };

    struct TooltipAppendRequest {
        std::vector<std::string> lines;
    };

    struct ShaderPackSetRequest {
        std::string name;
    };

    struct ServerItem {
        std::string name;
        uint32_t count = 0;
        uint32_t damage = 0;
    };

    struct ServerInventory {
        std::vector<ServerItem> main;
        std::vector<ServerItem> armor;
        ServerItem offhand;
        int32_t selected = 0;
    };

    struct PlayerDetails {
        uint64_t id = 0;
        std::string name;
        std::string xuid;
        std::string uuid;
        Dimension dimension = Dimension::Overworld;
        double x = 0;
        double y = 0;
        double z = 0;
        float yaw = 0.0f;
        float pitch = 0.0f;
        GameMode gameMode = GameMode::Survival;
        float health = 20.0f;
        float maxHealth = 20.0f;
        float absorption = 0.0f;
        int32_t food = 20;
        float saturation = 5.0f;
        int32_t xpLevel = 0;
        float xpProgress = 0.0f;
        bool onGround = true;
        bool sneaking = false;
        bool sprinting = false;
        bool dead = false;
        bool isOperator = false;
        std::vector<Effect> effects;
    };

    struct ServerEntity {
        uint64_t id = 0;
        std::string type;
        Dimension dimension = Dimension::Overworld;
        double x = 0;
        double y = 0;
        double z = 0;
        float yaw = 0.0f;
        float health = 0.0f;
        float maxHealth = 0.0f;
        bool baby = false;
    };

    struct FormButton {
        std::string text;
        std::string image;
    };

    struct FormControl {
        FormElement kind = FormElement::Label;
        std::string text;
        std::vector<std::string> options;
        float min = 0.0f;
        float max = 100.0f;
        float step = 1.0f;
        float defaultNumber = 0.0f;
        std::string defaultText;
        std::string placeholder;
    };

    struct Form {
        FormKind kind = FormKind::Message;
        std::string title;
        std::string body;
        std::vector<FormButton> buttons;
        std::vector<FormControl> controls;
    };

    struct EventServerDamage {
        uint64_t player = 0;
        float amount = 0.0f;
        DamageCause cause = DamageCause::Generic;
        uint64_t attacker = 0;
        std::string attackerType;
    };

    struct EventServerDeath {
        uint64_t player = 0;
        DamageCause cause = DamageCause::Generic;
        uint64_t attacker = 0;
        std::string message;
    };

    struct EventPlayerUse {
        uint64_t player = 0;
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        Face face = Face::Up;
        std::string block;
        std::string item;
    };

    struct EventPlayerAttack {
        uint64_t player = 0;
        uint64_t target = 0;
        std::string targetType;
        float damage = 0.0f;
    };

    struct EventEntityDamage {
        uint64_t entity = 0;
        std::string type;
        float amount = 0.0f;
        DamageCause cause = DamageCause::Generic;
        uint64_t attacker = 0;
    };

    struct EventEntityDeath {
        uint64_t entity = 0;
        std::string type;
        double x = 0;
        double y = 0;
        double z = 0;
        uint64_t killer = 0;
    };

    struct EventEntitySpawn {
        uint64_t entity = 0;
        std::string type;
        double x = 0;
        double y = 0;
        double z = 0;
        bool natural = false;
    };

    struct EventItem {
        uint64_t player = 0;
        std::string item;
        uint32_t count = 0;
    };

    struct EventGameModeChange {
        uint64_t player = 0;
        GameMode from{};
        GameMode to{};
    };

    struct EventFormResponse {
        uint64_t player = 0;
        uint32_t form = 0;
        bool closed = false;
        int32_t button = -1;
        std::vector<std::string> values;
    };

    struct EventPlayerMove {
        uint64_t player = 0;
        Dimension dimension = Dimension::Overworld;
        double x = 0;
        double y = 0;
        double z = 0;
        float yaw = 0.0f;
        float pitch = 0.0f;
        bool onGround = true;
    };

    struct EventExplosion {
        double x = 0;
        double y = 0;
        double z = 0;
        float power = 0.0f;
        uint64_t source = 0;
        std::string sourceType;
    };

    struct EventContainer {
        uint64_t player = 0;
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        std::string block;
    };

    struct EventPlayerDimension {
        uint64_t player = 0;
        Dimension from{};
        Dimension to{};
    };

    struct EventServerCommand {
        uint64_t player = 0;
        std::string line;
    };

    struct EventPreLogin {
        std::string name;
        std::string xuid;
        std::string address;
    };

    struct PlayerInfoRequest {
        uint64_t player{};
    };

    struct PlayerSetGameModeRequest {
        uint64_t player{};
        GameMode mode{};
    };

    struct PlayerSetHealthRequest {
        uint64_t player{};
        float health{};
    };

    struct PlayerDamageRequest {
        uint64_t player{};
        float amount{};
        DamageCause cause{};
        bool knockback{};
    };

    struct PlayerKillRequest {
        uint64_t player{};
    };

    struct PlayerSetFoodRequest {
        uint64_t player{};
        int32_t food{};
        float saturation{};
    };

    struct PlayerAddExperienceRequest {
        uint64_t player{};
        int32_t points{};
        int32_t levels{};
    };

    struct PlayerEffectAddRequest {
        uint64_t player{};
        EffectId effect{};
        int32_t amplifier{};
        int32_t ticks{};
        bool particles{};
    };

    struct PlayerEffectRemoveRequest {
        uint64_t player{};
        int32_t effect{};
    };

    struct PlayerInventoryRequest {
        uint64_t player{};
    };

    struct PlayerSetSlotRequest {
        uint64_t player{};
        int32_t container{};
        int32_t slot{};
        ServerItem item;
    };

    struct PlayerClearInventoryRequest {
        uint64_t player{};
    };

    struct PlayerSetVelocityRequest {
        uint64_t player{};
        double x{};
        double y{};
        double z{};
    };

    struct PlayerTransferDimensionRequest {
        uint64_t player{};
        Dimension dimension{};
        double x{};
        double y{};
        double z{};
    };

    struct PlayerTitleRequest {
        uint64_t player{};
        std::string title;
        std::string subtitle;
        int32_t fadeIn{};
        int32_t stay{};
        int32_t fadeOut{};
    };

    struct PlayerActionBarRequest {
        uint64_t player{};
        std::string text;
    };

    struct PlayerSoundRequest {
        uint64_t player{};
        std::string sound;
        double x{};
        double y{};
        double z{};
        float volume{};
        float pitch{};
    };

    struct PlayerTransferRequest {
        uint64_t player{};
        std::string host;
        uint16_t port{};
    };

    struct PlayerSetOperatorRequest {
        uint64_t player{};
        bool isOperator{};
    };

    struct PlayerFormRequest {
        uint64_t player{};
        Form form;
    };

    struct PlayerRunCommandRequest {
        uint64_t player{};
        std::string line;
    };

    struct PlayerSetSpawnRequest {
        uint64_t player{};
        int32_t x{};
        int32_t y{};
        int32_t z{};
    };

    struct PlayerByNameRequest {
        std::string name;
    };

    struct BlockStateRequest {
        Dimension dimension{};
        int32_t x{};
        int32_t y{};
        int32_t z{};
    };

    struct BlockStateSetRequest {
        Dimension dimension{};
        int32_t x{};
        int32_t y{};
        int32_t z{};
        std::string name;
        std::vector<std::pair<std::string, std::string>> states;
    };

    struct FillRequest {
        Dimension dimension{};
        int32_t x0{};
        int32_t y0{};
        int32_t z0{};
        int32_t x1{};
        int32_t y1{};
        int32_t z1{};
        std::string block;
    };

    struct TimeGetResponse {
        int64_t time{};
        int64_t day{};
        int32_t timeOfDay{};
    };

    struct TimeSetRequest {
        int64_t time{};
    };

    struct WeatherSetRequest {
        Weather weather{};
        int32_t ticks{};
    };

    struct GameRuleGetRequest {
        std::string name;
    };

    struct GameRuleSetRequest {
        std::string name;
        int32_t value{};
    };

    struct DifficultySetRequest {
        Difficulty difficulty{};
    };

    struct ExplodeRequest {
        Dimension dimension{};
        double x{};
        double y{};
        double z{};
        float power{};
        bool breakBlocks{};
    };

    struct HighestBlockRequest {
        Dimension dimension{};
        int32_t x{};
        int32_t z{};
    };

    struct ParticleAtRequest {
        Dimension dimension{};
        std::string name;
        double x{};
        double y{};
        double z{};
    };

    struct DropItemRequest {
        Dimension dimension{};
        ServerItem item;
        double x{};
        double y{};
        double z{};
    };

    struct EntitySpawnRequest {
        std::string type;
        double x{};
        double y{};
        double z{};
        bool baby{};
    };

    struct EntityListRequest {
        double x{};
        double y{};
        double z{};
        double radius{};
        std::string type;
    };

    struct EntityListResponse {
        std::vector<ServerEntity> entities;
    };

    struct EntityRemoveRequest {
        uint64_t entity{};
    };

    struct EntityDamageRequest {
        uint64_t entity{};
        float amount{};
    };

    struct EntityTeleportRequest {
        uint64_t entity{};
        double x{};
        double y{};
        double z{};
    };

    struct EntitySetVelocityRequest {
        uint64_t entity{};
        double x{};
        double y{};
        double z{};
    };

    struct EntityInfoRequest {
        uint64_t entity{};
    };

    struct EntitySetHealthRequest {
        uint64_t entity{};
        float health{};
    };

    struct RunCommandRequest {
        std::string line;
    };

    struct MotdSetRequest {
        std::string motd;
    };

    struct LootTableOverrideRequest {
        std::string key;
        std::string json;
    };

    struct LootTableGetRequest {
        std::string key;
    };

    struct EventAmendRequest {
        std::string field;
        double number{};
        std::string text;
    };

}
