#include "client/features/singleplayer/EmbeddedServer.hpp"

#include "perlite/perlite.h"
#include "util/Log.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>

namespace perlin {
    namespace {

        struct Handle {
            perlite_server* server;
            explicit Handle(perlite_server* s) : server(s) {}
            ~Handle() { perlite_destroy(server); }
            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;
        };

        // What the server is doing while it sets up, fed by Perlite's log lines.
        struct Setup {
            std::mutex mutex;
            std::condition_variable changed;
            enum class Phase { Idle, Starting, Ready, Failed } phase = Phase::Idle;
            std::shared_ptr<Handle> handle;
            bool cancelled = false;
            std::string error;
            std::string text;
            float progress = -1.0f;
        };

        std::string Plain(const char* text) {
            std::string out;
            for (const char* c = text; *c; c++) {
                if (static_cast<unsigned char>(c[0]) == 0xC2 && static_cast<unsigned char>(c[1]) == 0xA7) {
                    c += c[2] ? 2 : 1;
                    continue;
                }
                out += *c;
            }
            return out;
        }

        float PercentIn(const std::string& line) {
            const size_t at = line.find('%');
            if (at == std::string::npos || at == 0)
                return -1.0f;
            size_t begin = at;
            while (begin > 0 && line[begin - 1] >= '0' && line[begin - 1] <= '9')
                begin--;
            if (begin == at)
                return -1.0f;
            return std::clamp(std::atoi(line.c_str() + begin) / 100.0f, 0.0f, 1.0f);
        }

        void OnPerliteLog(int level, const char* text, void* user) {
            const std::string line = Plain(text ? text : "");
            if (level >= 3)
                Log("perlite", "error: {}", line);
            else if (level == 2)
                Log("perlite", "warning: {}", line);
            else
                Log("perlite", "{}", line);
            if (level != 1 || line.empty())
                return;
            auto* setup = static_cast<Setup*>(user);
            std::lock_guard lock(setup->mutex);
            if (setup->phase != Setup::Phase::Starting)
                return;
            setup->text = line;
            setup->progress = PercentIn(line);
        }

        class LoopbackTransport final : public Transport {
        public:
            explicit LoopbackTransport(std::shared_ptr<Setup> setup) : setup(std::move(setup)) {
                info.name = "Singleplayer";
                info.version = kBedrockVersion;
            }
            ~LoopbackTransport() override { Disconnect(); }

            void Disconnect() override {
                closed = true;
                if (conn) {
                    perlite_conn_close(conn);
                    conn = nullptr;
                }
                owner.reset();
            }
            bool SendReliable(const std::vector<uint8_t>& data) override {
                return Open() && perlite_conn_send(conn, data.data(), data.size()) == 1;
            }
            bool SendUnreliable(const std::vector<uint8_t>& data) override { return SendReliable(data); }
            bool TryReceive(std::vector<uint8_t>& out) override {
                if (!Open())
                    return false;
                size_t size = 0;
                if (perlite_conn_receive(conn, nullptr, 0, &size) == 0)
                    return false;
                out.resize(size);
                const int r = perlite_conn_receive(conn, out.data(), out.size(), &size);
                out.resize(size);
                return r == 1;
            }
            bool IsConnected() const override { return Open() && perlite_conn_alive(conn); }

            bool HasLivenessTimeout() const override { return true; }
            std::string LastError() const override { return error; }
            // The session reads Info() first, on its connect thread, so that is where the wait happens.
            const NetherNetServerInfo& Info() const override {
                info.protocol = Open() ? kBedrockProtocol : 0;
                return info;
            }

        private:
            bool Open() const {
                if (conn)
                    return true;
                if (closed || tried)
                    return false;
                tried = true;
                std::unique_lock lock(setup->mutex);
                setup->changed.wait(lock, [&] { return setup->phase != Setup::Phase::Starting; });
                if (setup->phase != Setup::Phase::Ready || !setup->handle) {
                    error = setup->error.empty() ? "the singleplayer server did not start" : setup->error;
                    return false;
                }
                owner = setup->handle;
                lock.unlock();
                conn = perlite_connect_loopback(owner->server);
                if (!conn)
                    error = "the singleplayer server refused the connection";
                return conn != nullptr;
            }

            std::shared_ptr<Setup> setup;
            mutable std::shared_ptr<Handle> owner;
            mutable perlite_conn* conn = nullptr;
            mutable bool tried = false;
            bool closed = false;
            mutable std::string error;
            mutable NetherNetServerInfo info;
        };

