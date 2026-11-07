/* perlin server plugin abi, shared by the host and by plugins written in c or c++
 *
 * a plugin is a webassembly module (wasm32 or wasm64, wasi reactor) that exports
 *   perlin_alloc(size) / perlin_free(ptr)         event payload buffers
 *   perlin_subscriptions() -> u32                 bit n set = wants event n
 *   perlin_event(id, ptr, len) -> u32             bit 0 of the result cancels / marks handled
 * and imports one function
 *   perlin.call(op, in_ptr, in_len, out_ptr, out_cap) -> i32
 * a result >= 0 is the number of bytes written to out (or just success), < 0 is a PsStatus
 *
 * payloads are little endian; a string is u32 length then utf-8 bytes
 */
#ifndef PERLIN_SERVER_ABI_H
#define PERLIN_SERVER_ABI_H

#include <stdint.h>

#define PERLIN_SERVER_ABI_VERSION 1

typedef enum PsEvent {
    PS_EVENT_ENABLE = 0,       /* () */
    PS_EVENT_DISABLE = 1,      /* () */
    PS_EVENT_TICK = 2,         /* (u64 tick) */
    PS_EVENT_PLAYER_JOIN = 3,  /* (u64 player, str name) */
    PS_EVENT_PLAYER_QUIT = 4,  /* (u64 player, str name) */
    PS_EVENT_CHAT = 5,         /* (u64 player, str name, str message)  result bit 0 cancels */
    PS_EVENT_COMMAND = 6,      /* (u64 player, str command, str args)  result bit 0 = handled; only for commands the plugin registered */
    PS_EVENT_BLOCK_CHANGE = 7, /* (i32 x, i32 y, i32 z, str block) after any change, whoever made it */
    PS_EVENT_BLOCK_BREAK = 8,  /* (u64 player, i32 x, i32 y, i32 z, str block)  result bit 0 cancels */
    PS_EVENT_BLOCK_PLACE = 9,  /* (u64 player, i32 x, i32 y, i32 z, str block)  result bit 0 cancels */
    PS_EVENT_COUNT = 10
} PsEvent;

typedef enum PsOp {
    PS_OP_LOG = 1,           /* in: u8 level (0 debug..3 error), str text */
    PS_OP_BROADCAST = 2,     /* in: str text */
    PS_OP_MESSAGE = 3,       /* in: u64 player, str text */
    PS_OP_KICK = 4,          /* in: u64 player, str reason */
    PS_OP_REGISTER_COMMAND = 5, /* in: str name, str description */
    PS_OP_PLAYERS = 6,       /* out: u32 n, n * (u64 id, str name, f64 x, f64 y, f64 z) */
    PS_OP_TELEPORT = 7,      /* in: u64 player, f64 x, f64 y, f64 z */
    PS_OP_SET_BLOCK = 8,     /* in: i32 x, i32 y, i32 z, str block name (no states yet) */
    PS_OP_GET_BLOCK = 9,     /* in: i32 x, i32 y, i32 z; out: str block name */
    PS_OP_SERVER_INFO = 10,  /* out: str name, u32 players, u32 max players, u64 tick, str terrain backend */
    PS_OP_TIME_MS = 11,      /* out: i64 */
    PS_OP_GIVE_ITEM = 12     /* in: u64 player, str item name, u32 count; out: u32 count that did not fit */
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
