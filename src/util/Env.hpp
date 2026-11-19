#pragma once
#include <cstdlib>

#define PERLIN_ENV(name)                                                                                     \
    ([]() -> const char* {                                                                                   \
        static const char* const value = std::getenv(name);                                                  \
        return value;                                                                                        \
    }())

namespace perlin {
    inline bool OffhandExperiment() {
        static const bool on = [] {
            const char* v = std::getenv("PERLIN_OFFHAND_EXPERIMENT");
            return v && *v && !(v[0] == '0' && v[1] == 0);
        }();
        return on;
    }
}
