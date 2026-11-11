#include "client/EmbeddedProvider.hpp"
#include "util/Resources.hpp"

#include <cstring>

#if defined(_MSC_VER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

extern "C" const char* const perlin_embedded_names[];
extern "C" const size_t perlin_embedded_count;

namespace perlin {
    namespace {
        bool Find(const char* name, const uint8_t** data, size_t* size) {
            for (size_t i = 0; i < perlin_embedded_count; i++) {
                if (std::strcmp(perlin_embedded_names[i], name) != 0)
                    continue;
                HRSRC res =
                    FindResourceA(nullptr, MAKEINTRESOURCEA(i + 1), reinterpret_cast<LPCSTR>(RT_RCDATA));
                if (!res)
                    return false;
                HGLOBAL handle = LoadResource(nullptr, res);
                if (!handle)
                    return false;
                *data = static_cast<const uint8_t*>(LockResource(handle));
                *size = SizeofResource(nullptr, res);
                return *data != nullptr;
            }
            return false;
        }
    }
    void RegisterEmbeddedResources() {
        resources::SetProvider(&Find);
    }
}
#else
struct PerlinEmbeddedFile {
    const char* name;
    const uint8_t* begin;
    const uint8_t* end;
};
extern "C" const PerlinEmbeddedFile perlin_embedded_files[];
extern "C" const size_t perlin_embedded_count;

namespace perlin {
    namespace {
        bool Find(const char* name, const uint8_t** data, size_t* size) {
            for (size_t i = 0; i < perlin_embedded_count; i++) {
                if (std::strcmp(perlin_embedded_files[i].name, name) == 0) {
                    *data = perlin_embedded_files[i].begin;
                    *size =
                        static_cast<size_t>(perlin_embedded_files[i].end - perlin_embedded_files[i].begin);
                    return true;
                }
            }
            return false;
        }
    }
    void RegisterEmbeddedResources() {
        resources::SetProvider(&Find);
    }
}
#endif
