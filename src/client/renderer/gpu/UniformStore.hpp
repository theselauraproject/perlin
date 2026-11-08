#pragma once
#include <conduit/shader_library.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace perlin::gpu {

    class UniformStore {
    public:
        static constexpr int kMaxFloats = 32;

        int Id(std::string_view name) {

            const auto it = ids.find(name);
            if (it != ids.end())
                return it->second;
            const int id = static_cast<int>(values.size());
            values.emplace_back();
            values.back().fill(0.0f);
            ids.emplace(std::string(name), id);
            return id;
        }

        void Set(int id, const float* v, int count) {
            auto& slot = values[static_cast<size_t>(id)];
            if (std::memcmp(slot.data(), v, sizeof(float) * static_cast<size_t>(count)) == 0)
                return;
            std::memcpy(slot.data(), v, sizeof(float) * static_cast<size_t>(count));
            version++;
        }

        void SetI(int id, const int32_t* v, int count) {
            auto& slot = values[static_cast<size_t>(id)];
            if (std::memcmp(slot.data(), v, sizeof(int32_t) * static_cast<size_t>(count)) == 0)
                return;
            std::memcpy(slot.data(), v, sizeof(int32_t) * static_cast<size_t>(count));
            version++;
        }
        void SetI(std::string_view name, const int32_t* v, int count) { SetI(Id(name), v, count); }
        void SetI(std::string_view name, int32_t v) { SetI(Id(name), &v, 1); }
        void Set(std::string_view name, const float* v, int count) { Set(Id(name), v, count); }
        void Vec4(std::string_view name, float x, float y, float z, float w) {
            const float v[4] = {x, y, z, w};
            Set(Id(name), v, 4);
        }

        void Mat4(int id, const float* columnMajor) {
            float t[16];
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++)
                    t[r * 4 + c] = columnMajor[c * 4 + r];
            Set(id, t, 16);
        }
        void Mat4(std::string_view name, const float* columnMajor) { Mat4(Id(name), columnMajor); }

        const float* Get(int id) const { return values[static_cast<size_t>(id)].data(); }
        uint64_t Version() const { return version; }

        struct Entry {
            int id;
            uint32_t offset;
            uint32_t bytes;
        };
        struct Binder {
            std::vector<Entry> entries;
            uint32_t bytes = 0;
        };

        const Binder& For(const conduit::program_info& program, int index) {
            if (binders.size() <= static_cast<size_t>(index))
                binders.resize(static_cast<size_t>(index) + 1);
            Binder& b = binders[static_cast<size_t>(index)];
            if (b.bytes == 0 && b.entries.empty() && program.uniform_bytes) {
                b.bytes = program.uniform_bytes;
                for (const conduit::uniform_member& m : program.uniforms) {

                    const uint32_t bytes = std::min<uint32_t>(m.bytes, kMaxFloats * 4);
                    b.entries.push_back({Id(m.name), m.offset, bytes});
                }
            }
            return b;
        }

        void Write(const Binder& b, uint8_t* dst) const {
            std::memset(dst, 0, b.bytes);
            for (const Entry& e : b.entries)
                std::memcpy(dst + e.offset, values[static_cast<size_t>(e.id)].data(), e.bytes);
        }

        void ForgetBinders() { binders.clear(); }

    private:
        struct NameHash {
            using is_transparent = void;
            size_t operator()(std::string_view v) const { return std::hash<std::string_view>{}(v); }
        };
        std::unordered_map<std::string, int, NameHash, std::equal_to<>> ids;
        std::vector<std::array<float, kMaxFloats>> values;
        std::vector<Binder> binders;
        uint64_t version = 1;
    };
}
