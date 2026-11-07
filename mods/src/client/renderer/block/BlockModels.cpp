#include "client/renderer/block/BlockModels.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace perlin {
    namespace {
        struct Reader {
            const unsigned char* p;
            const unsigned char* end;
            bool ok = true;
            template <class T> T Get() {
                T v{};
                if (p + sizeof(T) > end) {
                    ok = false;
                    return v;
                }
                std::memcpy(&v, p, sizeof(T));
                p += sizeof(T);
                return v;
            }
        };
    }

    bool BlockModels::Load(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return false;
        std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return LoadFromMemory(data);
    }

    bool BlockModels::LoadFromMemory(const std::string& data) {
        if (data.size() < 8 || std::memcmp(data.data(), "PBM1", 4) != 0)
            return false;
        Reader r{reinterpret_cast<const unsigned char*>(data.data()) + 4,
                 reinterpret_cast<const unsigned char*>(data.data()) + data.size()};
        textures.clear();
        variants.clear();
        entries.clear();
        byHash.clear();

        const uint32_t nTex = r.Get<uint32_t>();
        for (uint32_t i = 0; i < nTex && r.ok; i++) {
            const uint16_t len = r.Get<uint16_t>();
            if (r.p + len > r.end) {
                r.ok = false;
                break;
            }
            textures.emplace_back(reinterpret_cast<const char*>(r.p), len);
            r.p += len;
        }
        const uint32_t nVar = r.Get<uint32_t>();
        variants.resize(r.ok ? nVar : 0);
        for (uint32_t i = 0; i < nVar && r.ok; i++) {
            const uint16_t nq = r.Get<uint16_t>();
            variants[i].resize(nq);
            for (ModelQuad& q : variants[i]) {
                q.dir = r.Get<uint8_t>();
                q.shadeDir = r.Get<uint8_t>();
                q.cull = r.Get<int8_t>();
                q.tint = r.Get<int8_t>();
                q.shade = r.Get<uint8_t>();
                q.light = r.Get<uint8_t>();
                q.tex = r.Get<uint16_t>();
                for (auto& v : q.pos)
                    for (float& c : v)
                        c = r.Get<float>();
                for (auto& v : q.uv)
                    for (float& c : v)
                        c = r.Get<float>();
            }
        }
        const uint32_t nEnt = r.Get<uint32_t>();
        entries.resize(r.ok ? nEnt : 0);
        for (uint32_t i = 0; i < nEnt && r.ok; i++) {
            const uint8_t flags = r.Get<uint8_t>();
            entries[i].offset = flags & 3;
            entries[i].cube = (flags & 0x80) != 0;
            const uint8_t ng = r.Get<uint8_t>();
            entries[i].groups.resize(ng);
            for (auto& g : entries[i].groups) {
                const uint8_t nc = r.Get<uint8_t>();
                g.resize(nc);
                for (auto& c : g) {
                    c.variant = r.Get<uint32_t>();
                    c.weight = r.Get<uint8_t>();
                    if (c.variant >= variants.size())
                        r.ok = false;
                }
            }
        }
        const uint32_t nBlk = r.Get<uint32_t>();
        for (uint32_t i = 0; i < nBlk && r.ok; i++) {
            const int32_t h = r.Get<int32_t>();
            const uint32_t e = r.Get<uint32_t>();
            if (e >= entries.size()) {
                r.ok = false;
                break;
            }
            byHash[h] = e;
        }
        if (!r.ok) {
            std::printf("[models] corrupt block_models.bin\n");
            textures.clear();
            variants.clear();
            entries.clear();
            byHash.clear();
            return false;
        }
        tiles.assign(textures.size(), 0);
        loaded = true;
        std::printf("[models] %zu block states, %zu variants, %zu textures\n", byHash.size(), variants.size(),
                    textures.size());
        return true;
    }
}
