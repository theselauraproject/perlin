#pragma once
#include "network/XboxAuth.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace perlin::realms {

    struct Realm {
        int64_t id = 0;
        std::string name;
        std::string owner;
        std::string motd;
        std::string state;
        bool expired = false;
        bool member = false;
        int maxPlayers = 0;
    };

    struct Address {
        std::string host;
        uint16_t port = 19132;
        std::string protocol;
    };

    bool List(xbox::Account& account, std::vector<Realm>& out, std::string& error);

    bool AcceptInvite(xbox::Account& account, const std::string& codeOrLink, Realm& out, std::string& error);

    bool Join(xbox::Account& account, int64_t id, Address& out, std::string& error);

}
