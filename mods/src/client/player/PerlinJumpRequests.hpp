#pragma once

#include "client/player/PerlinEntityMembership.hpp"

namespace perlin::physics::bds {
    inline bool ScaffoldingAllowsJump(bool inside, bool over, bool descending) {
        return (!(inside || over)) || !descending;
    }

    template <class CanStandOnSnow, class IsLightweight>
    bool GroundJumpAllowed(uint64_t actorUpperFlags, bool powderSnow, CanStandOnSnow&& canStandOnSnow,
                           IsLightweight&& isLightweight) {
        bool blockedBySnow = false;
        if (powderSnow && !canStandOnSnow())
            blockedBySnow = !isLightweight();
        return ScaffoldingAllowsJump(actorUpperFlags & 0x20, actorUpperFlags & 0x40,
                                     actorUpperFlags & 0x80) &&
               !blockedBySnow;
    }

    enum class GroundJumpRequest : uint32_t {
        LocalPlayer = 0x3B2DFE4A,
        Other = 0x9D529862,
        Squid = 0x3B31D80A,
        LavaSlime = 0x83B7F546,
    };

    inline GroundJumpRequest SelectGroundJumpRequest(uint32_t entity, const EntityMembership& localPlayers,
                                                     const EntityMembership& lavaSlimes,
                                                     const EntityMembership& squids) {
        if (localPlayers.Contains(entity))
            return GroundJumpRequest::LocalPlayer;
        if (lavaSlimes.Contains(entity))
            return GroundJumpRequest::LavaSlime;
        if (squids.Contains(entity))
            return GroundJumpRequest::Squid;
        return GroundJumpRequest::Other;
    }

    template <class Lookup, class Create>
    void ClassifyGroundJump(uint32_t entity, const EntityMembership& localPlayers,
                            const EntityMembership& lavaSlimes, const EntityMembership& squids,
                            Lookup&& lookup, Create&& create) {
        const auto request = SelectGroundJumpRequest(entity, localPlayers, lavaSlimes, squids);
        if (!lookup(request).Contains(entity))
            create(request, entity);
    }
}
