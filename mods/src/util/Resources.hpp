#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace perlin::resources {
    using Provider = bool (*)(const char* name, const uint8_t** data, size_t* size);
    void SetProvider(Provider provider);

    bool Read(const std::string& name, std::string& out);
    std::string ReadText(const std::string& name);
}
