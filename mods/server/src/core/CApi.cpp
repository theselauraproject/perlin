#include "perlin/server.h"

#include "core/Json.hpp"
#include "core/Log.hpp"
#include "core/Server.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

using namespace perlin::server;

struct ps_server {
    std::unique_ptr<Server> server;
    std::thread thread;
    std::atomic<bool> asyncRunning{false};
    // the tick thread ended: a stop was asked for, or the server threw and shut itself down
    std::atomic<bool> finished{false};
    std::atomic<bool> crashed{false};
};

struct ps_conn {
    std::shared_ptr<LoopbackListener::Endpoint> endpoint;
    // a message too big for the caller's buffer waits here
    std::vector<uint8_t> held;
    bool hasHeld = false;
};

namespace {
    thread_local std::string g_error;
}

extern "C" {

PS_API uint32_t ps_abi_version(void) {
    return 1;
}

PS_API void ps_set_log(ps_log_fn fn, void* user) {
    if (!fn) {
        SetLogSink(nullptr);
        return;
    }
    SetLogSink([fn, user](LogLevel level, const std::string& text) {
        fn(static_cast<int>(level), text.c_str(), user);
    });
}

PS_API const char* ps_last_error(void) {
    return g_error.c_str();
}

PS_API ps_server* ps_create(const ps_options* options) {
    try {
        ServerConfig config;
        std::string dir = options && options->data_dir ? options->data_dir : ".";
        std::string error;
        config = ServerConfig::LoadDirectory(dir, &error);
        if (!error.empty())
            LogWarn(error);
        config.dataDir = dir;
        if (config.pluginDir.empty())
            config.pluginDir = dir + "/plugins";
        if (options && options->config_json) {
            // overrides: write them over the loaded file the same way server.json is read
            json::Value over;
            if (json::Parse(options->config_json, over, &error)) {
                if (over["name"].IsString())
                    config.name = over["name"].text;
                if (over["port"].Int(0) > 0)
                    config.port = static_cast<uint16_t>(over["port"].Int());
                if (over["view_distance"].Int(0) > 0)
                    config.viewDistance = static_cast<uint32_t>(over["view_distance"].Int());
                if (over["max_players"].Int(0) > 0)
                    config.maxPlayers = static_cast<uint32_t>(over["max_players"].Int());
                if (over["terrain_backend"].IsString())
                    config.terrainBackend = over["terrain_backend"].text;
                if (const int64_t gm = over["gamemode"].Int(-1); gm == 0 || gm == 1 || gm == 2 || gm == 6)
                    config.gameMode = static_cast<int>(gm);
                if (over["plugin_dir"].IsString())
                    config.pluginDir = over["plugin_dir"].text;
                if (over["generator"].IsString())
                    config.generator = over["generator"].text;
                if (over["seed"].kind == json::Value::Kind::Number) {
                    config.seed = static_cast<uint64_t>(over["seed"].Int());
                    config.randomSeed = false;
                }
            } else {
                g_error = "config_json: " + error;
                return nullptr;
            }
        }
        if (options && options->loopback_only)
            config.loopbackOnly = true;
        auto* s = new ps_server();
        s->server = std::make_unique<Server>(std::move(config));
        return s;
    } catch (const std::exception& e) {
        g_error = e.what();
        return nullptr;
    }
}

PS_API int ps_start(ps_server* s) {
    if (!s)
        return -1;
    std::string error;
    if (!s->server->Start(error)) {
        g_error = error;
        return -1;
    }
    return 0;
}

PS_API int ps_run_async(ps_server* s) {
    if (!s || s->asyncRunning.exchange(true))
        return -1;
    s->thread = std::thread([s] {
        try {
            s->server->Run();
        } catch (const std::exception& e) {
            // a bug in one tick must not take the host down with it: log it and let the host see the server is gone
            LogError(std::string("server crashed: ") + e.what());
            s->crashed = true;
        } catch (...) {
            LogError("server crashed");
            s->crashed = true;
        }
        s->finished = true;
    });
    return 0;
}

PS_API int ps_is_running(ps_server* s) {
    return s && s->asyncRunning && !s->finished ? 1 : 0;
}

PS_API void ps_tick(ps_server* s) {
    if (s)
        s->server->Tick();
}

PS_API void ps_stop(ps_server* s) {
    if (!s)
        return;
    s->server->RequestStop();
    if (s->thread.joinable())
        s->thread.join();
    s->asyncRunning = false;
    s->server->Stop();
}

PS_API void ps_destroy(ps_server* s) {
    if (!s)
        return;
    ps_stop(s);
    delete s;
}

PS_API void ps_command(ps_server* s, const char* line) {
    if (s && line)
        s->server->RunConsoleCommand(line);
}

PS_API ps_conn* ps_connect_loopback(ps_server* s) {
    if (!s)
        return nullptr;
    auto endpoint = s->server->Loopback().Connect();
    if (!endpoint)
        return nullptr;
    auto* c = new ps_conn();
    c->endpoint = std::move(endpoint);
    return c;
}

PS_API int ps_conn_send(ps_conn* c, const uint8_t* data, size_t size) {
    return c && c->endpoint->Send(data, size) ? 1 : 0;
}

PS_API int ps_conn_receive(ps_conn* c, uint8_t* buf, size_t capacity, size_t* size) {
    if (!c)
        return 0;
    if (!c->hasHeld) {
        if (!c->endpoint->Receive(c->held))
            return 0;
        c->hasHeld = true;
    }
    if (size)
        *size = c->held.size();
    if (c->held.size() > capacity)
        return -1;
    std::copy(c->held.begin(), c->held.end(), buf);
    c->hasHeld = false;
    return 1;
}

PS_API int ps_conn_alive(ps_conn* c) {
    return c && c->endpoint->Alive() ? 1 : 0;
}

PS_API void ps_conn_close(ps_conn* c) {
    if (!c)
        return;
    c->endpoint->Close();
    delete c;
}
}
