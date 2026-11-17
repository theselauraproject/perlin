#include "network/XboxRelyingParties.hpp"

#include "util/Log.hpp"

namespace perlin::xbox::rp {
    bool IsRejected(std::string_view relyingParty) {
        return relyingParty == kRejectedPlayFab;
    }

    std::string ForPlayFab(const std::string& advertisedByNsal) {
        if (!advertisedByNsal.empty() && advertisedByNsal != kPlayFab)
            Log("auth", "NSAL advertises relying party {} for PlayFab; using the pinned {}", advertisedByNsal,
                std::string(kPlayFab));
        return std::string(kPlayFab);
    }
}