        perlite_server* CreateAndStart(const std::string& dataDir, const std::string& configJson,
                                       std::string& error) {
            perlite_options options{};
            options.data_dir = dataDir.c_str();
            options.config_json = configJson.empty() ? nullptr : configJson.c_str();
            options.loopback_only = 1;
            perlite_server* server = perlite_create(&options);
            if (!server || perlite_start(server) != 0) {
                const char* why = perlite_last_error();
                error = why && *why ? why : "the server did not start";
                if (server)
                    perlite_destroy(server);
                return nullptr;
            }
            perlite_run_async(server);
            return server;
        }
    }

    struct EmbeddedServer::Impl {
        std::shared_ptr<Setup> setup = std::make_shared<Setup>();
        // Starting and stopping run here, one after the other, so leaving a world never waits on the UI thread.
        std::thread worker;
        std::atomic<bool> running{false};

        template <class F> void Then(F&& work) {
            worker = std::thread([previous = std::move(worker), work = std::forward<F>(work)]() mutable {
                if (previous.joinable())
                    previous.join();
                work();
            });
        }
        void Wait() {
            if (worker.joinable())
                worker.join();
        }
    };

    EmbeddedServer::EmbeddedServer() : impl(std::make_unique<Impl>()) {}
    EmbeddedServer::~EmbeddedServer() {
        Stop();
        impl->Wait();
        perlite_set_log(nullptr, nullptr);
    }

    bool EmbeddedServer::Start(const std::string& dataDir, const std::string& configJson,
                               std::string& error) {
        StartAsync(dataDir, configJson);
        impl->Wait();
        std::lock_guard lock(impl->setup->mutex);
        if (impl->setup->phase == Setup::Phase::Ready)
            return true;
        error = impl->setup->error;
        return false;
    }

    void EmbeddedServer::StartAsync(const std::string& dataDir, const std::string& configJson) {
        Stop();
        auto setup = std::make_shared<Setup>();
        setup->phase = Setup::Phase::Starting;
        setup->text = "Starting the server";
        perlite_set_log(&OnPerliteLog, setup.get());
        impl->setup = setup;
        impl->running = true;
        impl->Then([setup, dataDir, configJson] {
            std::string error;
            perlite_server* server = CreateAndStart(dataDir, configJson, error);
            bool cancelled = false;
            {
                std::lock_guard lock(setup->mutex);
                cancelled = setup->cancelled;
                if (server && !cancelled) {
                    setup->handle = std::make_shared<Handle>(server);
                    setup->phase = Setup::Phase::Ready;
                } else {
                    setup->error = cancelled ? "the world was closed while it loaded" : error;
                    setup->phase = Setup::Phase::Failed;
                }
            }
            setup->changed.notify_all();
            if (!server)
                Log("singleplayer", "{}", error);
            else if (cancelled) {
                perlite_stop(server);
                perlite_destroy(server);
            }
        });
    }

    void EmbeddedServer::Stop() {
        if (!impl->running.exchange(false))
            return;
        std::shared_ptr<Handle> handle;
        {
            std::lock_guard lock(impl->setup->mutex);
            if (impl->setup->phase == Setup::Phase::Starting)
                impl->setup->cancelled = true;
            handle = std::move(impl->setup->handle);
        }
        if (handle)
            impl->Then([handle = std::move(handle)] { perlite_stop(handle->server); });
    }

    bool EmbeddedServer::Starting() const {
        std::lock_guard lock(impl->setup->mutex);
        return impl->setup->phase == Setup::Phase::Starting;
    }

    EmbeddedServer::SetupStatus EmbeddedServer::Status() const {
        std::lock_guard lock(impl->setup->mutex);
        SetupStatus s;
        s.starting = impl->setup->phase == Setup::Phase::Starting;
        if (s.starting) {
            s.text = impl->setup->text;
            s.progress = impl->setup->progress;
        }
        return s;
    }

    bool EmbeddedServer::Running() const {
        if (!impl->running)
            return false;
        std::shared_ptr<Handle> handle;
        {
            std::lock_guard lock(impl->setup->mutex);
            if (impl->setup->phase == Setup::Phase::Starting)
                return true;
            handle = impl->setup->handle;
        }
        if (handle && perlite_is_running(handle->server))
            return true;
        const_cast<EmbeddedServer*>(this)->Stop();
        return false;
    }

    void EmbeddedServer::Tick() {
        std::shared_ptr<Handle> handle;
        {
            std::lock_guard lock(impl->setup->mutex);
            handle = impl->setup->handle;
        }
        if (handle)
            perlite_tick(handle->server);
    }

    void EmbeddedServer::Command(const std::string& line) {
        std::shared_ptr<Handle> handle;
        {
            std::lock_guard lock(impl->setup->mutex);
            handle = impl->setup->handle;
        }
        if (handle)
            perlite_command(handle->server, line.c_str());
    }

    std::unique_ptr<Transport> EmbeddedServer::Connect() {
        if (!impl->running)
            return nullptr;
        return std::make_unique<LoopbackTransport>(impl->setup);
    }
}
