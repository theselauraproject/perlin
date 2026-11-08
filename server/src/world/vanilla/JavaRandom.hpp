#pragma once

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

    inline Seed128 UpgradeSeedTo128(int64_t seed) {
        const uint64_t l = static_cast<uint64_t>(seed) ^ kSilver64;
        const uint64_t m = l + kGolden64;
        return {MixStafford13(l), MixStafford13(m)};
    }

    Seed128 SeedFromHashOf(std::string_view text);

    inline int32_t JavaStringHash(std::string_view text) {
        uint32_t h = 0;
        for (const unsigned char c : text)
            h = h * 31u + c;
        return static_cast<int32_t>(h);
    }

    inline int64_t PositionalSeed(int x, int y, int z) {
        const int32_t xs = static_cast<int32_t>(static_cast<uint32_t>(x) * 3129871u);
        uint64_t seed = static_cast<uint64_t>(static_cast<int64_t>(xs)) ^
                        (static_cast<uint64_t>(static_cast<int64_t>(z)) * 116129781ull) ^
                        static_cast<uint64_t>(static_cast<int64_t>(y));
        seed = seed * seed * 42317861ull + seed * 11ull;
        return static_cast<int64_t>(seed) >> 16;
    }

    class WorldRandom {
    public:
        WorldRandom(uint64_t lo, uint64_t hi) : lo(lo), hi(hi) {
            if ((lo | hi) == 0) {
                this->lo = kGolden64;
                this->hi = 7640891576956012809ull;
            }
        }
        explicit WorldRandom(const Seed128& s) : WorldRandom(s.lo, s.hi) {}
        static WorldRandom FromSeed(int64_t seed) { return WorldRandom(UpgradeSeedTo128(seed)); }
        static WorldRandom Legacy(int64_t seed) {
            WorldRandom r(1, 0);
            r.legacy = true;
            r.lo = (static_cast<uint64_t>(seed) ^ 0x5DEECE66Dull) & kLegacyMask;
            r.hi = 0;
            return r;
        }
        bool IsLegacy() const { return legacy; }

        int32_t Next(int bits) {
            lo = (lo * 0x5DEECE66Dull + 0xBull) & kLegacyMask;
            return static_cast<int32_t>(static_cast<int64_t>(lo) >> (48 - bits));
        }

        uint64_t NextLong() {
            if (legacy) {
                const int64_t upper = Next(32);
                const int64_t lower = Next(32);
                return static_cast<uint64_t>((upper << 32) + lower);
            }
            const uint64_t a = lo, b = hi;
            const uint64_t result = std::rotl(a + b, 17) + a;
            const uint64_t c = b ^ a;
            lo = std::rotl(a, 49) ^ c ^ (c << 21);
            hi = std::rotl(c, 28);
            return result;
        }
        int32_t NextInt() { return legacy ? Next(32) : static_cast<int32_t>(NextLong()); }
        int32_t NextInt(int32_t bound) {
            if (legacy) {
                if ((bound & (bound - 1)) == 0)
                    return static_cast<int32_t>((static_cast<int64_t>(bound) * Next(31)) >> 31);
                int32_t sample, modulo;
                do {
                    sample = Next(31);
                    modulo = sample % bound;
                } while (static_cast<int32_t>(static_cast<uint32_t>(sample) - static_cast<uint32_t>(modulo) +
                                              static_cast<uint32_t>(bound - 1)) < 0);
                return modulo;
            }
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
        bool NextBool() { return legacy ? Next(1) != 0 : (NextLong() & 1) != 0; }
        int32_t NextIntBetweenInclusive(int32_t a, int32_t b) { return NextInt(b - a + 1) + a; }
        double NextDouble() {
            if (legacy) {
                const int64_t upper = Next(26);
                const int64_t lower = Next(27);
                return static_cast<double>((upper << 27) + lower) * static_cast<double>(1.110223E-16f);
            }
            return static_cast<double>(NextLong() >> 11) * 1.1102230246251565E-16;
        }
        float NextFloat() {
            if (legacy)
                return static_cast<float>(Next(24)) * 5.9604645E-8f;
            return static_cast<float>(NextLong() >> 40) * 5.9604645E-8f;
        }
        void Consume(int count) {
            for (int i = 0; i < count; i++)
                NextInt();
        }

        struct Factory {
            uint64_t lo = 0, hi = 0;
            bool legacy = false;
            WorldRandom FromHashOf(std::string_view name) const {
                if (legacy)
                    return Legacy(static_cast<int64_t>(JavaStringHash(name)) ^ static_cast<int64_t>(lo));
                const Seed128 h = SeedFromHashOf(name).Xor(lo, hi);
                return WorldRandom(h);
            }
            WorldRandom FromSeed(int64_t seed) const {
                if (legacy)
                    return Legacy(seed);
                return WorldRandom(static_cast<uint64_t>(seed) ^ lo, static_cast<uint64_t>(seed) ^ hi);
            }
            WorldRandom At(int x, int y, int z) const {
                const int64_t seed = PositionalSeed(x, y, z);
                if (legacy)
                    return Legacy(seed ^ static_cast<int64_t>(lo));
                return WorldRandom(static_cast<uint64_t>(seed) ^ lo, hi);
            }
        };
        Factory ForkPositional() {
            Factory f;
            f.legacy = legacy;
            f.lo = NextLong();
            f.hi = legacy ? 0 : NextLong();
            return f;
        }

    private:
        static constexpr uint64_t kLegacyMask = (1ull << 48) - 1;
        uint64_t lo, hi;
        bool legacy = false;
    };
}
