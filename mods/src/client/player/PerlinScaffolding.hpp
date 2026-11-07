#pragma once

#include "client/player/PerlinPhysicsNumerics.hpp"
#include "client/player/PerlinInsideQuery.hpp"
#include <algorithm>
#include <bit>

namespace perlin::physics::bds {
    enum class ScaffoldingCell { Other, Scaffold, Air, Water, PowderSnow };
    struct ScaffoldingContacts {
        bool inside = false, over = false;
        bool ascendable = false, descendable = false;
        bool overPowderSnow = false;
    };

    template <class Read>
    ScaffoldingContacts SenseScaffolding(const CollisionBox& body, Read&& read, bool canStandOnSnow = false) {
        ScaffoldingContacts result;
        const int32_t minX = FloorBlockCoordinate(std::min(body[0], body[3]));
        const int32_t maxX = FloorBlockCoordinate(std::max(body[0], body[3]));
        const int32_t minZ = FloorBlockCoordinate(std::min(body[2], body[5]));
        const int32_t maxZ = FloorBlockCoordinate(std::max(body[2], body[5]));
        const auto sample = [&](int32_t y, bool& contact, bool& ascendable, bool* selectedPowder = nullptr) {
            for (int32_t x = minX;; x = detail::IncrementInsideCoordinate(x)) {
                for (int32_t z = minZ;; z = detail::IncrementInsideCoordinate(z)) {
                    const auto cell = read(x, y, z);
                    bool eligible = false;
                    if (cell == ScaffoldingCell::Scaffold) {
                        contact = true;
                        const auto below = read(x, std::bit_cast<int32_t>(static_cast<uint32_t>(y) - 1u), z);
                        eligible = below != ScaffoldingCell::Air && below != ScaffoldingCell::Water;
                    }
                    if (cell == ScaffoldingCell::PowderSnow && canStandOnSnow)
                        eligible = true;
                    if (eligible && !ascendable && selectedPowder)
                        *selectedPowder = cell == ScaffoldingCell::PowderSnow;
                    ascendable |= eligible;
                    if (z == maxZ)
                        break;
                }
                if (x == maxX)
                    break;
            }
        };
        sample(FloorBlockCoordinate(body[1]), result.inside, result.ascendable);
        sample(FloorBlockCoordinate(detail::Round(body[1] - 1.0f)), result.over, result.descendable,
               &result.overPowderSnow);
        return result;
    }

    inline bool ScaffoldingSupports(const CollisionBox& actor, const BlockPosition& block, int stability,
                                    bool fallThrough) {
        const auto next = [](int32_t value) {
            return std::bit_cast<int32_t>(static_cast<uint32_t>(value) + 1u);
        };
        return stability != 7 && !fallThrough && actor[1] >= static_cast<float>(next(block[1])) - 0.000001f &&
               actor[3] > static_cast<float>(block[0]) && actor[0] < static_cast<float>(next(block[0])) &&
               actor[5] > static_cast<float>(block[2]) && actor[2] < static_cast<float>(next(block[2]));
    }
}
