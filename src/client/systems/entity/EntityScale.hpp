#pragma once
#include "client/systems/model/EntityTracker.hpp"
#include <utility>

namespace perlin::entity {
    inline float MetadataScale(const Entity& e) {
        return e.scale > 0.0f ? e.scale : 1.0f;
    }

    inline float ModelScale(const Entity& e, float scriptScale) {
        return (scriptScale > 0.0f ? scriptScale : 1.0f) * MetadataScale(e);
    }

    inline std::pair<float, float> PlayerBox(const Entity& e) {
        const float height = (e.Has(flag::Gliding) || e.Has(flag::Swimming) || e.crawling) ? 0.6f
                             : e.Has(flag::Sneaking)                                       ? 1.5f
                                                                                           : 1.8f;
        const float scale = MetadataScale(e);
        return {0.6f * scale, height * scale};
    }
}
