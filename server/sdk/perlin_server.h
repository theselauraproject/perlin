#ifndef PERLIN_SERVER_ABI_H
#define PERLIN_SERVER_ABI_H

#include <stdint.h>

#define PERLIN_SERVER_ABI_VERSION 1

typedef enum PsEvent {
    PS_EVENT_ENABLE = 0,
    PS_EVENT_DISABLE = 1,
    PS_EVENT_TICK = 2,
    PS_EVENT_PLAYER_JOIN = 3,
    PS_EVENT_PLAYER_QUIT = 4,
    PS_EVENT_CHAT = 5,
    PS_EVENT_COMMAND = 6,
    PS_EVENT_BLOCK_CHANGE = 7,
    PS_EVENT_BLOCK_BREAK = 8,
    PS_EVENT_BLOCK_PLACE = 9,
    PS_EVENT_COUNT = 10
} PsEvent;

typedef enum PsOp {
    PS_OP_LOG = 1,
    PS_OP_BROADCAST = 2,
    PS_OP_MESSAGE = 3,
    PS_OP_KICK = 4,
    PS_OP_REGISTER_COMMAND = 5,
    PS_OP_PLAYERS = 6,
    PS_OP_TELEPORT = 7,
    PS_OP_SET_BLOCK = 8,
    PS_OP_GET_BLOCK = 9,
    PS_OP_SERVER_INFO = 10,
    PS_OP_TIME_MS = 11,
    PS_OP_GIVE_ITEM = 12
} PsOp;

typedef enum PsStatus {
    PS_OK = 0,
    PS_ERR_BAD_ARGUMENTS = -1,
    PS_ERR_NO_SUCH_PLAYER = -2,
    PS_ERR_NOT_LOADED = -3,
    PS_ERR_BUFFER_TOO_SMALL = -4,
    PS_ERR_UNKNOWN_OP = -5,
    PS_ERR_UNKNOWN_ITEM = -6
} PsStatus;

#if defined(__wasm__)
#define PERLIN_IMPORT(module, name) __attribute__((import_module(module), import_name(name)))
#define PERLIN_EXPORT(name) __attribute__((export_name(name)))
PERLIN_IMPORT("perlin", "call") int32_t perlin_call(uint32_t op, const void* in, uint32_t in_len, void* out, uint32_t out_cap);
#endif

#endif
