#include "client/ClientControl.hpp"
#include "util/Platform.hpp"

#if !defined(__EMSCRIPTEN__) && !defined(PERLIN_MOBILE)
#include "client/input/KeyMapping.hpp"
#include "client/ClientControlPage.hpp"
#include "util/Json.hpp"
#include <httplib.h>
#include <stb_image_write.h>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <random>
#include <thread>

namespace perlin {
    struct ClientControl::Impl {
        httplib::Server server;
        std::thread thread;
        std::mutex mutex;
        std::vector<ControlCommand> commands;
        std::deque<std::string> traces;
        std::string state = "{}", frame;
        uint64_t nextId = 0;
        std::atomic<bool> frameRequested{false};
        std::string token;
        std::thread encoder;
        std::mutex encodeMutex;
        std::condition_variable encodeReady;
        std::vector<uint8_t> pendingRgb;
        uint32_t pendingW = 0, pendingH = 0;
        bool hasPending = false, stopEncoder = false;
    };

    namespace {
        std::string RandomToken() {
            std::random_device device;
            std::string token;
            for (int i = 0; i < 4; ++i) {
                char hex[9];
                std::snprintf(hex, sizeof(hex), "%08x", static_cast<unsigned>(device()));
                token += hex;
            }
            return token;
        }

        bool SameToken(const std::string& a, const std::string& b) {
            if (a.size() != b.size())
                return false;
            unsigned char diff = 0;
            for (size_t i = 0; i < a.size(); ++i)
                diff |= static_cast<unsigned char>(a[i] ^ b[i]);
            return diff == 0;
        }
    }

    ClientControl::ClientControl() : impl(std::make_unique<Impl>()) {}
    ClientControl::~ClientControl() {
        Stop();
    }

    bool ClientControl::Start(int port) {
        if (port < 1024 || port > 65535 || impl->thread.joinable())
            return false;
        auto& p = *impl;
        p.token = RandomToken();
        p.server.set_keep_alive_max_count(1);
        p.server.set_payload_max_length(65536);
        p.server.set_pre_routing_handler([port, &p](const httplib::Request& req, httplib::Response& res) {
            const auto host = req.get_header_value("Host");
            const auto origin = req.get_header_value("Origin");
            const std::string local = "127.0.0.1:" + std::to_string(port);
            const std::string localhost = "localhost:" + std::to_string(port);
            if ((host != local && host != localhost) ||
                (!origin.empty() && origin != "http://" + local && origin != "http://" + localhost)) {
                res.status = 403;
                return httplib::Server::HandlerResponse::Handled;
            }
            const auto token = req.has_header("X-Perlin-Token") ? req.get_header_value("X-Perlin-Token")
                               : req.path == "/"                ? req.get_param_value("token")
                                                                : std::string();
            if (!SameToken(token, p.token)) {
                res.status = 401;
                res.set_header("Cache-Control", "no-store");
                return httplib::Server::HandlerResponse::Handled;
            }
            res.set_header("Cache-Control", "no-store");
            return httplib::Server::HandlerResponse::Unhandled;
        });
        p.server.Get("/", [&p](const auto&, auto& res) {
            std::string page(kClientControlPage);
            const std::string_view marker = "__PERLIN_TOKEN__";
            if (const auto at = page.find(marker); at != std::string::npos)
                page.replace(at, marker.size(), p.token);
            res.set_header("Referrer-Policy", "no-referrer");
            res.set_content(std::move(page), "text/html; charset=utf-8");
        });
        p.server.Get("/state", [&p](const auto&, auto& res) {
            std::lock_guard lock(p.mutex);
            res.set_content(p.state, "application/json");
        });
        p.server.Get("/frame", [&p](const auto&, auto& res) {
            p.frameRequested = true;
            std::lock_guard lock(p.mutex);
            if (p.frame.empty()) {
                res.status = 202;
                return;
            }
            res.set_content(p.frame, "image/jpeg");
        });
        p.server.Get("/trace", [&p](const auto&, auto& res) {
            std::deque<std::string> traces;
            {
                std::lock_guard lock(p.mutex);
                traces = p.traces;
            }
            std::string body;
            for (const auto& line : traces)
                body += line + '\n';
            res.set_content(std::move(body), "application/x-ndjson");
        });
        p.server.Post("/command", [&p](const auto& req, auto& res) {
            if (req.get_header_value("Content-Type") != "application/json") {
                res.status = 415;
                return;
            }
            auto command = json::Read<ControlCommand>(req.body);
            if (!command) {
                res.status = 400;
                return;
            }
            auto& c = *command;
            bool valid = c.op == "input" || c.op == "look" || c.op == "command" || c.op == "chat" ||
                         c.op == "key" || c.op == "character" || c.op == "pointer" || c.op == "scroll" ||
                         c.op == "mouse" || c.op == "release" || c.op == "shutdown" || c.op == "probe";
            const bool validKey =
                c.op == "character" ? c.key > 0 && c.key <= 0x10ffff && !(c.key >= 0xd800 && c.key <= 0xdfff)
                                    : c.key >= 0 && c.key < 256;
            valid &= c.ticks >= 0 && c.ticks <= 72000 && std::isfinite(c.yaw) && std::isfinite(c.pitch) &&
                     std::abs(c.yaw) <= 36000 && std::abs(c.pitch) <= 90 && std::isfinite(c.x) &&
                     std::isfinite(c.y) && std::isfinite(c.z) && std::abs(c.x) <= 30000000 &&
                     std::abs(c.y) <= 100000 && std::abs(c.z) <= 30000000 && validKey && c.button >= 0 &&
                     c.button < 8 && c.text.size() <= 8192;
            for (const auto& name : c.actions) {
                bool found = false;
                for (size_t i = 0; i < kActionCount; ++i)
                    found |= name == ActionId(static_cast<Action>(i)) ||
                             "key." + name == ActionId(static_cast<Action>(i));
                valid &= found;
            }
            if (!valid) {
                res.status = 400;
                return;
            }
            std::lock_guard lock(p.mutex);
            if (p.commands.size() >= 128) {
                res.status = 429;
                return;
            }
            c.id = ++p.nextId;
            p.commands.push_back(c);
            res.status = 202;
            res.set_content("{\"id\":" + std::to_string(c.id) + "}", "application/json");
        });
        if (!p.server.bind_to_port("127.0.0.1", port))
            return false;
        p.stopEncoder = false;
        p.encoder = std::thread([&p] {
            std::unique_lock lock(p.encodeMutex);
            for (;;) {
                p.encodeReady.wait(lock, [&p] { return p.hasPending || p.stopEncoder; });
                if (p.stopEncoder)
                    return;
                auto rgb = std::move(p.pendingRgb);
                const auto w = p.pendingW, h = p.pendingH;
                p.hasPending = false;
                lock.unlock();
                std::string jpeg;
                const bool ok = stbi_write_jpg_to_func(
                    [](void* ctx, void* bytes, int n) {
                        static_cast<std::string*>(ctx)->append(static_cast<char*>(bytes), n);
                    },
                    &jpeg, w, h, 3, rgb.data(), 85);
                if (ok) {
                    std::lock_guard frameLock(p.mutex);
                    p.frame = std::move(jpeg);
                }
                lock.lock();
            }
        });
        p.thread = std::thread([&p] { p.server.listen_after_bind(); });
        return true;
    }

