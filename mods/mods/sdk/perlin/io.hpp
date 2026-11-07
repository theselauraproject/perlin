#pragma once
#include <charconv>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "perlin/rpc.hpp"
#include "perlin/settings.hpp"

namespace perlin::fs {
    inline Result<std::vector<uint8_t>> read_bytes(std::string_view path) {
        abi::Writer w;
        w.str(path);
        return detail::call(Op::StorageRead, w);
    }

    inline Result<std::string> read_text(std::string_view path) {
        auto bytes = read_bytes(path);
        if (!bytes)
            return unexpected(bytes.error());
        return std::string(bytes->begin(), bytes->end());
    }

    inline Result<void> write_bytes(std::string_view path, std::span<const uint8_t> data) {
        abi::Writer w;
        w.str(path).bytes(data.data(), data.size());
        return detail::call_void(Op::StorageWrite, w);
    }

    inline Result<void> write_text(std::string_view path, std::string_view text) {
        return write_bytes(path, {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
    }

    inline bool exists(std::string_view path) {
        abi::Writer w;
        w.str(path);
        auto bytes = detail::call(Op::StorageExists, w);
        return bytes && !bytes->empty() && (*bytes)[0] != 0;
    }

    inline Result<void> remove(std::string_view path) {
        abi::Writer w;
        w.str(path);
        return detail::call_void(Op::StorageRemove, w);
    }

    inline Result<void> make_directory(std::string_view path) {
        abi::Writer w;
        w.str(path);
        return detail::call_void(Op::StorageMakeDir, w);
    }

    inline Result<std::vector<std::string>> list(std::string_view path = "") {
        abi::Writer w;
        w.str(path);
        return detail::call_list<std::string>(Op::StorageList, w);
    }

    using Location = abi::FileLocation;
    inline Location in(Area area, std::string_view path) {
        return {area, std::string(path)};
    }

    inline Result<void> copy(const Location& from, const Location& to) {
        abi::Writer w;
        w.write(from, to);
        return detail::call_void(Op::CopyFile, w);
    }

    inline Result<void> move(const Location& from, const Location& to) {
        abi::Writer w;
        w.write(from, to);
        return detail::call_void(Op::MoveFile, w);
    }
}

namespace perlin {
    class Config {
    public:
        explicit Config(std::string file = "config.cfg") : path(std::move(file)) { load(); }

        template <class T> T get(std::string_view key, T fallback = T{}) const {
            const auto it = values.find(std::string(key));
            if (it == values.end())
                return fallback;
            if constexpr (std::is_same_v<T, std::string>) {
                return it->second;
            } else if constexpr (std::is_same_v<T, bool>) {
                return it->second == "true" || it->second == "1";
            } else if constexpr (std::is_floating_point_v<T>) {
                T v{};
                return detail::parse_floating(it->second, v) ? v : fallback;
            } else {
                T v{};
                const auto r = std::from_chars(it->second.data(), it->second.data() + it->second.size(), v);
                return r.ec == std::errc{} ? v : fallback;
            }
        }

        template <class T> Config& set(std::string_view key, const T& value) {
            if constexpr (std::is_same_v<T, bool>)
                values[std::string(key)] = value ? "true" : "false";
            else if constexpr (std::is_convertible_v<T, std::string_view>)
                values[std::string(key)] = std::string(std::string_view(value));
            else
                values[std::string(key)] = std::format("{}", value);
            dirty = true;
            return *this;
        }

        bool has(std::string_view key) const { return values.contains(std::string(key)); }

        Result<void> save() {
            std::string text;
            for (const auto& [k, v] : values)
                text += k + "=" + v + "\n";
            dirty = false;
            return fs::write_text(path, text);
        }

        bool changed() const { return dirty; }

    private:
        void load() {
            auto text = fs::read_text(path);
            if (!text)
                return;
            size_t at = 0;
            while (at < text->size()) {
                size_t end = text->find('\n', at);
                if (end == std::string::npos)
                    end = text->size();
                const std::string_view line(text->data() + at, end - at);
                const size_t eq = line.find('=');
                if (eq != std::string_view::npos)
                    values[std::string(line.substr(0, eq))] = std::string(line.substr(eq + 1));
                at = end + 1;
            }
        }

        std::string path;
        std::map<std::string, std::string> values;
        bool dirty = false;
    };
}

namespace perlin::net {
    struct Header {
        std::string name, value;
    };

    struct Request {
        std::string method = "GET";
        std::string url;
        std::vector<Header> headers;
        std::vector<uint8_t> body;
        std::chrono::milliseconds timeout{10000};
    };

    struct Response {
        int status = 0;
        std::string error;
        std::vector<Header> headers;
        std::vector<uint8_t> body;

        bool ok() const { return error.empty() && status >= 200 && status < 300; }
        std::string text() const { return std::string(body.begin(), body.end()); }
    };

    using Callback = std::function<void(const Response&)>;

    namespace detail_net {
        inline std::unordered_map<uint32_t, Callback>& pending() {
            static std::unordered_map<uint32_t, Callback> map;
            return map;
        }
        inline uint32_t& counter() {
            static uint32_t next = 1;
            return next;
        }
    }

    inline Result<uint32_t> send(const Request& request, Callback callback) {
        const uint32_t id = detail_net::counter()++;
        abi::Writer w;
        w.put(id)
            .str(request.method)
            .str(request.url)
            .put<int32_t>(static_cast<int32_t>(request.timeout.count()));
        w.put<uint32_t>(static_cast<uint32_t>(request.headers.size()));
        for (const Header& h : request.headers)
            w.str(h.name).str(h.value);
        w.bytes(request.body.data(), request.body.size());
        auto r = detail::call_void(Op::HttpRequest, w);
        if (!r)
            return unexpected(r.error());
        detail_net::pending()[id] = std::move(callback);
        return id;
    }

    inline Result<uint32_t> get(std::string url, Callback callback) {
        Request r;
        r.url = std::move(url);
        return send(r, std::move(callback));
    }

    inline Result<uint32_t> post(std::string url, std::string body, std::string content_type,
                                 Callback callback) {
        Request r;
        r.method = "POST";
        r.url = std::move(url);
        r.headers.push_back({"Content-Type", std::move(content_type)});
        r.body.assign(body.begin(), body.end());
        return send(r, std::move(callback));
    }

    inline Result<uint32_t> download(std::string_view url, const fs::Location& to, Callback callback) {
        const uint32_t id = detail_net::counter()++;
        abi::Writer w;
        w.put(id).str(url).write(to);
        auto r = detail::call_void(Op::DownloadFile, w);
        if (!r)
            return unexpected(r.error());
        detail_net::pending()[id] = std::move(callback);
        return id;
    }

    inline Result<std::string> packet_name(uint32_t id) {
        abi::Writer w;
        w.put(id);
        auto bytes = detail::call(Op::PacketName, w);
        if (!bytes)
            return unexpected(bytes.error());
        return std::string(bytes->begin(), bytes->end());
    }

    inline Result<void> send_packet(uint32_t id, std::span<const uint8_t> body) {
        abi::Writer w;
        w.put(id).bytes(body.data(), body.size());
        return detail::call_void(Op::PacketSend, w);
    }
}
