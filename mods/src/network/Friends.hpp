#pragma once
#include "network/XboxAuth.hpp"

#include <string>
#include <vector>

namespace perlin::friends {

    struct Friend {
        std::string xuid;
        std::string gamertag;
        bool online = false;
        bool playingMinecraft = false;
        std::string pictureUrl;
        bool hosting = false;
        std::string worldName;
        int players = 0, maxPlayers = 0;
        std::string presence;
    };

    bool List(xbox::Account& account, std::vector<Friend>& out, std::string& error);

}
