#pragma once
#include "util/Env.hpp"

#include <atomic>

namespace perlin::anim {
    inline std::atomic<int> gFirstPersonStyle{0};
    inline std::atomic<int> gThirdPersonStyle{0};

    inline bool DefaultClassic() {
        static const bool modern = PERLIN_ENV("PERLIN_MODERN_ANIMATIONS") != nullptr;
        return !modern;
    }

    inline bool Classic(const std::atomic<int>& style) {
        const int s = style.load(std::memory_order_relaxed);
        return s == 1 ? true : s == 2 ? false : DefaultClassic();
    }

    inline bool ClassicFirstPerson() {
        return Classic(gFirstPersonStyle);
    }

    inline bool ClassicThirdPerson() {
        return Classic(gThirdPersonStyle);
    }
}
