#ifndef PERLIN_ABI_H
#define PERLIN_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct perlin_str { const char* data; uint32_t len; } perlin_str;
typedef struct perlin_bytes { const uint8_t* data; uint32_t len; } perlin_bytes;
typedef struct perlin_writer { uint8_t* data; uint32_t len; uint32_t cap; int overflow; } perlin_writer;
typedef struct perlin_reader { const uint8_t* p; const uint8_t* end; int failed; } perlin_reader;
typedef struct perlin_arena { uint8_t* base; size_t cap; size_t used; } perlin_arena;

static inline perlin_str perlin_cstr(const char* s) {
    perlin_str out;
    out.data = s;
    out.len = 0;
    while (s && s[out.len])
        out.len++;
    return out;
}
static inline perlin_arena perlin_arena_make(void* memory, size_t size) {
    perlin_arena a;
    a.base = (uint8_t*)memory;
    a.cap = size;
    a.used = 0;
    return a;
}
static inline void* perlin_arena_alloc(perlin_arena* a, size_t size) {
    size_t at;
    if (!a)
        return NULL;
    at = (a->used + 7u) & ~(size_t)7u;
    if (at + size > a->cap)
        return NULL;
    a->used = at + size;
    return a->base + at;
}
static inline perlin_writer perlin_writer_make(void* memory, uint32_t size) {
    perlin_writer w;
    w.data = (uint8_t*)memory;
    w.len = 0;
    w.cap = size;
    w.overflow = 0;
    return w;
}
static inline perlin_reader perlin_reader_make(const void* data, uint32_t size) {
    perlin_reader r;
    r.p = (const uint8_t*)data;
    r.end = r.p + size;
    r.failed = 0;
    return r;
}
static inline void perlin_w_raw(perlin_writer* w, const void* p, uint32_t n) {
    if (w->overflow || n > w->cap - w->len) {
        w->overflow = 1;
        return;
    }
    if (n)
        __builtin_memcpy(w->data + w->len, p, n);
    w->len += n;
}
static inline void perlin_r_raw(perlin_reader* r, void* out, uint32_t n) {
    if (r->failed || (size_t)(r->end - r->p) < n) {
        r->failed = 1;
        __builtin_memset(out, 0, n);
        return;
    }
    __builtin_memcpy(out, r->p, n);
    r->p += n;
}
#define PERLIN_SCALAR_CODEC(tag, type) \
    static inline void perlin_w_##tag(perlin_writer* w, type v) { perlin_w_raw(w, &v, sizeof v); } \
    static inline type perlin_r_##tag(perlin_reader* r) { type v; perlin_r_raw(r, &v, sizeof v); return v; }
PERLIN_SCALAR_CODEC(u8, uint8_t)
PERLIN_SCALAR_CODEC(i8, int8_t)
PERLIN_SCALAR_CODEC(u16, uint16_t)
PERLIN_SCALAR_CODEC(i16, int16_t)
PERLIN_SCALAR_CODEC(u32, uint32_t)
PERLIN_SCALAR_CODEC(i32, int32_t)
PERLIN_SCALAR_CODEC(u64, uint64_t)
PERLIN_SCALAR_CODEC(i64, int64_t)
PERLIN_SCALAR_CODEC(f32, float)
PERLIN_SCALAR_CODEC(f64, double)
#undef PERLIN_SCALAR_CODEC
static inline void perlin_w_str(perlin_writer* w, perlin_str s) {
    perlin_w_u32(w, s.len);
    perlin_w_raw(w, s.data, s.len);
}
static inline void perlin_w_bytes(perlin_writer* w, perlin_bytes b) {
    perlin_w_u32(w, b.len);
    perlin_w_raw(w, b.data, b.len);
}
static inline perlin_bytes perlin_r_bytes(perlin_reader* r) {
    perlin_bytes b;
    b.len = perlin_r_u32(r);
    b.data = r->p;
    if (r->failed || (size_t)(r->end - r->p) < b.len) {
        r->failed = 1;
        b.len = 0;
        b.data = NULL;
        return b;
    }
    r->p += b.len;
    return b;
}
static inline perlin_str perlin_r_str(perlin_reader* r) {
    perlin_bytes b = perlin_r_bytes(r);
    perlin_str s;
    s.data = (const char*)b.data;
    s.len = b.len;
    return s;
}
static inline perlin_bytes perlin_r_rest(perlin_reader* r) {
    perlin_bytes b;
    b.data = r->p;
    b.len = r->failed ? 0u : (uint32_t)(r->end - r->p);
    r->p = r->end;
    return b;
}
static inline perlin_str perlin_r_rest_str(perlin_reader* r) {
    perlin_bytes b = perlin_r_rest(r);
    perlin_str s;
    s.data = (const char*)b.data;
    s.len = b.len;
    return s;
}

#if defined(__wasm__)
#define PERLIN_C_IMPORT(name) __attribute__((import_module("perlin"), import_name(#name)))
#define PERLIN_C_EXPORT(name) __attribute__((export_name(#name)))
#else
#define PERLIN_C_IMPORT(name)
#define PERLIN_C_EXPORT(name)
#endif

#ifndef PERLIN_CALL_DECLARED
#define PERLIN_CALL_DECLARED
PERLIN_C_IMPORT(call) int32_t perlin_call(uint32_t op, const void* in, uint32_t in_len, void* out, uint32_t out_cap);
#endif

static inline int32_t perlin_invoke(uint32_t op, const perlin_writer* request, perlin_reader* reply, perlin_arena* a) {
    uint32_t cap = 1024;
    for (int attempt = 0; attempt < 4; attempt++) {
        size_t mark = a->used;
        uint8_t* out = (uint8_t*)perlin_arena_alloc(a, cap);
        if (!out)
            return -6;
        int32_t rc = perlin_call(op, request ? request->data : NULL, request ? request->len : 0u, out, cap);
        if (rc >= 0) {
            *reply = perlin_reader_make(out, (uint32_t)rc);
            a->used = (size_t)(out - a->base) + (size_t)rc;
            return 0;
        }
        a->used = mark;
        if (rc < -16) {
            cap = (uint32_t)(-rc - 16);
            continue;
        }
        return rc;
    }
    return -6;
}

#define PERLIN_SCHEMA_VERSION 3
#define PERLIN_API_VERSION (1u)
#define PERLIN_NEED_MORE_BASE (16)
#define PERLIN_EVENT_CANCEL (1u)
#define PERLIN_GFX_INDEX32 (1u)
#define PERLIN_GFX_MAX_BYTES (8388608u)
#define PERLIN_GFX_MAX_VERTICES (1048576u)
#define PERLIN_GFX_MAX_LAYERS (8u)
#define PERLIN_GFX_SCREEN_Z (100)
#define PERLIN_GFX_MAX_TEXTURES (64u)
#define PERLIN_GFX_MAX_TEXTURE_SIDE (4096u)
#define PERLIN_GFX_MAX_TEXTURE_BYTES (134217728ull)

typedef int32_t PerlinStatus;
enum {
    PERLIN_STATUS_OK = 0,
    PERLIN_STATUS_UNKNOWN_OP = -1,
    PERLIN_STATUS_DENIED = -2,
    PERLIN_STATUS_BAD_ARGUMENTS = -3,
    PERLIN_STATUS_UNAVAILABLE = -4,
    PERLIN_STATUS_NOT_FOUND = -5,
    PERLIN_STATUS_FAILED = -6,
};
typedef uint32_t PerlinPermission;
enum {
    PERLIN_PERMISSION_NONE = 0,
    PERLIN_PERMISSION_NETWORK = (1u << 0),
    PERLIN_PERMISSION_PACKETS = (1u << 1),
    PERLIN_PERMISSION_CONTROL = (1u << 2),
    PERLIN_PERMISSION_STORAGE = (1u << 3),
    PERLIN_PERMISSION_FILES = (1u << 4),
    PERLIN_PERMISSION_NATIVE = (1u << 5),
    PERLIN_PERMISSION_WORLD = (1u << 6),
    PERLIN_PERMISSION_PLAYERS = (1u << 7),
    PERLIN_PERMISSION_ADMIN = (1u << 8),
};
typedef uint8_t PerlinDraw;
enum {
    PERLIN_DRAW_RECT = 1,
    PERLIN_DRAW_TEXT = 2,
    PERLIN_DRAW_ITEM = 3,
    PERLIN_DRAW_IMAGE = 4,
    PERLIN_DRAW_LINE = 5,
    PERLIN_DRAW_GRADIENT = 6,
    PERLIN_DRAW_BORDER = 7,
};
typedef int32_t PerlinScreen;
enum {
    PERLIN_SCREEN_NONE = 0,
    PERLIN_SCREEN_PAUSE = 1,
    PERLIN_SCREEN_CHAT = 2,
    PERLIN_SCREEN_INVENTORY = 3,
    PERLIN_SCREEN_OPTIONS = 4,
    PERLIN_SCREEN_MODS = 5,
    PERLIN_SCREEN_WARDROBE = 6,
    PERLIN_SCREEN_DEATH = 7,
    PERLIN_SCREEN_WORKBENCH = 8,
    PERLIN_SCREEN_MENU = 9,
    PERLIN_SCREEN_OTHER = 10,
    PERLIN_SCREEN_CUSTOM = 11,
};
typedef int32_t PerlinHudElement;
enum {
    PERLIN_HUD_ELEMENT_HEARTS = 0,
    PERLIN_HUD_ELEMENT_ARMOR = 1,
    PERLIN_HUD_ELEMENT_HUNGER = 2,
    PERLIN_HUD_ELEMENT_AIR = 3,
    PERLIN_HUD_ELEMENT_HOTBAR = 4,
    PERLIN_HUD_ELEMENT_EXPERIENCE = 5,
    PERLIN_HUD_ELEMENT_DEBUG = 6,
    PERLIN_HUD_ELEMENT_CHAT = 7,
    PERLIN_HUD_ELEMENT_EFFECTS = 8,
    PERLIN_HUD_ELEMENT_LOCATOR = 9,
    PERLIN_HUD_ELEMENT_FIRE = 10,
    PERLIN_HUD_ELEMENT_SUBTITLES = 11,
    PERLIN_HUD_ELEMENT_CROSSHAIR = 12,
};
typedef int32_t PerlinInteractKind;
enum {
    PERLIN_INTERACT_KIND_START_BREAK = 0,
    PERLIN_INTERACT_KIND_ATTACK = 1,
    PERLIN_INTERACT_KIND_INTERACT_ENTITY = 2,
    PERLIN_INTERACT_KIND_USE_ON_BLOCK = 3,
    PERLIN_INTERACT_KIND_USE_ITEM = 4,
};
typedef int32_t PerlinRenderStage;
enum {
    PERLIN_RENDER_STAGE_AFTER_TERRAIN = 0,
    PERLIN_RENDER_STAGE_AFTER_ENTITIES = 1,
    PERLIN_RENDER_STAGE_OVERLAY = 2,
};
typedef int32_t PerlinKeyContext;
enum {
    PERLIN_KEY_CONTEXT_IN_GAME = 0,
    PERLIN_KEY_CONTEXT_IN_SCREEN = 1,
    PERLIN_KEY_CONTEXT_ANY = 2,
};
typedef int32_t PerlinGameMode;
enum {
    PERLIN_GAME_MODE_SURVIVAL = 0,
    PERLIN_GAME_MODE_CREATIVE = 1,
    PERLIN_GAME_MODE_ADVENTURE = 2,
    PERLIN_GAME_MODE_SURVIVAL_SPECTATOR = 3,
    PERLIN_GAME_MODE_CREATIVE_SPECTATOR = 4,
    PERLIN_GAME_MODE_DEFAULT = 5,
    PERLIN_GAME_MODE_SPECTATOR = 6,
};
typedef int32_t PerlinDimension;
enum {
    PERLIN_DIMENSION_OVERWORLD = 0,
    PERLIN_DIMENSION_NETHER = 1,
    PERLIN_DIMENSION_END = 2,
};
typedef int32_t PerlinDifficulty;
enum {
    PERLIN_DIFFICULTY_PEACEFUL = 0,
    PERLIN_DIFFICULTY_EASY = 1,
    PERLIN_DIFFICULTY_NORMAL = 2,
    PERLIN_DIFFICULTY_HARD = 3,
};
typedef int32_t PerlinPermissionLevel;
enum {
    PERLIN_PERMISSION_LEVEL_UNKNOWN = -1,
    PERLIN_PERMISSION_LEVEL_VISITOR = 0,
    PERLIN_PERMISSION_LEVEL_MEMBER = 1,
    PERLIN_PERMISSION_LEVEL_OPERATOR = 2,
    PERLIN_PERMISSION_LEVEL_CUSTOM = 3,
};
typedef int32_t PerlinFace;
enum {
    PERLIN_FACE_DOWN = 0,
    PERLIN_FACE_UP = 1,
    PERLIN_FACE_NORTH = 2,
    PERLIN_FACE_SOUTH = 3,
    PERLIN_FACE_WEST = 4,
    PERLIN_FACE_EAST = 5,
};
typedef int32_t PerlinAimKind;
enum {
    PERLIN_AIM_KIND_NONE = 0,
    PERLIN_AIM_KIND_BLOCK = 1,
    PERLIN_AIM_KIND_ENTITY = 2,
};
typedef int32_t PerlinChatType;
enum {
    PERLIN_CHAT_TYPE_RAW = 0,
    PERLIN_CHAT_TYPE_CHAT = 1,
    PERLIN_CHAT_TYPE_TRANSLATION = 2,
    PERLIN_CHAT_TYPE_POPUP = 3,
    PERLIN_CHAT_TYPE_JUKEBOX_POPUP = 4,
    PERLIN_CHAT_TYPE_TIP = 5,
    PERLIN_CHAT_TYPE_SYSTEM = 6,
    PERLIN_CHAT_TYPE_WHISPER = 7,
    PERLIN_CHAT_TYPE_ANNOUNCEMENT = 8,
    PERLIN_CHAT_TYPE_OBJECT_WHISPER = 9,
    PERLIN_CHAT_TYPE_OBJECT = 10,
    PERLIN_CHAT_TYPE_OBJECT_ANNOUNCEMENT = 11,
};
typedef int32_t PerlinDisconnectReason;
enum {
    PERLIN_DISCONNECT_REASON_UNKNOWN = 0,
    PERLIN_DISCONNECT_REASON_CANT_CONNECT_NO_INTERNET = 1,
    PERLIN_DISCONNECT_REASON_NO_PERMISSIONS = 2,
    PERLIN_DISCONNECT_REASON_UNRECOVERABLE_ERROR = 3,
    PERLIN_DISCONNECT_REASON_THIRD_PARTY_BLOCKED = 4,
    PERLIN_DISCONNECT_REASON_THIRD_PARTY_NO_INTERNET = 5,
    PERLIN_DISCONNECT_REASON_THIRD_PARTY_BAD_IP = 6,
    PERLIN_DISCONNECT_REASON_THIRD_PARTY_NO_SERVER_OR_SERVER_LOCKED = 7,
    PERLIN_DISCONNECT_REASON_VERSION_MISMATCH = 8,
    PERLIN_DISCONNECT_REASON_SKIN_ISSUE = 9,
    PERLIN_DISCONNECT_REASON_INVITE_SESSION_NOT_FOUND = 10,
};
typedef int32_t PerlinEffectId;
enum {
    PERLIN_EFFECT_ID_SPEED = 1,
    PERLIN_EFFECT_ID_SLOWNESS = 2,
    PERLIN_EFFECT_ID_HASTE = 3,
    PERLIN_EFFECT_ID_MINING_FATIGUE = 4,
    PERLIN_EFFECT_ID_STRENGTH = 5,
    PERLIN_EFFECT_ID_INSTANT_HEALTH = 6,
    PERLIN_EFFECT_ID_INSTANT_DAMAGE = 7,
    PERLIN_EFFECT_ID_JUMP_BOOST = 8,
    PERLIN_EFFECT_ID_NAUSEA = 9,
    PERLIN_EFFECT_ID_REGENERATION = 10,
    PERLIN_EFFECT_ID_RESISTANCE = 11,
    PERLIN_EFFECT_ID_FIRE_RESISTANCE = 12,
    PERLIN_EFFECT_ID_WATER_BREATHING = 13,
    PERLIN_EFFECT_ID_INVISIBILITY = 14,
    PERLIN_EFFECT_ID_BLINDNESS = 15,
    PERLIN_EFFECT_ID_NIGHT_VISION = 16,
    PERLIN_EFFECT_ID_HUNGER = 17,
    PERLIN_EFFECT_ID_WEAKNESS = 18,
    PERLIN_EFFECT_ID_POISON = 19,
    PERLIN_EFFECT_ID_WITHER = 20,
    PERLIN_EFFECT_ID_HEALTH_BOOST = 21,
    PERLIN_EFFECT_ID_ABSORPTION = 22,
    PERLIN_EFFECT_ID_SATURATION = 23,
    PERLIN_EFFECT_ID_LEVITATION = 24,
    PERLIN_EFFECT_ID_FATAL_POISON = 25,
    PERLIN_EFFECT_ID_CONDUIT_POWER = 26,
    PERLIN_EFFECT_ID_SLOW_FALLING = 27,
    PERLIN_EFFECT_ID_BAD_OMEN = 28,
    PERLIN_EFFECT_ID_VILLAGE_HERO = 29,
    PERLIN_EFFECT_ID_DARKNESS = 30,
    PERLIN_EFFECT_ID_TRIAL_OMEN = 31,
    PERLIN_EFFECT_ID_RAID_OMEN = 32,
    PERLIN_EFFECT_ID_WIND_CHARGED = 33,
    PERLIN_EFFECT_ID_WEAVING = 34,
    PERLIN_EFFECT_ID_OOZING = 35,
    PERLIN_EFFECT_ID_INFESTED = 36,
};
typedef int32_t PerlinPerspective;
enum {
    PERLIN_PERSPECTIVE_FIRST_PERSON = 0,
    PERLIN_PERSPECTIVE_THIRD_PERSON_BACK = 1,
    PERLIN_PERSPECTIVE_THIRD_PERSON_FRONT = 2,
};
typedef uint8_t PerlinSettingKind;
enum {
    PERLIN_SETTING_KIND_TOGGLE = 0,
    PERLIN_SETTING_KIND_INTEGER = 1,
    PERLIN_SETTING_KIND_NUMBER = 2,
    PERLIN_SETTING_KIND_CHOICE = 3,
    PERLIN_SETTING_KIND_BUTTON = 4,
    PERLIN_SETTING_KIND_LABEL = 5,
    PERLIN_SETTING_KIND_HEADER = 6,
};
typedef uint8_t PerlinArea;
enum {
    PERLIN_AREA_MOD = 0,
    PERLIN_AREA_SCREENSHOTS = 1,
    PERLIN_AREA_RESOURCE_PACKS = 2,
    PERLIN_AREA_SHADER_PACKS = 3,
};
typedef uint8_t PerlinEntityFlag;
enum {
    PERLIN_ENTITY_FLAG_ON_FIRE = 0,
    PERLIN_ENTITY_FLAG_SNEAKING = 1,
    PERLIN_ENTITY_FLAG_RIDING = 2,
    PERLIN_ENTITY_FLAG_SPRINTING = 3,
    PERLIN_ENTITY_FLAG_USING_ITEM = 4,
    PERLIN_ENTITY_FLAG_INVISIBLE = 5,
    PERLIN_ENTITY_FLAG_TEMPTED = 6,
    PERLIN_ENTITY_FLAG_IN_LOVE = 7,
    PERLIN_ENTITY_FLAG_SADDLED = 8,
    PERLIN_ENTITY_FLAG_POWERED = 9,
    PERLIN_ENTITY_FLAG_IGNITED = 10,
    PERLIN_ENTITY_FLAG_BABY = 11,
    PERLIN_ENTITY_FLAG_CONVERTING = 12,
    PERLIN_ENTITY_FLAG_CRITICAL = 13,
    PERLIN_ENTITY_FLAG_CAN_SHOW_NAME = 14,
    PERLIN_ENTITY_FLAG_ALWAYS_SHOW_NAME = 15,
    PERLIN_ENTITY_FLAG_NO_AI = 16,
    PERLIN_ENTITY_FLAG_SILENT = 17,
    PERLIN_ENTITY_FLAG_WALL_CLIMBING = 18,
    PERLIN_ENTITY_FLAG_CAN_CLIMB = 19,
    PERLIN_ENTITY_FLAG_CAN_SWIM = 20,
    PERLIN_ENTITY_FLAG_CAN_FLY = 21,
    PERLIN_ENTITY_FLAG_CAN_WALK = 22,
    PERLIN_ENTITY_FLAG_RESTING = 23,
    PERLIN_ENTITY_FLAG_SITTING = 24,
    PERLIN_ENTITY_FLAG_ANGRY = 25,
    PERLIN_ENTITY_FLAG_INTERESTED = 26,
    PERLIN_ENTITY_FLAG_CHARGED = 27,
    PERLIN_ENTITY_FLAG_TAMED = 28,
    PERLIN_ENTITY_FLAG_ORPHANED = 29,
    PERLIN_ENTITY_FLAG_LEASHED = 30,
    PERLIN_ENTITY_FLAG_SHEARED = 31,
    PERLIN_ENTITY_FLAG_GLIDING = 32,
    PERLIN_ENTITY_FLAG_ELDER = 33,
    PERLIN_ENTITY_FLAG_MOVING = 34,
    PERLIN_ENTITY_FLAG_BREATHING = 35,
    PERLIN_ENTITY_FLAG_CHESTED = 36,
    PERLIN_ENTITY_FLAG_STACKABLE = 37,
    PERLIN_ENTITY_FLAG_SHOW_BASE = 38,
    PERLIN_ENTITY_FLAG_REARING = 39,
    PERLIN_ENTITY_FLAG_VIBRATING = 40,
    PERLIN_ENTITY_FLAG_IDLING = 41,
    PERLIN_ENTITY_FLAG_EVOKER_SPELL = 42,
    PERLIN_ENTITY_FLAG_CHARGE_ATTACK = 43,
    PERLIN_ENTITY_FLAG_WASD_CONTROLLED = 44,
    PERLIN_ENTITY_FLAG_CAN_POWER_JUMP = 45,
    PERLIN_ENTITY_FLAG_LINGERING = 46,
    PERLIN_ENTITY_FLAG_HAS_COLLISION = 47,
    PERLIN_ENTITY_FLAG_HAS_GRAVITY = 48,
    PERLIN_ENTITY_FLAG_FIRE_IMMUNE = 49,
    PERLIN_ENTITY_FLAG_DANCING = 50,
    PERLIN_ENTITY_FLAG_ENCHANTED = 51,
    PERLIN_ENTITY_FLAG_RETURN_TRIDENT = 52,
    PERLIN_ENTITY_FLAG_CONTAINER_PRIVATE = 53,
    PERLIN_ENTITY_FLAG_TRANSFORMING = 54,
    PERLIN_ENTITY_FLAG_DAMAGE_NEARBY_MOBS = 55,
    PERLIN_ENTITY_FLAG_SWIMMING = 56,
    PERLIN_ENTITY_FLAG_BRIBED = 57,
    PERLIN_ENTITY_FLAG_PREGNANT = 58,
    PERLIN_ENTITY_FLAG_LAYING_EGG = 59,
    PERLIN_ENTITY_FLAG_PASSENGER_CAN_PICK = 60,
    PERLIN_ENTITY_FLAG_TRANSITION_SITTING = 61,
    PERLIN_ENTITY_FLAG_EATING = 62,
    PERLIN_ENTITY_FLAG_LAYING_DOWN = 63,
};
typedef uint8_t PerlinTool;
enum {
    PERLIN_TOOL_NONE = 0,
    PERLIN_TOOL_PICKAXE = 1,
    PERLIN_TOOL_AXE = 2,
    PERLIN_TOOL_SHOVEL = 3,
    PERLIN_TOOL_HOE = 4,
    PERLIN_TOOL_SHEARS = 5,
    PERLIN_TOOL_SWORD = 6,
};
typedef uint8_t PerlinNameKind;
enum {
    PERLIN_NAME_KIND_ITEM = 0,
    PERLIN_NAME_KIND_BLOCK = 1,
    PERLIN_NAME_KIND_ENTITY = 2,
};
typedef int32_t PerlinDamageCause;
enum {
    PERLIN_DAMAGE_CAUSE_GENERIC = 0,
    PERLIN_DAMAGE_CAUSE_MOB = 1,
    PERLIN_DAMAGE_CAUSE_PLAYER = 2,
    PERLIN_DAMAGE_CAUSE_PROJECTILE = 3,
    PERLIN_DAMAGE_CAUSE_EXPLOSION = 4,
    PERLIN_DAMAGE_CAUSE_FALL = 5,
    PERLIN_DAMAGE_CAUSE_VOID = 6,
    PERLIN_DAMAGE_CAUSE_LAVA = 7,
    PERLIN_DAMAGE_CAUSE_FIRE = 8,
    PERLIN_DAMAGE_CAUSE_BURNING = 9,
    PERLIN_DAMAGE_CAUSE_DROWNING = 10,
    PERLIN_DAMAGE_CAUSE_STARVING = 11,
    PERLIN_DAMAGE_CAUSE_SUFFOCATION = 12,
    PERLIN_DAMAGE_CAUSE_CACTUS = 13,
    PERLIN_DAMAGE_CAUSE_MAGMA = 14,
    PERLIN_DAMAGE_CAUSE_BERRY_BUSH = 15,
    PERLIN_DAMAGE_CAUSE_POISON = 16,
    PERLIN_DAMAGE_CAUSE_WITHER = 17,
    PERLIN_DAMAGE_CAUSE_MAGIC = 18,
    PERLIN_DAMAGE_CAUSE_FALLING_BLOCK = 19,
    PERLIN_DAMAGE_CAUSE_KILL = 20,
    PERLIN_DAMAGE_CAUSE_PLUGIN = 21,
};
typedef int32_t PerlinWeather;
enum {
    PERLIN_WEATHER_CLEAR = 0,
    PERLIN_WEATHER_RAIN = 1,
    PERLIN_WEATHER_THUNDER = 2,
};
typedef int32_t PerlinFormKind;
enum {
    PERLIN_FORM_KIND_MESSAGE = 0,
    PERLIN_FORM_KIND_MENU = 1,
    PERLIN_FORM_KIND_CUSTOM = 2,
};
typedef int32_t PerlinFormElement;
enum {
    PERLIN_FORM_ELEMENT_LABEL = 0,
    PERLIN_FORM_ELEMENT_TOGGLE = 1,
    PERLIN_FORM_ELEMENT_SLIDER = 2,
    PERLIN_FORM_ELEMENT_STEP_SLIDER = 3,
    PERLIN_FORM_ELEMENT_DROPDOWN = 4,
    PERLIN_FORM_ELEMENT_INPUT = 5,
    PERLIN_FORM_ELEMENT_HEADER = 6,
    PERLIN_FORM_ELEMENT_DIVIDER = 7,
};
typedef int32_t PerlinLogLevel;
enum {
    PERLIN_LOG_LEVEL_DEBUG = 0,
    PERLIN_LOG_LEVEL_INFO = 1,
    PERLIN_LOG_LEVEL_WARN = 2,
    PERLIN_LOG_LEVEL_ERROR = 3,
};

enum PerlinOpId {
    PERLIN_OP_LOG = 0x0001,
    PERLIN_OP_META = 0x0002,
    PERLIN_OP_CLOCK = 0x0003,
    PERLIN_OP_PLAYER_STATE = 0x0100,
    PERLIN_OP_PLAYER_EFFECTS = 0x0101,
    PERLIN_OP_PLAYER_LOOK = 0x0102,
    PERLIN_OP_INVENTORY = 0x0200,
    PERLIN_OP_INVENTORY_SELECT = 0x0201,
    PERLIN_OP_INVENTORY_DROP = 0x0202,
    PERLIN_OP_INVENTORY_SWAP = 0x0203,
    PERLIN_OP_INVENTORY_CLICK = 0x0204,
    PERLIN_OP_INVENTORY_OPEN = 0x0205,
    PERLIN_OP_INVENTORY_CLOSE = 0x0206,
    PERLIN_OP_WORLD_STATE = 0x0300,
    PERLIN_OP_BLOCK = 0x0301,
    PERLIN_OP_REGION = 0x0302,
    PERLIN_OP_RAYCAST = 0x0303,
    PERLIN_OP_AIM = 0x0304,
    PERLIN_OP_FIND_BLOCKS = 0x0305,
    PERLIN_OP_CHUNK_LOADED = 0x0306,
    PERLIN_OP_BLOCK_NAMES = 0x0307,
    PERLIN_OP_BLOCK_PROPS = 0x0308,
    PERLIN_OP_BLOCK_ENTITY_DATA = 0x0309,
    PERLIN_OP_DISPLAY_NAME = 0x030A,
    PERLIN_OP_BREAK_PROGRESS = 0x030B,
    PERLIN_OP_MAP_CHUNK = 0x030C,
    PERLIN_OP_ENTITIES = 0x0400,
    PERLIN_OP_ENTITY_BY_ID = 0x0401,
    PERLIN_OP_PLAYER_LIST = 0x0402,
    PERLIN_OP_OPTIONS = 0x0500,
    PERLIN_OP_OPTION_GET = 0x0501,
    PERLIN_OP_OPTION_SET = 0x0502,
    PERLIN_OP_SCREEN = 0x0503,
    PERLIN_OP_TRANSLATE = 0x0504,
    PERLIN_OP_RELOAD_RESOURCES = 0x0505,
    PERLIN_OP_PERSPECTIVE_GET = 0x0506,
    PERLIN_OP_PERSPECTIVE_SET = 0x0507,
    PERLIN_OP_RESOURCE_PACKS = 0x0508,
    PERLIN_OP_INPUT_DOWN = 0x0600,
    PERLIN_OP_INPUT_OVERRIDE = 0x0601,
    PERLIN_OP_KEY_REGISTER = 0x0602,
    PERLIN_OP_INPUT_ACTIONS = 0x0603,
    PERLIN_OP_MOUSE_POSITION = 0x0604,
    PERLIN_OP_CHAT = 0x0700,
    PERLIN_OP_COMMAND = 0x0701,
    PERLIN_OP_LOCAL_MESSAGE = 0x0702,
    PERLIN_OP_COMMAND_REGISTER = 0x0703,
    PERLIN_OP_COMMAND_UNREGISTER = 0x0704,
    PERLIN_OP_SERVER_COMMANDS = 0x0705,
    PERLIN_OP_TOAST = 0x0706,
    PERLIN_OP_TITLE = 0x0707,
    PERLIN_OP_CHAT_HISTORY = 0x0708,
    PERLIN_OP_TITLE_STATE = 0x0709,
    PERLIN_OP_SWING_ARM = 0x0800,
    PERLIN_OP_USE_ITEM = 0x0801,
    PERLIN_OP_USE_ON_BLOCK = 0x0802,
    PERLIN_OP_ATTACK_ENTITY = 0x0803,
    PERLIN_OP_INTERACT_ENTITY = 0x0804,
    PERLIN_OP_BREAK_BLOCK = 0x0805,
    PERLIN_OP_LOOK = 0x0806,
    PERLIN_OP_RESPAWN = 0x0807,
    PERLIN_OP_DISCONNECT = 0x0808,
    PERLIN_OP_PLAY_SOUND = 0x0809,
    PERLIN_OP_JUMP = 0x080B,
    PERLIN_OP_UI_SUBMIT = 0x0900,
    PERLIN_OP_UI_CLEAR = 0x0901,
    PERLIN_OP_UI_MEASURE = 0x0902,
    PERLIN_OP_GFX_TEXTURE_CREATE = 0x0910,
    PERLIN_OP_GFX_TEXTURE_UPDATE = 0x0911,
    PERLIN_OP_GFX_TEXTURE_DESTROY = 0x0912,
    PERLIN_OP_GFX_SUBMIT = 0x0913,
    PERLIN_OP_GFX_CLEAR = 0x0914,
    PERLIN_OP_GFX_DISPLAY = 0x0915,
    PERLIN_OP_GFX_IMAGE_LOAD = 0x0916,
    PERLIN_OP_INPUT_GRAB = 0x0920,
    PERLIN_OP_NATIVE_INFO = 0x0930,
    PERLIN_OP_STORAGE_READ = 0x0A00,
    PERLIN_OP_STORAGE_WRITE = 0x0A01,
    PERLIN_OP_STORAGE_LIST = 0x0A02,
    PERLIN_OP_STORAGE_REMOVE = 0x0A03,
    PERLIN_OP_STORAGE_EXISTS = 0x0A04,
    PERLIN_OP_STORAGE_MAKE_DIR = 0x0A05,
    PERLIN_OP_HTTP_REQUEST = 0x0B00,
    PERLIN_OP_PACKET_SEND = 0x0B01,
    PERLIN_OP_PACKET_NAME = 0x0B02,
    PERLIN_OP_SETTING_GET = 0x0C01,
    PERLIN_OP_SETTING_SET = 0x0C02,
    PERLIN_OP_SETTING_LIST = 0x0C03,
    PERLIN_OP_MOD_LIST = 0x0C10,
    PERLIN_OP_ICON_SET = 0x0C11,
    PERLIN_OP_OPEN_SCREEN = 0x0D00,
    PERLIN_OP_OPEN_MOD_SETTINGS = 0x0D01,
    PERLIN_OP_TAKE_SCREENSHOT = 0x0D02,
    PERLIN_OP_CYCLE_PERSPECTIVE = 0x0D03,
    PERLIN_OP_COPY_TO_CLIPBOARD = 0x0D04,
    PERLIN_OP_OPEN_URL = 0x0D05,
    PERLIN_OP_TOGGLE_HUD = 0x0D06,
    PERLIN_OP_COPY_FILE = 0x0E00,
    PERLIN_OP_MOVE_FILE = 0x0E01,
    PERLIN_OP_DOWNLOAD_FILE = 0x0E02,
    PERLIN_OP_CAMERA_CONTROL = 0x0F00,
    PERLIN_OP_CAMERA_STATE = 0x0F01,
    PERLIN_OP_WORLD_TO_SCREEN = 0x0F02,
    PERLIN_OP_PLAYER_MOTION = 0x0F10,
    PERLIN_OP_PLAYER_TELEPORT = 0x0F11,
    PERLIN_OP_PLAYER_MODEL_POSE = 0x0F20,
    PERLIN_OP_PLAYER_SET_LOOK = 0x0F21,
    PERLIN_OP_WORLD_LINES = 0x0F30,
    PERLIN_OP_WORLD_BOXES = 0x0F31,
    PERLIN_OP_CLIENT_BLOCK_SET = 0x0F40,
    PERLIN_OP_CLIENT_BLOCK_GET = 0x0F41,
    PERLIN_OP_PACKET_DROP = 0x0F50,
    PERLIN_OP_PACKET_COUNTERS = 0x0F51,
    PERLIN_OP_PACKET_INJECT = 0x0F52,
    PERLIN_OP_HUD_ELEMENT_SET = 0x1000,
    PERLIN_OP_DEBUG_LINES = 0x1001,
    PERLIN_OP_FOG_SET = 0x1002,
    PERLIN_OP_SCREEN_TINT = 0x1003,
    PERLIN_OP_ENTITY_RENDER_OVERRIDE = 0x1010,
    PERLIN_OP_WORLD_MESH = 0x1011,
    PERLIN_OP_WORLD_MESH_CLEAR = 0x1012,
    PERLIN_OP_FRAME_LIMIT = 0x1020,
    PERLIN_OP_WINDOW_STATE = 0x1021,
    PERLIN_OP_SCREEN_BUTTON_ADD = 0x1030,
    PERLIN_OP_SCREEN_BUTTON_REMOVE = 0x1031,
    PERLIN_OP_SCREEN_OPEN_CUSTOM = 0x1032,
    PERLIN_OP_TOOLTIP_APPEND = 0x1033,
    PERLIN_OP_LOADING_OVERLAY = 0x1040,
    PERLIN_OP_SKIN_OVERRIDE = 0x1041,
    PERLIN_OP_AMBIENT_SOUND = 0x1050,
    PERLIN_OP_SERVER_PING = 0x1060,
    PERLIN_OP_MOD_SEND = 0x1070,
    PERLIN_OP_PACK_REGISTER = 0x1080,
    PERLIN_OP_SHADER_PACK_SET = 0x1081,
    PERLIN_OP_PARTICLE_SPAWN = 0x1090,
    PERLIN_OP_KEY_REGISTER_EX = 0x10A0,
    PERLIN_OP_BROADCAST = 0x7000,
    PERLIN_OP_PLAYER_MESSAGE = 0x7001,
    PERLIN_OP_KICK = 0x7002,
    PERLIN_OP_ONLINE_PLAYERS = 0x7003,
    PERLIN_OP_TELEPORT = 0x7004,
    PERLIN_OP_SET_BLOCK = 0x7005,
    PERLIN_OP_BLOCK_NAME = 0x7006,
    PERLIN_OP_SERVER_INFO = 0x7007,
    PERLIN_OP_GIVE_ITEM = 0x7008,
    PERLIN_OP_PLAYER_INFO = 0x7010,
    PERLIN_OP_PLAYER_SET_GAME_MODE = 0x7011,
    PERLIN_OP_PLAYER_SET_HEALTH = 0x7012,
    PERLIN_OP_PLAYER_DAMAGE = 0x7013,
    PERLIN_OP_PLAYER_KILL = 0x7014,
    PERLIN_OP_PLAYER_SET_FOOD = 0x7015,
    PERLIN_OP_PLAYER_ADD_EXPERIENCE = 0x7016,
    PERLIN_OP_PLAYER_EFFECT_ADD = 0x7017,
    PERLIN_OP_PLAYER_EFFECT_REMOVE = 0x7018,
    PERLIN_OP_PLAYER_INVENTORY = 0x7019,
    PERLIN_OP_PLAYER_SET_SLOT = 0x701A,
    PERLIN_OP_PLAYER_CLEAR_INVENTORY = 0x701B,
    PERLIN_OP_PLAYER_SET_VELOCITY = 0x701C,
    PERLIN_OP_PLAYER_TRANSFER_DIMENSION = 0x701D,
    PERLIN_OP_PLAYER_TITLE = 0x701E,
    PERLIN_OP_PLAYER_ACTION_BAR = 0x701F,
    PERLIN_OP_PLAYER_SOUND = 0x7020,
    PERLIN_OP_PLAYER_TRANSFER = 0x7021,
    PERLIN_OP_PLAYER_SET_OPERATOR = 0x7022,
    PERLIN_OP_PLAYER_FORM = 0x7023,
    PERLIN_OP_PLAYER_RUN_COMMAND = 0x7024,
    PERLIN_OP_PLAYER_SET_SPAWN = 0x7025,
    PERLIN_OP_PLAYER_BY_NAME = 0x7026,
    PERLIN_OP_BLOCK_STATE = 0x7040,
    PERLIN_OP_BLOCK_STATE_SET = 0x7041,
    PERLIN_OP_FILL = 0x7042,
    PERLIN_OP_TIME_GET = 0x7043,
    PERLIN_OP_TIME_SET = 0x7044,
    PERLIN_OP_WEATHER_SET = 0x7045,
    PERLIN_OP_GAME_RULE_GET = 0x7046,
    PERLIN_OP_GAME_RULE_SET = 0x7047,
    PERLIN_OP_DIFFICULTY_SET = 0x7048,
    PERLIN_OP_EXPLODE = 0x7049,
    PERLIN_OP_HIGHEST_BLOCK = 0x704A,
    PERLIN_OP_PARTICLE_AT = 0x704B,
    PERLIN_OP_DROP_ITEM = 0x704C,
    PERLIN_OP_WORLD_SPAWN_GET = 0x704D,
    PERLIN_OP_WORLD_SPAWN_SET = 0x704E,
    PERLIN_OP_ENTITY_SPAWN = 0x7060,
    PERLIN_OP_ENTITY_LIST = 0x7061,
    PERLIN_OP_ENTITY_REMOVE = 0x7062,
    PERLIN_OP_ENTITY_DAMAGE = 0x7063,
    PERLIN_OP_ENTITY_TELEPORT = 0x7064,
    PERLIN_OP_ENTITY_SET_VELOCITY = 0x7065,
    PERLIN_OP_ENTITY_INFO = 0x7066,
    PERLIN_OP_ENTITY_SET_HEALTH = 0x7067,
    PERLIN_OP_RUN_COMMAND = 0x7080,
    PERLIN_OP_MOTD_SET = 0x7081,
    PERLIN_OP_LOOT_TABLE_OVERRIDE = 0x7082,
    PERLIN_OP_LOOT_TABLE_GET = 0x7083,
    PERLIN_OP_EVENT_AMEND = 0x7084,
};
enum PerlinEventId {
    PERLIN_EVENT_LOAD = 1,
    PERLIN_EVENT_UNLOAD = 2,
    PERLIN_EVENT_TICK = 3,
    PERLIN_EVENT_FRAME = 4,
    PERLIN_EVENT_CHAT_RECEIVED = 5,
    PERLIN_EVENT_CHAT_SENDING = 6,
    PERLIN_EVENT_CONNECTED = 7,
    PERLIN_EVENT_DISCONNECTED = 8,
    PERLIN_EVENT_KEY = 9,
    PERLIN_EVENT_COMMAND = 10,
    PERLIN_EVENT_DAMAGE = 11,
    PERLIN_EVENT_DEATH = 12,
    PERLIN_EVENT_RESPAWNED = 13,
    PERLIN_EVENT_DIMENSION_CHANGED = 14,
    PERLIN_EVENT_SCREEN_CHANGED = 15,
    PERLIN_EVENT_SLOT_CHANGED = 16,
    PERLIN_EVENT_ENTITY_ADDED = 17,
    PERLIN_EVENT_ENTITY_REMOVED = 18,
    PERLIN_EVENT_SOUND = 19,
    PERLIN_EVENT_HTTP_RESPONSE = 20,
    PERLIN_EVENT_PACKET = 21,
    PERLIN_EVENT_TITLE = 22,
    PERLIN_EVENT_SETTING_CHANGED = 23,
    PERLIN_EVENT_LOOK = 24,
    PERLIN_EVENT_MOUSE_MOVE = 25,
    PERLIN_EVENT_MOUSE_BUTTON = 26,
    PERLIN_EVENT_MOUSE_WHEEL = 27,
    PERLIN_EVENT_KEY_RAW = 28,
    PERLIN_EVENT_TEXT_INPUT = 29,
    PERLIN_EVENT_SCREEN_RENDER = 30,
    PERLIN_EVENT_SLOT_CLICK = 31,
    PERLIN_EVENT_INTERACT = 32,
    PERLIN_EVENT_CHUNK_LOADED = 33,
    PERLIN_EVENT_CHUNK_UNLOADED = 34,
    PERLIN_EVENT_BLOCK_CHANGED = 35,
    PERLIN_EVENT_RESOURCES_RELOADED = 36,
    PERLIN_EVENT_LEVEL_LOADED = 37,
    PERLIN_EVENT_LEVEL_UNLOADED = 38,
    PERLIN_EVENT_MODS_LOADED = 39,
    PERLIN_EVENT_CLIENT_STOPPING = 40,
    PERLIN_EVENT_TOAST = 41,
    PERLIN_EVENT_MOD_MESSAGE = 42,
    PERLIN_EVENT_TOOLTIP = 43,
    PERLIN_EVENT_SCREEN_BUTTON = 44,
    PERLIN_EVENT_WINDOW_FOCUS = 45,
    PERLIN_EVENT_SERVER_PING = 46,
    PERLIN_EVENT_PLAYER_JOIN = 47,
    PERLIN_EVENT_PLAYER_QUIT = 48,
    PERLIN_EVENT_PLAYER_CHAT = 49,
    PERLIN_EVENT_BLOCK_BREAK = 50,
    PERLIN_EVENT_BLOCK_PLACE = 51,
    PERLIN_EVENT_PLAYER_DAMAGE = 52,
    PERLIN_EVENT_PLAYER_DEATH = 53,
    PERLIN_EVENT_PLAYER_RESPAWN = 54,
    PERLIN_EVENT_PLAYER_USE_BLOCK = 55,
    PERLIN_EVENT_PLAYER_ATTACK = 56,
    PERLIN_EVENT_ENTITY_DAMAGE = 57,
    PERLIN_EVENT_ENTITY_DEATH = 58,
    PERLIN_EVENT_ENTITY_SPAWN = 59,
    PERLIN_EVENT_ITEM_CONSUME = 60,
    PERLIN_EVENT_GAME_MODE_CHANGE = 61,
    PERLIN_EVENT_FORM_RESPONSE = 62,
    PERLIN_EVENT_PLAYER_MOVE = 63,
    PERLIN_EVENT_EXPLOSION = 64,
    PERLIN_EVENT_ITEM_DROP = 65,
    PERLIN_EVENT_ITEM_PICKUP = 66,
    PERLIN_EVENT_CONTAINER_OPEN = 67,
    PERLIN_EVENT_DIMENSION_CHANGE = 68,
    PERLIN_EVENT_SERVER_COMMAND = 69,
    PERLIN_EVENT_PRE_LOGIN = 70,
};
#define PERLIN_EVENT_COUNT 71
#define PERLIN_SUBSCRIPTION_WORDS 3
#define PERLIN_SUBSCRIBE(words, event) ((words)[(event) / 32] |= (1u << ((event) % 32)))

typedef struct PerlinItem PerlinItem;
typedef struct PerlinPlayerState PerlinPlayerState;
typedef struct PerlinEffect PerlinEffect;
typedef struct PerlinInventory PerlinInventory;
typedef struct PerlinAttribute PerlinAttribute;
typedef struct PerlinEntity PerlinEntity;
typedef struct PerlinPlayerEntry PerlinPlayerEntry;
typedef struct PerlinWorldState PerlinWorldState;
typedef struct PerlinBlockInfo PerlinBlockInfo;
typedef struct PerlinBlockProps PerlinBlockProps;
typedef struct PerlinBlockEntityInfo PerlinBlockEntityInfo;
typedef struct PerlinBreakState PerlinBreakState;
typedef struct PerlinRayHit PerlinRayHit;
typedef struct PerlinAimState PerlinAimState;
typedef struct PerlinScreenState PerlinScreenState;
typedef struct PerlinMeta PerlinMeta;
typedef struct PerlinInputOverrideState PerlinInputOverrideState;
typedef struct PerlinCameraControlState PerlinCameraControlState;
typedef struct PerlinCameraInfo PerlinCameraInfo;
typedef struct PerlinWorldPoint PerlinWorldPoint;
typedef struct PerlinScreenPoint PerlinScreenPoint;
typedef struct PerlinMotionRequest PerlinMotionRequest;
typedef struct PerlinTeleportRequest PerlinTeleportRequest;
typedef struct PerlinModelPose PerlinModelPose;
typedef struct PerlinLookSet PerlinLookSet;
typedef struct PerlinWorldLine PerlinWorldLine;
typedef struct PerlinWorldLines PerlinWorldLines;
typedef struct PerlinWorldBox PerlinWorldBox;
typedef struct PerlinWorldBoxes PerlinWorldBoxes;
typedef struct PerlinClientBlockEdit PerlinClientBlockEdit;
typedef struct PerlinClientBlockEdits PerlinClientBlockEdits;
typedef struct PerlinBlockQuery PerlinBlockQuery;
typedef struct PerlinBlockQueryResult PerlinBlockQueryResult;
typedef struct PerlinPacketDropRequest PerlinPacketDropRequest;
typedef struct PerlinPacketCount PerlinPacketCount;
typedef struct PerlinRegion PerlinRegion;
typedef struct PerlinFoundBlock PerlinFoundBlock;
typedef struct PerlinSettingDef PerlinSettingDef;
typedef struct PerlinSettingEntry PerlinSettingEntry;
typedef struct PerlinFileLocation PerlinFileLocation;
typedef struct PerlinModEntry PerlinModEntry;
typedef struct PerlinHudElementState PerlinHudElementState;
typedef struct PerlinDebugLinesData PerlinDebugLinesData;
typedef struct PerlinFogOverride PerlinFogOverride;
typedef struct PerlinScreenTintData PerlinScreenTintData;
typedef struct PerlinEntityOverride PerlinEntityOverride;
typedef struct PerlinMeshVertex PerlinMeshVertex;
typedef struct PerlinWorldMeshData PerlinWorldMeshData;
typedef struct PerlinFrameLimitData PerlinFrameLimitData;
typedef struct PerlinWindowInfo PerlinWindowInfo;
typedef struct PerlinScreenButtonDef PerlinScreenButtonDef;
typedef struct PerlinLoadingOverlayData PerlinLoadingOverlayData;
typedef struct PerlinSkinOverrideData PerlinSkinOverrideData;
typedef struct PerlinAmbientSoundData PerlinAmbientSoundData;
typedef struct PerlinServerPingRequest PerlinServerPingRequest;
typedef struct PerlinModMessageData PerlinModMessageData;
typedef struct PerlinPackRegistration PerlinPackRegistration;
typedef struct PerlinParticleRequest PerlinParticleRequest;
typedef struct PerlinKeyBindEx PerlinKeyBindEx;
typedef struct PerlinChatLine PerlinChatLine;
typedef struct PerlinPackEntry PerlinPackEntry;
typedef struct PerlinTitleStateData PerlinTitleStateData;
typedef struct PerlinGfxVertex PerlinGfxVertex;
typedef struct PerlinGfxCmd PerlinGfxCmd;
typedef struct PerlinGfxHeader PerlinGfxHeader;
typedef struct PerlinNativeHandles PerlinNativeHandles;
typedef struct PerlinGfxDisplayInfo PerlinGfxDisplayInfo;
typedef struct PerlinTextMessage PerlinTextMessage;
typedef struct PerlinPlayerText PerlinPlayerText;
typedef struct PerlinOnlinePlayer PerlinOnlinePlayer;
typedef struct PerlinOnlinePlayerList PerlinOnlinePlayerList;
typedef struct PerlinServerTeleport PerlinServerTeleport;
typedef struct PerlinBlockCoord PerlinBlockCoord;
typedef struct PerlinBlockWrite PerlinBlockWrite;
typedef struct PerlinServerStatus PerlinServerStatus;
typedef struct PerlinGiveItemRequest PerlinGiveItemRequest;
typedef struct PerlinGiveItemResult PerlinGiveItemResult;
typedef struct PerlinEventNothing PerlinEventNothing;
typedef struct PerlinEventTick PerlinEventTick;
typedef struct PerlinEventFrame PerlinEventFrame;
typedef struct PerlinEventChat PerlinEventChat;
typedef struct PerlinEventKey PerlinEventKey;
typedef struct PerlinEventCommand PerlinEventCommand;
typedef struct PerlinEventConnection PerlinEventConnection;
typedef struct PerlinEventDamage PerlinEventDamage;
typedef struct PerlinEventText PerlinEventText;
typedef struct PerlinEventDimension PerlinEventDimension;
typedef struct PerlinEventScreen PerlinEventScreen;
typedef struct PerlinEventSlot PerlinEventSlot;
typedef struct PerlinEventEntity PerlinEventEntity;
typedef struct PerlinEventSound PerlinEventSound;
typedef struct PerlinEventHttp PerlinEventHttp;
typedef struct PerlinEventPacket PerlinEventPacket;
typedef struct PerlinEventTitle PerlinEventTitle;
typedef struct PerlinEventLook PerlinEventLook;
typedef struct PerlinEventSettingChanged PerlinEventSettingChanged;
typedef struct PerlinEventMouseMove PerlinEventMouseMove;
typedef struct PerlinEventMouseButton PerlinEventMouseButton;
typedef struct PerlinEventMouseWheel PerlinEventMouseWheel;
typedef struct PerlinEventKeyRaw PerlinEventKeyRaw;
typedef struct PerlinEventScreenRender PerlinEventScreenRender;
typedef struct PerlinEventSlotClick PerlinEventSlotClick;
typedef struct PerlinEventInteract PerlinEventInteract;
typedef struct PerlinEventChunk PerlinEventChunk;
typedef struct PerlinEventBlockChanged PerlinEventBlockChanged;
typedef struct PerlinEventLevel PerlinEventLevel;
typedef struct PerlinEventToast PerlinEventToast;
typedef struct PerlinEventModMessage PerlinEventModMessage;
typedef struct PerlinEventTooltip PerlinEventTooltip;
typedef struct PerlinEventScreenButton PerlinEventScreenButton;
typedef struct PerlinEventWindow PerlinEventWindow;
typedef struct PerlinEventServerPing PerlinEventServerPing;
typedef struct PerlinEventPlayer PerlinEventPlayer;
typedef struct PerlinEventPlayerChat PerlinEventPlayerChat;
typedef struct PerlinEventPlayerBlock PerlinEventPlayerBlock;
typedef struct PerlinLogRequest PerlinLogRequest;
typedef struct PerlinPlayerEffectsResponse PerlinPlayerEffectsResponse;
typedef struct PerlinPlayerLookResponse PerlinPlayerLookResponse;
typedef struct PerlinInventorySelectRequest PerlinInventorySelectRequest;
typedef struct PerlinInventoryDropRequest PerlinInventoryDropRequest;
typedef struct PerlinInventorySwapRequest PerlinInventorySwapRequest;
typedef struct PerlinInventoryClickRequest PerlinInventoryClickRequest;
typedef struct PerlinBlockRequest PerlinBlockRequest;
typedef struct PerlinRegionRequest PerlinRegionRequest;
typedef struct PerlinRaycastRequest PerlinRaycastRequest;
typedef struct PerlinFindBlocksRequest PerlinFindBlocksRequest;
typedef struct PerlinFindBlocksResponse PerlinFindBlocksResponse;
typedef struct PerlinChunkLoadedRequest PerlinChunkLoadedRequest;
typedef struct PerlinBlockNamesResponse PerlinBlockNamesResponse;
typedef struct PerlinBlockPropsRequest PerlinBlockPropsRequest;
typedef struct PerlinBlockEntityDataRequest PerlinBlockEntityDataRequest;
typedef struct PerlinDisplayNameRequest PerlinDisplayNameRequest;
typedef struct PerlinMapChunkRequest PerlinMapChunkRequest;
typedef struct PerlinEntitiesRequest PerlinEntitiesRequest;
typedef struct PerlinEntitiesResponse PerlinEntitiesResponse;
typedef struct PerlinEntityByIdRequest PerlinEntityByIdRequest;
typedef struct PerlinPlayerListResponse PerlinPlayerListResponse;
typedef struct PerlinOptionGetRequest PerlinOptionGetRequest;
typedef struct PerlinOptionSetRequest PerlinOptionSetRequest;
typedef struct PerlinTranslateRequest PerlinTranslateRequest;
typedef struct PerlinPerspectiveSetRequest PerlinPerspectiveSetRequest;
typedef struct PerlinResourcePacksResponse PerlinResourcePacksResponse;
typedef struct PerlinInputDownRequest PerlinInputDownRequest;
typedef struct PerlinKeyRegisterRequest PerlinKeyRegisterRequest;
typedef struct PerlinInputActionsResponse PerlinInputActionsResponse;
typedef struct PerlinMousePositionResponse PerlinMousePositionResponse;
typedef struct PerlinChatRequest PerlinChatRequest;
typedef struct PerlinCommandRequest PerlinCommandRequest;
typedef struct PerlinLocalMessageRequest PerlinLocalMessageRequest;
typedef struct PerlinCommandRegisterRequest PerlinCommandRegisterRequest;
typedef struct PerlinCommandUnregisterRequest PerlinCommandUnregisterRequest;
typedef struct PerlinServerCommandsResponse PerlinServerCommandsResponse;
typedef struct PerlinToastRequest PerlinToastRequest;
typedef struct PerlinTitleRequest PerlinTitleRequest;
typedef struct PerlinChatHistoryRequest PerlinChatHistoryRequest;
typedef struct PerlinChatHistoryResponse PerlinChatHistoryResponse;
typedef struct PerlinUseOnBlockRequest PerlinUseOnBlockRequest;
typedef struct PerlinAttackEntityRequest PerlinAttackEntityRequest;
typedef struct PerlinInteractEntityRequest PerlinInteractEntityRequest;
typedef struct PerlinBreakBlockRequest PerlinBreakBlockRequest;
typedef struct PerlinLookRequest PerlinLookRequest;
typedef struct PerlinPlaySoundRequest PerlinPlaySoundRequest;
typedef struct PerlinUiSubmitRequest PerlinUiSubmitRequest;
typedef struct PerlinUiClearRequest PerlinUiClearRequest;
typedef struct PerlinUiMeasureRequest PerlinUiMeasureRequest;
typedef struct PerlinGfxTextureCreateRequest PerlinGfxTextureCreateRequest;
typedef struct PerlinGfxTextureUpdateRequest PerlinGfxTextureUpdateRequest;
typedef struct PerlinGfxTextureDestroyRequest PerlinGfxTextureDestroyRequest;
typedef struct PerlinGfxSubmitRequest PerlinGfxSubmitRequest;
typedef struct PerlinGfxClearRequest PerlinGfxClearRequest;
typedef struct PerlinGfxImageLoadRequest PerlinGfxImageLoadRequest;
typedef struct PerlinGfxImageLoadResponse PerlinGfxImageLoadResponse;
typedef struct PerlinInputGrabRequest PerlinInputGrabRequest;
typedef struct PerlinStorageReadRequest PerlinStorageReadRequest;
typedef struct PerlinStorageWriteRequest PerlinStorageWriteRequest;
typedef struct PerlinStorageListRequest PerlinStorageListRequest;
typedef struct PerlinStorageListResponse PerlinStorageListResponse;
typedef struct PerlinStorageRemoveRequest PerlinStorageRemoveRequest;
typedef struct PerlinStorageExistsRequest PerlinStorageExistsRequest;
typedef struct PerlinStorageMakeDirRequest PerlinStorageMakeDirRequest;
typedef struct PerlinHttpRequestRequest PerlinHttpRequestRequest;
typedef struct PerlinPacketSendRequest PerlinPacketSendRequest;
typedef struct PerlinPacketNameRequest PerlinPacketNameRequest;
typedef struct PerlinSettingGetRequest PerlinSettingGetRequest;
typedef struct PerlinSettingSetRequest PerlinSettingSetRequest;
typedef struct PerlinSettingListResponse PerlinSettingListResponse;
typedef struct PerlinModListResponse PerlinModListResponse;
typedef struct PerlinIconSetRequest PerlinIconSetRequest;
typedef struct PerlinOpenScreenRequest PerlinOpenScreenRequest;
typedef struct PerlinOpenModSettingsRequest PerlinOpenModSettingsRequest;
typedef struct PerlinCopyToClipboardRequest PerlinCopyToClipboardRequest;
typedef struct PerlinOpenUrlRequest PerlinOpenUrlRequest;
typedef struct PerlinCopyFileRequest PerlinCopyFileRequest;
typedef struct PerlinMoveFileRequest PerlinMoveFileRequest;
typedef struct PerlinDownloadFileRequest PerlinDownloadFileRequest;
typedef struct PerlinPacketCountersResponse PerlinPacketCountersResponse;
typedef struct PerlinPacketInjectRequest PerlinPacketInjectRequest;
typedef struct PerlinWorldMeshClearRequest PerlinWorldMeshClearRequest;
typedef struct PerlinScreenButtonRemoveRequest PerlinScreenButtonRemoveRequest;
typedef struct PerlinScreenOpenCustomRequest PerlinScreenOpenCustomRequest;
typedef struct PerlinTooltipAppendRequest PerlinTooltipAppendRequest;
typedef struct PerlinShaderPackSetRequest PerlinShaderPackSetRequest;
typedef struct PerlinServerItem PerlinServerItem;
typedef struct PerlinServerInventory PerlinServerInventory;
typedef struct PerlinPlayerDetails PerlinPlayerDetails;
typedef struct PerlinServerEntity PerlinServerEntity;
typedef struct PerlinFormButton PerlinFormButton;
typedef struct PerlinFormControl PerlinFormControl;
typedef struct PerlinForm PerlinForm;
typedef struct PerlinEventServerDamage PerlinEventServerDamage;
typedef struct PerlinEventServerDeath PerlinEventServerDeath;
typedef struct PerlinEventPlayerUse PerlinEventPlayerUse;
typedef struct PerlinEventPlayerAttack PerlinEventPlayerAttack;
typedef struct PerlinEventEntityDamage PerlinEventEntityDamage;
typedef struct PerlinEventEntityDeath PerlinEventEntityDeath;
typedef struct PerlinEventEntitySpawn PerlinEventEntitySpawn;
typedef struct PerlinEventItem PerlinEventItem;
typedef struct PerlinEventGameModeChange PerlinEventGameModeChange;
typedef struct PerlinEventFormResponse PerlinEventFormResponse;
typedef struct PerlinEventPlayerMove PerlinEventPlayerMove;
typedef struct PerlinEventExplosion PerlinEventExplosion;
typedef struct PerlinEventContainer PerlinEventContainer;
typedef struct PerlinEventPlayerDimension PerlinEventPlayerDimension;
typedef struct PerlinEventServerCommand PerlinEventServerCommand;
typedef struct PerlinEventPreLogin PerlinEventPreLogin;
typedef struct PerlinPlayerInfoRequest PerlinPlayerInfoRequest;
typedef struct PerlinPlayerSetGameModeRequest PerlinPlayerSetGameModeRequest;
typedef struct PerlinPlayerSetHealthRequest PerlinPlayerSetHealthRequest;
typedef struct PerlinPlayerDamageRequest PerlinPlayerDamageRequest;
typedef struct PerlinPlayerKillRequest PerlinPlayerKillRequest;
typedef struct PerlinPlayerSetFoodRequest PerlinPlayerSetFoodRequest;
typedef struct PerlinPlayerAddExperienceRequest PerlinPlayerAddExperienceRequest;
typedef struct PerlinPlayerEffectAddRequest PerlinPlayerEffectAddRequest;
typedef struct PerlinPlayerEffectRemoveRequest PerlinPlayerEffectRemoveRequest;
typedef struct PerlinPlayerInventoryRequest PerlinPlayerInventoryRequest;
typedef struct PerlinPlayerSetSlotRequest PerlinPlayerSetSlotRequest;
typedef struct PerlinPlayerClearInventoryRequest PerlinPlayerClearInventoryRequest;
typedef struct PerlinPlayerSetVelocityRequest PerlinPlayerSetVelocityRequest;
typedef struct PerlinPlayerTransferDimensionRequest PerlinPlayerTransferDimensionRequest;
typedef struct PerlinPlayerTitleRequest PerlinPlayerTitleRequest;
typedef struct PerlinPlayerActionBarRequest PerlinPlayerActionBarRequest;
typedef struct PerlinPlayerSoundRequest PerlinPlayerSoundRequest;
typedef struct PerlinPlayerTransferRequest PerlinPlayerTransferRequest;
typedef struct PerlinPlayerSetOperatorRequest PerlinPlayerSetOperatorRequest;
typedef struct PerlinPlayerFormRequest PerlinPlayerFormRequest;
typedef struct PerlinPlayerRunCommandRequest PerlinPlayerRunCommandRequest;
typedef struct PerlinPlayerSetSpawnRequest PerlinPlayerSetSpawnRequest;
typedef struct PerlinPlayerByNameRequest PerlinPlayerByNameRequest;
typedef struct PerlinBlockStateRequest PerlinBlockStateRequest;
typedef struct PerlinBlockStateSetRequest PerlinBlockStateSetRequest;
typedef struct PerlinFillRequest PerlinFillRequest;
typedef struct PerlinTimeGetResponse PerlinTimeGetResponse;
typedef struct PerlinTimeSetRequest PerlinTimeSetRequest;
typedef struct PerlinWeatherSetRequest PerlinWeatherSetRequest;
typedef struct PerlinGameRuleGetRequest PerlinGameRuleGetRequest;
typedef struct PerlinGameRuleSetRequest PerlinGameRuleSetRequest;
typedef struct PerlinDifficultySetRequest PerlinDifficultySetRequest;
typedef struct PerlinExplodeRequest PerlinExplodeRequest;
typedef struct PerlinHighestBlockRequest PerlinHighestBlockRequest;
typedef struct PerlinParticleAtRequest PerlinParticleAtRequest;
typedef struct PerlinDropItemRequest PerlinDropItemRequest;
typedef struct PerlinEntitySpawnRequest PerlinEntitySpawnRequest;
typedef struct PerlinEntityListRequest PerlinEntityListRequest;
typedef struct PerlinEntityListResponse PerlinEntityListResponse;
typedef struct PerlinEntityRemoveRequest PerlinEntityRemoveRequest;
typedef struct PerlinEntityDamageRequest PerlinEntityDamageRequest;
typedef struct PerlinEntityTeleportRequest PerlinEntityTeleportRequest;
typedef struct PerlinEntitySetVelocityRequest PerlinEntitySetVelocityRequest;
typedef struct PerlinEntityInfoRequest PerlinEntityInfoRequest;
typedef struct PerlinEntitySetHealthRequest PerlinEntitySetHealthRequest;
typedef struct PerlinRunCommandRequest PerlinRunCommandRequest;
typedef struct PerlinMotdSetRequest PerlinMotdSetRequest;
typedef struct PerlinLootTableOverrideRequest PerlinLootTableOverrideRequest;
typedef struct PerlinLootTableGetRequest PerlinLootTableGetRequest;
typedef struct PerlinEventAmendRequest PerlinEventAmendRequest;

typedef struct PerlinPair_I32_I32 { int32_t first; int32_t second; } PerlinPair_I32_I32;
typedef struct PerlinList_Pair_I32_I32 { PerlinPair_I32_I32* data; uint32_t len; } PerlinList_Pair_I32_I32;
typedef struct PerlinList_Str { perlin_str* data; uint32_t len; } PerlinList_Str;
typedef struct PerlinList_Attribute { PerlinAttribute* data; uint32_t len; } PerlinList_Attribute;
typedef struct PerlinPair_Str_F32 { perlin_str first; float second; } PerlinPair_Str_F32;
typedef struct PerlinList_Pair_Str_F32 { PerlinPair_Str_F32* data; uint32_t len; } PerlinList_Pair_Str_F32;
typedef struct PerlinPair_Str_Str { perlin_str first; perlin_str second; } PerlinPair_Str_Str;
typedef struct PerlinList_Pair_Str_Str { PerlinPair_Str_Str* data; uint32_t len; } PerlinList_Pair_Str_Str;
typedef struct PerlinPair_Str_I32 { perlin_str first; int32_t second; } PerlinPair_Str_I32;
typedef struct PerlinList_Pair_Str_I32 { PerlinPair_Str_I32* data; uint32_t len; } PerlinList_Pair_Str_I32;
typedef struct PerlinList_WorldLine { PerlinWorldLine* data; uint32_t len; } PerlinList_WorldLine;
typedef struct PerlinList_WorldBox { PerlinWorldBox* data; uint32_t len; } PerlinList_WorldBox;
typedef struct PerlinList_ClientBlockEdit { PerlinClientBlockEdit* data; uint32_t len; } PerlinList_ClientBlockEdit;
typedef struct PerlinList_U32 { uint32_t* data; uint32_t len; } PerlinList_U32;
typedef struct PerlinList_U16 { uint16_t* data; uint32_t len; } PerlinList_U16;
typedef struct PerlinList_MeshVertex { PerlinMeshVertex* data; uint32_t len; } PerlinList_MeshVertex;
typedef struct PerlinList_OnlinePlayer { PerlinOnlinePlayer* data; uint32_t len; } PerlinList_OnlinePlayer;
typedef struct PerlinList_Effect { PerlinEffect* data; uint32_t len; } PerlinList_Effect;
typedef struct PerlinList_FoundBlock { PerlinFoundBlock* data; uint32_t len; } PerlinList_FoundBlock;
typedef struct PerlinList_Entity { PerlinEntity* data; uint32_t len; } PerlinList_Entity;
typedef struct PerlinList_PlayerEntry { PerlinPlayerEntry* data; uint32_t len; } PerlinList_PlayerEntry;
typedef struct PerlinList_PackEntry { PerlinPackEntry* data; uint32_t len; } PerlinList_PackEntry;
typedef struct PerlinList_ChatLine { PerlinChatLine* data; uint32_t len; } PerlinList_ChatLine;
typedef struct PerlinList_SettingEntry { PerlinSettingEntry* data; uint32_t len; } PerlinList_SettingEntry;
typedef struct PerlinList_ModEntry { PerlinModEntry* data; uint32_t len; } PerlinList_ModEntry;
typedef struct PerlinList_PacketCount { PerlinPacketCount* data; uint32_t len; } PerlinList_PacketCount;
typedef struct PerlinList_ServerItem { PerlinServerItem* data; uint32_t len; } PerlinList_ServerItem;
typedef struct PerlinList_FormButton { PerlinFormButton* data; uint32_t len; } PerlinList_FormButton;
typedef struct PerlinList_FormControl { PerlinFormControl* data; uint32_t len; } PerlinList_FormControl;
typedef struct PerlinList_ServerEntity { PerlinServerEntity* data; uint32_t len; } PerlinList_ServerEntity;
struct PerlinItem {
    perlin_str name;
    int32_t count;
    int32_t aux;
    int32_t damage;
    uint8_t enchanted;
    perlin_str customName;
    PerlinList_Pair_I32_I32 enchants;
    PerlinList_Str lore;
};
struct PerlinPlayerState {
    uint64_t runtimeId;
    int64_t uniqueId;
    perlin_str name;
    double x;
    double y;
    double z;
    double vx;
    double vy;
    double vz;
    float yaw;
    float pitch;
    float headYaw;
    float eyeHeight;
    uint8_t onGround;
    uint8_t sprinting;
    uint8_t sneaking;
    uint8_t swimming;
    uint8_t gliding;
    uint8_t flying;
    uint8_t mayFly;
    uint8_t spectator;
    uint8_t onFire;
    uint8_t riding;
    uint8_t connected;
    PerlinGameMode gameMode;
    PerlinDimension dimension;
    int32_t selectedSlot;
    PerlinPermissionLevel permission;
    float health;
    float maxHealth;
    float absorption;
    float hunger;
    float saturation;
    float xpProgress;
    float walkSpeed;
    int32_t xpLevel;
    int32_t air;
    int32_t maxAir;
};
struct PerlinEffect {
    PerlinEffectId id;
    int32_t amplifier;
    int32_t ticks;
    uint8_t ambient;
    uint8_t particles;
};
struct PerlinInventory {
    PerlinItem slots[36];
    PerlinItem armor[4];
    PerlinItem offhand;
    PerlinItem cursor;
    int32_t selected;
    uint8_t open;
};
struct PerlinAttribute {
    perlin_str name;
    float min;
    float value;
    float max;
};
struct PerlinEntity {
    uint64_t runtimeId;
    int64_t uniqueId;
    perlin_str type;
    perlin_str name;
    perlin_str nameTag;
    perlin_str uuid;
    double x;
    double y;
    double z;
    float yaw;
    float pitch;
    float headYaw;
    float health;
    float scale;
    uint8_t onGround;
    uint8_t isPlayer;
    uint8_t isItem;
    uint8_t isSelf;
    uint64_t flags;
    perlin_str itemName;
    int32_t itemCount;
    perlin_str mainHand;
    perlin_str offHand;
    perlin_str armor[4];
    int64_t vehicleUid;
    int64_t ownerUid;
    int64_t targetUid;
    int32_t variant;
    int32_t markVariant;
    int32_t color;
    float maxHealth;
    uint8_t dying;
    PerlinList_Attribute attributes;
    PerlinList_Pair_Str_F32 properties;
    PerlinList_Pair_Str_Str propertyText;
};
struct PerlinPlayerEntry {
    perlin_str uuid;
    perlin_str name;
    int64_t uid;
    PerlinPermissionLevel permission;
};
struct PerlinWorldState {
    PerlinDimension dimension;
    PerlinDifficulty difficulty;
    PerlinGameMode worldGameMode;
    int64_t day;
    int32_t timeOfDay;
    double totalTicks;
    float rain;
    float thunder;
    uint8_t commandsEnabled;
    uint8_t haveSpawn;
    int32_t spawnX;
    int32_t spawnY;
    int32_t spawnZ;
    PerlinList_Pair_Str_I32 rules;
};
struct PerlinBlockInfo {
    uint8_t loaded;
    uint32_t runtimeId;
    perlin_str name;
    int32_t biome;
    uint32_t liquid;
    PerlinList_Pair_Str_Str states;
};
struct PerlinBlockProps {
    uint8_t known;
    float hardness;
    PerlinTool tool;
    int32_t harvestLevel;
    uint8_t needsSupportBelow;
    uint8_t breaksWholeStack;
    perlin_str displayName;
    PerlinList_Str properties;
};
struct PerlinBlockEntityInfo {
    uint8_t present;
    perlin_str id;
    int32_t color;
    uint8_t waxed;
    float rotation;
    perlin_str frontText;
    perlin_str backText;
    uint8_t frontGlow;
    uint8_t backGlow;
    int32_t base;
    int32_t bannerType;
    PerlinList_Pair_Str_I32 patterns;
    perlin_str nbt;
};
struct PerlinBreakState {
    uint8_t active;
    int32_t x;
    int32_t y;
    int32_t z;
    float progress;
};
struct PerlinRayHit {
    uint8_t hit;
    int32_t x;
    int32_t y;
    int32_t z;
    PerlinFace face;
    double hx;
    double hy;
    double hz;
    double distance;
    perlin_str block;
};
struct PerlinAimState {
    PerlinAimKind kind;
    PerlinRayHit block;
    uint64_t entity;
    perlin_str entityType;
    double distance;
};
struct PerlinScreenState {
    int32_t guiWidth;
    int32_t guiHeight;
    int32_t guiScale;
    int32_t framebufferWidth;
    int32_t framebufferHeight;
    float fps;
    float frameMs;
    PerlinScreen screen;
    uint8_t paused;
    uint8_t connected;
    uint8_t hudHidden;
    uint8_t debug;
    perlin_str server;
    perlin_str language;
    perlin_str platform;
    perlin_str gameVersion;
};
struct PerlinMeta {
    uint32_t apiVersion;
    perlin_str gameVersion;
    perlin_str plugin;
    perlin_str platform;
    uint32_t permissions;
    int64_t nowMs;
    uint64_t tick;
};
struct PerlinInputOverrideState {
    float forward;
    float strafe;
    uint8_t jump;
    uint8_t sneak;
    uint8_t sprint;
    uint8_t attack;
    uint8_t use;
    int32_t ticks;
};
struct PerlinCameraControlState {
    float roll;
    float fovAdd;
    float fovSet;
    float offsetX;
    float offsetY;
    float offsetZ;
    float yawAdd;
    float pitchAdd;
    int32_t ticks;
};
struct PerlinCameraInfo {
    double x;
    double y;
    double z;
    float yaw;
    float pitch;
    float fov;
    float aspect;
    float nearPlane;
    float farPlane;
    int32_t perspective;
    float viewProjection[16];
};
struct PerlinWorldPoint {
    double x;
    double y;
    double z;
};
struct PerlinScreenPoint {
    float x;
    float y;
    float depth;
    uint8_t visible;
};
struct PerlinMotionRequest {
    uint8_t add;
    double x;
    double y;
    double z;
    uint8_t axisX;
    uint8_t axisY;
    uint8_t axisZ;
};
struct PerlinTeleportRequest {
    double x;
    double y;
    double z;
    uint8_t keepVelocity;
};
struct PerlinModelPose {
    float roll;
    float pitch;
    float yaw;
    int32_t ticks;
};
struct PerlinLookSet {
    double x;
    double y;
    double z;
};
struct PerlinWorldLine {
    double x1;
    double y1;
    double z1;
    double x2;
    double y2;
    double z2;
    uint32_t color;
    float width;
};
struct PerlinWorldLines {
    int32_t ticks;
    PerlinList_WorldLine lines;
};
struct PerlinWorldBox {
    double x0;
    double y0;
    double z0;
    double x1;
    double y1;
    double z1;
    uint32_t color;
    float width;
};
struct PerlinWorldBoxes {
    int32_t ticks;
    PerlinList_WorldBox boxes;
};
struct PerlinClientBlockEdit {
    int32_t x;
    int32_t y;
    int32_t z;
    uint32_t runtimeId;
};
struct PerlinClientBlockEdits {
    PerlinList_ClientBlockEdit edits;
};
struct PerlinBlockQuery {
    perlin_str name;
    PerlinList_Pair_Str_Str states;
};
struct PerlinBlockQueryResult {
    uint8_t found;
    uint32_t runtimeId;
};
struct PerlinPacketDropRequest {
    int32_t ticks;
    PerlinList_U32 ids;
    uint8_t outgoing;
};
struct PerlinPacketCount {
    uint32_t id;
    uint32_t count;
};
struct PerlinRegion {
    int32_t x0;
    int32_t y0;
    int32_t z0;
    int32_t sx;
    int32_t sy;
    int32_t sz;
    PerlinList_Str palette;
    PerlinList_U16 cells;
};
struct PerlinFoundBlock {
    int32_t x;
    int32_t y;
    int32_t z;
    perlin_str name;
};
struct PerlinSettingDef {
    perlin_str id;
    PerlinSettingKind kind;
    perlin_str label;
    perlin_str description;
    perlin_str icon;
    perlin_str value;
    perlin_str fallback;
    double min;
    double max;
    double step;
    PerlinList_Str choices;
    PerlinList_Str choiceLabels;
    perlin_str unit;
    uint8_t restart;
};
struct PerlinSettingEntry {
    perlin_str id;
    perlin_str value;
};
struct PerlinFileLocation {
    PerlinArea area;
    perlin_str path;
};
struct PerlinModEntry {
    perlin_str name;
    perlin_str displayName;
    perlin_str description;
    perlin_str version;
    perlin_str author;
    perlin_str state;
    uint8_t enabled;
    uint8_t self;
    uint8_t hasSettings;
    perlin_str website;
    perlin_str arch;
    PerlinList_Str hooks;
    int32_t priority;
    uint32_t permissions;
};
struct PerlinHudElementState {
    PerlinHudElement element;
    uint8_t visible;
    float dx;
    float dy;
};
struct PerlinDebugLinesData {
    PerlinList_Str left;
    PerlinList_Str right;
};
struct PerlinFogOverride {
    uint8_t enabled;
    uint32_t color;
    float start;
    float end;
    int32_t ticks;
};
struct PerlinScreenTintData {
    uint32_t color;
    float vignette;
    int32_t ticks;
};
struct PerlinEntityOverride {
    uint64_t entity;
    perlin_str type;
    uint8_t hidden;
    float scaleX;
    float scaleY;
    float scaleZ;
    uint32_t tint;
    perlin_str texture;
    int32_t nameTag;
    perlin_str nameText;
    int32_t ticks;
};
struct PerlinMeshVertex {
    double x;
    double y;
    double z;
    float u;
    float v;
    uint32_t color;
};
struct PerlinWorldMeshData {
    perlin_str id;
    PerlinRenderStage stage;
    uint32_t texture;
    uint8_t depthTest;
    int32_t ticks;
    PerlinList_MeshVertex triangles;
};
struct PerlinFrameLimitData {
    int32_t fps;
    int32_t unfocusedFps;
};
struct PerlinWindowInfo {
    uint8_t focused;
    uint8_t minimized;
    int32_t width;
    int32_t height;
};
struct PerlinScreenButtonDef {
    PerlinScreen screen;
    perlin_str id;
    perlin_str label;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};
struct PerlinLoadingOverlayData {
    uint8_t hidden;
};
struct PerlinSkinOverrideData {
    perlin_str player;
    uint8_t slim;
    perlin_bytes png;
};
struct PerlinAmbientSoundData {
    perlin_str id;
    perlin_str sound;
    float volume;
    float pitch;
    uint8_t play;
};
struct PerlinServerPingRequest {
    perlin_str host;
    uint16_t port;
};
struct PerlinModMessageData {
    perlin_str to;
    perlin_str channel;
    perlin_bytes body;
};
struct PerlinPackRegistration {
    perlin_str path;
    uint8_t enabled;
};
struct PerlinParticleRequest {
    perlin_str name;
    double x;
    double y;
    double z;
    int32_t count;
    float spread;
};
struct PerlinKeyBindEx {
    perlin_str id;
    perlin_str label;
    perlin_str key;
    uint8_t ctrl;
    uint8_t shift;
    uint8_t alt;
    PerlinKeyContext context;
};
struct PerlinChatLine {
    perlin_str text;
    float alpha;
};
struct PerlinPackEntry {
    perlin_str name;
    uint8_t server;
};
struct PerlinTitleStateData {
    perlin_str title;
    perlin_str subtitle;
    perlin_str actionBar;
    float titleAlpha;
    float actionBarAlpha;
};
struct PerlinGfxVertex {
    float x;
    float y;
    float u;
    float v;
    uint32_t color;
};
struct PerlinGfxCmd {
    float clip[4];
    uint32_t texture;
    uint32_t indexOffset;
    uint32_t indexCount;
    uint32_t vertexOffset;
};
struct PerlinGfxHeader {
    uint32_t vertexCount;
    uint32_t indexCount;
    uint32_t commandCount;
    uint32_t flags;
};
struct PerlinNativeHandles {
    perlin_str api;
    perlin_str adapter;
    uint64_t window;
    uint64_t display;
    uint64_t device;
};
struct PerlinGfxDisplayInfo {
    int32_t width;
    int32_t height;
    int32_t guiScale;
    float mouseX;
    float mouseY;
    uint8_t mouseFree;
    uint8_t inGame;
};
struct PerlinTextMessage {
    perlin_str text;
};
struct PerlinPlayerText {
    uint64_t player;
    perlin_str text;
};
struct PerlinOnlinePlayer {
    uint64_t id;
    perlin_str name;
    double x;
    double y;
    double z;
};
struct PerlinOnlinePlayerList {
    PerlinList_OnlinePlayer players;
};
struct PerlinServerTeleport {
    uint64_t player;
    double x;
    double y;
    double z;
};
struct PerlinBlockCoord {
    int32_t x;
    int32_t y;
    int32_t z;
};
struct PerlinBlockWrite {
    int32_t x;
    int32_t y;
    int32_t z;
    perlin_str block;
};
struct PerlinServerStatus {
    perlin_str name;
    uint32_t players;
    uint32_t maxPlayers;
    uint64_t tick;
    perlin_str backend;
};
struct PerlinGiveItemRequest {
    uint64_t player;
    perlin_str item;
    uint32_t count;
};
struct PerlinGiveItemResult {
    uint32_t leftover;
};
struct PerlinEventNothing {
    uint8_t unused_;
};
struct PerlinEventTick {
    float dt;
    uint64_t tick;
};
struct PerlinEventFrame {
    float dt;
    float fps;
    int32_t guiWidth;
    int32_t guiHeight;
};
struct PerlinEventChat {
    PerlinChatType type;
    perlin_str sender;
    perlin_str message;
    perlin_str raw;
};
struct PerlinEventKey {
    perlin_str id;
    uint8_t down;
};
struct PerlinEventCommand {
    perlin_str name;
    perlin_str args;
    uint64_t player;
};
struct PerlinEventConnection {
    perlin_str server;
    perlin_str message;
    PerlinDisconnectReason reason;
};
struct PerlinEventDamage {
    uint8_t known;
    float dx;
    float dz;
    float health;
};
struct PerlinEventText {
    perlin_str text;
};
struct PerlinEventDimension {
    PerlinDimension from;
    PerlinDimension to;
};
struct PerlinEventScreen {
    PerlinScreen from;
    PerlinScreen to;
};
struct PerlinEventSlot {
    int32_t slot;
    PerlinItem item;
};
struct PerlinEventEntity {
    PerlinEntity entity;
};
struct PerlinEventSound {
    perlin_str name;
    double x;
    double y;
    double z;
    float volume;
    float pitch;
};
struct PerlinEventHttp {
    uint32_t id;
    int32_t status;
    perlin_str error;
    PerlinList_Pair_Str_Str headers;
    perlin_bytes body;
};
struct PerlinEventPacket {
    uint32_t id;
    uint8_t outgoing;
    perlin_bytes body;
};
struct PerlinEventTitle {
    perlin_str title;
    perlin_str subtitle;
    perlin_str actionBar;
};
struct PerlinEventLook {
    float dx;
    float dy;
    float dt;
    double dirX;
    double dirY;
    double dirZ;
    double vx;
    double vy;
    double vz;
    uint32_t keys;
    uint8_t gliding;
    uint8_t underwater;
    uint8_t thirdPerson;
    uint8_t onGround;
};
struct PerlinEventSettingChanged {
    perlin_str id;
    perlin_str value;
    PerlinSettingKind kind;
};
struct PerlinEventMouseMove {
    float x;
    float y;
};
struct PerlinEventMouseButton {
    int32_t button;
    uint8_t down;
    float x;
    float y;
};
struct PerlinEventMouseWheel {
    float dx;
    float dy;
};
struct PerlinEventKeyRaw {
    int32_t key;
    uint8_t down;
    uint8_t repeat;
    uint8_t ctrl;
    uint8_t shift;
    uint8_t alt;
};
struct PerlinEventScreenRender {
    PerlinScreen screen;
    int32_t guiWidth;
    int32_t guiHeight;
    float mouseX;
    float mouseY;
};
struct PerlinEventSlotClick {
    int32_t slot;
    uint8_t right;
    uint8_t shift;
};
struct PerlinEventInteract {
    PerlinInteractKind kind;
    int32_t x;
    int32_t y;
    int32_t z;
    PerlinFace face;
    uint64_t entity;
};
struct PerlinEventChunk {
    int32_t x;
    int32_t z;
    PerlinDimension dimension;
};
struct PerlinEventBlockChanged {
    int32_t x;
    int32_t y;
    int32_t z;
    perlin_str from;
    perlin_str to;
};
struct PerlinEventLevel {
    PerlinDimension dimension;
    perlin_str server;
};
struct PerlinEventToast {
    perlin_str title;
    perlin_str body;
};
struct PerlinEventModMessage {
    perlin_str from;
    perlin_str channel;
    perlin_bytes body;
};
struct PerlinEventTooltip {
    PerlinItem item;
    PerlinList_Str lines;
};
struct PerlinEventScreenButton {
    perlin_str id;
};
struct PerlinEventWindow {
    uint8_t focused;
    uint8_t minimized;
};
struct PerlinEventServerPing {
    uint32_t id;
    uint8_t ok;
    perlin_str motd;
    perlin_str version;
    int32_t players;
    int32_t maxPlayers;
    int32_t protocol;
    float latencyMs;
};
struct PerlinEventPlayer {
    uint64_t player;
    perlin_str name;
};
struct PerlinEventPlayerChat {
    uint64_t player;
    perlin_str name;
    perlin_str message;
};
struct PerlinEventPlayerBlock {
    uint64_t player;
    int32_t x;
    int32_t y;
    int32_t z;
    perlin_str block;
};
struct PerlinLogRequest {
    PerlinLogLevel level;
    perlin_str text;
};
struct PerlinPlayerEffectsResponse {
    PerlinList_Effect effects;
};
struct PerlinPlayerLookResponse {
    float yaw;
    float pitch;
};
struct PerlinInventorySelectRequest {
    int32_t slot;
};
struct PerlinInventoryDropRequest {
    int32_t count;
};
struct PerlinInventorySwapRequest {
    int32_t a;
    int32_t b;
};
struct PerlinInventoryClickRequest {
    int32_t slot;
    uint8_t right;
    uint8_t shift;
};
struct PerlinBlockRequest {
    int32_t x;
    int32_t y;
    int32_t z;
};
struct PerlinRegionRequest {
    int32_t x0;
    int32_t y0;
    int32_t z0;
    int32_t x1;
    int32_t y1;
    int32_t z1;
};
struct PerlinRaycastRequest {
    double ox;
    double oy;
    double oz;
    double dx;
    double dy;
    double dz;
    double maxDistance;
    uint8_t liquids;
};
struct PerlinFindBlocksRequest {
    int32_t x;
    int32_t y;
    int32_t z;
    int32_t radius;
    int32_t limit;
    uint8_t exact;
    perlin_str name;
};
struct PerlinFindBlocksResponse {
    PerlinList_FoundBlock blocks;
};
struct PerlinChunkLoadedRequest {
    int32_t x;
    int32_t z;
};
struct PerlinBlockNamesResponse {
    PerlinList_Str names;
};
struct PerlinBlockPropsRequest {
    perlin_str name;
};
struct PerlinBlockEntityDataRequest {
    int32_t x;
    int32_t y;
    int32_t z;
};
struct PerlinDisplayNameRequest {
    PerlinNameKind kind;
    perlin_str id;
    int32_t aux;
};
struct PerlinMapChunkRequest {
    int32_t x;
    int32_t z;
    int32_t caveY;
};
struct PerlinEntitiesRequest {
    double radius;
    perlin_str type;
    uint8_t playersOnly;
    uint8_t includeItems;
};
struct PerlinEntitiesResponse {
    PerlinList_Entity entities;
};
struct PerlinEntityByIdRequest {
    uint64_t id;
};
struct PerlinPlayerListResponse {
    PerlinList_PlayerEntry players;
};
struct PerlinOptionGetRequest {
    perlin_str key;
};
struct PerlinOptionSetRequest {
    perlin_str key;
    perlin_str value;
};
struct PerlinTranslateRequest {
    perlin_str key;
};
struct PerlinPerspectiveSetRequest {
    PerlinPerspective perspective;
};
struct PerlinResourcePacksResponse {
    PerlinList_PackEntry packs;
};
struct PerlinInputDownRequest {
    perlin_str name;
};
struct PerlinKeyRegisterRequest {
    perlin_str id;
    perlin_str label;
    perlin_str fallback;
};
struct PerlinInputActionsResponse {
    PerlinList_Str actions;
};
struct PerlinMousePositionResponse {
    float x;
    float y;
    uint8_t captured;
};
struct PerlinChatRequest {
    perlin_str text;
};
struct PerlinCommandRequest {
    perlin_str line;
};
struct PerlinLocalMessageRequest {
    perlin_str text;
};
struct PerlinCommandRegisterRequest {
    perlin_str name;
    perlin_str usage;
    perlin_str description;
};
struct PerlinCommandUnregisterRequest {
    perlin_str name;
};
struct PerlinServerCommandsResponse {
    PerlinList_Str names;
};
struct PerlinToastRequest {
    perlin_str text;
    int32_t ms;
};
struct PerlinTitleRequest {
    perlin_str title;
    perlin_str subtitle;
    int32_t fadeIn;
    int32_t stay;
    int32_t fadeOut;
};
struct PerlinChatHistoryRequest {
    uint32_t count;
};
struct PerlinChatHistoryResponse {
    PerlinList_ChatLine lines;
};
struct PerlinUseOnBlockRequest {
    int32_t x;
    int32_t y;
    int32_t z;
    PerlinFace face;
};
struct PerlinAttackEntityRequest {
    uint64_t entity;
};
struct PerlinInteractEntityRequest {
    uint64_t entity;
};
struct PerlinBreakBlockRequest {
    int32_t x;
    int32_t y;
    int32_t z;
    PerlinFace face;
};
struct PerlinLookRequest {
    float yaw;
    float pitch;
};
struct PerlinPlaySoundRequest {
    perlin_str name;
    uint8_t positional;
    float x;
    float y;
    float z;
    float volume;
    float pitch;
};
struct PerlinUiSubmitRequest {
    perlin_str layer;
    int32_t z;
    perlin_bytes commands;
};
struct PerlinUiClearRequest {
    perlin_str layer;
};
struct PerlinUiMeasureRequest {
    perlin_str text;
    float scale;
};
struct PerlinGfxTextureCreateRequest {
    int32_t width;
    int32_t height;
    uint8_t linear;
    perlin_bytes rgba;
};
struct PerlinGfxTextureUpdateRequest {
    uint32_t id;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    perlin_bytes rgba;
};
struct PerlinGfxTextureDestroyRequest {
    uint32_t id;
};
struct PerlinGfxSubmitRequest {
    perlin_str layer;
    int32_t z;
    perlin_bytes blob;
};
struct PerlinGfxClearRequest {
    perlin_str layer;
};
struct PerlinGfxImageLoadRequest {
    perlin_str path;
};
struct PerlinGfxImageLoadResponse {
    int32_t width;
    int32_t height;
    perlin_bytes rgba;
};
struct PerlinInputGrabRequest {
    uint8_t mouse;
    uint8_t keyboard;
};
struct PerlinStorageReadRequest {
    perlin_str path;
};
struct PerlinStorageWriteRequest {
    perlin_str path;
    perlin_bytes data;
};
struct PerlinStorageListRequest {
    perlin_str path;
};
struct PerlinStorageListResponse {
    PerlinList_Str names;
};
struct PerlinStorageRemoveRequest {
    perlin_str path;
};
struct PerlinStorageExistsRequest {
    perlin_str path;
};
struct PerlinStorageMakeDirRequest {
    perlin_str path;
};
struct PerlinHttpRequestRequest {
    uint32_t id;
    perlin_str method;
    perlin_str url;
    int32_t timeoutMs;
    PerlinList_Pair_Str_Str headers;
    perlin_bytes body;
};
struct PerlinPacketSendRequest {
    uint32_t id;
    perlin_bytes body;
};
struct PerlinPacketNameRequest {
    uint32_t id;
};
struct PerlinSettingGetRequest {
    perlin_str id;
};
struct PerlinSettingSetRequest {
    perlin_str id;
    perlin_str value;
};
struct PerlinSettingListResponse {
    PerlinList_SettingEntry settings;
};
struct PerlinModListResponse {
    PerlinList_ModEntry mods;
};
struct PerlinIconSetRequest {
    perlin_str path;
};
struct PerlinOpenScreenRequest {
    PerlinScreen screen;
};
struct PerlinOpenModSettingsRequest {
    perlin_str mod;
};
struct PerlinCopyToClipboardRequest {
    perlin_str text;
};
struct PerlinOpenUrlRequest {
    perlin_str url;
};
struct PerlinCopyFileRequest {
    PerlinFileLocation from;
    PerlinFileLocation to;
};
struct PerlinMoveFileRequest {
    PerlinFileLocation from;
    PerlinFileLocation to;
};
struct PerlinDownloadFileRequest {
    uint32_t id;
    perlin_str url;
    PerlinFileLocation to;
};
struct PerlinPacketCountersResponse {
    PerlinList_PacketCount counters;
};
struct PerlinPacketInjectRequest {
    uint32_t id;
    perlin_bytes body;
};
struct PerlinWorldMeshClearRequest {
    perlin_str id;
};
struct PerlinScreenButtonRemoveRequest {
    perlin_str id;
};
struct PerlinScreenOpenCustomRequest {
    perlin_str id;
};
struct PerlinTooltipAppendRequest {
    PerlinList_Str lines;
};
struct PerlinShaderPackSetRequest {
    perlin_str name;
};
struct PerlinServerItem {
    perlin_str name;
    uint32_t count;
    uint32_t damage;
};
struct PerlinServerInventory {
    PerlinList_ServerItem main;
    PerlinList_ServerItem armor;
    PerlinServerItem offhand;
    int32_t selected;
};
struct PerlinPlayerDetails {
    uint64_t id;
    perlin_str name;
    perlin_str xuid;
    perlin_str uuid;
    PerlinDimension dimension;
    double x;
    double y;
    double z;
    float yaw;
    float pitch;
    PerlinGameMode gameMode;
    float health;
    float maxHealth;
    float absorption;
    int32_t food;
    float saturation;
    int32_t xpLevel;
    float xpProgress;
    uint8_t onGround;
    uint8_t sneaking;
    uint8_t sprinting;
    uint8_t dead;
    uint8_t isOperator;
    PerlinList_Effect effects;
};
struct PerlinServerEntity {
    uint64_t id;
    perlin_str type;
    PerlinDimension dimension;
    double x;
    double y;
    double z;
    float yaw;
    float health;
    float maxHealth;
    uint8_t baby;
};
struct PerlinFormButton {
    perlin_str text;
    perlin_str image;
};
struct PerlinFormControl {
    PerlinFormElement kind;
    perlin_str text;
    PerlinList_Str options;
    float min;
    float max;
    float step;
    float defaultNumber;
    perlin_str defaultText;
    perlin_str placeholder;
};
struct PerlinForm {
    PerlinFormKind kind;
    perlin_str title;
    perlin_str body;
    PerlinList_FormButton buttons;
    PerlinList_FormControl controls;
};
struct PerlinEventServerDamage {
    uint64_t player;
    float amount;
    PerlinDamageCause cause;
    uint64_t attacker;
    perlin_str attackerType;
};
struct PerlinEventServerDeath {
    uint64_t player;
    PerlinDamageCause cause;
    uint64_t attacker;
    perlin_str message;
};
struct PerlinEventPlayerUse {
    uint64_t player;
    int32_t x;
    int32_t y;
    int32_t z;
    PerlinFace face;
    perlin_str block;
    perlin_str item;
};
struct PerlinEventPlayerAttack {
    uint64_t player;
    uint64_t target;
    perlin_str targetType;
    float damage;
};
struct PerlinEventEntityDamage {
    uint64_t entity;
    perlin_str type;
    float amount;
    PerlinDamageCause cause;
    uint64_t attacker;
};
struct PerlinEventEntityDeath {
    uint64_t entity;
    perlin_str type;
    double x;
    double y;
    double z;
    uint64_t killer;
};
struct PerlinEventEntitySpawn {
    uint64_t entity;
    perlin_str type;
    double x;
    double y;
    double z;
    uint8_t natural;
};
struct PerlinEventItem {
    uint64_t player;
    perlin_str item;
    uint32_t count;
};
struct PerlinEventGameModeChange {
    uint64_t player;
    PerlinGameMode from;
    PerlinGameMode to;
};
struct PerlinEventFormResponse {
    uint64_t player;
    uint32_t form;
    uint8_t closed;
    int32_t button;
    PerlinList_Str values;
};
struct PerlinEventPlayerMove {
    uint64_t player;
    PerlinDimension dimension;
    double x;
    double y;
    double z;
    float yaw;
    float pitch;
    uint8_t onGround;
};
struct PerlinEventExplosion {
    double x;
    double y;
    double z;
    float power;
    uint64_t source;
    perlin_str sourceType;
};
struct PerlinEventContainer {
    uint64_t player;
    int32_t x;
    int32_t y;
    int32_t z;
    perlin_str block;
};
struct PerlinEventPlayerDimension {
    uint64_t player;
    PerlinDimension from;
    PerlinDimension to;
};
struct PerlinEventServerCommand {
    uint64_t player;
    perlin_str line;
};
struct PerlinEventPreLogin {
    perlin_str name;
    perlin_str xuid;
    perlin_str address;
};
struct PerlinPlayerInfoRequest {
    uint64_t player;
};
struct PerlinPlayerSetGameModeRequest {
    uint64_t player;
    PerlinGameMode mode;
};
struct PerlinPlayerSetHealthRequest {
    uint64_t player;
    float health;
};
struct PerlinPlayerDamageRequest {
    uint64_t player;
    float amount;
    PerlinDamageCause cause;
    uint8_t knockback;
};
struct PerlinPlayerKillRequest {
    uint64_t player;
};
struct PerlinPlayerSetFoodRequest {
    uint64_t player;
    int32_t food;
    float saturation;
};
struct PerlinPlayerAddExperienceRequest {
    uint64_t player;
    int32_t points;
    int32_t levels;
};
struct PerlinPlayerEffectAddRequest {
    uint64_t player;
    PerlinEffectId effect;
    int32_t amplifier;
    int32_t ticks;
    uint8_t particles;
};
struct PerlinPlayerEffectRemoveRequest {
    uint64_t player;
    int32_t effect;
};
struct PerlinPlayerInventoryRequest {
    uint64_t player;
};
struct PerlinPlayerSetSlotRequest {
    uint64_t player;
    int32_t container;
    int32_t slot;
    PerlinServerItem item;
};
struct PerlinPlayerClearInventoryRequest {
    uint64_t player;
};
struct PerlinPlayerSetVelocityRequest {
    uint64_t player;
    double x;
    double y;
    double z;
};
struct PerlinPlayerTransferDimensionRequest {
    uint64_t player;
    PerlinDimension dimension;
    double x;
    double y;
    double z;
};
struct PerlinPlayerTitleRequest {
    uint64_t player;
    perlin_str title;
    perlin_str subtitle;
    int32_t fadeIn;
    int32_t stay;
    int32_t fadeOut;
};
struct PerlinPlayerActionBarRequest {
    uint64_t player;
    perlin_str text;
};
struct PerlinPlayerSoundRequest {
    uint64_t player;
    perlin_str sound;
    double x;
    double y;
    double z;
    float volume;
    float pitch;
};
struct PerlinPlayerTransferRequest {
    uint64_t player;
    perlin_str host;
    uint16_t port;
};
struct PerlinPlayerSetOperatorRequest {
    uint64_t player;
    uint8_t isOperator;
};
struct PerlinPlayerFormRequest {
    uint64_t player;
    PerlinForm form;
};
struct PerlinPlayerRunCommandRequest {
    uint64_t player;
    perlin_str line;
};
struct PerlinPlayerSetSpawnRequest {
    uint64_t player;
    int32_t x;
    int32_t y;
    int32_t z;
};
struct PerlinPlayerByNameRequest {
    perlin_str name;
};
struct PerlinBlockStateRequest {
    PerlinDimension dimension;
    int32_t x;
    int32_t y;
    int32_t z;
};
struct PerlinBlockStateSetRequest {
    PerlinDimension dimension;
    int32_t x;
    int32_t y;
    int32_t z;
    perlin_str name;
    PerlinList_Pair_Str_Str states;
};
struct PerlinFillRequest {
    PerlinDimension dimension;
    int32_t x0;
    int32_t y0;
    int32_t z0;
    int32_t x1;
    int32_t y1;
    int32_t z1;
    perlin_str block;
};
struct PerlinTimeGetResponse {
    int64_t time;
    int64_t day;
    int32_t timeOfDay;
};
struct PerlinTimeSetRequest {
    int64_t time;
};
struct PerlinWeatherSetRequest {
    PerlinWeather weather;
    int32_t ticks;
};
struct PerlinGameRuleGetRequest {
    perlin_str name;
};
struct PerlinGameRuleSetRequest {
    perlin_str name;
    int32_t value;
};
struct PerlinDifficultySetRequest {
    PerlinDifficulty difficulty;
};
struct PerlinExplodeRequest {
    PerlinDimension dimension;
    double x;
    double y;
    double z;
    float power;
    uint8_t breakBlocks;
};
struct PerlinHighestBlockRequest {
    PerlinDimension dimension;
    int32_t x;
    int32_t z;
};
struct PerlinParticleAtRequest {
    PerlinDimension dimension;
    perlin_str name;
    double x;
    double y;
    double z;
};
struct PerlinDropItemRequest {
    PerlinDimension dimension;
    PerlinServerItem item;
    double x;
    double y;
    double z;
};
struct PerlinEntitySpawnRequest {
    perlin_str type;
    double x;
    double y;
    double z;
    uint8_t baby;
};
struct PerlinEntityListRequest {
    double x;
    double y;
    double z;
    double radius;
    perlin_str type;
};
struct PerlinEntityListResponse {
    PerlinList_ServerEntity entities;
};
struct PerlinEntityRemoveRequest {
    uint64_t entity;
};
struct PerlinEntityDamageRequest {
    uint64_t entity;
    float amount;
};
struct PerlinEntityTeleportRequest {
    uint64_t entity;
    double x;
    double y;
    double z;
};
struct PerlinEntitySetVelocityRequest {
    uint64_t entity;
    double x;
    double y;
    double z;
};
struct PerlinEntityInfoRequest {
    uint64_t entity;
};
struct PerlinEntitySetHealthRequest {
    uint64_t entity;
    float health;
};
struct PerlinRunCommandRequest {
    perlin_str line;
};
struct PerlinMotdSetRequest {
    perlin_str motd;
};
struct PerlinLootTableOverrideRequest {
    perlin_str key;
    perlin_str json;
};
struct PerlinLootTableGetRequest {
    perlin_str key;
};
struct PerlinEventAmendRequest {
    perlin_str field;
    double number;
    perlin_str text;
};

static inline void perlin_write_Item(perlin_writer* w, const PerlinItem* v);
static inline void perlin_read_Item(perlin_reader* r, PerlinItem* v, perlin_arena* a);
static inline void perlin_write_PlayerState(perlin_writer* w, const PerlinPlayerState* v);
static inline void perlin_read_PlayerState(perlin_reader* r, PerlinPlayerState* v, perlin_arena* a);
static inline void perlin_write_Effect(perlin_writer* w, const PerlinEffect* v);
static inline void perlin_read_Effect(perlin_reader* r, PerlinEffect* v, perlin_arena* a);
static inline void perlin_write_Inventory(perlin_writer* w, const PerlinInventory* v);
static inline void perlin_read_Inventory(perlin_reader* r, PerlinInventory* v, perlin_arena* a);
static inline void perlin_write_Attribute(perlin_writer* w, const PerlinAttribute* v);
static inline void perlin_read_Attribute(perlin_reader* r, PerlinAttribute* v, perlin_arena* a);
static inline void perlin_write_Entity(perlin_writer* w, const PerlinEntity* v);
static inline void perlin_read_Entity(perlin_reader* r, PerlinEntity* v, perlin_arena* a);
static inline void perlin_write_PlayerEntry(perlin_writer* w, const PerlinPlayerEntry* v);
static inline void perlin_read_PlayerEntry(perlin_reader* r, PerlinPlayerEntry* v, perlin_arena* a);
static inline void perlin_write_WorldState(perlin_writer* w, const PerlinWorldState* v);
static inline void perlin_read_WorldState(perlin_reader* r, PerlinWorldState* v, perlin_arena* a);
static inline void perlin_write_BlockInfo(perlin_writer* w, const PerlinBlockInfo* v);
static inline void perlin_read_BlockInfo(perlin_reader* r, PerlinBlockInfo* v, perlin_arena* a);
static inline void perlin_write_BlockProps(perlin_writer* w, const PerlinBlockProps* v);
static inline void perlin_read_BlockProps(perlin_reader* r, PerlinBlockProps* v, perlin_arena* a);
static inline void perlin_write_BlockEntityInfo(perlin_writer* w, const PerlinBlockEntityInfo* v);
static inline void perlin_read_BlockEntityInfo(perlin_reader* r, PerlinBlockEntityInfo* v, perlin_arena* a);
static inline void perlin_write_BreakState(perlin_writer* w, const PerlinBreakState* v);
static inline void perlin_read_BreakState(perlin_reader* r, PerlinBreakState* v, perlin_arena* a);
static inline void perlin_write_RayHit(perlin_writer* w, const PerlinRayHit* v);
static inline void perlin_read_RayHit(perlin_reader* r, PerlinRayHit* v, perlin_arena* a);
static inline void perlin_write_AimState(perlin_writer* w, const PerlinAimState* v);
static inline void perlin_read_AimState(perlin_reader* r, PerlinAimState* v, perlin_arena* a);
static inline void perlin_write_ScreenState(perlin_writer* w, const PerlinScreenState* v);
static inline void perlin_read_ScreenState(perlin_reader* r, PerlinScreenState* v, perlin_arena* a);
static inline void perlin_write_Meta(perlin_writer* w, const PerlinMeta* v);
static inline void perlin_read_Meta(perlin_reader* r, PerlinMeta* v, perlin_arena* a);
static inline void perlin_write_InputOverrideState(perlin_writer* w, const PerlinInputOverrideState* v);
static inline void perlin_read_InputOverrideState(perlin_reader* r, PerlinInputOverrideState* v, perlin_arena* a);
static inline void perlin_write_CameraControlState(perlin_writer* w, const PerlinCameraControlState* v);
static inline void perlin_read_CameraControlState(perlin_reader* r, PerlinCameraControlState* v, perlin_arena* a);
static inline void perlin_write_CameraInfo(perlin_writer* w, const PerlinCameraInfo* v);
static inline void perlin_read_CameraInfo(perlin_reader* r, PerlinCameraInfo* v, perlin_arena* a);
static inline void perlin_write_WorldPoint(perlin_writer* w, const PerlinWorldPoint* v);
static inline void perlin_read_WorldPoint(perlin_reader* r, PerlinWorldPoint* v, perlin_arena* a);
static inline void perlin_write_ScreenPoint(perlin_writer* w, const PerlinScreenPoint* v);
static inline void perlin_read_ScreenPoint(perlin_reader* r, PerlinScreenPoint* v, perlin_arena* a);
static inline void perlin_write_MotionRequest(perlin_writer* w, const PerlinMotionRequest* v);
static inline void perlin_read_MotionRequest(perlin_reader* r, PerlinMotionRequest* v, perlin_arena* a);
static inline void perlin_write_TeleportRequest(perlin_writer* w, const PerlinTeleportRequest* v);
static inline void perlin_read_TeleportRequest(perlin_reader* r, PerlinTeleportRequest* v, perlin_arena* a);
static inline void perlin_write_ModelPose(perlin_writer* w, const PerlinModelPose* v);
static inline void perlin_read_ModelPose(perlin_reader* r, PerlinModelPose* v, perlin_arena* a);
static inline void perlin_write_LookSet(perlin_writer* w, const PerlinLookSet* v);
static inline void perlin_read_LookSet(perlin_reader* r, PerlinLookSet* v, perlin_arena* a);
static inline void perlin_write_WorldLine(perlin_writer* w, const PerlinWorldLine* v);
static inline void perlin_read_WorldLine(perlin_reader* r, PerlinWorldLine* v, perlin_arena* a);
static inline void perlin_write_WorldLines(perlin_writer* w, const PerlinWorldLines* v);
static inline void perlin_read_WorldLines(perlin_reader* r, PerlinWorldLines* v, perlin_arena* a);
static inline void perlin_write_WorldBox(perlin_writer* w, const PerlinWorldBox* v);
static inline void perlin_read_WorldBox(perlin_reader* r, PerlinWorldBox* v, perlin_arena* a);
static inline void perlin_write_WorldBoxes(perlin_writer* w, const PerlinWorldBoxes* v);
static inline void perlin_read_WorldBoxes(perlin_reader* r, PerlinWorldBoxes* v, perlin_arena* a);
static inline void perlin_write_ClientBlockEdit(perlin_writer* w, const PerlinClientBlockEdit* v);
static inline void perlin_read_ClientBlockEdit(perlin_reader* r, PerlinClientBlockEdit* v, perlin_arena* a);
static inline void perlin_write_ClientBlockEdits(perlin_writer* w, const PerlinClientBlockEdits* v);
static inline void perlin_read_ClientBlockEdits(perlin_reader* r, PerlinClientBlockEdits* v, perlin_arena* a);
static inline void perlin_write_BlockQuery(perlin_writer* w, const PerlinBlockQuery* v);
static inline void perlin_read_BlockQuery(perlin_reader* r, PerlinBlockQuery* v, perlin_arena* a);
static inline void perlin_write_BlockQueryResult(perlin_writer* w, const PerlinBlockQueryResult* v);
static inline void perlin_read_BlockQueryResult(perlin_reader* r, PerlinBlockQueryResult* v, perlin_arena* a);
static inline void perlin_write_PacketDropRequest(perlin_writer* w, const PerlinPacketDropRequest* v);
static inline void perlin_read_PacketDropRequest(perlin_reader* r, PerlinPacketDropRequest* v, perlin_arena* a);
static inline void perlin_write_PacketCount(perlin_writer* w, const PerlinPacketCount* v);
static inline void perlin_read_PacketCount(perlin_reader* r, PerlinPacketCount* v, perlin_arena* a);
static inline void perlin_write_Region(perlin_writer* w, const PerlinRegion* v);
static inline void perlin_read_Region(perlin_reader* r, PerlinRegion* v, perlin_arena* a);
static inline void perlin_write_FoundBlock(perlin_writer* w, const PerlinFoundBlock* v);
static inline void perlin_read_FoundBlock(perlin_reader* r, PerlinFoundBlock* v, perlin_arena* a);
static inline void perlin_write_SettingDef(perlin_writer* w, const PerlinSettingDef* v);
static inline void perlin_read_SettingDef(perlin_reader* r, PerlinSettingDef* v, perlin_arena* a);
static inline void perlin_write_SettingEntry(perlin_writer* w, const PerlinSettingEntry* v);
static inline void perlin_read_SettingEntry(perlin_reader* r, PerlinSettingEntry* v, perlin_arena* a);
static inline void perlin_write_FileLocation(perlin_writer* w, const PerlinFileLocation* v);
static inline void perlin_read_FileLocation(perlin_reader* r, PerlinFileLocation* v, perlin_arena* a);
static inline void perlin_write_ModEntry(perlin_writer* w, const PerlinModEntry* v);
static inline void perlin_read_ModEntry(perlin_reader* r, PerlinModEntry* v, perlin_arena* a);
static inline void perlin_write_HudElementState(perlin_writer* w, const PerlinHudElementState* v);
static inline void perlin_read_HudElementState(perlin_reader* r, PerlinHudElementState* v, perlin_arena* a);
static inline void perlin_write_DebugLinesData(perlin_writer* w, const PerlinDebugLinesData* v);
static inline void perlin_read_DebugLinesData(perlin_reader* r, PerlinDebugLinesData* v, perlin_arena* a);
static inline void perlin_write_FogOverride(perlin_writer* w, const PerlinFogOverride* v);
static inline void perlin_read_FogOverride(perlin_reader* r, PerlinFogOverride* v, perlin_arena* a);
static inline void perlin_write_ScreenTintData(perlin_writer* w, const PerlinScreenTintData* v);
static inline void perlin_read_ScreenTintData(perlin_reader* r, PerlinScreenTintData* v, perlin_arena* a);
static inline void perlin_write_EntityOverride(perlin_writer* w, const PerlinEntityOverride* v);
static inline void perlin_read_EntityOverride(perlin_reader* r, PerlinEntityOverride* v, perlin_arena* a);
static inline void perlin_write_MeshVertex(perlin_writer* w, const PerlinMeshVertex* v);
static inline void perlin_read_MeshVertex(perlin_reader* r, PerlinMeshVertex* v, perlin_arena* a);
static inline void perlin_write_WorldMeshData(perlin_writer* w, const PerlinWorldMeshData* v);
static inline void perlin_read_WorldMeshData(perlin_reader* r, PerlinWorldMeshData* v, perlin_arena* a);
static inline void perlin_write_FrameLimitData(perlin_writer* w, const PerlinFrameLimitData* v);
static inline void perlin_read_FrameLimitData(perlin_reader* r, PerlinFrameLimitData* v, perlin_arena* a);
static inline void perlin_write_WindowInfo(perlin_writer* w, const PerlinWindowInfo* v);
static inline void perlin_read_WindowInfo(perlin_reader* r, PerlinWindowInfo* v, perlin_arena* a);
static inline void perlin_write_ScreenButtonDef(perlin_writer* w, const PerlinScreenButtonDef* v);
static inline void perlin_read_ScreenButtonDef(perlin_reader* r, PerlinScreenButtonDef* v, perlin_arena* a);
static inline void perlin_write_LoadingOverlayData(perlin_writer* w, const PerlinLoadingOverlayData* v);
static inline void perlin_read_LoadingOverlayData(perlin_reader* r, PerlinLoadingOverlayData* v, perlin_arena* a);
static inline void perlin_write_SkinOverrideData(perlin_writer* w, const PerlinSkinOverrideData* v);
static inline void perlin_read_SkinOverrideData(perlin_reader* r, PerlinSkinOverrideData* v, perlin_arena* a);
static inline void perlin_write_AmbientSoundData(perlin_writer* w, const PerlinAmbientSoundData* v);
static inline void perlin_read_AmbientSoundData(perlin_reader* r, PerlinAmbientSoundData* v, perlin_arena* a);
static inline void perlin_write_ServerPingRequest(perlin_writer* w, const PerlinServerPingRequest* v);
static inline void perlin_read_ServerPingRequest(perlin_reader* r, PerlinServerPingRequest* v, perlin_arena* a);
static inline void perlin_write_ModMessageData(perlin_writer* w, const PerlinModMessageData* v);
static inline void perlin_read_ModMessageData(perlin_reader* r, PerlinModMessageData* v, perlin_arena* a);
static inline void perlin_write_PackRegistration(perlin_writer* w, const PerlinPackRegistration* v);
static inline void perlin_read_PackRegistration(perlin_reader* r, PerlinPackRegistration* v, perlin_arena* a);
static inline void perlin_write_ParticleRequest(perlin_writer* w, const PerlinParticleRequest* v);
static inline void perlin_read_ParticleRequest(perlin_reader* r, PerlinParticleRequest* v, perlin_arena* a);
static inline void perlin_write_KeyBindEx(perlin_writer* w, const PerlinKeyBindEx* v);
static inline void perlin_read_KeyBindEx(perlin_reader* r, PerlinKeyBindEx* v, perlin_arena* a);
static inline void perlin_write_ChatLine(perlin_writer* w, const PerlinChatLine* v);
static inline void perlin_read_ChatLine(perlin_reader* r, PerlinChatLine* v, perlin_arena* a);
static inline void perlin_write_PackEntry(perlin_writer* w, const PerlinPackEntry* v);
static inline void perlin_read_PackEntry(perlin_reader* r, PerlinPackEntry* v, perlin_arena* a);
static inline void perlin_write_TitleStateData(perlin_writer* w, const PerlinTitleStateData* v);
static inline void perlin_read_TitleStateData(perlin_reader* r, PerlinTitleStateData* v, perlin_arena* a);
static inline void perlin_write_GfxVertex(perlin_writer* w, const PerlinGfxVertex* v);
static inline void perlin_read_GfxVertex(perlin_reader* r, PerlinGfxVertex* v, perlin_arena* a);
static inline void perlin_write_GfxCmd(perlin_writer* w, const PerlinGfxCmd* v);
static inline void perlin_read_GfxCmd(perlin_reader* r, PerlinGfxCmd* v, perlin_arena* a);
static inline void perlin_write_GfxHeader(perlin_writer* w, const PerlinGfxHeader* v);
static inline void perlin_read_GfxHeader(perlin_reader* r, PerlinGfxHeader* v, perlin_arena* a);
static inline void perlin_write_NativeHandles(perlin_writer* w, const PerlinNativeHandles* v);
static inline void perlin_read_NativeHandles(perlin_reader* r, PerlinNativeHandles* v, perlin_arena* a);
static inline void perlin_write_GfxDisplayInfo(perlin_writer* w, const PerlinGfxDisplayInfo* v);
static inline void perlin_read_GfxDisplayInfo(perlin_reader* r, PerlinGfxDisplayInfo* v, perlin_arena* a);
static inline void perlin_write_TextMessage(perlin_writer* w, const PerlinTextMessage* v);
static inline void perlin_read_TextMessage(perlin_reader* r, PerlinTextMessage* v, perlin_arena* a);
static inline void perlin_write_PlayerText(perlin_writer* w, const PerlinPlayerText* v);
static inline void perlin_read_PlayerText(perlin_reader* r, PerlinPlayerText* v, perlin_arena* a);
static inline void perlin_write_OnlinePlayer(perlin_writer* w, const PerlinOnlinePlayer* v);
static inline void perlin_read_OnlinePlayer(perlin_reader* r, PerlinOnlinePlayer* v, perlin_arena* a);
static inline void perlin_write_OnlinePlayerList(perlin_writer* w, const PerlinOnlinePlayerList* v);
static inline void perlin_read_OnlinePlayerList(perlin_reader* r, PerlinOnlinePlayerList* v, perlin_arena* a);
static inline void perlin_write_ServerTeleport(perlin_writer* w, const PerlinServerTeleport* v);
static inline void perlin_read_ServerTeleport(perlin_reader* r, PerlinServerTeleport* v, perlin_arena* a);
static inline void perlin_write_BlockCoord(perlin_writer* w, const PerlinBlockCoord* v);
static inline void perlin_read_BlockCoord(perlin_reader* r, PerlinBlockCoord* v, perlin_arena* a);
static inline void perlin_write_BlockWrite(perlin_writer* w, const PerlinBlockWrite* v);
static inline void perlin_read_BlockWrite(perlin_reader* r, PerlinBlockWrite* v, perlin_arena* a);
static inline void perlin_write_ServerStatus(perlin_writer* w, const PerlinServerStatus* v);
static inline void perlin_read_ServerStatus(perlin_reader* r, PerlinServerStatus* v, perlin_arena* a);
static inline void perlin_write_GiveItemRequest(perlin_writer* w, const PerlinGiveItemRequest* v);
static inline void perlin_read_GiveItemRequest(perlin_reader* r, PerlinGiveItemRequest* v, perlin_arena* a);
static inline void perlin_write_GiveItemResult(perlin_writer* w, const PerlinGiveItemResult* v);
static inline void perlin_read_GiveItemResult(perlin_reader* r, PerlinGiveItemResult* v, perlin_arena* a);
static inline void perlin_write_EventNothing(perlin_writer* w, const PerlinEventNothing* v);
static inline void perlin_read_EventNothing(perlin_reader* r, PerlinEventNothing* v, perlin_arena* a);
static inline void perlin_write_EventTick(perlin_writer* w, const PerlinEventTick* v);
static inline void perlin_read_EventTick(perlin_reader* r, PerlinEventTick* v, perlin_arena* a);
static inline void perlin_write_EventFrame(perlin_writer* w, const PerlinEventFrame* v);
static inline void perlin_read_EventFrame(perlin_reader* r, PerlinEventFrame* v, perlin_arena* a);
static inline void perlin_write_EventChat(perlin_writer* w, const PerlinEventChat* v);
static inline void perlin_read_EventChat(perlin_reader* r, PerlinEventChat* v, perlin_arena* a);
static inline void perlin_write_EventKey(perlin_writer* w, const PerlinEventKey* v);
static inline void perlin_read_EventKey(perlin_reader* r, PerlinEventKey* v, perlin_arena* a);
static inline void perlin_write_EventCommand(perlin_writer* w, const PerlinEventCommand* v);
static inline void perlin_read_EventCommand(perlin_reader* r, PerlinEventCommand* v, perlin_arena* a);
static inline void perlin_write_EventConnection(perlin_writer* w, const PerlinEventConnection* v);
static inline void perlin_read_EventConnection(perlin_reader* r, PerlinEventConnection* v, perlin_arena* a);
static inline void perlin_write_EventDamage(perlin_writer* w, const PerlinEventDamage* v);
static inline void perlin_read_EventDamage(perlin_reader* r, PerlinEventDamage* v, perlin_arena* a);
static inline void perlin_write_EventText(perlin_writer* w, const PerlinEventText* v);
static inline void perlin_read_EventText(perlin_reader* r, PerlinEventText* v, perlin_arena* a);
static inline void perlin_write_EventDimension(perlin_writer* w, const PerlinEventDimension* v);
static inline void perlin_read_EventDimension(perlin_reader* r, PerlinEventDimension* v, perlin_arena* a);
static inline void perlin_write_EventScreen(perlin_writer* w, const PerlinEventScreen* v);
static inline void perlin_read_EventScreen(perlin_reader* r, PerlinEventScreen* v, perlin_arena* a);
static inline void perlin_write_EventSlot(perlin_writer* w, const PerlinEventSlot* v);
static inline void perlin_read_EventSlot(perlin_reader* r, PerlinEventSlot* v, perlin_arena* a);
static inline void perlin_write_EventEntity(perlin_writer* w, const PerlinEventEntity* v);
static inline void perlin_read_EventEntity(perlin_reader* r, PerlinEventEntity* v, perlin_arena* a);
static inline void perlin_write_EventSound(perlin_writer* w, const PerlinEventSound* v);
static inline void perlin_read_EventSound(perlin_reader* r, PerlinEventSound* v, perlin_arena* a);
static inline void perlin_write_EventHttp(perlin_writer* w, const PerlinEventHttp* v);
static inline void perlin_read_EventHttp(perlin_reader* r, PerlinEventHttp* v, perlin_arena* a);
static inline void perlin_write_EventPacket(perlin_writer* w, const PerlinEventPacket* v);
static inline void perlin_read_EventPacket(perlin_reader* r, PerlinEventPacket* v, perlin_arena* a);
static inline void perlin_write_EventTitle(perlin_writer* w, const PerlinEventTitle* v);
static inline void perlin_read_EventTitle(perlin_reader* r, PerlinEventTitle* v, perlin_arena* a);
static inline void perlin_write_EventLook(perlin_writer* w, const PerlinEventLook* v);
static inline void perlin_read_EventLook(perlin_reader* r, PerlinEventLook* v, perlin_arena* a);
static inline void perlin_write_EventSettingChanged(perlin_writer* w, const PerlinEventSettingChanged* v);
static inline void perlin_read_EventSettingChanged(perlin_reader* r, PerlinEventSettingChanged* v, perlin_arena* a);
static inline void perlin_write_EventMouseMove(perlin_writer* w, const PerlinEventMouseMove* v);
static inline void perlin_read_EventMouseMove(perlin_reader* r, PerlinEventMouseMove* v, perlin_arena* a);
static inline void perlin_write_EventMouseButton(perlin_writer* w, const PerlinEventMouseButton* v);
static inline void perlin_read_EventMouseButton(perlin_reader* r, PerlinEventMouseButton* v, perlin_arena* a);
static inline void perlin_write_EventMouseWheel(perlin_writer* w, const PerlinEventMouseWheel* v);
static inline void perlin_read_EventMouseWheel(perlin_reader* r, PerlinEventMouseWheel* v, perlin_arena* a);
static inline void perlin_write_EventKeyRaw(perlin_writer* w, const PerlinEventKeyRaw* v);
static inline void perlin_read_EventKeyRaw(perlin_reader* r, PerlinEventKeyRaw* v, perlin_arena* a);
static inline void perlin_write_EventScreenRender(perlin_writer* w, const PerlinEventScreenRender* v);
static inline void perlin_read_EventScreenRender(perlin_reader* r, PerlinEventScreenRender* v, perlin_arena* a);
static inline void perlin_write_EventSlotClick(perlin_writer* w, const PerlinEventSlotClick* v);
static inline void perlin_read_EventSlotClick(perlin_reader* r, PerlinEventSlotClick* v, perlin_arena* a);
static inline void perlin_write_EventInteract(perlin_writer* w, const PerlinEventInteract* v);
static inline void perlin_read_EventInteract(perlin_reader* r, PerlinEventInteract* v, perlin_arena* a);
static inline void perlin_write_EventChunk(perlin_writer* w, const PerlinEventChunk* v);
static inline void perlin_read_EventChunk(perlin_reader* r, PerlinEventChunk* v, perlin_arena* a);
static inline void perlin_write_EventBlockChanged(perlin_writer* w, const PerlinEventBlockChanged* v);
static inline void perlin_read_EventBlockChanged(perlin_reader* r, PerlinEventBlockChanged* v, perlin_arena* a);
static inline void perlin_write_EventLevel(perlin_writer* w, const PerlinEventLevel* v);
static inline void perlin_read_EventLevel(perlin_reader* r, PerlinEventLevel* v, perlin_arena* a);
static inline void perlin_write_EventToast(perlin_writer* w, const PerlinEventToast* v);
static inline void perlin_read_EventToast(perlin_reader* r, PerlinEventToast* v, perlin_arena* a);
static inline void perlin_write_EventModMessage(perlin_writer* w, const PerlinEventModMessage* v);
static inline void perlin_read_EventModMessage(perlin_reader* r, PerlinEventModMessage* v, perlin_arena* a);
static inline void perlin_write_EventTooltip(perlin_writer* w, const PerlinEventTooltip* v);
static inline void perlin_read_EventTooltip(perlin_reader* r, PerlinEventTooltip* v, perlin_arena* a);
static inline void perlin_write_EventScreenButton(perlin_writer* w, const PerlinEventScreenButton* v);
static inline void perlin_read_EventScreenButton(perlin_reader* r, PerlinEventScreenButton* v, perlin_arena* a);
static inline void perlin_write_EventWindow(perlin_writer* w, const PerlinEventWindow* v);
static inline void perlin_read_EventWindow(perlin_reader* r, PerlinEventWindow* v, perlin_arena* a);
static inline void perlin_write_EventServerPing(perlin_writer* w, const PerlinEventServerPing* v);
static inline void perlin_read_EventServerPing(perlin_reader* r, PerlinEventServerPing* v, perlin_arena* a);
static inline void perlin_write_EventPlayer(perlin_writer* w, const PerlinEventPlayer* v);
static inline void perlin_read_EventPlayer(perlin_reader* r, PerlinEventPlayer* v, perlin_arena* a);
static inline void perlin_write_EventPlayerChat(perlin_writer* w, const PerlinEventPlayerChat* v);
static inline void perlin_read_EventPlayerChat(perlin_reader* r, PerlinEventPlayerChat* v, perlin_arena* a);
static inline void perlin_write_EventPlayerBlock(perlin_writer* w, const PerlinEventPlayerBlock* v);
static inline void perlin_read_EventPlayerBlock(perlin_reader* r, PerlinEventPlayerBlock* v, perlin_arena* a);
static inline void perlin_write_LogRequest(perlin_writer* w, const PerlinLogRequest* v);
static inline void perlin_read_LogRequest(perlin_reader* r, PerlinLogRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerEffectsResponse(perlin_writer* w, const PerlinPlayerEffectsResponse* v);
static inline void perlin_read_PlayerEffectsResponse(perlin_reader* r, PerlinPlayerEffectsResponse* v, perlin_arena* a);
static inline void perlin_write_PlayerLookResponse(perlin_writer* w, const PerlinPlayerLookResponse* v);
static inline void perlin_read_PlayerLookResponse(perlin_reader* r, PerlinPlayerLookResponse* v, perlin_arena* a);
static inline void perlin_write_InventorySelectRequest(perlin_writer* w, const PerlinInventorySelectRequest* v);
static inline void perlin_read_InventorySelectRequest(perlin_reader* r, PerlinInventorySelectRequest* v, perlin_arena* a);
static inline void perlin_write_InventoryDropRequest(perlin_writer* w, const PerlinInventoryDropRequest* v);
static inline void perlin_read_InventoryDropRequest(perlin_reader* r, PerlinInventoryDropRequest* v, perlin_arena* a);
static inline void perlin_write_InventorySwapRequest(perlin_writer* w, const PerlinInventorySwapRequest* v);
static inline void perlin_read_InventorySwapRequest(perlin_reader* r, PerlinInventorySwapRequest* v, perlin_arena* a);
static inline void perlin_write_InventoryClickRequest(perlin_writer* w, const PerlinInventoryClickRequest* v);
static inline void perlin_read_InventoryClickRequest(perlin_reader* r, PerlinInventoryClickRequest* v, perlin_arena* a);
static inline void perlin_write_BlockRequest(perlin_writer* w, const PerlinBlockRequest* v);
static inline void perlin_read_BlockRequest(perlin_reader* r, PerlinBlockRequest* v, perlin_arena* a);
static inline void perlin_write_RegionRequest(perlin_writer* w, const PerlinRegionRequest* v);
static inline void perlin_read_RegionRequest(perlin_reader* r, PerlinRegionRequest* v, perlin_arena* a);
static inline void perlin_write_RaycastRequest(perlin_writer* w, const PerlinRaycastRequest* v);
static inline void perlin_read_RaycastRequest(perlin_reader* r, PerlinRaycastRequest* v, perlin_arena* a);
static inline void perlin_write_FindBlocksRequest(perlin_writer* w, const PerlinFindBlocksRequest* v);
static inline void perlin_read_FindBlocksRequest(perlin_reader* r, PerlinFindBlocksRequest* v, perlin_arena* a);
static inline void perlin_write_FindBlocksResponse(perlin_writer* w, const PerlinFindBlocksResponse* v);
static inline void perlin_read_FindBlocksResponse(perlin_reader* r, PerlinFindBlocksResponse* v, perlin_arena* a);
static inline void perlin_write_ChunkLoadedRequest(perlin_writer* w, const PerlinChunkLoadedRequest* v);
static inline void perlin_read_ChunkLoadedRequest(perlin_reader* r, PerlinChunkLoadedRequest* v, perlin_arena* a);
static inline void perlin_write_BlockNamesResponse(perlin_writer* w, const PerlinBlockNamesResponse* v);
static inline void perlin_read_BlockNamesResponse(perlin_reader* r, PerlinBlockNamesResponse* v, perlin_arena* a);
static inline void perlin_write_BlockPropsRequest(perlin_writer* w, const PerlinBlockPropsRequest* v);
static inline void perlin_read_BlockPropsRequest(perlin_reader* r, PerlinBlockPropsRequest* v, perlin_arena* a);
static inline void perlin_write_BlockEntityDataRequest(perlin_writer* w, const PerlinBlockEntityDataRequest* v);
static inline void perlin_read_BlockEntityDataRequest(perlin_reader* r, PerlinBlockEntityDataRequest* v, perlin_arena* a);
static inline void perlin_write_DisplayNameRequest(perlin_writer* w, const PerlinDisplayNameRequest* v);
static inline void perlin_read_DisplayNameRequest(perlin_reader* r, PerlinDisplayNameRequest* v, perlin_arena* a);
static inline void perlin_write_MapChunkRequest(perlin_writer* w, const PerlinMapChunkRequest* v);
static inline void perlin_read_MapChunkRequest(perlin_reader* r, PerlinMapChunkRequest* v, perlin_arena* a);
static inline void perlin_write_EntitiesRequest(perlin_writer* w, const PerlinEntitiesRequest* v);
static inline void perlin_read_EntitiesRequest(perlin_reader* r, PerlinEntitiesRequest* v, perlin_arena* a);
static inline void perlin_write_EntitiesResponse(perlin_writer* w, const PerlinEntitiesResponse* v);
static inline void perlin_read_EntitiesResponse(perlin_reader* r, PerlinEntitiesResponse* v, perlin_arena* a);
static inline void perlin_write_EntityByIdRequest(perlin_writer* w, const PerlinEntityByIdRequest* v);
static inline void perlin_read_EntityByIdRequest(perlin_reader* r, PerlinEntityByIdRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerListResponse(perlin_writer* w, const PerlinPlayerListResponse* v);
static inline void perlin_read_PlayerListResponse(perlin_reader* r, PerlinPlayerListResponse* v, perlin_arena* a);
static inline void perlin_write_OptionGetRequest(perlin_writer* w, const PerlinOptionGetRequest* v);
static inline void perlin_read_OptionGetRequest(perlin_reader* r, PerlinOptionGetRequest* v, perlin_arena* a);
static inline void perlin_write_OptionSetRequest(perlin_writer* w, const PerlinOptionSetRequest* v);
static inline void perlin_read_OptionSetRequest(perlin_reader* r, PerlinOptionSetRequest* v, perlin_arena* a);
static inline void perlin_write_TranslateRequest(perlin_writer* w, const PerlinTranslateRequest* v);
static inline void perlin_read_TranslateRequest(perlin_reader* r, PerlinTranslateRequest* v, perlin_arena* a);
static inline void perlin_write_PerspectiveSetRequest(perlin_writer* w, const PerlinPerspectiveSetRequest* v);
static inline void perlin_read_PerspectiveSetRequest(perlin_reader* r, PerlinPerspectiveSetRequest* v, perlin_arena* a);
static inline void perlin_write_ResourcePacksResponse(perlin_writer* w, const PerlinResourcePacksResponse* v);
static inline void perlin_read_ResourcePacksResponse(perlin_reader* r, PerlinResourcePacksResponse* v, perlin_arena* a);
static inline void perlin_write_InputDownRequest(perlin_writer* w, const PerlinInputDownRequest* v);
static inline void perlin_read_InputDownRequest(perlin_reader* r, PerlinInputDownRequest* v, perlin_arena* a);
static inline void perlin_write_KeyRegisterRequest(perlin_writer* w, const PerlinKeyRegisterRequest* v);
static inline void perlin_read_KeyRegisterRequest(perlin_reader* r, PerlinKeyRegisterRequest* v, perlin_arena* a);
static inline void perlin_write_InputActionsResponse(perlin_writer* w, const PerlinInputActionsResponse* v);
static inline void perlin_read_InputActionsResponse(perlin_reader* r, PerlinInputActionsResponse* v, perlin_arena* a);
static inline void perlin_write_MousePositionResponse(perlin_writer* w, const PerlinMousePositionResponse* v);
static inline void perlin_read_MousePositionResponse(perlin_reader* r, PerlinMousePositionResponse* v, perlin_arena* a);
static inline void perlin_write_ChatRequest(perlin_writer* w, const PerlinChatRequest* v);
static inline void perlin_read_ChatRequest(perlin_reader* r, PerlinChatRequest* v, perlin_arena* a);
static inline void perlin_write_CommandRequest(perlin_writer* w, const PerlinCommandRequest* v);
static inline void perlin_read_CommandRequest(perlin_reader* r, PerlinCommandRequest* v, perlin_arena* a);
static inline void perlin_write_LocalMessageRequest(perlin_writer* w, const PerlinLocalMessageRequest* v);
static inline void perlin_read_LocalMessageRequest(perlin_reader* r, PerlinLocalMessageRequest* v, perlin_arena* a);
static inline void perlin_write_CommandRegisterRequest(perlin_writer* w, const PerlinCommandRegisterRequest* v);
static inline void perlin_read_CommandRegisterRequest(perlin_reader* r, PerlinCommandRegisterRequest* v, perlin_arena* a);
static inline void perlin_write_CommandUnregisterRequest(perlin_writer* w, const PerlinCommandUnregisterRequest* v);
static inline void perlin_read_CommandUnregisterRequest(perlin_reader* r, PerlinCommandUnregisterRequest* v, perlin_arena* a);
static inline void perlin_write_ServerCommandsResponse(perlin_writer* w, const PerlinServerCommandsResponse* v);
static inline void perlin_read_ServerCommandsResponse(perlin_reader* r, PerlinServerCommandsResponse* v, perlin_arena* a);
static inline void perlin_write_ToastRequest(perlin_writer* w, const PerlinToastRequest* v);
static inline void perlin_read_ToastRequest(perlin_reader* r, PerlinToastRequest* v, perlin_arena* a);
static inline void perlin_write_TitleRequest(perlin_writer* w, const PerlinTitleRequest* v);
static inline void perlin_read_TitleRequest(perlin_reader* r, PerlinTitleRequest* v, perlin_arena* a);
static inline void perlin_write_ChatHistoryRequest(perlin_writer* w, const PerlinChatHistoryRequest* v);
static inline void perlin_read_ChatHistoryRequest(perlin_reader* r, PerlinChatHistoryRequest* v, perlin_arena* a);
static inline void perlin_write_ChatHistoryResponse(perlin_writer* w, const PerlinChatHistoryResponse* v);
static inline void perlin_read_ChatHistoryResponse(perlin_reader* r, PerlinChatHistoryResponse* v, perlin_arena* a);
static inline void perlin_write_UseOnBlockRequest(perlin_writer* w, const PerlinUseOnBlockRequest* v);
static inline void perlin_read_UseOnBlockRequest(perlin_reader* r, PerlinUseOnBlockRequest* v, perlin_arena* a);
static inline void perlin_write_AttackEntityRequest(perlin_writer* w, const PerlinAttackEntityRequest* v);
static inline void perlin_read_AttackEntityRequest(perlin_reader* r, PerlinAttackEntityRequest* v, perlin_arena* a);
static inline void perlin_write_InteractEntityRequest(perlin_writer* w, const PerlinInteractEntityRequest* v);
static inline void perlin_read_InteractEntityRequest(perlin_reader* r, PerlinInteractEntityRequest* v, perlin_arena* a);
static inline void perlin_write_BreakBlockRequest(perlin_writer* w, const PerlinBreakBlockRequest* v);
static inline void perlin_read_BreakBlockRequest(perlin_reader* r, PerlinBreakBlockRequest* v, perlin_arena* a);
static inline void perlin_write_LookRequest(perlin_writer* w, const PerlinLookRequest* v);
static inline void perlin_read_LookRequest(perlin_reader* r, PerlinLookRequest* v, perlin_arena* a);
static inline void perlin_write_PlaySoundRequest(perlin_writer* w, const PerlinPlaySoundRequest* v);
static inline void perlin_read_PlaySoundRequest(perlin_reader* r, PerlinPlaySoundRequest* v, perlin_arena* a);
static inline void perlin_write_UiSubmitRequest(perlin_writer* w, const PerlinUiSubmitRequest* v);
static inline void perlin_read_UiSubmitRequest(perlin_reader* r, PerlinUiSubmitRequest* v, perlin_arena* a);
static inline void perlin_write_UiClearRequest(perlin_writer* w, const PerlinUiClearRequest* v);
static inline void perlin_read_UiClearRequest(perlin_reader* r, PerlinUiClearRequest* v, perlin_arena* a);
static inline void perlin_write_UiMeasureRequest(perlin_writer* w, const PerlinUiMeasureRequest* v);
static inline void perlin_read_UiMeasureRequest(perlin_reader* r, PerlinUiMeasureRequest* v, perlin_arena* a);
static inline void perlin_write_GfxTextureCreateRequest(perlin_writer* w, const PerlinGfxTextureCreateRequest* v);
static inline void perlin_read_GfxTextureCreateRequest(perlin_reader* r, PerlinGfxTextureCreateRequest* v, perlin_arena* a);
static inline void perlin_write_GfxTextureUpdateRequest(perlin_writer* w, const PerlinGfxTextureUpdateRequest* v);
static inline void perlin_read_GfxTextureUpdateRequest(perlin_reader* r, PerlinGfxTextureUpdateRequest* v, perlin_arena* a);
static inline void perlin_write_GfxTextureDestroyRequest(perlin_writer* w, const PerlinGfxTextureDestroyRequest* v);
static inline void perlin_read_GfxTextureDestroyRequest(perlin_reader* r, PerlinGfxTextureDestroyRequest* v, perlin_arena* a);
static inline void perlin_write_GfxSubmitRequest(perlin_writer* w, const PerlinGfxSubmitRequest* v);
static inline void perlin_read_GfxSubmitRequest(perlin_reader* r, PerlinGfxSubmitRequest* v, perlin_arena* a);
static inline void perlin_write_GfxClearRequest(perlin_writer* w, const PerlinGfxClearRequest* v);
static inline void perlin_read_GfxClearRequest(perlin_reader* r, PerlinGfxClearRequest* v, perlin_arena* a);
static inline void perlin_write_GfxImageLoadRequest(perlin_writer* w, const PerlinGfxImageLoadRequest* v);
static inline void perlin_read_GfxImageLoadRequest(perlin_reader* r, PerlinGfxImageLoadRequest* v, perlin_arena* a);
static inline void perlin_write_GfxImageLoadResponse(perlin_writer* w, const PerlinGfxImageLoadResponse* v);
static inline void perlin_read_GfxImageLoadResponse(perlin_reader* r, PerlinGfxImageLoadResponse* v, perlin_arena* a);
static inline void perlin_write_InputGrabRequest(perlin_writer* w, const PerlinInputGrabRequest* v);
static inline void perlin_read_InputGrabRequest(perlin_reader* r, PerlinInputGrabRequest* v, perlin_arena* a);
static inline void perlin_write_StorageReadRequest(perlin_writer* w, const PerlinStorageReadRequest* v);
static inline void perlin_read_StorageReadRequest(perlin_reader* r, PerlinStorageReadRequest* v, perlin_arena* a);
static inline void perlin_write_StorageWriteRequest(perlin_writer* w, const PerlinStorageWriteRequest* v);
static inline void perlin_read_StorageWriteRequest(perlin_reader* r, PerlinStorageWriteRequest* v, perlin_arena* a);
static inline void perlin_write_StorageListRequest(perlin_writer* w, const PerlinStorageListRequest* v);
static inline void perlin_read_StorageListRequest(perlin_reader* r, PerlinStorageListRequest* v, perlin_arena* a);
static inline void perlin_write_StorageListResponse(perlin_writer* w, const PerlinStorageListResponse* v);
static inline void perlin_read_StorageListResponse(perlin_reader* r, PerlinStorageListResponse* v, perlin_arena* a);
static inline void perlin_write_StorageRemoveRequest(perlin_writer* w, const PerlinStorageRemoveRequest* v);
static inline void perlin_read_StorageRemoveRequest(perlin_reader* r, PerlinStorageRemoveRequest* v, perlin_arena* a);
static inline void perlin_write_StorageExistsRequest(perlin_writer* w, const PerlinStorageExistsRequest* v);
static inline void perlin_read_StorageExistsRequest(perlin_reader* r, PerlinStorageExistsRequest* v, perlin_arena* a);
static inline void perlin_write_StorageMakeDirRequest(perlin_writer* w, const PerlinStorageMakeDirRequest* v);
static inline void perlin_read_StorageMakeDirRequest(perlin_reader* r, PerlinStorageMakeDirRequest* v, perlin_arena* a);
static inline void perlin_write_HttpRequestRequest(perlin_writer* w, const PerlinHttpRequestRequest* v);
static inline void perlin_read_HttpRequestRequest(perlin_reader* r, PerlinHttpRequestRequest* v, perlin_arena* a);
static inline void perlin_write_PacketSendRequest(perlin_writer* w, const PerlinPacketSendRequest* v);
static inline void perlin_read_PacketSendRequest(perlin_reader* r, PerlinPacketSendRequest* v, perlin_arena* a);
static inline void perlin_write_PacketNameRequest(perlin_writer* w, const PerlinPacketNameRequest* v);
static inline void perlin_read_PacketNameRequest(perlin_reader* r, PerlinPacketNameRequest* v, perlin_arena* a);
static inline void perlin_write_SettingGetRequest(perlin_writer* w, const PerlinSettingGetRequest* v);
static inline void perlin_read_SettingGetRequest(perlin_reader* r, PerlinSettingGetRequest* v, perlin_arena* a);
static inline void perlin_write_SettingSetRequest(perlin_writer* w, const PerlinSettingSetRequest* v);
static inline void perlin_read_SettingSetRequest(perlin_reader* r, PerlinSettingSetRequest* v, perlin_arena* a);
static inline void perlin_write_SettingListResponse(perlin_writer* w, const PerlinSettingListResponse* v);
static inline void perlin_read_SettingListResponse(perlin_reader* r, PerlinSettingListResponse* v, perlin_arena* a);
static inline void perlin_write_ModListResponse(perlin_writer* w, const PerlinModListResponse* v);
static inline void perlin_read_ModListResponse(perlin_reader* r, PerlinModListResponse* v, perlin_arena* a);
static inline void perlin_write_IconSetRequest(perlin_writer* w, const PerlinIconSetRequest* v);
static inline void perlin_read_IconSetRequest(perlin_reader* r, PerlinIconSetRequest* v, perlin_arena* a);
static inline void perlin_write_OpenScreenRequest(perlin_writer* w, const PerlinOpenScreenRequest* v);
static inline void perlin_read_OpenScreenRequest(perlin_reader* r, PerlinOpenScreenRequest* v, perlin_arena* a);
static inline void perlin_write_OpenModSettingsRequest(perlin_writer* w, const PerlinOpenModSettingsRequest* v);
static inline void perlin_read_OpenModSettingsRequest(perlin_reader* r, PerlinOpenModSettingsRequest* v, perlin_arena* a);
static inline void perlin_write_CopyToClipboardRequest(perlin_writer* w, const PerlinCopyToClipboardRequest* v);
static inline void perlin_read_CopyToClipboardRequest(perlin_reader* r, PerlinCopyToClipboardRequest* v, perlin_arena* a);
static inline void perlin_write_OpenUrlRequest(perlin_writer* w, const PerlinOpenUrlRequest* v);
static inline void perlin_read_OpenUrlRequest(perlin_reader* r, PerlinOpenUrlRequest* v, perlin_arena* a);
static inline void perlin_write_CopyFileRequest(perlin_writer* w, const PerlinCopyFileRequest* v);
static inline void perlin_read_CopyFileRequest(perlin_reader* r, PerlinCopyFileRequest* v, perlin_arena* a);
static inline void perlin_write_MoveFileRequest(perlin_writer* w, const PerlinMoveFileRequest* v);
static inline void perlin_read_MoveFileRequest(perlin_reader* r, PerlinMoveFileRequest* v, perlin_arena* a);
static inline void perlin_write_DownloadFileRequest(perlin_writer* w, const PerlinDownloadFileRequest* v);
static inline void perlin_read_DownloadFileRequest(perlin_reader* r, PerlinDownloadFileRequest* v, perlin_arena* a);
static inline void perlin_write_PacketCountersResponse(perlin_writer* w, const PerlinPacketCountersResponse* v);
static inline void perlin_read_PacketCountersResponse(perlin_reader* r, PerlinPacketCountersResponse* v, perlin_arena* a);
static inline void perlin_write_PacketInjectRequest(perlin_writer* w, const PerlinPacketInjectRequest* v);
static inline void perlin_read_PacketInjectRequest(perlin_reader* r, PerlinPacketInjectRequest* v, perlin_arena* a);
static inline void perlin_write_WorldMeshClearRequest(perlin_writer* w, const PerlinWorldMeshClearRequest* v);
static inline void perlin_read_WorldMeshClearRequest(perlin_reader* r, PerlinWorldMeshClearRequest* v, perlin_arena* a);
static inline void perlin_write_ScreenButtonRemoveRequest(perlin_writer* w, const PerlinScreenButtonRemoveRequest* v);
static inline void perlin_read_ScreenButtonRemoveRequest(perlin_reader* r, PerlinScreenButtonRemoveRequest* v, perlin_arena* a);
static inline void perlin_write_ScreenOpenCustomRequest(perlin_writer* w, const PerlinScreenOpenCustomRequest* v);
static inline void perlin_read_ScreenOpenCustomRequest(perlin_reader* r, PerlinScreenOpenCustomRequest* v, perlin_arena* a);
static inline void perlin_write_TooltipAppendRequest(perlin_writer* w, const PerlinTooltipAppendRequest* v);
static inline void perlin_read_TooltipAppendRequest(perlin_reader* r, PerlinTooltipAppendRequest* v, perlin_arena* a);
static inline void perlin_write_ShaderPackSetRequest(perlin_writer* w, const PerlinShaderPackSetRequest* v);
static inline void perlin_read_ShaderPackSetRequest(perlin_reader* r, PerlinShaderPackSetRequest* v, perlin_arena* a);
static inline void perlin_write_ServerItem(perlin_writer* w, const PerlinServerItem* v);
static inline void perlin_read_ServerItem(perlin_reader* r, PerlinServerItem* v, perlin_arena* a);
static inline void perlin_write_ServerInventory(perlin_writer* w, const PerlinServerInventory* v);
static inline void perlin_read_ServerInventory(perlin_reader* r, PerlinServerInventory* v, perlin_arena* a);
static inline void perlin_write_PlayerDetails(perlin_writer* w, const PerlinPlayerDetails* v);
static inline void perlin_read_PlayerDetails(perlin_reader* r, PerlinPlayerDetails* v, perlin_arena* a);
static inline void perlin_write_ServerEntity(perlin_writer* w, const PerlinServerEntity* v);
static inline void perlin_read_ServerEntity(perlin_reader* r, PerlinServerEntity* v, perlin_arena* a);
static inline void perlin_write_FormButton(perlin_writer* w, const PerlinFormButton* v);
static inline void perlin_read_FormButton(perlin_reader* r, PerlinFormButton* v, perlin_arena* a);
static inline void perlin_write_FormControl(perlin_writer* w, const PerlinFormControl* v);
static inline void perlin_read_FormControl(perlin_reader* r, PerlinFormControl* v, perlin_arena* a);
static inline void perlin_write_Form(perlin_writer* w, const PerlinForm* v);
static inline void perlin_read_Form(perlin_reader* r, PerlinForm* v, perlin_arena* a);
static inline void perlin_write_EventServerDamage(perlin_writer* w, const PerlinEventServerDamage* v);
static inline void perlin_read_EventServerDamage(perlin_reader* r, PerlinEventServerDamage* v, perlin_arena* a);
static inline void perlin_write_EventServerDeath(perlin_writer* w, const PerlinEventServerDeath* v);
static inline void perlin_read_EventServerDeath(perlin_reader* r, PerlinEventServerDeath* v, perlin_arena* a);
static inline void perlin_write_EventPlayerUse(perlin_writer* w, const PerlinEventPlayerUse* v);
static inline void perlin_read_EventPlayerUse(perlin_reader* r, PerlinEventPlayerUse* v, perlin_arena* a);
static inline void perlin_write_EventPlayerAttack(perlin_writer* w, const PerlinEventPlayerAttack* v);
static inline void perlin_read_EventPlayerAttack(perlin_reader* r, PerlinEventPlayerAttack* v, perlin_arena* a);
static inline void perlin_write_EventEntityDamage(perlin_writer* w, const PerlinEventEntityDamage* v);
static inline void perlin_read_EventEntityDamage(perlin_reader* r, PerlinEventEntityDamage* v, perlin_arena* a);
static inline void perlin_write_EventEntityDeath(perlin_writer* w, const PerlinEventEntityDeath* v);
static inline void perlin_read_EventEntityDeath(perlin_reader* r, PerlinEventEntityDeath* v, perlin_arena* a);
static inline void perlin_write_EventEntitySpawn(perlin_writer* w, const PerlinEventEntitySpawn* v);
static inline void perlin_read_EventEntitySpawn(perlin_reader* r, PerlinEventEntitySpawn* v, perlin_arena* a);
static inline void perlin_write_EventItem(perlin_writer* w, const PerlinEventItem* v);
static inline void perlin_read_EventItem(perlin_reader* r, PerlinEventItem* v, perlin_arena* a);
static inline void perlin_write_EventGameModeChange(perlin_writer* w, const PerlinEventGameModeChange* v);
static inline void perlin_read_EventGameModeChange(perlin_reader* r, PerlinEventGameModeChange* v, perlin_arena* a);
static inline void perlin_write_EventFormResponse(perlin_writer* w, const PerlinEventFormResponse* v);
static inline void perlin_read_EventFormResponse(perlin_reader* r, PerlinEventFormResponse* v, perlin_arena* a);
static inline void perlin_write_EventPlayerMove(perlin_writer* w, const PerlinEventPlayerMove* v);
static inline void perlin_read_EventPlayerMove(perlin_reader* r, PerlinEventPlayerMove* v, perlin_arena* a);
static inline void perlin_write_EventExplosion(perlin_writer* w, const PerlinEventExplosion* v);
static inline void perlin_read_EventExplosion(perlin_reader* r, PerlinEventExplosion* v, perlin_arena* a);
static inline void perlin_write_EventContainer(perlin_writer* w, const PerlinEventContainer* v);
static inline void perlin_read_EventContainer(perlin_reader* r, PerlinEventContainer* v, perlin_arena* a);
static inline void perlin_write_EventPlayerDimension(perlin_writer* w, const PerlinEventPlayerDimension* v);
static inline void perlin_read_EventPlayerDimension(perlin_reader* r, PerlinEventPlayerDimension* v, perlin_arena* a);
static inline void perlin_write_EventServerCommand(perlin_writer* w, const PerlinEventServerCommand* v);
static inline void perlin_read_EventServerCommand(perlin_reader* r, PerlinEventServerCommand* v, perlin_arena* a);
static inline void perlin_write_EventPreLogin(perlin_writer* w, const PerlinEventPreLogin* v);
static inline void perlin_read_EventPreLogin(perlin_reader* r, PerlinEventPreLogin* v, perlin_arena* a);
static inline void perlin_write_PlayerInfoRequest(perlin_writer* w, const PerlinPlayerInfoRequest* v);
static inline void perlin_read_PlayerInfoRequest(perlin_reader* r, PerlinPlayerInfoRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerSetGameModeRequest(perlin_writer* w, const PerlinPlayerSetGameModeRequest* v);
static inline void perlin_read_PlayerSetGameModeRequest(perlin_reader* r, PerlinPlayerSetGameModeRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerSetHealthRequest(perlin_writer* w, const PerlinPlayerSetHealthRequest* v);
static inline void perlin_read_PlayerSetHealthRequest(perlin_reader* r, PerlinPlayerSetHealthRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerDamageRequest(perlin_writer* w, const PerlinPlayerDamageRequest* v);
static inline void perlin_read_PlayerDamageRequest(perlin_reader* r, PerlinPlayerDamageRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerKillRequest(perlin_writer* w, const PerlinPlayerKillRequest* v);
static inline void perlin_read_PlayerKillRequest(perlin_reader* r, PerlinPlayerKillRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerSetFoodRequest(perlin_writer* w, const PerlinPlayerSetFoodRequest* v);
static inline void perlin_read_PlayerSetFoodRequest(perlin_reader* r, PerlinPlayerSetFoodRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerAddExperienceRequest(perlin_writer* w, const PerlinPlayerAddExperienceRequest* v);
static inline void perlin_read_PlayerAddExperienceRequest(perlin_reader* r, PerlinPlayerAddExperienceRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerEffectAddRequest(perlin_writer* w, const PerlinPlayerEffectAddRequest* v);
static inline void perlin_read_PlayerEffectAddRequest(perlin_reader* r, PerlinPlayerEffectAddRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerEffectRemoveRequest(perlin_writer* w, const PerlinPlayerEffectRemoveRequest* v);
static inline void perlin_read_PlayerEffectRemoveRequest(perlin_reader* r, PerlinPlayerEffectRemoveRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerInventoryRequest(perlin_writer* w, const PerlinPlayerInventoryRequest* v);
static inline void perlin_read_PlayerInventoryRequest(perlin_reader* r, PerlinPlayerInventoryRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerSetSlotRequest(perlin_writer* w, const PerlinPlayerSetSlotRequest* v);
static inline void perlin_read_PlayerSetSlotRequest(perlin_reader* r, PerlinPlayerSetSlotRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerClearInventoryRequest(perlin_writer* w, const PerlinPlayerClearInventoryRequest* v);
static inline void perlin_read_PlayerClearInventoryRequest(perlin_reader* r, PerlinPlayerClearInventoryRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerSetVelocityRequest(perlin_writer* w, const PerlinPlayerSetVelocityRequest* v);
static inline void perlin_read_PlayerSetVelocityRequest(perlin_reader* r, PerlinPlayerSetVelocityRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerTransferDimensionRequest(perlin_writer* w, const PerlinPlayerTransferDimensionRequest* v);
static inline void perlin_read_PlayerTransferDimensionRequest(perlin_reader* r, PerlinPlayerTransferDimensionRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerTitleRequest(perlin_writer* w, const PerlinPlayerTitleRequest* v);
static inline void perlin_read_PlayerTitleRequest(perlin_reader* r, PerlinPlayerTitleRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerActionBarRequest(perlin_writer* w, const PerlinPlayerActionBarRequest* v);
static inline void perlin_read_PlayerActionBarRequest(perlin_reader* r, PerlinPlayerActionBarRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerSoundRequest(perlin_writer* w, const PerlinPlayerSoundRequest* v);
static inline void perlin_read_PlayerSoundRequest(perlin_reader* r, PerlinPlayerSoundRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerTransferRequest(perlin_writer* w, const PerlinPlayerTransferRequest* v);
static inline void perlin_read_PlayerTransferRequest(perlin_reader* r, PerlinPlayerTransferRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerSetOperatorRequest(perlin_writer* w, const PerlinPlayerSetOperatorRequest* v);
static inline void perlin_read_PlayerSetOperatorRequest(perlin_reader* r, PerlinPlayerSetOperatorRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerFormRequest(perlin_writer* w, const PerlinPlayerFormRequest* v);
static inline void perlin_read_PlayerFormRequest(perlin_reader* r, PerlinPlayerFormRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerRunCommandRequest(perlin_writer* w, const PerlinPlayerRunCommandRequest* v);
static inline void perlin_read_PlayerRunCommandRequest(perlin_reader* r, PerlinPlayerRunCommandRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerSetSpawnRequest(perlin_writer* w, const PerlinPlayerSetSpawnRequest* v);
static inline void perlin_read_PlayerSetSpawnRequest(perlin_reader* r, PerlinPlayerSetSpawnRequest* v, perlin_arena* a);
static inline void perlin_write_PlayerByNameRequest(perlin_writer* w, const PerlinPlayerByNameRequest* v);
static inline void perlin_read_PlayerByNameRequest(perlin_reader* r, PerlinPlayerByNameRequest* v, perlin_arena* a);
static inline void perlin_write_BlockStateRequest(perlin_writer* w, const PerlinBlockStateRequest* v);
static inline void perlin_read_BlockStateRequest(perlin_reader* r, PerlinBlockStateRequest* v, perlin_arena* a);
static inline void perlin_write_BlockStateSetRequest(perlin_writer* w, const PerlinBlockStateSetRequest* v);
static inline void perlin_read_BlockStateSetRequest(perlin_reader* r, PerlinBlockStateSetRequest* v, perlin_arena* a);
static inline void perlin_write_FillRequest(perlin_writer* w, const PerlinFillRequest* v);
static inline void perlin_read_FillRequest(perlin_reader* r, PerlinFillRequest* v, perlin_arena* a);
static inline void perlin_write_TimeGetResponse(perlin_writer* w, const PerlinTimeGetResponse* v);
static inline void perlin_read_TimeGetResponse(perlin_reader* r, PerlinTimeGetResponse* v, perlin_arena* a);
static inline void perlin_write_TimeSetRequest(perlin_writer* w, const PerlinTimeSetRequest* v);
static inline void perlin_read_TimeSetRequest(perlin_reader* r, PerlinTimeSetRequest* v, perlin_arena* a);
static inline void perlin_write_WeatherSetRequest(perlin_writer* w, const PerlinWeatherSetRequest* v);
static inline void perlin_read_WeatherSetRequest(perlin_reader* r, PerlinWeatherSetRequest* v, perlin_arena* a);
static inline void perlin_write_GameRuleGetRequest(perlin_writer* w, const PerlinGameRuleGetRequest* v);
static inline void perlin_read_GameRuleGetRequest(perlin_reader* r, PerlinGameRuleGetRequest* v, perlin_arena* a);
static inline void perlin_write_GameRuleSetRequest(perlin_writer* w, const PerlinGameRuleSetRequest* v);
static inline void perlin_read_GameRuleSetRequest(perlin_reader* r, PerlinGameRuleSetRequest* v, perlin_arena* a);
static inline void perlin_write_DifficultySetRequest(perlin_writer* w, const PerlinDifficultySetRequest* v);
static inline void perlin_read_DifficultySetRequest(perlin_reader* r, PerlinDifficultySetRequest* v, perlin_arena* a);
static inline void perlin_write_ExplodeRequest(perlin_writer* w, const PerlinExplodeRequest* v);
static inline void perlin_read_ExplodeRequest(perlin_reader* r, PerlinExplodeRequest* v, perlin_arena* a);
static inline void perlin_write_HighestBlockRequest(perlin_writer* w, const PerlinHighestBlockRequest* v);
static inline void perlin_read_HighestBlockRequest(perlin_reader* r, PerlinHighestBlockRequest* v, perlin_arena* a);
static inline void perlin_write_ParticleAtRequest(perlin_writer* w, const PerlinParticleAtRequest* v);
static inline void perlin_read_ParticleAtRequest(perlin_reader* r, PerlinParticleAtRequest* v, perlin_arena* a);
static inline void perlin_write_DropItemRequest(perlin_writer* w, const PerlinDropItemRequest* v);
static inline void perlin_read_DropItemRequest(perlin_reader* r, PerlinDropItemRequest* v, perlin_arena* a);
static inline void perlin_write_EntitySpawnRequest(perlin_writer* w, const PerlinEntitySpawnRequest* v);
static inline void perlin_read_EntitySpawnRequest(perlin_reader* r, PerlinEntitySpawnRequest* v, perlin_arena* a);
static inline void perlin_write_EntityListRequest(perlin_writer* w, const PerlinEntityListRequest* v);
static inline void perlin_read_EntityListRequest(perlin_reader* r, PerlinEntityListRequest* v, perlin_arena* a);
static inline void perlin_write_EntityListResponse(perlin_writer* w, const PerlinEntityListResponse* v);
static inline void perlin_read_EntityListResponse(perlin_reader* r, PerlinEntityListResponse* v, perlin_arena* a);
static inline void perlin_write_EntityRemoveRequest(perlin_writer* w, const PerlinEntityRemoveRequest* v);
static inline void perlin_read_EntityRemoveRequest(perlin_reader* r, PerlinEntityRemoveRequest* v, perlin_arena* a);
static inline void perlin_write_EntityDamageRequest(perlin_writer* w, const PerlinEntityDamageRequest* v);
static inline void perlin_read_EntityDamageRequest(perlin_reader* r, PerlinEntityDamageRequest* v, perlin_arena* a);
static inline void perlin_write_EntityTeleportRequest(perlin_writer* w, const PerlinEntityTeleportRequest* v);
static inline void perlin_read_EntityTeleportRequest(perlin_reader* r, PerlinEntityTeleportRequest* v, perlin_arena* a);
static inline void perlin_write_EntitySetVelocityRequest(perlin_writer* w, const PerlinEntitySetVelocityRequest* v);
static inline void perlin_read_EntitySetVelocityRequest(perlin_reader* r, PerlinEntitySetVelocityRequest* v, perlin_arena* a);
static inline void perlin_write_EntityInfoRequest(perlin_writer* w, const PerlinEntityInfoRequest* v);
static inline void perlin_read_EntityInfoRequest(perlin_reader* r, PerlinEntityInfoRequest* v, perlin_arena* a);
static inline void perlin_write_EntitySetHealthRequest(perlin_writer* w, const PerlinEntitySetHealthRequest* v);
static inline void perlin_read_EntitySetHealthRequest(perlin_reader* r, PerlinEntitySetHealthRequest* v, perlin_arena* a);
static inline void perlin_write_RunCommandRequest(perlin_writer* w, const PerlinRunCommandRequest* v);
static inline void perlin_read_RunCommandRequest(perlin_reader* r, PerlinRunCommandRequest* v, perlin_arena* a);
static inline void perlin_write_MotdSetRequest(perlin_writer* w, const PerlinMotdSetRequest* v);
static inline void perlin_read_MotdSetRequest(perlin_reader* r, PerlinMotdSetRequest* v, perlin_arena* a);
static inline void perlin_write_LootTableOverrideRequest(perlin_writer* w, const PerlinLootTableOverrideRequest* v);
static inline void perlin_read_LootTableOverrideRequest(perlin_reader* r, PerlinLootTableOverrideRequest* v, perlin_arena* a);
static inline void perlin_write_LootTableGetRequest(perlin_writer* w, const PerlinLootTableGetRequest* v);
static inline void perlin_read_LootTableGetRequest(perlin_reader* r, PerlinLootTableGetRequest* v, perlin_arena* a);
static inline void perlin_write_EventAmendRequest(perlin_writer* w, const PerlinEventAmendRequest* v);
static inline void perlin_read_EventAmendRequest(perlin_reader* r, PerlinEventAmendRequest* v, perlin_arena* a);
static inline void perlin_write_Pair_I32_I32(perlin_writer* w, const PerlinPair_I32_I32* v);
static inline void perlin_read_Pair_I32_I32(perlin_reader* r, PerlinPair_I32_I32* v, perlin_arena* a);
static inline void perlin_write_List_Pair_I32_I32(perlin_writer* w, const PerlinList_Pair_I32_I32* v);
static inline void perlin_read_List_Pair_I32_I32(perlin_reader* r, PerlinList_Pair_I32_I32* v, perlin_arena* a);
static inline void perlin_write_List_Str(perlin_writer* w, const PerlinList_Str* v);
static inline void perlin_read_List_Str(perlin_reader* r, PerlinList_Str* v, perlin_arena* a);
static inline void perlin_write_List_Attribute(perlin_writer* w, const PerlinList_Attribute* v);
static inline void perlin_read_List_Attribute(perlin_reader* r, PerlinList_Attribute* v, perlin_arena* a);
static inline void perlin_write_Pair_Str_F32(perlin_writer* w, const PerlinPair_Str_F32* v);
static inline void perlin_read_Pair_Str_F32(perlin_reader* r, PerlinPair_Str_F32* v, perlin_arena* a);
static inline void perlin_write_List_Pair_Str_F32(perlin_writer* w, const PerlinList_Pair_Str_F32* v);
static inline void perlin_read_List_Pair_Str_F32(perlin_reader* r, PerlinList_Pair_Str_F32* v, perlin_arena* a);
static inline void perlin_write_Pair_Str_Str(perlin_writer* w, const PerlinPair_Str_Str* v);
static inline void perlin_read_Pair_Str_Str(perlin_reader* r, PerlinPair_Str_Str* v, perlin_arena* a);
static inline void perlin_write_List_Pair_Str_Str(perlin_writer* w, const PerlinList_Pair_Str_Str* v);
static inline void perlin_read_List_Pair_Str_Str(perlin_reader* r, PerlinList_Pair_Str_Str* v, perlin_arena* a);
static inline void perlin_write_Pair_Str_I32(perlin_writer* w, const PerlinPair_Str_I32* v);
static inline void perlin_read_Pair_Str_I32(perlin_reader* r, PerlinPair_Str_I32* v, perlin_arena* a);
static inline void perlin_write_List_Pair_Str_I32(perlin_writer* w, const PerlinList_Pair_Str_I32* v);
static inline void perlin_read_List_Pair_Str_I32(perlin_reader* r, PerlinList_Pair_Str_I32* v, perlin_arena* a);
static inline void perlin_write_List_WorldLine(perlin_writer* w, const PerlinList_WorldLine* v);
static inline void perlin_read_List_WorldLine(perlin_reader* r, PerlinList_WorldLine* v, perlin_arena* a);
static inline void perlin_write_List_WorldBox(perlin_writer* w, const PerlinList_WorldBox* v);
static inline void perlin_read_List_WorldBox(perlin_reader* r, PerlinList_WorldBox* v, perlin_arena* a);
static inline void perlin_write_List_ClientBlockEdit(perlin_writer* w, const PerlinList_ClientBlockEdit* v);
static inline void perlin_read_List_ClientBlockEdit(perlin_reader* r, PerlinList_ClientBlockEdit* v, perlin_arena* a);
static inline void perlin_write_List_U32(perlin_writer* w, const PerlinList_U32* v);
static inline void perlin_read_List_U32(perlin_reader* r, PerlinList_U32* v, perlin_arena* a);
static inline void perlin_write_List_U16(perlin_writer* w, const PerlinList_U16* v);
static inline void perlin_read_List_U16(perlin_reader* r, PerlinList_U16* v, perlin_arena* a);
static inline void perlin_write_List_MeshVertex(perlin_writer* w, const PerlinList_MeshVertex* v);
static inline void perlin_read_List_MeshVertex(perlin_reader* r, PerlinList_MeshVertex* v, perlin_arena* a);
static inline void perlin_write_List_OnlinePlayer(perlin_writer* w, const PerlinList_OnlinePlayer* v);
static inline void perlin_read_List_OnlinePlayer(perlin_reader* r, PerlinList_OnlinePlayer* v, perlin_arena* a);
static inline void perlin_write_List_Effect(perlin_writer* w, const PerlinList_Effect* v);
static inline void perlin_read_List_Effect(perlin_reader* r, PerlinList_Effect* v, perlin_arena* a);
static inline void perlin_write_List_FoundBlock(perlin_writer* w, const PerlinList_FoundBlock* v);
static inline void perlin_read_List_FoundBlock(perlin_reader* r, PerlinList_FoundBlock* v, perlin_arena* a);
static inline void perlin_write_List_Entity(perlin_writer* w, const PerlinList_Entity* v);
static inline void perlin_read_List_Entity(perlin_reader* r, PerlinList_Entity* v, perlin_arena* a);
static inline void perlin_write_List_PlayerEntry(perlin_writer* w, const PerlinList_PlayerEntry* v);
static inline void perlin_read_List_PlayerEntry(perlin_reader* r, PerlinList_PlayerEntry* v, perlin_arena* a);
static inline void perlin_write_List_PackEntry(perlin_writer* w, const PerlinList_PackEntry* v);
static inline void perlin_read_List_PackEntry(perlin_reader* r, PerlinList_PackEntry* v, perlin_arena* a);
static inline void perlin_write_List_ChatLine(perlin_writer* w, const PerlinList_ChatLine* v);
static inline void perlin_read_List_ChatLine(perlin_reader* r, PerlinList_ChatLine* v, perlin_arena* a);
static inline void perlin_write_List_SettingEntry(perlin_writer* w, const PerlinList_SettingEntry* v);
static inline void perlin_read_List_SettingEntry(perlin_reader* r, PerlinList_SettingEntry* v, perlin_arena* a);
static inline void perlin_write_List_ModEntry(perlin_writer* w, const PerlinList_ModEntry* v);
static inline void perlin_read_List_ModEntry(perlin_reader* r, PerlinList_ModEntry* v, perlin_arena* a);
static inline void perlin_write_List_PacketCount(perlin_writer* w, const PerlinList_PacketCount* v);
static inline void perlin_read_List_PacketCount(perlin_reader* r, PerlinList_PacketCount* v, perlin_arena* a);
static inline void perlin_write_List_ServerItem(perlin_writer* w, const PerlinList_ServerItem* v);
static inline void perlin_read_List_ServerItem(perlin_reader* r, PerlinList_ServerItem* v, perlin_arena* a);
static inline void perlin_write_List_FormButton(perlin_writer* w, const PerlinList_FormButton* v);
static inline void perlin_read_List_FormButton(perlin_reader* r, PerlinList_FormButton* v, perlin_arena* a);
static inline void perlin_write_List_FormControl(perlin_writer* w, const PerlinList_FormControl* v);
static inline void perlin_read_List_FormControl(perlin_reader* r, PerlinList_FormControl* v, perlin_arena* a);
static inline void perlin_write_List_ServerEntity(perlin_writer* w, const PerlinList_ServerEntity* v);
static inline void perlin_read_List_ServerEntity(perlin_reader* r, PerlinList_ServerEntity* v, perlin_arena* a);

static inline void perlin_write_Item(perlin_writer* w, const PerlinItem* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
    perlin_w_i32(w, (int32_t)(v->count));
    perlin_w_i32(w, (int32_t)(v->aux));
    perlin_w_i32(w, (int32_t)(v->damage));
    perlin_w_u8(w, (uint8_t)(v->enchanted));
    perlin_w_str(w, v->customName);
    perlin_write_List_Pair_I32_I32(w, &v->enchants);
    perlin_write_List_Str(w, &v->lore);
}
static inline void perlin_read_Item(perlin_reader* r, PerlinItem* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
    v->count = (int32_t)perlin_r_i32(r);
    v->aux = (int32_t)perlin_r_i32(r);
    v->damage = (int32_t)perlin_r_i32(r);
    v->enchanted = (uint8_t)perlin_r_u8(r);
    v->customName = perlin_r_str(r);
    perlin_read_List_Pair_I32_I32(r, &v->enchants, a);
    perlin_read_List_Str(r, &v->lore, a);
}
static inline void perlin_write_PlayerState(perlin_writer* w, const PerlinPlayerState* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->runtimeId));
    perlin_w_i64(w, (int64_t)(v->uniqueId));
    perlin_w_str(w, v->name);
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_f64(w, (double)(v->vx));
    perlin_w_f64(w, (double)(v->vy));
    perlin_w_f64(w, (double)(v->vz));
    perlin_w_f32(w, (float)(v->yaw));
    perlin_w_f32(w, (float)(v->pitch));
    perlin_w_f32(w, (float)(v->headYaw));
    perlin_w_f32(w, (float)(v->eyeHeight));
    perlin_w_u8(w, (uint8_t)(v->onGround));
    perlin_w_u8(w, (uint8_t)(v->sprinting));
    perlin_w_u8(w, (uint8_t)(v->sneaking));
    perlin_w_u8(w, (uint8_t)(v->swimming));
    perlin_w_u8(w, (uint8_t)(v->gliding));
    perlin_w_u8(w, (uint8_t)(v->flying));
    perlin_w_u8(w, (uint8_t)(v->mayFly));
    perlin_w_u8(w, (uint8_t)(v->spectator));
    perlin_w_u8(w, (uint8_t)(v->onFire));
    perlin_w_u8(w, (uint8_t)(v->riding));
    perlin_w_u8(w, (uint8_t)(v->connected));
    perlin_w_i32(w, (int32_t)(v->gameMode));
    perlin_w_i32(w, (int32_t)(v->dimension));
    perlin_w_i32(w, (int32_t)(v->selectedSlot));
    perlin_w_i32(w, (int32_t)(v->permission));
    perlin_w_f32(w, (float)(v->health));
    perlin_w_f32(w, (float)(v->maxHealth));
    perlin_w_f32(w, (float)(v->absorption));
    perlin_w_f32(w, (float)(v->hunger));
    perlin_w_f32(w, (float)(v->saturation));
    perlin_w_f32(w, (float)(v->xpProgress));
    perlin_w_f32(w, (float)(v->walkSpeed));
    perlin_w_i32(w, (int32_t)(v->xpLevel));
    perlin_w_i32(w, (int32_t)(v->air));
    perlin_w_i32(w, (int32_t)(v->maxAir));
}
static inline void perlin_read_PlayerState(perlin_reader* r, PerlinPlayerState* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->runtimeId = (uint64_t)perlin_r_u64(r);
    v->uniqueId = (int64_t)perlin_r_i64(r);
    v->name = perlin_r_str(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->vx = (double)perlin_r_f64(r);
    v->vy = (double)perlin_r_f64(r);
    v->vz = (double)perlin_r_f64(r);
    v->yaw = (float)perlin_r_f32(r);
    v->pitch = (float)perlin_r_f32(r);
    v->headYaw = (float)perlin_r_f32(r);
    v->eyeHeight = (float)perlin_r_f32(r);
    v->onGround = (uint8_t)perlin_r_u8(r);
    v->sprinting = (uint8_t)perlin_r_u8(r);
    v->sneaking = (uint8_t)perlin_r_u8(r);
    v->swimming = (uint8_t)perlin_r_u8(r);
    v->gliding = (uint8_t)perlin_r_u8(r);
    v->flying = (uint8_t)perlin_r_u8(r);
    v->mayFly = (uint8_t)perlin_r_u8(r);
    v->spectator = (uint8_t)perlin_r_u8(r);
    v->onFire = (uint8_t)perlin_r_u8(r);
    v->riding = (uint8_t)perlin_r_u8(r);
    v->connected = (uint8_t)perlin_r_u8(r);
    v->gameMode = (PerlinGameMode)perlin_r_i32(r);
    v->dimension = (PerlinDimension)perlin_r_i32(r);
    v->selectedSlot = (int32_t)perlin_r_i32(r);
    v->permission = (PerlinPermissionLevel)perlin_r_i32(r);
    v->health = (float)perlin_r_f32(r);
    v->maxHealth = (float)perlin_r_f32(r);
    v->absorption = (float)perlin_r_f32(r);
    v->hunger = (float)perlin_r_f32(r);
    v->saturation = (float)perlin_r_f32(r);
    v->xpProgress = (float)perlin_r_f32(r);
    v->walkSpeed = (float)perlin_r_f32(r);
    v->xpLevel = (int32_t)perlin_r_i32(r);
    v->air = (int32_t)perlin_r_i32(r);
    v->maxAir = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_Effect(perlin_writer* w, const PerlinEffect* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->id));
    perlin_w_i32(w, (int32_t)(v->amplifier));
    perlin_w_i32(w, (int32_t)(v->ticks));
    perlin_w_u8(w, (uint8_t)(v->ambient));
    perlin_w_u8(w, (uint8_t)(v->particles));
}
static inline void perlin_read_Effect(perlin_reader* r, PerlinEffect* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = (PerlinEffectId)perlin_r_i32(r);
    v->amplifier = (int32_t)perlin_r_i32(r);
    v->ticks = (int32_t)perlin_r_i32(r);
    v->ambient = (uint8_t)perlin_r_u8(r);
    v->particles = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_Inventory(perlin_writer* w, const PerlinInventory* v) {
    (void)w;
    (void)v;
    for (uint32_t i_ = 0; i_ < 36; i_++) {
        perlin_write_Item(w, &v->slots[i_]);
    }
    for (uint32_t i_ = 0; i_ < 4; i_++) {
        perlin_write_Item(w, &v->armor[i_]);
    }
    perlin_write_Item(w, &v->offhand);
    perlin_write_Item(w, &v->cursor);
    perlin_w_i32(w, (int32_t)(v->selected));
    perlin_w_u8(w, (uint8_t)(v->open));
}
static inline void perlin_read_Inventory(perlin_reader* r, PerlinInventory* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    for (uint32_t i_ = 0; i_ < 36; i_++) {
        perlin_read_Item(r, &v->slots[i_], a);
    }
    for (uint32_t i_ = 0; i_ < 4; i_++) {
        perlin_read_Item(r, &v->armor[i_], a);
    }
    perlin_read_Item(r, &v->offhand, a);
    perlin_read_Item(r, &v->cursor, a);
    v->selected = (int32_t)perlin_r_i32(r);
    v->open = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_Attribute(perlin_writer* w, const PerlinAttribute* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
    perlin_w_f32(w, (float)(v->min));
    perlin_w_f32(w, (float)(v->value));
    perlin_w_f32(w, (float)(v->max));
}
static inline void perlin_read_Attribute(perlin_reader* r, PerlinAttribute* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
    v->min = (float)perlin_r_f32(r);
    v->value = (float)perlin_r_f32(r);
    v->max = (float)perlin_r_f32(r);
}
static inline void perlin_write_Entity(perlin_writer* w, const PerlinEntity* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->runtimeId));
    perlin_w_i64(w, (int64_t)(v->uniqueId));
    perlin_w_str(w, v->type);
    perlin_w_str(w, v->name);
    perlin_w_str(w, v->nameTag);
    perlin_w_str(w, v->uuid);
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_f32(w, (float)(v->yaw));
    perlin_w_f32(w, (float)(v->pitch));
    perlin_w_f32(w, (float)(v->headYaw));
    perlin_w_f32(w, (float)(v->health));
    perlin_w_f32(w, (float)(v->scale));
    perlin_w_u8(w, (uint8_t)(v->onGround));
    perlin_w_u8(w, (uint8_t)(v->isPlayer));
    perlin_w_u8(w, (uint8_t)(v->isItem));
    perlin_w_u8(w, (uint8_t)(v->isSelf));
    perlin_w_u64(w, (uint64_t)(v->flags));
    perlin_w_str(w, v->itemName);
    perlin_w_i32(w, (int32_t)(v->itemCount));
    perlin_w_str(w, v->mainHand);
    perlin_w_str(w, v->offHand);
    for (uint32_t i_ = 0; i_ < 4; i_++) {
        perlin_w_str(w, v->armor[i_]);
    }
    perlin_w_i64(w, (int64_t)(v->vehicleUid));
    perlin_w_i64(w, (int64_t)(v->ownerUid));
    perlin_w_i64(w, (int64_t)(v->targetUid));
    perlin_w_i32(w, (int32_t)(v->variant));
    perlin_w_i32(w, (int32_t)(v->markVariant));
    perlin_w_i32(w, (int32_t)(v->color));
    perlin_w_f32(w, (float)(v->maxHealth));
    perlin_w_u8(w, (uint8_t)(v->dying));
    perlin_write_List_Attribute(w, &v->attributes);
    perlin_write_List_Pair_Str_F32(w, &v->properties);
    perlin_write_List_Pair_Str_Str(w, &v->propertyText);
}
static inline void perlin_read_Entity(perlin_reader* r, PerlinEntity* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->runtimeId = (uint64_t)perlin_r_u64(r);
    v->uniqueId = (int64_t)perlin_r_i64(r);
    v->type = perlin_r_str(r);
    v->name = perlin_r_str(r);
    v->nameTag = perlin_r_str(r);
    v->uuid = perlin_r_str(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->yaw = (float)perlin_r_f32(r);
    v->pitch = (float)perlin_r_f32(r);
    v->headYaw = (float)perlin_r_f32(r);
    v->health = (float)perlin_r_f32(r);
    v->scale = (float)perlin_r_f32(r);
    v->onGround = (uint8_t)perlin_r_u8(r);
    v->isPlayer = (uint8_t)perlin_r_u8(r);
    v->isItem = (uint8_t)perlin_r_u8(r);
    v->isSelf = (uint8_t)perlin_r_u8(r);
    v->flags = (uint64_t)perlin_r_u64(r);
    v->itemName = perlin_r_str(r);
    v->itemCount = (int32_t)perlin_r_i32(r);
    v->mainHand = perlin_r_str(r);
    v->offHand = perlin_r_str(r);
    for (uint32_t i_ = 0; i_ < 4; i_++) {
        v->armor[i_] = perlin_r_str(r);
    }
    v->vehicleUid = (int64_t)perlin_r_i64(r);
    v->ownerUid = (int64_t)perlin_r_i64(r);
    v->targetUid = (int64_t)perlin_r_i64(r);
    v->variant = (int32_t)perlin_r_i32(r);
    v->markVariant = (int32_t)perlin_r_i32(r);
    v->color = (int32_t)perlin_r_i32(r);
    v->maxHealth = (float)perlin_r_f32(r);
    v->dying = (uint8_t)perlin_r_u8(r);
    perlin_read_List_Attribute(r, &v->attributes, a);
    perlin_read_List_Pair_Str_F32(r, &v->properties, a);
    perlin_read_List_Pair_Str_Str(r, &v->propertyText, a);
}
static inline void perlin_write_PlayerEntry(perlin_writer* w, const PerlinPlayerEntry* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->uuid);
    perlin_w_str(w, v->name);
    perlin_w_i64(w, (int64_t)(v->uid));
    perlin_w_i32(w, (int32_t)(v->permission));
}
static inline void perlin_read_PlayerEntry(perlin_reader* r, PerlinPlayerEntry* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->uuid = perlin_r_str(r);
    v->name = perlin_r_str(r);
    v->uid = (int64_t)perlin_r_i64(r);
    v->permission = (PerlinPermissionLevel)perlin_r_i32(r);
}
static inline void perlin_write_WorldState(perlin_writer* w, const PerlinWorldState* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->dimension));
    perlin_w_i32(w, (int32_t)(v->difficulty));
    perlin_w_i32(w, (int32_t)(v->worldGameMode));
    perlin_w_i64(w, (int64_t)(v->day));
    perlin_w_i32(w, (int32_t)(v->timeOfDay));
    perlin_w_f64(w, (double)(v->totalTicks));
    perlin_w_f32(w, (float)(v->rain));
    perlin_w_f32(w, (float)(v->thunder));
    perlin_w_u8(w, (uint8_t)(v->commandsEnabled));
    perlin_w_u8(w, (uint8_t)(v->haveSpawn));
    perlin_w_i32(w, (int32_t)(v->spawnX));
    perlin_w_i32(w, (int32_t)(v->spawnY));
    perlin_w_i32(w, (int32_t)(v->spawnZ));
    perlin_write_List_Pair_Str_I32(w, &v->rules);
}
static inline void perlin_read_WorldState(perlin_reader* r, PerlinWorldState* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->dimension = (PerlinDimension)perlin_r_i32(r);
    v->difficulty = (PerlinDifficulty)perlin_r_i32(r);
    v->worldGameMode = (PerlinGameMode)perlin_r_i32(r);
    v->day = (int64_t)perlin_r_i64(r);
    v->timeOfDay = (int32_t)perlin_r_i32(r);
    v->totalTicks = (double)perlin_r_f64(r);
    v->rain = (float)perlin_r_f32(r);
    v->thunder = (float)perlin_r_f32(r);
    v->commandsEnabled = (uint8_t)perlin_r_u8(r);
    v->haveSpawn = (uint8_t)perlin_r_u8(r);
    v->spawnX = (int32_t)perlin_r_i32(r);
    v->spawnY = (int32_t)perlin_r_i32(r);
    v->spawnZ = (int32_t)perlin_r_i32(r);
    perlin_read_List_Pair_Str_I32(r, &v->rules, a);
}
static inline void perlin_write_BlockInfo(perlin_writer* w, const PerlinBlockInfo* v) {
    (void)w;
    (void)v;
    perlin_w_u8(w, (uint8_t)(v->loaded));
    perlin_w_u32(w, (uint32_t)(v->runtimeId));
    perlin_w_str(w, v->name);
    perlin_w_i32(w, (int32_t)(v->biome));
    perlin_w_u32(w, (uint32_t)(v->liquid));
    perlin_write_List_Pair_Str_Str(w, &v->states);
}
static inline void perlin_read_BlockInfo(perlin_reader* r, PerlinBlockInfo* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->loaded = (uint8_t)perlin_r_u8(r);
    v->runtimeId = (uint32_t)perlin_r_u32(r);
    v->name = perlin_r_str(r);
    v->biome = (int32_t)perlin_r_i32(r);
    v->liquid = (uint32_t)perlin_r_u32(r);
    perlin_read_List_Pair_Str_Str(r, &v->states, a);
}
static inline void perlin_write_BlockProps(perlin_writer* w, const PerlinBlockProps* v) {
    (void)w;
    (void)v;
    perlin_w_u8(w, (uint8_t)(v->known));
    perlin_w_f32(w, (float)(v->hardness));
    perlin_w_u8(w, (uint8_t)(v->tool));
    perlin_w_i32(w, (int32_t)(v->harvestLevel));
    perlin_w_u8(w, (uint8_t)(v->needsSupportBelow));
    perlin_w_u8(w, (uint8_t)(v->breaksWholeStack));
    perlin_w_str(w, v->displayName);
    perlin_write_List_Str(w, &v->properties);
}
static inline void perlin_read_BlockProps(perlin_reader* r, PerlinBlockProps* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->known = (uint8_t)perlin_r_u8(r);
    v->hardness = (float)perlin_r_f32(r);
    v->tool = (PerlinTool)perlin_r_u8(r);
    v->harvestLevel = (int32_t)perlin_r_i32(r);
    v->needsSupportBelow = (uint8_t)perlin_r_u8(r);
    v->breaksWholeStack = (uint8_t)perlin_r_u8(r);
    v->displayName = perlin_r_str(r);
    perlin_read_List_Str(r, &v->properties, a);
}
static inline void perlin_write_BlockEntityInfo(perlin_writer* w, const PerlinBlockEntityInfo* v) {
    (void)w;
    (void)v;
    perlin_w_u8(w, (uint8_t)(v->present));
    perlin_w_str(w, v->id);
    perlin_w_i32(w, (int32_t)(v->color));
    perlin_w_u8(w, (uint8_t)(v->waxed));
    perlin_w_f32(w, (float)(v->rotation));
    perlin_w_str(w, v->frontText);
    perlin_w_str(w, v->backText);
    perlin_w_u8(w, (uint8_t)(v->frontGlow));
    perlin_w_u8(w, (uint8_t)(v->backGlow));
    perlin_w_i32(w, (int32_t)(v->base));
    perlin_w_i32(w, (int32_t)(v->bannerType));
    perlin_write_List_Pair_Str_I32(w, &v->patterns);
    perlin_w_str(w, v->nbt);
}
static inline void perlin_read_BlockEntityInfo(perlin_reader* r, PerlinBlockEntityInfo* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->present = (uint8_t)perlin_r_u8(r);
    v->id = perlin_r_str(r);
    v->color = (int32_t)perlin_r_i32(r);
    v->waxed = (uint8_t)perlin_r_u8(r);
    v->rotation = (float)perlin_r_f32(r);
    v->frontText = perlin_r_str(r);
    v->backText = perlin_r_str(r);
    v->frontGlow = (uint8_t)perlin_r_u8(r);
    v->backGlow = (uint8_t)perlin_r_u8(r);
    v->base = (int32_t)perlin_r_i32(r);
    v->bannerType = (int32_t)perlin_r_i32(r);
    perlin_read_List_Pair_Str_I32(r, &v->patterns, a);
    v->nbt = perlin_r_str(r);
}
static inline void perlin_write_BreakState(perlin_writer* w, const PerlinBreakState* v) {
    (void)w;
    (void)v;
    perlin_w_u8(w, (uint8_t)(v->active));
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_f32(w, (float)(v->progress));
}
static inline void perlin_read_BreakState(perlin_reader* r, PerlinBreakState* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->active = (uint8_t)perlin_r_u8(r);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->progress = (float)perlin_r_f32(r);
}
static inline void perlin_write_RayHit(perlin_writer* w, const PerlinRayHit* v) {
    (void)w;
    (void)v;
    perlin_w_u8(w, (uint8_t)(v->hit));
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_i32(w, (int32_t)(v->face));
    perlin_w_f64(w, (double)(v->hx));
    perlin_w_f64(w, (double)(v->hy));
    perlin_w_f64(w, (double)(v->hz));
    perlin_w_f64(w, (double)(v->distance));
    perlin_w_str(w, v->block);
}
static inline void perlin_read_RayHit(perlin_reader* r, PerlinRayHit* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->hit = (uint8_t)perlin_r_u8(r);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->face = (PerlinFace)perlin_r_i32(r);
    v->hx = (double)perlin_r_f64(r);
    v->hy = (double)perlin_r_f64(r);
    v->hz = (double)perlin_r_f64(r);
    v->distance = (double)perlin_r_f64(r);
    v->block = perlin_r_str(r);
}
static inline void perlin_write_AimState(perlin_writer* w, const PerlinAimState* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->kind));
    perlin_write_RayHit(w, &v->block);
    perlin_w_u64(w, (uint64_t)(v->entity));
    perlin_w_str(w, v->entityType);
    perlin_w_f64(w, (double)(v->distance));
}
static inline void perlin_read_AimState(perlin_reader* r, PerlinAimState* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->kind = (PerlinAimKind)perlin_r_i32(r);
    perlin_read_RayHit(r, &v->block, a);
    v->entity = (uint64_t)perlin_r_u64(r);
    v->entityType = perlin_r_str(r);
    v->distance = (double)perlin_r_f64(r);
}
static inline void perlin_write_ScreenState(perlin_writer* w, const PerlinScreenState* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->guiWidth));
    perlin_w_i32(w, (int32_t)(v->guiHeight));
    perlin_w_i32(w, (int32_t)(v->guiScale));
    perlin_w_i32(w, (int32_t)(v->framebufferWidth));
    perlin_w_i32(w, (int32_t)(v->framebufferHeight));
    perlin_w_f32(w, (float)(v->fps));
    perlin_w_f32(w, (float)(v->frameMs));
    perlin_w_i32(w, (int32_t)(v->screen));
    perlin_w_u8(w, (uint8_t)(v->paused));
    perlin_w_u8(w, (uint8_t)(v->connected));
    perlin_w_u8(w, (uint8_t)(v->hudHidden));
    perlin_w_u8(w, (uint8_t)(v->debug));
    perlin_w_str(w, v->server);
    perlin_w_str(w, v->language);
    perlin_w_str(w, v->platform);
    perlin_w_str(w, v->gameVersion);
}
static inline void perlin_read_ScreenState(perlin_reader* r, PerlinScreenState* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->guiWidth = (int32_t)perlin_r_i32(r);
    v->guiHeight = (int32_t)perlin_r_i32(r);
    v->guiScale = (int32_t)perlin_r_i32(r);
    v->framebufferWidth = (int32_t)perlin_r_i32(r);
    v->framebufferHeight = (int32_t)perlin_r_i32(r);
    v->fps = (float)perlin_r_f32(r);
    v->frameMs = (float)perlin_r_f32(r);
    v->screen = (PerlinScreen)perlin_r_i32(r);
    v->paused = (uint8_t)perlin_r_u8(r);
    v->connected = (uint8_t)perlin_r_u8(r);
    v->hudHidden = (uint8_t)perlin_r_u8(r);
    v->debug = (uint8_t)perlin_r_u8(r);
    v->server = perlin_r_str(r);
    v->language = perlin_r_str(r);
    v->platform = perlin_r_str(r);
    v->gameVersion = perlin_r_str(r);
}
static inline void perlin_write_Meta(perlin_writer* w, const PerlinMeta* v) {
    (void)w;
    (void)v;
    perlin_w_u32(w, (uint32_t)(v->apiVersion));
    perlin_w_str(w, v->gameVersion);
    perlin_w_str(w, v->plugin);
    perlin_w_str(w, v->platform);
    perlin_w_u32(w, (uint32_t)(v->permissions));
    perlin_w_i64(w, (int64_t)(v->nowMs));
    perlin_w_u64(w, (uint64_t)(v->tick));
}
static inline void perlin_read_Meta(perlin_reader* r, PerlinMeta* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->apiVersion = (uint32_t)perlin_r_u32(r);
    v->gameVersion = perlin_r_str(r);
    v->plugin = perlin_r_str(r);
    v->platform = perlin_r_str(r);
    v->permissions = (uint32_t)perlin_r_u32(r);
    v->nowMs = (int64_t)perlin_r_i64(r);
    v->tick = (uint64_t)perlin_r_u64(r);
}
static inline void perlin_write_InputOverrideState(perlin_writer* w, const PerlinInputOverrideState* v) {
    (void)w;
    (void)v;
    perlin_w_f32(w, (float)(v->forward));
    perlin_w_f32(w, (float)(v->strafe));
    perlin_w_u8(w, (uint8_t)(v->jump));
    perlin_w_u8(w, (uint8_t)(v->sneak));
    perlin_w_u8(w, (uint8_t)(v->sprint));
    perlin_w_u8(w, (uint8_t)(v->attack));
    perlin_w_u8(w, (uint8_t)(v->use));
    perlin_w_i32(w, (int32_t)(v->ticks));
}
static inline void perlin_read_InputOverrideState(perlin_reader* r, PerlinInputOverrideState* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->forward = (float)perlin_r_f32(r);
    v->strafe = (float)perlin_r_f32(r);
    v->jump = (uint8_t)perlin_r_u8(r);
    v->sneak = (uint8_t)perlin_r_u8(r);
    v->sprint = (uint8_t)perlin_r_u8(r);
    v->attack = (uint8_t)perlin_r_u8(r);
    v->use = (uint8_t)perlin_r_u8(r);
    v->ticks = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_CameraControlState(perlin_writer* w, const PerlinCameraControlState* v) {
    (void)w;
    (void)v;
    perlin_w_f32(w, (float)(v->roll));
    perlin_w_f32(w, (float)(v->fovAdd));
    perlin_w_f32(w, (float)(v->fovSet));
    perlin_w_f32(w, (float)(v->offsetX));
    perlin_w_f32(w, (float)(v->offsetY));
    perlin_w_f32(w, (float)(v->offsetZ));
    perlin_w_f32(w, (float)(v->yawAdd));
    perlin_w_f32(w, (float)(v->pitchAdd));
    perlin_w_i32(w, (int32_t)(v->ticks));
}
static inline void perlin_read_CameraControlState(perlin_reader* r, PerlinCameraControlState* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->roll = (float)perlin_r_f32(r);
    v->fovAdd = (float)perlin_r_f32(r);
    v->fovSet = (float)perlin_r_f32(r);
    v->offsetX = (float)perlin_r_f32(r);
    v->offsetY = (float)perlin_r_f32(r);
    v->offsetZ = (float)perlin_r_f32(r);
    v->yawAdd = (float)perlin_r_f32(r);
    v->pitchAdd = (float)perlin_r_f32(r);
    v->ticks = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_CameraInfo(perlin_writer* w, const PerlinCameraInfo* v) {
    (void)w;
    (void)v;
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_f32(w, (float)(v->yaw));
    perlin_w_f32(w, (float)(v->pitch));
    perlin_w_f32(w, (float)(v->fov));
    perlin_w_f32(w, (float)(v->aspect));
    perlin_w_f32(w, (float)(v->nearPlane));
    perlin_w_f32(w, (float)(v->farPlane));
    perlin_w_i32(w, (int32_t)(v->perspective));
    for (uint32_t i_ = 0; i_ < 16; i_++) {
        perlin_w_f32(w, (float)(v->viewProjection[i_]));
    }
}
static inline void perlin_read_CameraInfo(perlin_reader* r, PerlinCameraInfo* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->yaw = (float)perlin_r_f32(r);
    v->pitch = (float)perlin_r_f32(r);
    v->fov = (float)perlin_r_f32(r);
    v->aspect = (float)perlin_r_f32(r);
    v->nearPlane = (float)perlin_r_f32(r);
    v->farPlane = (float)perlin_r_f32(r);
    v->perspective = (int32_t)perlin_r_i32(r);
    for (uint32_t i_ = 0; i_ < 16; i_++) {
        v->viewProjection[i_] = (float)perlin_r_f32(r);
    }
}
static inline void perlin_write_WorldPoint(perlin_writer* w, const PerlinWorldPoint* v) {
    (void)w;
    (void)v;
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
}
static inline void perlin_read_WorldPoint(perlin_reader* r, PerlinWorldPoint* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
}
static inline void perlin_write_ScreenPoint(perlin_writer* w, const PerlinScreenPoint* v) {
    (void)w;
    (void)v;
    perlin_w_f32(w, (float)(v->x));
    perlin_w_f32(w, (float)(v->y));
    perlin_w_f32(w, (float)(v->depth));
    perlin_w_u8(w, (uint8_t)(v->visible));
}
static inline void perlin_read_ScreenPoint(perlin_reader* r, PerlinScreenPoint* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (float)perlin_r_f32(r);
    v->y = (float)perlin_r_f32(r);
    v->depth = (float)perlin_r_f32(r);
    v->visible = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_MotionRequest(perlin_writer* w, const PerlinMotionRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u8(w, (uint8_t)(v->add));
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_u8(w, (uint8_t)(v->axisX));
    perlin_w_u8(w, (uint8_t)(v->axisY));
    perlin_w_u8(w, (uint8_t)(v->axisZ));
}
static inline void perlin_read_MotionRequest(perlin_reader* r, PerlinMotionRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->add = (uint8_t)perlin_r_u8(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->axisX = (uint8_t)perlin_r_u8(r);
    v->axisY = (uint8_t)perlin_r_u8(r);
    v->axisZ = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_TeleportRequest(perlin_writer* w, const PerlinTeleportRequest* v) {
    (void)w;
    (void)v;
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_u8(w, (uint8_t)(v->keepVelocity));
}
static inline void perlin_read_TeleportRequest(perlin_reader* r, PerlinTeleportRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->keepVelocity = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_ModelPose(perlin_writer* w, const PerlinModelPose* v) {
    (void)w;
    (void)v;
    perlin_w_f32(w, (float)(v->roll));
    perlin_w_f32(w, (float)(v->pitch));
    perlin_w_f32(w, (float)(v->yaw));
    perlin_w_i32(w, (int32_t)(v->ticks));
}
static inline void perlin_read_ModelPose(perlin_reader* r, PerlinModelPose* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->roll = (float)perlin_r_f32(r);
    v->pitch = (float)perlin_r_f32(r);
    v->yaw = (float)perlin_r_f32(r);
    v->ticks = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_LookSet(perlin_writer* w, const PerlinLookSet* v) {
    (void)w;
    (void)v;
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
}
static inline void perlin_read_LookSet(perlin_reader* r, PerlinLookSet* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
}
static inline void perlin_write_WorldLine(perlin_writer* w, const PerlinWorldLine* v) {
    (void)w;
    (void)v;
    perlin_w_f64(w, (double)(v->x1));
    perlin_w_f64(w, (double)(v->y1));
    perlin_w_f64(w, (double)(v->z1));
    perlin_w_f64(w, (double)(v->x2));
    perlin_w_f64(w, (double)(v->y2));
    perlin_w_f64(w, (double)(v->z2));
    perlin_w_u32(w, (uint32_t)(v->color));
    perlin_w_f32(w, (float)(v->width));
}
static inline void perlin_read_WorldLine(perlin_reader* r, PerlinWorldLine* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x1 = (double)perlin_r_f64(r);
    v->y1 = (double)perlin_r_f64(r);
    v->z1 = (double)perlin_r_f64(r);
    v->x2 = (double)perlin_r_f64(r);
    v->y2 = (double)perlin_r_f64(r);
    v->z2 = (double)perlin_r_f64(r);
    v->color = (uint32_t)perlin_r_u32(r);
    v->width = (float)perlin_r_f32(r);
}
static inline void perlin_write_WorldLines(perlin_writer* w, const PerlinWorldLines* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->ticks));
    perlin_write_List_WorldLine(w, &v->lines);
}
static inline void perlin_read_WorldLines(perlin_reader* r, PerlinWorldLines* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->ticks = (int32_t)perlin_r_i32(r);
    perlin_read_List_WorldLine(r, &v->lines, a);
}
static inline void perlin_write_WorldBox(perlin_writer* w, const PerlinWorldBox* v) {
    (void)w;
    (void)v;
    perlin_w_f64(w, (double)(v->x0));
    perlin_w_f64(w, (double)(v->y0));
    perlin_w_f64(w, (double)(v->z0));
    perlin_w_f64(w, (double)(v->x1));
    perlin_w_f64(w, (double)(v->y1));
    perlin_w_f64(w, (double)(v->z1));
    perlin_w_u32(w, (uint32_t)(v->color));
    perlin_w_f32(w, (float)(v->width));
}
static inline void perlin_read_WorldBox(perlin_reader* r, PerlinWorldBox* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x0 = (double)perlin_r_f64(r);
    v->y0 = (double)perlin_r_f64(r);
    v->z0 = (double)perlin_r_f64(r);
    v->x1 = (double)perlin_r_f64(r);
    v->y1 = (double)perlin_r_f64(r);
    v->z1 = (double)perlin_r_f64(r);
    v->color = (uint32_t)perlin_r_u32(r);
    v->width = (float)perlin_r_f32(r);
}
static inline void perlin_write_WorldBoxes(perlin_writer* w, const PerlinWorldBoxes* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->ticks));
    perlin_write_List_WorldBox(w, &v->boxes);
}
static inline void perlin_read_WorldBoxes(perlin_reader* r, PerlinWorldBoxes* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->ticks = (int32_t)perlin_r_i32(r);
    perlin_read_List_WorldBox(r, &v->boxes, a);
}
static inline void perlin_write_ClientBlockEdit(perlin_writer* w, const PerlinClientBlockEdit* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_u32(w, (uint32_t)(v->runtimeId));
}
static inline void perlin_read_ClientBlockEdit(perlin_reader* r, PerlinClientBlockEdit* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->runtimeId = (uint32_t)perlin_r_u32(r);
}
static inline void perlin_write_ClientBlockEdits(perlin_writer* w, const PerlinClientBlockEdits* v) {
    (void)w;
    (void)v;
    perlin_write_List_ClientBlockEdit(w, &v->edits);
}
static inline void perlin_read_ClientBlockEdits(perlin_reader* r, PerlinClientBlockEdits* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_ClientBlockEdit(r, &v->edits, a);
}
static inline void perlin_write_BlockQuery(perlin_writer* w, const PerlinBlockQuery* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
    perlin_write_List_Pair_Str_Str(w, &v->states);
}
static inline void perlin_read_BlockQuery(perlin_reader* r, PerlinBlockQuery* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
    perlin_read_List_Pair_Str_Str(r, &v->states, a);
}
static inline void perlin_write_BlockQueryResult(perlin_writer* w, const PerlinBlockQueryResult* v) {
    (void)w;
    (void)v;
    perlin_w_u8(w, (uint8_t)(v->found));
    perlin_w_u32(w, (uint32_t)(v->runtimeId));
}
static inline void perlin_read_BlockQueryResult(perlin_reader* r, PerlinBlockQueryResult* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->found = (uint8_t)perlin_r_u8(r);
    v->runtimeId = (uint32_t)perlin_r_u32(r);
}
static inline void perlin_write_PacketDropRequest(perlin_writer* w, const PerlinPacketDropRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->ticks));
    perlin_write_List_U32(w, &v->ids);
    perlin_w_u8(w, (uint8_t)(v->outgoing));
}
static inline void perlin_read_PacketDropRequest(perlin_reader* r, PerlinPacketDropRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->ticks = (int32_t)perlin_r_i32(r);
    perlin_read_List_U32(r, &v->ids, a);
    v->outgoing = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_PacketCount(perlin_writer* w, const PerlinPacketCount* v) {
    (void)w;
    (void)v;
    perlin_w_u32(w, (uint32_t)(v->id));
    perlin_w_u32(w, (uint32_t)(v->count));
}
static inline void perlin_read_PacketCount(perlin_reader* r, PerlinPacketCount* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = (uint32_t)perlin_r_u32(r);
    v->count = (uint32_t)perlin_r_u32(r);
}
static inline void perlin_write_Region(perlin_writer* w, const PerlinRegion* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->x0));
    perlin_w_i32(w, (int32_t)(v->y0));
    perlin_w_i32(w, (int32_t)(v->z0));
    perlin_w_i32(w, (int32_t)(v->sx));
    perlin_w_i32(w, (int32_t)(v->sy));
    perlin_w_i32(w, (int32_t)(v->sz));
    perlin_write_List_Str(w, &v->palette);
    perlin_write_List_U16(w, &v->cells);
}
static inline void perlin_read_Region(perlin_reader* r, PerlinRegion* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x0 = (int32_t)perlin_r_i32(r);
    v->y0 = (int32_t)perlin_r_i32(r);
    v->z0 = (int32_t)perlin_r_i32(r);
    v->sx = (int32_t)perlin_r_i32(r);
    v->sy = (int32_t)perlin_r_i32(r);
    v->sz = (int32_t)perlin_r_i32(r);
    perlin_read_List_Str(r, &v->palette, a);
    perlin_read_List_U16(r, &v->cells, a);
}
static inline void perlin_write_FoundBlock(perlin_writer* w, const PerlinFoundBlock* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_str(w, v->name);
}
static inline void perlin_read_FoundBlock(perlin_reader* r, PerlinFoundBlock* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->name = perlin_r_str(r);
}
static inline void perlin_write_SettingDef(perlin_writer* w, const PerlinSettingDef* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->id);
    perlin_w_u8(w, (uint8_t)(v->kind));
    perlin_w_str(w, v->label);
    perlin_w_str(w, v->description);
    perlin_w_str(w, v->icon);
    perlin_w_str(w, v->value);
    perlin_w_str(w, v->fallback);
    perlin_w_f64(w, (double)(v->min));
    perlin_w_f64(w, (double)(v->max));
    perlin_w_f64(w, (double)(v->step));
    perlin_write_List_Str(w, &v->choices);
    perlin_write_List_Str(w, &v->choiceLabels);
    perlin_w_str(w, v->unit);
    perlin_w_u8(w, (uint8_t)(v->restart));
}
static inline void perlin_read_SettingDef(perlin_reader* r, PerlinSettingDef* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = perlin_r_str(r);
    v->kind = (PerlinSettingKind)perlin_r_u8(r);
    v->label = perlin_r_str(r);
    v->description = perlin_r_str(r);
    v->icon = perlin_r_str(r);
    v->value = perlin_r_str(r);
    v->fallback = perlin_r_str(r);
    v->min = (double)perlin_r_f64(r);
    v->max = (double)perlin_r_f64(r);
    v->step = (double)perlin_r_f64(r);
    perlin_read_List_Str(r, &v->choices, a);
    perlin_read_List_Str(r, &v->choiceLabels, a);
    v->unit = perlin_r_str(r);
    v->restart = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_SettingEntry(perlin_writer* w, const PerlinSettingEntry* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->id);
    perlin_w_str(w, v->value);
}
static inline void perlin_read_SettingEntry(perlin_reader* r, PerlinSettingEntry* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = perlin_r_str(r);
    v->value = perlin_r_str(r);
}
static inline void perlin_write_FileLocation(perlin_writer* w, const PerlinFileLocation* v) {
    (void)w;
    (void)v;
    perlin_w_u8(w, (uint8_t)(v->area));
    perlin_w_str(w, v->path);
}
static inline void perlin_read_FileLocation(perlin_reader* r, PerlinFileLocation* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->area = (PerlinArea)perlin_r_u8(r);
    v->path = perlin_r_str(r);
}
static inline void perlin_write_ModEntry(perlin_writer* w, const PerlinModEntry* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
    perlin_w_str(w, v->displayName);
    perlin_w_str(w, v->description);
    perlin_w_str(w, v->version);
    perlin_w_str(w, v->author);
    perlin_w_str(w, v->state);
    perlin_w_u8(w, (uint8_t)(v->enabled));
    perlin_w_u8(w, (uint8_t)(v->self));
    perlin_w_u8(w, (uint8_t)(v->hasSettings));
    perlin_w_str(w, v->website);
    perlin_w_str(w, v->arch);
    perlin_write_List_Str(w, &v->hooks);
    perlin_w_i32(w, (int32_t)(v->priority));
    perlin_w_u32(w, (uint32_t)(v->permissions));
}
static inline void perlin_read_ModEntry(perlin_reader* r, PerlinModEntry* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
    v->displayName = perlin_r_str(r);
    v->description = perlin_r_str(r);
    v->version = perlin_r_str(r);
    v->author = perlin_r_str(r);
    v->state = perlin_r_str(r);
    v->enabled = (uint8_t)perlin_r_u8(r);
    v->self = (uint8_t)perlin_r_u8(r);
    v->hasSettings = (uint8_t)perlin_r_u8(r);
    v->website = perlin_r_str(r);
    v->arch = perlin_r_str(r);
    perlin_read_List_Str(r, &v->hooks, a);
    v->priority = (int32_t)perlin_r_i32(r);
    v->permissions = (uint32_t)perlin_r_u32(r);
}
static inline void perlin_write_HudElementState(perlin_writer* w, const PerlinHudElementState* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->element));
    perlin_w_u8(w, (uint8_t)(v->visible));
    perlin_w_f32(w, (float)(v->dx));
    perlin_w_f32(w, (float)(v->dy));
}
static inline void perlin_read_HudElementState(perlin_reader* r, PerlinHudElementState* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->element = (PerlinHudElement)perlin_r_i32(r);
    v->visible = (uint8_t)perlin_r_u8(r);
    v->dx = (float)perlin_r_f32(r);
    v->dy = (float)perlin_r_f32(r);
}
static inline void perlin_write_DebugLinesData(perlin_writer* w, const PerlinDebugLinesData* v) {
    (void)w;
    (void)v;
    perlin_write_List_Str(w, &v->left);
    perlin_write_List_Str(w, &v->right);
}
static inline void perlin_read_DebugLinesData(perlin_reader* r, PerlinDebugLinesData* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_Str(r, &v->left, a);
    perlin_read_List_Str(r, &v->right, a);
}
static inline void perlin_write_FogOverride(perlin_writer* w, const PerlinFogOverride* v) {
    (void)w;
    (void)v;
    perlin_w_u8(w, (uint8_t)(v->enabled));
    perlin_w_u32(w, (uint32_t)(v->color));
    perlin_w_f32(w, (float)(v->start));
    perlin_w_f32(w, (float)(v->end));
    perlin_w_i32(w, (int32_t)(v->ticks));
}
static inline void perlin_read_FogOverride(perlin_reader* r, PerlinFogOverride* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->enabled = (uint8_t)perlin_r_u8(r);
    v->color = (uint32_t)perlin_r_u32(r);
    v->start = (float)perlin_r_f32(r);
    v->end = (float)perlin_r_f32(r);
    v->ticks = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_ScreenTintData(perlin_writer* w, const PerlinScreenTintData* v) {
    (void)w;
    (void)v;
    perlin_w_u32(w, (uint32_t)(v->color));
    perlin_w_f32(w, (float)(v->vignette));
    perlin_w_i32(w, (int32_t)(v->ticks));
}
static inline void perlin_read_ScreenTintData(perlin_reader* r, PerlinScreenTintData* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->color = (uint32_t)perlin_r_u32(r);
    v->vignette = (float)perlin_r_f32(r);
    v->ticks = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_EntityOverride(perlin_writer* w, const PerlinEntityOverride* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->entity));
    perlin_w_str(w, v->type);
    perlin_w_u8(w, (uint8_t)(v->hidden));
    perlin_w_f32(w, (float)(v->scaleX));
    perlin_w_f32(w, (float)(v->scaleY));
    perlin_w_f32(w, (float)(v->scaleZ));
    perlin_w_u32(w, (uint32_t)(v->tint));
    perlin_w_str(w, v->texture);
    perlin_w_i32(w, (int32_t)(v->nameTag));
    perlin_w_str(w, v->nameText);
    perlin_w_i32(w, (int32_t)(v->ticks));
}
static inline void perlin_read_EntityOverride(perlin_reader* r, PerlinEntityOverride* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->entity = (uint64_t)perlin_r_u64(r);
    v->type = perlin_r_str(r);
    v->hidden = (uint8_t)perlin_r_u8(r);
    v->scaleX = (float)perlin_r_f32(r);
    v->scaleY = (float)perlin_r_f32(r);
    v->scaleZ = (float)perlin_r_f32(r);
    v->tint = (uint32_t)perlin_r_u32(r);
    v->texture = perlin_r_str(r);
    v->nameTag = (int32_t)perlin_r_i32(r);
    v->nameText = perlin_r_str(r);
    v->ticks = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_MeshVertex(perlin_writer* w, const PerlinMeshVertex* v) {
    (void)w;
    (void)v;
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_f32(w, (float)(v->u));
    perlin_w_f32(w, (float)(v->v));
    perlin_w_u32(w, (uint32_t)(v->color));
}
static inline void perlin_read_MeshVertex(perlin_reader* r, PerlinMeshVertex* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->u = (float)perlin_r_f32(r);
    v->v = (float)perlin_r_f32(r);
    v->color = (uint32_t)perlin_r_u32(r);
}
static inline void perlin_write_WorldMeshData(perlin_writer* w, const PerlinWorldMeshData* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->id);
    perlin_w_i32(w, (int32_t)(v->stage));
    perlin_w_u32(w, (uint32_t)(v->texture));
    perlin_w_u8(w, (uint8_t)(v->depthTest));
    perlin_w_i32(w, (int32_t)(v->ticks));
    perlin_write_List_MeshVertex(w, &v->triangles);
}
static inline void perlin_read_WorldMeshData(perlin_reader* r, PerlinWorldMeshData* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = perlin_r_str(r);
    v->stage = (PerlinRenderStage)perlin_r_i32(r);
    v->texture = (uint32_t)perlin_r_u32(r);
    v->depthTest = (uint8_t)perlin_r_u8(r);
    v->ticks = (int32_t)perlin_r_i32(r);
    perlin_read_List_MeshVertex(r, &v->triangles, a);
}
static inline void perlin_write_FrameLimitData(perlin_writer* w, const PerlinFrameLimitData* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->fps));
    perlin_w_i32(w, (int32_t)(v->unfocusedFps));
}
static inline void perlin_read_FrameLimitData(perlin_reader* r, PerlinFrameLimitData* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->fps = (int32_t)perlin_r_i32(r);
    v->unfocusedFps = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_WindowInfo(perlin_writer* w, const PerlinWindowInfo* v) {
    (void)w;
    (void)v;
    perlin_w_u8(w, (uint8_t)(v->focused));
    perlin_w_u8(w, (uint8_t)(v->minimized));
    perlin_w_i32(w, (int32_t)(v->width));
    perlin_w_i32(w, (int32_t)(v->height));
}
static inline void perlin_read_WindowInfo(perlin_reader* r, PerlinWindowInfo* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->focused = (uint8_t)perlin_r_u8(r);
    v->minimized = (uint8_t)perlin_r_u8(r);
    v->width = (int32_t)perlin_r_i32(r);
    v->height = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_ScreenButtonDef(perlin_writer* w, const PerlinScreenButtonDef* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->screen));
    perlin_w_str(w, v->id);
    perlin_w_str(w, v->label);
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->width));
    perlin_w_i32(w, (int32_t)(v->height));
}
static inline void perlin_read_ScreenButtonDef(perlin_reader* r, PerlinScreenButtonDef* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->screen = (PerlinScreen)perlin_r_i32(r);
    v->id = perlin_r_str(r);
    v->label = perlin_r_str(r);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->width = (int32_t)perlin_r_i32(r);
    v->height = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_LoadingOverlayData(perlin_writer* w, const PerlinLoadingOverlayData* v) {
    (void)w;
    (void)v;
    perlin_w_u8(w, (uint8_t)(v->hidden));
}
static inline void perlin_read_LoadingOverlayData(perlin_reader* r, PerlinLoadingOverlayData* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->hidden = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_SkinOverrideData(perlin_writer* w, const PerlinSkinOverrideData* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->player);
    perlin_w_u8(w, (uint8_t)(v->slim));
    perlin_w_bytes(w, v->png);
}
static inline void perlin_read_SkinOverrideData(perlin_reader* r, PerlinSkinOverrideData* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = perlin_r_str(r);
    v->slim = (uint8_t)perlin_r_u8(r);
    v->png = perlin_r_bytes(r);
}
static inline void perlin_write_AmbientSoundData(perlin_writer* w, const PerlinAmbientSoundData* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->id);
    perlin_w_str(w, v->sound);
    perlin_w_f32(w, (float)(v->volume));
    perlin_w_f32(w, (float)(v->pitch));
    perlin_w_u8(w, (uint8_t)(v->play));
}
static inline void perlin_read_AmbientSoundData(perlin_reader* r, PerlinAmbientSoundData* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = perlin_r_str(r);
    v->sound = perlin_r_str(r);
    v->volume = (float)perlin_r_f32(r);
    v->pitch = (float)perlin_r_f32(r);
    v->play = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_ServerPingRequest(perlin_writer* w, const PerlinServerPingRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->host);
    perlin_w_u16(w, (uint16_t)(v->port));
}
static inline void perlin_read_ServerPingRequest(perlin_reader* r, PerlinServerPingRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->host = perlin_r_str(r);
    v->port = (uint16_t)perlin_r_u16(r);
}
static inline void perlin_write_ModMessageData(perlin_writer* w, const PerlinModMessageData* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->to);
    perlin_w_str(w, v->channel);
    perlin_w_bytes(w, v->body);
}
static inline void perlin_read_ModMessageData(perlin_reader* r, PerlinModMessageData* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->to = perlin_r_str(r);
    v->channel = perlin_r_str(r);
    v->body = perlin_r_bytes(r);
}
static inline void perlin_write_PackRegistration(perlin_writer* w, const PerlinPackRegistration* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->path);
    perlin_w_u8(w, (uint8_t)(v->enabled));
}
static inline void perlin_read_PackRegistration(perlin_reader* r, PerlinPackRegistration* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->path = perlin_r_str(r);
    v->enabled = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_ParticleRequest(perlin_writer* w, const PerlinParticleRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_i32(w, (int32_t)(v->count));
    perlin_w_f32(w, (float)(v->spread));
}
static inline void perlin_read_ParticleRequest(perlin_reader* r, PerlinParticleRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->count = (int32_t)perlin_r_i32(r);
    v->spread = (float)perlin_r_f32(r);
}
static inline void perlin_write_KeyBindEx(perlin_writer* w, const PerlinKeyBindEx* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->id);
    perlin_w_str(w, v->label);
    perlin_w_str(w, v->key);
    perlin_w_u8(w, (uint8_t)(v->ctrl));
    perlin_w_u8(w, (uint8_t)(v->shift));
    perlin_w_u8(w, (uint8_t)(v->alt));
    perlin_w_i32(w, (int32_t)(v->context));
}
static inline void perlin_read_KeyBindEx(perlin_reader* r, PerlinKeyBindEx* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = perlin_r_str(r);
    v->label = perlin_r_str(r);
    v->key = perlin_r_str(r);
    v->ctrl = (uint8_t)perlin_r_u8(r);
    v->shift = (uint8_t)perlin_r_u8(r);
    v->alt = (uint8_t)perlin_r_u8(r);
    v->context = (PerlinKeyContext)perlin_r_i32(r);
}
static inline void perlin_write_ChatLine(perlin_writer* w, const PerlinChatLine* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->text);
    perlin_w_f32(w, (float)(v->alpha));
}
static inline void perlin_read_ChatLine(perlin_reader* r, PerlinChatLine* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->text = perlin_r_str(r);
    v->alpha = (float)perlin_r_f32(r);
}
static inline void perlin_write_PackEntry(perlin_writer* w, const PerlinPackEntry* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
    perlin_w_u8(w, (uint8_t)(v->server));
}
static inline void perlin_read_PackEntry(perlin_reader* r, PerlinPackEntry* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
    v->server = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_TitleStateData(perlin_writer* w, const PerlinTitleStateData* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->title);
    perlin_w_str(w, v->subtitle);
    perlin_w_str(w, v->actionBar);
    perlin_w_f32(w, (float)(v->titleAlpha));
    perlin_w_f32(w, (float)(v->actionBarAlpha));
}
static inline void perlin_read_TitleStateData(perlin_reader* r, PerlinTitleStateData* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->title = perlin_r_str(r);
    v->subtitle = perlin_r_str(r);
    v->actionBar = perlin_r_str(r);
    v->titleAlpha = (float)perlin_r_f32(r);
    v->actionBarAlpha = (float)perlin_r_f32(r);
}
static inline void perlin_write_GfxVertex(perlin_writer* w, const PerlinGfxVertex* v) {
    (void)w;
    (void)v;
    perlin_w_f32(w, (float)(v->x));
    perlin_w_f32(w, (float)(v->y));
    perlin_w_f32(w, (float)(v->u));
    perlin_w_f32(w, (float)(v->v));
    perlin_w_u32(w, (uint32_t)(v->color));
}
static inline void perlin_read_GfxVertex(perlin_reader* r, PerlinGfxVertex* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (float)perlin_r_f32(r);
    v->y = (float)perlin_r_f32(r);
    v->u = (float)perlin_r_f32(r);
    v->v = (float)perlin_r_f32(r);
    v->color = (uint32_t)perlin_r_u32(r);
}
static inline void perlin_write_GfxCmd(perlin_writer* w, const PerlinGfxCmd* v) {
    (void)w;
    (void)v;
    for (uint32_t i_ = 0; i_ < 4; i_++) {
        perlin_w_f32(w, (float)(v->clip[i_]));
    }
    perlin_w_u32(w, (uint32_t)(v->texture));
    perlin_w_u32(w, (uint32_t)(v->indexOffset));
    perlin_w_u32(w, (uint32_t)(v->indexCount));
    perlin_w_u32(w, (uint32_t)(v->vertexOffset));
}
static inline void perlin_read_GfxCmd(perlin_reader* r, PerlinGfxCmd* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    for (uint32_t i_ = 0; i_ < 4; i_++) {
        v->clip[i_] = (float)perlin_r_f32(r);
    }
    v->texture = (uint32_t)perlin_r_u32(r);
    v->indexOffset = (uint32_t)perlin_r_u32(r);
    v->indexCount = (uint32_t)perlin_r_u32(r);
    v->vertexOffset = (uint32_t)perlin_r_u32(r);
}
static inline void perlin_write_GfxHeader(perlin_writer* w, const PerlinGfxHeader* v) {
    (void)w;
    (void)v;
    perlin_w_u32(w, (uint32_t)(v->vertexCount));
    perlin_w_u32(w, (uint32_t)(v->indexCount));
    perlin_w_u32(w, (uint32_t)(v->commandCount));
    perlin_w_u32(w, (uint32_t)(v->flags));
}
static inline void perlin_read_GfxHeader(perlin_reader* r, PerlinGfxHeader* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->vertexCount = (uint32_t)perlin_r_u32(r);
    v->indexCount = (uint32_t)perlin_r_u32(r);
    v->commandCount = (uint32_t)perlin_r_u32(r);
    v->flags = (uint32_t)perlin_r_u32(r);
}
static inline void perlin_write_NativeHandles(perlin_writer* w, const PerlinNativeHandles* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->api);
    perlin_w_str(w, v->adapter);
    perlin_w_u64(w, (uint64_t)(v->window));
    perlin_w_u64(w, (uint64_t)(v->display));
    perlin_w_u64(w, (uint64_t)(v->device));
}
static inline void perlin_read_NativeHandles(perlin_reader* r, PerlinNativeHandles* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->api = perlin_r_str(r);
    v->adapter = perlin_r_str(r);
    v->window = (uint64_t)perlin_r_u64(r);
    v->display = (uint64_t)perlin_r_u64(r);
    v->device = (uint64_t)perlin_r_u64(r);
}
static inline void perlin_write_GfxDisplayInfo(perlin_writer* w, const PerlinGfxDisplayInfo* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->width));
    perlin_w_i32(w, (int32_t)(v->height));
    perlin_w_i32(w, (int32_t)(v->guiScale));
    perlin_w_f32(w, (float)(v->mouseX));
    perlin_w_f32(w, (float)(v->mouseY));
    perlin_w_u8(w, (uint8_t)(v->mouseFree));
    perlin_w_u8(w, (uint8_t)(v->inGame));
}
static inline void perlin_read_GfxDisplayInfo(perlin_reader* r, PerlinGfxDisplayInfo* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->width = (int32_t)perlin_r_i32(r);
    v->height = (int32_t)perlin_r_i32(r);
    v->guiScale = (int32_t)perlin_r_i32(r);
    v->mouseX = (float)perlin_r_f32(r);
    v->mouseY = (float)perlin_r_f32(r);
    v->mouseFree = (uint8_t)perlin_r_u8(r);
    v->inGame = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_TextMessage(perlin_writer* w, const PerlinTextMessage* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->text);
}
static inline void perlin_read_TextMessage(perlin_reader* r, PerlinTextMessage* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->text = perlin_r_str(r);
}
static inline void perlin_write_PlayerText(perlin_writer* w, const PerlinPlayerText* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_str(w, v->text);
}
static inline void perlin_read_PlayerText(perlin_reader* r, PerlinPlayerText* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->text = perlin_r_str(r);
}
static inline void perlin_write_OnlinePlayer(perlin_writer* w, const PerlinOnlinePlayer* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->id));
    perlin_w_str(w, v->name);
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
}
static inline void perlin_read_OnlinePlayer(perlin_reader* r, PerlinOnlinePlayer* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = (uint64_t)perlin_r_u64(r);
    v->name = perlin_r_str(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
}
static inline void perlin_write_OnlinePlayerList(perlin_writer* w, const PerlinOnlinePlayerList* v) {
    (void)w;
    (void)v;
    perlin_write_List_OnlinePlayer(w, &v->players);
}
static inline void perlin_read_OnlinePlayerList(perlin_reader* r, PerlinOnlinePlayerList* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_OnlinePlayer(r, &v->players, a);
}
static inline void perlin_write_ServerTeleport(perlin_writer* w, const PerlinServerTeleport* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
}
static inline void perlin_read_ServerTeleport(perlin_reader* r, PerlinServerTeleport* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
}
static inline void perlin_write_BlockCoord(perlin_writer* w, const PerlinBlockCoord* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
}
static inline void perlin_read_BlockCoord(perlin_reader* r, PerlinBlockCoord* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_BlockWrite(perlin_writer* w, const PerlinBlockWrite* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_str(w, v->block);
}
static inline void perlin_read_BlockWrite(perlin_reader* r, PerlinBlockWrite* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->block = perlin_r_str(r);
}
static inline void perlin_write_ServerStatus(perlin_writer* w, const PerlinServerStatus* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
    perlin_w_u32(w, (uint32_t)(v->players));
    perlin_w_u32(w, (uint32_t)(v->maxPlayers));
    perlin_w_u64(w, (uint64_t)(v->tick));
    perlin_w_str(w, v->backend);
}
static inline void perlin_read_ServerStatus(perlin_reader* r, PerlinServerStatus* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
    v->players = (uint32_t)perlin_r_u32(r);
    v->maxPlayers = (uint32_t)perlin_r_u32(r);
    v->tick = (uint64_t)perlin_r_u64(r);
    v->backend = perlin_r_str(r);
}
static inline void perlin_write_GiveItemRequest(perlin_writer* w, const PerlinGiveItemRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_str(w, v->item);
    perlin_w_u32(w, (uint32_t)(v->count));
}
static inline void perlin_read_GiveItemRequest(perlin_reader* r, PerlinGiveItemRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->item = perlin_r_str(r);
    v->count = (uint32_t)perlin_r_u32(r);
}
static inline void perlin_write_GiveItemResult(perlin_writer* w, const PerlinGiveItemResult* v) {
    (void)w;
    (void)v;
    perlin_w_u32(w, (uint32_t)(v->leftover));
}
static inline void perlin_read_GiveItemResult(perlin_reader* r, PerlinGiveItemResult* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->leftover = (uint32_t)perlin_r_u32(r);
}
static inline void perlin_write_EventNothing(perlin_writer* w, const PerlinEventNothing* v) {
    (void)w;
    (void)v;
}
static inline void perlin_read_EventNothing(perlin_reader* r, PerlinEventNothing* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
}
static inline void perlin_write_EventTick(perlin_writer* w, const PerlinEventTick* v) {
    (void)w;
    (void)v;
    perlin_w_f32(w, (float)(v->dt));
    perlin_w_u64(w, (uint64_t)(v->tick));
}
static inline void perlin_read_EventTick(perlin_reader* r, PerlinEventTick* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->dt = (float)perlin_r_f32(r);
    v->tick = (uint64_t)perlin_r_u64(r);
}
static inline void perlin_write_EventFrame(perlin_writer* w, const PerlinEventFrame* v) {
    (void)w;
    (void)v;
    perlin_w_f32(w, (float)(v->dt));
    perlin_w_f32(w, (float)(v->fps));
    perlin_w_i32(w, (int32_t)(v->guiWidth));
    perlin_w_i32(w, (int32_t)(v->guiHeight));
}
static inline void perlin_read_EventFrame(perlin_reader* r, PerlinEventFrame* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->dt = (float)perlin_r_f32(r);
    v->fps = (float)perlin_r_f32(r);
    v->guiWidth = (int32_t)perlin_r_i32(r);
    v->guiHeight = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_EventChat(perlin_writer* w, const PerlinEventChat* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->type));
    perlin_w_str(w, v->sender);
    perlin_w_str(w, v->message);
    perlin_w_str(w, v->raw);
}
static inline void perlin_read_EventChat(perlin_reader* r, PerlinEventChat* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->type = (PerlinChatType)perlin_r_i32(r);
    v->sender = perlin_r_str(r);
    v->message = perlin_r_str(r);
    v->raw = perlin_r_str(r);
}
static inline void perlin_write_EventKey(perlin_writer* w, const PerlinEventKey* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->id);
    perlin_w_u8(w, (uint8_t)(v->down));
}
static inline void perlin_read_EventKey(perlin_reader* r, PerlinEventKey* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = perlin_r_str(r);
    v->down = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_EventCommand(perlin_writer* w, const PerlinEventCommand* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
    perlin_w_str(w, v->args);
    perlin_w_u64(w, (uint64_t)(v->player));
}
static inline void perlin_read_EventCommand(perlin_reader* r, PerlinEventCommand* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
    v->args = perlin_r_str(r);
    v->player = (uint64_t)perlin_r_u64(r);
}
static inline void perlin_write_EventConnection(perlin_writer* w, const PerlinEventConnection* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->server);
    perlin_w_str(w, v->message);
    perlin_w_i32(w, (int32_t)(v->reason));
}
static inline void perlin_read_EventConnection(perlin_reader* r, PerlinEventConnection* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->server = perlin_r_str(r);
    v->message = perlin_r_str(r);
    v->reason = (PerlinDisconnectReason)perlin_r_i32(r);
}
static inline void perlin_write_EventDamage(perlin_writer* w, const PerlinEventDamage* v) {
    (void)w;
    (void)v;
    perlin_w_u8(w, (uint8_t)(v->known));
    perlin_w_f32(w, (float)(v->dx));
    perlin_w_f32(w, (float)(v->dz));
    perlin_w_f32(w, (float)(v->health));
}
static inline void perlin_read_EventDamage(perlin_reader* r, PerlinEventDamage* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->known = (uint8_t)perlin_r_u8(r);
    v->dx = (float)perlin_r_f32(r);
    v->dz = (float)perlin_r_f32(r);
    v->health = (float)perlin_r_f32(r);
}
static inline void perlin_write_EventText(perlin_writer* w, const PerlinEventText* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->text);
}
static inline void perlin_read_EventText(perlin_reader* r, PerlinEventText* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->text = perlin_r_str(r);
}
static inline void perlin_write_EventDimension(perlin_writer* w, const PerlinEventDimension* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->from));
    perlin_w_i32(w, (int32_t)(v->to));
}
static inline void perlin_read_EventDimension(perlin_reader* r, PerlinEventDimension* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->from = (PerlinDimension)perlin_r_i32(r);
    v->to = (PerlinDimension)perlin_r_i32(r);
}
static inline void perlin_write_EventScreen(perlin_writer* w, const PerlinEventScreen* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->from));
    perlin_w_i32(w, (int32_t)(v->to));
}
static inline void perlin_read_EventScreen(perlin_reader* r, PerlinEventScreen* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->from = (PerlinScreen)perlin_r_i32(r);
    v->to = (PerlinScreen)perlin_r_i32(r);
}
static inline void perlin_write_EventSlot(perlin_writer* w, const PerlinEventSlot* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->slot));
    perlin_write_Item(w, &v->item);
}
static inline void perlin_read_EventSlot(perlin_reader* r, PerlinEventSlot* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->slot = (int32_t)perlin_r_i32(r);
    perlin_read_Item(r, &v->item, a);
}
static inline void perlin_write_EventEntity(perlin_writer* w, const PerlinEventEntity* v) {
    (void)w;
    (void)v;
    perlin_write_Entity(w, &v->entity);
}
static inline void perlin_read_EventEntity(perlin_reader* r, PerlinEventEntity* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_Entity(r, &v->entity, a);
}
static inline void perlin_write_EventSound(perlin_writer* w, const PerlinEventSound* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_f32(w, (float)(v->volume));
    perlin_w_f32(w, (float)(v->pitch));
}
static inline void perlin_read_EventSound(perlin_reader* r, PerlinEventSound* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->volume = (float)perlin_r_f32(r);
    v->pitch = (float)perlin_r_f32(r);
}
static inline void perlin_write_EventHttp(perlin_writer* w, const PerlinEventHttp* v) {
    (void)w;
    (void)v;
    perlin_w_u32(w, (uint32_t)(v->id));
    perlin_w_i32(w, (int32_t)(v->status));
    perlin_w_str(w, v->error);
    perlin_write_List_Pair_Str_Str(w, &v->headers);
    perlin_w_bytes(w, v->body);
}
static inline void perlin_read_EventHttp(perlin_reader* r, PerlinEventHttp* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = (uint32_t)perlin_r_u32(r);
    v->status = (int32_t)perlin_r_i32(r);
    v->error = perlin_r_str(r);
    perlin_read_List_Pair_Str_Str(r, &v->headers, a);
    v->body = perlin_r_bytes(r);
}
static inline void perlin_write_EventPacket(perlin_writer* w, const PerlinEventPacket* v) {
    (void)w;
    (void)v;
    perlin_w_u32(w, (uint32_t)(v->id));
    perlin_w_u8(w, (uint8_t)(v->outgoing));
    perlin_w_bytes(w, v->body);
}
static inline void perlin_read_EventPacket(perlin_reader* r, PerlinEventPacket* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = (uint32_t)perlin_r_u32(r);
    v->outgoing = (uint8_t)perlin_r_u8(r);
    v->body = perlin_r_bytes(r);
}
static inline void perlin_write_EventTitle(perlin_writer* w, const PerlinEventTitle* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->title);
    perlin_w_str(w, v->subtitle);
    perlin_w_str(w, v->actionBar);
}
static inline void perlin_read_EventTitle(perlin_reader* r, PerlinEventTitle* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->title = perlin_r_str(r);
    v->subtitle = perlin_r_str(r);
    v->actionBar = perlin_r_str(r);
}
static inline void perlin_write_EventLook(perlin_writer* w, const PerlinEventLook* v) {
    (void)w;
    (void)v;
    perlin_w_f32(w, (float)(v->dx));
    perlin_w_f32(w, (float)(v->dy));
    perlin_w_f32(w, (float)(v->dt));
    perlin_w_f64(w, (double)(v->dirX));
    perlin_w_f64(w, (double)(v->dirY));
    perlin_w_f64(w, (double)(v->dirZ));
    perlin_w_f64(w, (double)(v->vx));
    perlin_w_f64(w, (double)(v->vy));
    perlin_w_f64(w, (double)(v->vz));
    perlin_w_u32(w, (uint32_t)(v->keys));
    perlin_w_u8(w, (uint8_t)(v->gliding));
    perlin_w_u8(w, (uint8_t)(v->underwater));
    perlin_w_u8(w, (uint8_t)(v->thirdPerson));
    perlin_w_u8(w, (uint8_t)(v->onGround));
}
static inline void perlin_read_EventLook(perlin_reader* r, PerlinEventLook* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->dx = (float)perlin_r_f32(r);
    v->dy = (float)perlin_r_f32(r);
    v->dt = (float)perlin_r_f32(r);
    v->dirX = (double)perlin_r_f64(r);
    v->dirY = (double)perlin_r_f64(r);
    v->dirZ = (double)perlin_r_f64(r);
    v->vx = (double)perlin_r_f64(r);
    v->vy = (double)perlin_r_f64(r);
    v->vz = (double)perlin_r_f64(r);
    v->keys = (uint32_t)perlin_r_u32(r);
    v->gliding = (uint8_t)perlin_r_u8(r);
    v->underwater = (uint8_t)perlin_r_u8(r);
    v->thirdPerson = (uint8_t)perlin_r_u8(r);
    v->onGround = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_EventSettingChanged(perlin_writer* w, const PerlinEventSettingChanged* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->id);
    perlin_w_str(w, v->value);
    perlin_w_u8(w, (uint8_t)(v->kind));
}
static inline void perlin_read_EventSettingChanged(perlin_reader* r, PerlinEventSettingChanged* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = perlin_r_str(r);
    v->value = perlin_r_str(r);
    v->kind = (PerlinSettingKind)perlin_r_u8(r);
}
static inline void perlin_write_EventMouseMove(perlin_writer* w, const PerlinEventMouseMove* v) {
    (void)w;
    (void)v;
    perlin_w_f32(w, (float)(v->x));
    perlin_w_f32(w, (float)(v->y));
}
static inline void perlin_read_EventMouseMove(perlin_reader* r, PerlinEventMouseMove* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (float)perlin_r_f32(r);
    v->y = (float)perlin_r_f32(r);
}
static inline void perlin_write_EventMouseButton(perlin_writer* w, const PerlinEventMouseButton* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->button));
    perlin_w_u8(w, (uint8_t)(v->down));
    perlin_w_f32(w, (float)(v->x));
    perlin_w_f32(w, (float)(v->y));
}
static inline void perlin_read_EventMouseButton(perlin_reader* r, PerlinEventMouseButton* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->button = (int32_t)perlin_r_i32(r);
    v->down = (uint8_t)perlin_r_u8(r);
    v->x = (float)perlin_r_f32(r);
    v->y = (float)perlin_r_f32(r);
}
static inline void perlin_write_EventMouseWheel(perlin_writer* w, const PerlinEventMouseWheel* v) {
    (void)w;
    (void)v;
    perlin_w_f32(w, (float)(v->dx));
    perlin_w_f32(w, (float)(v->dy));
}
static inline void perlin_read_EventMouseWheel(perlin_reader* r, PerlinEventMouseWheel* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->dx = (float)perlin_r_f32(r);
    v->dy = (float)perlin_r_f32(r);
}
static inline void perlin_write_EventKeyRaw(perlin_writer* w, const PerlinEventKeyRaw* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->key));
    perlin_w_u8(w, (uint8_t)(v->down));
    perlin_w_u8(w, (uint8_t)(v->repeat));
    perlin_w_u8(w, (uint8_t)(v->ctrl));
    perlin_w_u8(w, (uint8_t)(v->shift));
    perlin_w_u8(w, (uint8_t)(v->alt));
}
static inline void perlin_read_EventKeyRaw(perlin_reader* r, PerlinEventKeyRaw* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->key = (int32_t)perlin_r_i32(r);
    v->down = (uint8_t)perlin_r_u8(r);
    v->repeat = (uint8_t)perlin_r_u8(r);
    v->ctrl = (uint8_t)perlin_r_u8(r);
    v->shift = (uint8_t)perlin_r_u8(r);
    v->alt = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_EventScreenRender(perlin_writer* w, const PerlinEventScreenRender* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->screen));
    perlin_w_i32(w, (int32_t)(v->guiWidth));
    perlin_w_i32(w, (int32_t)(v->guiHeight));
    perlin_w_f32(w, (float)(v->mouseX));
    perlin_w_f32(w, (float)(v->mouseY));
}
static inline void perlin_read_EventScreenRender(perlin_reader* r, PerlinEventScreenRender* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->screen = (PerlinScreen)perlin_r_i32(r);
    v->guiWidth = (int32_t)perlin_r_i32(r);
    v->guiHeight = (int32_t)perlin_r_i32(r);
    v->mouseX = (float)perlin_r_f32(r);
    v->mouseY = (float)perlin_r_f32(r);
}
static inline void perlin_write_EventSlotClick(perlin_writer* w, const PerlinEventSlotClick* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->slot));
    perlin_w_u8(w, (uint8_t)(v->right));
    perlin_w_u8(w, (uint8_t)(v->shift));
}
static inline void perlin_read_EventSlotClick(perlin_reader* r, PerlinEventSlotClick* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->slot = (int32_t)perlin_r_i32(r);
    v->right = (uint8_t)perlin_r_u8(r);
    v->shift = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_EventInteract(perlin_writer* w, const PerlinEventInteract* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->kind));
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_i32(w, (int32_t)(v->face));
    perlin_w_u64(w, (uint64_t)(v->entity));
}
static inline void perlin_read_EventInteract(perlin_reader* r, PerlinEventInteract* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->kind = (PerlinInteractKind)perlin_r_i32(r);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->face = (PerlinFace)perlin_r_i32(r);
    v->entity = (uint64_t)perlin_r_u64(r);
}
static inline void perlin_write_EventChunk(perlin_writer* w, const PerlinEventChunk* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_i32(w, (int32_t)(v->dimension));
}
static inline void perlin_read_EventChunk(perlin_reader* r, PerlinEventChunk* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->dimension = (PerlinDimension)perlin_r_i32(r);
}
static inline void perlin_write_EventBlockChanged(perlin_writer* w, const PerlinEventBlockChanged* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_str(w, v->from);
    perlin_w_str(w, v->to);
}
static inline void perlin_read_EventBlockChanged(perlin_reader* r, PerlinEventBlockChanged* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->from = perlin_r_str(r);
    v->to = perlin_r_str(r);
}
static inline void perlin_write_EventLevel(perlin_writer* w, const PerlinEventLevel* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->dimension));
    perlin_w_str(w, v->server);
}
static inline void perlin_read_EventLevel(perlin_reader* r, PerlinEventLevel* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->dimension = (PerlinDimension)perlin_r_i32(r);
    v->server = perlin_r_str(r);
}
static inline void perlin_write_EventToast(perlin_writer* w, const PerlinEventToast* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->title);
    perlin_w_str(w, v->body);
}
static inline void perlin_read_EventToast(perlin_reader* r, PerlinEventToast* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->title = perlin_r_str(r);
    v->body = perlin_r_str(r);
}
static inline void perlin_write_EventModMessage(perlin_writer* w, const PerlinEventModMessage* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->from);
    perlin_w_str(w, v->channel);
    perlin_w_bytes(w, v->body);
}
static inline void perlin_read_EventModMessage(perlin_reader* r, PerlinEventModMessage* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->from = perlin_r_str(r);
    v->channel = perlin_r_str(r);
    v->body = perlin_r_bytes(r);
}
static inline void perlin_write_EventTooltip(perlin_writer* w, const PerlinEventTooltip* v) {
    (void)w;
    (void)v;
    perlin_write_Item(w, &v->item);
    perlin_write_List_Str(w, &v->lines);
}
static inline void perlin_read_EventTooltip(perlin_reader* r, PerlinEventTooltip* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_Item(r, &v->item, a);
    perlin_read_List_Str(r, &v->lines, a);
}
static inline void perlin_write_EventScreenButton(perlin_writer* w, const PerlinEventScreenButton* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->id);
}
static inline void perlin_read_EventScreenButton(perlin_reader* r, PerlinEventScreenButton* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = perlin_r_str(r);
}
static inline void perlin_write_EventWindow(perlin_writer* w, const PerlinEventWindow* v) {
    (void)w;
    (void)v;
    perlin_w_u8(w, (uint8_t)(v->focused));
    perlin_w_u8(w, (uint8_t)(v->minimized));
}
static inline void perlin_read_EventWindow(perlin_reader* r, PerlinEventWindow* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->focused = (uint8_t)perlin_r_u8(r);
    v->minimized = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_EventServerPing(perlin_writer* w, const PerlinEventServerPing* v) {
    (void)w;
    (void)v;
    perlin_w_u32(w, (uint32_t)(v->id));
    perlin_w_u8(w, (uint8_t)(v->ok));
    perlin_w_str(w, v->motd);
    perlin_w_str(w, v->version);
    perlin_w_i32(w, (int32_t)(v->players));
    perlin_w_i32(w, (int32_t)(v->maxPlayers));
    perlin_w_i32(w, (int32_t)(v->protocol));
    perlin_w_f32(w, (float)(v->latencyMs));
}
static inline void perlin_read_EventServerPing(perlin_reader* r, PerlinEventServerPing* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = (uint32_t)perlin_r_u32(r);
    v->ok = (uint8_t)perlin_r_u8(r);
    v->motd = perlin_r_str(r);
    v->version = perlin_r_str(r);
    v->players = (int32_t)perlin_r_i32(r);
    v->maxPlayers = (int32_t)perlin_r_i32(r);
    v->protocol = (int32_t)perlin_r_i32(r);
    v->latencyMs = (float)perlin_r_f32(r);
}
static inline void perlin_write_EventPlayer(perlin_writer* w, const PerlinEventPlayer* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_str(w, v->name);
}
static inline void perlin_read_EventPlayer(perlin_reader* r, PerlinEventPlayer* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->name = perlin_r_str(r);
}
static inline void perlin_write_EventPlayerChat(perlin_writer* w, const PerlinEventPlayerChat* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_str(w, v->name);
    perlin_w_str(w, v->message);
}
static inline void perlin_read_EventPlayerChat(perlin_reader* r, PerlinEventPlayerChat* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->name = perlin_r_str(r);
    v->message = perlin_r_str(r);
}
static inline void perlin_write_EventPlayerBlock(perlin_writer* w, const PerlinEventPlayerBlock* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_str(w, v->block);
}
static inline void perlin_read_EventPlayerBlock(perlin_reader* r, PerlinEventPlayerBlock* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->block = perlin_r_str(r);
}
static inline void perlin_write_LogRequest(perlin_writer* w, const PerlinLogRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->level));
    perlin_w_str(w, v->text);
}
static inline void perlin_read_LogRequest(perlin_reader* r, PerlinLogRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->level = (PerlinLogLevel)perlin_r_i32(r);
    v->text = perlin_r_str(r);
}
static inline void perlin_write_PlayerEffectsResponse(perlin_writer* w, const PerlinPlayerEffectsResponse* v) {
    (void)w;
    (void)v;
    perlin_write_List_Effect(w, &v->effects);
}
static inline void perlin_read_PlayerEffectsResponse(perlin_reader* r, PerlinPlayerEffectsResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_Effect(r, &v->effects, a);
}
static inline void perlin_write_PlayerLookResponse(perlin_writer* w, const PerlinPlayerLookResponse* v) {
    (void)w;
    (void)v;
    perlin_w_f32(w, (float)(v->yaw));
    perlin_w_f32(w, (float)(v->pitch));
}
static inline void perlin_read_PlayerLookResponse(perlin_reader* r, PerlinPlayerLookResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->yaw = (float)perlin_r_f32(r);
    v->pitch = (float)perlin_r_f32(r);
}
static inline void perlin_write_InventorySelectRequest(perlin_writer* w, const PerlinInventorySelectRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->slot));
}
static inline void perlin_read_InventorySelectRequest(perlin_reader* r, PerlinInventorySelectRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->slot = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_InventoryDropRequest(perlin_writer* w, const PerlinInventoryDropRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->count));
}
static inline void perlin_read_InventoryDropRequest(perlin_reader* r, PerlinInventoryDropRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->count = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_InventorySwapRequest(perlin_writer* w, const PerlinInventorySwapRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->a));
    perlin_w_i32(w, (int32_t)(v->b));
}
static inline void perlin_read_InventorySwapRequest(perlin_reader* r, PerlinInventorySwapRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->a = (int32_t)perlin_r_i32(r);
    v->b = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_InventoryClickRequest(perlin_writer* w, const PerlinInventoryClickRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->slot));
    perlin_w_u8(w, (uint8_t)(v->right));
    perlin_w_u8(w, (uint8_t)(v->shift));
}
static inline void perlin_read_InventoryClickRequest(perlin_reader* r, PerlinInventoryClickRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->slot = (int32_t)perlin_r_i32(r);
    v->right = (uint8_t)perlin_r_u8(r);
    v->shift = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_BlockRequest(perlin_writer* w, const PerlinBlockRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
}
static inline void perlin_read_BlockRequest(perlin_reader* r, PerlinBlockRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_RegionRequest(perlin_writer* w, const PerlinRegionRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->x0));
    perlin_w_i32(w, (int32_t)(v->y0));
    perlin_w_i32(w, (int32_t)(v->z0));
    perlin_w_i32(w, (int32_t)(v->x1));
    perlin_w_i32(w, (int32_t)(v->y1));
    perlin_w_i32(w, (int32_t)(v->z1));
}
static inline void perlin_read_RegionRequest(perlin_reader* r, PerlinRegionRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x0 = (int32_t)perlin_r_i32(r);
    v->y0 = (int32_t)perlin_r_i32(r);
    v->z0 = (int32_t)perlin_r_i32(r);
    v->x1 = (int32_t)perlin_r_i32(r);
    v->y1 = (int32_t)perlin_r_i32(r);
    v->z1 = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_RaycastRequest(perlin_writer* w, const PerlinRaycastRequest* v) {
    (void)w;
    (void)v;
    perlin_w_f64(w, (double)(v->ox));
    perlin_w_f64(w, (double)(v->oy));
    perlin_w_f64(w, (double)(v->oz));
    perlin_w_f64(w, (double)(v->dx));
    perlin_w_f64(w, (double)(v->dy));
    perlin_w_f64(w, (double)(v->dz));
    perlin_w_f64(w, (double)(v->maxDistance));
    perlin_w_u8(w, (uint8_t)(v->liquids));
}
static inline void perlin_read_RaycastRequest(perlin_reader* r, PerlinRaycastRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->ox = (double)perlin_r_f64(r);
    v->oy = (double)perlin_r_f64(r);
    v->oz = (double)perlin_r_f64(r);
    v->dx = (double)perlin_r_f64(r);
    v->dy = (double)perlin_r_f64(r);
    v->dz = (double)perlin_r_f64(r);
    v->maxDistance = (double)perlin_r_f64(r);
    v->liquids = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_FindBlocksRequest(perlin_writer* w, const PerlinFindBlocksRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_i32(w, (int32_t)(v->radius));
    perlin_w_i32(w, (int32_t)(v->limit));
    perlin_w_u8(w, (uint8_t)(v->exact));
    perlin_w_str(w, v->name);
}
static inline void perlin_read_FindBlocksRequest(perlin_reader* r, PerlinFindBlocksRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->radius = (int32_t)perlin_r_i32(r);
    v->limit = (int32_t)perlin_r_i32(r);
    v->exact = (uint8_t)perlin_r_u8(r);
    v->name = perlin_r_str(r);
}
static inline void perlin_write_FindBlocksResponse(perlin_writer* w, const PerlinFindBlocksResponse* v) {
    (void)w;
    (void)v;
    perlin_write_List_FoundBlock(w, &v->blocks);
}
static inline void perlin_read_FindBlocksResponse(perlin_reader* r, PerlinFindBlocksResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_FoundBlock(r, &v->blocks, a);
}
static inline void perlin_write_ChunkLoadedRequest(perlin_writer* w, const PerlinChunkLoadedRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->z));
}
static inline void perlin_read_ChunkLoadedRequest(perlin_reader* r, PerlinChunkLoadedRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_BlockNamesResponse(perlin_writer* w, const PerlinBlockNamesResponse* v) {
    (void)w;
    (void)v;
    perlin_write_List_Str(w, &v->names);
}
static inline void perlin_read_BlockNamesResponse(perlin_reader* r, PerlinBlockNamesResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_Str(r, &v->names, a);
}
static inline void perlin_write_BlockPropsRequest(perlin_writer* w, const PerlinBlockPropsRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
}
static inline void perlin_read_BlockPropsRequest(perlin_reader* r, PerlinBlockPropsRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
}
static inline void perlin_write_BlockEntityDataRequest(perlin_writer* w, const PerlinBlockEntityDataRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
}
static inline void perlin_read_BlockEntityDataRequest(perlin_reader* r, PerlinBlockEntityDataRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_DisplayNameRequest(perlin_writer* w, const PerlinDisplayNameRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u8(w, (uint8_t)(v->kind));
    perlin_w_str(w, v->id);
    perlin_w_i32(w, (int32_t)(v->aux));
}
static inline void perlin_read_DisplayNameRequest(perlin_reader* r, PerlinDisplayNameRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->kind = (PerlinNameKind)perlin_r_u8(r);
    v->id = perlin_r_str(r);
    v->aux = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_MapChunkRequest(perlin_writer* w, const PerlinMapChunkRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_i32(w, (int32_t)(v->caveY));
}
static inline void perlin_read_MapChunkRequest(perlin_reader* r, PerlinMapChunkRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->caveY = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_EntitiesRequest(perlin_writer* w, const PerlinEntitiesRequest* v) {
    (void)w;
    (void)v;
    perlin_w_f64(w, (double)(v->radius));
    perlin_w_str(w, v->type);
    perlin_w_u8(w, (uint8_t)(v->playersOnly));
    perlin_w_u8(w, (uint8_t)(v->includeItems));
}
static inline void perlin_read_EntitiesRequest(perlin_reader* r, PerlinEntitiesRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->radius = (double)perlin_r_f64(r);
    v->type = perlin_r_str(r);
    v->playersOnly = (uint8_t)perlin_r_u8(r);
    v->includeItems = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_EntitiesResponse(perlin_writer* w, const PerlinEntitiesResponse* v) {
    (void)w;
    (void)v;
    perlin_write_List_Entity(w, &v->entities);
}
static inline void perlin_read_EntitiesResponse(perlin_reader* r, PerlinEntitiesResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_Entity(r, &v->entities, a);
}
static inline void perlin_write_EntityByIdRequest(perlin_writer* w, const PerlinEntityByIdRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->id));
}
static inline void perlin_read_EntityByIdRequest(perlin_reader* r, PerlinEntityByIdRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = (uint64_t)perlin_r_u64(r);
}
static inline void perlin_write_PlayerListResponse(perlin_writer* w, const PerlinPlayerListResponse* v) {
    (void)w;
    (void)v;
    perlin_write_List_PlayerEntry(w, &v->players);
}
static inline void perlin_read_PlayerListResponse(perlin_reader* r, PerlinPlayerListResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_PlayerEntry(r, &v->players, a);
}
static inline void perlin_write_OptionGetRequest(perlin_writer* w, const PerlinOptionGetRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->key);
}
static inline void perlin_read_OptionGetRequest(perlin_reader* r, PerlinOptionGetRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->key = perlin_r_str(r);
}
static inline void perlin_write_OptionSetRequest(perlin_writer* w, const PerlinOptionSetRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->key);
    perlin_w_str(w, v->value);
}
static inline void perlin_read_OptionSetRequest(perlin_reader* r, PerlinOptionSetRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->key = perlin_r_str(r);
    v->value = perlin_r_str(r);
}
static inline void perlin_write_TranslateRequest(perlin_writer* w, const PerlinTranslateRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->key);
}
static inline void perlin_read_TranslateRequest(perlin_reader* r, PerlinTranslateRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->key = perlin_r_str(r);
}
static inline void perlin_write_PerspectiveSetRequest(perlin_writer* w, const PerlinPerspectiveSetRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->perspective));
}
static inline void perlin_read_PerspectiveSetRequest(perlin_reader* r, PerlinPerspectiveSetRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->perspective = (PerlinPerspective)perlin_r_i32(r);
}
static inline void perlin_write_ResourcePacksResponse(perlin_writer* w, const PerlinResourcePacksResponse* v) {
    (void)w;
    (void)v;
    perlin_write_List_PackEntry(w, &v->packs);
}
static inline void perlin_read_ResourcePacksResponse(perlin_reader* r, PerlinResourcePacksResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_PackEntry(r, &v->packs, a);
}
static inline void perlin_write_InputDownRequest(perlin_writer* w, const PerlinInputDownRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
}
static inline void perlin_read_InputDownRequest(perlin_reader* r, PerlinInputDownRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
}
static inline void perlin_write_KeyRegisterRequest(perlin_writer* w, const PerlinKeyRegisterRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->id);
    perlin_w_str(w, v->label);
    perlin_w_str(w, v->fallback);
}
static inline void perlin_read_KeyRegisterRequest(perlin_reader* r, PerlinKeyRegisterRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = perlin_r_str(r);
    v->label = perlin_r_str(r);
    v->fallback = perlin_r_str(r);
}
static inline void perlin_write_InputActionsResponse(perlin_writer* w, const PerlinInputActionsResponse* v) {
    (void)w;
    (void)v;
    perlin_write_List_Str(w, &v->actions);
}
static inline void perlin_read_InputActionsResponse(perlin_reader* r, PerlinInputActionsResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_Str(r, &v->actions, a);
}
static inline void perlin_write_MousePositionResponse(perlin_writer* w, const PerlinMousePositionResponse* v) {
    (void)w;
    (void)v;
    perlin_w_f32(w, (float)(v->x));
    perlin_w_f32(w, (float)(v->y));
    perlin_w_u8(w, (uint8_t)(v->captured));
}
static inline void perlin_read_MousePositionResponse(perlin_reader* r, PerlinMousePositionResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (float)perlin_r_f32(r);
    v->y = (float)perlin_r_f32(r);
    v->captured = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_ChatRequest(perlin_writer* w, const PerlinChatRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->text);
}
static inline void perlin_read_ChatRequest(perlin_reader* r, PerlinChatRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->text = perlin_r_str(r);
}
static inline void perlin_write_CommandRequest(perlin_writer* w, const PerlinCommandRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->line);
}
static inline void perlin_read_CommandRequest(perlin_reader* r, PerlinCommandRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->line = perlin_r_str(r);
}
static inline void perlin_write_LocalMessageRequest(perlin_writer* w, const PerlinLocalMessageRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->text);
}
static inline void perlin_read_LocalMessageRequest(perlin_reader* r, PerlinLocalMessageRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->text = perlin_r_str(r);
}
static inline void perlin_write_CommandRegisterRequest(perlin_writer* w, const PerlinCommandRegisterRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
    perlin_w_str(w, v->usage);
    perlin_w_str(w, v->description);
}
static inline void perlin_read_CommandRegisterRequest(perlin_reader* r, PerlinCommandRegisterRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
    v->usage = perlin_r_str(r);
    v->description = perlin_r_str(r);
}
static inline void perlin_write_CommandUnregisterRequest(perlin_writer* w, const PerlinCommandUnregisterRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
}
static inline void perlin_read_CommandUnregisterRequest(perlin_reader* r, PerlinCommandUnregisterRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
}
static inline void perlin_write_ServerCommandsResponse(perlin_writer* w, const PerlinServerCommandsResponse* v) {
    (void)w;
    (void)v;
    perlin_write_List_Str(w, &v->names);
}
static inline void perlin_read_ServerCommandsResponse(perlin_reader* r, PerlinServerCommandsResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_Str(r, &v->names, a);
}
static inline void perlin_write_ToastRequest(perlin_writer* w, const PerlinToastRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->text);
    perlin_w_i32(w, (int32_t)(v->ms));
}
static inline void perlin_read_ToastRequest(perlin_reader* r, PerlinToastRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->text = perlin_r_str(r);
    v->ms = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_TitleRequest(perlin_writer* w, const PerlinTitleRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->title);
    perlin_w_str(w, v->subtitle);
    perlin_w_i32(w, (int32_t)(v->fadeIn));
    perlin_w_i32(w, (int32_t)(v->stay));
    perlin_w_i32(w, (int32_t)(v->fadeOut));
}
static inline void perlin_read_TitleRequest(perlin_reader* r, PerlinTitleRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->title = perlin_r_str(r);
    v->subtitle = perlin_r_str(r);
    v->fadeIn = (int32_t)perlin_r_i32(r);
    v->stay = (int32_t)perlin_r_i32(r);
    v->fadeOut = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_ChatHistoryRequest(perlin_writer* w, const PerlinChatHistoryRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u32(w, (uint32_t)(v->count));
}
static inline void perlin_read_ChatHistoryRequest(perlin_reader* r, PerlinChatHistoryRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->count = (uint32_t)perlin_r_u32(r);
}
static inline void perlin_write_ChatHistoryResponse(perlin_writer* w, const PerlinChatHistoryResponse* v) {
    (void)w;
    (void)v;
    perlin_write_List_ChatLine(w, &v->lines);
}
static inline void perlin_read_ChatHistoryResponse(perlin_reader* r, PerlinChatHistoryResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_ChatLine(r, &v->lines, a);
}
static inline void perlin_write_UseOnBlockRequest(perlin_writer* w, const PerlinUseOnBlockRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_i32(w, (int32_t)(v->face));
}
static inline void perlin_read_UseOnBlockRequest(perlin_reader* r, PerlinUseOnBlockRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->face = (PerlinFace)perlin_r_i32(r);
}
static inline void perlin_write_AttackEntityRequest(perlin_writer* w, const PerlinAttackEntityRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->entity));
}
static inline void perlin_read_AttackEntityRequest(perlin_reader* r, PerlinAttackEntityRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->entity = (uint64_t)perlin_r_u64(r);
}
static inline void perlin_write_InteractEntityRequest(perlin_writer* w, const PerlinInteractEntityRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->entity));
}
static inline void perlin_read_InteractEntityRequest(perlin_reader* r, PerlinInteractEntityRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->entity = (uint64_t)perlin_r_u64(r);
}
static inline void perlin_write_BreakBlockRequest(perlin_writer* w, const PerlinBreakBlockRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_i32(w, (int32_t)(v->face));
}
static inline void perlin_read_BreakBlockRequest(perlin_reader* r, PerlinBreakBlockRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->face = (PerlinFace)perlin_r_i32(r);
}
static inline void perlin_write_LookRequest(perlin_writer* w, const PerlinLookRequest* v) {
    (void)w;
    (void)v;
    perlin_w_f32(w, (float)(v->yaw));
    perlin_w_f32(w, (float)(v->pitch));
}
static inline void perlin_read_LookRequest(perlin_reader* r, PerlinLookRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->yaw = (float)perlin_r_f32(r);
    v->pitch = (float)perlin_r_f32(r);
}
static inline void perlin_write_PlaySoundRequest(perlin_writer* w, const PerlinPlaySoundRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
    perlin_w_u8(w, (uint8_t)(v->positional));
    perlin_w_f32(w, (float)(v->x));
    perlin_w_f32(w, (float)(v->y));
    perlin_w_f32(w, (float)(v->z));
    perlin_w_f32(w, (float)(v->volume));
    perlin_w_f32(w, (float)(v->pitch));
}
static inline void perlin_read_PlaySoundRequest(perlin_reader* r, PerlinPlaySoundRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
    v->positional = (uint8_t)perlin_r_u8(r);
    v->x = (float)perlin_r_f32(r);
    v->y = (float)perlin_r_f32(r);
    v->z = (float)perlin_r_f32(r);
    v->volume = (float)perlin_r_f32(r);
    v->pitch = (float)perlin_r_f32(r);
}
static inline void perlin_write_UiSubmitRequest(perlin_writer* w, const PerlinUiSubmitRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->layer);
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_bytes(w, v->commands);
}
static inline void perlin_read_UiSubmitRequest(perlin_reader* r, PerlinUiSubmitRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->layer = perlin_r_str(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->commands = perlin_r_bytes(r);
}
static inline void perlin_write_UiClearRequest(perlin_writer* w, const PerlinUiClearRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->layer);
}
static inline void perlin_read_UiClearRequest(perlin_reader* r, PerlinUiClearRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->layer = perlin_r_str(r);
}
static inline void perlin_write_UiMeasureRequest(perlin_writer* w, const PerlinUiMeasureRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->text);
    perlin_w_f32(w, (float)(v->scale));
}
static inline void perlin_read_UiMeasureRequest(perlin_reader* r, PerlinUiMeasureRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->text = perlin_r_str(r);
    v->scale = (float)perlin_r_f32(r);
}
static inline void perlin_write_GfxTextureCreateRequest(perlin_writer* w, const PerlinGfxTextureCreateRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->width));
    perlin_w_i32(w, (int32_t)(v->height));
    perlin_w_u8(w, (uint8_t)(v->linear));
    perlin_w_bytes(w, v->rgba);
}
static inline void perlin_read_GfxTextureCreateRequest(perlin_reader* r, PerlinGfxTextureCreateRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->width = (int32_t)perlin_r_i32(r);
    v->height = (int32_t)perlin_r_i32(r);
    v->linear = (uint8_t)perlin_r_u8(r);
    v->rgba = perlin_r_bytes(r);
}
static inline void perlin_write_GfxTextureUpdateRequest(perlin_writer* w, const PerlinGfxTextureUpdateRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u32(w, (uint32_t)(v->id));
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->width));
    perlin_w_i32(w, (int32_t)(v->height));
    perlin_w_bytes(w, v->rgba);
}
static inline void perlin_read_GfxTextureUpdateRequest(perlin_reader* r, PerlinGfxTextureUpdateRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = (uint32_t)perlin_r_u32(r);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->width = (int32_t)perlin_r_i32(r);
    v->height = (int32_t)perlin_r_i32(r);
    v->rgba = perlin_r_bytes(r);
}
static inline void perlin_write_GfxTextureDestroyRequest(perlin_writer* w, const PerlinGfxTextureDestroyRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u32(w, (uint32_t)(v->id));
}
static inline void perlin_read_GfxTextureDestroyRequest(perlin_reader* r, PerlinGfxTextureDestroyRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = (uint32_t)perlin_r_u32(r);
}
static inline void perlin_write_GfxSubmitRequest(perlin_writer* w, const PerlinGfxSubmitRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->layer);
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_bytes(w, v->blob);
}
static inline void perlin_read_GfxSubmitRequest(perlin_reader* r, PerlinGfxSubmitRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->layer = perlin_r_str(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->blob = perlin_r_bytes(r);
}
static inline void perlin_write_GfxClearRequest(perlin_writer* w, const PerlinGfxClearRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->layer);
}
static inline void perlin_read_GfxClearRequest(perlin_reader* r, PerlinGfxClearRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->layer = perlin_r_str(r);
}
static inline void perlin_write_GfxImageLoadRequest(perlin_writer* w, const PerlinGfxImageLoadRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->path);
}
static inline void perlin_read_GfxImageLoadRequest(perlin_reader* r, PerlinGfxImageLoadRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->path = perlin_r_str(r);
}
static inline void perlin_write_GfxImageLoadResponse(perlin_writer* w, const PerlinGfxImageLoadResponse* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->width));
    perlin_w_i32(w, (int32_t)(v->height));
    perlin_w_bytes(w, v->rgba);
}
static inline void perlin_read_GfxImageLoadResponse(perlin_reader* r, PerlinGfxImageLoadResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->width = (int32_t)perlin_r_i32(r);
    v->height = (int32_t)perlin_r_i32(r);
    v->rgba = perlin_r_bytes(r);
}
static inline void perlin_write_InputGrabRequest(perlin_writer* w, const PerlinInputGrabRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u8(w, (uint8_t)(v->mouse));
    perlin_w_u8(w, (uint8_t)(v->keyboard));
}
static inline void perlin_read_InputGrabRequest(perlin_reader* r, PerlinInputGrabRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->mouse = (uint8_t)perlin_r_u8(r);
    v->keyboard = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_StorageReadRequest(perlin_writer* w, const PerlinStorageReadRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->path);
}
static inline void perlin_read_StorageReadRequest(perlin_reader* r, PerlinStorageReadRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->path = perlin_r_str(r);
}
static inline void perlin_write_StorageWriteRequest(perlin_writer* w, const PerlinStorageWriteRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->path);
    perlin_w_bytes(w, v->data);
}
static inline void perlin_read_StorageWriteRequest(perlin_reader* r, PerlinStorageWriteRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->path = perlin_r_str(r);
    v->data = perlin_r_bytes(r);
}
static inline void perlin_write_StorageListRequest(perlin_writer* w, const PerlinStorageListRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->path);
}
static inline void perlin_read_StorageListRequest(perlin_reader* r, PerlinStorageListRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->path = perlin_r_str(r);
}
static inline void perlin_write_StorageListResponse(perlin_writer* w, const PerlinStorageListResponse* v) {
    (void)w;
    (void)v;
    perlin_write_List_Str(w, &v->names);
}
static inline void perlin_read_StorageListResponse(perlin_reader* r, PerlinStorageListResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_Str(r, &v->names, a);
}
static inline void perlin_write_StorageRemoveRequest(perlin_writer* w, const PerlinStorageRemoveRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->path);
}
static inline void perlin_read_StorageRemoveRequest(perlin_reader* r, PerlinStorageRemoveRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->path = perlin_r_str(r);
}
static inline void perlin_write_StorageExistsRequest(perlin_writer* w, const PerlinStorageExistsRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->path);
}
static inline void perlin_read_StorageExistsRequest(perlin_reader* r, PerlinStorageExistsRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->path = perlin_r_str(r);
}
static inline void perlin_write_StorageMakeDirRequest(perlin_writer* w, const PerlinStorageMakeDirRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->path);
}
static inline void perlin_read_StorageMakeDirRequest(perlin_reader* r, PerlinStorageMakeDirRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->path = perlin_r_str(r);
}
static inline void perlin_write_HttpRequestRequest(perlin_writer* w, const PerlinHttpRequestRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u32(w, (uint32_t)(v->id));
    perlin_w_str(w, v->method);
    perlin_w_str(w, v->url);
    perlin_w_i32(w, (int32_t)(v->timeoutMs));
    perlin_write_List_Pair_Str_Str(w, &v->headers);
    perlin_w_bytes(w, v->body);
}
static inline void perlin_read_HttpRequestRequest(perlin_reader* r, PerlinHttpRequestRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = (uint32_t)perlin_r_u32(r);
    v->method = perlin_r_str(r);
    v->url = perlin_r_str(r);
    v->timeoutMs = (int32_t)perlin_r_i32(r);
    perlin_read_List_Pair_Str_Str(r, &v->headers, a);
    v->body = perlin_r_bytes(r);
}
static inline void perlin_write_PacketSendRequest(perlin_writer* w, const PerlinPacketSendRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u32(w, (uint32_t)(v->id));
    perlin_w_bytes(w, v->body);
}
static inline void perlin_read_PacketSendRequest(perlin_reader* r, PerlinPacketSendRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = (uint32_t)perlin_r_u32(r);
    v->body = perlin_r_bytes(r);
}
static inline void perlin_write_PacketNameRequest(perlin_writer* w, const PerlinPacketNameRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u32(w, (uint32_t)(v->id));
}
static inline void perlin_read_PacketNameRequest(perlin_reader* r, PerlinPacketNameRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = (uint32_t)perlin_r_u32(r);
}
static inline void perlin_write_SettingGetRequest(perlin_writer* w, const PerlinSettingGetRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->id);
}
static inline void perlin_read_SettingGetRequest(perlin_reader* r, PerlinSettingGetRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = perlin_r_str(r);
}
static inline void perlin_write_SettingSetRequest(perlin_writer* w, const PerlinSettingSetRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->id);
    perlin_w_str(w, v->value);
}
static inline void perlin_read_SettingSetRequest(perlin_reader* r, PerlinSettingSetRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = perlin_r_str(r);
    v->value = perlin_r_str(r);
}
static inline void perlin_write_SettingListResponse(perlin_writer* w, const PerlinSettingListResponse* v) {
    (void)w;
    (void)v;
    perlin_write_List_SettingEntry(w, &v->settings);
}
static inline void perlin_read_SettingListResponse(perlin_reader* r, PerlinSettingListResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_SettingEntry(r, &v->settings, a);
}
static inline void perlin_write_ModListResponse(perlin_writer* w, const PerlinModListResponse* v) {
    (void)w;
    (void)v;
    perlin_write_List_ModEntry(w, &v->mods);
}
static inline void perlin_read_ModListResponse(perlin_reader* r, PerlinModListResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_ModEntry(r, &v->mods, a);
}
static inline void perlin_write_IconSetRequest(perlin_writer* w, const PerlinIconSetRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->path);
}
static inline void perlin_read_IconSetRequest(perlin_reader* r, PerlinIconSetRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->path = perlin_r_str(r);
}
static inline void perlin_write_OpenScreenRequest(perlin_writer* w, const PerlinOpenScreenRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->screen));
}
static inline void perlin_read_OpenScreenRequest(perlin_reader* r, PerlinOpenScreenRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->screen = (PerlinScreen)perlin_r_i32(r);
}
static inline void perlin_write_OpenModSettingsRequest(perlin_writer* w, const PerlinOpenModSettingsRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->mod);
}
static inline void perlin_read_OpenModSettingsRequest(perlin_reader* r, PerlinOpenModSettingsRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->mod = perlin_r_str(r);
}
static inline void perlin_write_CopyToClipboardRequest(perlin_writer* w, const PerlinCopyToClipboardRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->text);
}
static inline void perlin_read_CopyToClipboardRequest(perlin_reader* r, PerlinCopyToClipboardRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->text = perlin_r_str(r);
}
static inline void perlin_write_OpenUrlRequest(perlin_writer* w, const PerlinOpenUrlRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->url);
}
static inline void perlin_read_OpenUrlRequest(perlin_reader* r, PerlinOpenUrlRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->url = perlin_r_str(r);
}
static inline void perlin_write_CopyFileRequest(perlin_writer* w, const PerlinCopyFileRequest* v) {
    (void)w;
    (void)v;
    perlin_write_FileLocation(w, &v->from);
    perlin_write_FileLocation(w, &v->to);
}
static inline void perlin_read_CopyFileRequest(perlin_reader* r, PerlinCopyFileRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_FileLocation(r, &v->from, a);
    perlin_read_FileLocation(r, &v->to, a);
}
static inline void perlin_write_MoveFileRequest(perlin_writer* w, const PerlinMoveFileRequest* v) {
    (void)w;
    (void)v;
    perlin_write_FileLocation(w, &v->from);
    perlin_write_FileLocation(w, &v->to);
}
static inline void perlin_read_MoveFileRequest(perlin_reader* r, PerlinMoveFileRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_FileLocation(r, &v->from, a);
    perlin_read_FileLocation(r, &v->to, a);
}
static inline void perlin_write_DownloadFileRequest(perlin_writer* w, const PerlinDownloadFileRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u32(w, (uint32_t)(v->id));
    perlin_w_str(w, v->url);
    perlin_write_FileLocation(w, &v->to);
}
static inline void perlin_read_DownloadFileRequest(perlin_reader* r, PerlinDownloadFileRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = (uint32_t)perlin_r_u32(r);
    v->url = perlin_r_str(r);
    perlin_read_FileLocation(r, &v->to, a);
}
static inline void perlin_write_PacketCountersResponse(perlin_writer* w, const PerlinPacketCountersResponse* v) {
    (void)w;
    (void)v;
    perlin_write_List_PacketCount(w, &v->counters);
}
static inline void perlin_read_PacketCountersResponse(perlin_reader* r, PerlinPacketCountersResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_PacketCount(r, &v->counters, a);
}
static inline void perlin_write_PacketInjectRequest(perlin_writer* w, const PerlinPacketInjectRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u32(w, (uint32_t)(v->id));
    perlin_w_bytes(w, v->body);
}
static inline void perlin_read_PacketInjectRequest(perlin_reader* r, PerlinPacketInjectRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = (uint32_t)perlin_r_u32(r);
    v->body = perlin_r_bytes(r);
}
static inline void perlin_write_WorldMeshClearRequest(perlin_writer* w, const PerlinWorldMeshClearRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->id);
}
static inline void perlin_read_WorldMeshClearRequest(perlin_reader* r, PerlinWorldMeshClearRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = perlin_r_str(r);
}
static inline void perlin_write_ScreenButtonRemoveRequest(perlin_writer* w, const PerlinScreenButtonRemoveRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->id);
}
static inline void perlin_read_ScreenButtonRemoveRequest(perlin_reader* r, PerlinScreenButtonRemoveRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = perlin_r_str(r);
}
static inline void perlin_write_ScreenOpenCustomRequest(perlin_writer* w, const PerlinScreenOpenCustomRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->id);
}
static inline void perlin_read_ScreenOpenCustomRequest(perlin_reader* r, PerlinScreenOpenCustomRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = perlin_r_str(r);
}
static inline void perlin_write_TooltipAppendRequest(perlin_writer* w, const PerlinTooltipAppendRequest* v) {
    (void)w;
    (void)v;
    perlin_write_List_Str(w, &v->lines);
}
static inline void perlin_read_TooltipAppendRequest(perlin_reader* r, PerlinTooltipAppendRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_Str(r, &v->lines, a);
}
static inline void perlin_write_ShaderPackSetRequest(perlin_writer* w, const PerlinShaderPackSetRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
}
static inline void perlin_read_ShaderPackSetRequest(perlin_reader* r, PerlinShaderPackSetRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
}
static inline void perlin_write_ServerItem(perlin_writer* w, const PerlinServerItem* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
    perlin_w_u32(w, (uint32_t)(v->count));
    perlin_w_u32(w, (uint32_t)(v->damage));
}
static inline void perlin_read_ServerItem(perlin_reader* r, PerlinServerItem* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
    v->count = (uint32_t)perlin_r_u32(r);
    v->damage = (uint32_t)perlin_r_u32(r);
}
static inline void perlin_write_ServerInventory(perlin_writer* w, const PerlinServerInventory* v) {
    (void)w;
    (void)v;
    perlin_write_List_ServerItem(w, &v->main);
    perlin_write_List_ServerItem(w, &v->armor);
    perlin_write_ServerItem(w, &v->offhand);
    perlin_w_i32(w, (int32_t)(v->selected));
}
static inline void perlin_read_ServerInventory(perlin_reader* r, PerlinServerInventory* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_ServerItem(r, &v->main, a);
    perlin_read_List_ServerItem(r, &v->armor, a);
    perlin_read_ServerItem(r, &v->offhand, a);
    v->selected = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_PlayerDetails(perlin_writer* w, const PerlinPlayerDetails* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->id));
    perlin_w_str(w, v->name);
    perlin_w_str(w, v->xuid);
    perlin_w_str(w, v->uuid);
    perlin_w_i32(w, (int32_t)(v->dimension));
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_f32(w, (float)(v->yaw));
    perlin_w_f32(w, (float)(v->pitch));
    perlin_w_i32(w, (int32_t)(v->gameMode));
    perlin_w_f32(w, (float)(v->health));
    perlin_w_f32(w, (float)(v->maxHealth));
    perlin_w_f32(w, (float)(v->absorption));
    perlin_w_i32(w, (int32_t)(v->food));
    perlin_w_f32(w, (float)(v->saturation));
    perlin_w_i32(w, (int32_t)(v->xpLevel));
    perlin_w_f32(w, (float)(v->xpProgress));
    perlin_w_u8(w, (uint8_t)(v->onGround));
    perlin_w_u8(w, (uint8_t)(v->sneaking));
    perlin_w_u8(w, (uint8_t)(v->sprinting));
    perlin_w_u8(w, (uint8_t)(v->dead));
    perlin_w_u8(w, (uint8_t)(v->isOperator));
    perlin_write_List_Effect(w, &v->effects);
}
static inline void perlin_read_PlayerDetails(perlin_reader* r, PerlinPlayerDetails* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = (uint64_t)perlin_r_u64(r);
    v->name = perlin_r_str(r);
    v->xuid = perlin_r_str(r);
    v->uuid = perlin_r_str(r);
    v->dimension = (PerlinDimension)perlin_r_i32(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->yaw = (float)perlin_r_f32(r);
    v->pitch = (float)perlin_r_f32(r);
    v->gameMode = (PerlinGameMode)perlin_r_i32(r);
    v->health = (float)perlin_r_f32(r);
    v->maxHealth = (float)perlin_r_f32(r);
    v->absorption = (float)perlin_r_f32(r);
    v->food = (int32_t)perlin_r_i32(r);
    v->saturation = (float)perlin_r_f32(r);
    v->xpLevel = (int32_t)perlin_r_i32(r);
    v->xpProgress = (float)perlin_r_f32(r);
    v->onGround = (uint8_t)perlin_r_u8(r);
    v->sneaking = (uint8_t)perlin_r_u8(r);
    v->sprinting = (uint8_t)perlin_r_u8(r);
    v->dead = (uint8_t)perlin_r_u8(r);
    v->isOperator = (uint8_t)perlin_r_u8(r);
    perlin_read_List_Effect(r, &v->effects, a);
}
static inline void perlin_write_ServerEntity(perlin_writer* w, const PerlinServerEntity* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->id));
    perlin_w_str(w, v->type);
    perlin_w_i32(w, (int32_t)(v->dimension));
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_f32(w, (float)(v->yaw));
    perlin_w_f32(w, (float)(v->health));
    perlin_w_f32(w, (float)(v->maxHealth));
    perlin_w_u8(w, (uint8_t)(v->baby));
}
static inline void perlin_read_ServerEntity(perlin_reader* r, PerlinServerEntity* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->id = (uint64_t)perlin_r_u64(r);
    v->type = perlin_r_str(r);
    v->dimension = (PerlinDimension)perlin_r_i32(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->yaw = (float)perlin_r_f32(r);
    v->health = (float)perlin_r_f32(r);
    v->maxHealth = (float)perlin_r_f32(r);
    v->baby = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_FormButton(perlin_writer* w, const PerlinFormButton* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->text);
    perlin_w_str(w, v->image);
}
static inline void perlin_read_FormButton(perlin_reader* r, PerlinFormButton* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->text = perlin_r_str(r);
    v->image = perlin_r_str(r);
}
static inline void perlin_write_FormControl(perlin_writer* w, const PerlinFormControl* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->kind));
    perlin_w_str(w, v->text);
    perlin_write_List_Str(w, &v->options);
    perlin_w_f32(w, (float)(v->min));
    perlin_w_f32(w, (float)(v->max));
    perlin_w_f32(w, (float)(v->step));
    perlin_w_f32(w, (float)(v->defaultNumber));
    perlin_w_str(w, v->defaultText);
    perlin_w_str(w, v->placeholder);
}
static inline void perlin_read_FormControl(perlin_reader* r, PerlinFormControl* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->kind = (PerlinFormElement)perlin_r_i32(r);
    v->text = perlin_r_str(r);
    perlin_read_List_Str(r, &v->options, a);
    v->min = (float)perlin_r_f32(r);
    v->max = (float)perlin_r_f32(r);
    v->step = (float)perlin_r_f32(r);
    v->defaultNumber = (float)perlin_r_f32(r);
    v->defaultText = perlin_r_str(r);
    v->placeholder = perlin_r_str(r);
}
static inline void perlin_write_Form(perlin_writer* w, const PerlinForm* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->kind));
    perlin_w_str(w, v->title);
    perlin_w_str(w, v->body);
    perlin_write_List_FormButton(w, &v->buttons);
    perlin_write_List_FormControl(w, &v->controls);
}
static inline void perlin_read_Form(perlin_reader* r, PerlinForm* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->kind = (PerlinFormKind)perlin_r_i32(r);
    v->title = perlin_r_str(r);
    v->body = perlin_r_str(r);
    perlin_read_List_FormButton(r, &v->buttons, a);
    perlin_read_List_FormControl(r, &v->controls, a);
}
static inline void perlin_write_EventServerDamage(perlin_writer* w, const PerlinEventServerDamage* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_f32(w, (float)(v->amount));
    perlin_w_i32(w, (int32_t)(v->cause));
    perlin_w_u64(w, (uint64_t)(v->attacker));
    perlin_w_str(w, v->attackerType);
}
static inline void perlin_read_EventServerDamage(perlin_reader* r, PerlinEventServerDamage* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->amount = (float)perlin_r_f32(r);
    v->cause = (PerlinDamageCause)perlin_r_i32(r);
    v->attacker = (uint64_t)perlin_r_u64(r);
    v->attackerType = perlin_r_str(r);
}
static inline void perlin_write_EventServerDeath(perlin_writer* w, const PerlinEventServerDeath* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_i32(w, (int32_t)(v->cause));
    perlin_w_u64(w, (uint64_t)(v->attacker));
    perlin_w_str(w, v->message);
}
static inline void perlin_read_EventServerDeath(perlin_reader* r, PerlinEventServerDeath* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->cause = (PerlinDamageCause)perlin_r_i32(r);
    v->attacker = (uint64_t)perlin_r_u64(r);
    v->message = perlin_r_str(r);
}
static inline void perlin_write_EventPlayerUse(perlin_writer* w, const PerlinEventPlayerUse* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_i32(w, (int32_t)(v->face));
    perlin_w_str(w, v->block);
    perlin_w_str(w, v->item);
}
static inline void perlin_read_EventPlayerUse(perlin_reader* r, PerlinEventPlayerUse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->face = (PerlinFace)perlin_r_i32(r);
    v->block = perlin_r_str(r);
    v->item = perlin_r_str(r);
}
static inline void perlin_write_EventPlayerAttack(perlin_writer* w, const PerlinEventPlayerAttack* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_u64(w, (uint64_t)(v->target));
    perlin_w_str(w, v->targetType);
    perlin_w_f32(w, (float)(v->damage));
}
static inline void perlin_read_EventPlayerAttack(perlin_reader* r, PerlinEventPlayerAttack* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->target = (uint64_t)perlin_r_u64(r);
    v->targetType = perlin_r_str(r);
    v->damage = (float)perlin_r_f32(r);
}
static inline void perlin_write_EventEntityDamage(perlin_writer* w, const PerlinEventEntityDamage* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->entity));
    perlin_w_str(w, v->type);
    perlin_w_f32(w, (float)(v->amount));
    perlin_w_i32(w, (int32_t)(v->cause));
    perlin_w_u64(w, (uint64_t)(v->attacker));
}
static inline void perlin_read_EventEntityDamage(perlin_reader* r, PerlinEventEntityDamage* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->entity = (uint64_t)perlin_r_u64(r);
    v->type = perlin_r_str(r);
    v->amount = (float)perlin_r_f32(r);
    v->cause = (PerlinDamageCause)perlin_r_i32(r);
    v->attacker = (uint64_t)perlin_r_u64(r);
}
static inline void perlin_write_EventEntityDeath(perlin_writer* w, const PerlinEventEntityDeath* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->entity));
    perlin_w_str(w, v->type);
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_u64(w, (uint64_t)(v->killer));
}
static inline void perlin_read_EventEntityDeath(perlin_reader* r, PerlinEventEntityDeath* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->entity = (uint64_t)perlin_r_u64(r);
    v->type = perlin_r_str(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->killer = (uint64_t)perlin_r_u64(r);
}
static inline void perlin_write_EventEntitySpawn(perlin_writer* w, const PerlinEventEntitySpawn* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->entity));
    perlin_w_str(w, v->type);
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_u8(w, (uint8_t)(v->natural));
}
static inline void perlin_read_EventEntitySpawn(perlin_reader* r, PerlinEventEntitySpawn* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->entity = (uint64_t)perlin_r_u64(r);
    v->type = perlin_r_str(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->natural = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_EventItem(perlin_writer* w, const PerlinEventItem* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_str(w, v->item);
    perlin_w_u32(w, (uint32_t)(v->count));
}
static inline void perlin_read_EventItem(perlin_reader* r, PerlinEventItem* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->item = perlin_r_str(r);
    v->count = (uint32_t)perlin_r_u32(r);
}
static inline void perlin_write_EventGameModeChange(perlin_writer* w, const PerlinEventGameModeChange* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_i32(w, (int32_t)(v->from));
    perlin_w_i32(w, (int32_t)(v->to));
}
static inline void perlin_read_EventGameModeChange(perlin_reader* r, PerlinEventGameModeChange* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->from = (PerlinGameMode)perlin_r_i32(r);
    v->to = (PerlinGameMode)perlin_r_i32(r);
}
static inline void perlin_write_EventFormResponse(perlin_writer* w, const PerlinEventFormResponse* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_u32(w, (uint32_t)(v->form));
    perlin_w_u8(w, (uint8_t)(v->closed));
    perlin_w_i32(w, (int32_t)(v->button));
    perlin_write_List_Str(w, &v->values);
}
static inline void perlin_read_EventFormResponse(perlin_reader* r, PerlinEventFormResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->form = (uint32_t)perlin_r_u32(r);
    v->closed = (uint8_t)perlin_r_u8(r);
    v->button = (int32_t)perlin_r_i32(r);
    perlin_read_List_Str(r, &v->values, a);
}
static inline void perlin_write_EventPlayerMove(perlin_writer* w, const PerlinEventPlayerMove* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_i32(w, (int32_t)(v->dimension));
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_f32(w, (float)(v->yaw));
    perlin_w_f32(w, (float)(v->pitch));
    perlin_w_u8(w, (uint8_t)(v->onGround));
}
static inline void perlin_read_EventPlayerMove(perlin_reader* r, PerlinEventPlayerMove* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->dimension = (PerlinDimension)perlin_r_i32(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->yaw = (float)perlin_r_f32(r);
    v->pitch = (float)perlin_r_f32(r);
    v->onGround = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_EventExplosion(perlin_writer* w, const PerlinEventExplosion* v) {
    (void)w;
    (void)v;
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_f32(w, (float)(v->power));
    perlin_w_u64(w, (uint64_t)(v->source));
    perlin_w_str(w, v->sourceType);
}
static inline void perlin_read_EventExplosion(perlin_reader* r, PerlinEventExplosion* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->power = (float)perlin_r_f32(r);
    v->source = (uint64_t)perlin_r_u64(r);
    v->sourceType = perlin_r_str(r);
}
static inline void perlin_write_EventContainer(perlin_writer* w, const PerlinEventContainer* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_str(w, v->block);
}
static inline void perlin_read_EventContainer(perlin_reader* r, PerlinEventContainer* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->block = perlin_r_str(r);
}
static inline void perlin_write_EventPlayerDimension(perlin_writer* w, const PerlinEventPlayerDimension* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_i32(w, (int32_t)(v->from));
    perlin_w_i32(w, (int32_t)(v->to));
}
static inline void perlin_read_EventPlayerDimension(perlin_reader* r, PerlinEventPlayerDimension* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->from = (PerlinDimension)perlin_r_i32(r);
    v->to = (PerlinDimension)perlin_r_i32(r);
}
static inline void perlin_write_EventServerCommand(perlin_writer* w, const PerlinEventServerCommand* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_str(w, v->line);
}
static inline void perlin_read_EventServerCommand(perlin_reader* r, PerlinEventServerCommand* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->line = perlin_r_str(r);
}
static inline void perlin_write_EventPreLogin(perlin_writer* w, const PerlinEventPreLogin* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
    perlin_w_str(w, v->xuid);
    perlin_w_str(w, v->address);
}
static inline void perlin_read_EventPreLogin(perlin_reader* r, PerlinEventPreLogin* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
    v->xuid = perlin_r_str(r);
    v->address = perlin_r_str(r);
}
static inline void perlin_write_PlayerInfoRequest(perlin_writer* w, const PerlinPlayerInfoRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
}
static inline void perlin_read_PlayerInfoRequest(perlin_reader* r, PerlinPlayerInfoRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
}
static inline void perlin_write_PlayerSetGameModeRequest(perlin_writer* w, const PerlinPlayerSetGameModeRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_i32(w, (int32_t)(v->mode));
}
static inline void perlin_read_PlayerSetGameModeRequest(perlin_reader* r, PerlinPlayerSetGameModeRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->mode = (PerlinGameMode)perlin_r_i32(r);
}
static inline void perlin_write_PlayerSetHealthRequest(perlin_writer* w, const PerlinPlayerSetHealthRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_f32(w, (float)(v->health));
}
static inline void perlin_read_PlayerSetHealthRequest(perlin_reader* r, PerlinPlayerSetHealthRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->health = (float)perlin_r_f32(r);
}
static inline void perlin_write_PlayerDamageRequest(perlin_writer* w, const PerlinPlayerDamageRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_f32(w, (float)(v->amount));
    perlin_w_i32(w, (int32_t)(v->cause));
    perlin_w_u8(w, (uint8_t)(v->knockback));
}
static inline void perlin_read_PlayerDamageRequest(perlin_reader* r, PerlinPlayerDamageRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->amount = (float)perlin_r_f32(r);
    v->cause = (PerlinDamageCause)perlin_r_i32(r);
    v->knockback = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_PlayerKillRequest(perlin_writer* w, const PerlinPlayerKillRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
}
static inline void perlin_read_PlayerKillRequest(perlin_reader* r, PerlinPlayerKillRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
}
static inline void perlin_write_PlayerSetFoodRequest(perlin_writer* w, const PerlinPlayerSetFoodRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_i32(w, (int32_t)(v->food));
    perlin_w_f32(w, (float)(v->saturation));
}
static inline void perlin_read_PlayerSetFoodRequest(perlin_reader* r, PerlinPlayerSetFoodRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->food = (int32_t)perlin_r_i32(r);
    v->saturation = (float)perlin_r_f32(r);
}
static inline void perlin_write_PlayerAddExperienceRequest(perlin_writer* w, const PerlinPlayerAddExperienceRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_i32(w, (int32_t)(v->points));
    perlin_w_i32(w, (int32_t)(v->levels));
}
static inline void perlin_read_PlayerAddExperienceRequest(perlin_reader* r, PerlinPlayerAddExperienceRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->points = (int32_t)perlin_r_i32(r);
    v->levels = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_PlayerEffectAddRequest(perlin_writer* w, const PerlinPlayerEffectAddRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_i32(w, (int32_t)(v->effect));
    perlin_w_i32(w, (int32_t)(v->amplifier));
    perlin_w_i32(w, (int32_t)(v->ticks));
    perlin_w_u8(w, (uint8_t)(v->particles));
}
static inline void perlin_read_PlayerEffectAddRequest(perlin_reader* r, PerlinPlayerEffectAddRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->effect = (PerlinEffectId)perlin_r_i32(r);
    v->amplifier = (int32_t)perlin_r_i32(r);
    v->ticks = (int32_t)perlin_r_i32(r);
    v->particles = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_PlayerEffectRemoveRequest(perlin_writer* w, const PerlinPlayerEffectRemoveRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_i32(w, (int32_t)(v->effect));
}
static inline void perlin_read_PlayerEffectRemoveRequest(perlin_reader* r, PerlinPlayerEffectRemoveRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->effect = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_PlayerInventoryRequest(perlin_writer* w, const PerlinPlayerInventoryRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
}
static inline void perlin_read_PlayerInventoryRequest(perlin_reader* r, PerlinPlayerInventoryRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
}
static inline void perlin_write_PlayerSetSlotRequest(perlin_writer* w, const PerlinPlayerSetSlotRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_i32(w, (int32_t)(v->container));
    perlin_w_i32(w, (int32_t)(v->slot));
    perlin_write_ServerItem(w, &v->item);
}
static inline void perlin_read_PlayerSetSlotRequest(perlin_reader* r, PerlinPlayerSetSlotRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->container = (int32_t)perlin_r_i32(r);
    v->slot = (int32_t)perlin_r_i32(r);
    perlin_read_ServerItem(r, &v->item, a);
}
static inline void perlin_write_PlayerClearInventoryRequest(perlin_writer* w, const PerlinPlayerClearInventoryRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
}
static inline void perlin_read_PlayerClearInventoryRequest(perlin_reader* r, PerlinPlayerClearInventoryRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
}
static inline void perlin_write_PlayerSetVelocityRequest(perlin_writer* w, const PerlinPlayerSetVelocityRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
}
static inline void perlin_read_PlayerSetVelocityRequest(perlin_reader* r, PerlinPlayerSetVelocityRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
}
static inline void perlin_write_PlayerTransferDimensionRequest(perlin_writer* w, const PerlinPlayerTransferDimensionRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_i32(w, (int32_t)(v->dimension));
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
}
static inline void perlin_read_PlayerTransferDimensionRequest(perlin_reader* r, PerlinPlayerTransferDimensionRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->dimension = (PerlinDimension)perlin_r_i32(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
}
static inline void perlin_write_PlayerTitleRequest(perlin_writer* w, const PerlinPlayerTitleRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_str(w, v->title);
    perlin_w_str(w, v->subtitle);
    perlin_w_i32(w, (int32_t)(v->fadeIn));
    perlin_w_i32(w, (int32_t)(v->stay));
    perlin_w_i32(w, (int32_t)(v->fadeOut));
}
static inline void perlin_read_PlayerTitleRequest(perlin_reader* r, PerlinPlayerTitleRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->title = perlin_r_str(r);
    v->subtitle = perlin_r_str(r);
    v->fadeIn = (int32_t)perlin_r_i32(r);
    v->stay = (int32_t)perlin_r_i32(r);
    v->fadeOut = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_PlayerActionBarRequest(perlin_writer* w, const PerlinPlayerActionBarRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_str(w, v->text);
}
static inline void perlin_read_PlayerActionBarRequest(perlin_reader* r, PerlinPlayerActionBarRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->text = perlin_r_str(r);
}
static inline void perlin_write_PlayerSoundRequest(perlin_writer* w, const PerlinPlayerSoundRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_str(w, v->sound);
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_f32(w, (float)(v->volume));
    perlin_w_f32(w, (float)(v->pitch));
}
static inline void perlin_read_PlayerSoundRequest(perlin_reader* r, PerlinPlayerSoundRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->sound = perlin_r_str(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->volume = (float)perlin_r_f32(r);
    v->pitch = (float)perlin_r_f32(r);
}
static inline void perlin_write_PlayerTransferRequest(perlin_writer* w, const PerlinPlayerTransferRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_str(w, v->host);
    perlin_w_u16(w, (uint16_t)(v->port));
}
static inline void perlin_read_PlayerTransferRequest(perlin_reader* r, PerlinPlayerTransferRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->host = perlin_r_str(r);
    v->port = (uint16_t)perlin_r_u16(r);
}
static inline void perlin_write_PlayerSetOperatorRequest(perlin_writer* w, const PerlinPlayerSetOperatorRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_u8(w, (uint8_t)(v->isOperator));
}
static inline void perlin_read_PlayerSetOperatorRequest(perlin_reader* r, PerlinPlayerSetOperatorRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->isOperator = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_PlayerFormRequest(perlin_writer* w, const PerlinPlayerFormRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_write_Form(w, &v->form);
}
static inline void perlin_read_PlayerFormRequest(perlin_reader* r, PerlinPlayerFormRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    perlin_read_Form(r, &v->form, a);
}
static inline void perlin_write_PlayerRunCommandRequest(perlin_writer* w, const PerlinPlayerRunCommandRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_str(w, v->line);
}
static inline void perlin_read_PlayerRunCommandRequest(perlin_reader* r, PerlinPlayerRunCommandRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->line = perlin_r_str(r);
}
static inline void perlin_write_PlayerSetSpawnRequest(perlin_writer* w, const PerlinPlayerSetSpawnRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->player));
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
}
static inline void perlin_read_PlayerSetSpawnRequest(perlin_reader* r, PerlinPlayerSetSpawnRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->player = (uint64_t)perlin_r_u64(r);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_PlayerByNameRequest(perlin_writer* w, const PerlinPlayerByNameRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
}
static inline void perlin_read_PlayerByNameRequest(perlin_reader* r, PerlinPlayerByNameRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
}
static inline void perlin_write_BlockStateRequest(perlin_writer* w, const PerlinBlockStateRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->dimension));
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
}
static inline void perlin_read_BlockStateRequest(perlin_reader* r, PerlinBlockStateRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->dimension = (PerlinDimension)perlin_r_i32(r);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_BlockStateSetRequest(perlin_writer* w, const PerlinBlockStateSetRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->dimension));
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->y));
    perlin_w_i32(w, (int32_t)(v->z));
    perlin_w_str(w, v->name);
    perlin_write_List_Pair_Str_Str(w, &v->states);
}
static inline void perlin_read_BlockStateSetRequest(perlin_reader* r, PerlinBlockStateSetRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->dimension = (PerlinDimension)perlin_r_i32(r);
    v->x = (int32_t)perlin_r_i32(r);
    v->y = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
    v->name = perlin_r_str(r);
    perlin_read_List_Pair_Str_Str(r, &v->states, a);
}
static inline void perlin_write_FillRequest(perlin_writer* w, const PerlinFillRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->dimension));
    perlin_w_i32(w, (int32_t)(v->x0));
    perlin_w_i32(w, (int32_t)(v->y0));
    perlin_w_i32(w, (int32_t)(v->z0));
    perlin_w_i32(w, (int32_t)(v->x1));
    perlin_w_i32(w, (int32_t)(v->y1));
    perlin_w_i32(w, (int32_t)(v->z1));
    perlin_w_str(w, v->block);
}
static inline void perlin_read_FillRequest(perlin_reader* r, PerlinFillRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->dimension = (PerlinDimension)perlin_r_i32(r);
    v->x0 = (int32_t)perlin_r_i32(r);
    v->y0 = (int32_t)perlin_r_i32(r);
    v->z0 = (int32_t)perlin_r_i32(r);
    v->x1 = (int32_t)perlin_r_i32(r);
    v->y1 = (int32_t)perlin_r_i32(r);
    v->z1 = (int32_t)perlin_r_i32(r);
    v->block = perlin_r_str(r);
}
static inline void perlin_write_TimeGetResponse(perlin_writer* w, const PerlinTimeGetResponse* v) {
    (void)w;
    (void)v;
    perlin_w_i64(w, (int64_t)(v->time));
    perlin_w_i64(w, (int64_t)(v->day));
    perlin_w_i32(w, (int32_t)(v->timeOfDay));
}
static inline void perlin_read_TimeGetResponse(perlin_reader* r, PerlinTimeGetResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->time = (int64_t)perlin_r_i64(r);
    v->day = (int64_t)perlin_r_i64(r);
    v->timeOfDay = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_TimeSetRequest(perlin_writer* w, const PerlinTimeSetRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i64(w, (int64_t)(v->time));
}
static inline void perlin_read_TimeSetRequest(perlin_reader* r, PerlinTimeSetRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->time = (int64_t)perlin_r_i64(r);
}
static inline void perlin_write_WeatherSetRequest(perlin_writer* w, const PerlinWeatherSetRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->weather));
    perlin_w_i32(w, (int32_t)(v->ticks));
}
static inline void perlin_read_WeatherSetRequest(perlin_reader* r, PerlinWeatherSetRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->weather = (PerlinWeather)perlin_r_i32(r);
    v->ticks = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_GameRuleGetRequest(perlin_writer* w, const PerlinGameRuleGetRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
}
static inline void perlin_read_GameRuleGetRequest(perlin_reader* r, PerlinGameRuleGetRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
}
static inline void perlin_write_GameRuleSetRequest(perlin_writer* w, const PerlinGameRuleSetRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->name);
    perlin_w_i32(w, (int32_t)(v->value));
}
static inline void perlin_read_GameRuleSetRequest(perlin_reader* r, PerlinGameRuleSetRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->name = perlin_r_str(r);
    v->value = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_DifficultySetRequest(perlin_writer* w, const PerlinDifficultySetRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->difficulty));
}
static inline void perlin_read_DifficultySetRequest(perlin_reader* r, PerlinDifficultySetRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->difficulty = (PerlinDifficulty)perlin_r_i32(r);
}
static inline void perlin_write_ExplodeRequest(perlin_writer* w, const PerlinExplodeRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->dimension));
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_f32(w, (float)(v->power));
    perlin_w_u8(w, (uint8_t)(v->breakBlocks));
}
static inline void perlin_read_ExplodeRequest(perlin_reader* r, PerlinExplodeRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->dimension = (PerlinDimension)perlin_r_i32(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->power = (float)perlin_r_f32(r);
    v->breakBlocks = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_HighestBlockRequest(perlin_writer* w, const PerlinHighestBlockRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->dimension));
    perlin_w_i32(w, (int32_t)(v->x));
    perlin_w_i32(w, (int32_t)(v->z));
}
static inline void perlin_read_HighestBlockRequest(perlin_reader* r, PerlinHighestBlockRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->dimension = (PerlinDimension)perlin_r_i32(r);
    v->x = (int32_t)perlin_r_i32(r);
    v->z = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_ParticleAtRequest(perlin_writer* w, const PerlinParticleAtRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->dimension));
    perlin_w_str(w, v->name);
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
}
static inline void perlin_read_ParticleAtRequest(perlin_reader* r, PerlinParticleAtRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->dimension = (PerlinDimension)perlin_r_i32(r);
    v->name = perlin_r_str(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
}
static inline void perlin_write_DropItemRequest(perlin_writer* w, const PerlinDropItemRequest* v) {
    (void)w;
    (void)v;
    perlin_w_i32(w, (int32_t)(v->dimension));
    perlin_write_ServerItem(w, &v->item);
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
}
static inline void perlin_read_DropItemRequest(perlin_reader* r, PerlinDropItemRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->dimension = (PerlinDimension)perlin_r_i32(r);
    perlin_read_ServerItem(r, &v->item, a);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
}
static inline void perlin_write_EntitySpawnRequest(perlin_writer* w, const PerlinEntitySpawnRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->type);
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_u8(w, (uint8_t)(v->baby));
}
static inline void perlin_read_EntitySpawnRequest(perlin_reader* r, PerlinEntitySpawnRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->type = perlin_r_str(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->baby = (uint8_t)perlin_r_u8(r);
}
static inline void perlin_write_EntityListRequest(perlin_writer* w, const PerlinEntityListRequest* v) {
    (void)w;
    (void)v;
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
    perlin_w_f64(w, (double)(v->radius));
    perlin_w_str(w, v->type);
}
static inline void perlin_read_EntityListRequest(perlin_reader* r, PerlinEntityListRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
    v->radius = (double)perlin_r_f64(r);
    v->type = perlin_r_str(r);
}
static inline void perlin_write_EntityListResponse(perlin_writer* w, const PerlinEntityListResponse* v) {
    (void)w;
    (void)v;
    perlin_write_List_ServerEntity(w, &v->entities);
}
static inline void perlin_read_EntityListResponse(perlin_reader* r, PerlinEntityListResponse* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    perlin_read_List_ServerEntity(r, &v->entities, a);
}
static inline void perlin_write_EntityRemoveRequest(perlin_writer* w, const PerlinEntityRemoveRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->entity));
}
static inline void perlin_read_EntityRemoveRequest(perlin_reader* r, PerlinEntityRemoveRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->entity = (uint64_t)perlin_r_u64(r);
}
static inline void perlin_write_EntityDamageRequest(perlin_writer* w, const PerlinEntityDamageRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->entity));
    perlin_w_f32(w, (float)(v->amount));
}
static inline void perlin_read_EntityDamageRequest(perlin_reader* r, PerlinEntityDamageRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->entity = (uint64_t)perlin_r_u64(r);
    v->amount = (float)perlin_r_f32(r);
}
static inline void perlin_write_EntityTeleportRequest(perlin_writer* w, const PerlinEntityTeleportRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->entity));
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
}
static inline void perlin_read_EntityTeleportRequest(perlin_reader* r, PerlinEntityTeleportRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->entity = (uint64_t)perlin_r_u64(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
}
static inline void perlin_write_EntitySetVelocityRequest(perlin_writer* w, const PerlinEntitySetVelocityRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->entity));
    perlin_w_f64(w, (double)(v->x));
    perlin_w_f64(w, (double)(v->y));
    perlin_w_f64(w, (double)(v->z));
}
static inline void perlin_read_EntitySetVelocityRequest(perlin_reader* r, PerlinEntitySetVelocityRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->entity = (uint64_t)perlin_r_u64(r);
    v->x = (double)perlin_r_f64(r);
    v->y = (double)perlin_r_f64(r);
    v->z = (double)perlin_r_f64(r);
}
static inline void perlin_write_EntityInfoRequest(perlin_writer* w, const PerlinEntityInfoRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->entity));
}
static inline void perlin_read_EntityInfoRequest(perlin_reader* r, PerlinEntityInfoRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->entity = (uint64_t)perlin_r_u64(r);
}
static inline void perlin_write_EntitySetHealthRequest(perlin_writer* w, const PerlinEntitySetHealthRequest* v) {
    (void)w;
    (void)v;
    perlin_w_u64(w, (uint64_t)(v->entity));
    perlin_w_f32(w, (float)(v->health));
}
static inline void perlin_read_EntitySetHealthRequest(perlin_reader* r, PerlinEntitySetHealthRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->entity = (uint64_t)perlin_r_u64(r);
    v->health = (float)perlin_r_f32(r);
}
static inline void perlin_write_RunCommandRequest(perlin_writer* w, const PerlinRunCommandRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->line);
}
static inline void perlin_read_RunCommandRequest(perlin_reader* r, PerlinRunCommandRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->line = perlin_r_str(r);
}
static inline void perlin_write_MotdSetRequest(perlin_writer* w, const PerlinMotdSetRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->motd);
}
static inline void perlin_read_MotdSetRequest(perlin_reader* r, PerlinMotdSetRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->motd = perlin_r_str(r);
}
static inline void perlin_write_LootTableOverrideRequest(perlin_writer* w, const PerlinLootTableOverrideRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->key);
    perlin_w_str(w, v->json);
}
static inline void perlin_read_LootTableOverrideRequest(perlin_reader* r, PerlinLootTableOverrideRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->key = perlin_r_str(r);
    v->json = perlin_r_str(r);
}
static inline void perlin_write_LootTableGetRequest(perlin_writer* w, const PerlinLootTableGetRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->key);
}
static inline void perlin_read_LootTableGetRequest(perlin_reader* r, PerlinLootTableGetRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->key = perlin_r_str(r);
}
static inline void perlin_write_EventAmendRequest(perlin_writer* w, const PerlinEventAmendRequest* v) {
    (void)w;
    (void)v;
    perlin_w_str(w, v->field);
    perlin_w_f64(w, (double)(v->number));
    perlin_w_str(w, v->text);
}
static inline void perlin_read_EventAmendRequest(perlin_reader* r, PerlinEventAmendRequest* v, perlin_arena* a) {
    (void)r;
    (void)a;
    __builtin_memset(v, 0, sizeof *v);
    v->field = perlin_r_str(r);
    v->number = (double)perlin_r_f64(r);
    v->text = perlin_r_str(r);
}
static inline void perlin_write_Pair_I32_I32(perlin_writer* w, const PerlinPair_I32_I32* v) {
    perlin_w_i32(w, (int32_t)(v->first));
    perlin_w_i32(w, (int32_t)(v->second));
}
static inline void perlin_read_Pair_I32_I32(perlin_reader* r, PerlinPair_I32_I32* v, perlin_arena* a) {
    (void)a;
    v->first = (int32_t)perlin_r_i32(r);
    v->second = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_List_Pair_I32_I32(perlin_writer* w, const PerlinList_Pair_I32_I32* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_Pair_I32_I32(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_Pair_I32_I32(perlin_reader* r, PerlinList_Pair_I32_I32* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinPair_I32_I32*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_Pair_I32_I32(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_Str(perlin_writer* w, const PerlinList_Str* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_w_str(w, v->data[k_]);
    }
}
static inline void perlin_read_List_Str(perlin_reader* r, PerlinList_Str* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (perlin_str*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        v->data[k_] = perlin_r_str(r);
    }
}
static inline void perlin_write_List_Attribute(perlin_writer* w, const PerlinList_Attribute* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_Attribute(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_Attribute(perlin_reader* r, PerlinList_Attribute* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinAttribute*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_Attribute(r, &v->data[k_], a);
    }
}
static inline void perlin_write_Pair_Str_F32(perlin_writer* w, const PerlinPair_Str_F32* v) {
    perlin_w_str(w, v->first);
    perlin_w_f32(w, (float)(v->second));
}
static inline void perlin_read_Pair_Str_F32(perlin_reader* r, PerlinPair_Str_F32* v, perlin_arena* a) {
    (void)a;
    v->first = perlin_r_str(r);
    v->second = (float)perlin_r_f32(r);
}
static inline void perlin_write_List_Pair_Str_F32(perlin_writer* w, const PerlinList_Pair_Str_F32* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_Pair_Str_F32(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_Pair_Str_F32(perlin_reader* r, PerlinList_Pair_Str_F32* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinPair_Str_F32*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_Pair_Str_F32(r, &v->data[k_], a);
    }
}
static inline void perlin_write_Pair_Str_Str(perlin_writer* w, const PerlinPair_Str_Str* v) {
    perlin_w_str(w, v->first);
    perlin_w_str(w, v->second);
}
static inline void perlin_read_Pair_Str_Str(perlin_reader* r, PerlinPair_Str_Str* v, perlin_arena* a) {
    (void)a;
    v->first = perlin_r_str(r);
    v->second = perlin_r_str(r);
}
static inline void perlin_write_List_Pair_Str_Str(perlin_writer* w, const PerlinList_Pair_Str_Str* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_Pair_Str_Str(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_Pair_Str_Str(perlin_reader* r, PerlinList_Pair_Str_Str* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinPair_Str_Str*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_Pair_Str_Str(r, &v->data[k_], a);
    }
}
static inline void perlin_write_Pair_Str_I32(perlin_writer* w, const PerlinPair_Str_I32* v) {
    perlin_w_str(w, v->first);
    perlin_w_i32(w, (int32_t)(v->second));
}
static inline void perlin_read_Pair_Str_I32(perlin_reader* r, PerlinPair_Str_I32* v, perlin_arena* a) {
    (void)a;
    v->first = perlin_r_str(r);
    v->second = (int32_t)perlin_r_i32(r);
}
static inline void perlin_write_List_Pair_Str_I32(perlin_writer* w, const PerlinList_Pair_Str_I32* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_Pair_Str_I32(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_Pair_Str_I32(perlin_reader* r, PerlinList_Pair_Str_I32* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinPair_Str_I32*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_Pair_Str_I32(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_WorldLine(perlin_writer* w, const PerlinList_WorldLine* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_WorldLine(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_WorldLine(perlin_reader* r, PerlinList_WorldLine* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinWorldLine*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_WorldLine(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_WorldBox(perlin_writer* w, const PerlinList_WorldBox* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_WorldBox(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_WorldBox(perlin_reader* r, PerlinList_WorldBox* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinWorldBox*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_WorldBox(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_ClientBlockEdit(perlin_writer* w, const PerlinList_ClientBlockEdit* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_ClientBlockEdit(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_ClientBlockEdit(perlin_reader* r, PerlinList_ClientBlockEdit* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinClientBlockEdit*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_ClientBlockEdit(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_U32(perlin_writer* w, const PerlinList_U32* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_w_u32(w, (uint32_t)(v->data[k_]));
    }
}
static inline void perlin_read_List_U32(perlin_reader* r, PerlinList_U32* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (uint32_t*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        v->data[k_] = (uint32_t)perlin_r_u32(r);
    }
}
static inline void perlin_write_List_U16(perlin_writer* w, const PerlinList_U16* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_w_u16(w, (uint16_t)(v->data[k_]));
    }
}
static inline void perlin_read_List_U16(perlin_reader* r, PerlinList_U16* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (uint16_t*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        v->data[k_] = (uint16_t)perlin_r_u16(r);
    }
}
static inline void perlin_write_List_MeshVertex(perlin_writer* w, const PerlinList_MeshVertex* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_MeshVertex(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_MeshVertex(perlin_reader* r, PerlinList_MeshVertex* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinMeshVertex*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_MeshVertex(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_OnlinePlayer(perlin_writer* w, const PerlinList_OnlinePlayer* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_OnlinePlayer(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_OnlinePlayer(perlin_reader* r, PerlinList_OnlinePlayer* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinOnlinePlayer*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_OnlinePlayer(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_Effect(perlin_writer* w, const PerlinList_Effect* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_Effect(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_Effect(perlin_reader* r, PerlinList_Effect* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinEffect*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_Effect(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_FoundBlock(perlin_writer* w, const PerlinList_FoundBlock* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_FoundBlock(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_FoundBlock(perlin_reader* r, PerlinList_FoundBlock* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinFoundBlock*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_FoundBlock(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_Entity(perlin_writer* w, const PerlinList_Entity* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_Entity(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_Entity(perlin_reader* r, PerlinList_Entity* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinEntity*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_Entity(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_PlayerEntry(perlin_writer* w, const PerlinList_PlayerEntry* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_PlayerEntry(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_PlayerEntry(perlin_reader* r, PerlinList_PlayerEntry* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinPlayerEntry*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_PlayerEntry(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_PackEntry(perlin_writer* w, const PerlinList_PackEntry* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_PackEntry(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_PackEntry(perlin_reader* r, PerlinList_PackEntry* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinPackEntry*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_PackEntry(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_ChatLine(perlin_writer* w, const PerlinList_ChatLine* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_ChatLine(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_ChatLine(perlin_reader* r, PerlinList_ChatLine* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinChatLine*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_ChatLine(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_SettingEntry(perlin_writer* w, const PerlinList_SettingEntry* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_SettingEntry(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_SettingEntry(perlin_reader* r, PerlinList_SettingEntry* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinSettingEntry*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_SettingEntry(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_ModEntry(perlin_writer* w, const PerlinList_ModEntry* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_ModEntry(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_ModEntry(perlin_reader* r, PerlinList_ModEntry* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinModEntry*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_ModEntry(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_PacketCount(perlin_writer* w, const PerlinList_PacketCount* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_PacketCount(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_PacketCount(perlin_reader* r, PerlinList_PacketCount* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinPacketCount*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_PacketCount(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_ServerItem(perlin_writer* w, const PerlinList_ServerItem* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_ServerItem(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_ServerItem(perlin_reader* r, PerlinList_ServerItem* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinServerItem*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_ServerItem(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_FormButton(perlin_writer* w, const PerlinList_FormButton* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_FormButton(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_FormButton(perlin_reader* r, PerlinList_FormButton* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinFormButton*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_FormButton(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_FormControl(perlin_writer* w, const PerlinList_FormControl* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_FormControl(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_FormControl(perlin_reader* r, PerlinList_FormControl* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinFormControl*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_FormControl(r, &v->data[k_], a);
    }
}
static inline void perlin_write_List_ServerEntity(perlin_writer* w, const PerlinList_ServerEntity* v) {
    perlin_w_u32(w, v->len);
    for (uint32_t k_ = 0; k_ < v->len; k_++) {
        perlin_write_ServerEntity(w, &v->data[k_]);
    }
}
static inline void perlin_read_List_ServerEntity(perlin_reader* r, PerlinList_ServerEntity* v, perlin_arena* a) {
    v->len = perlin_r_u32(r);
    v->data = NULL;
    if (r->failed || v->len == 0) { v->len = 0; return; }
    if ((size_t)v->len > (size_t)(r->end - r->p)) { r->failed = 1; v->len = 0; return; }
    v->data = (PerlinServerEntity*)perlin_arena_alloc(a, sizeof(*v->data) * v->len);
    if (!v->data) { r->failed = 1; v->len = 0; return; }
    for (uint32_t k_ = 0; k_ < v->len && !r->failed; k_++) {
        perlin_read_ServerEntity(r, &v->data[k_], a);
    }
}

static inline int32_t perlin_log(const PerlinLogRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_LogRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_LOG, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_meta(PerlinMeta* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_META, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_Meta(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_clock(int64_t* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_CLOCK, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = (int64_t)perlin_r_i64(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_player_state(PerlinPlayerState* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_STATE, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_PlayerState(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_player_effects(PerlinPlayerEffectsResponse* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_EFFECTS, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_PlayerEffectsResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_player_look(PerlinPlayerLookResponse* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_LOOK, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_PlayerLookResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_inventory(PerlinInventory* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_INVENTORY, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_Inventory(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_inventory_select(const PerlinInventorySelectRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_InventorySelectRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_INVENTORY_SELECT, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_inventory_drop(const PerlinInventoryDropRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_InventoryDropRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_INVENTORY_DROP, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_inventory_swap(const PerlinInventorySwapRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_InventorySwapRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_INVENTORY_SWAP, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_inventory_click(const PerlinInventoryClickRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_InventoryClickRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_INVENTORY_CLICK, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_inventory_open(perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_INVENTORY_OPEN, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_inventory_close(perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_INVENTORY_CLOSE, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_world_state(PerlinWorldState* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_WORLD_STATE, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_WorldState(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_block(const PerlinBlockRequest* in, PerlinBlockInfo* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_BlockRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_BLOCK, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_BlockInfo(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_region(const PerlinRegionRequest* in, PerlinRegion* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_RegionRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_REGION, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_Region(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_raycast(const PerlinRaycastRequest* in, PerlinRayHit* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_RaycastRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_RAYCAST, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_RayHit(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_aim(PerlinAimState* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_AIM, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_AimState(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_find_blocks(const PerlinFindBlocksRequest* in, PerlinFindBlocksResponse* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_FindBlocksRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_FIND_BLOCKS, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_FindBlocksResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_chunk_loaded(const PerlinChunkLoadedRequest* in, uint8_t* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_ChunkLoadedRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_CHUNK_LOADED, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = (uint8_t)perlin_r_u8(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_block_names(PerlinBlockNamesResponse* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_BLOCK_NAMES, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_BlockNamesResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_block_props(const PerlinBlockPropsRequest* in, PerlinBlockProps* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_BlockPropsRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_BLOCK_PROPS, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_BlockProps(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_block_entity_data(const PerlinBlockEntityDataRequest* in, PerlinBlockEntityInfo* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_BlockEntityDataRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_BLOCK_ENTITY_DATA, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_BlockEntityInfo(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_display_name(const PerlinDisplayNameRequest* in, perlin_str* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_DisplayNameRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_DISPLAY_NAME, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = perlin_r_rest_str(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_break_progress(PerlinBreakState* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_BREAK_PROGRESS, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_BreakState(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_map_chunk(const PerlinMapChunkRequest* in, perlin_bytes* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_MapChunkRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_MAP_CHUNK, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = perlin_r_rest(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_entities(const PerlinEntitiesRequest* in, PerlinEntitiesResponse* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_EntitiesRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_ENTITIES, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_EntitiesResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_entity_by_id(const PerlinEntityByIdRequest* in, PerlinEntity* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_EntityByIdRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_ENTITY_BY_ID, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_Entity(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_player_list(PerlinPlayerListResponse* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_LIST, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_PlayerListResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_options(perlin_str* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_OPTIONS, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = perlin_r_rest_str(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_option_get(const PerlinOptionGetRequest* in, perlin_str* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_OptionGetRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_OPTION_GET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = perlin_r_rest_str(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_option_set(const PerlinOptionSetRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_OptionSetRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_OPTION_SET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_screen(PerlinScreenState* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_SCREEN, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_ScreenState(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_translate(const PerlinTranslateRequest* in, perlin_str* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_TranslateRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_TRANSLATE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = perlin_r_rest_str(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_reload_resources(perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_RELOAD_RESOURCES, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_perspective_get(PerlinPerspective* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PERSPECTIVE_GET, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = (PerlinPerspective)perlin_r_i32(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_perspective_set(const PerlinPerspectiveSetRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PerspectiveSetRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PERSPECTIVE_SET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_resource_packs(PerlinResourcePacksResponse* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_RESOURCE_PACKS, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_ResourcePacksResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_input_down(const PerlinInputDownRequest* in, uint8_t* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_InputDownRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_INPUT_DOWN, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = (uint8_t)perlin_r_u8(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_input_override(const PerlinInputOverrideState* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_InputOverrideState(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_INPUT_OVERRIDE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_key_register(const PerlinKeyRegisterRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_KeyRegisterRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_KEY_REGISTER, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_input_actions(PerlinInputActionsResponse* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_INPUT_ACTIONS, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_InputActionsResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_mouse_position(PerlinMousePositionResponse* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_MOUSE_POSITION, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_MousePositionResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_chat(const PerlinChatRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_ChatRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_CHAT, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_command(const PerlinCommandRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_CommandRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_COMMAND, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_local_message(const PerlinLocalMessageRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_LocalMessageRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_LOCAL_MESSAGE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_command_register(const PerlinCommandRegisterRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_CommandRegisterRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_COMMAND_REGISTER, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_command_unregister(const PerlinCommandUnregisterRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_CommandUnregisterRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_COMMAND_UNREGISTER, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_server_commands(PerlinServerCommandsResponse* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_SERVER_COMMANDS, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_ServerCommandsResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_toast(const PerlinToastRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_ToastRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_TOAST, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_title(const PerlinTitleRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_TitleRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_TITLE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_chat_history(const PerlinChatHistoryRequest* in, PerlinChatHistoryResponse* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_ChatHistoryRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_CHAT_HISTORY, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_ChatHistoryResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_title_state(PerlinTitleStateData* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_TITLE_STATE, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_TitleStateData(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_swing_arm(perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_SWING_ARM, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_use_item(perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_USE_ITEM, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_use_on_block(const PerlinUseOnBlockRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_UseOnBlockRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_USE_ON_BLOCK, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_attack_entity(const PerlinAttackEntityRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_AttackEntityRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_ATTACK_ENTITY, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_interact_entity(const PerlinInteractEntityRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_InteractEntityRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_INTERACT_ENTITY, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_break_block(const PerlinBreakBlockRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_BreakBlockRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_BREAK_BLOCK, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_look(const PerlinLookRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_LookRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_LOOK, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_respawn(perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_RESPAWN, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_disconnect(perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_DISCONNECT, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_play_sound(const PerlinPlaySoundRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlaySoundRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAY_SOUND, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_jump(perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_JUMP, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_ui_submit(const PerlinUiSubmitRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_UiSubmitRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_UI_SUBMIT, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_ui_clear(const PerlinUiClearRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_UiClearRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_UI_CLEAR, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_ui_measure(const PerlinUiMeasureRequest* in, float* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_UiMeasureRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_UI_MEASURE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = (float)perlin_r_f32(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_gfx_texture_create(const PerlinGfxTextureCreateRequest* in, uint32_t* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_GfxTextureCreateRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_GFX_TEXTURE_CREATE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = (uint32_t)perlin_r_u32(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_gfx_texture_update(const PerlinGfxTextureUpdateRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_GfxTextureUpdateRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_GFX_TEXTURE_UPDATE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_gfx_texture_destroy(const PerlinGfxTextureDestroyRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_GfxTextureDestroyRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_GFX_TEXTURE_DESTROY, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_gfx_submit(const PerlinGfxSubmitRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_GfxSubmitRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_GFX_SUBMIT, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_gfx_clear(const PerlinGfxClearRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_GfxClearRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_GFX_CLEAR, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_gfx_display(PerlinGfxDisplayInfo* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_GFX_DISPLAY, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_GfxDisplayInfo(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_gfx_image_load(const PerlinGfxImageLoadRequest* in, PerlinGfxImageLoadResponse* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_GfxImageLoadRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_GFX_IMAGE_LOAD, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_GfxImageLoadResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_input_grab(const PerlinInputGrabRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_InputGrabRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_INPUT_GRAB, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_native_info(PerlinNativeHandles* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_NATIVE_INFO, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_NativeHandles(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_storage_read(const PerlinStorageReadRequest* in, perlin_bytes* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_StorageReadRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_STORAGE_READ, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = perlin_r_rest(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_storage_write(const PerlinStorageWriteRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_StorageWriteRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_STORAGE_WRITE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_storage_list(const PerlinStorageListRequest* in, PerlinStorageListResponse* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_StorageListRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_STORAGE_LIST, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_StorageListResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_storage_remove(const PerlinStorageRemoveRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_StorageRemoveRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_STORAGE_REMOVE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_storage_exists(const PerlinStorageExistsRequest* in, uint8_t* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_StorageExistsRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_STORAGE_EXISTS, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = (uint8_t)perlin_r_u8(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_storage_make_dir(const PerlinStorageMakeDirRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_StorageMakeDirRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_STORAGE_MAKE_DIR, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_http_request(const PerlinHttpRequestRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_HttpRequestRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_HTTP_REQUEST, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_packet_send(const PerlinPacketSendRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PacketSendRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PACKET_SEND, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_packet_name(const PerlinPacketNameRequest* in, perlin_str* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PacketNameRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PACKET_NAME, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = perlin_r_rest_str(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_setting_get(const PerlinSettingGetRequest* in, perlin_str* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_SettingGetRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_SETTING_GET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = perlin_r_rest_str(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_setting_set(const PerlinSettingSetRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_SettingSetRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_SETTING_SET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_setting_list(PerlinSettingListResponse* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_SETTING_LIST, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_SettingListResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_mod_list(PerlinModListResponse* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_MOD_LIST, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_ModListResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_icon_set(const PerlinIconSetRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_IconSetRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_ICON_SET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_open_screen(const PerlinOpenScreenRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_OpenScreenRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_OPEN_SCREEN, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_open_mod_settings(const PerlinOpenModSettingsRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_OpenModSettingsRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_OPEN_MOD_SETTINGS, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_take_screenshot(perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_TAKE_SCREENSHOT, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_cycle_perspective(perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_CYCLE_PERSPECTIVE, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_copy_to_clipboard(const PerlinCopyToClipboardRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_CopyToClipboardRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_COPY_TO_CLIPBOARD, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_open_url(const PerlinOpenUrlRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_OpenUrlRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_OPEN_URL, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_toggle_hud(perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_TOGGLE_HUD, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_copy_file(const PerlinCopyFileRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_CopyFileRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_COPY_FILE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_move_file(const PerlinMoveFileRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_MoveFileRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_MOVE_FILE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_download_file(const PerlinDownloadFileRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_DownloadFileRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_DOWNLOAD_FILE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_camera_control(const PerlinCameraControlState* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_CameraControlState(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_CAMERA_CONTROL, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_camera_state(PerlinCameraInfo* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_CAMERA_STATE, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_CameraInfo(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_world_to_screen(const PerlinWorldPoint* in, PerlinScreenPoint* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_WorldPoint(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_WORLD_TO_SCREEN, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_ScreenPoint(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_player_motion(const PerlinMotionRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_MotionRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_MOTION, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_teleport(const PerlinTeleportRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_TeleportRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_TELEPORT, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_model_pose(const PerlinModelPose* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_ModelPose(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_MODEL_POSE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_set_look(const PerlinLookSet* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_LookSet(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_SET_LOOK, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_world_lines(const PerlinWorldLines* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_WorldLines(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_WORLD_LINES, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_world_boxes(const PerlinWorldBoxes* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_WorldBoxes(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_WORLD_BOXES, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_client_block_set(const PerlinClientBlockEdits* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_ClientBlockEdits(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_CLIENT_BLOCK_SET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_client_block_get(const PerlinBlockQuery* in, PerlinBlockQueryResult* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_BlockQuery(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_CLIENT_BLOCK_GET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_BlockQueryResult(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_packet_drop(const PerlinPacketDropRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PacketDropRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PACKET_DROP, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_packet_counters(PerlinPacketCountersResponse* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PACKET_COUNTERS, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_PacketCountersResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_packet_inject(const PerlinPacketInjectRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PacketInjectRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PACKET_INJECT, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_hud_element_set(const PerlinHudElementState* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_HudElementState(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_HUD_ELEMENT_SET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_debug_lines(const PerlinDebugLinesData* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_DebugLinesData(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_DEBUG_LINES, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_fog_set(const PerlinFogOverride* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_FogOverride(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_FOG_SET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_screen_tint(const PerlinScreenTintData* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_ScreenTintData(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_SCREEN_TINT, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_entity_render_override(const PerlinEntityOverride* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_EntityOverride(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_ENTITY_RENDER_OVERRIDE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_world_mesh(const PerlinWorldMeshData* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_WorldMeshData(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_WORLD_MESH, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_world_mesh_clear(const PerlinWorldMeshClearRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_WorldMeshClearRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_WORLD_MESH_CLEAR, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_frame_limit(const PerlinFrameLimitData* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_FrameLimitData(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_FRAME_LIMIT, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_window_state(PerlinWindowInfo* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_WINDOW_STATE, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_WindowInfo(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_screen_button_add(const PerlinScreenButtonDef* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_ScreenButtonDef(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_SCREEN_BUTTON_ADD, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_screen_button_remove(const PerlinScreenButtonRemoveRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_ScreenButtonRemoveRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_SCREEN_BUTTON_REMOVE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_screen_open_custom(const PerlinScreenOpenCustomRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_ScreenOpenCustomRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_SCREEN_OPEN_CUSTOM, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_tooltip_append(const PerlinTooltipAppendRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_TooltipAppendRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_TOOLTIP_APPEND, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_loading_overlay(const PerlinLoadingOverlayData* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_LoadingOverlayData(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_LOADING_OVERLAY, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_skin_override(const PerlinSkinOverrideData* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_SkinOverrideData(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_SKIN_OVERRIDE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_ambient_sound(const PerlinAmbientSoundData* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_AmbientSoundData(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_AMBIENT_SOUND, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_server_ping(const PerlinServerPingRequest* in, uint32_t* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_ServerPingRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_SERVER_PING, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = (uint32_t)perlin_r_u32(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_mod_send(const PerlinModMessageData* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_ModMessageData(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_MOD_SEND, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_pack_register(const PerlinPackRegistration* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PackRegistration(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PACK_REGISTER, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_shader_pack_set(const PerlinShaderPackSetRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_ShaderPackSetRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_SHADER_PACK_SET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_particle_spawn(const PerlinParticleRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_ParticleRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PARTICLE_SPAWN, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_key_register_ex(const PerlinKeyBindEx* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_KeyBindEx(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_KEY_REGISTER_EX, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_broadcast(const PerlinTextMessage* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_TextMessage(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_BROADCAST, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_message(const PerlinPlayerText* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerText(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_MESSAGE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_kick(const PerlinPlayerText* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerText(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_KICK, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_online_players(PerlinOnlinePlayerList* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_ONLINE_PLAYERS, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_OnlinePlayerList(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_teleport(const PerlinServerTeleport* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_ServerTeleport(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_TELEPORT, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_set_block(const PerlinBlockWrite* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_BlockWrite(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_SET_BLOCK, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_block_name(const PerlinBlockCoord* in, PerlinTextMessage* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_BlockCoord(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_BLOCK_NAME, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_TextMessage(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_server_info(PerlinServerStatus* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_SERVER_INFO, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_ServerStatus(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_give_item(const PerlinGiveItemRequest* in, PerlinGiveItemResult* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_GiveItemRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_GIVE_ITEM, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_GiveItemResult(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_player_info(const PerlinPlayerInfoRequest* in, PerlinPlayerDetails* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerInfoRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_INFO, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_PlayerDetails(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_player_set_game_mode(const PerlinPlayerSetGameModeRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerSetGameModeRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_SET_GAME_MODE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_set_health(const PerlinPlayerSetHealthRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerSetHealthRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_SET_HEALTH, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_damage(const PerlinPlayerDamageRequest* in, uint8_t* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerDamageRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_DAMAGE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = (uint8_t)perlin_r_u8(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_player_kill(const PerlinPlayerKillRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerKillRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_KILL, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_set_food(const PerlinPlayerSetFoodRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerSetFoodRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_SET_FOOD, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_add_experience(const PerlinPlayerAddExperienceRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerAddExperienceRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_ADD_EXPERIENCE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_effect_add(const PerlinPlayerEffectAddRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerEffectAddRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_EFFECT_ADD, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_effect_remove(const PerlinPlayerEffectRemoveRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerEffectRemoveRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_EFFECT_REMOVE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_inventory(const PerlinPlayerInventoryRequest* in, PerlinServerInventory* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerInventoryRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_INVENTORY, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_ServerInventory(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_player_set_slot(const PerlinPlayerSetSlotRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerSetSlotRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_SET_SLOT, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_clear_inventory(const PerlinPlayerClearInventoryRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerClearInventoryRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_CLEAR_INVENTORY, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_set_velocity(const PerlinPlayerSetVelocityRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerSetVelocityRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_SET_VELOCITY, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_transfer_dimension(const PerlinPlayerTransferDimensionRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerTransferDimensionRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_TRANSFER_DIMENSION, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_title(const PerlinPlayerTitleRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerTitleRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_TITLE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_action_bar(const PerlinPlayerActionBarRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerActionBarRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_ACTION_BAR, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_sound(const PerlinPlayerSoundRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerSoundRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_SOUND, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_transfer(const PerlinPlayerTransferRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerTransferRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_TRANSFER, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_set_operator(const PerlinPlayerSetOperatorRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerSetOperatorRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_SET_OPERATOR, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_form(const PerlinPlayerFormRequest* in, uint32_t* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerFormRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_FORM, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = (uint32_t)perlin_r_u32(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_player_run_command(const PerlinPlayerRunCommandRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerRunCommandRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_RUN_COMMAND, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_set_spawn(const PerlinPlayerSetSpawnRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerSetSpawnRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_SET_SPAWN, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_player_by_name(const PerlinPlayerByNameRequest* in, uint64_t* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_PlayerByNameRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PLAYER_BY_NAME, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = (uint64_t)perlin_r_u64(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_block_state(const PerlinBlockStateRequest* in, PerlinBlockInfo* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_BlockStateRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_BLOCK_STATE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_BlockInfo(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_block_state_set(const PerlinBlockStateSetRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_BlockStateSetRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_BLOCK_STATE_SET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_fill(const PerlinFillRequest* in, uint32_t* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_FillRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_FILL, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = (uint32_t)perlin_r_u32(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_time_get(PerlinTimeGetResponse* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_TIME_GET, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_TimeGetResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_time_set(const PerlinTimeSetRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_TimeSetRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_TIME_SET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_weather_set(const PerlinWeatherSetRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_WeatherSetRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_WEATHER_SET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_game_rule_get(const PerlinGameRuleGetRequest* in, int32_t* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_GameRuleGetRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_GAME_RULE_GET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = (int32_t)perlin_r_i32(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_game_rule_set(const PerlinGameRuleSetRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_GameRuleSetRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_GAME_RULE_SET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_difficulty_set(const PerlinDifficultySetRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_DifficultySetRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_DIFFICULTY_SET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_explode(const PerlinExplodeRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_ExplodeRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_EXPLODE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_highest_block(const PerlinHighestBlockRequest* in, int32_t* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_HighestBlockRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_HIGHEST_BLOCK, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = (int32_t)perlin_r_i32(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_particle_at(const PerlinParticleAtRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_ParticleAtRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_PARTICLE_AT, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_drop_item(const PerlinDropItemRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_DropItemRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_DROP_ITEM, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_world_spawn_get(PerlinBlockCoord* out, perlin_arena* a) {
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_WORLD_SPAWN_GET, NULL, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_BlockCoord(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_world_spawn_set(const PerlinBlockCoord* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_BlockCoord(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_WORLD_SPAWN_SET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_entity_spawn(const PerlinEntitySpawnRequest* in, uint64_t* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_EntitySpawnRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_ENTITY_SPAWN, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = (uint64_t)perlin_r_u64(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_entity_list(const PerlinEntityListRequest* in, PerlinEntityListResponse* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_EntityListRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_ENTITY_LIST, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_EntityListResponse(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_entity_remove(const PerlinEntityRemoveRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_EntityRemoveRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_ENTITY_REMOVE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_entity_damage(const PerlinEntityDamageRequest* in, uint8_t* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_EntityDamageRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_ENTITY_DAMAGE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = (uint8_t)perlin_r_u8(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_entity_teleport(const PerlinEntityTeleportRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_EntityTeleportRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_ENTITY_TELEPORT, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_entity_set_velocity(const PerlinEntitySetVelocityRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_EntitySetVelocityRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_ENTITY_SET_VELOCITY, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_entity_info(const PerlinEntityInfoRequest* in, PerlinServerEntity* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_EntityInfoRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_ENTITY_INFO, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    perlin_read_ServerEntity(r, &(*out), a);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_entity_set_health(const PerlinEntitySetHealthRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_EntitySetHealthRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_ENTITY_SET_HEALTH, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_run_command(const PerlinRunCommandRequest* in, perlin_str* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_RunCommandRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_RUN_COMMAND, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = perlin_r_rest_str(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_motd_set(const PerlinMotdSetRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_MotdSetRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_MOTD_SET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_loot_table_override(const PerlinLootTableOverrideRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_LootTableOverrideRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_LOOT_TABLE_OVERRIDE, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}
static inline int32_t perlin_loot_table_get(const PerlinLootTableGetRequest* in, perlin_str* out, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_LootTableGetRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_LOOT_TABLE_GET, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    perlin_reader* r = &rb_;
    (*out) = perlin_r_rest_str(r);
    return rb_.failed ? -6 : 0;
}
static inline int32_t perlin_event_amend(const PerlinEventAmendRequest* in, perlin_arena* a) {
    size_t mark_ = a->used;
    uint32_t room_ = (uint32_t)(a->cap > a->used + 8 ? (a->cap - a->used - 8) / 2 : 0);
    perlin_writer wb_ = perlin_writer_make(perlin_arena_alloc(a, room_), room_);
    perlin_writer* w = &wb_;
    perlin_write_EventAmendRequest(w, &(*in));
    if (!wb_.data || wb_.overflow) { a->used = mark_; return -3; }
    a->used = mark_ + ((size_t)(wb_.data - (a->base + mark_))) + wb_.len;
    perlin_reader rb_;
    int32_t rc_ = perlin_invoke(PERLIN_OP_EVENT_AMEND, &wb_, &rb_, a);
    if (rc_ < 0) return rc_;
    return 0;
}

static inline int perlin_decode_event_tick(const void* data, uint32_t len, PerlinEventTick* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventTick(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_frame(const void* data, uint32_t len, PerlinEventFrame* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventFrame(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_chat_received(const void* data, uint32_t len, PerlinEventChat* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventChat(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_chat_sending(const void* data, uint32_t len, PerlinEventText* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventText(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_connected(const void* data, uint32_t len, PerlinEventConnection* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventConnection(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_disconnected(const void* data, uint32_t len, PerlinEventConnection* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventConnection(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_key(const void* data, uint32_t len, PerlinEventKey* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventKey(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_command(const void* data, uint32_t len, PerlinEventCommand* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventCommand(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_damage(const void* data, uint32_t len, PerlinEventDamage* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventDamage(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_death(const void* data, uint32_t len, PerlinEventText* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventText(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_dimension_changed(const void* data, uint32_t len, PerlinEventDimension* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventDimension(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_screen_changed(const void* data, uint32_t len, PerlinEventScreen* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventScreen(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_slot_changed(const void* data, uint32_t len, PerlinEventSlot* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventSlot(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_entity_added(const void* data, uint32_t len, PerlinEventEntity* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventEntity(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_entity_removed(const void* data, uint32_t len, PerlinEventEntity* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventEntity(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_sound(const void* data, uint32_t len, PerlinEventSound* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventSound(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_http_response(const void* data, uint32_t len, PerlinEventHttp* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventHttp(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_packet(const void* data, uint32_t len, PerlinEventPacket* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventPacket(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_title(const void* data, uint32_t len, PerlinEventTitle* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventTitle(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_setting_changed(const void* data, uint32_t len, PerlinEventSettingChanged* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventSettingChanged(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_look(const void* data, uint32_t len, PerlinEventLook* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventLook(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_mouse_move(const void* data, uint32_t len, PerlinEventMouseMove* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventMouseMove(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_mouse_button(const void* data, uint32_t len, PerlinEventMouseButton* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventMouseButton(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_mouse_wheel(const void* data, uint32_t len, PerlinEventMouseWheel* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventMouseWheel(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_key_raw(const void* data, uint32_t len, PerlinEventKeyRaw* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventKeyRaw(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_text_input(const void* data, uint32_t len, PerlinEventText* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventText(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_screen_render(const void* data, uint32_t len, PerlinEventScreenRender* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventScreenRender(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_slot_click(const void* data, uint32_t len, PerlinEventSlotClick* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventSlotClick(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_interact(const void* data, uint32_t len, PerlinEventInteract* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventInteract(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_chunk_loaded(const void* data, uint32_t len, PerlinEventChunk* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventChunk(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_chunk_unloaded(const void* data, uint32_t len, PerlinEventChunk* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventChunk(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_block_changed(const void* data, uint32_t len, PerlinEventBlockChanged* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventBlockChanged(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_level_loaded(const void* data, uint32_t len, PerlinEventLevel* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventLevel(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_toast(const void* data, uint32_t len, PerlinEventToast* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventToast(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_mod_message(const void* data, uint32_t len, PerlinEventModMessage* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventModMessage(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_tooltip(const void* data, uint32_t len, PerlinEventTooltip* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventTooltip(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_screen_button(const void* data, uint32_t len, PerlinEventScreenButton* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventScreenButton(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_window_focus(const void* data, uint32_t len, PerlinEventWindow* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventWindow(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_server_ping(const void* data, uint32_t len, PerlinEventServerPing* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventServerPing(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_player_join(const void* data, uint32_t len, PerlinEventPlayer* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventPlayer(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_player_quit(const void* data, uint32_t len, PerlinEventPlayer* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventPlayer(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_player_chat(const void* data, uint32_t len, PerlinEventPlayerChat* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventPlayerChat(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_block_break(const void* data, uint32_t len, PerlinEventPlayerBlock* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventPlayerBlock(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_block_place(const void* data, uint32_t len, PerlinEventPlayerBlock* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventPlayerBlock(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_player_damage(const void* data, uint32_t len, PerlinEventServerDamage* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventServerDamage(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_player_death(const void* data, uint32_t len, PerlinEventServerDeath* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventServerDeath(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_player_respawn(const void* data, uint32_t len, PerlinEventPlayer* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventPlayer(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_player_use_block(const void* data, uint32_t len, PerlinEventPlayerUse* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventPlayerUse(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_player_attack(const void* data, uint32_t len, PerlinEventPlayerAttack* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventPlayerAttack(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_entity_damage(const void* data, uint32_t len, PerlinEventEntityDamage* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventEntityDamage(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_entity_death(const void* data, uint32_t len, PerlinEventEntityDeath* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventEntityDeath(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_entity_spawn(const void* data, uint32_t len, PerlinEventEntitySpawn* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventEntitySpawn(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_item_consume(const void* data, uint32_t len, PerlinEventItem* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventItem(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_game_mode_change(const void* data, uint32_t len, PerlinEventGameModeChange* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventGameModeChange(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_form_response(const void* data, uint32_t len, PerlinEventFormResponse* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventFormResponse(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_player_move(const void* data, uint32_t len, PerlinEventPlayerMove* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventPlayerMove(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_explosion(const void* data, uint32_t len, PerlinEventExplosion* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventExplosion(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_item_drop(const void* data, uint32_t len, PerlinEventItem* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventItem(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_item_pickup(const void* data, uint32_t len, PerlinEventItem* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventItem(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_container_open(const void* data, uint32_t len, PerlinEventContainer* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventContainer(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_dimension_change(const void* data, uint32_t len, PerlinEventPlayerDimension* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventPlayerDimension(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_server_command(const void* data, uint32_t len, PerlinEventServerCommand* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventServerCommand(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}
static inline int perlin_decode_event_pre_login(const void* data, uint32_t len, PerlinEventPreLogin* out, perlin_arena* a) {
    perlin_reader rb_ = perlin_reader_make(data, len);
    perlin_read_EventPreLogin(&rb_, out, a);
    return rb_.failed ? -3 : 0;
}

#ifdef __cplusplus
}
#endif

#endif
