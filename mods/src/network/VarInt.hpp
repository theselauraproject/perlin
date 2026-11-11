#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace perlin::wire {

    inline size_t VarUint32Size(uint32_t v) {
        size_t n = 1;
        while (v >= 0x80) {
            v >>= 7;
            n++;
        }
        return n;
    }

    inline void WriteVarUint32(std::vector<uint8_t>& out, uint32_t v) {
        while (v >= 0x80) {
            out.push_back(static_cast<uint8_t>((v & 0x7F) | 0x80));
            v >>= 7;
        }
        out.push_back(static_cast<uint8_t>(v));
    }

    inline void WriteVarUint64(std::vector<uint8_t>& out, uint64_t v) {
        while (v >= 0x80) {
            out.push_back(static_cast<uint8_t>((v & 0x7F) | 0x80));
            v >>= 7;
        }
        out.push_back(static_cast<uint8_t>(v));
    }

    inline void WriteBE32(std::vector<uint8_t>& out, int32_t v) {
        out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(v & 0xFF));
    }

    inline void WriteLE16(std::vector<uint8_t>& out, uint16_t v) {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }

    inline void WriteLE32(std::vector<uint8_t>& out, uint32_t v) {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    }

    inline void WriteLEFloat(std::vector<uint8_t>& out, float v) {
        uint32_t b = 0;
        memcpy(&b, &v, 4);
        WriteLE32(out, b);
    }

    inline void WriteZigzag32(std::vector<uint8_t>& out, int32_t v) {
        WriteVarUint32(out, (static_cast<uint32_t>(v) << 1) ^ static_cast<uint32_t>(v >> 31));
    }

    inline void WriteZigzag64(std::vector<uint8_t>& out, int64_t v) {
        WriteVarUint64(out, (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(-(v < 0 ? 1 : 0)));
    }

    inline void WriteString(std::vector<uint8_t>& out, const std::string& s) {
        WriteVarUint32(out, static_cast<uint32_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }

    class Reader {
    public:
        Reader(const uint8_t* data, size_t size) : p(data), end(data + size) {}

        bool ReadVarUint32(uint32_t& out) {
            uint32_t v = 0;
            unsigned shift = 0;
            while (p < end) {
                uint8_t b = *p++;
                v |= static_cast<uint32_t>(b & 0x7F) << shift;
                if (!(b & 0x80)) {
                    out = v;
                    return true;
                }
                shift += 7;
                if (shift >= 35)
                    return false;
            }
            return false;
        }

        bool ReadBE32(int32_t& out) {
            if (Remaining() < 4)
                return false;
            out = (static_cast<int32_t>(p[0]) << 24) | (static_cast<int32_t>(p[1]) << 16) |
                  (static_cast<int32_t>(p[2]) << 8) | static_cast<int32_t>(p[3]);
            p += 4;
            return true;
        }

        bool ReadLE16(uint16_t& out) {
            if (Remaining() < 2)
                return false;
            out = static_cast<uint16_t>(p[0] | (p[1] << 8));
            p += 2;
            return true;
        }

        bool ReadByte(uint8_t& out) {
            if (p >= end)
                return false;
            out = *p++;
            return true;
        }

        bool ReadLEFloat(float& out) {
            uint32_t b = 0;
            if (Remaining() < 4)
                return false;
            b = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            p += 4;
            memcpy(&out, &b, 4);
            return true;
        }

        bool ReadVarUint64(uint64_t& out) {
            uint64_t v = 0;
            unsigned shift = 0;
            while (p < end) {
                uint8_t b = *p++;
                v |= static_cast<uint64_t>(b & 0x7F) << shift;
                if (!(b & 0x80)) {
                    out = v;
                    return true;
                }
                shift += 7;
                if (shift >= 70)
                    return false;
            }
            return false;
        }

        bool ReadZigzag32(int32_t& out) {
            uint32_t raw = 0;
            if (!ReadVarUint32(raw))
                return false;
            out = (int32_t)((raw >> 1) ^ (0u - (raw & 1)));
            return true;
        }

        bool ReadZigzag64(int64_t& out) {
            uint64_t raw = 0;
            if (!ReadVarUint64(raw))
                return false;
            out = (int64_t)((raw >> 1) ^ (0ull - (raw & 1)));
            return true;
        }

        bool ReadString(std::string& out) {
            uint32_t len = 0;
            if (!ReadVarUint32(len))
                return false;
            if (Remaining() < len)
                return false;
            out.assign(reinterpret_cast<const char*>(p), len);
            p += len;
            return true;
        }

        bool ReadInt8(int8_t& out) {
            uint8_t b = 0;
            if (!ReadByte(b))
                return false;
            out = static_cast<int8_t>(b);
            return true;
        }

        bool ReadLE32(uint32_t& out) {
            if (Remaining() < 4)
                return false;
            out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            p += 4;
            return true;
        }

        bool ReadLE64(uint64_t& out) {
            uint32_t lo = 0, hi = 0;
            if (Remaining() < 8 || !ReadLE32(lo) || !ReadLE32(hi))
                return false;
            out = (uint64_t)lo | ((uint64_t)hi << 32);
            return true;
        }

        bool ReadBytes(std::vector<uint8_t>& out, size_t n) {
            if (Remaining() < n)
                return false;
            out.assign(p, p + n);
            p += n;
            return true;
        }

        bool ReadByteArray(std::vector<uint8_t>& out) {
            uint32_t len = 0;
            if (!ReadVarUint32(len))
                return false;
            return ReadBytes(out, len);
        }

        size_t Remaining() const { return static_cast<size_t>(end - p); }
        const uint8_t* Cursor() const { return p; }
        void Skip(size_t n) { p += n > Remaining() ? Remaining() : n; }

    private:
        const uint8_t* p;
        const uint8_t* end;
    };

}
