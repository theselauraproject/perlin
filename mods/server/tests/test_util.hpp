#pragma once
#include <cstdio>
#include <cstdlib>

static int g_failed = 0;
#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                             \
            g_failed++;                                                                                      \
        }                                                                                                    \
    } while (0)
