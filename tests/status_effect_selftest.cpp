#include "world/movement/PerlinStatusEffect.hpp"

#include <array>
#include <cstdio>
#include <limits>

namespace {
    using perlin::physics::bds::MergeStatusEffect;
    using perlin::physics::bds::StatusEffectTicksAt;
    using Value = perlin::physics::bds::StatusEffectValue;

    struct Case {
        Value current;
        Value incoming;
        Value expected;
        const char* description;
    };

    // Literal outcomes from the native amplifier/duration comparison branches,
    // including visual fields that can change without replacing the effect.
    constexpr std::array cases{
        Case{{2, 40, false, true}, {1, 200, false, true}, {2, 40, false, true},
             "weaker effect does not extend duration"},
        Case{{2, 40, false, true}, {1, -1, false, true}, {2, 40, false, true},
             "weaker infinite effect does not replace finite stronger effect"},
        Case{{2, 40, false, true}, {2, 5, false, true}, {2, 40, false, true},
             "equal effect does not shorten duration"},
        Case{{2, 40, false, true}, {2, 60, false, true}, {2, 60, false, true},
             "equal effect extends duration"},
        Case{{2, 40, false, true}, {3, 2, false, true}, {3, 2, false, true},
             "stronger effect replaces with shorter duration"},
        Case{{2, -1, false, true}, {2, 1, false, true}, {2, -1, false, true},
             "equal finite effect cannot shorten infinite duration"},
        Case{{2, 40, false, true}, {2, -1, false, true}, {2, -1, false, true},
             "equal infinite effect replaces finite duration"},
        Case{{2, -1, false, true}, {3, 2, false, true}, {3, 2, false, true},
             "stronger finite effect replaces weaker infinite effect"},
        Case{{2, 40, true, true}, {1, 20, false, false}, {2, 40, false, false},
             "nonambient and visibility update without magnitude replacement"},
        Case{{2, 40, false, true}, {1, 20, true, false}, {2, 40, false, false},
             "ambient weaker effect cannot make an existing effect ambient"},
        Case{{2, 40, true, true}, {2, 40, false, true}, {2, 40, false, true},
             "equal duration can clear ambient"},
        Case{{2, 40, false, false}, {2, 60, true, true}, {2, 60, true, true},
             "extended duration copies ambient and particles"},
        Case{{2, 40, false, true}, {3, 0, true, false}, {3, 0, true, false},
             "merge itself does not reject zero duration"},
        Case{{-2, 40, false, true}, {-1, 2, false, true}, {-1, 2, false, true},
             "amplifier comparison is signed"},
        Case{{std::numeric_limits<int32_t>::max(), 40, false, true},
             {std::numeric_limits<int32_t>::min(), -1, false, true},
             {std::numeric_limits<int32_t>::max(), 40, false, true},
             "full32-bit amplifier comparison does not narrow or overflow"},
    };
}

int main() {
    int failures = 0;
    for (const auto& test : cases) {
        if (MergeStatusEffect(test.current, test.incoming) != test.expected) {
            std::fprintf(stderr, "FAIL: %s\n", test.description);
            ++failures;
        }
    }
    // Duration must be compared at the same simulation time, rather than using
    // the original duration from an effect that has already partially elapsed.
    const Value elapsed{2, StatusEffectTicksAt(40, 100, 130), false, true};
    if (MergeStatusEffect(elapsed, {2, 15, false, true}).duration != 15) {
        std::fprintf(stderr, "FAIL: equal amplifier extends the remaining duration\n");
        ++failures;
    }
    if (failures) {
        std::fprintf(stderr, "status_effect_selftest FAILED (%d)\n", failures);
        return 1;
    }
    std::puts("status_effect_selftest passed (16 checks)");
    return 0;
}
