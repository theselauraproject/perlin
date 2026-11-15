#include "client/systems/javashaders/ShaderUsage.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace perlin::jshader;

namespace {
    int failures = 0;

    void Check(bool ok, const std::string& what) {
        if (!ok) {
            std::printf("FAIL %s\n", what.c_str());
            failures++;
        }
    }

    struct Reference {
        bool colorTex[kMaxColorTex] = {};
        bool shadowColor[2] = {};
        bool shadow = false;
        bool depth1 = false, depth2 = false;
        bool image[kMaxColorTex] = {};
    };

    Reference ReferenceScan(const std::string& t) {
        Reference r;
        const size_t n = t.size();
        for (size_t i = 0; i < n;) {
            const unsigned char c = static_cast<unsigned char>(t[i]);
            if (!(std::isalpha(c) || c == '_')) {
                i++;
                continue;
            }
            size_t e = i + 1;
            while (e < n && (std::isalnum(static_cast<unsigned char>(t[e])) || t[e] == '_'))
                e++;
            const size_t len = e - i;
            if (len >= 5 && len <= 12) {
                const char* w = t.data() + i;
                const auto digits = [&](size_t prefix) {
                    return len > prefix &&
                           std::all_of(w + prefix, w + len, [](char d) { return d >= '0' && d <= '9'; });
                };
                if (len > 8 && std::strncmp(w, "colortex", 8) == 0 && digits(8)) {
                    const int idx = std::atoi(std::string(w + 8, len - 8).c_str());
                    if (idx < kMaxColorTex)
                        r.colorTex[idx] = true;
                } else if (len == 5 && std::strncmp(w, "gaux", 4) == 0 && w[4] >= '1' && w[4] <= '4') {
                    r.colorTex[3 + (w[4] - '0')] = true;
                } else if (len > 11 && std::strncmp(w, "shadowcolor", 11) == 0 && digits(11)) {
                    const int idx = std::atoi(std::string(w + 11, len - 11).c_str());
                    if (idx < 2) {
                        r.shadowColor[idx] = true;
                        r.shadow = true;
                    }
                } else if (len == 6 && std::strncmp(w, "gcolor", 6) == 0) {
                    r.colorTex[0] = true;
                } else if (len == 6 && std::strncmp(w, "gdepth", 6) == 0) {
                    r.colorTex[1] = true;
                } else if (len == 11 && std::strncmp(w, "shadowcolor", 11) == 0) {
                    r.shadowColor[0] = true;
                    r.shadow = true;
                } else if ((len == 9 && std::strncmp(w, "shadowtex", 9) == 0) ||
                           ((len == 10 || len == 12) && std::strncmp(w, "shadowtex", 9) == 0 &&
                            (w[9] == '0' || w[9] == '1')) ||
                           (len == 6 && std::strncmp(w, "shadow", 6) == 0)) {
                    r.shadow = true;
                }
            }
            i = e;
        }
        r.depth1 = t.find("depthtex1") != std::string::npos;
        r.depth2 = t.find("depthtex2") != std::string::npos;
        for (size_t at = t.find("colorimg"); at != std::string::npos; at = t.find("colorimg", at + 8)) {
            if (at > 0 && (std::isalnum(static_cast<unsigned char>(t[at - 1])) || t[at - 1] == '_'))
                continue;
            size_t e = at + 8;
            while (e < t.size() && std::isdigit(static_cast<unsigned char>(t[e])))
                e++;
            if (e == at + 8 ||
                (e < t.size() && (std::isalnum(static_cast<unsigned char>(t[e])) || t[e] == '_')))
                continue;
            const int idx = std::atoi(t.substr(at + 8, e - at - 8).c_str());
            if (idx >= 0 && idx < kMaxColorTex)
                r.image[idx] = true;
        }
        return r;
    }

    bool Same(const Reference& r, const ShaderUsage& u) {
        for (int i = 0; i < kMaxColorTex; i++)
            if (r.colorTex[i] != u.colorTex[static_cast<size_t>(i)] ||
                r.image[i] != u.colorImage[static_cast<size_t>(i)])
                return false;
        return r.shadowColor[0] == u.shadowColor[0] && r.shadowColor[1] == u.shadowColor[1] &&
               r.shadow == u.shadow && r.depth1 == u.depthTex1 && r.depth2 == u.depthTex2;
    }

    void Differential(const std::string& text, const std::string& label) {
        Check(Same(ReferenceScan(text), ScanShaderText(text)), "matches original: " + label);
    }
}

int main() {
    const std::vector<std::string> corpus = {
        "",
        "uniform sampler2D colortex0, colortex7;",
        "uniform sampler2D colortex15; colortex16 colortex99999999999",
        "gcolor gdepth gaux1 gaux2 gaux3 gaux4 gaux5 gaux0",
        "shadowcolor0 shadowcolor1 shadowcolor2 shadowcolor",
        "shadowtex0 shadowtex1 shadowtex0HW shadowtex1HW shadowtex2 shadowtex",
        "shadow shadows myshadow shadow_ x_shadow",
        "depthtex0 depthtex1 depthtex2",
        "layout(rgba8) uniform image2D colorimg3; colorimg12 xcolorimg4 colorimg5x colorimg",
        "2colortex3 colortex 9colortex1",
        "colortex8colortex9 colortex3\ncolortex4;colortex5",
    };
    for (size_t i = 0; i < corpus.size(); i++)
        Differential(corpus[i], "corpus " + std::to_string(i));

    const std::vector<std::string> vocabulary = {
        "colortex", "gaux", "gcolor", "gdepth", "shadowcolor", "shadowtex", "shadow", "colorimg",
        "depthtex", "0",    "1",      "2",      "3",           "7",         "12",     "17",
        "HW",       "_",    " ",      "\n",     ";",           "x",         "9",      "watercolor"};
    std::mt19937 rng(12345);
    for (int trial = 0; trial < 20000; trial++) {
        std::string text;
        const int pieces = static_cast<int>(rng() % 12);
        for (int p = 0; p < pieces; p++)
            text += vocabulary[rng() % vocabulary.size()];
        Differential(text, "fuzz '" + text + "'");
        if (failures > 10)
            break;
    }

    const ShaderUsage water = ScanShaderText("uniform sampler2DShadow watershadow;");
    Check(water.shadow, "watershadow is recognised (intentional fix: original never matched it)");
    Check(!ScanShaderText("uniform sampler2D noise;").shadow, "unrelated source does not use shadows");

    ShaderUsage merged;
    merged.Merge(ScanShaderText("colortex2"));
    merged.Merge(ScanShaderText("colorimg4 depthtex1"));
    Check(merged.colorTex[2] && merged.colorImage[4] && merged.depthTex1 && !merged.depthTex2,
          "merge accumulates");

    if (failures == 0)
        std::printf("shader_usage_selftest OK\n");
    return failures == 0 ? 0 : 1;
}
