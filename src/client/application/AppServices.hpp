#pragma once
#include "client/services/account/AccountService.hpp"
#include "client/services/discord/DiscordService.hpp"

#include <memory>

namespace perlin {
    class AppServices {
    public:
        std::unique_ptr<services::AccountService> accounts;
        std::unique_ptr<services::DiscordService> discord;

        void Stop() {
            if (discord)
                discord->Stop();
            if (accounts)
                accounts->Stop();
        }
    };
}
