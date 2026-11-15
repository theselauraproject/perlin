#include "client/systems/javashaders/PackTextureUtil.hpp"

#include <cstdio>
#include <random>
#include <string>

using namespace perlin::jshader;

namespace {
    int failures = 0;

    void Check(bool ok, const std::string& what) {
        if (!ok) {
            std::printf("FAIL %s\n", what.c_str());
            failures++;
        }
    }

    bool ReferenceFlag(const std::string& meta, const char* key) {
        const size_t at = meta.find(std::string("\"") + key + "\"");
        if (at == std::string::npos)
            return false;
        const size_t colon = meta.find(':', at);
        return colon != std::string::npos && meta.find("true", colon) != std::string::npos &&
               meta.find("true", colon) < meta.find_first_of(",}", colon);
    }

    std::string ReferenceStrip(std::string clean) {
        while (!clean.empty() && clean[0] == '/')
            clean.erase(0, 1);
        return clean;
    }

    std::vector<uint8_t> ReferenceNoise(int res) {
        std::vector<uint8_t> px(static_cast<size_t>(res) * res * 4);
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

int main() {
    Check(ParseSamplerFlags("{\"texture\":{\"blur\":true,\"clamp\":false}}") == SamplerFlags{true, false},
          "blur only");
    Check(ParseSamplerFlags("{\"texture\":{\"blur\":false,\"clamp\":true}}") == SamplerFlags{false, true},
          "clamp only");
    Check(ParseSamplerFlags("") == SamplerFlags{}, "empty metadata");
    Check(ParseSamplerFlags("{\"blur\":false,\"clamp\":true}") == SamplerFlags{false, true},
          "later true does not leak into earlier key");

    const std::vector<std::string> pieces = {"\"blur\"", "\"clamp\"", ":", "true", "false",
                                             ",",        "}",         "{", " ",    "x"};
    std::mt19937 rng(99);
    for (int trial = 0; trial < 20000; trial++) {
        std::string meta;
        for (int i = 0, n = static_cast<int>(rng() % 10); i < n; i++)
            meta += pieces[rng() % pieces.size()];
        const SamplerFlags flags = ParseSamplerFlags(meta);
        Check(flags.blur == ReferenceFlag(meta, "blur") && flags.clamp == ReferenceFlag(meta, "clamp"),
              "flags match original for " + meta);
        if (failures > 10)
            break;
    }

    for (const std::string& path : {std::string(), std::string("/"), std::string("///a/b.png"),
                                    std::string("a//b"), std::string("noise.png")})
        Check(StripLeadingSlashes(path) == ReferenceStrip(path), "strip matches original for '" + path + "'");

    for (int res : {1, 2, 16, 64})
        Check(GenerateNoise(res) == ReferenceNoise(res), "noise matches original");
    const auto noise = GenerateNoise(8);
    bool opaque = true;
    for (size_t i = 3; i < noise.size(); i += 4)
        opaque = opaque && noise[i] == 255;
    Check(opaque, "noise alpha is opaque");

    if (failures == 0)
        std::printf("pack_texture_util_selftest OK\n");
    return failures == 0 ? 0 : 1;
}
