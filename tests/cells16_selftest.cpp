// the paletted section storage against a plain array: byte mode, the switch to two bytes past 256 ids, copies
#include "world/level/Level.hpp"

#include <cstdio>
#include <random>
#include <vector>

using namespace perlin;

int main() {
    std::mt19937 rng(3);
    int failures = 0;
    const auto check = [&](bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
        failures += !ok;
    };
    for (int distinct : {1, 7, 200, 256, 257, 900}) {
        std::vector<uint16_t> ref(4096);
        for (auto& v : ref)
            v = static_cast<uint16_t>(rng() % distinct * 37);
        Cells16 c;
        c.Load(ref.data());
        bool same = true;
        for (int i = 0; i < 4096; i++)
            same &= c.Get(i) == ref[static_cast<size_t>(i)];
        for (int k = 0; k < 3000; k++) {
            const int cell = static_cast<int>(rng() % 4096);
            const uint16_t v = static_cast<uint16_t>(rng() % 400 * 13);
            c.Set(cell, v);
            ref[static_cast<size_t>(cell)] = v;
        }
        const Cells16 copy = c.Clone();
        std::vector<uint16_t> out(4096);
        copy.CopyOut(0, 4096, out.data());
        same &= out == ref;
        std::vector<uint16_t> run(16);
        copy.CopyOut(1000, 16, run.data());
        same &= std::equal(run.begin(), run.end(), ref.begin() + 1000);
        char what[64];
        std::snprintf(what, sizeof what, "%d distinct ids round-trip (%zu bytes)", distinct, c.Bytes());
        check(same, what);
    }
    Cells16 small;
    std::vector<uint16_t> two(4096, 5);
    two[10] = 9;
    small.Load(two.data());
    check(small.Bytes() < 4200, "a section of two ids takes one byte per cell");
    check(small.AllZero(0, 4096) == false && small.Get(10) == 9, "reads after load");
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
