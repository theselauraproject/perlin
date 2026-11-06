#define PERLIN_DEFINE_RUNTIME
#include "perlin.h"

void* memcpy(void* d, const void* s, size_t n) {
    unsigned char* a = d;
    const unsigned char* b = s;
    while (n--)
        *a++ = *b++;
    return d;
}
void* memset(void* d, int c, size_t n) {
    unsigned char* a = d;
    while (n--)
        *a++ = (unsigned char)c;
    return d;
}

PERLIN_EXPORT(perlin_event) uint32_t perlin_event(uint32_t id, const unsigned char* data, uint32_t len) {
    (void)data;
    (void)len;
    perlin_arena a = perlin_frame();
    if (id == PERLIN_EVENT_LOAD) {
        perlin_subscribe(PERLIN_EVENT_CONNECTED);
        return 0;
    }
    if (id == PERLIN_EVENT_CONNECTED) {
        PerlinLocalMessageRequest m;
        m.text = perlin_cstr("hello from a C plugin");
        perlin_local_message(&m, &a);
    }
    return 0;
}
