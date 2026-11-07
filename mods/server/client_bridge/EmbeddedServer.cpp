#include "EmbeddedServer.hpp"

#include "perlin/server.h"

#include <atomic>
#include <memory>

namespace perlin {
    namespace {
        // one server instance shared by its transports: a transport that outlives Stop() keeps the server alive
        struct Handle {
            ps_server* server;
            explicit Handle(ps_server* s) : server(s) {}
            ~Handle() { ps_destroy(server); }
            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;
        };

        class LoopbackTransport final : public Transport {
        public:
            LoopbackTransport(std::shared_ptr<Handle> owner, ps_conn* conn)
                : owner(std::move(owner)), conn(conn) {
                info.name = "Singleplayer";
                info.protocol = kBedrockProtocol;
                info.version = kBedrockVersion;
            }
            ~LoopbackTransport() override { Disconnect(); }

            void Disconnect() override {
                if (conn) {
                    ps_conn_close(conn);
                    conn = nullptr;
                }
            }
            bool SendReliable(const std::vector<uint8_t>& data) override {
                return conn && ps_conn_send(conn, data.data(), data.size()) == 1;
            }
            bool SendUnreliable(const std::vector<uint8_t>& data) override { return SendReliable(data); }
            bool TryReceive(std::vector<uint8_t>& out) override {
                if (!conn)
                    return false;
                size_t size = 0;
                if (ps_conn_receive(conn, nullptr, 0, &size) == 0)
                    return false;
                out.resize(size);
                const int r = ps_conn_receive(conn, out.data(), out.size(), &size);
                out.resize(size);
                return r == 1;
            }
            bool IsConnected() const override { return conn && ps_conn_alive(conn); }
            // a local server cannot go quiet on a dead link: it is either connected or the connection reports it is gone
            bool HasLivenessTimeout() const override { return true; }
            std::string LastError() const override { return {}; }
            const NetherNetServerInfo& Info() const override { return info; }

        private:
            std::shared_ptr<Handle> owner;
            ps_conn* conn;
            NetherNetServerInfo info;
        };
    }

    struct EmbeddedServer::Impl {
        std::shared_ptr<Handle> handle;
        std::atomic<bool> running{false};
    };

    EmbeddedServer::EmbeddedServer() : impl(std::make_unique<Impl>()) {}
    EmbeddedServer::~EmbeddedServer() {
        Stop();
    }

    bool EmbeddedServer::Start(const std::string& dataDir, const std::string& configJson,
                               std::string& error) {
        Stop();
        ps_options options{};
        options.data_dir = dataDir.c_str();
        options.config_json = configJson.empty() ? nullptr : configJson.c_str();
        options.loopback_only = 1;
        ps_server* server = ps_create(&options);
        if (!server || ps_start(server) != 0) {
            const char* why = ps_last_error();
            error = why && *why ? why : "the server did not start";
            if (server)
                ps_destroy(server);
            return false;
        }
        ps_run_async(server);
        impl->handle = std::make_shared<Handle>(server);
        impl->running = true;
        return true;
    }

    void EmbeddedServer::Stop() {
        if (!impl->running.exchange(false))
            return;
        ps_stop(impl->handle->server);
        impl->handle.reset();
    }

    bool EmbeddedServer::Running() const {
        if (!impl->running)
            return false;
        if (ps_is_running(impl->handle->server))
            return true;
        // the server's thread ended by itself (it crashed): drop it so the next join starts a fresh one
        const_cast<EmbeddedServer*>(this)->Stop();
        return false;
    }
    void EmbeddedServer::Tick() {
        if (impl->running)
            ps_tick(impl->handle->server);
    }
    void EmbeddedServer::Command(const std::string& line) {
        if (impl->running)
            ps_command(impl->handle->server, line.c_str());
    }

    std::unique_ptr<Transport> EmbeddedServer::Connect() {
        if (!impl->running)
            return nullptr;
        ps_conn* conn = ps_connect_loopback(impl->handle->server);
        return conn ? std::make_unique<LoopbackTransport>(impl->handle, conn) : nullptr;
    }
}
