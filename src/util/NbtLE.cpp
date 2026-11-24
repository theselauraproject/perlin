#include "util/NbtLE.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace perlin::nbtle {
    namespace {
        struct In {
            std::span<const uint8_t> b;
            size_t at = 0;
            bool ok = true;
            bool Need(size_t n) {
                if (!ok || b.size() - at < n || at > b.size())
                    ok = false;
                return ok;
            }
            uint64_t Le(size_t n) {
                if (!Need(n))
                    return 0;
                uint64_t v = 0;
                for (size_t i = 0; i < n; i++)
                    v |= static_cast<uint64_t>(b[at + i]) << (8 * i);
                at += n;
                return v;
            }
            std::string Str() {
                const size_t n = static_cast<size_t>(Le(2));
                if (!Need(n))
                    return {};
                std::string s(reinterpret_cast<const char*>(b.data() + at), n);
                at += n;
                return s;
            }
        };

        bool Payload(In& in, Type type, Tag& t, int depth) {
            t.type = type;
            if (depth > 64)
                return false;
            switch (type) {
            case Byte:
                t.integer = static_cast<int8_t>(in.Le(1));
                break;
            case Short:
                t.integer = static_cast<int16_t>(in.Le(2));
                break;
            case Int:
                t.integer = static_cast<int32_t>(in.Le(4));
                break;
            case Long:
                t.integer = static_cast<int64_t>(in.Le(8));
                break;
            case Float: {
                const uint32_t bits = static_cast<uint32_t>(in.Le(4));
                float f;
                std::memcpy(&f, &bits, 4);
                t.real = f;
                break;
            }
            case Double: {
                const uint64_t bits = in.Le(8);
                double d;
                std::memcpy(&d, &bits, 8);
                t.real = d;
                break;
            }
            case ByteArray:
            case IntArray:
            case LongArray: {
                const int32_t n = static_cast<int32_t>(in.Le(4));
                const size_t width = type == ByteArray ? 1 : type == IntArray ? 4 : 8;
                if (n < 0 || !in.Need(static_cast<size_t>(n) * width))
                    return false;
                t.numbers.reserve(static_cast<size_t>(n));
                for (int32_t i = 0; i < n; i++) {
                    const uint64_t v = in.Le(width);
                    t.numbers.push_back(width == 1   ? static_cast<int8_t>(v)
                                        : width == 4 ? static_cast<int32_t>(v)
                                                     : static_cast<int64_t>(v));
                }
                break;
            }
            case String:
                t.text = in.Str();
                break;
            case List: {
                t.listType = static_cast<Type>(in.Le(1));
                const int32_t n = static_cast<int32_t>(in.Le(4));
                if (n < 0 || n > (1 << 24))
                    return false;
                for (int32_t i = 0; i < n && in.ok; i++) {
                    Tag item;
                    if (!Payload(in, t.listType, item, depth + 1))
                        return false;
                    t.items.push_back(std::move(item));
                }
                break;
            }
            case Compound:
                while (in.ok) {
                    const Type child = static_cast<Type>(in.Le(1));
                    if (!in.ok || child == End)
                        break;
                    std::string name = in.Str();
                    Tag value;
                    if (!Payload(in, child, value, depth + 1))
                        return false;
                    t.fields.emplace_back(std::move(name), std::move(value));
                }
                break;
            default:
                return false;
            }
            return in.ok;
        }

        void Le(std::vector<uint8_t>& out, uint64_t v, size_t n) {
            for (size_t i = 0; i < n; i++)
                out.push_back(static_cast<uint8_t>(v >> (8 * i)));
        }
        void Str(std::vector<uint8_t>& out, const std::string& s) {
            Le(out, std::min<size_t>(s.size(), 0xFFFF), 2);
            out.insert(out.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(s.size(), 0xFFFF)));
        }

        void Emit(std::vector<uint8_t>& out, const Tag& t) {
            switch (t.type) {
            case Byte:
                Le(out, static_cast<uint64_t>(t.integer), 1);
                break;
            case Short:
                Le(out, static_cast<uint64_t>(t.integer), 2);
                break;
            case Int:
                Le(out, static_cast<uint64_t>(t.integer), 4);
                break;
            case Long:
                Le(out, static_cast<uint64_t>(t.integer), 8);
                break;
            case Float: {
                const float f = static_cast<float>(t.real);
                uint32_t bits;
                std::memcpy(&bits, &f, 4);
                Le(out, bits, 4);
                break;
            }
            case Double: {
                uint64_t bits;
                std::memcpy(&bits, &t.real, 8);
                Le(out, bits, 8);
                break;
            }
            case ByteArray:
            case IntArray:
            case LongArray: {
                const size_t width = t.type == ByteArray ? 1 : t.type == IntArray ? 4 : 8;
                Le(out, t.numbers.size(), 4);
                for (int64_t v : t.numbers)
                    Le(out, static_cast<uint64_t>(v), width);
                break;
            }
            case String:
                Str(out, t.text);
                break;
            case List:
                Le(out, t.items.empty() && t.listType == End ? End : t.listType, 1);
                Le(out, t.items.size(), 4);
                for (const Tag& item : t.items)
                    Emit(out, item);
                break;
            case Compound:
                for (const auto& [name, value] : t.fields) {
                    Le(out, value.type, 1);
                    Str(out, name);
                    Emit(out, value);
                }
                Le(out, End, 1);
                break;
            default:
                break;
            }
        }
    }

    const Tag* Tag::Find(const std::string& name) const {
        for (const auto& [n, v] : fields)
            if (n == name)
                return &v;
        return nullptr;
    }

    Tag* Tag::Find(const std::string& name) {
        for (auto& [n, v] : fields)
            if (n == name)
                return &v;
        return nullptr;
    }

    int64_t Tag::Int(const std::string& name, int64_t fallback) const {
        const Tag* t = Find(name);
        return t && t->type >= Byte && t->type <= Long ? t->integer : fallback;
    }

    std::string Tag::Str(const std::string& name, const std::string& fallback) const {
        const Tag* t = Find(name);
        return t && t->type == String ? t->text : fallback;
    }

    void Tag::SetString(const std::string& name, const std::string& value) {
        if (Tag* t = Find(name)) {
            t->type = String;
            t->text = value;
            return;
        }
        Tag t;
        t.type = String;
        t.text = value;
        fields.emplace_back(name, std::move(t));
    }

    bool Read(std::span<const uint8_t> bytes, Tag& root, std::string* rootName) {
        In in{bytes};
        const Type type = static_cast<Type>(in.Le(1));
        if (!in.ok || type != Compound)
            return false;
        std::string name = in.Str();
        if (rootName)
            *rootName = name;
        root = Tag{};
        return Payload(in, Compound, root, 0);
    }

    std::vector<uint8_t> Write(const Tag& root, const std::string& rootName) {
        std::vector<uint8_t> out;
        Le(out, Compound, 1);
        Str(out, rootName);
        Emit(out, root);
        return out;
    }

    bool ReadLevelDat(const std::string& path, Tag& root, uint32_t& storageVersion) {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return false;
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (bytes.size() < 8)
            return false;
        storageVersion = static_cast<uint32_t>(bytes[0] | bytes[1] << 8 | bytes[2] << 16 | static_cast<uint32_t>(bytes[3]) << 24);
        return Read(std::span<const uint8_t>(bytes.data() + 8, bytes.size() - 8), root);
    }

    bool WriteLevelDat(const std::string& path, const Tag& root, uint32_t storageVersion) {
        const std::vector<uint8_t> body = Write(root);
        std::vector<uint8_t> out;
        Le(out, storageVersion, 4);
        Le(out, body.size(), 4);
        out.insert(out.end(), body.begin(), body.end());
        const std::string temp = path + ".tmp";
        {
            std::ofstream f(temp, std::ios::binary | std::ios::trunc);
            if (!f)
                return false;
            f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
            if (!f)
                return false;
        }
        std::error_code ec;
        std::filesystem::rename(temp, path, ec);
        return !ec;
    }
}