    void ClientControl::Stop() {
        impl->server.stop();
        if (impl->thread.joinable())
            impl->thread.join();
        {
            std::lock_guard lock(impl->encodeMutex);
            impl->stopEncoder = true;
        }
        impl->encodeReady.notify_all();
        if (impl->encoder.joinable())
            impl->encoder.join();
    }

    const std::string& ClientControl::Token() const {
        return impl->token;
    }

    std::vector<ControlCommand> ClientControl::TakeCommands() {
        std::lock_guard lock(impl->mutex);
        std::vector<ControlCommand> out;
        out.swap(impl->commands);
        return out;
    }

    void ClientControl::Publish(const ControlSnapshot& state) {
        auto body = json::Write(state);
        std::lock_guard lock(impl->mutex);
        impl->state = std::move(body);
    }

    bool ClientControl::TakeFrameRequest() {
        return impl->frameRequested.exchange(false);
    }

    void ClientControl::Frame(uint32_t w, uint32_t h, uint32_t pitch, const uint8_t* bgra, bool flipY) {
        std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
        for (uint32_t y = 0; y < h; ++y) {
            const auto* row = bgra + static_cast<size_t>(flipY ? h - 1 - y : y) * pitch;
            for (uint32_t x = 0; x < w; ++x) {
                const size_t dst = (static_cast<size_t>(y) * w + x) * 3;
                rgb[dst] = row[x * 4 + 2];
                rgb[dst + 1] = row[x * 4 + 1];
                rgb[dst + 2] = row[x * 4];
            }
        }
        {
            std::lock_guard lock(impl->encodeMutex);
            impl->pendingRgb = std::move(rgb);
            impl->pendingW = w;
            impl->pendingH = h;
            impl->hasPending = true;
        }
        impl->encodeReady.notify_one();
    }

    void ClientControl::Trace(const std::string& line) {
        std::lock_guard lock(impl->mutex);
        impl->traces.push_back(line);
        if (impl->traces.size() > 24000)
            impl->traces.pop_front();
    }
}
#else
namespace perlin {
    struct ClientControl::Impl {};
    ClientControl::ClientControl() : impl(std::make_unique<Impl>()) {}
    ClientControl::~ClientControl() = default;
    bool ClientControl::Start(int) {
        return false;
    }
    void ClientControl::Stop() {}
    const std::string& ClientControl::Token() const {
        static const std::string empty;
        return empty;
    }
    std::vector<ControlCommand> ClientControl::TakeCommands() {
        return {};
    }
    void ClientControl::Publish(const ControlSnapshot&) {}
    bool ClientControl::TakeFrameRequest() {
        return false;
    }
    void ClientControl::Frame(uint32_t, uint32_t, uint32_t, const uint8_t*, bool) {}
    void ClientControl::Trace(const std::string&) {}
}
#endif
