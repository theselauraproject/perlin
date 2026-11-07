#include "network/Stream.hpp"

namespace perlin::net {
    namespace {
        enum Tag : uint8_t {
            End,
            Byte,
            Short,
            Int,
            Long,
            Float,
            Double,
            ByteArray,
            String,
            List,
            Compound,
            IntArray,
            LongArray
        };

        constexpr int kMaxDepth = 64;

        class Walker {
        public:
            Walker(ReadStream& s, bool little) : s(s), little(little) {}

            bool Root() {
                uint8_t type = 0;
                s.u8(type);
                if (!s.ok)
                    return false;
                if (type == End)
                    return true;
                SkipString();
                return Payload(type, 0);
            }

        private:
            void SkipBytes(uint64_t n) {
                if (!s.length(n))
                    return;
                s.slice(static_cast<size_t>(n));
            }

            int64_t ReadLength() {
                if (little) {
                    int32_t n = 0;
                    s.le(n);
                    return n;
                }
                int32_t n = 0;
                s.zigzag(n);
                return n;
            }

            void SkipString() {
                if (little) {
                    uint16_t n = 0;
                    s.le(n);
                    SkipBytes(n);
                    return;
                }
                uint32_t n = 0;
                s.varint(n);
                SkipBytes(n);
            }

            void SkipInt() {
                if (little) {
                    SkipBytes(4);
                    return;
                }
                int32_t v = 0;
                s.zigzag(v);
            }

            void SkipLong() {
                if (little) {
                    SkipBytes(8);
                    return;
                }
                int64_t v = 0;
                s.zigzag(v);
            }

            bool Payload(uint8_t type, int depth) {
                if (depth > kMaxDepth) {
                    s.ok = false;
                    return false;
                }
                switch (type) {
                case Byte:
                    SkipBytes(1);
                    break;
                case Short:
                    SkipBytes(2);
                    break;
                case Int:
                    SkipInt();
                    break;
                case Long:
                    SkipLong();
                    break;
                case Float:
                    SkipBytes(4);
                    break;
                case Double:
                    SkipBytes(8);
                    break;
                case ByteArray: {
                    const int64_t n = ReadLength();
                    if (n < 0) {
                        s.ok = false;
                        break;
                    }
                    SkipBytes(static_cast<uint64_t>(n));
                    break;
                }
                case String:
                    SkipString();
                    break;
                case List: {
                    uint8_t element = 0;
                    s.u8(element);
                    const int64_t n = ReadLength();
                    if (n < 0 || (n > 0 && element == End)) {
                        s.ok = false;
                        break;
                    }
                    for (int64_t i = 0; i < n && s.ok; i++)
                        Payload(element, depth + 1);
                    break;
                }
                case Compound:
                    while (s.ok) {
                        uint8_t child = 0;
                        s.u8(child);
                        if (!s.ok || child == End)
                            break;
                        SkipString();
                        Payload(child, depth + 1);
                    }
                    break;
                case IntArray:
                case LongArray: {
                    const int64_t n = ReadLength();
                    if (n < 0) {
                        s.ok = false;
                        break;
                    }
                    for (int64_t i = 0; i < n && s.ok; i++)
                        type == IntArray ? SkipInt() : SkipLong();
                    break;
                }
                default:
                    s.ok = false;
                    break;
                }
                return s.ok;
            }

            ReadStream& s;
            bool little;
        };
    }

    void ReadStream::nbt(Nbt& v, bool little) {
        const uint8_t* begin = cursor;
        Walker(*this, little).Root();
        if (!ok)
            return;
        v.bytes.assign(begin, cursor);
    }
}
