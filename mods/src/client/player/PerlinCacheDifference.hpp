#pragma once

#include "client/player/PerlinCollision.hpp"

#include <limits>
#include <vector>

namespace perlin::physics::bds {
    inline std::vector<CollisionBox> CacheRegionDifference(const CollisionBox& query,
                                                           const CollisionBox& cached) {
        const CollisionVector origin{query[0], query[1], query[2]};
        const CollisionVector extent{detail::Round(query[3] - origin[0]), detail::Round(query[4] - origin[1]),
                                     detail::Round(query[5] - origin[2])};
        std::array<float, 7> planes{};
        for (int axis = 0; axis < 3; ++axis) {
            planes[axis * 2] = detail::Round(cached[axis] - origin[axis]);
            planes[axis * 2 + 1] = detail::Round(cached[axis + 3] - origin[axis]);
        }
        planes[6] = detail::Round(query[0] - origin[0]);
        const auto lowerBound = [&](int axis, float value) {
            int first = 0, count = 2;
            while (count > 0) {
                const int half = count / 2;
                if (value <= planes[axis * 2 + first + half]) {
                    count = half;
                } else {
                    first += half + 1;
                    count -= half + 1;
                }
            }
            return first;
        };
        std::vector<CollisionVector> pending{{detail::Round(query[0] - origin[0]),
                                              detail::Round(query[1] - origin[1]),
                                              detail::Round(query[2] - origin[2])}};
        std::vector<CollisionBox> candidates;
        for (size_t current = 0; current < pending.size(); ++current) {
            const auto point = pending[current];
            bool inside = true;
            CollisionBox candidate{};
            for (int axis = 0; axis < 3; ++axis) {
                const int first = lowerBound(axis, point[axis]);
                const int last = lowerBound(axis, extent[axis]);
                float end = extent[axis];
                if (first != last) {
                    end = planes[axis * 2 + first];
                    auto next = point;
                    next[axis] = detail::Round(end + 0.00001f);
                    bool duplicate = false;
                    for (const auto& previous : pending) {
                        bool equal = true;
                        for (int lane = 0; lane < 3; ++lane)
                            equal &= std::fabs(detail::Round(next[lane] - previous[lane])) <=
                                     std::numeric_limits<float>::epsilon();
                        duplicate |= equal;
                    }
                    if (!duplicate) {
                        if (pending.size() >= 27)
                            return {};
                        pending.push_back(next);
                    }
                    inside = inside && first != 0;
                } else if (last != 1) {
                    inside = false;
                }
                candidate[axis] = point[axis];
                candidate[axis + 3] = end;
            }
            if (!inside)
                candidates.push_back(candidate);
        }
        const auto volume = [](const CollisionBox& box) {
            return detail::Round(
                detail::Round(detail::Round(box[3] - box[0]) * detail::Round(box[4] - box[1])) *
                detail::Round(box[5] - box[2]));
        };
        std::vector<CollisionBox> result;
        for (auto candidate : candidates) {
            for (int axis = 0; axis < 3; ++axis) {
                const float low = std::fmin(candidate[axis], candidate[axis + 3]);
                const float high = std::fmax(candidate[axis], candidate[axis + 3]);
                candidate[axis] = low;
                candidate[axis + 3] = high;
            }
            if (!(candidate[0] < candidate[3] && candidate[1] < candidate[4] && candidate[2] < candidate[5]))
                continue;
            bool merged = false;
            for (auto& previous : result) {
                CollisionBox combined;
                for (int axis = 0; axis < 3; ++axis) {
                    combined[axis] = std::min(previous[axis], candidate[axis]);
                    combined[axis + 3] = std::max(previous[axis + 3], candidate[axis + 3]);
                }
                const float error =
                    detail::Round(volume(combined) - detail::Round(volume(previous) + volume(candidate)));
                if (std::fabs(error) <= 0.001f) {
                    previous = combined;
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                for (int axis = 0; axis < 3; ++axis) {
                    candidate[axis] = detail::Round(candidate[axis] + origin[axis]);
                    candidate[axis + 3] = detail::Round(candidate[axis + 3] + origin[axis]);
                }
                result.push_back(candidate);
            }
        }
        return result;
    }
}
