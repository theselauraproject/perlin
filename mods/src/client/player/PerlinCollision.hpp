#pragma once

#include "client/player/PerlinPhysicsNumerics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace perlin::physics::bds {
    using CollisionVector = std::array<float, 3>;
    using CollisionBox = std::array<float, 6>;

    struct Contact {
        float penetration = 0;
        CollisionVector clipped{};
        CollisionVector recovered{};
        int axis = 0;
    };

    inline Contact Collide(const CollisionBox& stationary, const CollisionBox& moving,
                           CollisionVector velocity) {
        Contact result{0, velocity, velocity, 0};
        for (int axis = 0; axis < 3; ++axis)
            if (stationary[axis + 3] <= stationary[axis])
                return result;

        CollisionVector depths{}, signedDepths{}, normals{};
        int separated = 0, separatingAxis = 0;
        for (int axis = 0; axis < 3; ++axis) {
            float lower = moving[axis + 3] - stationary[axis];
            float upper = stationary[axis + 3] - moving[axis];
            if (std::fabs(lower) <= 1.0e-6f)
                lower = 0;
            if (std::fabs(upper) <= 1.0e-6f)
                upper = 0;
            const float positiveLower = std::max(lower, 0.0f);
            const float positiveUpper = std::max(upper, 0.0f);
            if (positiveLower == 0) {
                normals[axis] = -1;
                signedDepths[axis] = lower;
                ++separated;
                separatingAxis = axis;
            } else if (positiveUpper == 0) {
                normals[axis] = 1;
                signedDepths[axis] = upper;
                ++separated;
                separatingAxis = axis;
            } else if (positiveLower < positiveUpper) {
                normals[axis] = -1;
                signedDepths[axis] = depths[axis] = positiveLower;
            } else {
                normals[axis] = 1;
                signedDepths[axis] = depths[axis] = positiveUpper;
            }
            if (separated > 1)
                return result;
        }
        if (separated == 0) {
            int axis = depths[0] > depths[1] ? 1 : 0;
            if (depths[2] < depths[axis])
                axis = 2;
            result.axis = axis;
            result.penetration = std::min(std::min(depths[0], depths[1]), depths[2]);
            const float displacement = normals[axis] * depths[axis];
            result.recovered[axis] = displacement > 0 ? std::max(velocity[axis], displacement)
                                                      : std::min(velocity[axis], displacement);
        } else if (signedDepths[separatingAxis] - normals[separatingAxis] * velocity[separatingAxis] > 0) {
            result.clipped[separatingAxis] = result.recovered[separatingAxis] =
                normals[separatingAxis] * signedDepths[separatingAxis];
        }
        return result;
    }

    struct CollisionMove {
        CollisionVector displacement{};
        float penetration = 0;
        CollisionBox box{};
    };

    struct MovementSearch {
        CollisionVector movement{};
        CollisionBox region{};
    };

    inline MovementSearch SearchMovement(CollisionBox box, CollisionVector movement, float stepHeight) {
        const float lengthSquared = detail::Round(detail::Round(movement[1] * movement[1]) +
                                                  detail::Round(movement[0] * movement[0])) +
                                    detail::Round(movement[2] * movement[2]);
        // MoveCollisionSystem 0x14360a290: divides by the length (divss/divps), no reciprocal
        if (lengthSquared > 256.0f) {
            const float length = std::sqrt(lengthSquared);
            for (auto& value : movement)
                value = detail::Round(value / length) * 16.0f;
        }
        const auto expand = [](CollisionBox bounds, CollisionVector delta) {
            for (int axis = 0; axis < 3; ++axis)
                if (delta[axis] < 0)
                    bounds[axis] += delta[axis];
                else if (delta[axis] > 0)
                    bounds[axis + 3] += delta[axis];
            return bounds;
        };
        const auto swept = expand(box, movement);
        const auto raised = expand(box, {0, stepHeight, 0});
        const auto combined = expand(box, {movement[0], stepHeight + movement[1], movement[2]});
        auto inset = box;
        for (int axis : {0, 2}) {
            inset[axis] += 0.025f;
            inset[axis + 3] -= 0.025f;
        }
        for (int axis = 0; axis < 3; ++axis)
            if (inset[axis] > inset[axis + 3])
                inset[axis] = inset[axis + 3] = (box[axis] + box[axis + 3]) * 0.5f;
        CollisionBox edge;
        for (int axis : {0, 2}) {
            edge[axis] = std::min(std::min(inset[axis] + movement[axis], inset[axis]), swept[axis]);
            edge[axis + 3] =
                std::max(std::max(inset[axis + 3] + movement[axis], inset[axis + 3]), swept[axis + 3]);
        }
        const float drop = detail::Round(stepHeight * -1.01f);
        edge[1] = std::min(inset[1] + drop, swept[1]);
        edge[4] = std::max(inset[4] + drop, swept[4]);
        CollisionBox region;
        for (int axis = 0; axis < 3; ++axis) {
            region[axis] =
                std::min(std::min(std::min(raised[axis], swept[axis]), combined[axis]), edge[axis]);
            region[axis + 3] = std::max(
                std::max(std::max(raised[axis + 3], swept[axis + 3]), combined[axis + 3]), edge[axis + 3]);
        }
        region[0] = std::min(region[0], box[0]);
        region[2] = std::min(region[2], box[2]);
        region[3] = std::max(region[3], box[3]);
        region[5] = std::max(region[5], box[5]);
        region[1] = std::min(box[1] - detail::Round(std::fabs(movement[1]) + 0.2f), region[1]);
        region[4] = std::max(box[4] + 0.08f, region[4]);
        return {movement, region};
    }

    struct RecoveryState {
        uint32_t flags = 0;
        CollisionVector minimum{};
        bool hasHistory = false;
        std::optional<CollisionVector> customMinimum;
    };

    inline CollisionVector RecoveryLimits(const RecoveryState& state) {
        const bool suppress = (state.flags & 8) == 0 && (state.hasHistory || (state.flags & 0x15) != 0);
        CollisionVector result{};
        for (int axis = 0; axis < 3; ++axis) {
            result[axis] = std::max(suppress ? 0.0f : 1.0f, state.minimum[axis]);
            if (state.customMinimum)
                result[axis] = std::max(result[axis], (*state.customMinimum)[axis]);
        }
        return result;
    }

    inline void UpdateRecovery(RecoveryState& state, bool penetrated, bool pushTowardsClosestSpace) {
        state.flags = (state.flags & ~0x10u) | (pushTowardsClosestSpace ? 0x10u : 0u);
        if (penetrated)
            state.flags |= (state.flags & 2) != 0 ? 4u : 2u;
        else
            state.flags &= 0x19;
        state.customMinimum.reset();
    }

    inline CollisionMove ResolveMoves(CollisionBox box, std::span<const CollisionVector> moves,
                                      std::span<const CollisionBox> shapes, CollisionVector recoveryLimits) {
        CollisionMove result{{}, 0, box};
        for (auto movement : moves) {
            for (auto shape = shapes.rbegin(); shape != shapes.rend(); ++shape) {
                const auto contact = Collide(*shape, result.box, movement);
                movement =
                    contact.penetration > recoveryLimits[contact.axis] ? contact.clipped : contact.recovered;
                result.penetration += contact.penetration;
            }
            for (int axis = 0; axis < 3; ++axis) {
                result.box[axis] = movement[axis] + result.box[axis];
                result.box[axis + 3] = movement[axis] + result.box[axis + 3];
                result.displacement[axis] = movement[axis] + result.displacement[axis];
            }
        }
        return result;
    }

    inline bool ShouldAutoStep(float height, CollisionVector requested, CollisionVector resolved,
                               bool onGround, bool flyingAbility, bool& oneShotRequest) {
        const bool requestedStep = oneShotRequest;
        oneShotRequest = false;
        return height > 0 && (requested[0] != resolved[0] || requested[2] != resolved[2]) &&
               (onGround || flyingAbility || requestedStep ||
                (requested[1] < 0 && requested[1] != resolved[1]));
    }

    inline bool TryAutoStep(const CollisionBox& start, CollisionVector requested, float height,
                            std::span<const CollisionBox> shapes, CollisionVector recoveryLimits,
                            CollisionMove& normal) {
        std::vector<CollisionBox> stepShapes;
        stepShapes.reserve(shapes.size());
        for (const auto& shape : shapes)
            if (shape[1] < start[4])
                stepShapes.push_back(shape);
        const std::array<CollisionVector, 3> moves{
            {{0, height, 0}, {requested[0], 0, 0}, {0, 0, requested[2]}}};
        const auto up = ResolveMoves(start, moves, stepShapes, recoveryLimits);
        const std::array<CollisionVector, 1> downward{{{0, -up.displacement[1], 0}}};
        const auto down = ResolveMoves(up.box, downward, stepShapes, recoveryLimits);
        for (const auto& shape : shapes)
            if (down.box[0] < shape[3] && down.box[3] > shape[0] && down.box[1] < shape[4] &&
                down.box[4] > shape[1] && down.box[2] < shape[5] && down.box[5] > shape[2])
                return false;
        CollisionVector displacement{};
        for (int axis = 0; axis < 3; ++axis)
            displacement[axis] = up.displacement[axis] + down.displacement[axis];
        const float before = detail::Round(normal.displacement[2] * normal.displacement[2]) +
                             detail::Round(normal.displacement[0] * normal.displacement[0]);
        const float after = detail::Round(displacement[0] * displacement[0]) +
                            detail::Round(displacement[2] * displacement[2]);
        if (!(before < after))
            return false;
        normal.box = down.box;
        normal.displacement = displacement;
        return true;
    }
}
