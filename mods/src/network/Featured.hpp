#pragma once
#include "network/XboxAuth.hpp"

#include <string>
#include <vector>

namespace perlin::featured {

    struct Server {
        std::string name;
        std::string description;
        std::string host;
        uint16_t port = 19132;
        std::string logoUrl;
        bool ranked = false;
        int rank = 0;
    };

    bool List(xbox::Account& account, std::vector<Server>& out, std::string& error);

}
