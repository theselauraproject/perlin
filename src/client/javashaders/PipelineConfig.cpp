#include "client/javashaders/PipelineConfig.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <cstdlib>
#include <cctype>

namespace perlin::jshader {
    namespace {
        bool IsTrue(const std::string& v) {
            const std::string l = str::Lower(v);
            return l == "true" || l == "on" || l == "1" || l == "yes";
        }

        int BufferIndex(const std::string& name) {
            static const char* const prefixes[] = {"colortex", "gaux", "composite"};
            if (name.compare(0, 8, "colortex") == 0) {
                const std::string n = name.substr(8);
                if (!n.empty() &&
                    std::all_of(n.begin(), n.end(), [](unsigned char c) { return std::isdigit(c); }))
                    return std::atoi(n.c_str());
            }
            if (name.compare(0, 4, "gaux") == 0 && name.size() == 5 &&
                std::isdigit(static_cast<unsigned char>(name[4])))
                return 3 + (name[4] - '0');
            if (name == "gcolor")
                return 0;
            if (name == "gdepth")
                return 1;
            (void)prefixes;
            return -1;
        }

        void ParseBlend(const std::string& value, BlendFactors& out) {
            const std::vector<std::string> w = SplitWords(value);
            out.set = true;
            if (w.empty() || str::Lower(w[0]) == "off") {
                out.off = true;
                return;
            }
            if (w.size() >= 2) {
                out.src = GlEnumFromName(w[0]);
                out.dst = GlEnumFromName(w[1]);
                out.srcA = w.size() >= 4 ? GlEnumFromName(w[2]) : out.src;
                out.dstA = w.size() >= 4 ? GlEnumFromName(w[3]) : out.dst;
            }
        }
    }

    int GlEnumFromName(const std::string& n) {
        static const std::map<std::string, int> table = {{"ZERO", 0},
                                                         {"ONE", 1},
                                                         {"SRC_COLOR", 0x300},
                                                         {"ONE_MINUS_SRC_COLOR", 0x301},
                                                         {"SRC_ALPHA", 0x302},
                                                         {"ONE_MINUS_SRC_ALPHA", 0x303},
                                                         {"DST_ALPHA", 0x304},
                                                         {"ONE_MINUS_DST_ALPHA", 0x305},
                                                         {"DST_COLOR", 0x306},
                                                         {"ONE_MINUS_DST_COLOR", 0x307},
                                                         {"SRC_ALPHA_SATURATE", 0x308},
                                                         {"NEVER", 0x200},
                                                         {"LESS", 0x201},
                                                         {"EQUAL", 0x202},
                                                         {"LEQUAL", 0x203},
                                                         {"GREATER", 0x204},
                                                         {"NOTEQUAL", 0x205},
                                                         {"GEQUAL", 0x206},
                                                         {"ALWAYS", 0x207}};
        std::string key = n;
        if (key.compare(0, 3, "GL_") == 0)
            key = key.substr(3);
        for (char& c : key)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const auto it = table.find(key);
        if (it != table.end())
            return it->second;
        char* end = nullptr;
        const long v = std::strtol(n.c_str(), &end, 0);
        return end && *end == 0 ? static_cast<int>(v) : 1;
    }

    BufferFormat ParseBufferFormat(const std::string& text, BufferFormat fallback) {
        static const std::map<std::string, BufferFormat> table = {
            {"RGBA8", BufferFormat::RGBA8},
            {"RGBA16", BufferFormat::RGBA16},
            {"RGBA16F", BufferFormat::RGBA16F},
            {"RGBA32F", BufferFormat::RGBA32F},
            {"R8", BufferFormat::R8},
            {"RG8", BufferFormat::RG8},
            {"R16F", BufferFormat::R16F},
            {"RG16F", BufferFormat::RG16F},
            {"R32F", BufferFormat::R32F},
            {"RGB8", BufferFormat::RGB8},
            {"RGB16F", BufferFormat::RGB16F},
            {"R11F_G11F_B10F", BufferFormat::R11F_G11F_B10F},
            {"RGB10_A2", BufferFormat::RGB10_A2},
            {"RG16", BufferFormat::RG16},
            {"R16", BufferFormat::R16},
            {"RGBA8_SNORM", BufferFormat::RGBA8_SNORM},
            {"RGB16", BufferFormat::RGB16},
            {"RGBA32I", BufferFormat::RGBA32I},
            {"RG32F", BufferFormat::RG32F},
            {"RGB32F", BufferFormat::RGBA32F}};
        std::string k = text;
        for (char& c : k)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const auto it = table.find(k);
        return it == table.end() ? fallback : it->second;
    }

