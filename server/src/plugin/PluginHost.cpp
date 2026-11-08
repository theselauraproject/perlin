#include "plugin/PluginHost.hpp"

#include "host/PluginCore.hpp"
#include "core/Json.hpp"
#include "core/Log.hpp"
#include "util/FileIo.hpp"
#include "util/ModSocket.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

#if __has_include(<httplib.h>)
#include <httplib.h>
#define PERLIN_SERVER_PLUGIN_HTTP 1
#endif

namespace perlin::server {
    namespace fs = std::filesystem;
    using mods::WasmInstance;
    using abi::Status;
    template <class T> using Result = abi::host::Result<T>;

    struct PluginHost::Plugin {
        Info info;
        std::string dataDir;
        std::vector<uint8_t> wasm;
        std::unique_ptr<WasmInstance> instance;
        mods::GuestExports exports;
        mods::Subscriptions subscriptions = mods::AllEvents();
        uint32_t apiRevision = mods::kDefaultApiRevision;
        std::set<std::string> commands;
        std::map<std::string, std::string> settings;
        std::unique_ptr<util::SocketSet> sockets;
    };

    namespace {
        Result<void> Check(bool ok, Status failure = Status::Failed) {
            if (ok)
                return {};
            return std::unexpected(failure);
        }

        bool SafePath(const std::string& root, const std::string& rel, fs::path& out) {
            if (rel.size() > 512 || rel.find('\0') != std::string::npos)
                return false;
            const fs::path p = fs::path(rel).lexically_normal();
            if (p.is_absolute() || p.has_root_name())
                return false;
            for (const auto& part : p)
                if (part == "..")
                    return false;
            out = fs::path(root) / p;
            return true;
        }

        Result<void> FromSocket(util::SocketResult r) {
            switch (r) {
            case util::SocketResult::Ok:
                return {};
            case util::SocketResult::BadArguments:
                return std::unexpected(Status::BadArguments);
            case util::SocketResult::Unavailable:
                return std::unexpected(Status::Unavailable);
            case util::SocketResult::NotFound:
                return std::unexpected(Status::NotFound);
            }
            return std::unexpected(Status::Failed);
        }

        std::string Escape(const std::string& s) {
            return json::Escape(s);
        }
    }

