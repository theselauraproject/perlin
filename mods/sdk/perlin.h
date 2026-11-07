#ifndef PERLIN_H
#define PERLIN_H

#include "perlin_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PERLIN_EXPORT(name) PERLIN_C_EXPORT(name)

#ifndef PERLIN_HEAP_SIZE
#define PERLIN_HEAP_SIZE (1u << 20)
#endif

#ifdef PERLIN_DEFINE_RUNTIME
static unsigned char perlin_heap_[PERLIN_HEAP_SIZE];
static uint32_t perlin_heap_top_;
static unsigned char perlin_scratch_[1u << 16];

PERLIN_EXPORT(perlin_alloc) void* perlin_alloc(uint32_t size) {
    if (perlin_heap_top_ + size > sizeof perlin_heap_)
        perlin_heap_top_ = 0;
    void* p = perlin_heap_ + perlin_heap_top_;
    perlin_heap_top_ += (size + 7u) & ~7u;
    return p;
}
PERLIN_EXPORT(perlin_free) void perlin_free(void* p) {
    (void)p;
}

static perlin_arena perlin_frame(void) {
    return perlin_arena_make(perlin_scratch_, sizeof perlin_scratch_);
}

static uint32_t perlin_subscribed_[PERLIN_SUBSCRIPTION_WORDS];
static void perlin_subscribe(uint32_t event) {
    if (event / 32 < PERLIN_SUBSCRIPTION_WORDS)
        perlin_subscribed_[event / 32] |= 1u << (event % 32);
}
PERLIN_EXPORT(perlin_subscriptions_word) uint32_t perlin_subscriptions_word(uint32_t word) {
    return word < PERLIN_SUBSCRIPTION_WORDS ? perlin_subscribed_[word] : 0u;
}
PERLIN_EXPORT(perlin_subscriptions) uint32_t perlin_subscriptions(void) {
    return perlin_subscribed_[0];
}

static void perlin_print(const char* text) {
    perlin_arena a = perlin_frame();
    PerlinLogRequest r;
    r.level = PERLIN_LOG_LEVEL_INFO;
    r.text = perlin_cstr(text);
    perlin_log(&r, &a);
}
#endif

static inline uint32_t perlin_itoa(char* out, int64_t v) {
    char tmp[24];
    uint32_t n = 0, len = 0;
    int neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-v) : (uint64_t)v;
    do {
        tmp[n++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (neg)
        out[len++] = '-';
    while (n)
        out[len++] = tmp[--n];
    out[len] = 0;
    return len;
}

#ifdef __cplusplus
}
#endif

#endif
