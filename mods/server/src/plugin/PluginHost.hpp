#pragma once
#include "plugin/PluginApi.hpp"

#include "perlin/host.hpp"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace perlin::server {
    class PluginHost {
    public:
        struct Info {
            std::string name;
            std::string file;
            std::string version;
            std::string author;
            std::string description;
            std::string engine;
            std::string arch;
            bool enabled = false;
            std::string error;
        };

        explicit PluginHost(PluginApi& api);
        ~PluginHost();
        PluginHost(const PluginHost&) = delete;
        PluginHost& operator=(const PluginHost&) = delete;

        void LoadDirectory(const std::string& dir);
        bool LoadFile(const std::string& path, std::string& error);
        void UnloadAll();
        void Pump();

        template <abi::Event E> bool Dispatch(const abi::payload_t<E>& payload) {
            const abi::Writer w = abi::host::Encode<E>(payload);
            return DispatchEncoded(E, {w.Data(), w.Size()});
        }
        bool AnyoneListens(abi::Event event) const;
        bool DispatchCommand(const std::string& plugin, uint64_t player, const std::string& command,
                             const std::string& args);
        std::vector<Info> List() const;

        std::optional<double> AmendedNumber(const std::string& field) const;
        std::optional<std::string> AmendedText(const std::string& field) const;

    private:
        struct Plugin;
        struct Amendment {
            std::optional<double> number;
            std::optional<std::string> text;
        };
        struct HttpDone {
            std::string plugin;
            abi::EventHttp event;
        };

        void RegisterOps();
        bool DispatchEncoded(abi::Event event, std::span<const uint8_t> payload);
        bool CallPlugin(Plugin& plugin, abi::Event event, std::span<const uint8_t> payload, uint32_t& result);
        Plugin* Find(const std::string& name) const;
        void LoadSettings(Plugin& plugin);
        void SaveSettings(const Plugin& plugin) const;

        std::vector<std::unique_ptr<Plugin>> plugins;
        abi::host::OpTable<PluginCall> ops;
        PluginApi& api;
        std::map<std::string, Amendment> amendments;
        int dispatchDepth = 0;
        std::mutex httpMutex;
        std::vector<HttpDone> httpDone;
        std::atomic<int> httpActive{0};
        std::vector<std::pair<std::string, abi::ModMessageData>> mail;
    };
}
