#pragma once
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace perlin::net {

    struct Uuid {
        std::array<uint8_t, 16> bytes{};
        bool operator==(const Uuid&) const = default;
    };

    struct Nbt {
        std::vector<uint8_t> bytes;
        bool empty() const { return bytes.empty(); }
    };

    namespace context {
        inline std::atomic<int32_t>& ShieldItemIdSlot() {
            static std::atomic<int32_t> slot{-1};
            return slot;
        }
        inline int32_t ShieldItemID() {
            return ShieldItemIdSlot().load(std::memory_order_relaxed);
        }
        inline void SetShieldItemID(int32_t id) {
            ShieldItemIdSlot().store(id, std::memory_order_relaxed);
        }
        inline std::atomic<int32_t>& ProtocolSlot() {
            static std::atomic<int32_t> slot{0};
            return slot;
        }
        inline int32_t Protocol() {
            return ProtocolSlot().load(std::memory_order_relaxed);
        }
        inline void SetProtocol(int32_t protocol) {
            ProtocolSlot().store(protocol, std::memory_order_relaxed);
        }
    }

    class ReadStream {
    public:
        static constexpr bool kReading = true;

        ReadStream(const uint8_t* data, size_t size) : cursor(data), end(data + size) {}

        bool ok = true;

        size_t remaining() const { return static_cast<size_t>(end - cursor); }
        bool more() const { return ok && cursor < end; }
        const uint8_t* position() const { return cursor; }

        void boolean(bool& v) {
            uint8_t b = 0;
            u8(b);
            v = b != 0;
        }
        void boolean(uint8_t& v) {
            u8(v);
            v = v != 0 ? 1 : 0;
        }
        void u8(uint8_t& v) {
            if (!Need(1))
                return;
            v = *cursor++;
        }
        void i8(int8_t& v) {
            uint8_t b = 0;
            u8(b);
            v = static_cast<int8_t>(b);
        }

        template <class T> void le(T& v) { Fixed<false>(v); }
        template <class T> void be(T& v) { Fixed<true>(v); }

        template <class T> void varint(T& v) {
            static_assert(std::is_unsigned_v<T>);
            T out = 0;
            for (unsigned shift = 0; shift < sizeof(T) * 8 + 7; shift += 7) {
                uint8_t b = 0;
                u8(b);
                if (!ok)
                    return;
                if (shift < sizeof(T) * 8)
                    out |= static_cast<T>(static_cast<T>(b & 0x7F) << shift);
                if (!(b & 0x80)) {
                    v = out;
                    return;
                }
            }
            ok = false;
        }

        template <class T> void zigzag(T& v) {
            using U = std::make_unsigned_t<T>;
            U raw = 0;
            varint(raw);
            v = static_cast<T>((raw >> 1) ^ (~(raw & 1) + 1));
        }

        void uuid(Uuid& v) { Bytes(v.bytes.data(), v.bytes.size()); }

        void varint128(std::array<uint64_t, 2>& v) {
            std::array<uint64_t, 2> out{};
            for (unsigned shift = 0; shift < 133; shift += 7) {
                uint8_t b = 0;
                u8(b);
                if (!ok)
                    return;
                const uint64_t bits = b & 0x7F;
                if (shift < 64) {
                    out[0] |= bits << shift;
                    if (shift > 57)
                        out[1] |= bits >> (64 - shift);
                } else if (shift < 128) {
                    out[1] |= bits << (shift - 64);
                }
                if (!(b & 0x80)) {
                    v = out;
                    return;
                }
            }
            ok = false;
        }

        void chars(std::string& v, size_t n) {
            if (!Need(n))
                return;
            v.assign(reinterpret_cast<const char*>(cursor), n);
            cursor += n;
        }
        void bytes(std::vector<uint8_t>& v, size_t n) {
            if (!Need(n))
                return;
            v.assign(cursor, cursor + n);
            cursor += n;
        }
        void rest(std::vector<uint8_t>& v) { bytes(v, remaining()); }
        void skip(size_t n) {
            if (Need(n))
                cursor += n;
        }

        template <class N> bool count(N n) {
            if constexpr (std::is_signed_v<N>) {
                if (n < 0) {
                    ok = false;
                    return false;
                }
            }
            if (static_cast<uint64_t>(n) > remaining()) {
                ok = false;
                return false;
            }
            return ok;
        }

        template <class N> bool length(N n) {
            if constexpr (std::is_signed_v<N>) {
                if (n < 0) {
                    ok = false;
                    return false;
                }
            }
            return Need(static_cast<size_t>(n));
        }

        ReadStream slice(size_t n) {
            if (!Need(n))
                return ReadStream(cursor, 0);
            ReadStream sub(cursor, n);
            cursor += n;
            return sub;
        }

        void nbt(Nbt& v, bool little);

    private:
        bool Need(size_t n) {
            if (!ok || remaining() < n) {
                ok = false;
                return false;
            }
            return true;
        }
        void Bytes(uint8_t* dst, size_t n) {
            if (!Need(n))
                return;
            std::memcpy(dst, cursor, n);
            cursor += n;
        }
        template <bool Big, class T> void Fixed(T& v) {
            static_assert(std::is_arithmetic_v<T>);
            using U = std::conditional_t<
                sizeof(T) == 1, uint8_t,
                std::conditional_t<sizeof(T) == 2, uint16_t,
                                   std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>>>;
            if (!Need(sizeof(T)))
                return;
            U raw = 0;
            for (size_t i = 0; i < sizeof(T); i++) {
                const size_t shift = Big ? (sizeof(T) - 1 - i) * 8 : i * 8;
                raw |= static_cast<U>(static_cast<U>(cursor[i]) << shift);
            }
            cursor += sizeof(T);
            v = std::bit_cast<T>(raw);
        }

        const uint8_t* cursor;
        const uint8_t* end;
    };

    class WriteStream {
    public:
        static constexpr bool kReading = false;

        explicit WriteStream(std::vector<uint8_t>& out) : out(out) {}

        bool ok = true;

        size_t remaining() const { return 0; }
        bool more() const { return false; }

        void boolean(bool& v) { out.push_back(v ? 1 : 0); }
        void boolean(uint8_t& v) { out.push_back(v ? 1 : 0); }
        void u8(uint8_t& v) { out.push_back(v); }
        void i8(int8_t& v) { out.push_back(static_cast<uint8_t>(v)); }

        template <class T> void le(T& v) { Fixed<false>(v); }
        template <class T> void be(T& v) { Fixed<true>(v); }

        template <class T> void varint(T& v) {
            static_assert(std::is_unsigned_v<T>);
            T value = v;
            while (value >= 0x80) {
                out.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
                value >>= 7;
            }
            out.push_back(static_cast<uint8_t>(value));
        }

        template <class T> void zigzag(T& v) {
            using U = std::make_unsigned_t<T>;
            U raw = static_cast<U>((static_cast<U>(v) << 1) ^ static_cast<U>(v >> (sizeof(T) * 8 - 1)));
            varint(raw);
        }

        void uuid(Uuid& v) { out.insert(out.end(), v.bytes.begin(), v.bytes.end()); }

        void varint128(std::array<uint64_t, 2>& v) {
            uint64_t lo = v[0], hi = v[1];
            for (;;) {
                uint8_t b = static_cast<uint8_t>(lo & 0x7F);
                lo = (lo >> 7) | (hi << 57);
                hi >>= 7;
                if (lo == 0 && hi == 0) {
                    out.push_back(b);
                    return;
                }
                out.push_back(b | 0x80);
            }
        }

        void chars(std::string& v, size_t) { out.insert(out.end(), v.begin(), v.end()); }
        void bytes(std::vector<uint8_t>& v, size_t) { out.insert(out.end(), v.begin(), v.end()); }
        void rest(std::vector<uint8_t>& v) { out.insert(out.end(), v.begin(), v.end()); }

        template <class N> bool count(N) { return true; }
        template <class N> bool length(N) { return true; }

        void nbt(Nbt& v, bool little) {
            if (v.bytes.empty()) {
                out.push_back(0x0A);
                out.push_back(0x00);
                if (little)
                    out.push_back(0x00);
                out.push_back(0x00);
                return;
            }
            out.insert(out.end(), v.bytes.begin(), v.bytes.end());
        }

        std::vector<uint8_t>& buffer() { return out; }

    private:
        template <bool Big, class T> void Fixed(T& v) {
            static_assert(std::is_arithmetic_v<T>);
            using U = std::conditional_t<
                sizeof(T) == 1, uint8_t,
                std::conditional_t<sizeof(T) == 2, uint16_t,
                                   std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>>>;
            const U raw = std::bit_cast<U>(v);
            for (size_t i = 0; i < sizeof(T); i++) {
                const size_t shift = Big ? (sizeof(T) - 1 - i) * 8 : i * 8;
                out.push_back(static_cast<uint8_t>(raw >> shift));
            }
        }

        std::vector<uint8_t>& out;
    };

    template <class T> void Transfer(ReadStream& s, T& v) {
        if (!Read(s, v))
            s.ok = false;
    }
    template <class T> void Transfer(WriteStream& s, T& v) {
        Write(s, v);
    }

    template <class F> void Frame(ReadStream& s, F&& body) {
        uint32_t length = 0;
        s.varint(length);
        if (!s.length(length))
            return;
        if (length == 0)
            return;
        ReadStream sub = s.slice(length);
        body(sub);
        if (!sub.ok)
            s.ok = false;
    }
    template <class F> void Frame(WriteStream& s, F&& body) {
        std::vector<uint8_t> scratch;
        WriteStream sub(scratch);
        body(sub);
        uint32_t length = static_cast<uint32_t>(scratch.size());
        s.varint(length);
        s.buffer().insert(s.buffer().end(), scratch.begin(), scratch.end());
        if (!sub.ok)
            s.ok = false;
    }

    template <size_t I, class V> auto* PickAt(ReadStream&, V& variant) {
        return &variant.template emplace<I>();
    }
    template <size_t I, class V> auto* PickAt(WriteStream& s, V& variant) {
        auto* value = std::get_if<I>(&variant);
        if (!value)
            s.ok = false;
        return value;
    }
    template <class T> T* Pick(ReadStream&, std::optional<T>& slot) {
        return &slot.emplace();
    }
    template <class T> T* Pick(WriteStream& s, std::optional<T>& slot) {
        if (!slot)
            s.ok = false;
        return slot ? &*slot : nullptr;
    }

    template <class T> bool Present(ReadStream& s, std::optional<T>& slot) {
        bool present = false;
        s.boolean(present);
        if (!s.ok)
            return false;
        if (present)
            slot.emplace();
        else
            slot.reset();
        return present;
    }
    template <class T> bool Present(WriteStream& s, std::optional<T>& slot) {
        bool present = slot.has_value();
        s.boolean(present);
        return present;
    }

    template <class T> bool PresentIfRemaining(ReadStream& s, std::optional<T>& slot) {
        if (s.more()) {
            slot.emplace();
            return true;
        }
        slot.reset();
        return false;
    }
    template <class T> bool PresentIfRemaining(WriteStream&, std::optional<T>& slot) {
        return slot.has_value();
    }

    template <class P> std::vector<uint8_t> Encode(const P& packet) {
        std::vector<uint8_t> out;
        WriteStream stream(out);
        Write(stream, packet);
        return out;
    }

    template <class P> bool EncodeChecked(const P& packet, std::vector<uint8_t>& out) {
        out.clear();
        WriteStream stream(out);
        Write(stream, packet);
        return stream.ok;
    }

}
