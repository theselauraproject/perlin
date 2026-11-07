typedef unsigned int u32;
typedef unsigned long long u64;
typedef __SIZE_TYPE__ usize;
__attribute__((import_module("perlin"), import_name("call"))) int perlin_call(u32 op, const void* in, u32 in_len, void* out, u32 out_cap);
__attribute__((import_module("wasi_snapshot_preview1"), import_name("fd_write"))) int fd_write(int fd, const void* iovs, usize n, usize* written);
__attribute__((import_module("wasi_snapshot_preview1"), import_name("clock_time_get"))) int clock_time_get(int id, u64 prec, u64* out);
static unsigned char heap[1 << 16];
static usize top = 16;
__attribute__((export_name("perlin_alloc"))) void* perlin_alloc(u32 n) { void* p = heap + top; top += (n + 15) & ~15u; if (top > sizeof heap) top = 16; return p; }
__attribute__((export_name("perlin_free"))) void perlin_free(void* p) { (void)p; }
__attribute__((export_name("perlin_subscriptions"))) u32 perlin_subscriptions(void) { return (1u << 7) | (1u << 3); }
static usize len(const char* s) { usize n = 0; while (s[n]) n++; return n; }
static void say(const char* s) {
    struct { const void* p; usize n; } iov = { s, len(s) };
    usize w = 0; fd_write(1, &iov, 1, &w);
}
static void local_message(const char* text) {
    u32 n = (u32)len(text); unsigned char buf[300];
    buf[0] = n; buf[1] = n >> 8; buf[2] = 0; buf[3] = 0;
    for (u32 i = 0; i < n; i++) buf[4 + i] = text[i];
    unsigned char out[16];
    perlin_call(0x0702, buf, 4 + n, out, sizeof out);
}
static volatile int ticks;
__attribute__((export_name("perlin_event"))) u32 perlin_event(u32 id, const unsigned char* data, u32 n) {
    if (id == 1) { u64 t = 0; clock_time_get(0, 0, &t); say(t > 1700000000000000000ull ? (sizeof(void*) == 8 ? "loaded as wasm64, clock ok\n" : "loaded as wasm32, clock ok\n") : "clock broken\n"); }
    if (id == 7) local_message("hello from a test plugin");
#ifdef SPIN
    if (id == 3) { ticks++; if (ticks == 3) for (;;) { ticks++; } }
#endif
    (void)data; (void)n;
    return 0;
}