    std::string ImageFormatQualifier(const std::string& internalFormat) {
        const std::string f = str::Lower(internalFormat);
        if (f == "rgba" || f == "rgb" || f == "rgb8")
            return "rgba8";
        if (f == "rg")
            return "rg8";
        if (f == "red" || f == "r")
            return "r8";
        if (f == "rgb16f")
            return "rgba16f";
        if (f == "rgb32f")
            return "rgba32f";
        if (f == "rgb16")
            return "rgba16";
        return f;
    }

    PipelineConfig ParsePipelineConfig(ShaderPackInstance& pack) {
        PipelineConfig cfg;
        const Properties& p = pack.Props();
        const Directives& d = pack.GetDirectives();

        for (int i = 0; i < kMaxColorTex; i++) {
            const std::string n = "colortex" + std::to_string(i);
            BufferSetup& b = cfg.color[static_cast<size_t>(i)];
            if (i == 0)
                b.format = BufferFormat::RGBA8;
            b.format = ParseBufferFormat(d.Text(n + "Format"), b.format);
            b.clear = d.Bool(n + "Clear", true);
            const std::string cc = d.Text(n + "ClearColor");
            if (!cc.empty()) {
                std::string s = cc;
                const size_t o = s.find('(');
                if (o != std::string::npos)
                    s = s.substr(o + 1);
                size_t k = 0;
                for (int c = 0; c < 4 && k < s.size(); c++) {
                    b.clearColor[c] = static_cast<float>(std::atof(s.c_str() + k));
                    const size_t comma = s.find(',', k);
                    if (comma == std::string::npos)
                        break;
                    k = comma + 1;
                }
            }
            b.mipmap = d.Bool(n + "MipmapEnabled", false);
            b.linear = !d.Bool(n + "Nearest", false);
        }
        for (int i = 0; i < kMaxShadowColor; i++) {
            const std::string n = "shadowcolor" + std::to_string(i);
            cfg.shadowColorFormat[i] = ParseBufferFormat(d.Text(n + "Format"), BufferFormat::RGBA8);
            cfg.shadowColorClear[i] = d.Bool(n + "Clear", true);
            cfg.shadowColorLinear[i] = !d.Bool(n + "Nearest", false);
            const std::string t = "shadowtex" + std::to_string(i);
            cfg.shadowHardwareFiltering[i] = d.Bool(t + "HardwareFiltering", false) ||
                                             d.Bool("shadowHardwareFiltering", false) ||
                                             d.Bool("shadowHardwareFiltering" + std::to_string(i), false);
            cfg.shadowFilterLinear[i] = !d.Bool(t + "Nearest", false);
            cfg.shadowMipmap[i] = d.Bool(t + "Mipmap", false);
        }
        cfg.shadowMapResolution = static_cast<int>(d.Number("shadowMapResolution", 2048));
        cfg.shadowDistance = static_cast<float>(d.Number("shadowDistance", 128));
        cfg.shadowIntervalSize = static_cast<float>(d.Number("shadowIntervalSize", 2));
        cfg.sunPathRotation = static_cast<float>(d.Number("sunPathRotation", 0));
        cfg.eyeBrightnessHalflife = static_cast<float>(d.Number("eyeBrightnessHalflife", 10));
        cfg.centerDepthHalflife = static_cast<float>(d.Number("centerDepthHalflife", 1));
        cfg.wetnessHalflife = static_cast<float>(d.Number("wetnessHalflife", 600));
        cfg.drynessHalflife = static_cast<float>(d.Number("drynessHalflife", 200));
        cfg.ambientOcclusionLevel = static_cast<float>(d.Number("ambientOcclusionLevel", 1));
        cfg.depthMipmap = d.Bool("depthtex0MipmapEnabled", false);
        cfg.shadowMapResolution = std::clamp(cfg.shadowMapResolution, 128, 16384);

        cfg.oldLighting = p.Bool("oldLighting", false);
        cfg.oldHandLight = p.Bool("oldHandLight", true);
        cfg.separateAo = p.Bool("separateAo", false);
        cfg.underwaterOverlay = p.Bool("underwaterOverlay", true);
        cfg.sun = p.Bool("sun", true);
        cfg.moon = p.Bool("moon", true);
        cfg.vignette = p.Bool("vignette", true);
        cfg.rainDepth = p.Bool("rain.depth", false);
        cfg.beaconBeamDepth = p.Bool("beacon.beam.depth", false);
        cfg.dynamicHandLight = p.Bool("dynamicHandLight", true);
        cfg.frustumCulling = p.Bool("frustum.culling", true);
        cfg.shadowTranslucent = p.Bool("shadowTranslucent", true);
        cfg.clouds = str::Lower(p.Get("clouds", "default"));
        if (p.Has("texture.noise"))
            cfg.noiseTexture = p.Get("texture.noise");

        for (const auto& kv : p.entries) {
            const std::string& key = kv.first;
            const std::string& val = kv.second;
            if (key.compare(0, 8, "uniform.") == 0 || key.compare(0, 9, "variable.") == 0) {
                cfg.customUniformProps.push_back(kv);
                continue;
            }
            if (key.compare(0, 6, "blend.") == 0) {
                const std::string rest = key.substr(6);
                const size_t dot = rest.find('.');
                if (dot == std::string::npos)
                    ParseBlend(val, cfg.programs[rest].blend);
                else {
                    const int idx = BufferIndex(rest.substr(dot + 1));
                    if (idx >= 0)
                        ParseBlend(val, cfg.programs[rest.substr(0, dot)].blendPerBuffer[idx]);
                }
            } else if (key.compare(0, 10, "alphaTest.") == 0) {
                AlphaTest& a = cfg.programs[key.substr(10)].alpha;
                a.set = true;
                const auto w = SplitWords(val);
                if (w.empty() || str::Lower(w[0]) == "off")
                    a.off = true;
                else {
                    a.func = GlEnumFromName(w[0]);
                    if (w.size() > 1)
                        a.ref = static_cast<float>(std::atof(w[1].c_str()));
                }
            } else if (key.compare(0, 6, "scale.") == 0) {
                const std::string rest = key.substr(6);
                const size_t dot = rest.find('.');
                const std::string prog = dot == std::string::npos ? rest : rest.substr(0, dot);
                ProgramConfig& pc = cfg.programs[prog];
                const auto w = SplitWords(val);
                if (dot == std::string::npos) {
                    if (!w.empty()) {
                        pc.scale = static_cast<float>(std::atof(w[0].c_str()));
                        pc.hasScale = true;
                    }
                    if (w.size() >= 3) {
                        pc.offsetX = static_cast<float>(std::atof(w[1].c_str()));
                        pc.offsetY = static_cast<float>(std::atof(w[2].c_str()));
                    }
                }
            } else if (key.compare(0, 5, "flip.") == 0) {
                const std::string rest = key.substr(5);
                const size_t dot = rest.find('.');
                if (dot != std::string::npos) {
                    const int idx = BufferIndex(rest.substr(dot + 1));
                    if (idx >= 0)
                        cfg.programs[rest.substr(0, dot)].flip[idx] = IsTrue(val);
                }
            } else if (key.compare(0, 6, "image.") == 0 && key.size() > 6) {
                const auto w = SplitWords(val);
                if (w.size() < 7)
                    continue;
                CustomImage img;
                img.name = key.substr(6);
                img.sampler = w[0] == "none" ? std::string() : w[0];
                img.format = str::Lower(w[1]);
                img.internalFormat = str::Lower(w[2]);
                img.pixelType = str::Lower(w[3]);
                img.clear = IsTrue(w[4]);
                img.relative = IsTrue(w[5]);
                if (img.relative && w.size() >= 8) {
                    img.relativeWidth = static_cast<float>(std::atof(w[6].c_str()));
                    img.relativeHeight = static_cast<float>(std::atof(w[7].c_str()));
                } else if (!img.relative) {
                    img.dims = static_cast<int>(w.size()) - 6;
                    if (img.dims < 1 || img.dims > 3)
                        continue;
                    img.width = std::max(1, std::atoi(w[6].c_str()));
                    img.height = img.dims >= 2 ? std::max(1, std::atoi(w[7].c_str())) : 1;
                    img.depth = img.dims == 3 ? std::max(1, std::atoi(w[8].c_str())) : 1;
                }
                // a later definition (another #if branch) replaces an earlier one of the same name
                std::erase_if(cfg.images, [&](const CustomImage& o) { return o.name == img.name; });
                if (cfg.images.size() < 16)
                    cfg.images.push_back(std::move(img));
                else
                    cfg.problems.push_back("more than 16 custom images: " + key);
            } else if (key.compare(0, 13, "bufferObject.") == 0) {
                const auto w = SplitWords(val);
                if (w.empty())
                    continue;
                BufferObject b;
                b.index = std::atoi(key.c_str() + 13);
                b.size = std::strtoull(w[0].c_str(), nullptr, 10);
                if (w.size() >= 4) {
                    b.relative = IsTrue(w[1]);
                    b.scaleX = static_cast<float>(std::atof(w[2].c_str()));
                    b.scaleY = static_cast<float>(std::atof(w[3].c_str()));
                }
                if (b.index < 0 || b.index > 12 || b.size == 0)
                    continue;
                std::erase_if(cfg.bufferObjects, [&](const BufferObject& o) { return o.index == b.index; });
                cfg.bufferObjects.push_back(b);
            } else if (key.compare(0, 12, "size.buffer.") == 0) {
                const int idx = BufferIndex(key.substr(12));
                const auto w = SplitWords(val);
                if (idx >= 0 && idx < kMaxColorTex && w.size() >= 2) {
                    BufferSetup& b = cfg.color[static_cast<size_t>(idx)];
                    // a fraction is a scale of the screen (iris), a whole number a size in pixels
                    const auto relative = [](const std::string& t) {
                        return t.find('.') != std::string::npos;
                    };
                    if (relative(w[0]) || relative(w[1])) {
                        b.scaleX = static_cast<float>(std::atof(w[0].c_str()));
                        b.scaleY = static_cast<float>(std::atof(w[1].c_str()));
                    } else {
                        b.fixedWidth = std::atoi(w[0].c_str());
                        b.fixedHeight = std::atoi(w[1].c_str());
                    }
                }
            } else if (key.compare(0, 8, "texture.") == 0 && key != "texture.noise") {
                const std::string rest = key.substr(8);
                const size_t d1 = rest.find('.');
                if (d1 == std::string::npos)
                    continue;
                CustomTexture t;
                t.stage = rest.substr(0, d1);
                t.sampler = rest.substr(d1 + 1);
                const auto w = SplitWords(val);
                if (w.empty())
                    continue;
                t.path = w[0];
                if (w.size() > 1)
                    t.type = w[1];
                cfg.textures.push_back(std::move(t));
            }
        }
        for (const char* s : {"shadow"})
            if (pack.ProgramExists(s) && pack.ProgramEnabled(s))
                cfg.shadowEnabled = true;
        return cfg;
    }
}
