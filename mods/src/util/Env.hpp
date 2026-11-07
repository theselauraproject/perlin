#pragma once
#include <cstdlib>

#define PERLIN_ENV(name)                                                                                     \
    ([]() -> const char* {                                                                                   \
        static const char* const value = std::getenv(name);                                                  \
        return value;                                                                                        \
    }())
