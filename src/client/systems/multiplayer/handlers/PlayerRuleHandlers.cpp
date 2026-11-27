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
            if (runtimeId == 0 || p.entityUniqueId != uniqueId)
                return;
            // SerializedAbilitiesData replaces each named layer. Its resolved
            // value comes from the highest set layer, independent of wire order.
            // A later record for the same layer replaces that entire layer,
            // including abilities omitted by its allowed mask.
            std::array<const AbilityLayers*, 6> layers{};
            AbilityLayers permissions;
            const auto permission = static_cast<unsigned>(p.permissionLevel);
            if (permission <= 2) {
                permissions.allowed = 0xffu;
                permissions.enabled = permission == 2 ? 0xffu : permission == 1 ? 0x3fu : 0u;
                layers[1] = &permissions;
            }
            for (const AbilityLayers& layer : p.abilities) {
                const auto type = static_cast<unsigned>(layer.type);
                layers[type < layers.size() ? type : 1] = &layer;
            }
            const auto source = [&](unsigned bit) -> const AbilityLayers* {
                for (size_t i = layers.size(); i-- > 0;)
                    if (layers[i] && (layers[i]->allowed & (1u << bit)))
                        return layers[i];
                return nullptr;
            };
            const auto enabled = [&](unsigned bit) {
                const auto* layer = source(bit);
                return layer && (layer->enabled & (1u << bit)) != 0;
            };
            mayFly = enabled(10);
            mayMine = enabled(1);
            instabuild = enabled(11);
            isFlying = enabled(9);
            noClip = enabled(17);
            // An unset ability resolves to the native null ability's zero
            // value; it does not retain the previous packet's speed.
            const auto* fly = source(13);
            const auto* vertical = source(19);
            const auto* walk = source(14);
            flySpeed = fly ? fly->flySpeed : 0.0f;
            vFlySpeed = vertical ? vertical->verticalFlySpeed : 0.0f;
            walkSpeed = walk ? walk->walkSpeed : 0.0f;
            ++abilitiesSeq;
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
