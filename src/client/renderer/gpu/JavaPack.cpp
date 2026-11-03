#include "client/renderer/gpu/JavaPack.hpp"
#include "client/javashaders/Biomes.hpp"
#include "util/Log.hpp"

#include "client/javashaders/GlslangBridge.hpp"
#include "client/javashaders/PackSource.hpp"
#include "util/ThreadPool.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
// private copy of the decoder: the app and the headless test each link a different set of the engine
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>

namespace perlin::gpu {
    namespace {
        // converted programs are kept on disk: glslang, the cross compiler and fxc take seconds per program for a big pack,
        // and nothing about them changes between runs
        constexpr uint32_t kCacheMagic = 0x4a504b34u;
        constexpr int kCacheVersion = 6;

        struct CachedProgram {
            std::vector<jshader::UniformDecl> uniforms;
            std::vector<int> drawBuffers;
            std::vector<jshader::RhiSampler> samplers;
            std::vector<std::string> images;
            int uniformRegs = 0;
            std::vector<uint8_t> vertex, fragment;
        };

        uint64_t HashBytes(uint64_t h, const void* data, size_t n) {
            const unsigned char* p = static_cast<const unsigned char*>(data);
            for (size_t i = 0; i < n; i++) {
                h ^= p[i];
                h *= 1099511628211ull;
            }
            return h;
        }
        uint64_t HashString(uint64_t h, const std::string& s) {
            h = HashBytes(h, s.data(), s.size());
            const uint64_t n = s.size();
            return HashBytes(h, &n, sizeof(n));
        }

        std::filesystem::path CacheFile(const jshader::ExpandedSource& vs, const jshader::ExpandedSource& fs,
                                        const jshader::TransformOptions& o, int kind,
                                        const std::string& name) {
            uint64_t h = 1469598103934665603ull;
            h = HashString(h, vs.text);
            h = HashString(h, fs.text);
            for (const auto& m : o.macros) {
                h = HashString(h, m.first);
                h = HashString(h, m.second);
            }
            const int flags[] = {kCacheVersion,    kind,
                                 o.flipVertexY,    o.flipFragCoordY,
                                 o.zeroToOneDepth, o.splitSamplers,
                                 o.explicitLod,    static_cast<int>(o.availableAttribs)};
            h = HashBytes(h, flags, sizeof(flags));
            h = HashString(h, o.alphaTest);
            for (const auto& [img, slot] : o.imageSlots) {
                h = HashString(h, img);
                h = HashBytes(h, &slot, sizeof(slot));
            }
            for (const auto& [img, fmt] : o.imageFormats)
                h = HashString(h, img + "=" + fmt);
            h = HashString(h, __DATE__ " " __TIME__);
            try {
                const std::filesystem::path dir =
                    std::filesystem::temp_directory_path() / "perlin_javapack_cache";
                std::filesystem::create_directories(dir);
                char file[64];
                std::snprintf(file, sizeof(file), "%016llx.prog", static_cast<unsigned long long>(h));
                (void)name;
                return dir / file;
            } catch (...) {
                return {};
            }
        }

        template <class T> void Put(std::string& out, const T& v) {
            out.append(reinterpret_cast<const char*>(&v), sizeof(T));
        }
        void PutString(std::string& out, const std::string& s) {
            Put<uint32_t>(out, static_cast<uint32_t>(s.size()));
            out += s;
        }
        struct Reader {
            const std::string& d;
            size_t at = 0;
            bool ok = true;
            template <class T> T Get() {
                T v{};
                if (at + sizeof(T) > d.size()) {
                    ok = false;
                    return v;
                }
                std::memcpy(&v, d.data() + at, sizeof(T));
                at += sizeof(T);
                return v;
            }
            std::string Str() {
                const uint32_t n = Get<uint32_t>();
                if (!ok || at + n > d.size()) {
                    ok = false;
                    return {};
                }
                std::string s = d.substr(at, n);
                at += n;
                return s;
            }
        };

        void SaveCache(const std::filesystem::path& file, const CachedProgram& c) {
            if (file.empty())
                return;
            std::string out;
            Put<uint32_t>(out, kCacheMagic);
            Put<int32_t>(out, c.uniformRegs);
            Put<uint32_t>(out, static_cast<uint32_t>(c.uniforms.size()));
            for (const auto& u : c.uniforms) {
                PutString(out, u.name);
                PutString(out, u.type);
                PutString(out, u.arraySize);
                Put<int32_t>(out, u.arrayCount);
                Put<int32_t>(out, u.reg);
                Put<int32_t>(out, u.regs);
                Put<int32_t>(out, u.regsPerElement);
                Put<int32_t>(out, u.components);
            }
            Put<uint32_t>(out, static_cast<uint32_t>(c.drawBuffers.size()));
            for (int b : c.drawBuffers)
                Put<int32_t>(out, b);
            Put<uint32_t>(out, static_cast<uint32_t>(c.samplers.size()));
            for (const auto& s : c.samplers) {
                PutString(out, s.name);
                Put<int32_t>(out, s.slot);
                Put<uint8_t>(out, s.shadow ? 1 : 0);
            }
            Put<uint32_t>(out, static_cast<uint32_t>(c.images.size()));
            for (const auto& img : c.images)
                PutString(out, img);
            PutString(out, std::string(c.vertex.begin(), c.vertex.end()));
            PutString(out, std::string(c.fragment.begin(), c.fragment.end()));
            try {
                const std::filesystem::path tmp = file.string() + ".tmp";
                {
                    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                    f.write(out.data(), static_cast<std::streamsize>(out.size()));
                }
                std::filesystem::rename(tmp, file);
            } catch (...) {}
        }

        bool LoadCache(const std::filesystem::path& file, CachedProgram& c) {
            if (file.empty())
                return false;
            std::string data;
            try {
                std::ifstream f(file, std::ios::binary);
                if (!f)
                    return false;
                data.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            } catch (...) {
                return false;
            }
            Reader r{data};
            if (r.Get<uint32_t>() != kCacheMagic)
                return false;
            c.uniformRegs = r.Get<int32_t>();
            const uint32_t nu = r.Get<uint32_t>();
            for (uint32_t i = 0; i < nu && r.ok; i++) {
                jshader::UniformDecl u;
                u.name = r.Str();
                u.type = r.Str();
                u.arraySize = r.Str();
                u.arrayCount = r.Get<int32_t>();
                u.reg = r.Get<int32_t>();
                u.regs = r.Get<int32_t>();
                u.regsPerElement = r.Get<int32_t>();
                u.components = r.Get<int32_t>();
                c.uniforms.push_back(std::move(u));
            }
            const uint32_t nb = r.Get<uint32_t>();
            for (uint32_t i = 0; i < nb && r.ok; i++)
                c.drawBuffers.push_back(r.Get<int32_t>());
            const uint32_t ns = r.Get<uint32_t>();
            for (uint32_t i = 0; i < ns && r.ok; i++) {
                jshader::RhiSampler s;
                s.name = r.Str();
                s.slot = r.Get<int32_t>();
                s.shadow = r.Get<uint8_t>() != 0;
                c.samplers.push_back(std::move(s));
            }
            const uint32_t ni = r.Get<uint32_t>();
            for (uint32_t i = 0; i < ni && r.ok; i++)
                c.images.push_back(r.Str());
            const std::string vs = r.Str(), fs = r.Str();
            c.vertex.assign(vs.begin(), vs.end());
            c.fragment.assign(fs.begin(), fs.end());
            return r.ok && !c.vertex.empty() && !c.fragment.empty();
        }
    }
}

namespace perlin::gpu {
    namespace {
        constexpr double kPi = 3.14159265358979323846;

        // vertex stream of the pack, 72 bytes: position, abgr, uv, light uv, normal (half4), mc_Entity, mid uv, midBlock (half4), tangent (half4)
        enum PackOffset : uint32_t {
            kPos = 0,
            kColor = 12,
            kUv = 16,
            kLight = 24,
            kNormal = 32,
            kEntity = 40,
            kMid = 48,
            kMidBlock = 56,
            kTangent = 64
        };

