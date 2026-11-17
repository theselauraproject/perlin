#include "client/systems/multiplayer/handlers/HandlerSupport.hpp"

namespace perlin {
    using namespace proto;
    using namespace handlers;

    void BedrockSession::RegisterPacketHandlers() {
        InstallPersistentChunkResolver();
        RegisterServerUiHandlers();
        RegisterWorldExtraHandlers();
        RegisterProtocolExtras();
        RegisterIgnoredHandlers();
        dispatcher.OnFailure([](uint32_t id, const char* name, size_t size) {
            Log("net", "failed to decode {} (id {}, {} bytes)", name, id, size);
        });
        RegisterSessionHandlers();
        RegisterWorldStateHandlers();
        RegisterLevelHandlers();
        RegisterLocalPlayerHandlers();
        RegisterEntityLifecycleHandlers();
        RegisterEntityStateHandlers();
        RegisterPlayerRuleHandlers();

        if (PERLIN_ENV("PERLIN_TRACE")) {
            Trace<StartGamePacket>(*this);
            Trace<UpdateAbilitiesPacket>(*this);
            Trace<TextPacket>(*this);
            Trace<CommandOutputPacket>(*this);
            Trace<MovePlayerPacket>(*this);
        }

        On<LevelChunkPacket>([this](const LevelChunkPacket& p) { HandleLevelChunk(p); });
        On<SubchunkPacket>([this](const SubchunkPacket& p) { HandleSubChunk(p); });
        On<ClientCacheMissResponsePacket>([this](const ClientCacheMissResponsePacket& p) { HandleCacheMiss(p); });
        RegisterPlayerStateHandlers();
    }

}
