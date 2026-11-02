#include "client/renderer/gpu/JavaPack.hpp"

#include "client/javashaders/GlslangBridge.hpp"
#include "client/javashaders/PackSource.hpp"

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
        constexpr int kCacheVersion = 4;

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

        std::string Normalize(std::string n) {
            if (n.compare(0, 10, "minecraft:") == 0)
                n.erase(0, 10);
            return n;
        }

        std::string BaseName(const std::string& entry) {
            std::vector<std::string> parts;
            size_t start = 0;
            for (;;) {
                const size_t colon = entry.find(':', start);
                parts.push_back(
                    entry.substr(start, colon == std::string::npos ? std::string::npos : colon - start));
                if (colon == std::string::npos)
                    break;
                start = colon + 1;
            }
            std::vector<std::string> kept;
            for (const std::string& p : parts)
                if (!p.contains('='))
                    kept.push_back(p);
            if (kept.empty())
                return {};
            return kept.size() >= 2 ? kept[1] : kept[0];
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

        const char* kFullscreenVertex = "#version 120\nvarying vec2 texcoord;\nvoid main() { gl_Position = "
                                        "ftransform(); texcoord = gl_MultiTexCoord0.xy; }\n";
        // the engine's depth is reversed (1 near, 0 far): the pack's conventional depth goes back as 1 - d
        const char* kReverseDepthFragment =
            "#version 120\nuniform sampler2D depthtex0;\nvarying vec2 texcoord;\nvoid main() { gl_FragDepth "
            "= 1.0 - texture2D(depthtex0, texcoord).r; }\n";
        const char* kCopyColorFragment =
            "#version 120\nuniform sampler2D colortex0;\nvarying vec2 texcoord;\n/* DRAWBUFFERS:0 */\nvoid "
            "main() { gl_FragData[0] = texture2D(colortex0, texcoord); }\n";

        enum SamplerId : int {
            SPointClamp,
            SLinearClamp,
            SLinearRepeat,
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
        if (atlas)
            gpu.DestroyTexture(atlas);
        atlas = {};
        white = flatNormal = blackTex = noise = {};
        customTextures.clear();
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
            }
            return t;
        };
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
            }
            stbi_image_free(data);
            return t;
        };
        if (!config.noiseTexture.empty()) {
            const rhi::TextureHandle t = load(config.noiseTexture);
            if (t)
                noise = t;
        }
        for (const jshader::CustomTexture& ct : config.textures) {
            if (customTextures.contains(ct.sampler))
                continue;
            const rhi::TextureHandle t = load(ct.path);
            if (t)
                customTextures[ct.sampler] = t;
        }
    }

    void JavaPack::ScanUsage() {
        std::set<std::string> names;
        for (const jshader::ProgramInfo& p : jshader::StandardPrograms())
            names.insert(p.name);
        for (const char* base : {"begin", "prepare", "deferred", "composite", "shadowcomp"})
            for (const std::string& n : instance->PassChain(base))
                names.insert(n);
        names.insert("final");
        const std::regex re("(colortex|shadowcolor|gaux)([0-9]+)|\\b(gcolor|gdepth)\\b|\\bshadowtex[01]\\b|"
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
                                   (len == 10 && std::strncmp(w, "shadowtex", 9) == 0 &&
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
            for (int i = 0; i < 2; i++)
                if (shadowColorUsed[i])
                    shadowColor[i] = MakeTexture(res, res, ToFormat(config.shadowColorFormat[i]), kColor,
                                                 "pack shadow color");
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
            bool persistent = t == white || t == flatNormal || t == blackTex || t == noise;
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
        name = instance->Name().empty() ? cfg.path : instance->Name();
        config = jshader::ParsePipelineConfig(*instance);
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
                std::fprintf(stderr, "[jshader] image %s: format %s / %dD is not supported\n",
                             info.name.c_str(), info.internalFormat.c_str(), info.dims);
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
        pool.Start(static_cast<int>(std::max(2u, std::min(6u, std::thread::hardware_concurrency() > 2
                                                                  ? std::thread::hardware_concurrency() - 1
                                                                  : 2u))));
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
    }

    void JavaPack::BuildBlockMap() {
        nameToPackId.clear();
        paletteMap.assign(65536, -2);
        if (!instance)
            return;
        std::unordered_map<std::string, bool> plain;
        for (const auto& kv : instance->BlockIds())
            for (const std::string& entry : kv.second) {
                const std::string base = BaseName(entry);
                if (base.empty())
                    continue;
                const bool isPlain = !entry.contains('=');
                auto it = nameToPackId.find(base);
                if (it == nameToPackId.end() || (isPlain && !plain[base])) {
                    nameToPackId[base] = kv.first;
                    plain[base] = isPlain;
                }
            }
    }

    int JavaPack::PackBlockId(uint16_t palette) {
        if (paletteMap.empty())
            return -1;
        int16_t& slot = paletteMap[palette];
        if (slot != -2)
            return slot;
        int result = -1;
        if (blockName) {
            std::string n = Normalize(blockName(palette));
            auto it = nameToPackId.find(n);
            if (it == nameToPackId.end())
                if (const char* alias = Alias(n))
                    it = nameToPackId.find(alias);
            if (it != nameToPackId.end())
                result = it->second;
        }
        slot = static_cast<int16_t>(std::clamp(result, -1, 32000));
        return slot;
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
            const int packId = block ? PackBlockId(block) : -1;
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
            if (layerType > 0.5f && std::getenv("JSHADER_WATERDBG")) {
                static int shown = 0;
                if (shown++ < 6)
                    std::fprintf(stderr,
                                 "[jshader] translucent quad block=%u name='%s' packId=%d rgba=%08x\n", block,
                                 blockName ? blockName(block).c_str() : "?", packId, v[0].rgba);
            }
            const float entity[2] = {static_cast<float>(packId), layerType};
            for (int i = 0; i < 4; i++) {
                uint8_t* o = out.data() + (q + static_cast<size_t>(i)) * kPackVertexBytes;
                const float pos[3] = {v[i].x, v[i].y, v[i].z};
                const uint32_t abgr = v[i].rgba;
                const float uv[2] = {au[i], av[i]};
                const float light[2] = {static_cast<float>(v[i].light & 0xFF),
                                        static_cast<float>((v[i].light >> 8) & 0xFF)};
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
        } else if (n == "shadowtex0" || n == "shadowtex1" || n == "shadow" || n == "watershadow") {
            b.kind = Kind::ShadowDepth;
            b.index = 0;
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
        } else if (customTextures.contains(n))
            b.kind = Kind::Custom;
        else
            b.kind = Kind::Dummy;
        return b;
    }

    void JavaPack::FillProgramResources(Program& p, const std::string& progName,
                                        const std::vector<jshader::RhiSampler>& samplerList,
                                        const std::vector<std::string>& imageNames,
                                        const std::vector<jshader::UniformDecl>& uniforms, int regs) {
        p.regs = regs;
        for (const jshader::RhiSampler& s : samplerList) {
            if (s.slot >= static_cast<int>(kTextureSlots)) {
                std::fprintf(stderr, "[jshader] %s: sampler '%s' needs slot %d (only %u exist)\n",
                             progName.c_str(), s.name.c_str(), s.slot, kTextureSlots);
                p.valid = false;
                continue;
            }
            p.samplers.push_back(ResolveSampler(s));
        }
        for (const std::string& img : imageNames) {
            const auto it = imageSlotOf.find(img);
            if (it == imageSlotOf.end() || !images[static_cast<size_t>(it->second)].tex) {
                std::fprintf(stderr, "[jshader] %s: image '%s' has no texture\n", progName.c_str(),
                             img.c_str());
                p.valid = false;
                continue;
            }
            p.imageSlots.push_back(it->second);
            p.imageMask |= 1u << it->second;
        }
        if (p.regs > 256) {
            std::fprintf(stderr, "[jshader] %s: %d uniform registers (limit 256)\n", progName.c_str(),
                         p.regs);
            p.valid = false;
        }
        for (const jshader::UniformDecl& d : uniforms) {
            UniformSlot slot;
            slot.decl = d;
            slot.known = table.Known(d.name);
            static const std::set<std::string> perDraw = {"entityColor", "entityId", "blockEntityId",
                                                          "currentRenderedItemId", "renderStage"};
            slot.perDraw = d.name.compare(0, 5, "iris_") == 0 || perDraw.contains(d.name);
            if (!slot.known && loggedMissing.insert(progName + ":" + d.name).second)
                std::fprintf(stderr, "[jshader] %s: unknown uniform '%s' (left at 0)\n", progName.c_str(),
                             d.name.c_str());
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
        sd.stage = rhi::ShaderStage::Vertex;
        sd.code = rp.vertex;
        p->vs = gpu.Dev().CreateShader(sd);
        sd.stage = rhi::ShaderStage::Fragment;
        sd.code = rp.fragment;
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

    JavaPack::Program* JavaPack::GetProgram(const std::string& progName, bool toBackbuffer,
                                            const jshader::ExpandedSource* vs,
                                            const jshader::ExpandedSource* fs, rhi::Format) {
        const std::string key = KeyFor(progName, toBackbuffer);
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
            jshader::TransformOptions opts;
            opts.macros = instance->EngineMacros();
            opts.preprocess = jshader::PreprocessGlsl;
            opts.availableAttribs = AttribMask();
            opts.zeroToOneDepth = true;
            opts.splitSamplers = true;
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
            const jshader::RhiShaderKind kindUsed = std::getenv("JSHADER_FORCE_HLSL")
                                                        ? jshader::RhiShaderKind::Hlsl
                                                        : kind; // conversion check without a d3d device
            const jshader::DxbcCompiler dxbc = jshader::SystemDxbcCompiler();
            const std::filesystem::path cacheFile =
                CacheFile(vertex, fragment, opts, static_cast<int>(kind), progName);
            auto fut = pool.Submit([vertex = std::move(vertex), fragment = std::move(fragment), opts,
                                    kind = kindUsed, dxbc, cacheFile]() {
                auto c = std::make_shared<Converted>();
                CachedProgram cached;
                if (LoadCache(cacheFile, cached)) {
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
                } else {
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
            std::fprintf(stderr, "[jshader] program %s failed: %.1500s\n", progName.c_str(),
                         c->error.c_str());
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
        if (!p->valid) {
            std::fprintf(stderr, "[jshader] program %s could not be created on this device\n",
                         progName.c_str());
            return nullptr;
        }
        return p;
    }

    int JavaPack::WarmUp() {
        int remaining = 0, started = 0;
        const auto touch = [&](const std::string& n, bool toBackbuffer) {
            const std::string key = KeyFor(n, toBackbuffer);
            if (programs.contains(key))
                return;
            if (!pending.contains(key)) {
                if (started >= 2) {
                    remaining++;
                    return;
                }
                started++;
            }
            GetProgram(n, toBackbuffer);
            if (!programs.contains(key))
                remaining++;
        };
        for (const char* n : {"gbuffers_terrain", "gbuffers_water", "shadow"})
            touch(n, false);
        for (const char* base : {"begin", "prepare", "deferred", "composite", "shadowcomp"})
            for (const std::string& n : instance->PassChain(base))
                if (instance->ProgramEnabled(n))
                    touch(n, false);
        touch("final", true);
        return remaining;
    }

    bool JavaPack::TerrainUsable() {
        const auto it = programs.find(KeyFor("gbuffers_terrain", false));
        return it != programs.end() && it->second->valid;
    }

    rhi::PipelineHandle JavaPack::PipelineFor(Program& p, const PipeState& st) {
        char key[256];
        std::snprintf(key, sizeof(key), "%p|%d|%u|%d|%d%d|%d|%d|%d,%d,%d,%d|%d|", static_cast<void*>(&p),
                      st.scene ? 1 : 0, st.colorCount, static_cast<int>(st.depth), st.depthTest,
                      st.depthWrite, static_cast<int>(st.depthOp), st.blend ? 1 : 0, static_cast<int>(st.src),
                      static_cast<int>(st.dst), static_cast<int>(st.srcA), static_cast<int>(st.dstA),
                      static_cast<int>(st.mask));
        std::string k = key;
        for (uint32_t i = 0; i < st.colorCount; i++)
            k += std::to_string(static_cast<int>(st.colors[i])) + ",";
        const auto it = pipelines.find(k);
        if (it != pipelines.end())
            return it->second;
        rhi::GraphicsPipelineDesc gd;
        gd.vertex = p.vs;
        gd.fragment = p.fs;
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
        gd.raster.cull = rhi::CullMode::None;
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
        for (const UniformSlot& s : p.slots) {
            if (perDrawOnly && !s.perDraw)
                continue;
            std::memset(natural, 0, sizeof(natural));
            int comps = 0;
            if (s.known)
                table.Evaluate(s.decl.name, ctx, natural, comps);
            float* dst = (perDrawOnly ? p.drawRegs : p.frameRegs).data() + s.decl.reg * 4;
            jshader::PackUniform(s.decl, natural, dst);
        }
    }

    bool JavaPack::BindUniforms(rhi::CommandList& list, Program& p, const jshader::PackDrawState& draw) {
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
            int smp = SLinearClamp;
            switch (s.kind) {
            case Kind::Color: {
                const Buffer& b = bufs[static_cast<size_t>(s.index)];
                t = b.used ? b.tex[b.cur] : blackTex;
                smp = config.color[static_cast<size_t>(s.index)].linear ? SLinearClamp : SPointClamp;
                break;
            }
            case Kind::Depth:
                t = depth[s.index];
                smp = SPointClamp;
                break;
            case Kind::ShadowDepth:
                t = shadowDepth ? shadowDepth : white;
                if (s.shadow)
                    smp = config.shadowFilterLinear[0] ? SCompareLinear : SComparePoint;
                else
                    smp = config.shadowFilterLinear[0] ? SLinearClamp : SPointClamp;
                break;
            case Kind::ShadowColor:
                t = shadowColor[s.index] ? shadowColor[s.index] : white;
                smp = config.shadowColorLinear[s.index] ? SLinearClamp : SPointClamp;
                break;
            case Kind::Noise:
                t = noise;
                smp = SLinearRepeat;
                break;
            case Kind::Atlas:
                if (textureOverride)
                    t = textureOverride;
                else if (atlas)
                    t = atlas;
                smp = SAtlas;
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
                t = customTextures[s.name];
                smp = SLinearRepeat;
                break;
            case Kind::Image: {
                // iris samples custom images with linear filtering, integer ones nearest
                const Image& img = images[static_cast<size_t>(s.index)];
                t = img.tex;
                smp = img.integer ? SPointClamp : SLinearClamp;
                set.dim[static_cast<size_t>(s.slot)] = img.dim;
                break;
            }
            case Kind::Dummy:
                break;
            }
            if (!t || isAttached(t)) {
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
                std::fprintf(stderr, "[jshader] image %s (%ux%ux%u) could not be created\n",
                             info.name.c_str(), img.w, img.h, img.d);
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
            auto fut = pool.Submit([text = source->compute[index], opts, kind, dxbc]() {
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
            std::fprintf(stderr, "[jshader] compute %s #%zu failed: %.1500s\n", progName.c_str(), index,
                         c->error.c_str());
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
        sd.code = rc.code;
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
        FromMat(rotation, s.modelView);
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

        const double a = params.sky.sunAngle;
        double frac = a / (2.0 * kPi) + 0.25;
        frac -= std::floor(frac);
        s.sunAngle = static_cast<float>(frac);
        s.shadowAngle = s.sunAngle <= 0.5f ? s.sunAngle : s.sunAngle - 0.5f;
        s.worldTime = static_cast<int>(frac * 24000.0) % 24000;
        s.worldDay = 0;
        s.moonPhase = static_cast<int>(std::floor(params.sky.moonPhase)) & 7;
        const glm::vec3 sunDir(-std::sin(static_cast<float>(a)), std::cos(static_cast<float>(a)), 0.0f);
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
        for (int i = 0; i < 3; i++) {
            s.skyColor[i] = params.sky.skyColor[i];
            s.fogColor[i] = params.fogColor[i];
        }
        s.fogStart = params.renderDistanceFogStart;
        s.fogEnd = params.renderDistanceFogEnd;
        s.fogDensity = 1.0f;
        s.rainStrength = std::clamp(1.0f - params.sky.rainBrightness, 0.0f, 1.0f);
        s.wetness = s.rainStrength;
        s.thunderStrength = 0.0f;
        s.frameCounter = static_cast<int>(params.frameIndex);
        s.frameTimeCounter = params.timeSeconds;
        s.frameTime = std::clamp(params.timeSeconds - lastTime, 0.0f, 0.25f);
        lastTime = params.timeSeconds;
        const float c = s.projection[10], d = s.projection[14];
        if (std::fabs(c - 1.0f) > 1e-6f && std::fabs(c + 1.0f) > 1e-6f) {
            s.nearPlane = d / (c - 1.0f);
            s.farPlane = d / (c + 1.0f);
        }
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

        // the shadow camera sits on the light and snaps to whole shadow texels so the map does not swim while the player moves
        glm::vec3 lightDir = dayLight ? sunDir : -sunDir;
        if (std::fabs(config.sunPathRotation) > 0.001f) {
            const glm::mat4 tilt =
                glm::rotate(glm::mat4(1.0f), glm::radians(config.sunPathRotation), glm::vec3(1, 0, 0));
            lightDir = glm::vec3(tilt * glm::vec4(lightDir, 0.0f));
        }
        lightDir = glm::normalize(lightDir);
        glm::vec3 up(0, 0, 1);
        if (std::fabs(lightDir.z) > 0.99f)
            up = glm::vec3(0, 1, 0);
        const glm::mat4 lookAt = glm::lookAt(lightDir * 100.0f, glm::vec3(0), up);
        const glm::vec3 camera(params.cameraPosition[0], params.cameraPosition[1], params.cameraPosition[2]);
        const float r = std::max(8.0f, config.shadowDistance);
        const float texel = 2.0f * r / static_cast<float>(std::max(1, config.shadowMapResolution));
        const glm::vec3 camLight = glm::mat3(lookAt) * camera;
        const glm::vec2 snapped = glm::round(glm::vec2(camLight) / texel) * texel;
        // the camera ends up (within a texel) at the middle of the map, halfway down the depth range so occluders toward the light are kept
        const glm::mat4 sv =
            glm::translate(glm::mat4(1.0f), glm::vec3(-snapped.x, -snapped.y, -camLight.z - 128.0f)) * lookAt;
        // the pack's shaders hand shadowModelView camera-relative positions; the shadow pass draws world positions
        FromMat(sv * glm::translate(glm::mat4(1.0f), camera), shadowView);
        FromMat(glm::orthoRH_NO(-r, r, -r, r, 0.05f, 256.0f), shadowProjection);
        FromMat(sv, shadowDrawView);
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
        static const RenderLayer kShadowLayers[3] = {RenderLayer::Solid, RenderLayer::Cutout,
                                                     RenderLayer::CutoutCull};
        for (auto& v : shadowRuns)
            v.clear();
        if (!config.shadowEnabled)
            return;
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
            for (size_t i = 0; i < 3; i++) {
                const size_t slot = static_cast<size_t>(kShadowLayers[i]);
                if (g.layerCount[slot])
                    shadowRuns[i].push_back(
                        {static_cast<uint32_t>(g.page), g.iOff[slot], g.layerCount[slot]});
            }
        }
    }

    // -- passes --------------------------------------------------------------------------------------------------------

    void JavaPack::BeginPass(rhi::CommandList& list, const std::vector<rhi::TextureHandle>& colors,
                             rhi::TextureHandle depthTex, uint32_t w, uint32_t h, bool clearDepth,
                             const std::vector<std::array<float, 4>>* clears, const char* passName) {
        rhi::RenderingDesc rd;
        rd.colorCount = static_cast<uint32_t>(colors.size());
        for (size_t i = 0; i < colors.size(); i++) {
            gpu.Transition(list, colors[i], rhi::State::ColorTarget);
            rd.colors[i].texture = colors[i];
            const bool clear = clears && i < clears->size() && (*clears)[i][0] > -1e20f;
            rd.colors[i].load = clear ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
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
        constexpr float kKeep = -1e30f;
        std::vector<int> used;
        for (int i = 0; i < jshader::kMaxColorTex; i++)
            if (bufs[static_cast<size_t>(i)].used)
                used.push_back(i);
        for (size_t g = 0; g < used.size(); g += 8) {
            std::vector<rhi::TextureHandle> tex;
            std::vector<std::array<float, 4>> clears;
            for (size_t k = g; k < used.size() && k < g + 8; k++) {
                const int idx = used[k];
                Buffer& b = bufs[static_cast<size_t>(idx)];
                const jshader::BufferSetup& cfg = config.color[static_cast<size_t>(idx)];
                tex.push_back(b.tex[b.cur]);
                std::array<float, 4> c = {kKeep, 0, 0, 0};
                if (cfg.clear || gpu.StateOf(b.tex[b.cur]) == rhi::State::Undefined) {
                    c = {cfg.clearColor[0], cfg.clearColor[1], cfg.clearColor[2], cfg.clearColor[3]};
                    if (idx == 0 && instance->GetDirectives().Text("colortex0ClearColor").empty())
                        c = {params.fogColor[0], params.fogColor[1], params.fogColor[2], 1.0f};
                    if (idx == 1 && instance->GetDirectives().Text("colortex1ClearColor").empty())
                        c = {1.0f, 1.0f, 1.0f, 1.0f};
                }
                clears.push_back(c);
            }
            const bool first = g == 0;
            BeginPass(list, tex, first ? depth[0] : rhi::TextureHandle{},
                      bufs[static_cast<size_t>(used[g])].w, bufs[static_cast<size_t>(used[g])].h, first,
                      &clears, "pack clear");
            list.EndRendering();
        }
    }

    bool JavaPack::DrawLayers(rhi::CommandList& list, const char* programName,
                              const std::vector<RenderLayer>& layers, bool translucent, bool shadow) {
        Program* p = GetProgram(programName, false);
        if (!p)
            return false;
        bool any = false;
        if (trace)
            std::fprintf(stderr, "[jshader] draw %s shadow=%d runs: solid %zu cutout %zu translucent %zu\n",
                         programName, shadow ? 1 : 0, layerRuns[0].size(), layerRuns[1].size(),
                         layerRuns[2].size());
        for (RenderLayer layer : layers) {
            const auto& list2 = shadow ? shadowRuns[layer == RenderLayer::Solid    ? 0
                                                    : layer == RenderLayer::Cutout ? 1
                                                                                   : 2]
                                       : layerRuns[static_cast<size_t>(layer)];
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
        const jshader::ProgramConfig* pc = config.Find(programName);

        if (shadow) {
            const std::vector<std::array<float, 4>> zero(colors.size(), std::array<float, 4>{0, 0, 0, 0});
            BeginPass(list, colors, depthTarget, w, h, true, &zero, "pack shadow");
        } else {
            BeginPass(list, colors, depthTarget, w, h, false, nullptr, "pack scene");
        }

        jshader::PackDrawState draw = TerrainDraw();
        if (shadow) {
            std::memcpy(draw.modelView, shadowDrawView, sizeof(draw.modelView));
            std::memcpy(draw.projection, shadowProjection, sizeof(draw.projection));
            for (int c = 0; c < 3; c++)
                for (int r = 0; r < 3; r++)
                    draw.normalMatrix[c * 3 + r] = shadowDrawView[c * 4 + r];
            draw.renderStage = 0;
        }
        if (!BindUniforms(list, *p, draw)) {
            list.EndRendering();
            return false;
        }
        list.SetBindGroup(1, textures);
        BindStorage(list, *p);
        for (RenderLayer layer : layers) {
            const auto& runList = shadow ? shadowRuns[layer == RenderLayer::Solid    ? 0
                                                      : layer == RenderLayer::Cutout ? 1
                                                                                     : 2]
                                         : layerRuns[static_cast<size_t>(layer)];
            if (runList.empty())
                continue;
            PipeState ls = st;
            const bool blendLayer = !shadow && translucent;
            ls.blend = blendLayer;
            ls.depthWrite = true; // depthtex0 holds the translucent surfaces too
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
                std::fprintf(stderr, "[jshader] %s: pipeline creation failed\n", programName);
                continue;
            }
            list.SetPipeline(pipe);
            for (const ChunkMeshStore::Run& r : runList) {
                const rhi::BufferHandle vb = meshes.Extension(r.page);
                const rhi::BufferHandle ib = meshes.Indices(r.page, layer);
                if (!vb || !ib) {
                    if (trace)
                        std::fprintf(stderr, "[jshader] %s: page %u has no %s\n", programName, r.page,
                                     vb ? "indices" : "pack vertices");
                    continue;
                }
                if (trace)
                    std::fprintf(stderr, "[jshader] %s: draw %u indices from %u\n", programName, r.count,
                                 r.first);
                list.SetVertexBuffer(0, vb);
                list.SetIndexBuffer(ib, rhi::IndexType::U32);
                list.DrawIndexed(r.count, 1, r.first, 0, 0);
            }
        }
        list.EndRendering();
        if (p->imageMask || !config.bufferObjects.empty())
            StorageBarrier(list, *p); // shadow voxelization writes images from the vertex shader
        return true;
    }

    void JavaPack::DrawShadows(rhi::CommandList& list) {
        if (!config.shadowEnabled || !shadowDepth)
            return;
        DrawLayers(list, "shadow", {RenderLayer::Solid, RenderLayer::Cutout, RenderLayer::CutoutCull}, false,
                   true);
    }

    void JavaPack::DrawTerrain(rhi::CommandList& list) {
        DrawLayers(list, "gbuffers_terrain",
                   {RenderLayer::Solid, RenderLayer::Cutout, RenderLayer::CutoutCull}, false, false);
    }

    void JavaPack::DrawTranslucent(rhi::CommandList& list) {
        DrawLayers(list, "gbuffers_water", {RenderLayer::Translucent, RenderLayer::TranslucentCull}, true,
                   false);
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
                                  rhi::TextureHandle texture, bool additive) {
        Program* p = GetProgram(programName, false);
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
        const double a = frame.sky.sunAngle;
        const glm::mat4 spin = glm::rotate(glm::mat4(1.0f), static_cast<float>(a), glm::vec3(0, 0, 1));
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
        // stars rotate with the sun
        DrawSkyObjects(list, "gbuffers_skybasic", 6, skyStatic, 0, starVertexCount, {}, 0, spun, {}, false);
        // sun
        DrawSkyObjects(list, "gbuffers_skytextured", 4, skyStatic, starVertexCount, 6, {}, 0, spun, skySunTex,
                       true);
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
                DrawSkyObjects(list, "gbuffers_skytextured", 5, {}, 0, sb.count, slice.buffer, slice.offset,
                               spun, skyMoonTex, false);
            }
        }
    }

    bool JavaPack::RunPass(rhi::CommandList& list, const std::string& passName, bool toBackbuffer,
                           const Output& out) {
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
        BeginPass(list, targets, {}, vw, vh, false, toBackbuffer ? &clears : nullptr, "pack pass");
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
        c.width = width;
        c.height = height;
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
        frameStamp++;
        BuildFrame(params, out);

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
        RunChain(list, "begin");
        DrawShadows(list);
        // iris runs shadowcomp only with a shadow pass
        if (config.shadowEnabled && shadowDepth)
            RunChain(list, "shadowcomp");
        RunChain(list, "prepare");
        skySunTex = out.sun;
        skyMoonTex = out.moon;
        DrawSky(list);
        DrawTerrain(list);
        // depthtex1 / depthtex2 hold the opaque world: water and glass are drawn after the copy
        CopyDepth(list, depth[0], depth[2]);
        CopyDepth(list, depth[0], depth[1]);
        static const char* const debugTex = std::getenv("PERLIN_PACK_DEBUG_TEX");
        if (debugTex)
            DrawTranslucent(list);
        if (!debugTex) {
            RunChain(list, "deferred");
            // translucent terrain lands on the lit opaque picture, as in Iris
            DrawTranslucent(list);
            RunChain(list, "composite");
        }

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
        DepthToEngine(list, out);
        return true;
    }
}
