#pragma once
#include <algorithm>
#include <span>
#include "client/systems/text/ChatFormat.hpp"
#include "util/Env.hpp"
#include "client/systems/multiplayer/SessionPolicy.hpp"
#include "client/systems/multiplayer/BedrockSession.hpp"
#include "world/level/chunk/ChunkCodec.hpp"
#include "client/systems/multiplayer/CustomBlocks.hpp"
#include "client/systems/i18n/Lang.hpp"
#include "world/movement/VoxelShapes.hpp"
#include "client/systems/mods/ModManager.hpp"
#include "network/ClientIdentity.hpp"
#include "network/PacketDump.hpp"
#include "world/level/block/BlockPalette.hpp"
#include <atomic>
#include <cctype>
#include <chrono>
#include <variant>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include "util/Log.hpp"
#include <string>

namespace perlin::handlers {
    using namespace proto;

    using PackStatus = ResourcePackClientResponsePacketResponseStatus;

    bool SaneWorldPos(float x, float y, float z);
    bool SanePlayerMove(const MovePlayerPacket& packet);

    std::string UuidKey(const Uuid& u);

    bool MetaNumber(const MetadataDictionaryEntry& m, double& out);

    bool MetaInt(const MetadataDictionaryEntry& m, int64_t& out);

    void ApplyMetadata(entity::EntityTracker& tracker, uint64_t rid,
                       const std::vector<MetadataDictionaryEntry>& metadata);

    uint64_t MetaFlags(const std::vector<MetadataDictionaryEntry>& metadata);

    std::shared_ptr<const entity::SkinData> ConvertSkin(const Skin& s);

    template <class P> void Trace(BedrockSession& session) {
        session.OnPartial<P>([](const P& packet) { Log("trace", "{} {}", P::kName, net::Dump(packet)); });
    }

    bool EqualsIgnoreCase(const std::string& a, std::string_view b);
}
