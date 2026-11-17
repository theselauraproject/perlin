#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace perlin {
    inline constexpr const char* kBundledPackName = "Vibrant Visuals";
    inline bool IsBundledName(const std::string& name) {
        std::string lower;
        for (char c : name)
            lower += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        return lower == "vibrant visuals";
    }
    const char* BundledShaderMeta();
    bool BundledShaderBytes(const std::string& backend, const std::string& shader, const uint8_t*& data,
                            size_t& size);
}
