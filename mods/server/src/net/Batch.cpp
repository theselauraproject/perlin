#include "net/Batch.hpp"

#include <miniz.h>

namespace perlin::server {
    namespace {
        // a batch inflating past this is treated as hostile
        constexpr size_t kMaxInflated = 64u << 20;

        void PutVar(std::vector<uint8_t>& o, uint32_t v) {
            while (v >= 0x80) {
                o.push_back(static_cast<uint8_t>((v & 0x7F) | 0x80));
                v >>= 7;
            }
            o.push_back(static_cast<uint8_t>(v));
        }

        bool GetVar(std::span<const uint8_t> in, size_t& at, uint32_t& v) {
            v = 0;
            for (int shift = 0; shift < 35 && at < in.size(); shift += 7) {
                const uint8_t b = in[at++];
                v |= static_cast<uint32_t>(b & 0x7F) << shift;
                if (!(b & 0x80))
                    return true;
            }
            return false;
        }
    }

    void BatchCodec::AppendPacket(std::vector<uint8_t>& content, uint32_t id, std::span<const uint8_t> body) {
        // header: packet id in the low 10 bits, sender and target sub client ids above it (both 0)
        std::vector<uint8_t> header;
        PutVar(header, id & 0x3FF);
        PutVar(content, static_cast<uint32_t>(header.size() + body.size()));
        content.insert(content.end(), header.begin(), header.end());
        content.insert(content.end(), body.begin(), body.end());
    }

    std::vector<uint8_t> BatchCodec::Seal(const std::vector<uint8_t>& content) const {
        if (!compression)
            return content;
        std::vector<uint8_t> out;
        if (content.size() >= threshold) {
            size_t packed = 0;
            // no TDEFL_WRITE_ZLIB_HEADER: the wire wants raw deflate
            void* buf = tdefl_compress_mem_to_heap(content.data(), content.size(), &packed, 128);
            if (buf) {
                out.reserve(packed + 1);
                out.push_back(0x00);
                out.insert(out.end(), static_cast<uint8_t*>(buf), static_cast<uint8_t*>(buf) + packed);
                mz_free(buf);
                return out;
            }
        }
        out.reserve(content.size() + 1);
        out.push_back(0xFF);
        out.insert(out.end(), content.begin(), content.end());
        return out;
    }

    bool BatchCodec::Open(std::span<const uint8_t> wire, std::vector<uint8_t>& content) const {
        content.clear();
        if (!compression) {
            content.assign(wire.begin(), wire.end());
            return true;
        }
        if (wire.empty())
            return false;
        if (wire[0] == 0xFF) {
            content.assign(wire.begin() + 1, wire.end());
            return true;
        }
        if (wire[0] != 0x00)
            return false;
        size_t out = 0;
        void* buf = tinfl_decompress_mem_to_heap(wire.data() + 1, wire.size() - 1, &out, 0);
        if (!buf)
            return false;
        const bool fits = out <= kMaxInflated;
        if (fits)
            content.assign(static_cast<uint8_t*>(buf), static_cast<uint8_t*>(buf) + out);
        mz_free(buf);
        return fits;
    }

    bool BatchCodec::Split(std::span<const uint8_t> content, const PacketFn& fn) {
        size_t at = 0;
        while (at < content.size()) {
            uint32_t len = 0;
            if (!GetVar(content, at, len) || len > content.size() - at)
                return false;
            const std::span<const uint8_t> packet = content.subspan(at, len);
            size_t h = 0;
            uint32_t header = 0;
            if (!GetVar(packet, h, header))
                return false;
            fn(header & 0x3FF, packet.subspan(h));
            at += len;
        }
        return true;
    }
}
