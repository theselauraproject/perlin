#include "network/PacketDispatcher.hpp"
#include "network/Uuid.hpp"
#include "protocol/Packets.hpp"

#include <cstdio>
#include <string>

namespace perlin::proto2225 {
    int RoundTripAll(std::string& failures, int& skipped);
}

using namespace perlin;
using namespace perlin::proto;

static_assert(FlagsMetadataFlags2::LayingDown == (uint64_t{1} << 0));
static_assert(FlagsMetadataFlags2::InScaffolding == (uint64_t{1} << 5));
static_assert(FlagsMetadataFlags2::OverScaffolding == (uint64_t{1} << 6));
static_assert(FlagsMetadataFlags2::FallThroughScaffolding == (uint64_t{1} << 7));
static_assert(FlagsMetadataFlags2::Sleeping == (uint64_t{1} << 12));
static_assert(FlagsMetadataFlags2::PushTowardsClosestSpace == (uint64_t{1} << 45));

static int failures = 0;
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                          \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

template <class P> static bool Decode(const std::vector<uint8_t>& body, P& packet) {
    net::ReadStream stream(body.data(), body.size());
    return Read(stream, packet);
}

int main() {
    {
        SetTimePacket p;
        p.time = -5000;
        const std::vector<uint8_t> body = net::Encode(p);
        CHECK((body == std::vector<uint8_t>{0x8F, 0x4E}));
        SetTimePacket q;
        CHECK(Decode(body, q) && q.time == -5000);
    }
    {
        CommandRequestPacket p;
        p.command = "/time query daytime";
        p.origin.uuid = net::RandomUuidV4();
        p.origin.type = "player";
        p.version = "52";
        const std::vector<uint8_t> body = net::Encode(p);
        CHECK(body.size() == 1 + p.command.size() + (1 + 6) + 16 + 1 + 8 + 1 + (1 + 2));
        CHECK(body[0] == p.command.size());
        CHECK(body[1 + p.command.size()] == 6);
        CHECK(body.back() == '2');
        CommandRequestPacket q;
        CHECK(Decode(body, q));
        CHECK(q.command == p.command && q.origin.uuid == p.origin.uuid);
        CHECK(q.version == "52" && !q.internal && q.origin.type == "player" && q.origin.playerEntityId == 0);

        p.origin.type = "test";
        p.origin.playerEntityId = -9;
        CommandRequestPacket r;
        CHECK(Decode(net::Encode(p), r) && r.origin.playerEntityId == -9 && r.origin.type == "test");
    }
    {
        PlayerAuthInputPacket p;
        p.inputData = {InputData::Up, InputData::BlockAction};
        p.inputMode = PlayerAuthInputPacketInputMode::Mouse;
        p.interactionModel = PlayerAuthInputPacketInteractionModel::Crosshair;
        p.tick = 5;
        const auto plain = net::Encode(p);
        CHECK(plain.size() == 4 + 4 + 12 + 8 + 4 + (1 + 2) + 1 + 1 + 1 + 8 + 1 + 12 + 5 + 8 + 12 + 8);
        PlayerAuthInputPacketBlockActionEntry e;
        e.action = Action::StartBreak;
        e.position = {1, 64, 2};
        e.face = 1;
        p.blockAction = std::vector<PlayerAuthInputPacketBlockActionEntry>{e};
        PlayerAuthInputPacket q;
        CHECK(Decode(net::Encode(p), q) && q.blockAction && q.blockAction->size() == 1 &&
              (*q.blockAction)[0].position.y == 64 && q.inputData.size() == 2 && q.tick == 5);
    }
    {
        TextPacket p;
        p.category = TextPacketCategory::Authored;
        p.type = TextPacketType::Chat;
        p.sourceName = "Steve";
        p.message = "hello";
        const std::vector<uint8_t> body = net::Encode(p);
        CHECK(body[0] == 0 && body[1] == 1 && body[2] == 1);
        TextPacket q;
        CHECK(Decode(body, q) && q.sourceName == "Steve" && q.message == "hello");

        p.category = TextPacketCategory::Parameters;
        p.type = TextPacketType::Translation;
        p.parameters = {"a", "b"};
        TextPacket r;
        CHECK(Decode(net::Encode(p), r) && r.parameters.size() == 2 && r.parameters[1] == "b");
    }
    {
        UpdateAbilitiesPacket p;
        p.entityUniqueId = 77;
        AbilityLayers layer;
        layer.type = AbilityLayersType::Base;
        layer.allowed = 1u << 10;
        layer.enabled = 1u << 10;
        layer.flySpeed = 0.05f;
        p.abilities.push_back(layer);
        UpdateAbilitiesPacket q;
        CHECK(Decode(net::Encode(p), q) && q.entityUniqueId == 77 && q.abilities.size() == 1);
        CHECK(q.abilities[0].allowed == (1u << 10) && q.abilities[0].flySpeed == 0.05f);
    }
    {
        PlayerActionPacket p;
        p.runtimeEntityId = 3;
        p.position = {10, -64, -20};
        PlayerActionPacket q;
        CHECK(Decode(net::Encode(p), q) && q.position.x == 10 && static_cast<int32_t>(q.position.y) == -64 &&
              q.position.z == -20);
    }
    {

        const std::vector<uint8_t> wire{0xa8, 0xf8, 0xd8, 0xbd, 0xfe, 0xa8, 0xca, 0x6d, 0x00, 0xc0, 0x9c,
                                        0x44, 0x71, 0x3d, 0xdb, 0x42, 0x00, 0x00, 0x63, 0x44, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x00, 0xb4, 0x42, 0x00, 0x00, 0xb4, 0x42, 0x02,
                                        0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x63};
        MovePlayerPacket captured;
        CHECK(Decode(wire, captured) && captured.runtimeId == 61688408706530344ULL &&
              captured.position.x == 1254.0f && captured.teleport && captured.tick == 99);
        CHECK(net::Encode(captured) == wire);
        TakeItemEntityPacket pickup;
        pickup.runtimeEntityId = 7;
        pickup.target = 61688408706530344ULL;
        const std::vector<uint8_t> pickupWire{7, 0xa8, 0xf8, 0xd8, 0xbd, 0xfe, 0xa8, 0xca, 0x6d};
        CHECK(net::Encode(pickup) == pickupWire);
        TakeItemEntityPacket received;
        CHECK(Decode(pickupWire, received) && received.target == pickup.target);
        MovePlayerPacket p;
        p.mode = MovePlayerPacketMode::Teleport;
        p.teleport = MovePlayerPacketTeleport{MovePlayerPacketTeleportCause::Command, LegacyEntityType{}};
        p.tick = 99;
        MovePlayerPacket q;
        CHECK(Decode(net::Encode(p), q) && q.teleport &&
              q.teleport->cause == MovePlayerPacketTeleportCause::Command && q.tick == 99);

        p.mode = MovePlayerPacketMode::Normal;
        p.teleport.reset();
        MovePlayerPacket r;
        CHECK(Decode(net::Encode(p), r) && !r.teleport && r.tick == 99);
    }
    {
        ResourcePackClientResponsePacket p;
        p.responseStatus = ResourcePackClientResponsePacketResponseStatus::Completed;
        p.responseStatusName = "resourcepackstackfinished";
        const std::vector<uint8_t> body = net::Encode(p);
        CHECK(body.size() == 27 && body[0] == 3 && body[1] == 25);
    }
    {
        RequestNetworkSettingsPacket p;
        p.clientProtocol = 2225;
        CHECK((net::Encode(p) == std::vector<uint8_t>{0, 0, 0x08, 0xB1}));
        NetworkSettingsPacket ns;
        CHECK(Decode({1, 0, 0, 0, 0, 0, 0, 0, 0, 0}, ns) && ns.compressionThreshold == 1);
    }
    {
        net::PacketDispatcher dispatcher;
        int calls = 0;
        int32_t seen = 0;
        dispatcher.On<SetTimePacket>([&](const SetTimePacket& p) {
            calls++;
            seen = p.time;
        });
        dispatcher.On<SetTimePacket>([&](const SetTimePacket&) { calls++; });
        SetTimePacket p;
        p.time = 12000;
        const std::vector<uint8_t> body = net::Encode(p);
        CHECK(dispatcher.Dispatch(SetTimePacket::kId, body.data(), body.size()) ==
              net::PacketDispatcher::Result::Handled);
        CHECK(calls == 2 && seen == 12000);
        CHECK(dispatcher.Dispatch(SetTimePacket::kId, body.data(), 0) ==
              net::PacketDispatcher::Result::Malformed);
        CHECK(dispatcher.Dispatch(9999, body.data(), body.size()) ==
              net::PacketDispatcher::Result::Unhandled);
        CHECK(calls == 2);
    }
    {
        MobEquipmentPacket air;
        air.runtimeEntityId = 1;
        air.windowId = static_cast<WindowID>(0);
        std::vector<uint8_t> body;
        CHECK(net::EncodeChecked(air, body));
        CHECK((body ==
               std::vector<uint8_t>{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));

        MobEquipmentPacket stick;
        stick.runtimeEntityId = 1;
        stick.item.networkId = 5;
        stick.item.count = 3;
        stick.item.blockRuntimeId = 0;
        stick.slot = 2;
        stick.selectedSlot = 2;
        std::vector<uint8_t> encoded;
        CHECK(net::EncodeChecked(stick, encoded));
        const std::vector<uint8_t> expected{0x01, 0x05, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00,
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00};
        CHECK(encoded == expected);
        MobEquipmentPacket back;
        CHECK(Decode(encoded, back) && back.item.networkId == 5 && back.item.count == 3 && back.slot == 2);

        stick.item.stackId = 9;
        stick.item.hasStackId = true;
        std::vector<uint8_t> withStack;
        CHECK(net::EncodeChecked(stick, withStack));
        MobEquipmentPacket backStack;
        CHECK(Decode(withStack, backStack) && backStack.item.hasStackId && backStack.item.stackId &&
              *backStack.item.stackId == 9);
    }
    {
        std::string failed;
        int skipped = 0;
        CHECK(RoundTripAll(failed, skipped) == 0);
        if (!failed.empty())
            std::printf("round trip failures:\n%s", failed.c_str());
    }
    CHECK(std::string(PacketName(SetTimePacket::kId)) == "set_time");
    CHECK(std::string(PacketName(LevelChunkPacket::kId)) == "level_chunk");

    {
        net::context::SetProtocol(944);
        ResourcePackClientResponsePacket p;
        p.responseStatus = ResourcePackClientResponsePacketResponseStatus::HaveAllPacks;
        p.responseStatusName = "downloadingfinished";
        CHECK((net::Encode(p) == std::vector<uint8_t>{0x03, 0x00, 0x00}));
        p.responseStatus = ResourcePackClientResponsePacketResponseStatus::Completed;
        CHECK((net::Encode(p) == std::vector<uint8_t>{0x04, 0x00, 0x00}));
        ResourcePackClientResponsePacket q;
        CHECK(Decode(std::vector<uint8_t>{0x03, 0x00, 0x00}, q) &&
              q.responseStatus == ResourcePackClientResponsePacketResponseStatus::HaveAllPacks);

        PlayerAuthInputPacket in;
        in.inputData = {InputData::Up, InputData::Sneaking, InputData::BlockAction};
        PlayerAuthInputPacketBlockActionEntry e;
        e.action = Action::StartBreak;
        e.position = {1, 64, 2};
        e.face = 1;
        in.blockAction = std::vector<PlayerAuthInputPacketBlockActionEntry>{e};
        in.tick = 7;
        PlayerAuthInputPacket out;
        CHECK(Decode(net::Encode(in), out));
        CHECK(out.tick == 7 && out.inputData.size() == 3 && out.blockAction && out.blockAction->size() == 1 &&
              (*out.blockAction)[0].position.y == 64);
        net::context::SetProtocol(0);
    }
    for (const int version : {0, 944, 975, 1001, 2168, 2193}) {

        net::context::SetProtocol(version);
        const std::vector<uint8_t> wire{0x05, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10, 0x40, 0x41};
        MoveEntityPacket move;
        CHECK(Decode(wire, move) && move.rotation.pitch == 0x10 && move.rotation.yaw == 0x40 &&
              move.rotation.headYaw == 0x41);
    }
    for (const int version : {944, 975, 1001, 2168, 2193}) {
        net::context::SetProtocol(version);
        std::string failed;
        int skipped = 0;
        CHECK(RoundTripAll(failed, skipped) == 0);
        if (!failed.empty())
            std::printf("round trip failures at %d:\n%s", version, failed.c_str());
    }
    net::context::SetProtocol(0);

    std::printf(failures ? "%d FAILED\n" : "packet self-tests passed\n", failures);
    return failures ? 1 : 0;
}
