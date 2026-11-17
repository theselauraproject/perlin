#include "client/systems/javashaders/PackTextureUtil.hpp"

namespace perlin::jshader {
    namespace {
        bool FlagIsTrue(std::string_view meta, std::string_view key) {
            const std::string quoted = "\"" + std::string(key) + "\"";
            const size_t at = meta.find(quoted);
            if (at == std::string_view::npos)
                return false;
            const size_t colon = meta.find(':', at);
            if (colon == std::string_view::npos)
                return false;
            const size_t value = meta.find("true", colon);
            return value != std::string_view::npos && value < meta.find_first_of(",}", colon);
        }
    }

    SamplerFlags ParseSamplerFlags(std::string_view mcmeta) {
        return {FlagIsTrue(mcmeta, "blur"), FlagIsTrue(mcmeta, "clamp")};
    }

    std::string StripLeadingSlashes(std::string_view path) {
        const size_t first = path.find_first_not_of('/');
        return std::string(first == std::string_view::npos ? std::string_view() : path.substr(first));
    }

    std::vector<uint8_t> GenerateNoise(int resolution) {
        std::vector<uint8_t> px(static_cast<size_t>(resolution) * static_cast<size_t>(resolution) * 4);
        uint64_t state = 0x9E3779B97F4A7C15ull;
        for (size_t i = 0; i < px.size(); i++) {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            px[i] = (i % 4 == 3) ? 255 : static_cast<uint8_t>(state >> 56);
        }
        return px;
    }

}
