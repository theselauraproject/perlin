#pragma once

#include "client/player/PerlinSupport.hpp"
#include "client/player/PerlinCacheDifference.hpp"

#include <bit>
#include <vector>

namespace perlin::physics::bds {
    struct CachedBlockMetadata {
        uint64_t block = 0;
        BlockPosition position{};
    };

    struct CollisionRecord {
        CollisionBox box;
        CachedBlockMetadata metadata;
        bool preserveHorizontalMotion = false;
    };

    struct CollisionCache {
        CollisionBox region = [] {
            CollisionBox bounds;
            bounds.fill(std::bit_cast<float>(uint32_t{0xFFFFFFFF}));
            return bounds;
        }();
        std::vector<CollisionBox> shapes;
        std::vector<CachedBlockMetadata> blocks;
        bool preserveHorizontalMotion = false;
    };

    inline void AppendCollisionRecords(CollisionCache& cache, const CollisionBox& region,
                                       std::span<const CollisionRecord> records) {
        cache.shapes.reserve(cache.shapes.size() + records.size());
        cache.blocks.reserve(cache.blocks.size() + records.size());
        cache.preserveHorizontalMotion = false;
        for (const auto& record : records) {
            cache.shapes.push_back(record.box);
            cache.blocks.push_back(record.metadata.block ? record.metadata : CachedBlockMetadata{});
            cache.preserveHorizontalMotion |= record.preserveHorizontalMotion;
        }
        cache.region = region;
    }

    template <class Query>
    bool RefreshCollisionCache(CollisionCache& cache, const CollisionBox& region, Query&& query) {
        bool contained = true;
        for (int axis = 0; axis < 3; ++axis)
            contained &= region[axis] >= cache.region[axis] && !(region[axis] > cache.region[axis + 3]) &&
                         region[axis + 3] >= cache.region[axis] &&
                         !(region[axis + 3] > cache.region[axis + 3]);
        if (contained)
            return false;
        const bool full = cache.region[0] >= cache.region[3] || !(cache.region[4] > cache.region[1]) ||
                          !(cache.region[5] > cache.region[2]) || !(cache.region[3] > region[0]) ||
                          cache.region[0] >= region[3] || region[1] >= cache.region[4] ||
                          !(region[4] > cache.region[1]) || region[2] >= cache.region[5] ||
                          !(region[5] > cache.region[2]);
        const auto regions =
            full ? std::vector<CollisionBox>{region} : CacheRegionDifference(region, cache.region);
        if (regions.empty())
            return false;
        std::vector<CollisionRecord> records;
        for (const auto& part : regions) {
            auto incoming = query(part);
            records.insert(records.end(), incoming.begin(), incoming.end());
        }
        AppendCollisionRecords(cache, region, records);
        return true;
    }
}
