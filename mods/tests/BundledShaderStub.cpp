#include "client/resources/BundledShaderPack.hpp"

namespace perlin {
    const char* BundledShaderMeta() {
        return "";
    }
    bool BundledShaderBytes(const std::string&, const std::string&, const uint8_t*&, size_t&) {
        return false;
    }
}
