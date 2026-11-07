// the c api an embedding host uses: create, start on its own thread, loopback connection, stop
#include "test_util.hpp"

#include "perlin/server.h"

#include "net/Batch.hpp"
#include "protocol/Packets.hpp"

#include <chrono>
#include <thread>

using namespace perlin::server;
namespace proto = perlin::proto2225;

int main() {
    CHECK(ps_abi_version() == 1);
    ps_options options{};
    options.loopback_only = 1;
    options.data_dir = "capi_test_data";
    options.config_json = "{\"generator\":\"flat\",\"terrain_backend\":\"cpu\"}";
    ps_server* server = ps_create(&options);
    CHECK(server != nullptr);
    if (!server)
        return 1;
    CHECK(ps_start(server) == 0);
    CHECK(ps_run_async(server) == 0);

    ps_conn* conn = ps_connect_loopback(server);
    CHECK(conn != nullptr);
    perlin::proto2225::RequestNetworkSettingsPacket req;
    req.clientProtocol = 2225;
    std::vector<uint8_t> body, content;
    perlin::net::EncodeChecked(req, body);
    BatchCodec::AppendPacket(content, proto::RequestNetworkSettingsPacket::kId, body);
    CHECK(ps_conn_send(conn, content.data(), content.size()) == 1);

    std::vector<uint8_t> buf(64);
    size_t size = 0;
    int r = 0;
    for (int i = 0; i < 200 && r == 0; i++) {
        r = ps_conn_receive(conn, buf.data(), buf.size(), &size);
        if (r == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(r == 1);
    BatchCodec plain;
    std::vector<uint8_t> out;
    CHECK(plain.Open(std::span<const uint8_t>(buf.data(), size), out));
    uint32_t seen = 0;
    BatchCodec::Split(out, [&](uint32_t id, std::span<const uint8_t>) { seen = id; });
    CHECK(seen == proto::NetworkSettingsPacket::kId);

    // a message bigger than the caller's buffer is reported and kept for the next call
    ps_conn* second = ps_connect_loopback(server);
    CHECK(ps_conn_send(second, content.data(), content.size()) == 1);
    std::vector<uint8_t> tiny(1);
    r = 0;
    for (int i = 0; i < 200 && r == 0; i++) {
        r = ps_conn_receive(second, tiny.data(), tiny.size(), &size);
        if (r == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(r == -1 && size > 1);
    std::vector<uint8_t> big(size);
    CHECK(ps_conn_receive(second, big.data(), big.size(), &size) == 1 && size == big.size());
    ps_conn_close(second);
    CHECK(ps_conn_alive(conn) == 1);
    ps_conn_close(conn);
    ps_destroy(server);
    if (g_failed) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::puts("server_capi_test ok");
    return 0;
}