    PluginHost::PluginHost(PluginApi& api) : api(api) {
        RegisterOps();
    }
    PluginHost::~PluginHost() {
        UnloadAll();
        while (httpActive.load() > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    PluginHost::Plugin* PluginHost::Find(const std::string& name) const {
        for (const auto& p : plugins)
            if (p->info.name == name)
                return p.get();
        return nullptr;
    }

    void PluginHost::RegisterOps() {
        using abi::Op;
        using Ctx = PluginCall;
        api.Bind(ops);

        ops.Handle<Op::Log>([](Ctx& c, const abi::LogRequest& r) {
            Log(static_cast<LogLevel>(std::clamp(static_cast<int>(r.level), 0, 3)), "[" + c.plugin + "] " + r.text);
        });
        ops.Handle<Op::Clock>([](Ctx&) {
            return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count());
        });
        ops.Handle<Op::ApiInfo>([this](Ctx& c) {
            abi::ApiInfoResponse r;
            r.major = abi::kApiVersion;
            r.revision = abi::kApiRevision;
            r.oldest = abi::kApiOldestRevision;
            const Plugin* p = Find(c.plugin);
            r.plugin = p ? p->apiRevision : mods::kDefaultApiRevision;
            return r;
        });
        ops.Handle<Op::Capabilities>([this](Ctx&) {
            abi::CapabilityList list;
            list.ops = ops.Ids();
            return list;
        });
        ops.Handle<Op::SocketOpen>([this](Ctx& c, const abi::SocketOpenRequest& r) -> Result<void> {
            Plugin* p = Find(c.plugin);
            if (!p)
                return std::unexpected(Status::NotFound);
            if (!p->sockets)
                p->sockets = std::make_unique<util::SocketSet>();
            util::SocketOpenOptions o;
            o.id = r.id;
            o.webSocket = r.kind == abi::SocketKind::WebSocket;
            o.url = r.url;
            o.timeoutMs = r.timeoutMs;
            o.headers = r.headers;
            o.protocols = r.protocols;
            return FromSocket(p->sockets->Open(std::move(o)));
        });
        ops.Handle<Op::SocketSend>([this](Ctx& c, const abi::SocketSendRequest& r) -> Result<void> {
            Plugin* p = Find(c.plugin);
            if (!p || !p->sockets)
                return std::unexpected(Status::NotFound);
            return FromSocket(p->sockets->Send(r.id, r.binary, r.data));
        });
        ops.Handle<Op::SocketClose>([this](Ctx& c, const abi::SocketCloseRequest& r) -> Result<void> {
            Plugin* p = Find(c.plugin);
            if (!p || !p->sockets)
                return std::unexpected(Status::NotFound);
            return FromSocket(p->sockets->Close(r.id, r.code, r.reason));
        });
        ops.Handle<Op::CommandRegister>([this](Ctx& c, const abi::CommandRegisterRequest& r) -> Result<void> {
            std::string name = r.name;
            if (name.starts_with('/'))
                name.erase(0, 1);
            if (name.empty())
                return std::unexpected(Status::BadArguments);
            if (Plugin* p = Find(c.plugin))
                p->commands.insert(name);
            api.RegisterCommand(c.plugin, name, r.description);
            return {};
        });
        ops.Handle<Op::CommandUnregister>([this](Ctx& c, const abi::CommandUnregisterRequest& r) {
            if (Plugin* p = Find(c.plugin))
                p->commands.erase(r.name);
            api.UnregisterCommand(c.plugin, r.name);
        });

        ops.Handle<Op::StorageRead>([](Ctx& c, const abi::StorageReadRequest& r) -> Result<abi::RestBytes> {
            fs::path p;
            if (!SafePath(c.dataDir, r.path, p))
                return std::unexpected(Status::BadArguments);
            abi::RestBytes out;
            if (!file::ReadBytes(p.string(), out.value))
                return std::unexpected(Status::NotFound);
            return out;
        });
        ops.Handle<Op::StorageWrite>([](Ctx& c, const abi::StorageWriteRequest& r) -> Result<void> {
            fs::path p;
            if (!SafePath(c.dataDir, r.path, p) || r.data.size() > (64u << 20))
                return std::unexpected(Status::BadArguments);
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
            std::ofstream f(p, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(r.data.data()), static_cast<std::streamsize>(r.data.size()));
            return Check(static_cast<bool>(f));
        });
        ops.Handle<Op::StorageList>([](Ctx& c, const abi::StorageListRequest& r) -> Result<abi::StorageListResponse> {
            fs::path p;
            if (!SafePath(c.dataDir, r.path.empty() ? "." : r.path, p))
                return std::unexpected(Status::BadArguments);
            abi::StorageListResponse out;
            std::error_code ec;
            for (const auto& e : fs::directory_iterator(p, ec))
                out.names.push_back(e.path().filename().string() + (e.is_directory() ? "/" : ""));
            std::ranges::sort(out.names);
            return out;
        });
        ops.Handle<Op::StorageRemove>([](Ctx& c, const abi::StorageRemoveRequest& r) -> Result<void> {
            fs::path p;
            if (!SafePath(c.dataDir, r.path, p) || r.path.empty())
                return std::unexpected(Status::BadArguments);
            std::error_code ec;
            return Check(fs::remove_all(p, ec) > 0, Status::NotFound);
        });
        ops.Handle<Op::StorageExists>([](Ctx& c, const abi::StorageExistsRequest& r) -> Result<bool> {
            fs::path p;
            if (!SafePath(c.dataDir, r.path, p))
                return std::unexpected(Status::BadArguments);
            std::error_code ec;
            return fs::exists(p, ec);
        });
        ops.Handle<Op::StorageMakeDir>([](Ctx& c, const abi::StorageMakeDirRequest& r) -> Result<void> {
            fs::path p;
            if (!SafePath(c.dataDir, r.path, p))
                return std::unexpected(Status::BadArguments);
            std::error_code ec;
            fs::create_directories(p, ec);
            return Check(!ec);
        });

        ops.Handle<Op::SettingGet>([this](Ctx& c, const abi::SettingGetRequest& r) -> Result<abi::RestText> {
            const Plugin* p = Find(c.plugin);
            const auto it = p ? p->settings.find(r.id) : decltype(p->settings.cend()){};
            if (!p || it == p->settings.end())
                return std::unexpected(Status::NotFound);
            return abi::RestText{it->second};
        });
        ops.Handle<Op::SettingSet>([this](Ctx& c, const abi::SettingSetRequest& r) -> Result<void> {
            Plugin* p = Find(c.plugin);
            if (!p || r.id.empty() || r.id.size() > 128 || r.value.size() > 65536)
                return std::unexpected(Status::BadArguments);
            p->settings[r.id] = r.value;
            SaveSettings(*p);
            return {};
        });
        ops.Handle<Op::SettingList>([this](Ctx& c) {
            abi::SettingListResponse out;
            if (const Plugin* p = Find(c.plugin))
                for (const auto& [id, value] : p->settings)
                    out.settings.push_back({id, value});
            return out;
        });
        ops.Handle<Op::ModList>([this](Ctx& c) {
            abi::ModListResponse out;
            for (const auto& p : plugins) {
                abi::ModEntry e;
                e.name = p->info.name;
                e.displayName = p->info.name;
                e.description = p->info.description;
                e.version = p->info.version;
                e.author = p->info.author;
                e.state = p->info.enabled ? "running" : (p->info.error.empty() ? "disabled" : "error");
                e.enabled = p->info.enabled;
                e.self = p->info.name == c.plugin;
                e.hasSettings = !p->settings.empty();
                e.arch = p->info.arch;
                for (const abi::Event ev : abi::kAllEvents)
                    if (mods::Subscribed(p->subscriptions, ev) && abi::HostsOf(ev) != abi::Hosts::Client)
                        e.hooks.emplace_back(abi::SnakeName(ev));
                out.mods.push_back(std::move(e));
            }
            return out;
        });
        ops.Handle<Op::ModSend>([this](Ctx& c, const abi::ModMessageData& m) -> Result<void> {
            if (m.channel.empty() || m.body.size() > (1u << 20) || mail.size() > 4096)
                return std::unexpected(Status::BadArguments);
            if (!m.to.empty() && !Find(m.to))
                return std::unexpected(Status::NotFound);
            mail.emplace_back(c.plugin, m);
            return {};
        });
        ops.Handle<Op::HttpRequest>([this](Ctx& c, const abi::HttpRequestRequest& r) -> Result<void> {
#ifdef PERLIN_SERVER_PLUGIN_HTTP
            static const std::set<std::string> methods = {"GET", "POST", "PUT", "DELETE", "PATCH", "HEAD"};
            if (!methods.contains(r.method) || r.body.size() > (4u << 20) || r.headers.size() > 64)
                return std::unexpected(Status::BadArguments);
            const bool tls = r.url.starts_with("https://");
            if (!tls && !r.url.starts_with("http://"))
                return std::unexpected(Status::BadArguments);
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
            if (tls)
                return std::unexpected(Status::Unavailable);
#endif
            if (httpActive.load() >= 8)
                return std::unexpected(Status::Unavailable);
            httpActive++;
            std::thread([this, plugin = c.plugin, r] {
                abi::EventHttp ev;
                ev.id = r.id;
                const size_t schemeEnd = r.url.find("://") + 3;
                const size_t pathAt = r.url.find('/', schemeEnd);
                const std::string origin = r.url.substr(0, pathAt);
                const std::string path = pathAt == std::string::npos ? "/" : r.url.substr(pathAt);
                httplib::Client cli(origin);
                const auto timeout = std::chrono::milliseconds(std::clamp(r.timeoutMs, 500, 60000));
                cli.set_connection_timeout(timeout);
                cli.set_read_timeout(timeout);
                cli.set_follow_location(true);
                httplib::Headers headers;
                for (const auto& [k, v] : r.headers)
                    headers.emplace(k, v);
                const std::string body(r.body.begin(), r.body.end());
                std::string type = "application/octet-stream";
                for (const auto& [k, v] : r.headers)
                    if (k.size() == 12 && std::equal(k.begin(), k.end(), "content-type",
                                                     [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; }))
                        type = v;
                httplib::Result res = r.method == "GET"      ? cli.Get(path, headers)
                                      : r.method == "HEAD"   ? cli.Head(path, headers)
                                      : r.method == "DELETE" ? cli.Delete(path, headers, body, type)
                                      : r.method == "PUT"    ? cli.Put(path, headers, body, type)
                                      : r.method == "PATCH"  ? cli.Patch(path, headers, body, type)
                                                             : cli.Post(path, headers, body, type);
                if (res) {
                    ev.status = res->status;
                    for (const auto& [k, v] : res->headers)
                        ev.headers.emplace_back(k, v);
                    ev.body.assign(res->body.begin(), res->body.end());
                } else {
                    ev.status = -1;
                    ev.error = httplib::to_string(res.error());
                }
                {
                    std::lock_guard lock(httpMutex);
                    httpDone.push_back({plugin, std::move(ev)});
                }
                httpActive--;
            }).detach();
            return {};
#else
            (void)c;
            (void)r;
            return std::unexpected(Status::Unavailable);
#endif
        });
        ops.Handle<Op::EventAmend>([this](Ctx&, const abi::EventAmendRequest& r) -> Result<void> {
            if (dispatchDepth == 0 || r.field.empty())
                return std::unexpected(Status::Unavailable);
            Amendment& a = amendments[r.field];
            a.number = r.number;
            a.text = r.text;
            return {};
        });
    }

    std::optional<double> PluginHost::AmendedNumber(const std::string& field) const {
        const auto it = amendments.find(field);
        return it == amendments.end() ? std::nullopt : it->second.number;
    }
    std::optional<std::string> PluginHost::AmendedText(const std::string& field) const {
        const auto it = amendments.find(field);
        return it == amendments.end() ? std::nullopt : it->second.text;
    }

    void PluginHost::LoadSettings(Plugin& p) {
        std::string text;
        if (!file::Read((fs::path(p.dataDir) / "settings.json").string(), text))
            return;
        json::Value v;
        if (!json::Parse(text, v))
            return;
        for (const auto& [k, value] : v.members)
            p.settings[k] = value.String();
    }

    void PluginHost::SaveSettings(const Plugin& p) const {
        std::string text = "{";
        bool first = true;
        for (const auto& [k, v] : p.settings) {
            text += (first ? "\n  \"" : ",\n  \"") + Escape(k) + "\": \"" + Escape(v) + "\"";
            first = false;
        }
        text += "\n}\n";
        std::error_code ec;
        fs::create_directories(p.dataDir, ec);
        std::ofstream(fs::path(p.dataDir) / "settings.json", std::ios::trunc) << text;
    }

    void PluginHost::LoadDirectory(const std::string& dir) {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec))
            return;
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(dir, ec))
            if (entry.is_regular_file() && entry.path().extension() == ".wasm")
                files.push_back(entry.path());
        std::ranges::sort(files);
        for (const fs::path& file : files) {
            std::string error;
            if (!LoadFile(file.string(), error))
                LogWarn("plugin " + file.filename().string() + " not loaded: " + error);
        }
        DispatchEncoded(abi::Event::ModsLoaded, {});
    }

    bool PluginHost::LoadFile(const std::string& path, std::string& error) {
        auto plugin = std::make_unique<Plugin>();
        plugin->info.file = path;
        plugin->info.name = fs::path(path).stem().string();
        plugin->info.engine = mods::WasmEngineName();
        if (!file::ReadBytes(path, plugin->wasm)) {
            error = "cannot read the file";
            return false;
        }
        if (std::string text; file::Read(fs::path(path).replace_extension(".json").string(), text)) {
            json::Value manifest;
            if (json::Parse(text, manifest)) {
                if (std::string name = manifest["name"].String(); !name.empty())
                    plugin->info.name = std::move(name);
                plugin->info.version = manifest["version"].String();
                plugin->info.author = manifest["author"].String();
                plugin->info.description = manifest["description"].String();
            }
        }
        plugin->dataDir = (fs::path(path).parent_path() / "data" / plugin->info.name).string();
        LoadSettings(*plugin);

        Plugin* raw = plugin.get();
        raw->instance = std::make_unique<WasmInstance>();
        const std::string pluginName = raw->info.name;
        const auto call = [this, raw](uint32_t op, std::span<const uint8_t> in, uint8_t* out, uint32_t cap,
                                      std::string&) {
            PluginCall ctx{raw->info.name, raw->dataDir};
            abi::Writer reply;
            const Status status = ops.Call(ctx, op, in, reply, ~0u);
            return mods::Reply(status, reply, out, cap);
        };
        const auto log = [pluginName](const std::string& text) { LogInfo("[" + pluginName + "] " + text); };
        if (!raw->instance->Load(raw->wasm.data(), raw->wasm.size(), mods::StandardImports(call, log), error))
            return false;
        raw->exports = mods::ProbeExports(*raw->instance);
        if (!raw->exports.event) {
            error = "not a Perlin plugin (no perlin_event export)";
            return false;
        }
        std::string ignored;
        if (raw->exports.initialize)
            raw->instance->Call("_initialize", {}, nullptr, ignored);
        raw->info.enabled = true;
        raw->info.arch = raw->instance->Is64() ? "wasm64" : "wasm32";
        raw->apiRevision = mods::ReadApiRevision(*raw->instance);
        LogInfo("plugin " + raw->info.name + (raw->info.version.empty() ? "" : " " + raw->info.version) +
                " loaded (" + raw->info.arch + " on " + raw->info.engine + ", api revision " +
                std::to_string(raw->apiRevision) + ")");
        if (raw->apiRevision > abi::kApiRevision)
            LogInfo("plugin " + raw->info.name + " is built for a newer api revision than this server provides: newer operations answer UnknownOp");
        plugins.push_back(std::move(plugin));
        uint32_t result = 0;
        CallPlugin(*raw, abi::Event::Load, {}, result);
        raw->subscriptions = mods::ReadSubscriptions(*raw->instance);
        return true;
    }

    void PluginHost::UnloadAll() {
        for (auto& p : plugins) {
            uint32_t result = 0;
            if (p->info.enabled)
                CallPlugin(*p, abi::Event::Unload, {}, result);
        }
        plugins.clear();
    }

    void PluginHost::Pump() {
        std::vector<HttpDone> done;
        {
            std::lock_guard lock(httpMutex);
            done.swap(httpDone);
        }
        for (HttpDone& d : done)
            if (Plugin* p = Find(d.plugin); p && p->info.enabled) {
                const abi::Writer w = abi::host::Encode<abi::Event::HttpResponse>(d.event);
                uint32_t result = 0;
                CallPlugin(*p, abi::Event::HttpResponse, {w.Data(), w.Size()}, result);
            }
        for (auto& p : plugins) {
            if (!p->sockets || !p->info.enabled)
                continue;
            std::vector<util::SocketEvent> batch;
            p->sockets->Poll(batch);
            for (util::SocketEvent& e : batch) {
                if (!p->info.enabled)
                    break;
                uint32_t result = 0;
                if (e.kind == util::SocketEvent::Kind::Open) {
                    const abi::Writer w = abi::host::Encode<abi::Event::SocketOpen>({e.id, e.text});
                    CallPlugin(*p, abi::Event::SocketOpen, {w.Data(), w.Size()}, result);
                } else if (e.kind == util::SocketEvent::Kind::Data) {
                    const abi::Writer w = abi::host::Encode<abi::Event::SocketData>({e.id, e.binary, std::move(e.data)});
                    CallPlugin(*p, abi::Event::SocketData, {w.Data(), w.Size()}, result);
                } else {
                    const abi::Writer w = abi::host::Encode<abi::Event::SocketClose>({e.id, e.code, e.text, e.error});
                    CallPlugin(*p, abi::Event::SocketClose, {w.Data(), w.Size()}, result);
                }
            }
        }
        auto letters = std::move(mail);
        mail.clear();
        for (auto& [from, m] : letters) {
            const abi::Writer w = abi::host::Encode<abi::Event::ModMessage>({from, m.channel, m.body});
            for (auto& p : plugins) {
                if (!p->info.enabled || p->info.name == from || (!m.to.empty() && p->info.name != m.to))
                    continue;
                if (!mods::Subscribed(p->subscriptions, abi::Event::ModMessage))
                    continue;
                uint32_t result = 0;
                CallPlugin(*p, abi::Event::ModMessage, {w.Data(), w.Size()}, result);
            }
        }
    }

    bool PluginHost::CallPlugin(Plugin& p, abi::Event event, std::span<const uint8_t> payload,
                                uint32_t& result) {
        std::string error;
        if (mods::CallEvent(*p.instance, p.exports, static_cast<uint32_t>(event), payload, result, error)) {
            if (p.exports.subscriptions && event != abi::Event::Tick && event != abi::Event::PlayerMove)
                p.subscriptions = mods::ReadSubscriptions(*p.instance);
            return true;
        }
        p.info.enabled = false;
        p.info.error = error;
        LogError("plugin " + p.info.name + " disabled after an error: " + error);
        return false;
    }

    bool PluginHost::AnyoneListens(abi::Event event) const {
        return std::ranges::any_of(plugins, [event](const auto& p) {
            return p->info.enabled && mods::Subscribed(p->subscriptions, event);
        });
    }

    bool PluginHost::DispatchEncoded(abi::Event event, std::span<const uint8_t> payload) {
        if (dispatchDepth == 0)
            amendments.clear();
        dispatchDepth++;
        bool cancelled = false;
        for (auto& p : plugins) {
            if (!p->info.enabled || !mods::Subscribed(p->subscriptions, event))
                continue;
            uint32_t result = 0;
            if (CallPlugin(*p, event, payload, result) && (result & abi::kEventCancel) && abi::Cancellable(event)) {
                cancelled = true;
                break;
            }
        }
        dispatchDepth--;
        return cancelled;
    }

    bool PluginHost::DispatchCommand(const std::string& plugin, uint64_t player, const std::string& command,
                                     const std::string& args) {
        Plugin* p = Find(plugin);
        if (!p || !p->info.enabled)
            return false;
        const abi::Writer w = abi::host::Encode<abi::Event::Command>({command, args, player});
        uint32_t result = 0;
        return CallPlugin(*p, abi::Event::Command, {w.Data(), w.Size()}, result);
    }

    std::vector<PluginHost::Info> PluginHost::List() const {
        std::vector<Info> out;
        out.reserve(plugins.size());
        for (const auto& p : plugins)
            out.push_back(p->info);
        return out;
    }
}
