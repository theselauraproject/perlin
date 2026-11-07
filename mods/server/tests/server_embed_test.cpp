// the client's singleplayer path without the client: EmbeddedServer starts the linked-in server as a loopback-only
// world and hands out a perlin::Transport, which is what BedrockSession gets
#include "test_util.hpp"
#include "test_auth.hpp"

#include "EmbeddedServer.hpp"
#include "net/Batch.hpp"
#include "protocol/Packets.hpp"

#include <chrono>
#include <filesystem>
#include <thread>

using namespace perlin;
namespace proto = perlin::proto2225;

template <class P> static bool SendPacket(Transport& t, server::BatchCodec& codec, const P& packet) {
    std::vector<uint8_t> body, content;
    net::EncodeChecked(packet, body);
    server::BatchCodec::AppendPacket(content, P::kId, body);
    return t.SendReliable(codec.Seal(content));
}

static bool WaitFor(Transport& t, server::BatchCodec& codec, uint32_t id,
                    std::vector<std::pair<uint32_t, std::vector<uint8_t>>>& seen) {
    for (int i = 0; i < 200; i++) {
        std::vector<uint8_t> wire;
        while (t.TryReceive(wire)) {
            std::vector<uint8_t> content;
            if (!codec.Open(wire, content))
                continue;
            server::BatchCodec::Split(content, [&](uint32_t pid, std::span<const uint8_t> body) {
                seen.emplace_back(pid, std::vector<uint8_t>(body.begin(), body.end()));
                if (pid == proto::NetworkSettingsPacket::kId)
                    codec.EnableCompression(256);
            });
        }
        for (auto it = seen.begin(); it != seen.end(); ++it)
            if (it->first == id) {
                seen.erase(it);
                return true;
            }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

int main() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "perlin_embed_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    EmbeddedServer embedded;
    CHECK(!embedded.Running());
    std::string error;
    CHECK(embedded.Start(dir.string(),
                         "{\"terrain_backend\":\"cpu\",\"generator\":\"flat\",\"view_distance\":4}", error));
    if (!embedded.Running()) {
        std::fprintf(stderr, "start: %s\n", error.c_str());
        return 1;
    }

    std::unique_ptr<Transport> transport = embedded.Connect();
    CHECK(transport != nullptr);
    if (!transport)
        return 1;
    CHECK(transport->IsConnected());
    CHECK(transport->Info().protocol == kBedrockProtocol && transport->Info().version == kBedrockVersion);

    server::BatchCodec codec;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> seen;
    proto::RequestNetworkSettingsPacket req;
    req.clientProtocol = kBedrockProtocol;
    CHECK(SendPacket(*transport, codec, req));
    CHECK(WaitFor(*transport, codec, proto::NetworkSettingsPacket::kId, seen));

    testauth::Account me("Solo", "");
    proto::LoginPacket login;
    login.protocolVersion = kBedrockProtocol;
    login.tokens.identity = me.Identity(me.Chain(false), 2);
    login.tokens.client = me.ClientToken();
    CHECK(SendPacket(*transport, codec, login));
    CHECK(WaitFor(*transport, codec, proto::PlayStatusPacket::kId, seen));
    CHECK(WaitFor(*transport, codec, proto::ResourcePacksInfoPacket::kId, seen));

    proto::ResourcePackClientResponsePacket have;
    have.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::HaveAllPacks;
    CHECK(SendPacket(*transport, codec, have));
    CHECK(WaitFor(*transport, codec, proto::ResourcePackStackPacket::kId, seen));
    proto::ResourcePackClientResponsePacket done;
    done.responseStatus = proto::ResourcePackClientResponsePacketResponseStatus::Completed;
    CHECK(SendPacket(*transport, codec, done));
    CHECK(WaitFor(*transport, codec, proto::StartGamePacket::kId, seen));
    // the singleplayer player owns the world: she gets operator rights (the start game packet says so)
    CHECK(WaitFor(*transport, codec, proto::CreativeContentPacket::kId, seen));

    transport->Disconnect();
    CHECK(!transport->IsConnected());
    embedded.Stop();
    CHECK(!embedded.Running());
    std::filesystem::remove_all(dir);
    if (g_failed) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::puts("server_embed_test ok");
    return 0;
}