        uint16_t ToHalf(float f) {
            uint32_t x;
            std::memcpy(&x, &f, 4);
            const uint32_t sign = (x >> 16) & 0x8000u;
            int32_t exp = static_cast<int32_t>((x >> 23) & 0xFF) - 127 + 15;
            uint32_t mant = x & 0x7FFFFFu;
            if (exp <= 0) {
                if (exp < -10)
                    return static_cast<uint16_t>(sign);
                mant |= 0x800000u;
                const uint32_t shift = static_cast<uint32_t>(14 - exp);
                return static_cast<uint16_t>(sign | ((mant + (1u << (shift - 1))) >> shift));
            }
            if (exp >= 31)
                return static_cast<uint16_t>(sign | 0x7BFFu);
            return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) |
                                         ((mant + 0x1000u) >> 13));
        }

        glm::mat4 ToMat(const float* m) {
            glm::mat4 out;
            std::memcpy(&out[0][0], m, sizeof(float) * 16);
            return out;
        }
        void FromMat(const glm::mat4& m, float* out) {
            std::memcpy(out, &m[0][0], sizeof(float) * 16);
        }

        // the engine's clip z is 0..1; the pack's matrices are opengl, -1..1
        void ToGlDepth(const float* in, float* out) {
            std::memcpy(out, in, sizeof(float) * 16);
            for (int c = 0; c < 4; c++)
                out[c * 4 + 2] = 2.0f * in[c * 4 + 2] - in[c * 4 + 3];
        }

        // bedrock names for blocks the java packs list under their old ids
        const char* Alias(const std::string& n) {
            static const std::pair<const char*, const char*> table[] = {
                {"flowing_water", "water"},
                {"flowing_lava", "lava"},
                {"tallgrass", "short_grass"},
                {"grass", "grass_block"},
                {"web", "cobweb"},
                {"yellow_flower", "dandelion"},
                {"red_flower", "poppy"},
                {"reeds", "sugar_cane"},
                {"deadbush", "dead_bush"},
                {"waterlily", "lily_pad"},
                {"snow_layer", "snow"},
                {"lit_furnace", "furnace"},
                {"lit_redstone_lamp", "redstone_lamp"},
                {"lit_redstone_ore", "redstone_ore"},
                {"portal", "nether_portal"},
                {"sea_lantern", "sea_lantern"},
                {"mob_spawner", "spawner"},
                {"stonebrick", "stone_bricks"},
                {"wooden_slab", "oak_slab"},
                {"double_plant", "tall_grass"},
                {"concretePowder", "white_concrete_powder"},
                {"leaves", "oak_leaves"},
                {"leaves2", "acacia_leaves"},
                {"log", "oak_log"},
                {"log2", "acacia_log"}};
            for (const auto& e : table)
                if (n == e.first)
                    return e.second;
            return nullptr;
        }

        // alpha weighted so transparent texels do not darken the edges of cut-out tiles
        void BoxDown(const uint8_t* src, int sw, int sh, int sstride, uint8_t* dst, int dstride) {
            const int dw = std::max(1, sw / 2), dh = std::max(1, sh / 2);
            for (int y = 0; y < dh; y++)
                for (int x = 0; x < dw; x++) {
                    int sum[4] = {0, 0, 0, 0}, alphaSum = 0;
                    for (int j = 0; j < 2; j++)
                        for (int i = 0; i < 2; i++) {
                            const int sx = std::min(sw - 1, x * 2 + i), sy = std::min(sh - 1, y * 2 + j);
                            const uint8_t* p =
                                src + static_cast<size_t>(sy) * sstride + static_cast<size_t>(sx) * 4;
                            sum[0] += p[0] * p[3];
                            sum[1] += p[1] * p[3];
                            sum[2] += p[2] * p[3];
                            sum[3] += p[3];
                            alphaSum += p[3];
                        }
                    uint8_t* d = dst + static_cast<size_t>(y) * dstride + static_cast<size_t>(x) * 4;
                    if (alphaSum > 0) {
                        d[0] = static_cast<uint8_t>(sum[0] / alphaSum);
                        d[1] = static_cast<uint8_t>(sum[1] / alphaSum);
                        d[2] = static_cast<uint8_t>(sum[2] / alphaSum);
                    } else {
                        d[0] = d[1] = d[2] = 0;
                    }
                    d[3] = static_cast<uint8_t>(sum[3] / 4);
                }
        }

        // iris internal format names of custom images; integer formats sample nearest
        rhi::Format ImageFormat(const std::string& internal, bool& integer) {
            static const std::map<std::string, std::pair<rhi::Format, bool>> formats = {
                {"rgba8", {rhi::Format::RGBA8Unorm, false}},
                {"rgba", {rhi::Format::RGBA8Unorm, false}},
                {"rgb8", {rhi::Format::RGBA8Unorm, false}},
                {"rg8", {rhi::Format::RG8Unorm, false}},
                {"r8", {rhi::Format::R8Unorm, false}},
                {"rgba16f", {rhi::Format::RGBA16Float, false}},
                {"rgb16f", {rhi::Format::RGBA16Float, false}},
                {"rg16f", {rhi::Format::RG16Float, false}},
                {"r16f", {rhi::Format::R16Float, false}},
                {"rgba32f", {rhi::Format::RGBA32Float, false}},
                {"rgb32f", {rhi::Format::RGBA32Float, false}},
                {"rg32f", {rhi::Format::RG32Float, false}},
                {"r32f", {rhi::Format::R32Float, false}},
                {"rgba16", {rhi::Format::RGBA16Unorm, false}},
                {"rg16", {rhi::Format::RG16Unorm, false}},
                {"rgb10_a2", {rhi::Format::RGB10A2Unorm, false}},
                {"r11f_g11f_b10f", {rhi::Format::R11G11B10Float, false}},
                {"r8ui", {rhi::Format::R8Uint, true}},
                {"r16ui", {rhi::Format::R16Uint, true}},
                {"r32ui", {rhi::Format::R32Uint, true}},
                {"rg32ui", {rhi::Format::RG32Uint, true}},
                {"rgba32ui", {rhi::Format::RGBA32Uint, true}},
                {"r32i", {rhi::Format::R32Uint, true}},
                {"rgba32i", {rhi::Format::RGBA32Uint, true}}};
            const auto it = formats.find(jshader::ImageFormatQualifier(internal));
            if (it == formats.end()) {
                integer = false;
                return rhi::Format::Unknown;
            }
            integer = it->second.second;
            return it->second.first;
        }

        rhi::Format ToFormat(jshader::BufferFormat f) {
            using B = jshader::BufferFormat;
            switch (f) {
            case B::RGBA8:
                return rhi::Format::RGBA8Unorm;
            case B::RGBA16:
                return rhi::Format::RGBA16Unorm;
            case B::RGBA16F:
                return rhi::Format::RGBA16Float;
            case B::RGBA32F:
                return rhi::Format::RGBA32Float;
            case B::R8:
                return rhi::Format::R8Unorm;
            case B::RG8:
                return rhi::Format::RG8Unorm;
            case B::R16F:
                return rhi::Format::R16Float;
            case B::RG16F:
                return rhi::Format::RG16Float;
            case B::R32F:
                return rhi::Format::R32Float;
            // no three-channel render targets, no signed or integer ones: the nearest wider format keeps the values
            case B::RGB8:
                return rhi::Format::RGBA8Unorm;
            case B::RGB16F:
                return rhi::Format::RGBA16Float;
            case B::R11F_G11F_B10F:
                return rhi::Format::R11G11B10Float;
            case B::RGB10_A2:
                return rhi::Format::RGB10A2Unorm;
            case B::RG16:
                return rhi::Format::RG16Unorm;
            case B::R16:
                return rhi::Format::R16Float;
            case B::RGBA8_SNORM:
                return rhi::Format::RGBA16Float;
            case B::RGB16:
                return rhi::Format::RGBA16Unorm;
            case B::RGBA32I:
                return rhi::Format::RGBA32Float;
            case B::RG32F:
                return rhi::Format::RG32Float;
            }
            return rhi::Format::RGBA8Unorm;
        }

        rhi::BlendFactor GlFactor(int gl) {
            using F = rhi::BlendFactor;
            switch (gl) {
            case 0:
                return F::Zero;
            case 1:
                return F::One;
            case 0x300:
                return F::SrcColor;
            case 0x301:
                return F::InvSrcColor;
            case 0x302:
                return F::SrcAlpha;
            case 0x303:
                return F::InvSrcAlpha;
            case 0x304:
                return F::DstAlpha;
            case 0x305:
                return F::InvDstAlpha;
            case 0x306:
                return F::DstColor;
            case 0x307:
                return F::InvDstColor;
            case 0x308:
                return F::SrcAlphaSaturate;
            }
            return F::One;
        }

        jshader::ExpandedSource MakeSource(const char* text) {
            jshader::ExpandedSource s;
            s.text = text;
            const size_t lines = static_cast<size_t>(std::count(s.text.begin(), s.text.end(), '\n')) + 1;
            s.origins.assign(lines, jshader::LineOrigin{"builtin", 0});
            s.files.insert("builtin");
            return s;
        }

        bool IdentChar(char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        void PatchShadowResolution(std::string& text, int res) {
            static constexpr std::string_view kName = "shadowMapResolution";
            const std::string value = std::to_string(res);
            size_t at = 0;
            while ((at = text.find(kName, at)) != std::string::npos) {
                const size_t end = at + kName.size();
                if ((at > 0 && IdentChar(text[at - 1])) || (end < text.size() && IdentChar(text[end]))) {
                    at = end;
                    continue;
                }
                size_t lineStart = text.rfind('\n', at);
                lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
                const std::string_view head(text.data() + lineStart, at - lineStart);
                const size_t constAt = head.find("const");
                const size_t intAt = head.find("int");
                size_t eq = end;
                while (eq < text.size() && (text[eq] == ' ' || text[eq] == '\t'))
                    eq++;
                const size_t semi = text.find(';', eq);
                const size_t newline = text.find('\n', eq);
                if (constAt == std::string_view::npos || intAt == std::string_view::npos || eq >= text.size() ||
                    text[eq] != '=' || semi == std::string::npos ||
                    (newline != std::string::npos && newline < semi)) {
                    at = end;
                    continue;
                }
                text.replace(eq + 1, semi - eq - 1, " " + value);
                at = eq + 1 + value.size() + 1;
            }
        }

        const char* kFullscreenVertex = "#version 120\nvarying vec2 texcoord;\nvoid main() { gl_Position = "
                                        "ftransform(); texcoord = gl_MultiTexCoord0.xy; }\n";
        // the engine's depth is reversed (1 near, 0 far): the pack's conventional depth goes back as 1 - d
        const char* kReverseDepthFragment =
            "#version 120\nuniform sampler2D depthtex0;\nvarying vec2 texcoord;\nvoid main() { gl_FragDepth "
            "= 1.0 - texture2D(depthtex0, texcoord).r; }\n";
        const char* kEmptyFragment = "#version 120\nvoid main() {}\n";
        const char* kCopyColorFragment =
            "#version 120\nuniform sampler2D colortex0;\nvarying vec2 texcoord;\n/* DRAWBUFFERS:0 */\nvoid "
            "main() { gl_FragData[0] = texture2D(colortex0, texcoord); }\n";

        // iris texture stages: which texture.<stage>.* lines a program sees
        std::string TextureStageOf(const std::string& program) {
            const auto starts = [&](const char* p) { return program.compare(0, std::strlen(p), p) == 0; };
            if (starts("shadowcomp"))
                return "shadowcomp";
            if (starts("gbuffers") || starts("shadow") || starts("dh_"))
                return "gbuffers";
            if (starts("deferred"))
                return "deferred";
            if (starts("composite") || starts("final"))
                return "composite";
            if (starts("prepare"))
                return "prepare";
            if (starts("begin"))
                return "begin";
            if (starts("setup"))
                return "setup";
            return "composite";
        }

        // the legacy buffer names stand for colortex0..7
        std::string CanonicalSamplerName(const std::string& n) {
            static const std::pair<const char*, const char*> table[] = {
                {"gcolor", "colortex0"},    {"gdepth", "colortex1"}, {"gnormal", "colortex2"},
                {"composite", "colortex3"}, {"gaux1", "colortex4"},  {"gaux2", "colortex5"},
                {"gaux3", "colortex6"},     {"gaux4", "colortex7"}};
            for (const auto& e : table)
                if (n == e.first)
                    return e.second;
            return n;
        }

        enum SamplerId : int {
            SPointClamp,
            SLinearClamp,
            SLinearRepeat,
            SPointRepeat,
            SAtlas,
            SCompareLinear,
            SComparePoint,
            SCount
        };

        struct QuadVertex {
            float x, y, z, u, v;
        };
    }

    // -- thread pool for shader conversion -----------------------------------------------------------------------------

    void JavaPack::Pool::Start(int n) {
        for (int i = 0; i < std::max(1, n); i++)
            threads.emplace_back([this] { Loop(); });
    }

    JavaPack::Pool::~Pool() {
        {
            std::lock_guard<std::mutex> lock(m);
            quit = true;
        }
        cv.notify_all();
        for (auto& t : threads)
            t.join();
    }

    std::shared_future<std::shared_ptr<JavaPack::Converted>>
    JavaPack::Pool::Submit(std::function<std::shared_ptr<Converted>()> fn) {
        auto task = std::make_shared<std::packaged_task<std::shared_ptr<Converted>()>>(std::move(fn));
        auto fut = task->get_future().share();
        {
            std::lock_guard<std::mutex> lock(m);
            jobs.emplace_back([task] { (*task)(); });
        }
        cv.notify_one();
        return fut;
    }

    void JavaPack::Pool::Loop() {
        LowerThreadPriority();
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(m);
                cv.wait(lock, [this] { return quit || !jobs.empty(); });
                if (quit && jobs.empty())
                    return;
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            job();
        }
    }

    size_t JavaPack::PackSetHash::operator()(const PackSet& s) const {
        size_t h = 1469598103934665603ull;
        for (size_t i = 0; i < s.texture.size(); i++) {
            h = (h ^ s.texture[i].value) * 1099511628211ull;
            h = (h ^ s.sampler[i]) * 1099511628211ull;
            h = (h ^ static_cast<size_t>(s.dim[i])) * 1099511628211ull;
        }
        return h;
    }

    // -- setup ---------------------------------------------------------------------------------------------------------

    JavaPack::JavaPack(Gpu& g, ChunkMeshStore& store) : gpu(g), meshes(store) {
        trace = std::getenv("JSHADER_TRACE") != nullptr;
    }

    JavaPack::~JavaPack() {
        for (auto& kv : pending)
            kv.second.wait();
        ReleaseAll();
    }

    void JavaPack::ReleaseAll() {
        rhi::Device& dev = gpu.Dev();
        dev.WaitIdle();
        DestroyTargets();
        if (timing.queries)
            dev.Destroy(timing.queries);
        for (rhi::BufferHandle b : timing.readback)
            if (b)
                dev.Destroy(b);
        timing = {};
        for (auto& kv : programs) {
            if (kv.second->vs)
                dev.Destroy(kv.second->vs);
            if (kv.second->fs)
                dev.Destroy(kv.second->fs);
            if (kv.second->cs)
                dev.Destroy(kv.second->cs);
            if (kv.second->computePipeline)
                dev.Destroy(kv.second->computePipeline);
        }
        programs.clear();
        for (auto& kv : pipelines)
            if (kv.second)
                dev.Destroy(kv.second);
        pipelines.clear();
        for (auto& kv : groups)
            dev.Destroy(kv.second);
        groups.clear();
        for (rhi::SamplerHandle& s : samplers)
            if (s) {
                dev.Destroy(s);
                s = {};
            }
        if (pipelineLayout)
            dev.Destroy(pipelineLayout);
        if (textureLayout)
            dev.Destroy(textureLayout);
        if (imageLayout)
            dev.Destroy(imageLayout);
        if (bufferLayout)
            dev.Destroy(bufferLayout);
        pipelineLayout = {};
        textureLayout = imageLayout = bufferLayout = {};
        if (skyStatic) {
            gpu.ForgetBuffer(skyStatic);
            dev.Destroy(skyStatic);
            skyStatic = {};
        }
        if (quad) {
            gpu.ForgetBuffer(quad);
            dev.Destroy(quad);
            quad = {};
        }
        for (rhi::TextureHandle t : owned)
            gpu.DestroyTexture(t);
        owned.clear();
        staticTextures.clear();
        if (atlas)
            gpu.DestroyTexture(atlas);
        atlas = {};
        white = flatNormal = blackTex = noise = {};
        customTextures.clear();
        customSamplers.clear();
    }

    rhi::TextureHandle JavaPack::MakeTexture(uint32_t w, uint32_t h, rhi::Format format,
                                             rhi::TextureUsage usage, const char* texName) {
        rhi::TextureDesc td;
        td.format = format;
        td.width = std::max(1u, w);
        td.height = std::max(1u, h);
        td.usage = usage;
        td.name = texName;
        const rhi::TextureHandle t = gpu.Dev().CreateTexture(td);
        if (t) {
            gpu.SetState(t, rhi::State::Undefined);
            owned.push_back(t);
        }
        return t;
    }

    rhi::SamplerHandle JavaPack::SamplerFor(int id) {
        rhi::SamplerHandle& slot = samplers[static_cast<size_t>(id)];
        if (slot)
            return slot;
        rhi::SamplerDesc sd;
        sd.maxAnisotropy = 1.0f;
        const auto point = [&] { sd.min = sd.mag = rhi::Filter::Nearest; };
        switch (id) {
        case SPointClamp:
            point();
            sd.mip = rhi::Filter::Nearest;
            break;
        case SLinearClamp:
            break;
        case SLinearRepeat:
            sd.u = sd.v = sd.w = rhi::AddressMode::Repeat;
            break;
        case SPointRepeat:
            point();
            sd.mip = rhi::Filter::Nearest;
            sd.u = sd.v = sd.w = rhi::AddressMode::Repeat;
            break;
        case SAtlas:
            point();
            sd.u = sd.v = sd.w = rhi::AddressMode::Repeat;
            break;
        case SCompareLinear:
            sd.compare = true;
            sd.compareOp = rhi::CompareOp::LessEqual;
            sd.mip = rhi::Filter::Nearest;
            break;
        case SComparePoint:
            point();
            sd.mip = rhi::Filter::Nearest;
            sd.compare = true;
            sd.compareOp = rhi::CompareOp::LessEqual;
            break;
        }
        slot = gpu.Dev().CreateSampler(sd);
        return slot;
    }

    bool JavaPack::CreateLayouts() {
        rhi::Device& dev = gpu.Dev();
        std::vector<rhi::BindLayoutEntry> entries;
        for (uint32_t s = 0; s < kTextureSlots; s++) {
            entries.push_back({s * 2, rhi::BindingType::SampledTexture,
                               rhi::ShaderStage::Fragment | rhi::ShaderStage::Vertex, 1,
                               rhi::TextureDim::Tex2D, true});
            entries.push_back({s * 2 + 1, rhi::BindingType::Sampler,
                               rhi::ShaderStage::Fragment | rhi::ShaderStage::Vertex, 1,
                               rhi::TextureDim::Tex2D, true});
        }
        for (auto& e : entries)
            e.stages = rhi::ShaderStage::All;
        rhi::BindLayoutDesc bl;
        bl.entries = entries;
        bl.name = "java pack textures";
        textureLayout = dev.CreateBindLayout(bl);
        // set 2: the pack's custom images for load / store, set 3: its shader storage buffers
        std::vector<rhi::BindLayoutEntry> imageEntries, bufferEntries;
        for (uint32_t i = 0; i < kImageSlots; i++)
            imageEntries.push_back({i, rhi::BindingType::StorageTextureRW, rhi::ShaderStage::All, 1,
                                    rhi::TextureDim::Tex2D, false});
        for (uint32_t i = 0; i < kSsboSlots; i++)
            bufferEntries.push_back({i, rhi::BindingType::StorageBufferRW, rhi::ShaderStage::All, 1,
                                     rhi::TextureDim::Tex2D, false});
        bl.entries = imageEntries;
        bl.name = "java pack images";
        imageLayout = dev.CreateBindLayout(bl);
        bl.entries = bufferEntries;
        bl.name = "java pack buffers";
        bufferLayout = dev.CreateBindLayout(bl);
        rhi::PipelineLayoutDesc pl;
        pl.sets[0] = gpu.UniformLayout();
        pl.sets[1] = textureLayout;
        pl.sets[2] = imageLayout;
        pl.sets[3] = bufferLayout;
        pl.name = "java pack";
        pipelineLayout = dev.CreatePipelineLayout(pl);

        const QuadVertex verts[6] = {{0, 0, 0, 0, 0}, {1, 0, 0, 1, 0}, {1, 1, 0, 1, 1},
                                     {0, 0, 0, 0, 0}, {1, 1, 0, 1, 1}, {0, 1, 0, 0, 1}};
        quad = gpu.CreateStatic(verts, sizeof(verts), rhi::BufferUsage::Vertex, "java pack quad");
        return textureLayout && imageLayout && bufferLayout && pipelineLayout && quad;
    }

    void JavaPack::CreateStatic() {
        const uint8_t whitePx[4] = {255, 255, 255, 255}, normalPx[4] = {128, 128, 255, 255},
                      blackPx[4] = {0, 0, 0, 0};
        const auto make = [&](const uint8_t* px, const char* texName) {
            rhi::TextureDesc td;
            td.format = rhi::Format::RGBA8Unorm;
            td.width = td.height = 1;
            td.name = texName;
            const rhi::TextureHandle t = gpu.CreateSampled(td);
            if (t) {
                gpu.WriteTexture(t, 0, 0, 0, 1, 1, px);
                owned.push_back(t);
                staticTextures.push_back(t);
            }
            return t;
        };
        staticTextures.clear();
        white = make(whitePx, "pack white");
        flatNormal = make(normalPx, "pack normal");
        blackTex = make(blackPx, "pack black");
        noise = make(whitePx, "pack noise");

        const auto load = [&](const std::string& file) -> rhi::TextureHandle {
            std::string clean = file;
            while (!clean.empty() && clean[0] == '/')
                clean.erase(0, 1);
            std::string bytes;
            if (!instance->Source().Read(clean, bytes))
                return {};
            int w = 0, h = 0, n = 0;
            unsigned char* data = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()),
                                                        static_cast<int>(bytes.size()), &w, &h, &n, 4);
            if (!data)
                return {};
            rhi::TextureDesc td;
            td.format = rhi::Format::RGBA8Unorm;
            td.width = static_cast<uint32_t>(w);
            td.height = static_cast<uint32_t>(h);
            td.name = "pack texture";
            const rhi::TextureHandle t = gpu.CreateSampled(td);
            if (t) {
                gpu.WriteTexture(t, 0, 0, 0, td.width, td.height, data);
                owned.push_back(t);
                staticTextures.push_back(t);
            }
            stbi_image_free(data);
            return t;
        };
        // iris CustomTextureData: a png's sampling comes from its .mcmeta (texture.blur / texture.clamp), nearest
        // and repeating without one
        const auto samplerFor = [&](const std::string& file) {
            std::string clean = file;
            while (!clean.empty() && clean[0] == '/')
                clean.erase(0, 1);
            std::string meta;
            bool blur = false, clamp = false;
            if (instance->Source().Read(clean + ".mcmeta", meta)) {
                const auto flag = [&](const char* key) {
                    const size_t at = meta.find(std::string("\"") + key + "\"");
                    if (at == std::string::npos)
                        return false;
                    const size_t colon = meta.find(':', at);
                    return colon != std::string::npos && meta.find("true", colon) != std::string::npos &&
                           meta.find("true", colon) < meta.find_first_of(",}", colon);
                };
                blur = flag("blur");
                clamp = flag("clamp");
            }
            return blur ? (clamp ? SLinearClamp : SLinearRepeat) : (clamp ? SPointClamp : SPointRepeat);
        };
        {
            // iris NativeImageBackedNoiseTexture: random rgb noise, noiseTextureResolution square, linear and
            // repeating, when the pack names no texture.noise
            const int res = config.noiseTextureResolution;
            std::vector<uint8_t> px(static_cast<size_t>(res) * res * 4);
            uint64_t state = 0x9E3779B97F4A7C15ull;
            for (size_t i = 0; i < px.size(); i++) {
                state ^= state << 13;
                state ^= state >> 7;
                state ^= state << 17;
                px[i] = (i % 4 == 3) ? 255 : static_cast<uint8_t>(state >> 56);
            }
            rhi::TextureDesc td;
            td.format = rhi::Format::RGBA8Unorm;
            td.width = td.height = static_cast<uint32_t>(res);
            td.name = "pack noise";
            if (const rhi::TextureHandle t = gpu.CreateSampled(td)) {
                gpu.WriteTexture(t, 0, 0, 0, td.width, td.height, px.data());
                owned.push_back(t);
                staticTextures.push_back(t);
                noise = t;
                noiseSampler = SLinearRepeat;
            }
        }
        if (!config.noiseTexture.empty()) {
            const rhi::TextureHandle t = load(config.noiseTexture);
            if (t) {
                noise = t;
                noiseSampler = samplerFor(config.noiseTexture);
            }
        }
        for (const jshader::CustomTexture& ct : config.textures) {
            const std::string key = ct.stage + ":" + CanonicalSamplerName(ct.sampler);
            if (customTextures.contains(key))
                continue;
            const rhi::TextureHandle t = load(ct.path);
            if (t) {
                customTextures[key] = t;
                customSamplers[key] = samplerFor(ct.path);
            }
        }
    }

    void JavaPack::ScanUsage() {
        depthTexUsed[0] = true;
        depthTexUsed[1] = depthTexUsed[2] = false;
        std::set<std::string> names;
        for (const jshader::ProgramInfo& p : jshader::StandardPrograms())
            names.insert(p.name);
        for (const char* base : {"begin", "prepare", "deferred", "composite", "shadowcomp"})
            for (const std::string& n : instance->PassChain(base))
                names.insert(n);
        names.insert("final");
        const std::regex re(
            "(colortex|shadowcolor|gaux)([0-9]+)|\\b(gcolor|gdepth)\\b|\\bshadowtex[01](HW)?\\b|"
            "\\bshadow\\b|\\bwatershadow\\b|\\bshadowcolor\\b");
        bufs[0].used = true;
        for (const std::string& n : names) {
            if (!instance->ProgramExists(n))
                continue;
            auto src = instance->Program(n);
            if (!src || !src->found)
                continue;
            for (const jshader::ExpandedSource* s : {&src->vertex, &src->fragment}) {
                // identifiers only: a regex over a pack's megabytes of expanded source took seconds
                const std::string& t = s->text;
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
                            return len > prefix && std::all_of(w + prefix, w + len,
                                                               [](char d) { return d >= '0' && d <= '9'; });
                        };
                        if (len > 8 && std::strncmp(w, "colortex", 8) == 0 && digits(8)) {
                            const int idx = std::atoi(std::string(w + 8, len - 8).c_str());
                            if (idx < jshader::kMaxColorTex)
                                bufs[static_cast<size_t>(idx)].used = true;
                        } else if (len == 5 && std::strncmp(w, "gaux", 4) == 0 && w[4] >= '1' &&
                                   w[4] <= '4') {
                            bufs[static_cast<size_t>(3 + (w[4] - '0'))].used = true;
                        } else if (len > 11 && std::strncmp(w, "shadowcolor", 11) == 0 && digits(11)) {
                            const int idx = std::atoi(std::string(w + 11, len - 11).c_str());
                            if (idx < 2) {
                                shadowColorUsed[idx] = true;
                                shadowUsed = true;
                            }
                        } else if (len == 6 && std::strncmp(w, "gcolor", 6) == 0) {
                            bufs[0].used = true;
                        } else if (len == 6 && std::strncmp(w, "gdepth", 6) == 0) {
                            bufs[1].used = true;
                        } else if (len == 11 && std::strncmp(w, "shadowcolor", 11) == 0) {
                            shadowColorUsed[0] = true;
                            shadowUsed = true;
                        } else if ((len == 9 && std::strncmp(w, "shadowtex", 9) == 0) ||
                                   ((len == 10 || len == 12) && std::strncmp(w, "shadowtex", 9) == 0 &&
                                    (w[9] == '0' || w[9] == '1')) ||
                                   (len == 6 && std::strncmp(w, "shadow", 6) == 0) ||
                                   (len == 12 && std::strncmp(w, "watershadow", 11) == 0)) {
                            shadowUsed = true;
                        }
                    } else if (len == 6 && std::strncmp(t.data() + i, "shadow", 6) == 0) {
                        shadowUsed = true;
                    }
                    i = e;
                }
            }
            for (int b : src->drawBuffers)
                if (b >= 0 && b < jshader::kMaxColorTex)
                    bufs[static_cast<size_t>(b)].used = true;
            const auto readsDepth = [&](const std::string& text) {
                if (text.find("depthtex1") != std::string::npos)
                    depthTexUsed[1] = true;
                if (text.find("depthtex2") != std::string::npos)
                    depthTexUsed[2] = true;
            };
            readsDepth(src->vertex.text);
            readsDepth(src->fragment.text);
            for (const jshader::ExpandedSource& c : src->compute)
                readsDepth(c.text);
        }
        // a disabled shadow program (complementary at shadow quality -1) means no shadow pass, as in iris; the
        // shadow textures still exist when the pack samples them, so its fallback paths read something valid
        config.shadowEnabled = instance->ProgramExists("shadow") && instance->ProgramEnabled("shadow");
        if (config.shadowEnabled)
            shadowUsed = true;
    }

    bool JavaPack::CreateTargets(uint32_t w, uint32_t h) {
        width = w;
        height = h;
        constexpr rhi::TextureUsage kColor =
            rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySrc;
        for (int i = 0; i < jshader::kMaxColorTex; i++) {
            Buffer& b = bufs[static_cast<size_t>(i)];
            if (!b.used)
                continue;
            const jshader::BufferSetup& cfg = config.color[static_cast<size_t>(i)];
            b.w = cfg.fixedWidth > 0
                      ? static_cast<uint32_t>(cfg.fixedWidth)
                      : std::max(1u, static_cast<uint32_t>(static_cast<float>(w) * cfg.scaleX));
            b.h = cfg.fixedHeight > 0
                      ? static_cast<uint32_t>(cfg.fixedHeight)
                      : std::max(1u, static_cast<uint32_t>(static_cast<float>(h) * cfg.scaleY));
            for (auto& k : b.tex) {
                k = MakeTexture(b.w, b.h, ToFormat(cfg.format), kColor, "pack color");
                if (!k)
                    return false;
            }
            b.cur = 0;
        }
        constexpr rhi::TextureUsage kDepth = rhi::TextureUsage::DepthTarget | rhi::TextureUsage::Sampled |
                                             rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst;
        for (auto& k : depth) {
            k = MakeTexture(w, h, rhi::Format::D32Float, kDepth, "pack depth");
            if (!k)
                return false;
        }
        if (shadowUsed) {
            const uint32_t res = static_cast<uint32_t>(config.shadowMapResolution);
            shadowDepth = MakeTexture(res, res, rhi::Format::D32Float, kDepth, "pack shadow depth");
            shadowDepthNoTranslucents =
                MakeTexture(res, res, rhi::Format::D32Float, kDepth, "pack shadow depth (no translucents)");
            for (int i = 0; i < 2; i++)
                if (shadowColorUsed[i])
                    shadowColor[i] = MakeTexture(res, res, ToFormat(config.shadowColorFormat[i]),
                                                 kColor | rhi::TextureUsage::CopyDst, "pack shadow color");
            if (!shadowDepth)
                return false;
        }
        return CreateStorage();
    }

    void JavaPack::DestroyTargets() {
        rhi::Device& dev = gpu.Dev();
        for (auto& kv : groups)
            dev.Destroy(kv.second);
        groups.clear();
        std::vector<rhi::TextureHandle> keep;
        for (rhi::TextureHandle t : owned) {
            bool persistent = t == white || t == flatNormal || t == blackTex || t == noise ||
                              std::find(staticTextures.begin(), staticTextures.end(), t) != staticTextures.end();
            for (auto& kv : customTextures)
                persistent = persistent || kv.second == t;
            if (persistent) {
                keep.push_back(t);
            } else {
                gpu.DestroyTexture(t);
            }
        }
        owned = keep;
        for (auto& kv : imageGroups)
            dev.Destroy(kv.second);
        imageGroups.clear();
        if (bufferGroup)
            dev.Destroy(bufferGroup);
        bufferGroup = {};
        for (rhi::BufferHandle& b : ssbo)
            if (b) {
                dev.Destroy(b);
                b = {};
            }
        for (rhi::BufferHandle* b : {&dummySsbo, &zeroBuffer})
            if (*b) {
                dev.Destroy(*b);
                *b = {};
            }
        zeroBytes = 0;
        for (Image& img : images)
            img.tex = {}; // owned: destroyed above
        dummyStorage = {};
        for (Buffer& b : bufs)
            b.tex[0] = b.tex[1] = {};
        depth[0] = depth[1] = depth[2] = {};
        shadowDepth = {};
        shadowDepthNoTranslucents = {};
        shadowCleared = false;
        shadowKept = shadowReuse = false;
        shadowPartial = shadowCacheValid = false;
        shadowCacheDepth = shadowCacheColor[0] = shadowCacheColor[1] = {};
        feedback.clear();
        shadowColor[0] = shadowColor[1] = {};
    }

    std::vector<std::string> JavaPack::ExtraFeatures() const {
        // storage images and buffers in every stage: vulkan, d3d12 and metal. d3d11 binds pixel shader uavs
        // against the render targets, which the rhi does not do, so packs take their fallback paths there
        const rhi::Api api = gpu.Api();
        if (api == rhi::Api::Vulkan || api == rhi::Api::D3D12 || api == rhi::Api::Metal)
            return {"CUSTOM_IMAGES", "COMPUTE_SHADERS", "SSBO"};
        return {};
    }

    bool JavaPack::Load(const Config& cfg, std::string& error) {
        alphaCache.clear();
        const rhi::Api api = gpu.Api();
        if (api != rhi::Api::Vulkan && api != rhi::Api::D3D12 && api != rhi::Api::D3D11 &&
            api != rhi::Api::Metal) {
            error =
                std::string("Java shader packs need the Vulkan, Direct3D 12, Direct3D 11 or Metal renderer "
                            "(this one is ") +
                rhi::ApiName(api) + ")";
            lastError = error;
            return false;
        }
        const auto t0 = std::chrono::steady_clock::now();
        const auto lap = [&](const char* what) {
            if (std::getenv("JSHADER_TRACE"))
                std::fprintf(stderr, "[jshader] load %s: %lld ms\n", what,
                             static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                        std::chrono::steady_clock::now() - t0)
                                                        .count()));
        };
        auto source = jshader::OpenPack(cfg.path);
        if (!source) {
            error = "could not open shader pack '" + cfg.path + "'";
            lastError = error;
            return false;
        }
        path = cfg.path;
        blockName = cfg.blockName;
        blockEmission = cfg.blockEmission;
        jshader::EngineEnvironment env;
        {
            const rhi::AdapterInfo& adapter = gpu.Dev().Caps().adapter;
            env.renderer = adapter.name;
            env.vendor = adapter.vendorId == 0x10DE   ? "NVIDIA Corporation"
                         : adapter.vendorId == 0x1002 ? "AMD"
                         : adapter.vendorId == 0x8086 ? "Intel"
                         : adapter.vendorId == 0x106B ? "Apple"
                                                      : adapter.name;
            env.features = jshader::BaseFeatureFlags();
            for (const std::string& f : ExtraFeatures())
                env.features.insert(f);
        }
        instance = std::make_shared<jshader::ShaderPackInstance>(
            std::shared_ptr<jshader::PackSource>(std::move(source)), cfg.options,
            jshader::EngineMacros(260300, false, env));
        if (!instance->MissingFeatures().empty()) {
            error = instance->FirstError();
            lastError = error;
            return false;
        }
        lap("opened");
        instance->SetDimension(cfg.dimension);
        noSkyLight = cfg.dimension != "world0";
        name = instance->Name().empty() ? cfg.path : instance->Name();
        config = jshader::ParsePipelineConfig(*instance);
        shadowResPatch = 0;
        shadowInterval = std::clamp(cfg.shadowInterval, 1, 4);
        shadowSkipState = -1;
        prepassState = -1;
        shadowKept = shadowReuse = false;
        shadowCacheOn = cfg.shadowCache;
        shadowPartial = shadowCacheValid = false;
        if (cfg.shadowResolutionLimit >= 128 && config.shadowMapResolution > cfg.shadowResolutionLimit) {
            Log("jshader", "shadow map {} capped to {} for this gpu and shadow quality", config.shadowMapResolution,
                cfg.shadowResolutionLimit);
            config.shadowMapResolution = cfg.shadowResolutionLimit;
            shadowResPatch = cfg.shadowResolutionLimit;
        }
        lap("parsed config");
        // the image table: slot i of set 2 is config.images[i]; the textures come with the render targets
        images.clear();
        imageSlotOf.clear();
        imageSamplerOf.clear();
        for (const jshader::CustomImage& info : config.images) {
            Image img;
            img.info = info;
            img.format = ImageFormat(info.internalFormat, img.integer);
            img.dim = info.dims == 3 ? rhi::TextureDim::Tex3D : rhi::TextureDim::Tex2D;
            if (img.format == rhi::Format::Unknown || info.dims == 1) {
                Log("jshader", "image {}: format {} / {}D is not supported", info.name.c_str(),
                    info.internalFormat.c_str(), info.dims);
                continue;
            }
            const int slot = static_cast<int>(images.size());
            imageSlotOf[info.name] = slot;
            if (!info.sampler.empty())
                imageSamplerOf[info.sampler] = slot;
            images.push_back(std::move(img));
        }
        setupDone = false;
        custom.Load(config.customUniformProps);
        table.SetCustom(&custom);
        {
            const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
            pool.Start(static_cast<int>(cores > 4 ? std::min(6u, cores - 2) : std::max(1u, cores / 2)));
        }
        if (!CreateLayouts()) {
            error = "could not create the shader pack's bind layouts";
            lastError = error;
            return false;
        }
        lap("layouts");
        ScanUsage();
        lap("scanned");
        CreateStatic();
        lap("static");
        BuildBlockMap();
        lap("blocks");
        return true;
    }

    void JavaPack::SetDimension(const std::string& folder) {
        if (instance && instance->Dimension() != folder)
            instance->SetDimension(folder);
        noSkyLight = folder != "world0";
    }

    void JavaPack::BuildBlockMap() {
        blockMatcher = {};
        entityIdMap.clear();
        itemIdMap.clear();
        paletteMap.assign(65536, -2);
        fluidMap.assign(65536, 0);
        fluidIds[0] = fluidIds[1] = -1;
        if (!instance)
            return;
        blockMatcher.Build(instance->BlockIdLines());
        entityIdMap = jshader::FlatIdMap(instance->EntityIdLines());
        itemIdMap = jshader::FlatIdMap(instance->ItemIdLines());
        jshader::JavaBlock water, lava;
        water.name = "water";
        water.props = {{"level", "0"}};
        lava.name = "lava";
        lava.props = {{"level", "0"}};
        fluidIds[0] = blockMatcher.Match(water);
        fluidIds[1] = blockMatcher.Match(lava);
    }

    int JavaPack::PackBlockId(uint16_t palette) {
        if (paletteMap.empty())
            return -1;
        int16_t& slot = paletteMap[palette];
        if (slot != -2)
            return slot;
        int result = -1;
        if (blockName) {
            const jshader::JavaBlock jb = jshader::ToJavaBlock(blockName(palette));
            result = blockMatcher.Match(jb);
            if (result < 0) {
                // packs written for older versions list the pre-flattening names
                if (const char* alias = Alias(jb.name)) {
                    jshader::JavaBlock old = jb;
                    old.name = alias;
                    result = blockMatcher.Match(old);
                }
            }
        }
        slot = static_cast<int16_t>(std::clamp(result, -1, 32000));
        return slot;
    }

    bool JavaPack::IsFluidBlock(uint16_t palette) {
        // per palette entry, filled on first use (chunk meshing runs on several threads: a racing duplicate
        // fill writes the same value)
        if (fluidMap.size() < 65536)
            return false;
        uint8_t& slot = fluidMap[palette];
        if (slot == 0) {
            bool fluid = false;
            if (blockName) {
                const std::string n = jshader::ToJavaBlock(blockName(palette)).name;
                fluid = n == "water" || n == "lava";
            }
            slot = fluid ? 2 : 1;
        }
        return slot == 2;
    }

    int JavaPack::PackEntityId(const std::string& bedrockType) {
        if (bedrockType.empty() || entityIdMap.empty())
            return 0;
        auto cached = entityIdCache.find(bedrockType);
        if (cached != entityIdCache.end())
            return cached->second;
        const std::string java = jshader::JavaEntityName(bedrockType);
        auto it = entityIdMap.find(java);
        // iris leaves unlisted entities at 0
        const int id = it != entityIdMap.end() ? it->second : 0;
        entityIdCache.emplace(bedrockType, id);
        return id;
    }

    int JavaPack::PackItemId(const std::string& bedrockItem) {
        if (bedrockItem.empty() || itemIdMap.empty())
            return 0;
        auto cached = itemIdCache.find(bedrockItem);
        if (cached != itemIdCache.end())
            return cached->second;
        const std::string java = jshader::JavaItemName(bedrockItem);
        auto it = itemIdMap.find(java);
        const int id = it != itemIdMap.end() ? it->second : 0;
        itemIdCache.emplace(bedrockItem, id);
        return id;
    }

    int JavaPack::PackBlockIdByName(const std::string& bedrockName) {
        if (blockMatcher.Empty() || bedrockName.empty())
            return 0;
        const int id = blockMatcher.Match(jshader::ToJavaBlock(bedrockName));
        return id < 0 ? 0 : id;
    }

    void JavaPack::SetAtlas(const uint8_t* rgba, int tilePx, int tiles) {
        if (!rgba || tilePx <= 0 || tiles <= 0)
            return;
        if (atlas) {
            gpu.Dev().WaitIdle();
            owned.erase(std::remove(owned.begin(), owned.end(), atlas), owned.end());
            gpu.DestroyTexture(atlas);
            atlas = {};
        }
        atlasTilePx = tilePx;
        atlasTiles = tiles;
        const int maxTex = static_cast<int>(gpu.Caps().limits.maxTexture2D);
        gridCols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(tiles)))));
        gridCols = std::min(gridCols, std::max(1, maxTex / tilePx));
        gridRows = (tiles + gridCols - 1) / gridCols;
        if (gridRows * tilePx > maxTex) {
            std::fprintf(stderr, "[javapack] atlas of %d tiles (%dpx) does not fit a %d texture\n", tiles,
                         tilePx, maxTex);
            gridRows = std::max(1, maxTex / tilePx);
            atlasTiles = gridRows * gridCols;
        }
        const int W = gridCols * tilePx, H = gridRows * tilePx;
        atlasWidth = W;
        // mips stop at the tile size so neighbouring tiles never bleed together
        int tileLevels = 0;
        while ((tilePx >> (tileLevels + 1)) >= 2 && tileLevels < 4)
            tileLevels++;
        atlasLevels = tileLevels + 1;
        std::vector<std::vector<uint8_t>> levels;
        std::vector<std::pair<int, int>> sizes;
        levels.emplace_back(static_cast<size_t>(W) * H * 4, 0);
        sizes.emplace_back(W, H);
        {
            std::vector<uint8_t>& base = levels[0];
            const size_t tileBytes = static_cast<size_t>(tilePx) * tilePx * 4;
            for (int t = 0; t < atlasTiles; t++) {
                const int gx = t % gridCols, gy = t / gridCols;
                for (int y = 0; y < tilePx; y++)
                    std::memcpy(
                        &base[(static_cast<size_t>(gy * tilePx + y) * W + static_cast<size_t>(gx) * tilePx) *
                              4],
                        rgba + tileBytes * static_cast<size_t>(t) + static_cast<size_t>(y) * tilePx * 4,
                        static_cast<size_t>(tilePx) * 4);
            }
        }
        for (int l = 1; l <= tileLevels; l++) {
            const int s = tilePx >> l, ps = tilePx >> (l - 1);
            const int lw = gridCols * s, lh = gridRows * s;
            levels.emplace_back(static_cast<size_t>(lw) * lh * 4, 0);
            sizes.emplace_back(lw, lh);
            const std::vector<uint8_t>& prev = levels[static_cast<size_t>(l) - 1];
            const int pw = sizes[static_cast<size_t>(l) - 1].first;
            for (int t = 0; t < atlasTiles; t++) {
                const int gx = t % gridCols, gy = t / gridCols;
                BoxDown(&prev[(static_cast<size_t>(gy * ps) * pw + static_cast<size_t>(gx) * ps) * 4], ps, ps,
                        pw * 4,
                        &levels[static_cast<size_t>(l)]
                               [(static_cast<size_t>(gy * s) * lw + static_cast<size_t>(gx) * s) * 4],
                        lw * 4);
            }
        }
        rhi::TextureDesc td;
        td.format = rhi::Format::RGBA8Unorm;
        td.width = static_cast<uint32_t>(W);
        td.height = static_cast<uint32_t>(H);
        td.mips = static_cast<uint32_t>(levels.size());
        td.name = "pack atlas";
        atlas = gpu.CreateSampled(td);
        if (!atlas) {
            std::fprintf(stderr, "[javapack] atlas texture creation failed (%dx%d)\n", W, H);
            return;
        }
        for (size_t l = 0; l < levels.size(); l++)
            gpu.WriteTexture(atlas, 0, 0, 0, static_cast<uint32_t>(sizes[l].first),
                             static_cast<uint32_t>(sizes[l].second), levels[l].data(), 0,
                             static_cast<uint32_t>(l));
    }

    void JavaPack::UpdateTile(int tile, const uint8_t* rgba) {
        if (!atlas || !rgba || tile < 0 || tile >= atlasTiles)
            return;
        const int gx = tile % gridCols, gy = tile / gridCols;
        std::vector<uint8_t> cur(rgba, rgba + static_cast<size_t>(atlasTilePx) * atlasTilePx * 4), next;
        int s = atlasTilePx;
        for (int l = 0; l < atlasLevels; l++) {
            gpu.WriteTexture(atlas, 0, static_cast<uint32_t>(gx * s), static_cast<uint32_t>(gy * s),
                             static_cast<uint32_t>(s), static_cast<uint32_t>(s), cur.data(), 0,
                             static_cast<uint32_t>(l));
            if (l + 1 >= atlasLevels)
                break;
            next.assign(static_cast<size_t>(s / 2) * (s / 2) * 4, 0);
            BoxDown(cur.data(), s, s, s * 4, next.data(), (s / 2) * 4);
            cur.swap(next);
            s /= 2;
        }
    }

    // vertex stream for the pack: normals, tangents and the block id come from the quad, which the main stream does not carry
    void JavaPack::Convert(const Tessellator& tess, std::vector<uint8_t>& out) {
        const std::vector<Vertex>& verts = tess.Vertices();
        const std::vector<QuadInfo>& quads = tess.Quads();
        const bool haveQuads = quads.size() * 4 == verts.size();
        out.assign(verts.size() * kPackVertexBytes, 0);
        const float inset = 1.0f / (2.0f * static_cast<float>(atlasTilePx));
        const float span = 1.0f - 2.0f * inset;
        const float invCols = 1.0f / static_cast<float>(gridCols),
                    invRows = 1.0f / static_cast<float>(gridRows);
        const auto put = [&](uint8_t* base, uint32_t offset, const void* data, size_t bytes) {
            std::memcpy(base + offset, data, bytes);
        };
        for (size_t q = 0; q + 3 < verts.size(); q += 4) {
            const Vertex* v = &verts[q];
            const glm::vec3 p0(v[0].x, v[0].y, v[0].z), p1(v[1].x, v[1].y, v[1].z),
                p2(v[2].x, v[2].y, v[2].z), p3(v[3].x, v[3].y, v[3].z);
            glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
            if (glm::dot(n, n) < 1e-12f)
                n = glm::cross(p2 - p0, p3 - p0);
            const float len = glm::length(n);
            n = len > 1e-8f ? n / len : glm::vec3(0, 1, 0);
            const glm::vec3 centre = (p0 + p1 + p2 + p3) * 0.25f;
            glm::vec3 blockOrigin;
            uint16_t block = 0;
            float layerType = 0.0f;
            if (haveQuads) {
                const QuadInfo& info = quads[q / 4];
                blockOrigin = glm::vec3(info.bx, info.by, info.bz);
                block = info.block;
                layerType = (info.layer == static_cast<uint8_t>(RenderLayer::Translucent) ||
                             info.layer == static_cast<uint8_t>(RenderLayer::TranslucentCull))
                                ? 1.0f
                                : 0.0f;
            } else {
                blockOrigin = glm::floor(centre - n * 0.01f);
            }
            const glm::vec3 blockCentre = blockOrigin + glm::vec3(0.5f);
            // the quad winding does not say which way it faces: the side of the block centre does
            const float side = glm::dot(n, centre - blockCentre);
            if (std::fabs(side) < 0.15f)
                n = glm::vec3(0, 1, 0);
            else if (side < 0.0f)
                n = -n;
            const glm::vec2 uv0(v[0].u, v[0].v), uv1(v[1].u, v[1].v), uv2(v[2].u, v[2].v);
            const glm::vec3 e1 = p1 - p0, e2 = p2 - p0;
            const glm::vec2 d1 = uv1 - uv0, d2 = uv2 - uv0;
            const float det = d1.x * d2.y - d2.x * d1.y;
            glm::vec3 tangent = std::fabs(det) > 1e-8f ? (e1 * d2.y - e2 * d1.y) / det : e1;
            if (glm::dot(tangent, tangent) < 1e-12f)
                tangent = glm::vec3(1, 0, 0);
            tangent = glm::normalize(tangent);
            int packId = block ? PackBlockId(block) : -1;
            // fluids: iris passes the fluid's own block id (water in a waterlogged block too) and marks it in
            // mc_Entity.y; nothing else has y set
            bool fluid = false;
            {
                const int tile0 = static_cast<int>(v[0].light >> 16);
                const bool waterTile = tile0 >= 0 && (tile0 == fluidTiles[0] || tile0 == fluidTiles[1]);
                const bool lavaTile = tile0 >= 0 && (tile0 == fluidTiles[2] || tile0 == fluidTiles[3]);
                if (waterTile || lavaTile) {
                    fluid = true;
                    packId = fluidIds[waterTile ? 0 : 1];
                } else if (block) {
                    fluid = IsFluidBlock(block);
                }
            }
            // iris BLOCK_EMISSION_ATTRIBUTE: the block's light level rides in at_midBlock.w
            const float emission =
                block && blockEmission ? static_cast<float>(std::clamp(blockEmission(block), 0, 15)) : 0.0f;
            float au[4], av[4];
            for (int i = 0; i < 4; i++) {
                const uint32_t tile = v[i].light >> 16;
                const int gx = static_cast<int>(tile % static_cast<uint32_t>(gridCols)),
                          gy = static_cast<int>(tile / static_cast<uint32_t>(gridCols));
                au[i] = (static_cast<float>(gx) + inset + v[i].u * span) * invCols;
                av[i] = (static_cast<float>(gy) + inset + v[i].v * span) * invRows;
            }
            const float mid[2] = {(au[0] + au[1] + au[2] + au[3]) * 0.25f,
                                  (av[0] + av[1] + av[2] + av[3]) * 0.25f};
            const uint16_t normal[4] = {ToHalf(n.x), ToHalf(n.y), ToHalf(n.z), 0};
            const uint16_t tang[4] = {ToHalf(tangent.x), ToHalf(tangent.y), ToHalf(tangent.z), ToHalf(1.0f)};
            static const bool waterDebug = std::getenv("JSHADER_WATERDBG") != nullptr;
            if (layerType > 0.5f && waterDebug) {
                static int shown = 0;
                if (shown++ < 6)
                    std::fprintf(stderr,
                                 "[jshader] translucent quad block=%u name='%s' packId=%d rgba=%08x\n", block,
                                 blockName ? blockName(block).c_str() : "?", packId, v[0].rgba);
            }
            (void)layerType;
            static const bool noIds = std::getenv("JSHADER_NOIDS") != nullptr;
            if (noIds)
                packId = -1;
            const float entity[2] = {static_cast<float>(packId), fluid ? 1.0f : 0.0f};
            for (int i = 0; i < 4; i++) {
                uint8_t* o = out.data() + (q + static_cast<size_t>(i)) * kPackVertexBytes;
                const float pos[3] = {v[i].x, v[i].y, v[i].z};
                // vanilla ambient occlusion rides in the tangent's fraction; java gives it to packs in the vertex
                // colour: in the alpha with separateAo, multiplied into the colour otherwise
                const float ao = std::clamp((v[i].tangent - std::floor(v[i].tangent)) * 16.0f, 0.0f, 1.0f);
                uint32_t abgr = v[i].rgba;
                const uint32_t aoQ = static_cast<uint32_t>(ao * 255.0f + 0.5f);
                if (config.separateAo) {
                    abgr = (abgr & 0x00FFFFFFu) | (aoQ << 24);
                } else if (aoQ != 255u) {
                    uint32_t mixed = abgr & 0xFF000000u;
                    for (int ch = 0; ch < 3; ch++)
                        mixed |= (((abgr >> (ch * 8)) & 0xFFu) * aoQ + 127u) / 255u << (ch * 8);
                    abgr = mixed;
                }
                const float uv[2] = {au[i], av[i]};
                const float light[2] = {static_cast<float>(v[i].light & 0xFF),
                                        noSkyLight ? 0.0f : static_cast<float>((v[i].light >> 8) & 0xFF)};
                const glm::vec3 toMid = (blockCentre - glm::vec3(pos[0], pos[1], pos[2])) * 64.0f;
                const uint16_t midBlock[4] = {ToHalf(std::clamp(std::round(toMid.x), -127.0f, 127.0f)),
                                              ToHalf(std::clamp(std::round(toMid.y), -127.0f, 127.0f)),
                                              ToHalf(std::clamp(std::round(toMid.z), -127.0f, 127.0f)),
                                              ToHalf(emission)};
                put(o, kPos, pos, 12);
                put(o, kColor, &abgr, 4);
                put(o, kUv, uv, 8);
                put(o, kLight, light, 8);
                put(o, kNormal, normal, 8);
                put(o, kEntity, entity, 8);
                put(o, kMid, mid, 8);
                put(o, kMidBlock, midBlock, 8);
                put(o, kTangent, tang, 8);
            }
        }
    }

    // -- programs ------------------------------------------------------------------------------------------------------

    bool JavaPack::IsFullscreenPass(const std::string& progName) {
        static constexpr const char* kPassPrefixes[] = {"composite", "deferred",   "prepare", "final",
                                                        "begin",     "shadowcomp", "setup"};
        for (const char* prefix : kPassPrefixes)
            if (progName.rfind(prefix, 0) == 0)
                return true;
        return false;
    }

    std::string JavaPack::KeyFor(const std::string& n, bool toBackbuffer) const {
        return n + (toBackbuffer ? "#B" : "#O");
    }

    JavaPack::SamplerBind JavaPack::ResolveSampler(const jshader::RhiSampler& s) {
        SamplerBind b;
        b.slot = s.slot;
        b.shadow = s.shadow;
        b.name = s.name;
        const std::string& n = s.name;
        const auto numeric = [&](const char* prefix, int& out) {
            const size_t len = std::strlen(prefix);
            if (n.compare(0, len, prefix) != 0 || n.size() == len)
                return false;
            for (size_t i = len; i < n.size(); i++)
                if (!std::isdigit(static_cast<unsigned char>(n[i])))
                    return false;
            out = std::atoi(n.c_str() + len);
            return true;
        };
        int idx = 0;
        // iris: texture.<stage>.<name> replaces the sampler (buffer names included) in that stage's programs
        if (const std::string key = resolvingStage + ":" + CanonicalSamplerName(n);
            customTextures.contains(key)) {
            b.kind = Kind::Custom;
            b.customKey = key;
            return b;
        }
        if (numeric("colortex", idx) && idx < jshader::kMaxColorTex) {
            b.kind = Kind::Color;
            b.index = idx;
        } else if (n == "gcolor") {
            b.kind = Kind::Color;
            b.index = 0;
        } else if (n == "gdepth") {
            b.kind = Kind::Color;
            b.index = 1;
        } else if (n.compare(0, 4, "gaux") == 0 && n.size() == 5 && n[4] >= '1' && n[4] <= '4') {
            b.kind = Kind::Color;
            b.index = 3 + (n[4] - '0');
        } else if (n == "depthtex0" || n == "gdepthtex") {
            b.kind = Kind::Depth;
            b.index = 0;
        } else if (n == "depthtex1") {
            b.kind = Kind::Depth;
            b.index = 1;
        } else if (n == "depthtex2") {
            b.kind = Kind::Depth;
            b.index = 2;
        } else if (n == "shadowtex0" || n == "shadowtex0HW" || n == "watershadow") {
            b.kind = Kind::ShadowDepth;
            b.index = 0;
        } else if (n == "shadowtex1" || n == "shadowtex1HW") {
            b.kind = Kind::ShadowDepth;
            b.index = 1;
        } else if (n == "shadow") {
            // iris IrisSamplers: `shadow` is shadowtex1 in programs that also read watershadow
            b.kind = Kind::ShadowDepth;
            b.index = resolvingWaterShadow ? 1 : 0;
        } else if (n == "shadowcolor0" || n == "shadowcolor") {
            b.kind = Kind::ShadowColor;
            b.index = 0;
        } else if (n == "shadowcolor1") {
            b.kind = Kind::ShadowColor;
            b.index = 1;
        } else if (n == "noisetex")
            b.kind = Kind::Noise;
        else if (n == "gtexture" || n == "tex" || n == "texture" || n == "iris_texture")
            b.kind = Kind::Atlas;
        else if (n == "lightmap" || n == "iris_lightmap")
            b.kind = Kind::Lightmap;
        else if (n == "normals")
            b.kind = Kind::Normals;
        else if (n == "specular")
            b.kind = Kind::Specular;
        else if (n == "iris_overlay")
            b.kind = Kind::Overlay;
        else if (const auto img = imageSamplerOf.find(n); img != imageSamplerOf.end()) {
            b.kind = Kind::Image;
            b.index = img->second;
        } else
            b.kind = Kind::Dummy;
        // iris IrisSamplers: colortex0 is the default sampler (texture unit 0) of fullscreen passes, so the
        // gbuffer names (texture, tex, gtexture) and any sampler iris does not bind read colortex0 there
        if (resolvingFullscreen && (b.kind == Kind::Atlas || b.kind == Kind::Dummy)) {
            b.kind = Kind::Color;
            b.index = 0;
        }
        return b;
    }

    std::vector<JavaPack::SamplerBind> JavaPack::ResolveAll(const std::string& progName,
                                                            const std::vector<jshader::RhiSampler>& samplerList) {
        resolvingWaterShadow = false;
        resolvingStage = TextureStageOf(progName);
        resolvingFullscreen = IsFullscreenPass(progName);
        for (const jshader::RhiSampler& s : samplerList)
            resolvingWaterShadow = resolvingWaterShadow || s.name == "watershadow";
        std::vector<SamplerBind> out;
        for (const jshader::RhiSampler& s : samplerList)
            out.push_back(ResolveSampler(s));
        return out;
    }

    void JavaPack::FillProgramResources(Program& p, const std::string& progName,
                                        const std::vector<jshader::RhiSampler>& samplerList,
                                        const std::vector<std::string>& imageNames,
                                        const std::vector<jshader::UniformDecl>& uniforms, int regs) {
        p.regs = regs;
        resolvingWaterShadow = false;
        resolvingStage = TextureStageOf(progName);
        resolvingFullscreen = IsFullscreenPass(progName);
        for (const jshader::RhiSampler& s : samplerList)
            resolvingWaterShadow = resolvingWaterShadow || s.name == "watershadow";
        for (const jshader::RhiSampler& s : samplerList) {
            if (s.slot >= static_cast<int>(kTextureSlots)) {
                Log("jshader", "{}: sampler '{}' needs slot {} (only {} exist)", progName.c_str(),
                    s.name.c_str(), s.slot, kTextureSlots);
                p.valid = false;
                continue;
            }
            p.samplers.push_back(ResolveSampler(s));
        }
        for (const std::string& img : imageNames) {
            const auto it = imageSlotOf.find(img);
            if (it == imageSlotOf.end() || !images[static_cast<size_t>(it->second)].tex) {
                Log("jshader", "{}: image '{}' has no texture", progName.c_str(), img.c_str());
                p.valid = false;
                continue;
            }
            p.imageSlots.push_back(it->second);
            p.imageMask |= 1u << it->second;
        }
        if (p.regs > 256) {
            Log("jshader", "{}: {} uniform registers (limit 256)", progName.c_str(), p.regs);
            p.valid = false;
        }
        for (const jshader::UniformDecl& d : uniforms) {
            UniformSlot slot;
            slot.decl = d;
            slot.known = table.Known(d.name);
            static const std::set<std::string> perDraw = {"entityColor",   "entityId",
                                                          "blockEntityId", "currentRenderedItemId",
                                                          "renderStage",   "alphaTestRef"};
            slot.perDraw = d.name.compare(0, 5, "iris_") == 0 || perDraw.contains(d.name);
            if (!slot.known && loggedMissing.insert(progName + ":" + d.name).second)
                Log("jshader", "{}: unknown uniform '{}' (left at 0)", progName.c_str(), d.name.c_str());
            p.anyPerDraw = p.anyPerDraw || slot.perDraw;
            p.slots.push_back(std::move(slot));
        }
        p.frameRegs.assign(static_cast<size_t>(std::max(1, p.regs)) * 4, 0.0f);
        p.drawRegs = p.frameRegs;
    }

    JavaPack::Program* JavaPack::Finish(const std::string& key, const std::string& progName,
                                        const Converted& c, std::shared_ptr<jshader::ProgramSource> source) {
        auto p = std::make_unique<Program>();
        p->source = std::move(source);
        const jshader::RhiProgram& rp = c.program;
        const rhi::ShaderFormat format = rp.kind == jshader::RhiShaderKind::SpirV  ? rhi::ShaderFormat::SpirV
                                         : rp.kind == jshader::RhiShaderKind::Dxbc ? rhi::ShaderFormat::Dxbc
                                         : rp.kind == jshader::RhiShaderKind::Msl  ? rhi::ShaderFormat::Msl
                                                                                   : rhi::ShaderFormat::Hlsl;
        rhi::ShaderDesc sd;
        sd.format = format;
        sd.name = progName.c_str();
        if (format == rhi::ShaderFormat::Msl)
            sd.entry = "main0"; // spirv-cross renames main, which msl reserves
        std::vector<uint8_t> vsCode = rp.vertex, fsCode = rp.fragment;
        // developer testing: JSHADER_VIA_HLSL=11 (or 12) runs the hlsl the direct3d 11 (12) backend would get on
        // vulkan, through glslang
        static const char* viaHlsl = std::getenv("JSHADER_VIA_HLSL");
        const bool d3d11Samplers = gpu.Api() == rhi::Api::D3D11 || (viaHlsl && std::string(viaHlsl) == "11");
        if (format == rhi::ShaderFormat::Hlsl && d3d11Samplers) {
            const std::vector<SamplerBind> binds = ResolveAll(progName, rp.samplers);
            for (std::vector<uint8_t>* code : {&vsCode, &fsCode}) {
                const std::string merged = MergeHlslSamplers(std::string(code->begin(), code->end()), binds);
                code->assign(merged.begin(), merged.end());
            }
        }
        sd.stage = rhi::ShaderStage::Vertex;
        sd.code = vsCode;
        p->vs = gpu.Dev().CreateShader(sd);
        sd.stage = rhi::ShaderStage::Fragment;
        sd.code = fsCode;
        p->fs = gpu.Dev().CreateShader(sd);
        p->valid = p->vs && p->fs;
        FillProgramResources(*p, progName, rp.samplers, rp.images, c.transform.uniforms, rp.uniformRegs);
        int maxLoc = -1;
        for (int l : c.transform.fragment.drawBuffers)
            maxLoc = std::max(maxLoc, l);
        const std::vector<int> none;
        const std::vector<int>& tag = p->source ? p->source->drawBuffers : none;
        for (int l = 0; l <= maxLoc; l++)
            p->attachments.push_back(l < static_cast<int>(tag.size()) ? tag[static_cast<size_t>(l)] : l);
        Program* raw = p.get();
        programs[key] = std::move(p);
        return raw;
    }

    std::string JavaPack::AlphaFor(const std::string& progName, float defaultRef) const {
        // called for every draw: remember each answer (the pack's settings only change with a reload)
        char keyBuf[16];
        std::snprintf(keyBuf, sizeof(keyBuf), "|%g", static_cast<double>(defaultRef));
        std::string cacheKey = progName + keyBuf;
        if (auto it = alphaCache.find(cacheKey); it != alphaCache.end())
            return it->second;
        std::string result = ComputeAlpha(progName, defaultRef);
        alphaCache.emplace(std::move(cacheKey), result);
        return result;
    }

    std::string JavaPack::ComputeAlpha(const std::string& progName, float defaultRef) const {
        // iris: the shaders.properties override for the program file that loaded, else the render type's test
        const jshader::ProgramConfig* pc = nullptr;
        if (instance) {
            if (auto src = instance->Program(progName); src && src->found && !src->sourceName.empty())
                pc = config.Find(src->sourceName);
        }
        if (!pc)
            pc = config.Find(progName);
        if (pc && pc->alpha.set) {
            if (pc->alpha.off)
                return {};
            char ref[32];
            std::snprintf(ref, sizeof(ref), "%.9g", static_cast<double>(pc->alpha.ref));
            switch (pc->alpha.func) {
            case 0x200: // NEVER
                return "< -1.0e30";
            case 0x201:
                return std::string("< ") + ref;
            case 0x202:
                return std::string("== ") + ref;
            case 0x203:
                return std::string("<= ") + ref;
            case 0x205:
                return std::string("!= ") + ref;
            case 0x206:
                return std::string(">= ") + ref;
            case 0x207: // ALWAYS
                return {};
            default:
                return std::string("> ") + ref;
            }
        }
        if (defaultRef < 0.0f)
            return {};
        char ref[32];
        std::snprintf(ref, sizeof(ref), "%.9g", static_cast<double>(defaultRef));
        return std::string("> ") + ref;
    }

    JavaPack::Program* JavaPack::GetProgram(const std::string& progName, bool toBackbuffer,
                                            const jshader::ExpandedSource* vs,
                                            const jshader::ExpandedSource* fs, rhi::Format,
                                            const std::string& alpha) {
        const std::string key = KeyFor(progName, toBackbuffer) + (alpha.empty() ? "" : "@" + alpha);
        const auto ready = programs.find(key);
        if (ready != programs.end())
            return ready->second->valid ? ready->second.get() : nullptr;
        std::shared_ptr<jshader::ProgramSource> source;
        if (!vs) {
            source = instance->Program(progName);
            if (!source || !source->found) {
                programs[key] = std::make_unique<Program>();
                return nullptr;
            }
        }
        auto it = pending.find(key);
        if (it == pending.end()) {
            jshader::ExpandedSource vertex, fragment;
            if (vs) {
                vertex = *vs;
                fragment = *fs;
            } else {
                vertex = source->vertex;
                fragment = source->fragment;
            }
            if (shadowResPatch > 0) {
                PatchShadowResolution(vertex.text, shadowResPatch);
                PatchShadowResolution(fragment.text, shadowResPatch);
            }
            jshader::TransformOptions opts;
            opts.macros = instance->EngineMacros();
            opts.preprocess = jshader::PreprocessGlsl;
            // full-screen passes draw a quad with only a position and a uv (iris POSITION_TEX): everything
            // else reads iris's defaults (white colour, full light) instead of an unbound vertex input
            opts.availableAttribs = IsFullscreenPass(progName) ? jshader::AttribBit(jshader::kAttribPosition) |
                                                                     jshader::AttribBit(jshader::kAttribTexCoord0)
                                                               : AttribMask();
            opts.zeroToOneDepth = true;
            opts.splitSamplers = true;
            opts.alphaTest = alpha;
            opts.explicitLod = progName.compare(0, 8, "gbuffers") != 0 &&
                               progName.compare(0, 6, "shadow") != 0 && progName.compare(0, 4, "hand") != 0 &&
                               progName.compare(0, 6, "entity") != 0;
            // offscreen targets are stored upside down against the screen so the pack's opengl uv convention holds
            opts.flipVertexY = !toBackbuffer;
            opts.flipFragCoordY = toBackbuffer;
            for (const auto& [imgName, slot] : imageSlotOf) {
                opts.imageSlots[imgName] = slot;
                opts.imageFormats[imgName] =
                    jshader::ImageFormatQualifier(images[static_cast<size_t>(slot)].info.internalFormat);
            }
            const rhi::Api api = gpu.Api();
            const jshader::RhiShaderKind kind = api == rhi::Api::Vulkan  ? jshader::RhiShaderKind::SpirV
                                                : api == rhi::Api::D3D12 ? jshader::RhiShaderKind::Dxbc
                                                : api == rhi::Api::Metal ? jshader::RhiShaderKind::Msl
                                                                         : jshader::RhiShaderKind::Hlsl;
            const jshader::RhiShaderKind kindUsed = std::getenv("JSHADER_FORCE_HLSL") || std::getenv("JSHADER_VIA_HLSL")
                                                        ? jshader::RhiShaderKind::Hlsl
                                                        : kind; // conversion check without a d3d device
            const jshader::DxbcCompiler dxbc = jshader::SystemDxbcCompiler();
            const std::filesystem::path cacheFile =
                CacheFile(vertex, fragment, opts, static_cast<int>(kindUsed), progName);
            // developer testing (JSHADER_VIA_HLSL): the sampler key of every name a program can declare, for the
            // direct3d 11 sampler merge done on the worker
            std::map<std::string, std::string> viaKeys;
            static const char* viaHlslEnv = std::getenv("JSHADER_VIA_HLSL");
            const bool via = viaHlslEnv && gpu.Api() == rhi::Api::Vulkan;
            if (via && std::string(viaHlslEnv) == "11") {
                std::vector<jshader::RhiSampler> names;
                const auto add = [&](const std::string& n) {
                    for (bool sh : {false, true}) {
                        jshader::RhiSampler r;
                        r.name = n;
                        r.shadow = sh;
                        names.push_back(r);
                    }
                };
                for (int i = 0; i < 16; i++)
                    add("colortex" + std::to_string(i));
                for (const char* n : {"gcolor", "gdepth", "gnormal", "composite", "gaux1", "gaux2", "gaux3", "gaux4",
                                      "depthtex0", "depthtex1", "depthtex2", "gdepthtex", "shadowtex0", "shadowtex1",
                                      "shadow", "watershadow", "shadowcolor", "shadowcolor0", "shadowcolor1",
                                      "noisetex", "tex", "texture", "gtexture", "lightmap", "normals", "specular"})
                    add(n);
                for (const auto& [k, v] : customTextures)
                    add(k.substr(k.find(':') + 1));
                const std::vector<SamplerBind> binds = ResolveAll(progName, names);
                for (const SamplerBind& b : binds)
                    viaKeys[(b.shadow ? "S:" : "P:") + b.name] =
                        b.kind == Kind::Atlas ? std::string("atlas") : std::to_string(ChooseSampler(b));
            }
            auto fut = pool.Submit([vertex = std::move(vertex), fragment = std::move(fragment), opts,
                                    kind = kindUsed, dxbc, cacheFile, via, viaKeys]() {
                auto c = std::make_shared<Converted>();
                CachedProgram cached;
                if (!via && LoadCache(cacheFile, cached)) {
                    c->transform.uniforms = std::move(cached.uniforms);
                    c->transform.fragment.drawBuffers = std::move(cached.drawBuffers);
                    c->program.ok = true;
                    c->program.kind = kind;
                    c->program.vertex = std::move(cached.vertex);
                    c->program.fragment = std::move(cached.fragment);
                    c->program.samplers = std::move(cached.samplers);
                    c->program.images = std::move(cached.images);
                    c->program.uniformRegs = cached.uniformRegs;
                    c->ok = true;
                    if (std::getenv("JSHADER_TRACE"))
                        std::fprintf(stderr, "[jshader] program loaded from the cache\n");
                    return c;
                }
                c->transform = jshader::TransformProgram(vertex, fragment, opts);
                if (!c->transform.ok()) {
                    c->error =
                        c->transform.vertex.ok ? c->transform.fragment.error : c->transform.vertex.error;
                    return c;
                }
                c->program = jshader::BuildRhiProgram(c->transform, kind, dxbc);
                c->ok = c->program.ok;
                if (via && c->ok) {
                    for (auto [code, stage] : {std::pair{&c->program.vertex, jshader::Stage::Vertex},
                                               std::pair{&c->program.fragment, jshader::Stage::Fragment}}) {
                        std::string text(code->begin(), code->end());
                        if (!viaKeys.empty()) {
                            const std::string plain = jshader::MergeHlslSamplers(text, [&](const std::string& t, bool cmp) {
                                const auto it = viaKeys.find((cmp ? "S:" : "P:") + t);
                                return it != viaKeys.end() ? it->second : std::string();
                            });
                            text = plain;
                        }
                        std::vector<uint32_t> spirv;
                        std::string log;
                        if (!jshader::HlslToSpirv(text, stage, spirv, log)) {
                            c->ok = false;
                            c->error = "hlsl for vulkan: " + log.substr(0, 800);
                            break;
                        }
                        code->assign(reinterpret_cast<const uint8_t*>(spirv.data()),
                                     reinterpret_cast<const uint8_t*>(spirv.data() + spirv.size()));
                    }
                    c->program.kind = jshader::RhiShaderKind::SpirV;
                }
                if (const char* dumpName = std::getenv("JSHADER_DUMP_HLSL")) {
                    // writes the converted hlsl of every program next to the given prefix
                    const std::string base = dumpName;
                    std::ofstream(base + std::to_string(std::hash<std::string>()(fragment.text)) + ".ps.hlsl")
                        << c->program.fragmentSource;
                    std::ofstream(base + std::to_string(std::hash<std::string>()(fragment.text)) + ".vs.hlsl")
                        << c->program.vertexSource;
                }
                if (!c->ok) {
                    c->error = c->program.error;
                } else if (!via) {
                    CachedProgram save;
                    save.uniforms = c->transform.uniforms;
                    save.drawBuffers = c->transform.fragment.drawBuffers;
                    save.samplers = c->program.samplers;
                    save.images = c->program.images;
                    save.uniformRegs = c->program.uniformRegs;
                    save.vertex = c->program.vertex;
                    save.fragment = c->program.fragment;
                    SaveCache(cacheFile, save);
                }
                return c;
            });
            it = pending.emplace(key, fut).first;
        }
        if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return nullptr;
        std::shared_ptr<Converted> c = it->second.get();
        if (!c->ok) {
            Log("jshader", "program {} failed: {}", progName, c->error.substr(0, 1500));
            if (const char* dump = std::getenv("JSHADER_DUMP"))
                if (progName == dump)
                    std::fprintf(stderr, "---- %s vertex (failed)\n%s\n---- fragment (failed)\n%s\n",
                                 progName.c_str(), c->transform.vertex.glsl.c_str(),
                                 c->transform.fragment.glsl.c_str());
            lastError = c->error;
            programs[key] = std::make_unique<Program>();
            return nullptr;
        }
        if (const char* dump = std::getenv("JSHADER_DUMP"))
            if (progName == dump)
                std::fprintf(stderr, "---- %s vertex\n%s\n---- fragment\n%s\n", progName.c_str(),
                             c->transform.vertex.glsl.c_str(), c->transform.fragment.glsl.c_str());
        Program* p = Finish(key, progName, *c, source);
        // alphaTestRef: the reference of the comparison the program was built with
        if (const size_t sp = alpha.find(' '); sp != std::string::npos)
            p->alphaRef = static_cast<float>(std::atof(alpha.c_str() + sp + 1));
        if (!p->valid) {
            Log("jshader", "program {} could not be created on this device", progName.c_str());
            return nullptr;
        }
        return p;
    }

    int JavaPack::WarmUp() {
        // every program the pack draws with, with the alpha test each draw asks for, so nothing (the hand, the
        // player, particles) pops in after the pack starts. reading a program's source happens here on the render
        // thread; converting and compiling runs on the pool. a small time budget keeps the menus responsive.
        struct Want {
            const char* name;
            float alpha;
            bool back;
        };
        std::vector<Want> wants = {
            {"gbuffers_terrain_solid", -1.0f, false},
            {"gbuffers_terrain_cutout", 0.5f, false},
            {"gbuffers_water", 0.0001f, false},
            {"gbuffers_skybasic", -1.0f, false},
            {"gbuffers_skytextured", -1.0f, false},
            {"gbuffers_entities", -1.0f, false},
            {"gbuffers_entities", 0.1f, false},
            {"gbuffers_hand", 0.1f, false},
            {"gbuffers_entities_translucent", 0.1f, false},
            {"gbuffers_hand_water", 0.1f, false},
            {"gbuffers_spidereyes", 0.0001f, false},
            {"gbuffers_particles", 0.1f, false},
            {"gbuffers_particles_translucent", 0.1f, false},
            {"gbuffers_weather", 0.1f, false},
            {"gbuffers_block", 0.1f, false},
            {"gbuffers_block_translucent", 0.1f, false},
            {"gbuffers_damagedblock", 0.1f, false},
            {"gbuffers_armor_glint", 0.0001f, false},
            {"gbuffers_clouds", 0.1f, false},
            {"gbuffers_line", -1.0f, false},
            {"gbuffers_lightning", -1.0f, false},
        };
        if (config.shadowEnabled)
            for (const Want& w : std::initializer_list<Want>{{"shadow_solid", -1.0f, false},
                                                             {"shadow_cutout", 0.5f, false},
                                                             {"shadow_water", -1.0f, false},
                                                             {"shadow_entities", 0.1f, false},
                                                             {"shadow_block", 0.1f, false}})
                wants.push_back(w);
        std::vector<std::string> passNames;
        for (const char* base : {"begin", "prepare", "deferred", "composite", "shadowcomp"})
            for (const std::string& n : instance->PassChain(base))
                if (instance->ProgramEnabled(n) &&
                    (std::string(base) != "shadowcomp" || config.shadowEnabled))
                    passNames.push_back(n);
        for (const std::string& n : passNames)
            wants.push_back({n.c_str(), -2.0f, false}); // full screen passes run without an alpha test
        wants.push_back({"final", -2.0f, true});

        const auto started = std::chrono::steady_clock::now();
        int remaining = 0, done = 0, total = 0;
        std::string current;
        for (const Want& w : wants) {
            if (!instance->ProgramExists(w.name) && std::strcmp(w.name, "final") != 0) {
                // a missing program falls back (gbuffers_hand -> textured_lit ...); count it only if one loads
                const auto src = instance->Program(w.name);
                if (!src || !src->found)
                    continue;
            }
            const std::string alpha = w.back || w.alpha < -1.5f ? std::string() : AlphaFor(w.name, w.alpha);
            const std::string key = KeyFor(w.name, w.back) + (alpha.empty() ? "" : "@" + alpha);
            total++;
            if (programs.contains(key)) {
                done++;
                continue;
            }
            const bool overBudget =
                std::chrono::steady_clock::now() - started > std::chrono::milliseconds(12);
            if (!pending.contains(key) && overBudget) {
                remaining++;
                if (current.empty())
                    current = w.name;
                continue;
            }
            GetProgram(w.name, w.back, nullptr, nullptr, rhi::Format::Unknown, alpha);
            if (programs.contains(key)) {
                done++;
            } else {
                remaining++;
                if (current.empty())
                    current = w.name;
            }
        }
        {
            std::lock_guard lock(progressMutex);
            warmProgress.done = done;
            warmProgress.total = total;
            warmProgress.current = current;
        }
        return remaining;
    }

    JavaPack::WarmProgress JavaPack::Progress() const {
        std::lock_guard lock(progressMutex);
        return warmProgress;
    }

    bool JavaPack::TerrainUsable() {
        const std::string alpha = AlphaFor("gbuffers_terrain_solid", -1.0f);
        const auto it =
            programs.find(KeyFor("gbuffers_terrain_solid", false) + (alpha.empty() ? "" : "@" + alpha));
        return it != programs.end() && it->second->valid;
    }

    rhi::PipelineHandle JavaPack::PipelineFor(Program& p, const PipeState& st) {
        // a flat key: this runs for every draw
        PipeKey k{};
        k.program = &p;
        uint8_t* b = k.bytes.data();
        *b++ = st.scene ? 1 : 0;
        *b++ = static_cast<uint8_t>(st.colorCount);
        *b++ = static_cast<uint8_t>(st.depth);
        *b++ = static_cast<uint8_t>((st.depthTest ? 1 : 0) | (st.depthWrite ? 2 : 0) | (st.blend ? 4 : 0) |
                                    (st.cull ? 8 : 0));
        *b++ = static_cast<uint8_t>(st.depthOp);
        *b++ = static_cast<uint8_t>(st.src);
        *b++ = static_cast<uint8_t>(st.dst);
        *b++ = static_cast<uint8_t>(st.srcA);
        *b++ = static_cast<uint8_t>(st.dstA);
        *b++ = static_cast<uint8_t>(st.mask);
        for (uint32_t i = 0; i < st.colorCount && i < 8; i++)
            *b++ = static_cast<uint8_t>(st.colors[i]);
        *b++ = fragmentOverride ? 1 : 0;
        const auto it = pipelines.find(k);
        if (it != pipelines.end())
            return it->second;
        rhi::GraphicsPipelineDesc gd;
        gd.vertex = p.vs;
        gd.fragment = fragmentOverride ? fragmentOverride : p.fs;
        gd.layout = pipelineLayout;
        using namespace jshader;
        if (st.scene) {
            gd.streams[0] = {kPackVertexBytes, rhi::VertexStep::Vertex};
            gd.streamCount = 1;
            const rhi::VertexAttributeDesc attrs[] = {
                {static_cast<uint32_t>(kAttribPosition), 0, rhi::Format::RGB32Float, kPos},
                {static_cast<uint32_t>(kAttribColor0), 0, rhi::Format::RGBA8Unorm, kColor},
                {static_cast<uint32_t>(kAttribTexCoord0), 0, rhi::Format::RG32Float, kUv},
                {static_cast<uint32_t>(kAttribTexCoord5), 0, rhi::Format::RG32Float, kLight},
                {static_cast<uint32_t>(kAttribNormal), 0, rhi::Format::RGBA16Float, kNormal},
                {static_cast<uint32_t>(kAttribTexCoord2), 0, rhi::Format::RG32Float, kEntity},
                {static_cast<uint32_t>(kAttribTexCoord3), 0, rhi::Format::RG32Float, kMid},
                {static_cast<uint32_t>(kAttribTexCoord4), 0, rhi::Format::RGBA16Float, kMidBlock},
                {static_cast<uint32_t>(kAttribTangent), 0, rhi::Format::RGBA16Float, kTangent}};
            for (const rhi::VertexAttributeDesc& a : attrs)
                gd.attributes[gd.attributeCount++] = a;
        } else {
            gd.streams[0] = {sizeof(QuadVertex), rhi::VertexStep::Vertex};
            gd.streamCount = 1;
            gd.attributes[0] = {static_cast<uint32_t>(kAttribPosition), 0, rhi::Format::RGB32Float, 0};
            gd.attributes[1] = {static_cast<uint32_t>(kAttribTexCoord0), 0, rhi::Format::RG32Float, 12};
            gd.attributeCount = 2;
        }
        // offscreen targets are drawn upside down (flipVertexY), which turns the winding around
        gd.raster.cull = st.cull ? rhi::CullMode::Back : rhi::CullMode::None;
        gd.raster.frontFace = rhi::FrontFace::CounterClockwise;
        gd.depthStencil.depthTest = st.depthTest;
        gd.depthStencil.depthWrite = st.depthWrite;
        gd.depthStencil.depthCompare = st.depthOp;
        for (uint32_t i = 0; i < st.colorCount; i++) {
            gd.colorFormats[i] = st.colors[i];
            rhi::BlendTarget& bt = gd.blend.targets[i];
            bt.enable = st.blend;
            bt.srcColor = st.src;
            bt.dstColor = st.dst;
            bt.srcAlpha = st.srcA;
            bt.dstAlpha = st.dstA;
            bt.writeMask = st.mask;
        }
        // iris PER_BUFFER_BLENDING: blend.<program>.<buffer> overrides the draw buffer writing that colortex
        if (p.source) {
            const std::string& n = p.source->sourceName.empty() ? p.source->name : p.source->sourceName;
            if (const jshader::ProgramConfig* pc = config.Find(n); pc && !pc->blendPerBuffer.empty())
                for (uint32_t i = 0; i < st.colorCount && i < p.attachments.size(); i++) {
                    const auto it = pc->blendPerBuffer.find(p.attachments[i]);
                    if (it == pc->blendPerBuffer.end() || !it->second.set)
                        continue;
                    rhi::BlendTarget& bt = gd.blend.targets[i];
                    bt.enable = !it->second.off;
                    if (bt.enable) {
                        bt.srcColor = GlFactor(it->second.src);
                        bt.dstColor = GlFactor(it->second.dst);
                        bt.srcAlpha = GlFactor(it->second.srcA);
                        bt.dstAlpha = GlFactor(it->second.dstA);
                    }
                }
        }
        gd.colorCount = st.colorCount;
        gd.depthFormat = st.depth;
        gd.name = "java pack";
        const rhi::PipelineHandle h = gpu.Dev().CreateGraphicsPipeline(gd);
        pipelines.emplace(k, h);
        return h;
    }

    // -- per draw state ------------------------------------------------------------------------------------------------

    void JavaPack::FillUniforms(Program& p, const jshader::PackDrawState& draw, bool perDrawOnly) {
        jshader::UniformTable::Context ctx;
        ctx.frame = &frameState;
        ctx.draw = &draw;
        float natural[96];
        for (UniformSlot& s : p.slots) {
            if (perDrawOnly && !s.perDraw)
                continue;
            std::memset(natural, 0, sizeof(natural));
            int comps = 0;
            if (s.known) {
                if (!s.resolved) {
                    s.entry = table.Lookup(s.decl.name);
                    s.resolved = true;
                }
                if (s.entry)
                    comps = jshader::UniformTable::EvaluateLookup(s.entry, ctx, natural);
                else
                    table.Evaluate(s.decl.name, ctx, natural, comps);
            }
            float* dst = (perDrawOnly ? p.drawRegs : p.frameRegs).data() + s.decl.reg * 4;
            jshader::PackUniform(s.decl, natural, dst);
        }
    }

    bool JavaPack::BindUniforms(rhi::CommandList& list, Program& p, const jshader::PackDrawState& drawIn) {
        jshader::PackDrawState draw = drawIn;
        draw.alphaTestRef = p.alphaRef;
        if (p.frameStamp != frameStamp) {
            p.frameStamp = frameStamp;
            p.haveDraw = false;
            FillUniforms(p, jshader::PackDrawState(), false);
            p.drawRegs = p.frameRegs;
        }
        if (p.anyPerDraw &&
            (!p.haveDraw || std::memcmp(&p.lastDraw, &draw, sizeof(jshader::PackDrawState)) != 0)) {
            FillUniforms(p, draw, true);
            p.lastDraw = draw;
            p.haveDraw = true;
        }
        const uint64_t bytes = static_cast<uint64_t>(std::max(1, p.regs)) * 16;
        const RingSlice slice =
            gpu.Ring(bytes, std::max<uint32_t>(256, gpu.Caps().limits.uniformBufferAlignment));
        if (!slice.data)
            return false;
        std::memcpy(slice.data, p.drawRegs.data(), static_cast<size_t>(bytes));
        const uint32_t offset = static_cast<uint32_t>(slice.offset);
        list.SetBindGroup(0, gpu.UniformGroup(slice.chunk), std::span<const uint32_t>(&offset, 1));
        return true;
    }

    rhi::BindGroupHandle JavaPack::GroupFor(const PackSet& set) {
        const auto it = groups.find(set);
        if (it != groups.end())
            return it->second;
        std::vector<rhi::BindGroupEntry> entries;
        for (uint32_t s = 0; s < kTextureSlots; s++) {
            rhi::BindGroupEntry t;
            t.binding = s * 2;
            t.texture.texture = set.texture[s] ? set.texture[s] : gpu.White2D();
            t.texture.viewDim = set.texture[s] ? set.dim[s] : rhi::TextureDim::Tex2D;
            entries.push_back(t);
            rhi::BindGroupEntry smp;
            smp.binding = s * 2 + 1;
            smp.sampler = SamplerFor(set.sampler[s]);
            entries.push_back(smp);
        }
        rhi::BindGroupDesc gd;
        gd.layout = textureLayout;
        gd.entries = entries;
        gd.name = "java pack textures";
        const rhi::BindGroupHandle g = gpu.Dev().CreateBindGroup(gd);
        groups.emplace(set, g);
        return g;
    }

    // the sampler state a texture is read with (the same for every draw, apart from the end sky's repeating atlas)
    int JavaPack::ChooseSampler(const SamplerBind& s) const {
        switch (s.kind) {
        case Kind::Color:
            return config.color[static_cast<size_t>(s.index)].linear ? SLinearClamp : SPointClamp;
        case Kind::Depth:
            return SPointClamp;
        case Kind::ShadowDepth: {
            const size_t fi = s.index == 1 ? 1 : 0;
            if (s.shadow)
                return config.shadowFilterLinear[fi] ? SCompareLinear : SComparePoint;
            return config.shadowFilterLinear[fi] ? SLinearClamp : SPointClamp;
        }
        case Kind::ShadowColor:
            return config.shadowColorLinear[s.index] ? SLinearClamp : SPointClamp;
        case Kind::Noise:
            return noiseSampler;
        case Kind::Atlas:
            return textureOverride && overrideRepeat ? SPointRepeat : SAtlas;
        case Kind::Custom: {
            const auto it = customSamplers.find(s.customKey);
            return it != customSamplers.end() ? it->second : SLinearClamp;
        }
        case Kind::Image:
            // iris samples custom images with linear filtering, integer ones nearest
            return images[static_cast<size_t>(s.index)].integer ? SPointClamp : SLinearClamp;
        default:
            return SLinearClamp;
        }
    }

    // direct3d 11 has 16 sampler registers a stage, and packs read more textures than that, each with its own
    // sampler: textures read with the same sampler state share one sampler variable there (the atlas stays apart,
    // its state changes for the end sky)
    std::string JavaPack::MergeHlslSamplers(const std::string& src, const std::vector<SamplerBind>& binds) const {
        return jshader::MergeHlslSamplers(src, [&](const std::string& texture, bool) -> std::string {
            for (const SamplerBind& b : binds)
                if (b.name == texture)
                    return b.kind == Kind::Atlas ? std::string("atlas") : std::to_string(ChooseSampler(b));
            return {};
        });
    }

    rhi::BindGroupHandle JavaPack::Textures(rhi::CommandList& list, Program& p,
                                            const std::vector<rhi::TextureHandle>& attached) {
        PackSet set;
        for (size_t i = 0; i < kTextureSlots; i++) {
            set.texture[i] = white;
            set.sampler[i] = SLinearClamp;
            set.dim[i] = rhi::TextureDim::Tex2D;
        }
        const auto isAttached = [&](rhi::TextureHandle t) {
            return std::find(attached.begin(), attached.end(), t) != attached.end();
        };
        for (const SamplerBind& s : p.samplers) {
            rhi::TextureHandle t = white;
            const int smp = ChooseSampler(s);
            switch (s.kind) {
            case Kind::Color: {
                const Buffer& b = bufs[static_cast<size_t>(s.index)];
                t = b.used ? b.tex[b.cur] : blackTex;
                break;
            }
            case Kind::Depth:
                t = depthTexUsed[s.index] ? depth[s.index] : depth[0];
                break;
            case Kind::ShadowDepth: {
                const rhi::TextureHandle depthTex =
                    s.index == 1 && shadowDepthNoTranslucents ? shadowDepthNoTranslucents : shadowDepth;
                t = depthTex ? depthTex : white;
                break;
            }
            case Kind::ShadowColor:
                t = shadowColor[s.index] ? shadowColor[s.index] : white;
                break;
            case Kind::Noise:
                t = noise;
                break;
            case Kind::Atlas:
                if (textureOverride)
                    t = textureOverride;
                else if (atlas)
                    t = atlas;
                break;
            case Kind::Lightmap:
                if (lightmapTex)
                    t = lightmapTex;
                break;
            case Kind::Normals:
                t = flatNormal;
                break;
            case Kind::Specular:
            case Kind::Overlay:
                t = blackTex;
                break;
            case Kind::Custom:
                t = customTextures[s.customKey];
                break;
            case Kind::Image: {
                const Image& img = images[static_cast<size_t>(s.index)];
                t = img.tex;
                set.dim[static_cast<size_t>(s.slot)] = img.dim;
                break;
            }
            case Kind::Dummy:
                break;
            }
            if (t && isAttached(t)) {
                // iris samples a buffer the pass also draws into (gbuffers_water reading colortex4 for its
                // reflections sees the lit scene): a copy taken just before the pass reads the same
                if (trace)
                    std::fprintf(stderr, "[jshader] %s samples %s while drawing to it\n",
                                 p.source ? p.source->name.c_str() : "?", s.name.c_str());
                t = FeedbackCopy(list, t);
            }
            if (!t) {
                t = white;
                set.dim[static_cast<size_t>(s.slot)] = rhi::TextureDim::Tex2D;
            }
            set.texture[static_cast<size_t>(s.slot)] = t;
            set.sampler[static_cast<size_t>(s.slot)] = static_cast<uint8_t>(smp);
        }
        // images the program stores to stay writable for the whole pass (sampling them too reads that state)
        const auto stored = [&](rhi::TextureHandle t) {
            for (int slot : p.imageSlots)
                if (images[static_cast<size_t>(slot)].tex == t)
                    return true;
            return false;
        };
        for (size_t i = 0; i < kTextureSlots; i++)
            if (set.texture[i] && !stored(set.texture[i]))
                gpu.Transition(list, set.texture[i], rhi::State::ShaderRead);
        for (int slot : p.imageSlots)
            gpu.Transition(list, images[static_cast<size_t>(slot)].tex, rhi::State::ShaderWrite);
        return GroupFor(set);
    }

    // -- custom images, storage buffers and compute ----------------------------------------------------------------------

    bool JavaPack::CreateStorage() {
        rhi::Device& dev = gpu.Dev();
        constexpr rhi::TextureUsage kImage =
            rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage | rhi::TextureUsage::CopyDst;
        uint64_t zero = 256;
        for (Image& img : images) {
            const jshader::CustomImage& info = img.info;
            img.w = info.relative
                        ? std::max(1u, static_cast<uint32_t>(static_cast<float>(width) * info.relativeWidth))
                        : static_cast<uint32_t>(info.width);
            img.h =
                info.relative
                    ? std::max(1u, static_cast<uint32_t>(static_cast<float>(height) * info.relativeHeight))
                    : static_cast<uint32_t>(info.height);
            img.d = img.dim == rhi::TextureDim::Tex3D ? static_cast<uint32_t>(info.depth) : 1u;
            rhi::TextureDesc td;
            td.dim = img.dim;
            td.format = img.format;
            td.width = img.w;
            td.height = img.h;
            td.depthOrLayers = img.d;
            td.usage = kImage;
            td.name = info.name.c_str();
            img.tex = dev.CreateTexture(td);
            if (!img.tex) {
                Log("jshader", "image {} ({}x{}x{}) could not be created", info.name.c_str(), img.w, img.h,
                    img.d);
                continue;
            }
            gpu.SetState(img.tex, rhi::State::Undefined);
            owned.push_back(img.tex);
            const uint64_t pitch =
                (static_cast<uint64_t>(img.w) * rhi::GetFormatInfo(img.format).bytesPerBlock + 255) &
                ~uint64_t(255);
            zero = std::max(zero, pitch * img.h * img.d);
        }
        // a 1x1 storage texture fills the image slots a program does not use
        rhi::TextureDesc dd;
        dd.format = rhi::Format::RGBA8Unorm;
        dd.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
        dd.name = "pack dummy image";
        dummyStorage = dev.CreateTexture(dd);
        if (dummyStorage) {
            gpu.SetState(dummyStorage, rhi::State::Undefined);
            owned.push_back(dummyStorage);
        }
        rhi::BufferDesc bd;
        bd.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::CopyDst;
        for (const jshader::BufferObject& b : config.bufferObjects) {
            bd.size =
                b.relative
                    ? std::max<uint64_t>(16, static_cast<uint64_t>(static_cast<double>(b.size) *
                                                                   (width * b.scaleX) * (height * b.scaleY)))
                    : std::max<uint64_t>(16, b.size);
            bd.name = "pack ssbo";
            ssbo[static_cast<size_t>(b.index)] = dev.CreateBuffer(bd);
            zero = std::max(zero, bd.size);
        }
        bd.size = 256;
        bd.name = "pack dummy ssbo";
        dummySsbo = dev.CreateBuffer(bd);
        rhi::BufferDesc zd;
        zd.size = zero;
        zd.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::CopyDst;
        zd.name = "pack zeroes";
        zeroBuffer = dev.CreateBuffer(zd);
        zeroBytes = zero;
        storageFresh = true;
        if (!dummyStorage || !dummySsbo || !zeroBuffer)
            return false;
        std::vector<rhi::BindGroupEntry> entries;
        for (uint32_t i = 0; i < kSsboSlots; i++) {
            rhi::BindGroupEntry e;
            e.binding = i;
            e.buffer.buffer = ssbo[i] ? ssbo[i] : dummySsbo;
            entries.push_back(e);
        }
        rhi::BindGroupDesc gd;
        gd.layout = bufferLayout;
        gd.entries = entries;
        gd.name = "java pack buffers";
        bufferGroup = dev.CreateBindGroup(gd);
        return static_cast<bool>(bufferGroup);
    }

    // fresh storage is zeroed (iris clears images and buffers when it creates them); clear=true images every frame
    void JavaPack::ClearImages(rhi::CommandList& list, bool all) {
        if (!zeroBuffer)
            return;
        if (storageFresh) {
            list.Barrier(rhi::BufferBarrier{zeroBuffer, rhi::State::Undefined, rhi::State::CopyDst});
            list.FillBuffer(zeroBuffer, 0, zeroBytes, 0);
            list.Barrier(rhi::BufferBarrier{zeroBuffer, rhi::State::CopyDst, rhi::State::CopySrc});
            for (rhi::BufferHandle b : ssbo) {
                if (!b)
                    continue;
                const rhi::BufferDesc* d = gpu.Dev().GetDesc(b);
                list.Barrier(rhi::BufferBarrier{b, rhi::State::Undefined, rhi::State::CopyDst});
                if (d)
                    list.CopyBuffer(b, 0, zeroBuffer, 0, std::min(d->size, zeroBytes));
                list.Barrier(rhi::BufferBarrier{b, rhi::State::CopyDst, rhi::State::ShaderWrite});
            }
            list.Barrier(rhi::BufferBarrier{dummySsbo, rhi::State::Undefined, rhi::State::ShaderWrite});
            gpu.Transition(list, dummyStorage, rhi::State::ShaderWrite);
        }
        for (Image& img : images) {
            if (!img.tex || !(all || storageFresh || img.info.clear))
                continue;
            gpu.Transition(list, img.tex, rhi::State::CopyDst);
            rhi::BufferTextureCopy c;
            c.bufferRowPitch = static_cast<uint32_t>(
                (static_cast<uint64_t>(img.w) * rhi::GetFormatInfo(img.format).bytesPerBlock + 255) &
                ~uint64_t(255));
            c.width = img.w;
            c.height = img.h;
            c.depth = img.d;
            list.CopyBufferToTexture(img.tex, zeroBuffer, std::span<const rhi::BufferTextureCopy>(&c, 1));
            gpu.Transition(list, img.tex, rhi::State::ShaderRead);
        }
        storageFresh = false;
    }

    rhi::BindGroupHandle JavaPack::ImageGroup(uint32_t mask) {
        const auto it = imageGroups.find(mask);
        if (it != imageGroups.end())
            return it->second;
        std::vector<rhi::BindGroupEntry> entries;
        for (uint32_t i = 0; i < kImageSlots; i++) {
            rhi::BindGroupEntry e;
            e.binding = i;
            const bool used = (mask >> i) & 1u && i < images.size() && images[i].tex;
            e.texture.texture = used ? images[i].tex : dummyStorage;
            e.texture.viewDim = used ? images[i].dim : rhi::TextureDim::Tex2D;
            entries.push_back(e);
        }
        rhi::BindGroupDesc gd;
        gd.layout = imageLayout;
        gd.entries = entries;
        gd.name = "java pack images";
        const rhi::BindGroupHandle g = gpu.Dev().CreateBindGroup(gd);
        imageGroups.emplace(mask, g);
        return g;
    }

    void JavaPack::BindStorage(rhi::CommandList& list, const Program& p) {
        const rhi::BindGroupHandle g = ImageGroup(p.imageMask);
        if (g)
            list.SetBindGroup(2, g);
        if (bufferGroup)
            list.SetBindGroup(3, bufferGroup);
    }

    // writes of one pass become visible to the next (images stay writable, storage buffers always are)
    void JavaPack::StorageBarrier(rhi::CommandList& list, const Program& p) {
        for (int slot : p.imageSlots)
            list.UavBarrier(images[static_cast<size_t>(slot)].tex);
        if (!config.bufferObjects.empty())
            for (rhi::BufferHandle b : ssbo)
                if (b)
                    list.UavBarrier(b);
    }

    JavaPack::Program* JavaPack::GetCompute(const std::string& progName, size_t index) {
        const std::string key = "cs:" + progName + ":" + std::to_string(index);
        const auto ready = programs.find(key);
        if (ready != programs.end())
            return ready->second->valid ? ready->second.get() : nullptr;
        auto it = pending.find(key);
        if (it == pending.end()) {
            const std::shared_ptr<jshader::ProgramSource> source = instance->Program(progName);
            if (!source || index >= source->compute.size()) {
                programs[key] = std::make_unique<Program>();
                return nullptr;
            }
            jshader::TransformOptions opts;
            opts.macros = instance->EngineMacros();
            opts.preprocess = jshader::PreprocessGlsl;
            opts.zeroToOneDepth = true;
            opts.splitSamplers = true;
            opts.explicitLod = true;
            for (const auto& [imgName, slot] : imageSlotOf) {
                opts.imageSlots[imgName] = slot;
                opts.imageFormats[imgName] =
                    jshader::ImageFormatQualifier(images[static_cast<size_t>(slot)].info.internalFormat);
            }
            const rhi::Api api = gpu.Api();
            const jshader::RhiShaderKind kind = api == rhi::Api::Vulkan  ? jshader::RhiShaderKind::SpirV
                                                : api == rhi::Api::D3D12 ? jshader::RhiShaderKind::Dxbc
                                                : api == rhi::Api::Metal ? jshader::RhiShaderKind::Msl
                                                                         : jshader::RhiShaderKind::Hlsl;
            const jshader::DxbcCompiler dxbc = jshader::SystemDxbcCompiler();
            auto computeText = source->compute[index];
            if (shadowResPatch > 0)
                PatchShadowResolution(computeText.text, shadowResPatch);
            auto fut = pool.Submit([text = std::move(computeText), opts, kind, dxbc]() {
                auto c = std::make_shared<Converted>();
                c->isCompute = true;
                c->computeTransform = jshader::TransformCompute(text, opts);
                if (!c->computeTransform.ok()) {
                    c->error = c->computeTransform.stage.error;
                    return c;
                }
                c->computeProgram = jshader::BuildRhiCompute(c->computeTransform, kind, dxbc);
                c->ok = c->computeProgram.ok;
                if (!c->ok)
                    c->error = c->computeProgram.error;
                return c;
            });
            it = pending.emplace(key, fut).first;
        }
        if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return nullptr;
        const std::shared_ptr<Converted> c = it->second.get();
        pending.erase(it);
        if (!c->ok) {
            Log("jshader", "compute {} #{} failed: {}", progName, index, c->error.substr(0, 1500));
            if (lastError.empty())
                lastError = progName + ".csh: " + c->error.substr(0, 300);
            programs[key] = std::make_unique<Program>();
            return nullptr;
        }
        return FinishCompute(key, progName, *c);
    }

    JavaPack::Program* JavaPack::FinishCompute(const std::string& key, const std::string& progName,
                                               const Converted& c) {
        auto p = std::make_unique<Program>();
        p->compute = true;
        p->dispatch = c.computeTransform;
        const jshader::RhiCompute& rc = c.computeProgram;
        rhi::ShaderDesc sd;
        sd.format = rc.kind == jshader::RhiShaderKind::SpirV  ? rhi::ShaderFormat::SpirV
                    : rc.kind == jshader::RhiShaderKind::Dxbc ? rhi::ShaderFormat::Dxbc
                    : rc.kind == jshader::RhiShaderKind::Msl  ? rhi::ShaderFormat::Msl
                                                              : rhi::ShaderFormat::Hlsl;
        sd.name = progName.c_str();
        if (sd.format == rhi::ShaderFormat::Msl)
            sd.entry = "main0";
        sd.stage = rhi::ShaderStage::Compute;
        std::vector<uint8_t> csCode = rc.code;
        if (sd.format == rhi::ShaderFormat::Hlsl && gpu.Api() == rhi::Api::D3D11) {
            const std::string merged =
                MergeHlslSamplers(std::string(rc.code.begin(), rc.code.end()), ResolveAll(progName, rc.samplers));
            csCode.assign(merged.begin(), merged.end());
        }
        sd.code = csCode;
        p->cs = gpu.Dev().CreateShader(sd);
        p->valid = static_cast<bool>(p->cs);
        FillProgramResources(*p, progName, rc.samplers, rc.images, c.computeTransform.stage.uniforms,
                             rc.uniformRegs);
        if (p->valid) {
            rhi::ComputePipelineDesc cd;
            cd.compute = p->cs;
            cd.layout = pipelineLayout;
            cd.name = progName.c_str();
            p->computePipeline = gpu.Dev().CreateComputePipeline(cd);
            p->valid = static_cast<bool>(p->computePipeline);
        }
        Program* raw = p.get();
        programs[key] = std::move(p);
        return raw;
    }

    bool JavaPack::RunCompute(rhi::CommandList& list, const std::string& progName, uint32_t renderW,
                              uint32_t renderH) {
        const std::shared_ptr<jshader::ProgramSource> source = instance->Program(progName);
        if (!source)
            return true;
        bool allReady = true;
        for (size_t i = 0; i < source->compute.size(); i++) {
            const std::string key = "cs:" + progName + ":" + std::to_string(i);
            Program* p = GetCompute(progName, i);
            if (!p) {
                // still compiling (a failed one is final and skipped)
                if (pending.contains(key))
                    allReady = false;
                continue;
            }
            const rhi::BindGroupHandle textures = Textures(list, *p, {});
            if (!textures)
                continue;
            list.SetPipeline(p->computePipeline);
            jshader::PackDrawState d;
            d.viewHeight = static_cast<int>(renderH);
            if (!BindUniforms(list, *p, d))
                continue;
            list.SetBindGroup(1, textures);
            BindStorage(list, *p);
            const jshader::ComputeResult& cr = p->dispatch;
            uint32_t groups[3] = {1, 1, 1};
            if (cr.fixedGroups) {
                for (int a = 0; a < 3; a++)
                    groups[a] = cr.workGroups[a];
            } else {
                const float sx = static_cast<float>(renderW) * cr.workGroupsRender[0];
                const float sy = static_cast<float>(renderH) * cr.workGroupsRender[1];
                groups[0] = std::max(1u, (static_cast<uint32_t>(std::ceil(sx)) + cr.localSize[0] - 1) /
                                             cr.localSize[0]);
                groups[1] = std::max(1u, (static_cast<uint32_t>(std::ceil(sy)) + cr.localSize[1] - 1) /
                                             cr.localSize[1]);
            }
            const uint32_t limit = 65535;
            if (groups[0] && groups[1] && groups[2] && groups[0] <= limit && groups[1] <= limit &&
                groups[2] <= limit)
                list.Dispatch(groups[0], groups[1], groups[2]);
            StorageBarrier(list, *p);
            if (trace)
                std::fprintf(stderr, "[jshader] compute %s #%zu groups %ux%ux%u\n", progName.c_str(), i,
                             groups[0], groups[1], groups[2]);
        }
        return allReady;
    }

    // -- frame state ---------------------------------------------------------------------------------------------------

    void JavaPack::BuildFrame(const FrameParams& params, const Output& out) {
        jshader::FrameState& s = frameState;
        s = jshader::FrameState();
        const glm::mat4 view = ToMat(params.view);
        glm::mat4 rotation = view;
        rotation[3] = glm::vec4(0, 0, 0, 1);
        // iris: gbufferModelView is the real camera-relative modelview, view bobbing included. dropping the bob's
        // translation (as a pure rotation did) made world positions rebuilt from depth wobble with every step,
        // and shadows looked up with them jumped while walking
        {
            const glm::dmat4 v(view);
            const glm::dmat4 relative =
                v *
                glm::translate(glm::dmat4(1.0), glm::dvec3(params.cameraPosition[0], params.cameraPosition[1],
                                                           params.cameraPosition[2]));
            FromMat(glm::mat4(relative), s.modelView);
        }
        ToGlDepth(params.projection, s.projection);
        if (havePrev) {
            std::memcpy(s.prevModelView, prevModelView, sizeof(prevModelView));
            std::memcpy(s.prevProjection, prevProjection, sizeof(prevProjection));
            std::memcpy(s.previousCameraPosition, prevCamera, sizeof(prevCamera));
        } else {
            std::memcpy(s.prevModelView, s.modelView, sizeof(s.modelView));
            std::memcpy(s.prevProjection, s.projection, sizeof(s.projection));
            for (int i = 0; i < 3; i++)
                s.previousCameraPosition[i] = params.cameraPosition[i];
        }
        std::memcpy(prevModelView, s.modelView, sizeof(prevModelView));
        std::memcpy(prevProjection, s.projection, sizeof(prevProjection));
        for (int i = 0; i < 3; i++) {
            s.cameraPosition[i] = params.cameraPosition[i];
            prevCamera[i] = params.cameraPosition[i];
        }
        havePrev = true;

        // java's nether and end have a fixed time of day (18000 and 6000): the celestial angle is 0.5 and 0
        const bool nether = instance && instance->Dimension() == "world-1",
                   end = instance && instance->Dimension() == "world1";
        const double a = nether ? kPi : (end ? 0.0 : static_cast<double>(params.sky.sunAngle));
        double frac = a / (2.0 * kPi) + 0.25;
        frac -= std::floor(frac);
        s.sunAngle = static_cast<float>(frac);
        s.shadowAngle = s.sunAngle <= 0.5f ? s.sunAngle : s.sunAngle - 0.5f;
        s.worldTime = static_cast<int>(frac * 24000.0) % 24000;
        s.worldDay = 0;
        s.moonPhase = static_cast<int>(std::floor(params.sky.moonPhase)) & 7;
        // iris CelestialUniforms: modelView * rotY(-90) * rotZ(sunPathRotation) * rotX(skyAngle) * (0, 100, 0)
        const float tiltRad = glm::radians(config.sunPathRotation);
        const glm::vec3 sunDir(-std::sin(static_cast<float>(a)),
                               std::cos(static_cast<float>(a)) * std::cos(tiltRad),
                               -std::cos(static_cast<float>(a)) * std::sin(tiltRad));
        const glm::mat3 rot3(rotation);
        const glm::vec3 sunView = rot3 * sunDir * 100.0f, moonView = -sunView,
                        upView = rot3 * glm::vec3(0, 1, 0) * 100.0f;
        for (int i = 0; i < 3; i++) {
            s.sunPosition[i] = sunView[i];
            s.moonPosition[i] = moonView[i];
            s.upPosition[i] = upView[i];
        }
        const bool dayLight = s.sunAngle < 0.5f;
        for (int i = 0; i < 3; i++)
            s.shadowLightPosition[i] = dayLight ? sunView[i] : moonView[i];
        shadowLightWorld = dayLight ? sunDir : -sunDir;
        for (int i = 0; i < 3; i++) {
            s.skyColor[i] = params.sky.skyColor[i];
            s.fogColor[i] = params.fogColor[i];
        }
        // iris FogUniforms: fogStart / fogEnd are vanilla's environmental fog, not the render distance fog.
        // 26.3 FogRenderer: air 0..1024 (rain pulls both in), water -8..96 scaled by the player's water vision,
        // lava 0.25..1. fogDensity is iris's legacy water density (exp2) and 0 elsewhere (linear)
        {
            const float frameDt = std::clamp(params.timeSeconds - fogClock, 0.0f, 0.25f);
            fogClock = params.timeSeconds;
            if (params.cameraMedium == 1)
                waterVisionTicks = std::min(600.0f, waterVisionTicks + frameDt * 20.0f);
            else
                waterVisionTicks = 0.0f;
            // LocalPlayer.getWaterVision
            const float t = waterVisionTicks;
            const float f = std::clamp(t / 100.0f, 0.0f, 1.0f);
            const float g = t < 100.0f ? 0.0f : std::clamp((t - 100.0f) / 500.0f, 0.0f, 1.0f);
            const float waterVision = t >= 600.0f ? 1.0f : f * 0.6f + g * 0.39999998f;
            const float rain = std::clamp(1.0f - params.sky.rainBrightness, 0.0f, 1.0f);
            if (params.cameraMedium == 1) {
                s.fogStart = -8.0f;
                s.fogEnd = 96.0f * std::max(0.25f, waterVision);
                s.fogDensity = 0.05f - waterVision * waterVision * 0.03f;
                s.fogMode = 0x0801; // GL_EXP2
            } else if (params.cameraMedium == 2) {
                s.fogStart = 0.25f;
                s.fogEnd = 1.0f;
                s.fogDensity = 0.0f;
                s.fogMode = 0x2601; // GL_LINEAR
            } else if (nether) {
                // FogRenderer: a dimension with thick fog (the nether) pulls the fog to 5%..50% of the view
                const float view = static_cast<float>(std::clamp(params.renderDistance, 2, 64) * 16);
                s.fogStart = view * 0.05f;
                s.fogEnd = std::min(view, 192.0f) * 0.5f;
                s.fogDensity = 0.0f;
                s.fogMode = 0x2601;
            } else {
                s.fogStart = 0.0f - 160.0f * rain;
                s.fogEnd = std::max(std::min(96.0f, 1024.0f), 1024.0f - 256.0f * rain);
                s.fogDensity = 0.0f;
                s.fogMode = 0x2601;
            }
            s.fogShape = 1; // iris keeps 1 (cylindrical) as its stable value
        }
        s.rainStrength = std::clamp(1.0f - params.sky.rainBrightness, 0.0f, 1.0f);
        s.thunderStrength = std::clamp(params.thunder, 0.0f, 1.0f);
        // the level clock when the session has one (moon phases, day counters)
        if (params.worldTick >= 0) {
            s.worldTime = params.worldTick % 24000;
            s.worldDay = std::max(0, params.worldDay);
        }
        // iris SmoothedFloat / SmoothedVec2f: exponential approach with the pack's half-lives (seconds)
        const float dtSmooth = std::clamp(params.timeSeconds - smoothAt, 0.0f, 1.0f);
        smoothAt = params.timeSeconds;
        const auto approach = [&](float& v, float target, float halflife, bool first) {
            if (first || halflife <= 0.0f) {
                v = target;
                return;
            }
            v += (target - v) * (1.0f - std::exp(-dtSmooth * 0.69314718f / halflife));
        };
        const bool firstSmooth = !smoothStarted;
        smoothStarted = true;
        // wetness follows rain with its own fade in and out
        approach(wetnessSmooth, s.rainStrength,
                 // iris counts these half-lives in tenths of a second
                 s.rainStrength > wetnessSmooth ? config.wetnessHalflife * 0.1f
                                                : config.drynessHalflife * 0.1f,
                 firstSmooth);
        s.wetness = wetnessSmooth;
        // light at the eye as the game has it: 0-240 per channel (block, sky), smoothed with eyeBrightnessHalflife
        s.eyeBrightness[0] = std::clamp(params.eyeBlockLight, 0, 15) * 16;
        s.eyeBrightness[1] = noSkyLight ? 0 : std::clamp(params.eyeSkyLight, 0, 15) * 16;
        for (int i = 0; i < 2; i++) {
            approach(eyeSmooth[i], static_cast<float>(s.eyeBrightness[i]),
                     config.eyeBrightnessHalflife * 0.1f, firstSmooth);
            s.eyeBrightnessSmooth[i] = static_cast<int>(std::lround(eyeSmooth[i]));
        }
        // the biome at the eye, numbered as iris numbers java's biomes
        {
            const int id = jshader::JavaBiomeId(params.biome);
            const auto& biome = jshader::JavaBiomes()[static_cast<size_t>(id)];
            s.biome = id;
            s.biomeCategory = biome.category;
            s.biomePrecipitation = jshader::BiomePrecipitationAt(id, params.cameraPosition[1]);
            s.biomeTemperature = biome.temperature;
            s.biomeRainfall = biome.downfall;
            s.inSwamp = biome.category == 14;
        }
        s.frameCounter = static_cast<int>(params.frameIndex);
        s.frameTimeCounter = params.timeSeconds;
        s.frameTime = std::clamp(params.timeSeconds - lastTime, 0.0f, 0.25f);
        lastTime = params.timeSeconds;
        const float c = s.projection[10], d = s.projection[14];
        if (std::fabs(c - 1.0f) > 1e-6f && std::fabs(c + 1.0f) > 1e-6f)
            s.nearPlane = d / (c - 1.0f);
        // iris: `far` is the render distance in blocks (packs place their border fog with it), not the
        // projection's far plane
        s.farPlane = static_cast<float>(std::clamp(params.renderDistance, 2, 64) * 16);
        s.isEyeInWater = params.cameraMedium == 1 ? 1 : (params.cameraMedium == 2 ? 2 : 0);
        s.eyeAltitude = params.cameraPosition[1];
        s.heldBlockLightValue = static_cast<int>(params.heldLight);
        s.atlasSize[0] = atlasWidth;
        s.atlasSize[1] = gridRows * atlasTilePx;
        s.cloudTime = static_cast<float>(params.cloud.gameTicks);
        s.screenBrightness = 0.5f;
        s.dimensionNether = instance && instance->Dimension() == "world-1";
        s.dimensionEnd = instance && instance->Dimension() == "world1";
        s.heavyFog = s.dimensionNether ? 1.0f : 0.0f;
        s.cloudHeight = params.cloud.height;
        s.viewWidth = static_cast<int>(width);
        s.viewHeight = static_cast<int>(height);
        s.dimensionId = instance->Dimension() == "world-1" ? -1 : (instance->Dimension() == "world1" ? 1 : 0);

        // the shadow camera, as iris ShadowMatrices builds it: rotX(90) * rotZ(-skyAngle * 360) *
        // rotX(sunPathRotation), shifted by the camera's offset inside a shadowIntervalSize grid cell; it takes
        // camera-relative positions. the projection is a symmetric ortho box of +-shadowDistance between the pack's
        // near and far planes (gl depth range, like gbufferProjection)
        const glm::dvec3 camera(params.cameraPosition[0], params.cameraPosition[1], params.cameraPosition[2]);
        {
            const float shadowAngle = s.shadowAngle;
            const float skyAngle = shadowAngle < 0.25f ? shadowAngle + 0.75f : shadowAngle - 0.25f;
            glm::mat4 rot(1.0f);
            rot = glm::rotate(rot, glm::radians(90.0f), glm::vec3(1, 0, 0));
            rot = glm::rotate(rot, glm::radians(skyAngle * -360.0f), glm::vec3(0, 0, 1));
            rot = glm::rotate(rot, glm::radians(config.sunPathRotation), glm::vec3(1, 0, 0));
            glm::vec3 offset(0.0f);
            const float interval = config.shadowIntervalSize;
            if (std::fabs(interval) > 0.0f) {
                const float half = interval / 2.0f;
                offset = glm::vec3(std::fmod(static_cast<float>(camera.x), interval) - half,
                                   std::fmod(static_cast<float>(camera.y), interval) - half,
                                   std::fmod(static_cast<float>(camera.z), interval) - half);
            }
            const glm::mat4 mv = glm::translate(rot, offset);
            FromMat(mv, shadowView);
            const float hpl = std::max(1.0f, config.shadowDistance);
            FromMat(glm::orthoRH_NO(-hpl, hpl, -hpl, hpl, config.shadowNearPlane, config.shadowFarPlane),
                    shadowProjection);
            // the shadow pass draws world positions: move them to the camera first
            FromMat(mv * glm::translate(glm::mat4(1.0f), -glm::vec3(camera)), shadowDrawView);
            shadowReuse = shadowPartial = false;
            if (config.shadowEnabled && shadowSkipState == 1 && shadowKept && shadowDepth) {
                const double moved = glm::length(camera - keptShadowCamera);
                const float turned = std::fabs(s.shadowAngle - keptShadowAngle);
                if (shadowInterval > 1)
                    shadowReuse = frameStamp % shadowInterval != 0 && moved < 4.0 && turned < 0.01f;
                if (!shadowReuse && shadowCacheOn && shadowCacheValid)
                    shadowPartial = moved < 1.5 && turned < 0.0002f && shadowCacheAge < 300 &&
                                    meshes.Generation() == shadowCacheGeneration;
            }
            if (shadowReuse || shadowPartial) {
                const glm::mat4 reused =
                    glm::translate(keptShadowRot, keptShadowOffset + glm::vec3(camera - keptShadowCamera));
                FromMat(reused, shadowView);
                std::memcpy(shadowDrawView, keptShadowDrawView, sizeof(shadowDrawView));
            } else {
                keptShadowRot = rot;
                keptShadowOffset = offset;
                keptShadowCamera = camera;
                keptShadowAngle = s.shadowAngle;
                std::memcpy(keptShadowDrawView, shadowDrawView, sizeof(shadowDrawView));
                shadowKept = config.shadowEnabled;
            }
        }
        if (config.shadowEnabled) {
            std::memcpy(s.shadowModelView, shadowView, sizeof(shadowView));
            std::memcpy(s.shadowProjection, shadowProjection, sizeof(shadowProjection));
        }
        custom.Evaluate(frameState, table);
    }

    jshader::PackDrawState JavaPack::TerrainDraw() const {
        jshader::PackDrawState d;
        std::memcpy(d.modelView, frame.view, sizeof(d.modelView));
        std::memcpy(d.projection, frameState.projection, sizeof(d.projection));
        const glm::mat3 normalMat(ToMat(frame.view));
        std::memcpy(d.normalMatrix, &normalMat[0][0], sizeof(d.normalMatrix));
        const float lm[16] = {1.0f / 256.0f, 0, 0, 0, 0, 1.0f / 256.0f, 0, 0, 0, 0, 1, 0, 1.0f / 32.0f,
                              1.0f / 32.0f,  0, 1};
        std::memcpy(d.textureMatrix1, lm, sizeof(lm));
        for (int i = 0; i < 3; i++)
            d.fogColor[i] = frameState.fogColor[i];
        d.fogColor[3] = 1.0f;
        d.fogStart = frameState.fogStart;
        d.fogEnd = frameState.fogEnd;
        d.fogDensity = frameState.fogDensity;
        d.viewHeight = frameState.viewHeight;
        return d;
    }

    void JavaPack::ClearGeometry() {
        for (auto& v : layerRuns)
            v.clear();
        for (auto& v : shadowRuns)
            v.clear();
    }

    void JavaPack::GatherGeometry(float camX, float camZ) {
        for (int l = 0; l < kRenderLayerCount; l++) {
            const RenderLayer layer = static_cast<RenderLayer>(l);
            const bool ordered = layer == RenderLayer::Translucent || layer == RenderLayer::TranslucentCull;
            meshes.CollectRuns(layer, ordered, layerRuns[static_cast<size_t>(l)]);
        }
        const float reach = std::max(8.0f, config.shadowDistance) * 1.5f + 24.0f;
        // last frame's shadow camera (it moves in shadowIntervalSize steps), widened a little for the step
        bool haveShadowCull = false;
        glm::mat4 shadowCull(1.0f);
        if (shadowDrawView[15] != 0.0f && shadowProjection[15] != 0.0f) {
            glm::mat4 proj = ToMat(shadowProjection);
            proj[0] *= 0.9f; // ±11% margin in x and y
            proj[1] *= 0.9f;
            shadowCull = proj * ToMat(shadowDrawView);
            haveShadowCull = true;
        }
        for (auto& v : shadowRuns)
            v.clear();
        if (!config.shadowEnabled)
            return;
        const float lightLen = glm::length(shadowLightWorld);
        const bool viewCull = haveViewFrustum && shadowSkipState == 1 && lightLen > 0.5f &&
                              !std::getenv("PERLIN_PACK_NO_SHADOW_CULL");
        const float sweepLen = std::clamp(config.shadowFarPlane - config.shadowNearPlane, 64.0f, 512.0f);
        const glm::vec3 sweep = viewCull ? -shadowLightWorld / lightLen * sweepLen : glm::vec3(0.0f);
        const auto castsIntoView = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
            for (const auto& p : viewFrustum.planes) {
                const float far = p[0] * (p[0] >= 0 ? x1 : x0) + p[1] * (p[1] >= 0 ? y1 : y0) +
                                  p[2] * (p[2] >= 0 ? z1 : z0) + p[3];
                const float shift = p[0] * sweep.x + p[1] * sweep.y + p[2] * sweep.z;
                if (far < 0.0f && far + shift < 0.0f)
                    return false;
            }
            return true;
        };
        shadowCulled = shadowKeptChunks = 0;
        for (const auto& entry : meshes.ChunkMap()) {
            const ChunkMeshStore::Chunk& g = entry.second;
            if (g.page < 0)
                continue;
            const float dx = static_cast<float>(g.cx) * static_cast<float>(g.size) +
                             static_cast<float>(g.size) * 0.5f - camX;
            const float dz = static_cast<float>(g.cz) * static_cast<float>(g.size) +
                             static_cast<float>(g.size) * 0.5f - camZ;
            if (dx * dx + dz * dz > reach * reach)
                continue;
            // iris culls shadow terrain to the shadow camera's box: a column whose corners all fall outside it on
            // one side of x or y is never drawn into the shadow map
            if (haveShadowCull) {
                const float x0 = static_cast<float>(g.cx) * static_cast<float>(g.size),
                            z0 = static_cast<float>(g.cz) * static_cast<float>(g.size);
                const float x1 = x0 + static_cast<float>(g.size), z1 = z0 + static_cast<float>(g.size);
                const float ys[2] = {g.minY, g.maxY > g.minY ? g.maxY : g.minY + 16.0f};
                int outside[4] = {0, 0, 0, 0};
                for (int c = 0; c < 8; c++) {
                    const glm::vec4 p((c & 1) ? x1 : x0, ys[(c >> 1) & 1], (c & 4) ? z1 : z0, 1.0f);
                    const glm::vec4 q = shadowCull * p;
                    outside[0] += q.x < -q.w;
                    outside[1] += q.x > q.w;
                    outside[2] += q.y < -q.w;
                    outside[3] += q.y > q.w;
                }
                if (outside[0] == 8 || outside[1] == 8 || outside[2] == 8 || outside[3] == 8)
                    continue;
            }
            bool outOfView = false;
            if (viewCull) {
                const float x0 = static_cast<float>(g.cx) * static_cast<float>(g.size),
                            z0 = static_cast<float>(g.cz) * static_cast<float>(g.size);
                const float top = g.maxY > g.minY ? g.maxY : g.minY + 16.0f;
                outOfView = !castsIntoView(x0, g.minY, z0, x0 + static_cast<float>(g.size), top,
                                           z0 + static_cast<float>(g.size));
            }
            if (outOfView)
                shadowCulled++;
            else
                shadowKeptChunks++;
            for (size_t slot = 0; slot < static_cast<size_t>(kRenderLayerCount); slot++)
                if (g.layerCount[slot] &&
                    (!outOfView || (shadowCacheOn && slot == static_cast<size_t>(RenderLayer::Solid))))
                    shadowRuns[slot].push_back(
                        {static_cast<uint32_t>(g.page), g.iOff[slot], g.layerCount[slot]});
        }
        // columns stored back to back in a page draw as one: far fewer draw calls in the shadow pass
        for (auto& runs : shadowRuns) {
            std::sort(runs.begin(), runs.end(), [](const ChunkMeshStore::Run& a, const ChunkMeshStore::Run& b) {
                return a.page != b.page ? a.page < b.page : a.first < b.first;
            });
            size_t out = 0;
            for (size_t i = 0; i < runs.size(); i++) {
                if (out > 0 && runs[out - 1].page == runs[i].page &&
                    runs[out - 1].first + runs[out - 1].count == runs[i].first)
                    runs[out - 1].count += runs[i].count;
                else
                    runs[out++] = runs[i];
            }
            runs.resize(out);
        }
    }

    // -- passes --------------------------------------------------------------------------------------------------------

    rhi::TextureHandle JavaPack::FeedbackCopy(rhi::CommandList& list, rhi::TextureHandle src) {
        const rhi::TextureDesc* sd = gpu.Dev().GetDesc(src);
        if (!sd)
            return {};
        Feedback& f = feedback[src.value];
        if (f.tex) {
            const rhi::TextureDesc* fd = gpu.Dev().GetDesc(f.tex);
            if (!fd || fd->width != sd->width || fd->height != sd->height || fd->format != sd->format) {
                gpu.Dev().WaitIdle();
                owned.erase(std::remove(owned.begin(), owned.end(), f.tex), owned.end());
                gpu.DestroyTexture(f.tex);
                f.tex = {};
            }
        }
        if (!f.tex) {
            const bool isDepth = sd->format == rhi::Format::D32Float;
            f.tex = MakeTexture(sd->width, sd->height, sd->format,
                                (isDepth ? rhi::TextureUsage::DepthTarget : rhi::TextureUsage::ColorTarget) |
                                    rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst,
                                "pack feedback copy");
            f.serial = ~0ull;
            if (!f.tex)
                return {};
        }
        // once per pass: the attachment does not change between the draws recorded before it begins
        if (f.serial != passSerial) {
            gpu.Transition(list, src, rhi::State::CopySrc);
            gpu.Transition(list, f.tex, rhi::State::CopyDst);
            rhi::TextureCopy c;
            c.width = sd->width;
            c.height = sd->height;
            list.CopyTexture(f.tex, src, std::span<const rhi::TextureCopy>(&c, 1));
            gpu.Transition(list, f.tex, rhi::State::ShaderRead);
            f.serial = passSerial;
            statCopies++;
            if (trace)
                std::fprintf(stderr, "[jshader] feedback copy %s %ux%u fmt %d\n", sd->name ? sd->name : "?", sd->width,
                             sd->height, static_cast<int>(sd->format));
        }
        return f.tex;
    }

    void JavaPack::BeginPass(rhi::CommandList& list, const std::vector<rhi::TextureHandle>& colors,
                             rhi::TextureHandle depthTex, uint32_t w, uint32_t h, bool clearDepth,
                             const std::vector<std::array<float, 4>>* clears, const char* passName,
                             bool overwrite) {
        passSerial++;
        statPasses++;
        rhi::RenderingDesc rd;
        rd.colorCount = static_cast<uint32_t>(colors.size());
        for (size_t i = 0; i < colors.size(); i++) {
            gpu.Transition(list, colors[i], rhi::State::ColorTarget);
            rd.colors[i].texture = colors[i];
            const bool clear = clears && i < clears->size() && (*clears)[i][0] > -1e20f;
            rd.colors[i].load = clear ? rhi::LoadOp::Clear : overwrite ? rhi::LoadOp::DontCare : rhi::LoadOp::Load;
            rd.colors[i].store = rhi::StoreOp::Store;
            if (clear)
                rd.colors[i].clear = (*clears)[i];
        }
        if (depthTex) {
            gpu.Transition(list, depthTex, rhi::State::DepthWrite);
            rd.depth.texture = depthTex;
            rd.depth.load = clearDepth ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
            rd.depth.store = rhi::StoreOp::Store;
            rd.depth.clearDepth = 1.0f;
        }
        rd.name = passName;
        list.BeginRendering(rd);
        list.SetViewport({0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f});
        list.SetScissor({0, 0, w, h});
    }

    void JavaPack::ClearBuffers(rhi::CommandList& list, const FrameParams& params) {
        // iris ClearPassCreator: a buffer that clears has both its textures (main and alt) cleared at the start of the
        // frame, colortex0 to the fog colour, colortex1 to white, the rest to transparent black, unless the pack
        // names a colour. a pass that writes part of a buffer (scaled passes, bloom tiles) then sees the clear
        // colour around what it wrote, not last frame's picture
        constexpr float kKeep = -1e30f;
        struct Target {
            rhi::TextureHandle tex;
            std::array<float, 4> color;
            uint32_t w, h;
        };
        std::vector<Target> targets;
        for (int i = 0; i < jshader::kMaxColorTex; i++) {
            Buffer& b = bufs[static_cast<size_t>(i)];
            if (!b.used)
                continue;
            const jshader::BufferSetup& cfg = config.color[static_cast<size_t>(i)];
            std::array<float, 4> c = {cfg.clearColor[0], cfg.clearColor[1], cfg.clearColor[2],
                                      cfg.clearColor[3]};
            if (i == 0 && instance->GetDirectives().Text("colortex0ClearColor").empty() &&
                instance->GetDirectives().Text("gcolorClearColor").empty())
                c = {params.fogColor[0], params.fogColor[1], params.fogColor[2], 1.0f};
            if (i == 1 && instance->GetDirectives().Text("colortex1ClearColor").empty() &&
                instance->GetDirectives().Text("gdepthClearColor").empty())
                c = {1.0f, 1.0f, 1.0f, 1.0f};
            for (int k = 0; k < 2; k++) {
                const rhi::TextureHandle t = b.tex[k];
                if (!t)
                    continue;
                // a texture never written yet is cleared whatever the pack says, so it holds no garbage
                const bool clear = cfg.clear || gpu.StateOf(t) == rhi::State::Undefined;
                if (!clear && k != b.cur)
                    continue;
                targets.push_back({t, clear ? c : std::array<float, 4>{kKeep, 0, 0, 0}, b.w, b.h});
            }
        }
        // one pass per size, up to eight targets each; the first also clears the scene depth
        bool depthCleared = false;
        while (!targets.empty()) {
            const uint32_t w = targets.front().w, h = targets.front().h;
            std::vector<rhi::TextureHandle> tex;
            std::vector<std::array<float, 4>> clears;
            for (auto it = targets.begin(); it != targets.end() && tex.size() < 8;) {
                if (it->w == w && it->h == h) {
                    tex.push_back(it->tex);
                    clears.push_back(it->color);
                    it = targets.erase(it);
                } else {
                    ++it;
                }
            }
            const bool withDepth = !depthCleared && w == width && h == height;
            depthCleared = depthCleared || withDepth;
            BeginPass(list, tex, withDepth ? depth[0] : rhi::TextureHandle{}, w, h, withDepth, &clears,
                      "pack clear");
            list.EndRendering();
        }
        if (!depthCleared) {
            BeginPass(list, {}, depth[0], width, height, true, nullptr, "pack depth clear");
            list.EndRendering();
        }
    }

    bool JavaPack::DrawLayers(rhi::CommandList& list, const char* programName,
                              const std::vector<RenderLayer>& layers, bool translucent, bool shadow,
                              int stage, float alphaRef) {
        Program* p = GetProgram(programName, false, nullptr, nullptr, rhi::Format::Unknown,
                                AlphaFor(programName, alphaRef));
        if (!p)
            return false;
        bool any = false;
        if (trace)
            std::fprintf(stderr, "[jshader] draw %s shadow=%d runs: solid %zu cutout %zu translucent %zu\n",
                         programName, shadow ? 1 : 0, layerRuns[0].size(), layerRuns[1].size(),
                         layerRuns[2].size());
        for (RenderLayer layer : layers) {
            const auto& list2 =
                shadow ? shadowRuns[static_cast<size_t>(layer)] : layerRuns[static_cast<size_t>(layer)];
            any = any || !list2.empty();
        }
        if (!any)
            return true;

        std::vector<rhi::TextureHandle> colors;
        rhi::TextureHandle depthTarget;
        uint32_t w = width, h = height;
        PipeState st;
        if (shadow) {
            for (auto i : shadowColor)
                if (i)
                    colors.push_back(i);
            depthTarget = shadowDepth;
            w = h = static_cast<uint32_t>(config.shadowMapResolution);
        } else {
            for (int idx : p->attachments) {
                if (idx < 0 || idx >= jshader::kMaxColorTex)
                    idx = 0;
                Buffer& b = bufs[static_cast<size_t>(idx)];
                if (!b.used)
                    b.used = true;
                colors.push_back(b.tex[b.cur] ? b.tex[b.cur] : bufs[0].tex[bufs[0].cur]);
            }
            depthTarget = depth[0];
        }
        if (colors.empty() && !shadow)
            return true;
        std::vector<rhi::TextureHandle> attached = colors;
        attached.push_back(depthTarget);
        const rhi::BindGroupHandle textures = Textures(list, *p, attached);
        if (!textures)
            return false;
        st.colorCount = static_cast<uint32_t>(colors.size());
        for (size_t i = 0; i < colors.size(); i++) {
            const rhi::TextureDesc* td = gpu.Dev().GetDesc(colors[i]);
            st.colors[i] = td ? td->format : rhi::Format::RGBA8Unorm;
        }
        st.depth = rhi::Format::D32Float;
        st.depthTest = true;
        st.depthWrite = true;
        st.depthOp = rhi::CompareOp::LessEqual;
        const jshader::ProgramConfig* pc = BlendConfigFor(programName);

        jshader::PackDrawState draw = TerrainDraw();
        if (shadow) {
            std::memcpy(draw.modelView, shadowDrawView, sizeof(draw.modelView));
            std::memcpy(draw.projection, shadowProjection, sizeof(draw.projection));
            for (int c = 0; c < 3; c++)
                for (int r = 0; r < 3; r++)
                    draw.normalMatrix[c * 3 + r] = shadowDrawView[c * 4 + r];
        }
        draw.renderStage = stage;

        const bool prepassed = !shadow && !translucent && layers.size() == 1 && layers[0] == RenderLayer::Solid &&
                               DepthPrepass(list, *p, draw, textures, depthTarget, w, h);
        BeginPass(list, colors, depthTarget, w, h, false, nullptr, shadow ? "pack shadow" : "pack scene");
        if (!BindUniforms(list, *p, draw)) {
            list.EndRendering();
            return false;
        }
        list.SetBindGroup(1, textures);
        BindStorage(list, *p);
        for (RenderLayer layer : layers) {
            const auto& runList =
                shadow ? shadowRuns[static_cast<size_t>(layer)] : layerRuns[static_cast<size_t>(layer)];
            if (runList.empty())
                continue;
            PipeState ls = st;
            ls.cull = !shadow && (layer == RenderLayer::Solid || layer == RenderLayer::CutoutCull ||
                                  layer == RenderLayer::TranslucentCull);
            const bool blendLayer = !shadow && translucent;
            ls.blend = blendLayer;
            ls.depthWrite = !prepassed;
            if (blendLayer) {
                ls.src = rhi::BlendFactor::SrcAlpha;
                ls.dst = rhi::BlendFactor::InvSrcAlpha;
                ls.srcA = rhi::BlendFactor::One;
                ls.dstA = rhi::BlendFactor::InvSrcAlpha;
            }
            if (!shadow && pc && pc->blend.set) {
                ls.blend = !pc->blend.off;
                if (ls.blend) {
                    ls.src = GlFactor(pc->blend.src);
                    ls.dst = GlFactor(pc->blend.dst);
                    ls.srcA = GlFactor(pc->blend.srcA);
                    ls.dstA = GlFactor(pc->blend.dstA);
                }
            }
            const rhi::PipelineHandle pipe = PipelineFor(*p, ls);
            if (!pipe) {
                Log("jshader", "{}: pipeline creation failed", programName);
                continue;
            }
            list.SetPipeline(pipe);
            DrawRunList(list, runList, layer);
        }
        list.EndRendering();
        if (p->imageMask || !config.bufferObjects.empty())
            StorageBarrier(list, *p); // shadow voxelization writes images from the vertex shader
        return true;
    }

    void JavaPack::DrawRunList(rhi::CommandList& list, const std::vector<ChunkMeshStore::Run>& runList, RenderLayer layer) {
        static const bool noMulti = std::getenv("PERLIN_NO_MDI") != nullptr;
        const bool multi = !noMulti && gpu.Caps().features.multiDrawIndirect;
        rhi::BufferHandle boundVb, boundIb;
        for (size_t i = 0; i < runList.size();) {
            size_t j = i + 1;
            while (j < runList.size() && runList[j].page == runList[i].page)
                j++;
            const rhi::BufferHandle vb = meshes.Extension(runList[i].page);
            const rhi::BufferHandle ib = meshes.Indices(runList[i].page, layer);
            if (!vb || !ib) {
                i = j;
                continue;
            }
            if (vb != boundVb) {
                list.SetVertexBuffer(0, vb);
                boundVb = vb;
            }
            if (ib != boundIb) {
                list.SetIndexBuffer(ib, rhi::IndexType::U32);
                boundIb = ib;
            }
            const size_t n = j - i;
            RingSlice slice;
            if (multi && n > 1)
                slice = gpu.Ring(n * sizeof(rhi::DrawIndexedIndirectCommand), 16);
            if (slice.data) {
                auto* cmds = reinterpret_cast<rhi::DrawIndexedIndirectCommand*>(slice.data);
                for (size_t k = 0; k < n; k++)
                    cmds[k] = {runList[i + k].count, 1, runList[i + k].first, 0, 0};
                list.DrawIndexedIndirect(slice.buffer, slice.offset, static_cast<uint32_t>(n),
                                         sizeof(rhi::DrawIndexedIndirectCommand));
            } else {
                for (size_t k = i; k < j; k++)
                    list.DrawIndexed(runList[k].count, 1, runList[k].first, 0, 0);
            }
            i = j;
        }
    }

    bool JavaPack::EnsureShadowCache() {
        if (shadowCacheDepth)
            return true;
        const rhi::TextureDesc* dd = gpu.Dev().GetDesc(shadowDepth);
        if (!dd)
            return false;
        constexpr rhi::TextureUsage kCopy = rhi::TextureUsage::CopySrc | rhi::TextureUsage::CopyDst |
                                            rhi::TextureUsage::Sampled;
        shadowCacheDepth = MakeTexture(dd->width, dd->height, dd->format, kCopy | rhi::TextureUsage::DepthTarget,
                                       "pack shadow cache");
        for (int i = 0; i < 2; i++)
            if (shadowColor[i])
                if (const rhi::TextureDesc* cd = gpu.Dev().GetDesc(shadowColor[i]))
                    shadowCacheColor[i] = MakeTexture(cd->width, cd->height, cd->format,
                                                      kCopy | rhi::TextureUsage::ColorTarget, "pack shadow cache");
        if (!shadowCacheDepth || (shadowColor[0] && !shadowCacheColor[0]) ||
            (shadowColor[1] && !shadowCacheColor[1])) {
            shadowCacheOn = false;
            return false;
        }
        return true;
    }

    bool JavaPack::DepthPrepass(rhi::CommandList& list, Program& p, const jshader::PackDrawState& draw,
                                rhi::BindGroupHandle textures, rhi::TextureHandle depthTarget, uint32_t w,
                                uint32_t h) {
        if (prepassState < 0) {
            prepassState = 0;
            if (std::getenv("PERLIN_PACK_PREPASS") && !p.imageMask && config.bufferObjects.empty() && p.source &&
                p.source->fragment.text.find("gl_FragDepth") == std::string::npos &&
                p.source->vertex.text.find("imageStore") == std::string::npos)
                prepassState = 1;
        }
        if (prepassState != 1)
            return false;
        static const jshader::ExpandedSource vs = MakeSource(kFullscreenVertex), fs = MakeSource(kEmptyFragment);
        Program* empty = GetProgram("builtin_depth_only", false, &vs, &fs);
        if (!empty || !empty->fs)
            return false;
        const auto& runList = layerRuns[static_cast<size_t>(RenderLayer::Solid)];
        if (runList.empty())
            return false;
        PipeState ps;
        ps.colorCount = 0;
        ps.depth = rhi::Format::D32Float;
        ps.depthTest = true;
        ps.depthWrite = true;
        ps.depthOp = rhi::CompareOp::LessEqual;
        ps.cull = true;
        ps.mask = rhi::ColorMask::None;
        fragmentOverride = empty->fs;
        const rhi::PipelineHandle pipe = PipelineFor(p, ps);
        fragmentOverride = {};
        if (!pipe)
            return false;
        BeginPass(list, {}, depthTarget, w, h, false, nullptr, "pack depth prepass");
        if (!BindUniforms(list, p, draw)) {
            list.EndRendering();
            return false;
        }
        list.SetBindGroup(1, textures);
        BindStorage(list, p);
        list.SetPipeline(pipe);
        DrawRunList(list, runList, RenderLayer::Solid);
        list.EndRendering();
        return true;
    }

    void JavaPack::DrawShadows(rhi::CommandList& list) {
        if (!config.shadowEnabled || !shadowDepth)
            return;
        // iris ShadowRenderer: clear (depth to 1, colour buffers to their clear colour unless shadowcolorNClear is
        // off), solid and cutout terrain, entities, block entities, copy shadowtex0 to shadowtex1, translucent
        // terrain
        const bool partial = shadowPartial && shadowCacheValid && shadowCacheDepth;
        if (partial) {
            GpuMark(list, "shadow cached");
            CopyDepth(list, shadowCacheDepth, shadowDepth);
            for (int i = 0; i < 2; i++)
                if (shadowColor[i] && shadowCacheColor[i])
                    CopyDepth(list, shadowCacheColor[i], shadowColor[i]);
            shadowCacheAge++;
        } else {
            std::vector<rhi::TextureHandle> colors;
            std::vector<std::array<float, 4>> clears;
            for (int i = 0; i < 2; i++)
                if (shadowColor[i] && (config.shadowColorClear[i] || !shadowCleared)) {
                    colors.push_back(shadowColor[i]);
                    clears.push_back({config.shadowColorClearColor[i][0], config.shadowColorClearColor[i][1],
                                      config.shadowColorClearColor[i][2],
                                      config.shadowColorClearColor[i][3]});
                }
            const uint32_t res = static_cast<uint32_t>(config.shadowMapResolution);
            BeginPass(list, colors, shadowDepth, res, res, true, &clears, "pack shadow clear");
            list.EndRendering();
            shadowCleared = true;
        }
        using K = ExternalDraw::Kind;
        if (config.shadowTerrain && !partial) {
            GpuMark(list, "shadow solid");
            DrawLayers(list, "shadow_solid", {RenderLayer::Solid}, false, true, 8, -1.0f);
        }
        if (!partial && shadowCacheOn && shadowSkipState == 1 && EnsureShadowCache()) {
            CopyDepth(list, shadowDepth, shadowCacheDepth);
            for (int i = 0; i < 2; i++)
                if (shadowColor[i] && shadowCacheColor[i])
                    CopyDepth(list, shadowColor[i], shadowCacheColor[i]);
            shadowCacheValid = true;
            shadowCacheGeneration = meshes.Generation();
            shadowCacheAge = 0;
        }
        if (config.shadowTerrain) {
            GpuMark(list, "shadow cutout");
            DrawLayers(list, "shadow_cutout", {RenderLayer::Cutout, RenderLayer::CutoutCull}, false, true, 10,
                       0.5f);
        }
        GpuMark(list, "shadow entities");
        DrawExternal(list, {K::Entity, K::EntityTranslucent, K::Lightning}, true);
        DrawExternal(list, {K::Block}, true);
        GpuMark(list, "shadow copy");
        if (shadowDepthNoTranslucents)
            CopyDepth(list, shadowDepth, shadowDepthNoTranslucents);
        GpuMark(list, "shadow water");
        if (config.shadowTerrain && config.shadowTranslucent)
            DrawLayers(list, "shadow_water", {RenderLayer::Translucent, RenderLayer::TranslucentCull}, false,
                       true, 17, -1.0f);
    }

    // -- engine draws through the pack -----------------------------------------------------------------------------

    void JavaPack::BuildVertices(const UiVertex* v, size_t vertexCount, const uint32_t* idx,
                                 size_t indexCount, const float model[16], int blockLight, int skyLight,
                                 std::vector<uint8_t>& out, const int8_t* vertexLight,
                                 const float* faceToward, bool noSkyLight) {
        const glm::mat4 m = glm::make_mat4(model);
        const glm::mat3 nm = glm::mat3(m);
        // the mesh's middle: box models are near enough convex that a face normal pointing away from it is outward
        glm::vec3 centre(0.0f);
        for (size_t i = 0; i < vertexCount; i++)
            centre += glm::vec3(m * glm::vec4(v[i].x, v[i].y, v[i].z, 1.0f));
        if (vertexCount)
            centre /= static_cast<float>(vertexCount);
        const size_t tris = indexCount / 3;
        out.resize(tris * 3 * kPackVertexBytes);
        const float light[2] = {static_cast<float>(std::clamp(blockLight, 0, 15) * 16),
                                static_cast<float>(std::clamp(skyLight, 0, 15) * 16)};
        const float entity[2] = {-1.0f, 0.0f};
        size_t written = 0;
        for (size_t t = 0; t < tris; t++) {
            const uint32_t i0 = idx[t * 3], i1 = idx[t * 3 + 1], i2 = idx[t * 3 + 2];
            if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
                continue;
            const UiVertex* tv[3] = {&v[i0], &v[i1], &v[i2]};
            glm::vec3 p[3];
            for (int k = 0; k < 3; k++)
                p[k] = glm::vec3(m * glm::vec4(tv[k]->x, tv[k]->y, tv[k]->z, 1.0f));
            glm::vec3 n = glm::cross(p[1] - p[0], p[2] - p[0]);
            const float len = glm::length(n);
            if (len < 1e-10f)
                n = glm::normalize(nm * glm::vec3(0, 1, 0));
            else
                n /= len;
            if (faceToward) {
                if (glm::dot(n, glm::vec3(faceToward[0], faceToward[1], faceToward[2]) - p[0]) < 0.0f)
                    n = -n;
            } else if (glm::dot(n, (p[0] + p[1] + p[2]) / 3.0f - centre) < 0.0f) {
                n = -n;
            }
            const glm::vec2 uv0(tv[0]->u, tv[0]->v), uv1(tv[1]->u, tv[1]->v), uv2(tv[2]->u, tv[2]->v);
            const glm::vec3 e1 = p[1] - p[0], e2 = p[2] - p[0];
            const glm::vec2 d1 = uv1 - uv0, d2 = uv2 - uv0;
            const float det = d1.x * d2.y - d2.x * d1.y;
            glm::vec3 tangent = std::fabs(det) > 1e-10f ? (e1 * d2.y - e2 * d1.y) / det : e1;
            if (glm::dot(tangent, tangent) < 1e-12f)
                tangent = glm::vec3(1, 0, 0);
            tangent = glm::normalize(tangent);
            const float mid[2] = {(uv0.x + uv1.x + uv2.x) / 3.0f, (uv0.y + uv1.y + uv2.y) / 3.0f};
            const uint16_t normal[4] = {ToHalf(n.x), ToHalf(n.y), ToHalf(n.z), 0};
            const uint16_t tang[4] = {ToHalf(tangent.x), ToHalf(tangent.y), ToHalf(tangent.z), ToHalf(1.0f)};
            const uint16_t midBlock[4] = {0, 0, 0, 0};
            const uint32_t tri[3] = {i0, i1, i2};
            for (int k = 0; k < 3; k++) {
                uint8_t* o = out.data() + written * kPackVertexBytes;
                const float pos[3] = {p[k].x, p[k].y, p[k].z};
                const float uv[2] = {tv[k]->u, tv[k]->v};
                std::memcpy(o + kPos, pos, 12);
                std::memcpy(o + kColor, &tv[k]->abgr, 4);
                std::memcpy(o + kUv, uv, 8);
                float vl[2] = {light[0], light[1]};
                if (vertexLight) {
                    const int8_t b = vertexLight[tri[k] * 2], s = vertexLight[tri[k] * 2 + 1];
                    if (b >= 0)
                        vl[0] = static_cast<float>(std::min<int>(b, 15) * 16);
                    if (s >= 0)
                        vl[1] = static_cast<float>(std::min<int>(s, 15) * 16);
                }
                if (noSkyLight)
                    vl[1] = 0.0f;
                std::memcpy(o + kLight, vl, 8);
                std::memcpy(o + kNormal, normal, 8);
                std::memcpy(o + kEntity, entity, 8);
                std::memcpy(o + kMid, mid, 8);
                std::memcpy(o + kMidBlock, midBlock, 8);
                std::memcpy(o + kTangent, tang, 8);
                written++;
            }
        }
        out.resize(written * kPackVertexBytes);
    }

    void JavaPack::DrawExternal(rhi::CommandList& list, std::initializer_list<ExternalDraw::Kind> kinds,
                                bool shadowPass) {
        using K = ExternalDraw::Kind;
        for (const K kind : kinds) {
            std::vector<const ExternalDraw*> draws;
            for (const ExternalDraw& d : queued) {
                if (d.kind != kind || d.count == 0)
                    continue;
                if (shadowPass) {
                    if (!d.castsShadow)
                        continue;
                    if (kind == K::Block ? !config.shadowBlockEntities
                                         : !(frame.shadowEntities &&
                                             (config.shadowEntities || (d.player && config.shadowPlayer))))
                        continue;
                } else if (d.shadowOnly) {
                    continue;
                }
                draws.push_back(&d);
            }
            if (draws.empty())
                continue;
            const bool hand = kind == K::Hand || kind == K::HandTranslucent || kind == K::HandGlint;
            const char* programName = nullptr;
            if (shadowPass)
                programName = kind == K::Block       ? "shadow_block"
                              : kind == K::Lightning ? "shadow_lightning"
                                                     : "shadow_entities";
            else
                switch (kind) {
                case K::Entity:
                    programName = "gbuffers_entities";
                    break;
                case K::EntityTranslucent:
                    programName = "gbuffers_entities_translucent";
                    break;
                case K::Eyes:
                    programName = "gbuffers_spidereyes";
                    break;
                case K::Hand:
                    programName = "gbuffers_hand";
                    break;
                case K::HandTranslucent:
                    programName = "gbuffers_hand_water";
                    break;
                case K::Particle:
                    programName = "gbuffers_particles";
                    break;
                case K::ParticleTranslucent:
                    programName = "gbuffers_particles_translucent";
                    break;
                case K::Weather:
                    programName = "gbuffers_weather";
                    break;
                case K::Cracks:
                    programName = "gbuffers_damagedblock";
                    break;
                case K::Block:
                    programName = "gbuffers_block";
                    break;
                case K::BlockTranslucent:
                    programName = "gbuffers_block_translucent";
                    break;
                case K::Glint:
                case K::HandGlint:
                    programName = "gbuffers_armor_glint";
                    break;
                case K::Clouds:
                    programName = "gbuffers_clouds";
                    break;
                case K::Lightning:
                    programName = "gbuffers_lightning";
                    break;
                case K::Line:
                    programName = "gbuffers_line";
                    break;
                case K::Glowing:
                    programName = "gbuffers_entities_glowing";
                    break;
                }
            // iris WorldRenderingPhase ordinals (MC_RENDER_STAGE_*)
            int stage = 11;
            switch (kind) {
            case K::Hand:
            case K::HandGlint:
                stage = 16;
                break;
            case K::HandTranslucent:
                stage = 23;
                break;
            case K::Particle:
            case K::ParticleTranslucent:
                stage = 19;
                break;
            case K::Weather:
                stage = 21;
                break;
            case K::Cracks:
                stage = 13;
                break;
            case K::Block:
            case K::BlockTranslucent:
                stage = 12;
                break;
            case K::Clouds:
                stage = 20;
                break;
            case K::Line:
                stage = 14;
                break;
            default:
                stage = 11;
                break;
            }
            // the targets come from the program's source, the same for every alpha variant
            Program* base = nullptr;
            for (const ExternalDraw* d : draws) {
                base = GetProgram(programName, false, nullptr, nullptr, rhi::Format::Unknown,
                                  AlphaFor(programName, shadowPass ? 0.1f : d->alphaRef));
                if (base)
                    break;
            }
            if (!base) {
                if (shadowPass && loggedMissing.insert(std::string("shadow-program:") + programName).second)
                    Log("jshader", "{}: no program for the shadow pass, {} shadow draws skipped", programName,
                        draws.size());
                continue;
            }
            std::vector<rhi::TextureHandle> colors;
            rhi::TextureHandle depthTarget;
            uint32_t w = width, h = height;
            if (shadowPass) {
                for (auto t : shadowColor)
                    if (t)
                        colors.push_back(t);
                depthTarget = shadowDepth;
                w = h = static_cast<uint32_t>(config.shadowMapResolution);
            } else {
                for (int idx : base->attachments) {
                    if (idx < 0 || idx >= jshader::kMaxColorTex)
                        idx = 0;
                    Buffer& b = bufs[static_cast<size_t>(idx)];
                    b.used = true;
                    colors.push_back(b.tex[b.cur] ? b.tex[b.cur] : bufs[0].tex[bufs[0].cur]);
                }
                depthTarget = depth[0];
            }
            if (!depthTarget || (colors.empty() && !shadowPass)) {
                if (shadowPass && loggedMissing.insert("shadow-target").second)
                    Log("jshader", "entity shadows: the shadow map has no depth target");
                continue;
            }
            std::vector<rhi::TextureHandle> attached = colors;
            attached.push_back(depthTarget);
            const jshader::ProgramConfig* pc = BlendConfigFor(programName);

            struct Ready {
                Program* program = nullptr;
                rhi::BindGroupHandle textures;
                rhi::PipelineHandle pipe;
                gpu::RingSlice slice;
                uint32_t count = 0;
                jshader::PackDrawState state;
            };
            std::vector<Ready> ready;
            ready.reserve(draws.size());
            std::map<std::pair<const Program*, uint64_t>, rhi::BindGroupHandle> textureGroups;
            std::vector<std::pair<float, Program*>> programByAlpha;
            const jshader::PackDrawState baseState = TerrainDraw();
            for (const ExternalDraw* d : draws) {
                Ready r;
                const float alphaRef = shadowPass ? 0.1f : d->alphaRef;
                const auto known = std::find_if(programByAlpha.begin(), programByAlpha.end(),
                                                [&](const auto& e) { return e.first == alphaRef; });
                if (known != programByAlpha.end()) {
                    r.program = known->second;
                } else {
                    r.program = GetProgram(programName, false, nullptr, nullptr, rhi::Format::Unknown,
                                           AlphaFor(programName, alphaRef));
                    programByAlpha.emplace_back(alphaRef, r.program);
                }
                if (!r.program)
                    continue;
                Program& p = *r.program;
                const rhi::TextureHandle skin = d->texture ? d->texture : white;
                const auto texKey = std::make_pair(static_cast<const Program*>(r.program),
                                                   static_cast<uint64_t>(skin.value));
                auto cachedGroup = textureGroups.find(texKey);
                if (cachedGroup == textureGroups.end()) {
                    textureOverride = skin;
                    const rhi::BindGroupHandle group = Textures(list, p, attached);
                    textureOverride = {};
                    cachedGroup = textureGroups.emplace(texKey, group).first;
                }
                r.textures = cachedGroup->second;
                PipeState st;
                st.colorCount = static_cast<uint32_t>(colors.size());
                for (size_t i = 0; i < colors.size(); i++) {
                    const rhi::TextureDesc* td = gpu.Dev().GetDesc(colors[i]);
                    st.colors[i] = td ? td->format : rhi::Format::RGBA8Unorm;
                }
                st.depth = rhi::Format::D32Float;
                st.depthTest = shadowPass || d->depthTest;
                st.depthWrite = shadowPass || d->depthWrite;
                st.depthOp = rhi::CompareOp::LessEqual;
                st.blend = !shadowPass && (d->blend || d->additive || d->glintBlend);
                st.src = rhi::BlendFactor::SrcAlpha;
                st.dst = d->additive ? rhi::BlendFactor::One : rhi::BlendFactor::InvSrcAlpha;
                st.srcA = rhi::BlendFactor::One;
                st.dstA = rhi::BlendFactor::InvSrcAlpha;
                if (!shadowPass && kind == K::Eyes) {
                    // iris ProgramId.SpiderEyes: src alpha, one / zero, one
                    st.blend = true;
                    st.src = rhi::BlendFactor::SrcAlpha;
                    st.dst = rhi::BlendFactor::One;
                    st.srcA = rhi::BlendFactor::Zero;
                    st.dstA = rhi::BlendFactor::One;
                }
                if (!shadowPass && kind == K::Lightning) {
                    st.blend = true;
                    st.src = rhi::BlendFactor::SrcAlpha;
                    st.dst = rhi::BlendFactor::One;
                }
                if (!shadowPass && d->glintBlend) {
                    // vanilla GLINT transparency and depth: src colour, one / zero, one; depth equal, no write
                    st.src = rhi::BlendFactor::SrcColor;
                    st.dst = rhi::BlendFactor::One;
                    st.srcA = rhi::BlendFactor::Zero;
                    st.dstA = rhi::BlendFactor::One;
                    st.depthOp = rhi::CompareOp::Equal;
                    st.depthWrite = false;
                }
                if (!shadowPass && pc && pc->blend.set) {
                    st.blend = !pc->blend.off;
                    if (st.blend) {
                        st.src = GlFactor(pc->blend.src);
                        st.dst = GlFactor(pc->blend.dst);
                        st.srcA = GlFactor(pc->blend.srcA);
                        st.dstA = GlFactor(pc->blend.dstA);
                    }
                }
                r.pipe = PipelineFor(p, st);
                if (!r.textures || !r.pipe) {
                    if (loggedMissing.insert(std::string("draw-state:") + programName + (shadowPass ? ":s" : ":m"))
                            .second)
                        Log("jshader", "{}: {} unavailable, draws skipped", programName,
                            !r.textures ? "texture bind group" : "pipeline");
                    continue;
                }
                if (d->ringStamp != frameStamp || !d->ring.data) {
                    d->ring = gpu.Ring(d->vertices.size(), 16);
                    if (d->ring.data) {
                        std::memcpy(d->ring.data, d->vertices.data(), d->vertices.size());
                        d->ringStamp = frameStamp;
                    }
                }
                r.slice = d->ring;
                if (!r.slice.data)
                    continue;
                r.count = d->count;
                r.state = baseState;
                if (shadowPass) {
                    std::memcpy(r.state.modelView, shadowDrawView, sizeof(r.state.modelView));
                    std::memcpy(r.state.projection, shadowProjection, sizeof(r.state.projection));
                    for (int c = 0; c < 3; c++)
                        for (int rr = 0; rr < 3; rr++)
                            r.state.normalMatrix[c * 3 + rr] = shadowDrawView[c * 4 + rr];
                } else if (hand) {
                    // the hand's vertices are already in view space; iris squeezes its depth by MC_HAND_DEPTH
                    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                    std::memcpy(r.state.modelView, identity, sizeof(identity));
                    std::memcpy(r.state.projection, handProjection, sizeof(handProjection));
                    const float n3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
                    std::memcpy(r.state.normalMatrix, n3, sizeof(n3));
                }
                r.state.renderStage = stage;
                r.state.overrideEntity = true;
                r.state.entityId = d->entityId;
                r.state.blockEntityId = d->blockEntityId;
                r.state.currentRenderedItemId = d->itemId;
                std::memcpy(r.state.textureMatrix0, d->textureMatrix, sizeof(d->textureMatrix));
                std::memcpy(r.state.entityColor, d->entityColor, sizeof(d->entityColor));
                ready.push_back(r);
            }
            if (ready.empty())
                continue;
            if (shadowPass && loggedMissing.insert(std::string("shadow-drawn:") + programName).second)
                Log("jshader", "{}: {} draws into the {}x{} shadow map", programName, ready.size(), w, h);
            BeginPass(list, colors, depthTarget, w, h, false, nullptr,
                      shadowPass ? "pack entity shadows" : programName);
            bool anyStorage = false;
            for (Ready& r : ready) {
                if (!BindUniforms(list, *r.program, r.state)) {
                    if (shadowPass && loggedMissing.insert(std::string("shadow-uniforms:") + programName).second)
                        Log("jshader", "{}: uniform binding failed in the shadow pass", programName);
                    continue;
                }
                list.SetBindGroup(1, r.textures);
                BindStorage(list, *r.program);
                list.SetPipeline(r.pipe);
                list.SetVertexBuffer(0, r.slice.buffer, r.slice.offset);
                list.Draw(r.count, 1, 0, 0);
                anyStorage = anyStorage || r.program->imageMask;
            }
            list.EndRendering();
            if (anyStorage || !config.bufferObjects.empty())
                StorageBarrier(list, *base);
        }
    }

    const jshader::ProgramConfig* JavaPack::BlendConfigFor(const std::string& progName) const {
        // iris ProgramDirectives: blend.<program> of the program file that loaded
        if (instance)
            if (auto src = instance->Program(progName); src && src->found && !src->sourceName.empty())
                if (const jshader::ProgramConfig* pc = config.Find(src->sourceName))
                    return pc;
        return config.Find(progName);
    }

    // iris (sodium) terrain passes: gbuffers_terrain_solid with no alpha test, gbuffers_terrain_cutout at 0.5,
    // gbuffers_water above zero
    void JavaPack::DrawTerrain(rhi::CommandList& list) {
        DrawLayers(list, "gbuffers_terrain_solid", {RenderLayer::Solid}, false, false, 8, -1.0f);
        DrawLayers(list, "gbuffers_terrain_cutout", {RenderLayer::Cutout, RenderLayer::CutoutCull}, false,
                   false, 10, 0.5f);
    }

    void JavaPack::DrawTranslucent(rhi::CommandList& list) {
        DrawLayers(list, "gbuffers_water", {RenderLayer::Translucent, RenderLayer::TranslucentCull}, true,
                   false, 17, 0.0001f);
    }

    namespace {
        struct SkyBuilder {
            std::vector<uint8_t> bytes;
            uint32_t count = 0;
            void Vertex(float x, float y, float z, const uint8_t rgba[4], float u, float v) {
                uint8_t px[kPackVertexBytes] = {};
                const float pos[3] = {x, y, z};
                std::memcpy(px + kPos, pos, 12);
                std::memcpy(px + kColor, rgba, 4);
                const float uv[2] = {u, v}, light[2] = {240.0f, 240.0f};
                std::memcpy(px + kUv, uv, 8);
                std::memcpy(px + kLight, light, 8);
                const uint16_t n[4] = {0, 0x3C00, 0, 0};
                std::memcpy(px + kNormal, n, 8);
                bytes.insert(bytes.end(), px, px + kPackVertexBytes);
                count++;
            }
            // quad around a centre in the plane spanned by right and up
            void Quad(const glm::vec3& c, const glm::vec3& r, const glm::vec3& u, const uint8_t rgba[4],
                      float u0, float v0, float u1, float v1) {
                const glm::vec3 p0 = c - r - u, p1 = c + r - u, p2 = c + r + u, p3 = c - r + u;
                Vertex(p0.x, p0.y, p0.z, rgba, u0, v1);
                Vertex(p1.x, p1.y, p1.z, rgba, u1, v1);
                Vertex(p2.x, p2.y, p2.z, rgba, u1, v0);
                Vertex(p0.x, p0.y, p0.z, rgba, u0, v1);
                Vertex(p2.x, p2.y, p2.z, rgba, u1, v0);
                Vertex(p3.x, p3.y, p3.z, rgba, u0, v0);
            }
        };
    }

    void JavaPack::DrawSkyObjects(rhi::CommandList& list, const char* programName, int stage,
                                  rhi::BufferHandle vb, uint32_t first, uint32_t count,
                                  rhi::BufferHandle ring, uint64_t ringOffset, const float* modelView,
                                  rhi::TextureHandle texture, bool additive, const float* colorModulator) {
        Program* p = GetProgram(programName, false, nullptr, nullptr, rhi::Format::Unknown,
                                AlphaFor(programName, -1.0f));
        if (!p || count == 0)
            return;
        std::vector<rhi::TextureHandle> colors;
        for (int idx : p->attachments) {
            if (idx < 0 || idx >= jshader::kMaxColorTex)
                idx = 0;
            Buffer& b = bufs[static_cast<size_t>(idx)];
            b.used = true;
            colors.push_back(b.tex[b.cur] ? b.tex[b.cur] : bufs[0].tex[bufs[0].cur]);
        }
        if (colors.empty())
            return;
        textureOverride = texture;
        std::vector<rhi::TextureHandle> attached = colors;
        attached.push_back(depth[0]);
        const rhi::BindGroupHandle textures = Textures(list, *p, attached);
        textureOverride = {};
        if (!textures)
            return;
        PipeState st;
        st.colorCount = static_cast<uint32_t>(colors.size());
        for (size_t i = 0; i < colors.size(); i++) {
            const rhi::TextureDesc* td = gpu.Dev().GetDesc(colors[i]);
            st.colors[i] = td ? td->format : rhi::Format::RGBA8Unorm;
        }
        st.depth = rhi::Format::D32Float;
        st.depthTest = false;
        st.depthWrite = false;
        const jshader::ProgramConfig* pc = config.Find(programName);
        st.blend = additive || stage == 5;
        st.src = rhi::BlendFactor::SrcAlpha;
        st.dst = additive ? rhi::BlendFactor::One : rhi::BlendFactor::InvSrcAlpha;
        st.srcA = rhi::BlendFactor::One;
        st.dstA = rhi::BlendFactor::InvSrcAlpha;
        if (pc && pc->blend.set) {
            st.blend = !pc->blend.off;
            if (st.blend) {
                st.src = GlFactor(pc->blend.src);
                st.dst = GlFactor(pc->blend.dst);
                st.srcA = GlFactor(pc->blend.srcA);
                st.dstA = GlFactor(pc->blend.dstA);
            }
        }
        const rhi::PipelineHandle pipe = PipelineFor(*p, st);
        if (!pipe)
            return;
        BeginPass(list, colors, depth[0], width, height, false, nullptr, "pack sky");
        jshader::PackDrawState d = TerrainDraw();
        std::memcpy(d.modelView, modelView, sizeof(d.modelView));
        d.renderStage = stage;
        if (colorModulator)
            std::memcpy(d.colorModulator, colorModulator, sizeof(d.colorModulator));
        if (!BindUniforms(list, *p, d)) {
            list.EndRendering();
            return;
        }
        list.SetBindGroup(1, textures);
        BindStorage(list, *p);
        list.SetPipeline(pipe);
        list.SetVertexBuffer(0, ring ? ring : vb,
                             ring ? ringOffset : static_cast<uint64_t>(first) * kPackVertexBytes);
        list.Draw(count, 1, 0, 0);
        list.EndRendering();
    }

    void JavaPack::DrawSky(rhi::CommandList& list) {
        if (!instance)
            return;
        const float rot[16] = {frameState.modelView[0],
                               frameState.modelView[1],
                               frameState.modelView[2],
                               frameState.modelView[3],
                               frameState.modelView[4],
                               frameState.modelView[5],
                               frameState.modelView[6],
                               frameState.modelView[7],
                               frameState.modelView[8],
                               frameState.modelView[9],
                               frameState.modelView[10],
                               frameState.modelView[11],
                               0,
                               0,
                               0,
                               1};
        // java's DimensionSpecialEffects: the nether draws no sky at all, the end draws its textured box
        const bool nether = instance->Dimension() == "world-1", end = instance->Dimension() == "world1";
        if (nether)
            return;
        if (end) {
            // LevelRenderer.renderEndSky: six faces of end_sky.png repeated 16 times, colour (40, 40, 40),
            // drawn with gbuffers_skytextured in iris's CUSTOM_SKY phase
            SkyBuilder sb;
            const uint8_t grey[4] = {40, 40, 40, 255};
            const glm::mat4 faces[6] = {
                glm::mat4(1.0f),
                glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)),
                glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1, 0, 0)),
                glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1, 0, 0)),
                glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1)),
                glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0, 0, 1)),
            };
            for (const glm::mat4& f : faces) {
                const glm::vec3 p[4] = {glm::vec3(f * glm::vec4(-100, -100, -100, 1)),
                                        glm::vec3(f * glm::vec4(-100, -100, 100, 1)),
                                        glm::vec3(f * glm::vec4(100, -100, 100, 1)),
                                        glm::vec3(f * glm::vec4(100, -100, -100, 1))};
                const float uv[4][2] = {{0, 0}, {0, 16}, {16, 16}, {16, 0}};
                for (int k : {0, 1, 2, 0, 2, 3})
                    sb.Vertex(p[k].x, p[k].y, p[k].z, grey, uv[k][0], uv[k][1]);
            }
            const gpu::RingSlice slice = gpu.Ring(sb.bytes.size(), 16);
            if (slice.data && skyEndTex) {
                std::memcpy(slice.data, sb.bytes.data(), sb.bytes.size());
                overrideRepeat = true;
                DrawSkyObjects(list, "gbuffers_skytextured", 3, {}, 0, sb.count, slice.buffer, slice.offset, rot,
                               skyEndTex, false);
                overrideRepeat = false;
            }
            return;
        }
        const double a = frame.sky.sunAngle;
        const glm::mat4 spin =
            glm::rotate(glm::mat4(1.0f), -glm::radians(config.sunPathRotation), glm::vec3(1, 0, 0)) *
            glm::rotate(glm::mat4(1.0f), static_cast<float>(a), glm::vec3(0, 0, 1));
        float spun[16];
        FromMat(ToMat(rot) * spin, spun);

        if (!skyStatic) {
            // stars first, then the sun quad (the moon changes with the phase, so it goes through the ring)
            SkyBuilder sb;
            const uint8_t white[4] = {255, 255, 255, 255};
            uint32_t seed = 10842u;
            const auto rnd = [&]() {
                seed = seed * 1664525u + 1013904223u;
                return static_cast<float>((seed >> 8) & 0xFFFFFF) / 16777216.0f;
            };
            for (int i = 0; i < 1500; i++) {
                const float z = rnd() * 2.0f - 1.0f, t = rnd() * 6.2831853f,
                            r = std::sqrt(std::max(0.0f, 1.0f - z * z));
                const glm::vec3 dir(r * std::cos(t), z, r * std::sin(t));
                const glm::vec3 right = glm::normalize(
                    glm::cross(dir, std::fabs(dir.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
                const glm::vec3 up = glm::cross(dir, right);
                const float size = 0.15f + rnd() * 0.1f;
                sb.Quad(dir * 100.0f, right * size, up * size, white, 0, 0, 1, 1);
            }
            starVertexCount = sb.count;
            sb.Quad(glm::vec3(0, 100, 0), glm::vec3(0, 0, 30), glm::vec3(30, 0, 0), white, 0, 0, 1, 1);
            skyStatic =
                gpu.CreateStatic(sb.bytes.data(), sb.bytes.size(), rhi::BufferUsage::Vertex, "java pack sky");
        }
        if (!skyStatic)
            return;

        // dome around the camera, tinted with the sky colour
        {
            SkyBuilder sb;
            const uint8_t sc[4] = {
                static_cast<uint8_t>(std::clamp(frameState.skyColor[0], 0.0f, 1.0f) * 255.0f),
                static_cast<uint8_t>(std::clamp(frameState.skyColor[1], 0.0f, 1.0f) * 255.0f),
                static_cast<uint8_t>(std::clamp(frameState.skyColor[2], 0.0f, 1.0f) * 255.0f), 255};
            constexpr int kLat = 12, kLon = 24;
            const auto point = [](int la, int lo) {
                const float phi = (static_cast<float>(la) / kLat - 0.5f) * 3.14159265f,
                            th = static_cast<float>(lo) / kLon * 6.2831853f;
                return glm::vec3(std::cos(phi) * std::cos(th), std::sin(phi), std::cos(phi) * std::sin(th)) *
                       100.0f;
            };
            for (int la = 0; la < kLat; la++)
                for (int lo = 0; lo < kLon; lo++) {
                    const glm::vec3 p00 = point(la, lo), p10 = point(la + 1, lo), p11 = point(la + 1, lo + 1),
                                    p01 = point(la, lo + 1);
                    sb.Vertex(p00.x, p00.y, p00.z, sc, 0, 0);
                    sb.Vertex(p10.x, p10.y, p10.z, sc, 0, 0);
                    sb.Vertex(p11.x, p11.y, p11.z, sc, 0, 0);
                    sb.Vertex(p00.x, p00.y, p00.z, sc, 0, 0);
                    sb.Vertex(p11.x, p11.y, p11.z, sc, 0, 0);
                    sb.Vertex(p01.x, p01.y, p01.z, sc, 0, 0);
                }
            const gpu::RingSlice slice = gpu.Ring(sb.bytes.size(), 16);
            if (slice.data) {
                std::memcpy(slice.data, sb.bytes.data(), sb.bytes.size());
                DrawSkyObjects(list, "gbuffers_skybasic", 1, {}, 0, sb.count, slice.buffer, slice.offset, rot,
                               {}, false);
            }
        }
        // sunrise / sunset fan (iris SUNSET phase): DimensionSpecialEffects.getSunriseColor, a fan of 16 segments
        // facing the sun on the horizon, centre colour with the fan's alpha fading to 0 at the rim
        if (frame.sky.sunrise[3] > 0.0f) {
            SkyBuilder sb;
            const auto c8 = [](float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
            const uint8_t centre[4] = {c8(frame.sky.sunrise[0]), c8(frame.sky.sunrise[1]), c8(frame.sky.sunrise[2]),
                                       c8(frame.sky.sunrise[3])};
            const uint8_t rim[4] = {centre[0], centre[1], centre[2], 0};
            // rotX(90) * rotZ(sun below/above: 180 or 0) * rotZ(90), as LevelRenderer.renderSky
            const float sinSun = std::sin(static_cast<float>(a));
            const glm::mat4 fan = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0)) *
                                  glm::rotate(glm::mat4(1.0f), glm::radians(sinSun < 0.0f ? 180.0f : 0.0f),
                                              glm::vec3(0, 0, 1)) *
                                  glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1));
            const glm::vec3 mid = glm::vec3(fan * glm::vec4(0, 100, 0, 1));
            glm::vec3 prev;
            for (int i = 0; i <= 16; i++) {
                const float t = static_cast<float>(i) * 6.2831853f / 16.0f;
                const float sn = std::sin(t), cs = std::cos(t);
                const glm::vec3 p = glm::vec3(fan * glm::vec4(sn * 120.0f, cs * 120.0f, -cs * 40.0f * frame.sky.sunrise[3], 1));
                if (i > 0) {
                    sb.Vertex(mid.x, mid.y, mid.z, centre, 0, 0);
                    sb.Vertex(prev.x, prev.y, prev.z, rim, 0, 0);
                    sb.Vertex(p.x, p.y, p.z, rim, 0, 0);
                }
                prev = p;
            }
            const gpu::RingSlice slice = gpu.Ring(sb.bytes.size(), 16);
            if (slice.data) {
                std::memcpy(slice.data, sb.bytes.data(), sb.bytes.size());
                DrawSkyObjects(list, "gbuffers_skybasic", 2, {}, 0, sb.count, slice.buffer, slice.offset, rot, {},
                               false);
            }
        }
        // the sun and moon fade out in rain: shader colour (1, 1, 1, 1 - rain)
        const float rainAlpha = std::clamp(frame.sky.rainBrightness, 0.0f, 1.0f);
        const float celestial[4] = {1.0f, 1.0f, 1.0f, rainAlpha};
        // sun
        DrawSkyObjects(list, "gbuffers_skytextured", 4, skyStatic, starVertexCount, 6, {}, 0, spun, skySunTex,
                       true, celestial);
        // moon: opposite the sun, one phase cell of the 4x2 sheet
        {
            SkyBuilder sb;
            const uint8_t white[4] = {255, 255, 255, 255};
            const int phase = static_cast<int>(std::floor(frame.sky.moonPhase)) & 7;
            const float u0 = static_cast<float>(phase % 4) / 4.0f, v0 = static_cast<float>(phase / 4) / 2.0f;
            sb.Quad(glm::vec3(0, -100, 0), glm::vec3(0, 0, 20), glm::vec3(20, 0, 0), white, u0, v0,
                    u0 + 0.25f, v0 + 0.5f);
            const gpu::RingSlice slice = gpu.Ring(sb.bytes.size(), 16);
            if (slice.data) {
                std::memcpy(slice.data, sb.bytes.data(), sb.bytes.size());
                // vanilla (and iris) draw the moon like the sun, additively: its black surround adds nothing
                DrawSkyObjects(list, "gbuffers_skytextured", 5, {}, 0, sb.count, slice.buffer, slice.offset,
                               spun, skyMoonTex, true, celestial);
            }
        }
        // stars rotate with the sun; vanilla skips them when dark is 0 and colours them with the star brightness
        // (times the rain fade) in all four channels
        const float stars = std::clamp(frame.sky.starBrightness * rainAlpha, 0.0f, 1.0f);
        if (stars > 0.0f) {
            const float starColor[4] = {stars, stars, stars, stars};
            DrawSkyObjects(list, "gbuffers_skybasic", 6, skyStatic, 0, starVertexCount, {}, 0, spun, {}, false,
                           starColor);
        }
        // the dark void disc below the horizon when the camera is under the horizon height (VOID phase)
        if (frame.sky.darkDisc) {
            SkyBuilder sb;
            const uint8_t black[4] = {0, 0, 0, 255};
            glm::vec3 prev(0.0f);
            for (int i = 0; i <= 16; i++) {
                const float t = static_cast<float>(i) * 6.2831853f / 16.0f;
                const glm::vec3 p(std::sin(t) * 512.0f, -16.0f, std::cos(t) * 512.0f);
                if (i > 0) {
                    sb.Vertex(0.0f, -16.0f, 0.0f, black, 0, 0);
                    sb.Vertex(p.x, p.y, p.z, black, 0, 0);
                    sb.Vertex(prev.x, prev.y, prev.z, black, 0, 0);
                }
                prev = p;
            }
            const gpu::RingSlice slice = gpu.Ring(sb.bytes.size(), 16);
            if (slice.data) {
                std::memcpy(slice.data, sb.bytes.data(), sb.bytes.size());
                DrawSkyObjects(list, "gbuffers_skybasic", 7, {}, 0, sb.count, slice.buffer, slice.offset, rot, {},
                               false);
            }
        }
    }

    bool JavaPack::RunPass(rhi::CommandList& list, const std::string& passName, bool toBackbuffer,
                           const Output& out) {
        GpuMark(list, passName);
        Program* p = GetProgram(passName, toBackbuffer);
        if (!p)
            return false;
        const jshader::ProgramConfig* pc = config.Find(passName);
        const float scale = pc && pc->hasScale ? pc->scale : 1.0f;
        uint32_t rw = width, rh = height;
        std::vector<rhi::TextureHandle> targets;
        PipeState st;
        st.scene = false;
        if (toBackbuffer) {
            rw = out.width;
            rh = out.height;
            targets.push_back(out.color);
            st.colors[0] = out.colorFormat;
            st.colorCount = 1;
        } else {
            for (int idx : p->attachments) {
                if (idx < 0 || idx >= jshader::kMaxColorTex)
                    idx = 0;
                Buffer& b = bufs[static_cast<size_t>(idx)];
                if (!b.used)
                    b.used = true;
                targets.push_back(b.tex[1 - b.cur]);
                const rhi::TextureDesc* td = gpu.Dev().GetDesc(b.tex[1 - b.cur]);
                st.colors[st.colorCount++] = td ? td->format : rhi::Format::RGBA8Unorm;
            }
            if (targets.empty())
                return true;
            const Buffer& b0 = bufs[static_cast<size_t>(
                std::max(0, std::min(p->attachments[0], jshader::kMaxColorTex - 1)))];
            rw = b0.w;
            rh = b0.h;
        }
        const uint32_t vw = std::max(1u, static_cast<uint32_t>(static_cast<float>(rw) * scale));
        const uint32_t vh = std::max(1u, static_cast<uint32_t>(static_cast<float>(rh) * scale));
        if (pc && pc->blend.set && !pc->blend.off) {
            st.blend = true;
            st.src = GlFactor(pc->blend.src);
            st.dst = GlFactor(pc->blend.dst);
            st.srcA = GlFactor(pc->blend.srcA);
            st.dstA = GlFactor(pc->blend.dstA);
        }
        const rhi::BindGroupHandle textures = Textures(list, *p, targets);
        const rhi::PipelineHandle pipe = PipelineFor(*p, st);
        if (!textures || !pipe)
            return false;
        // only the buffers a pass writes keep their old contents under a blend; the rest are fully overwritten
        std::vector<std::array<float, 4>> clears;
        if (toBackbuffer)
            clears.push_back({0, 0, 0, 1});
        const bool overwrite = !toBackbuffer && !st.blend && !(pc && !pc->blendPerBuffer.empty()) && vw == rw &&
                               vh == rh && p->source &&
                               p->source->fragment.text.find("discard") == std::string::npos &&
                               !std::getenv("PERLIN_PACK_KEEP_LOADS");
        BeginPass(list, targets, {}, vw, vh, false, toBackbuffer ? &clears : nullptr, "pack pass", overwrite);
        jshader::PackDrawState d;
        const float mv[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1};
        std::memcpy(d.modelView, mv, sizeof(mv));
        d.viewHeight = static_cast<int>(rh);
        if (!BindUniforms(list, *p, d)) {
            list.EndRendering();
            return false;
        }
        list.SetBindGroup(1, textures);
        BindStorage(list, *p);
        list.SetPipeline(pipe);
        list.SetVertexBuffer(0, quad);
        list.Draw(6, 1, 0, 0);
        list.EndRendering();
        if (p->imageMask || !config.bufferObjects.empty())
            StorageBarrier(list, *p);
        if (trace)
            std::fprintf(stderr, "[jshader] pass %s %ux%u targets %zu\n", passName.c_str(), vw, vh,
                         targets.size());
        for (int idx : p->attachments) {
            if (idx < 0 || idx >= jshader::kMaxColorTex)
                continue;
            Buffer& b = bufs[static_cast<size_t>(idx)];
            b.cur = 1 - b.cur;
        }
        return true;
    }

    void JavaPack::RunChain(rhi::CommandList& list, const char* base) {
        const bool shadowChain = std::strcmp(base, "shadowcomp") == 0;
        const uint32_t res = static_cast<uint32_t>(config.shadowMapResolution);
        for (const std::string& n : instance->PassChain(base)) {
            if (!instance->ProgramEnabled(n))
                continue;
            // iris runs a pass's compute shaders (name.csh, name_a.csh ...) before its fragment program
            RunCompute(list, n, shadowChain ? res : width, shadowChain ? res : height);
            const std::shared_ptr<jshader::ProgramSource> src = instance->Program(n);
            if (!src || src->fragment.text.empty())
                continue; // compute only
            Output unused;
            RunPass(list, n, false, unused);
        }
    }

    void JavaPack::CopyDepth(rhi::CommandList& list, rhi::TextureHandle src, rhi::TextureHandle dst) {
        gpu.Transition(list, src, rhi::State::CopySrc);
        gpu.Transition(list, dst, rhi::State::CopyDst);
        rhi::TextureCopy c;
        const rhi::TextureDesc* sd = gpu.Dev().GetDesc(src);
        c.width = sd ? sd->width : width;
        c.height = sd ? sd->height : height;
        list.CopyTexture(dst, src, std::span<const rhi::TextureCopy>(&c, 1));
        gpu.Transition(list, dst, rhi::State::ShaderRead);
    }

    void JavaPack::DepthToEngine(rhi::CommandList& list, const Output& out) {
        if (!out.depth)
            return;
        static const jshader::ExpandedSource vs = MakeSource(kFullscreenVertex),
                                             fs = MakeSource(kReverseDepthFragment);
        Program* p = GetProgram("builtin_reverse_depth", true, &vs, &fs);
        if (!p)
            return;
        PipeState st;
        st.scene = false;
        st.colors[0] = out.colorFormat;
        st.colorCount = 1;
        st.mask = rhi::ColorMask::None;
        st.depth = rhi::Format::D32Float;
        st.depthTest = true;
        st.depthWrite = true;
        st.depthOp = rhi::CompareOp::Always;
        const rhi::BindGroupHandle textures = Textures(list, *p, {out.color, out.depth});
        const rhi::PipelineHandle pipe = PipelineFor(*p, st);
        if (!textures || !pipe)
            return;
        BeginPass(list, {out.color}, out.depth, out.width, out.height, false, nullptr, "pack depth");
        jshader::PackDrawState d;
        const float mv[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1};
        std::memcpy(d.modelView, mv, sizeof(mv));
        d.viewHeight = static_cast<int>(out.height);
        if (BindUniforms(list, *p, d)) {
            list.SetBindGroup(1, textures);
            BindStorage(list, *p);
            list.SetPipeline(pipe);
            list.SetVertexBuffer(0, quad);
            list.Draw(6, 1, 0, 0);
        }
        list.EndRendering();
    }

    bool JavaPack::Record(rhi::CommandList& list, const FrameParams& params, const Output& out) {
        if (!instance || !out.color || out.width == 0 || out.height == 0)
            return false;
        const auto cpuStart = std::chrono::steady_clock::now();
        struct CpuTime {
            JavaPack& pack;
            std::chrono::steady_clock::time_point start;
            ~CpuTime() {
                if (pack.timing.on)
                    pack.timing.cpuMs +=
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            }
        } cpuTime{*this, cpuStart};
        // the pack renders at the engine's render scale and its final pass scales up to the screen
        const float scale = std::max(0.4f, std::min(1.0f, out.scale));
        const uint32_t iw = std::max(1u, static_cast<uint32_t>(static_cast<float>(out.width) * scale));
        const uint32_t ih = std::max(1u, static_cast<uint32_t>(static_cast<float>(out.height) * scale));
        if (width != iw || height != ih || !depth[0]) {
            gpu.Dev().WaitIdle();
            DestroyTargets();
            if (!CreateTargets(iw, ih)) {
                lastError = "could not create the shader pack's render targets";
                DestroyTargets();
                return false;
            }
        }
        frame = params;
        lightmapTex = out.lightmap;
        if (shadowSkipState < 0) {
            bool plain = config.images.empty() && config.bufferObjects.empty();
            for (const std::string& n : instance->PassChain("shadowcomp"))
                plain = plain && !instance->ProgramEnabled(n);
            shadowSkipState = plain ? 1 : 0;
        }
        {
            // iris HandRenderer: the hand projection with its depth squeezed by MC_HAND_DEPTH (0.125)
            ToGlDepth(params.handProjection, handProjection);
            for (int c = 0; c < 4; c++)
                handProjection[c * 4 + 2] *= 0.125f;
        }
        frameStamp++;
        // JSHADER_STATS: render passes and feedback copies per frame, every 60 frames
        if (static const bool stats = std::getenv("JSHADER_STATS") != nullptr; stats && frameStamp % 60 == 0)
            std::fprintf(stderr, "[jshader] frame %llu: %u passes, %u feedback copies\n",
                         static_cast<unsigned long long>(frameStamp), statPasses, statCopies);
        statPasses = statCopies = 0;
        GpuTimesBegin(list);
        GpuMark(list, "uniforms");
        BuildFrame(params, out);
        GpuMark(list, "clears");

        ClearImages(list, false);
        ClearBuffers(list, params);
        // setup runs once, when all of its compute shaders are ready
        if (!setupDone) {
            bool ready = true;
            for (const std::string& n : instance->PassChain("setup"))
                if (instance->ProgramEnabled(n))
                    for (size_t i = 0; i < (instance->Program(n) ? instance->Program(n)->compute.size() : 0);
                         i++)
                        ready = GetCompute(n, i) != nullptr ||
                                        !pending.contains("cs:" + n + ":" + std::to_string(i))
                                    ? ready
                                    : false;
            if (ready) {
                for (const std::string& n : instance->PassChain("setup"))
                    if (instance->ProgramEnabled(n))
                        RunCompute(list, n, width, height);
                setupDone = true;
            }
        }
        GpuMark(list, "begin");
        RunChain(list, "begin");
        GpuMark(list, "shadow");
        if (!shadowReuse) {
            DrawShadows(list);
            if (config.shadowEnabled && shadowDepth)
                RunChain(list, "shadowcomp");
        }
        GpuMark(list, "prepare");
        RunChain(list, "prepare");
        GpuMark(list, "sky");
        skySunTex = out.sun;
        skyMoonTex = out.moon;
        skyEndTex = out.endSky;
        DrawSky(list);
        GpuMark(list, "terrain");
        DrawTerrain(list);
        GpuMark(list, "entities+hand");
        using K = ExternalDraw::Kind;
        // iris order: opaque entities, block breaking and particles with the terrain; depthtex2 is the depth before
        // the hand; the solid hand; depthtex1 is the depth before translucents
        DrawExternal(list,
                     {K::Entity, K::Eyes, K::Glowing, K::Lightning, K::Glint, K::Block, K::Cracks, K::Line,
                      K::Particle},
                     false);
        if (depthTexUsed[2])
            CopyDepth(list, depth[0], depth[2]);
        DrawExternal(list, {K::Hand, K::HandGlint}, false);
        if (depthTexUsed[1])
            CopyDepth(list, depth[0], depth[1]);
        static const char* const debugTex = std::getenv("PERLIN_PACK_DEBUG_TEX");
        if (debugTex)
            DrawTranslucent(list);
        if (!debugTex) {
            GpuMark(list, "deferred");
            RunChain(list, "deferred");
            GpuMark(list, "translucent");
            // translucent terrain lands on the lit opaque picture, as in Iris, then translucent entities,
            // particles, weather and the translucent hand
            DrawTranslucent(list);
            DrawExternal(
                list,
                {K::EntityTranslucent, K::BlockTranslucent, K::ParticleTranslucent, K::Clouds, K::Weather},
                false);
            DrawExternal(list, {K::HandTranslucent}, false);
            RunChain(list, "composite");
        }
        GpuMark(list, "final");

        bool done = false;
        if (!debugTex && instance->ProgramExists("final"))
            done = RunPass(list, "final", true, out);
        if (!done) {
            std::string copySrc = kCopyColorFragment;
            if (debugTex)
                for (size_t at = copySrc.find("colortex0"); at != std::string::npos;
                     at = copySrc.find("colortex0", at + 1))
                    copySrc.replace(at, 9, std::string("colortex") + debugTex);
            static const jshader::ExpandedSource vs = MakeSource(kFullscreenVertex),
                                                 fs = MakeSource(copySrc.c_str());
            Program* p = GetProgram("builtin_copy_color", true, &vs, &fs);
            if (!p)
                return false;
            PipeState st;
            st.scene = false;
            st.colors[0] = out.colorFormat;
            st.colorCount = 1;
            const rhi::BindGroupHandle textures = Textures(list, *p, {out.color});
            const rhi::PipelineHandle pipe = PipelineFor(*p, st);
            if (!textures || !pipe)
                return false;
            const std::vector<std::array<float, 4>> clears = {{0, 0, 0, 1}};
            BeginPass(list, {out.color}, {}, out.width, out.height, false, &clears, "pack copy");
            jshader::PackDrawState d;
            const float mv[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1};
            std::memcpy(d.modelView, mv, sizeof(mv));
            d.viewHeight = static_cast<int>(out.height);
            if (BindUniforms(list, *p, d)) {
                list.SetBindGroup(1, textures);
                BindStorage(list, *p);
                list.SetPipeline(pipe);
                list.SetVertexBuffer(0, quad);
                list.Draw(6, 1, 0, 0);
            }
            list.EndRendering();
        }
        GpuMark(list, "depth to engine");
        DepthToEngine(list, out);
        GpuTimesEnd(list);
        return true;
    }

    // JSHADER_GPU_TIMES: gpu milliseconds of each part of the pack's frame (composite passes by name), averaged
    // and logged every 120 frames
    void JavaPack::GpuTimesBegin(rhi::CommandList& list) {
        static const bool on = std::getenv("JSHADER_GPU_TIMES") != nullptr;
        timing.on = on && gpu.Caps().features.timestampQueries;
        if (!timing.on)
            return;
        const uint32_t slots = std::max(1u, gpu.Frames());
        if (!timing.queries) {
            rhi::QuerySetDesc qd;
            qd.count = kTimerMarks * slots;
            qd.name = "pack gpu times";
            timing.queries = gpu.Dev().CreateQuerySet(qd);
            timing.readback.assign(slots, {});
            timing.names.assign(slots, {});
            for (auto& b : timing.readback) {
                rhi::BufferDesc bd;
                bd.size = kTimerMarks * sizeof(uint64_t);
                bd.usage = rhi::BufferUsage::CopyDst;
                bd.memory = rhi::MemoryClass::Readback;
                bd.name = "pack gpu times";
                b = gpu.Dev().CreateBuffer(bd);
            }
            if (!timing.queries) {
                timing.on = false;
                return;
            }
        }
        timing.slot = gpu.FrameSlot() % slots;
        // this slot's previous frame has retired: add its times up
        std::vector<std::string>& names = timing.names[timing.slot];
        if (names.size() >= 2) {
            if (const uint8_t* data = gpu.Dev().Map(timing.readback[timing.slot])) {
                const uint64_t* t = reinterpret_cast<const uint64_t*>(data);
                const double ns = gpu.Dev().TimestampPeriodNs();
                for (size_t i = 0; i + 1 < names.size(); i++)
                    if (t[i + 1] >= t[i])
                        timing.sum[names[i]] += static_cast<double>(t[i + 1] - t[i]) * ns * 1e-6;
                timing.frames++;
            }
        }
        static const int every = [] {
            const char* v = std::getenv("JSHADER_GPU_TIMES");
            return v && std::atoi(v) > 1 ? std::atoi(v) : 120;
        }();
        if (timing.frames >= every) {
            std::vector<std::pair<double, std::string>> rows;
            double total = 0;
            for (const auto& [n, ms] : timing.sum) {
                rows.emplace_back(ms / timing.frames, n);
                total += ms / timing.frames;
            }
            std::sort(rows.rbegin(), rows.rend());
            std::string line = "gpu ms per frame: total " + std::to_string(total).substr(0, 5) +
                               " (cpu recording " + std::to_string(timing.cpuMs / timing.frames).substr(0, 5) + " ms)";
            timing.cpuMs = 0;
            for (const auto& [ms, n] : rows)
                line += ", " + n + " " + std::to_string(ms).substr(0, 5);
            Log("jshader", "{}", line.c_str());
            std::vector<std::pair<double, std::string>> cpuRows;
            for (const auto& [n, ms] : timing.cpuSum)
                cpuRows.emplace_back(ms / timing.frames, n);
            std::sort(cpuRows.rbegin(), cpuRows.rend());
            std::string cpuLine = "cpu ms per frame:";
            for (const auto& [ms, n] : cpuRows)
                cpuLine += " " + n + " " + std::to_string(ms).substr(0, 5) + ",";
            Log("jshader", "{}", cpuLine.c_str());
            timing.cpuSum.clear();
            timing.sum.clear();
            timing.frames = 0;
        }
        names.clear();
        GpuMark(list, "start");
    }

    void JavaPack::GpuMark(rhi::CommandList& list, const std::string& name) {
        if (!timing.on)
            return;
        std::vector<std::string>& names = timing.names[timing.slot];
        if (names.size() + 1 >= kTimerMarks)
            return;
        list.WriteTimestamp(timing.queries, timing.slot * kTimerMarks + static_cast<uint32_t>(names.size()));
        const auto now = std::chrono::steady_clock::now();
        if (!names.empty())
            timing.cpuSum[names.back()] +=
                std::chrono::duration<double, std::milli>(now - timing.cpuLast).count();
        timing.cpuLast = now;
        names.push_back(name);
    }

    void JavaPack::GpuTimesEnd(rhi::CommandList& list) {
        if (!timing.on)
            return;
        GpuMark(list, "end");
        std::vector<std::string>& names = timing.names[timing.slot];
        list.ResolveQueries(timing.queries, timing.slot * kTimerMarks, static_cast<uint32_t>(names.size()),
                            timing.readback[timing.slot], 0);
    }
}
