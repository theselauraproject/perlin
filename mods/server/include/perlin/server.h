/* perlin server: c api for embedding (singleplayer inside the client) and for loading as a library
 *
 * a host creates a server, starts it (own thread) or ticks it itself, and talks to it either through
 * nethernet like any remote player or through in-process loopback connections that skip the network entirely
 */
#ifndef PERLIN_SERVER_H
#define PERLIN_SERVER_H

#include <stddef.h>
#include <stdint.h>

#if defined(PERLIN_SERVER_STATIC)
#define PS_API
#elif defined(_WIN32)
#if defined(PERLIN_SERVER_BUILD_SHARED)
#define PS_API __declspec(dllexport)
#else
#define PS_API __declspec(dllimport)
#endif
#else
#define PS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ps_server ps_server;
typedef struct ps_conn ps_conn;

typedef struct ps_options {
    /* directory holding server.properties (or the older server.json), plugins/ and world data; NULL = current directory */
    const char* data_dir;
    /* json text overriding the config file (server.json keys); NULL = none */
    const char* config_json;
    /* embedded singleplayer: no nethernet listener, the one local player is an operator */
    int loopback_only;
} ps_options;

typedef void (*ps_log_fn)(int level, const char* text, void* user);

PS_API uint32_t ps_abi_version(void);
PS_API void ps_set_log(ps_log_fn fn, void* user);

/* NULL on failure; ps_last_error() says why */
PS_API ps_server* ps_create(const ps_options* options);
PS_API const char* ps_last_error(void);
/* binds listeners, loads plugins; 0 on success */
PS_API int ps_start(ps_server* server);
/* ticks on an internal thread until ps_stop */
PS_API int ps_run_async(ps_server* server);
/* 1 while the internal thread is ticking; 0 once it stopped or the server crashed (then ps_destroy it and start a new one) */
PS_API int ps_is_running(ps_server* server);
/* one tick on the caller's thread, for hosts with their own loop (not with ps_run_async) */
PS_API void ps_tick(ps_server* server);
PS_API void ps_stop(ps_server* server);
PS_API void ps_destroy(ps_server* server);
PS_API void ps_command(ps_server* server, const char* line);

/* in-process client connection: the bytes are the same batches a nethernet message would carry */
PS_API ps_conn* ps_connect_loopback(ps_server* server);
PS_API int ps_conn_send(ps_conn* conn, const uint8_t* data, size_t size);
/* 1 = a message was copied to buf (*size = its length), 0 = none waiting, -1 = buf too small (*size = needed) */
PS_API int ps_conn_receive(ps_conn* conn, uint8_t* buf, size_t capacity, size_t* size);
PS_API int ps_conn_alive(ps_conn* conn);
PS_API void ps_conn_close(ps_conn* conn);

#ifdef __cplusplus
}
#endif

#endif
