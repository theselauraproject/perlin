#pragma once
// the random number generators the Java worldgen uses, ported from the 26.3 jar: xoroshiro128++ and the positional factory that seeds every noise
#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <string_view>

namespace perlin::server::vanilla {

    struct Seed128 {
        uint64_t lo = 0, hi = 0;
        Seed128 Xor(uint64_t l, uint64_t h) const { return {lo ^ l, hi ^ h}; }
    };

    inline uint64_t MixStafford13(uint64_t z) {
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    inline constexpr uint64_t kGolden64 = 0x9E3779B97F4A7C15ull;
    inline constexpr uint64_t kSilver64 = 0x6A09E667F3BCC909ull;

    // seed to 128 bits: xor/add the two constants, then mix each half
    inline Seed128 UpgradeSeedTo128(int64_t seed) {
        const uint64_t l = static_cast<uint64_t>(seed) ^ kSilver64;
        const uint64_t m = l + kGolden64;
        return {MixStafford13(l), MixStafford13(m)};
    }

    // md5 of a string as two big-endian 64 bit halves (the way the game turns a name into a seed)
    Seed128 SeedFromHashOf(std::string_view text);

    class Xoroshiro {
    public:
        Xoroshiro(uint64_t lo, uint64_t hi) : lo(lo), hi(hi) {
            if ((lo | hi) == 0) {
                this->lo = kGolden64;
                this->hi = 7640891576956012809ull;
            }
        }
        explicit Xoroshiro(const Seed128& s) : Xoroshiro(s.lo, s.hi) {}
        static Xoroshiro FromSeed(int64_t seed) { return Xoroshiro(UpgradeSeedTo128(seed)); }

        uint64_t NextLong() {
            const uint64_t a = lo, b = hi;
            const uint64_t result = std::rotl(a + b, 17) + a;
            const uint64_t c = b ^ a;
            lo = std::rotl(a, 49) ^ c ^ (c << 21);
            hi = std::rotl(c, 28);
            return result;
        }
        int32_t NextInt() { return static_cast<int32_t>(NextLong()); }
        int32_t NextInt(int32_t bound) {
            uint64_t l = static_cast<uint32_t>(NextInt());
            uint64_t m = l * static_cast<uint64_t>(bound);
            uint64_t n = m & 0xFFFFFFFFull;
            if (n < static_cast<uint64_t>(bound)) {
                const uint32_t o = (~static_cast<uint32_t>(bound) + 1u) % static_cast<uint32_t>(bound);
                while (n < o) {
                    l = static_cast<uint32_t>(NextInt());
                    m = l * static_cast<uint64_t>(bound);
                    n = m & 0xFFFFFFFFull;
                }
            }
            return static_cast<int32_t>(m >> 32);
        }
        bool NextBool() { return (NextLong() & 1) != 0; }
        int32_t NextIntBetweenInclusive(int32_t a, int32_t b) { return NextInt(b - a + 1) + a; }
        double NextDouble() { return static_cast<double>(NextLong() >> 11) * 1.1102230246251565E-16; }
        float NextFloat() { return static_cast<float>(NextLong() >> 40) * 5.9604645E-8f; }

        // positional factory: two draws from this generator become the base seed of everything forked from it
        struct Factory {
            uint64_t lo = 0, hi = 0;
            Xoroshiro FromHashOf(std::string_view name) const {
                const Seed128 h = SeedFromHashOf(name).Xor(lo, hi);
                return Xoroshiro(h);
            }
            Xoroshiro FromSeed(int64_t seed) const {
                return Xoroshiro(static_cast<uint64_t>(seed) ^ lo, static_cast<uint64_t>(seed) ^ hi);
            }
            Xoroshiro At(int x, int y, int z) const {
                const int32_t xs = static_cast<int32_t>(static_cast<uint32_t>(x) * 3129871u);
                uint64_t seed = static_cast<uint64_t>(static_cast<int64_t>(xs)) ^
                                (static_cast<uint64_t>(static_cast<int64_t>(z)) * 116129781ull) ^
                                static_cast<uint64_t>(static_cast<int64_t>(y));
                seed = seed * seed * 42317861ull + seed * 11ull;
                return Xoroshiro(static_cast<uint64_t>(static_cast<int64_t>(seed) >> 16) ^ lo, hi);
            }
        };
        Factory ForkPositional() {
            Factory f;
            f.lo = NextLong();
            f.hi = NextLong();
            return f;
        }

    private:
        uint64_t lo, hi;
    };
}
