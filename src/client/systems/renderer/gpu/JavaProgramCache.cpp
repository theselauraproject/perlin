#include "client/systems/renderer/gpu/JavaProgramCache.hpp"

#include "util/FileIo.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>

namespace perlin::gpu::program_cache {
    namespace {
        constexpr uint32_t kCacheMagic = 0x4a504b34u;

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

        template <class T> void Put(std::string& out, const T& v) {
            out.append(reinterpret_cast<const char*>(&v), sizeof(T));
        }
        void PutString(std::string& out, const std::string& s) {
            Put<uint32_t>(out, static_cast<uint32_t>(s.size()));
            out += s;
        }
        struct Reader {
            const std::string& data;
            size_t at = 0;
            bool ok = true;

            template <class T> T Get() {
                T value{};
                if (!Take(sizeof(T))) {
                    return value;
                }
                std::memcpy(&value, data.data() + at - sizeof(T), sizeof(T));
                return value;
            }
            std::string Str() {
                const uint32_t size = Get<uint32_t>();
                if (!Take(size))
                    return {};
                return data.substr(at - size, size);
            }
            uint32_t Count(size_t minElementBytes) {
                const uint32_t count = Get<uint32_t>();
                if (ok && count > (data.size() - at) / minElementBytes)
                    ok = false;
                return ok ? count : 0;
            }

        private:
            bool Take(size_t bytes) {
                if (!ok || bytes > data.size() - at) {
                    ok = false;
                    return false;
                }
                at += bytes;
                return true;
            }
        };
    }

    std::filesystem::path PathFor(const jshader::ExpandedSource& vs, const jshader::ExpandedSource& fs,
                                      const jshader::TransformOptions& o, int kind) {
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
        std::error_code ec;
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path(ec) / "perlin_javapack_cache";
        if (ec)
            return {};
        std::filesystem::create_directories(dir, ec);
        if (ec)
            return {};
        char file[64];
        std::snprintf(file, sizeof(file), "%016llx.prog", static_cast<unsigned long long>(h));
        return dir / file;
    }

void Store(const std::filesystem::path& file, const Program& c) {
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
        Put<uint32_t>(out, static_cast<uint32_t>(c.silentOutputs.size()));
        for (int b : c.silentOutputs)
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
        Put<uint32_t>(out, static_cast<uint32_t>(c.imageDecls.size()));
        for (const auto& d : c.imageDecls) {
            PutString(out, d.name);
            PutString(out, d.type);
            PutString(out, d.imageFormat);
        }
        PutString(out, std::string(c.vertex.begin(), c.vertex.end()));
        PutString(out, std::string(c.fragment.begin(), c.fragment.end()));
        const std::filesystem::path tmp =
            file.string() + "." + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            f.write(out.data(), static_cast<std::streamsize>(out.size()));
            if (!f)
                return;
        }
        std::error_code ec;
        std::filesystem::rename(tmp, file, ec);
    }

    std::optional<Program> Load(const std::filesystem::path& file) {
        if (file.empty())
            return std::nullopt;
        std::string data;
        {
            std::ifstream f(file, std::ios::binary);
            if (!f)
                return std::nullopt;
            file::ReadRest(f, data);
        }
        Reader r{data};
        if (r.Get<uint32_t>() != kCacheMagic)
            return std::nullopt;
        Program c;
        c.uniformRegs = r.Get<int32_t>();
        for (uint32_t i = 0, n = r.Count(8); i < n && r.ok; i++) {
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
        for (uint32_t i = 0, n = r.Count(sizeof(int32_t)); i < n && r.ok; i++)
            c.drawBuffers.push_back(r.Get<int32_t>());
        for (uint32_t i = 0, n = r.Count(sizeof(int32_t)); i < n && r.ok; i++)
            c.silentOutputs.push_back(r.Get<int32_t>());
        for (uint32_t i = 0, n = r.Count(5); i < n && r.ok; i++) {
            jshader::RhiSampler s;
            s.name = r.Str();
            s.slot = r.Get<int32_t>();
            s.shadow = r.Get<uint8_t>() != 0;
            c.samplers.push_back(std::move(s));
        }
        for (uint32_t i = 0, n = r.Count(sizeof(uint32_t)); i < n && r.ok; i++)
            c.images.push_back(r.Str());
        for (uint32_t i = 0, n = r.Count(sizeof(uint32_t) * 3); i < n && r.ok; i++) {
            jshader::SamplerDecl d;
            d.name = r.Str();
            d.type = r.Str();
            d.imageFormat = r.Str();
            d.image = true;
            c.imageDecls.push_back(std::move(d));
        }
        const std::string vs = r.Str(), fs = r.Str();
        c.vertex.assign(vs.begin(), vs.end());
        c.fragment.assign(fs.begin(), fs.end());
        if (!r.ok || c.vertex.empty() || c.fragment.empty())
            return std::nullopt;
        return c;
    }
}
