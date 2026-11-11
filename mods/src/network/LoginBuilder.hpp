#pragma once
#include <string>

namespace perlin {
    class ClientIdentity;

    std::string BuildIdentityJson(ClientIdentity& identity, const std::string* onlineChainJson,
                                  std::string& error, const std::string& displayName = {});
}
