#include "client/systems/multiplayer/handlers/HandlerSupport.hpp"

namespace perlin {
    using namespace proto;
    using namespace handlers;

    void BedrockSession::RegisterPlayerRuleHandlers() {
        On<UpdateAbilitiesPacket>([this](const UpdateAbilitiesPacket& p) {
            {
                std::lock_guard<std::mutex> lk(tabMutex);
                permByUid[p.entityUniqueId] = static_cast<int>(p.permissionLevel);
                for (TabPlayer& t : tabPlayers)
                    if (t.uid == p.entityUniqueId)
                        t.perm = static_cast<int>(p.permissionLevel);
            }
            if (runtimeId != 0 && p.entityUniqueId != 0 && uniqueId != 0 && p.entityUniqueId != uniqueId)
                return;
            bool fly = false, flying = false, clip = false, build = false, mine = true;
            for (const AbilityLayers& layer : p.abilities) {
                if (layer.allowed & (1u << 1))
                    mine = (layer.enabled & (1u << 1)) != 0;
                if (layer.allowed & (1u << 10))
                    fly = (layer.enabled & (1u << 10)) != 0;
                if (layer.allowed & (1u << 11))
                    build = (layer.enabled & (1u << 11)) != 0;
                if (layer.allowed & (1u << 9))
                    flying = (layer.enabled & (1u << 9)) != 0;
                if (layer.allowed & (1u << 17))
                    clip = (layer.enabled & (1u << 17)) != 0;
                if (layer.allowed & (1u << 13))
                    flySpeed = layer.flySpeed;
                if (layer.allowed & (1u << 19))
                    vFlySpeed = layer.verticalFlySpeed;
                if (layer.allowed & (1u << 14))
                    walkSpeed = layer.walkSpeed;
            }
            mayFly = fly;
            mayMine = mine;
            instabuild = build;
            isFlying = flying;
            ++abilitiesSeq;
            noClip = clip;
            Log("bedrock", "abilities mayFly={} flying={} noclip={} fly={}", mayFly, isFlying, noClip,
                flySpeed);
        });

        On<UpdatePlayerGameTypePacket>([this](const UpdatePlayerGameTypePacket& p) {
            if (uniqueId != 0 && p.playerUniqueId != uniqueId)
                return;
            gameType = static_cast<int>(p.gamemode);
            int worldGameType;
            {
                std::lock_guard<std::mutex> lk(worldMutex);
                worldGameType = world.worldGameMode;
            }
            ApplyGameTypeAbilities(worldGameType);
        });
        On<SetDefaultGameTypePacket>([this](const SetDefaultGameTypePacket& p) {
            std::lock_guard<std::mutex> lk(worldMutex);
            world.worldGameMode = static_cast<int>(p.gamemode);
        });

        On<UpdateAdventureSettingsPacket>([this](const UpdateAdventureSettingsPacket& p) {
            std::lock_guard<std::mutex> lk(worldMutex);
            adventure = {p.noPvm, p.noMvp, p.immutableWorld, p.showNameTags, p.autoJump};
        });

        On<OnScreenTextureAnimationPacket>([this](const OnScreenTextureAnimationPacket& p) {
            std::lock_guard<std::mutex> lk(worldMutex);
            screenAnimation = {static_cast<int>(p.animationType), std::chrono::steady_clock::now()};
        });

        On<ShowCreditsPacket>([this](const ShowCreditsPacket& p) {
            if (p.status == 0 && (runtimeId == 0 || p.runtimeEntityId == runtimeId))
                creditsRequested = true;
        });

        On<SetPlayerGameTypePacket>([this](const SetPlayerGameTypePacket& p) {
            gameType = static_cast<int>(p.gamemode);
            int worldGameType;
            {
                std::lock_guard<std::mutex> lk(worldMutex);
                worldGameType = world.worldGameMode;
            }
            ApplyGameTypeAbilities(worldGameType);
        });

        On<NetworkStackLatencyPacket>([this](const NetworkStackLatencyPacket& p) {
            if (p.needsResponse)
                SendLatencyReply(p.timestamp);
        });

        On<CommandOutputPacket>([this](const CommandOutputPacket& p) {
            {
                std::lock_guard lock(commandResultMutex);
                ++commandResult.received;
                commandResult.success = p.successCount;
                commandResult.output.clear();
                for (const auto& message : p.output) {
                    commandResult.output += message.messageId;
                    for (const auto& parameter : message.parameters)
                        commandResult.output += " " + parameter;
                    commandResult.output += '\n';
                }
            }
            std::cout << "[bedrock] CommandOutput success=" << p.successCount << " msgs=" << p.output.size();
            for (size_t i = 0; i < p.output.size() && i < 4; i++) {
                std::cout << " [" << p.output[i].messageId;
                for (size_t j = 0; j < p.output[i].parameters.size() && j < 4; j++)
                    std::cout << " '" << p.output[i].parameters[j].substr(0, 40) << "'";
                std::cout << "]";
            }
            std::cout << std::endl;
        });
    }
}
