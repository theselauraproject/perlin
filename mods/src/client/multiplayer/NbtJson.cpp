#include "client/multiplayer/NbtJson.hpp"

#include "network/Stream.hpp"

namespace perlin {
    namespace {
        using net::ReadStream;

        bool ReadName(ReadStream& s, std::string& out) {
            uint32_t n = 0;
            s.varint(n);
            if (!s.ok || !s.length(n))
                return false;
            s.chars(out, n);
            return s.ok;
        }

        bool Payload(ReadStream& s, uint8_t type, int depth, ui::Json& out) {
            if (depth > 48)
                return false;
            switch (type) {
            case 1: {
                uint8_t v = 0;
                s.u8(v);
                out = ui::Json(static_cast<bool>(v != 0));
                out.number = static_cast<double>(v);
                out.type = ui::Json::Type::Bool;
                out.boolean = v != 0;
                break;
            }
            case 2: {
                int16_t v = 0;
                s.le(v);
                out = ui::Json(static_cast<double>(v));
                break;
            }
            case 3: {
                int32_t v = 0;
                s.zigzag(v);
                out = ui::Json(static_cast<double>(v));
                break;
            }
            case 4: {
                int64_t v = 0;
                s.zigzag(v);
                out = ui::Json(static_cast<double>(v));
                break;
            }
            case 5: {
                float v = 0;
                s.le(v);
                out = ui::Json(static_cast<double>(v));
                break;
            }
            case 6: {
                double v = 0;
                s.le(v);
                out = ui::Json(v);
                break;
            }
            case 7: {
                int32_t n = 0;
                s.zigzag(n);
                if (n < 0 || !s.length(static_cast<uint64_t>(n)))
                    return false;
                out = ui::Json::MakeArray();
                for (int32_t i = 0; i < n && s.ok; i++) {
                    uint8_t v = 0;
                    s.u8(v);
                    out.Push(ui::Json(static_cast<double>(v)));
                }
                break;
            }
            case 8: {
                std::string str;
                if (!ReadName(s, str))
                    return false;
                out = ui::Json(std::move(str));
                break;
            }
            case 9: {
                uint8_t element = 0;
                s.u8(element);
                int32_t n = 0;
                s.zigzag(n);
                if (!s.ok || n < 0 || (n > 0 && element == 0) || !s.length(static_cast<uint64_t>(n)))
                    return false;
                out = ui::Json::MakeArray();
                for (int32_t i = 0; i < n && s.ok; i++) {
                    ui::Json item;
                    if (!Payload(s, element, depth + 1, item))
                        return false;
                    out.Push(std::move(item));
                }
                break;
            }
            case 10: {
                out = ui::Json::MakeObject();
                while (s.ok) {
                    uint8_t child = 0;
                    s.u8(child);
                    if (!s.ok || child == 0)
                        break;
                    std::string key;
                    if (!ReadName(s, key))
                        return false;
                    ui::Json value;
                    if (!Payload(s, child, depth + 1, value))
                        return false;
                    out.Set(key, std::move(value));
                }
                break;
            }
            case 11: {
                int32_t n = 0;
                s.zigzag(n);
                if (n < 0 || !s.length(static_cast<uint64_t>(n)))
                    return false;
                out = ui::Json::MakeArray();
                for (int32_t i = 0; i < n && s.ok; i++) {
                    int32_t v = 0;
                    s.zigzag(v);
                    out.Push(ui::Json(static_cast<double>(v)));
                }
                break;
            }
            case 12: {
                int32_t n = 0;
                s.zigzag(n);
                if (n < 0 || !s.length(static_cast<uint64_t>(n)))
                    return false;
                out = ui::Json::MakeArray();
                for (int32_t i = 0; i < n && s.ok; i++) {
                    int64_t v = 0;
                    s.zigzag(v);
                    out.Push(ui::Json(static_cast<double>(v)));
                }
                break;
            }
            default:
                return false;
            }
            return s.ok;
        }
    }

    bool NetworkNbtToJson(const std::vector<uint8_t>& bytes, ui::Json& out) {
        if (bytes.empty())
            return false;
        ReadStream s(bytes.data(), bytes.size());
        uint8_t type = 0;
        s.u8(type);
        if (!s.ok || type == 0)
            return false;
        std::string rootName;
        if (!ReadName(s, rootName))
            return false;
        return Payload(s, type, 0, out);
    }

    size_t NetworkNbtPrefix(const uint8_t* data, size_t size, ui::Json& out) {
        if (!data || size == 0)
            return 0;
        ReadStream s(data, size);
        uint8_t type = 0;
        s.u8(type);
        if (!s.ok || type != 10)
            return 0;
        std::string rootName;
        if (!ReadName(s, rootName))
            return 0;
        if (!Payload(s, type, 0, out) || !s.ok)
            return 0;
        return size - s.remaining();
    }
}
