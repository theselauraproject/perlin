#pragma once
#include <cstdint>
#include <limits>

namespace perlin::server::vanilla {

    int64_t ObfuscateBiomeSeed(int64_t seed);

    struct QuartPos3 {
        int x, y, z;
    };

    inline int64_t LcgNext(int64_t rval, int64_t c) {
        const uint64_t r = static_cast<uint64_t>(rval);
        return static_cast<int64_t>(r * (r * 6364136223846793005ull + 1442695040888963407ull) + static_cast<uint64_t>(c));
    }

    inline double Fiddle(int64_t rval) {
        const int64_t shifted = rval >> 24;
        const int64_t m = ((shifted % 1024) + 1024) % 1024;
        return (static_cast<double>(m) / 1024.0 - 0.5) * 0.9;
    }

    inline double FiddledDistance(int64_t seed, int x, int y, int z, double dx, double dy, double dz) {
        int64_t r = seed;
        r = LcgNext(r, x);
        r = LcgNext(r, y);
        r = LcgNext(r, z);
        r = LcgNext(r, x);
        r = LcgNext(r, y);
        r = LcgNext(r, z);
        const double fx = Fiddle(r);
        r = LcgNext(r, seed);
        const double fy = Fiddle(r);
        r = LcgNext(r, seed);
        const double fz = Fiddle(r);
        return (dz + fz) * (dz + fz) + (dy + fy) * (dy + fy) + (dx + fx) * (dx + fx);
    }

    inline QuartPos3 ZoomedQuart(int64_t zoomSeed, int x, int y, int z) {
        const int ax = x - 2, ay = y - 2, az = z - 2;
        const int px = ax >> 2, py = ay >> 2, pz = az >> 2;
        const double fx = static_cast<double>(ax & 3) / 4.0, fy = static_cast<double>(ay & 3) / 4.0,
                     fz = static_cast<double>(az & 3) / 4.0;
        int minI = 0;
        double minDistance = std::numeric_limits<double>::infinity();
        for (int i = 0; i < 8; i++) {
            const bool xe = (i & 4) == 0, ye = (i & 2) == 0, ze = (i & 1) == 0;
            const double d = FiddledDistance(zoomSeed, xe ? px : px + 1, ye ? py : py + 1, ze ? pz : pz + 1,
                                             xe ? fx : fx - 1.0, ye ? fy : fy - 1.0, ze ? fz : fz - 1.0);
            if (minDistance > d) {
                minI = i;
                minDistance = d;
            }
        }
        return {(minI & 4) == 0 ? px : px + 1, (minI & 2) == 0 ? py : py + 1, (minI & 1) == 0 ? pz : pz + 1};
    }
}
